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

#define TEE_INVOKE_CMD_ADDR 0xd842430cUL
#define MAX_PAYLOAD_LEN     0x1000UL

static struct cdev tz_tee_cdev;
static dev_t tz_tee_dev;

static noinline uint64_t tee_invoke_command(uint64_t session_handle,
                                           uint64_t cmd_id,
                                           uint64_t param_types,
                                           uint64_t param_attrs,
                                           uint64_t params,
                                           uint64_t return_origin) {
    if (TEE_INVOKE_CMD_ADDR == 0 || (TEE_INVOKE_CMD_ADDR & 3) != 0) {
        pr_err("TZ_TEE: 无效TEE地址 0x%lx\n", (unsigned long)TEE_INVOKE_CMD_ADDR);
        return 0xFFFFFFFFUL;
    }

    register uint64_t x0 asm("x0") = session_handle;
    register uint64_t x1 asm("x1") = cmd_id;
    register uint64_t x2 asm("x2") = param_types;
    register uint64_t x3 asm("x3") = param_attrs;
    register uint64_t x4 asm("x4") = params;
    register uint64_t x5 asm("x5") = return_origin;

    uint64_t ret = 0xFFFFFFFFUL;

    asm volatile(
        "mov x30, #0\n"
        "mov x29, sp\n"
        "blr %[tee_addr]\n"
        "mov sp, x29\n"
        "mov %[ret], x0\n"
        : [ret] "=r"(ret)
        : [tee_addr] "r"(TEE_INVOKE_CMD_ADDR),
          "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17",
          "x19", "x20", "x21", "x22", "x23", "x24", "x25",
          "x26", "x27", "x28", "x29", "x30"
    );

    pr_info("TZ_TEE: TEE调用返回 0x%llx (cmd_id=0x%llx)\n", ret, cmd_id);
    return ret;
}

static ssize_t tz_tee_write(struct file *filp, const char __user *buf,
                           size_t len, loff_t *off) {
    uint64_t cmd_args[7];
    char *kernel_payload = NULL;
    size_t payload_len = 0;

    if (!capable(CAP_SYS_ADMIN)) {
        pr_err("TZ_TEE: 无ROOT权限 (UID=%d)\n", current_uid().val);
        return -EPERM;
    }

    if (len < sizeof(cmd_args) || len > MAX_PAYLOAD_LEN) {
        pr_err("TZ_TEE: 无效长度 %zu (范围: %zu ~ %zu)\n",
               len, sizeof(cmd_args), (size_t)MAX_PAYLOAD_LEN);
        return -EINVAL;
    }

    if (copy_from_user(cmd_args, buf, sizeof(cmd_args))) {
        pr_err("TZ_TEE: 拷贝入参失败\n");
        return -EFAULT;
    }

    payload_len = len - sizeof(cmd_args);
    kernel_payload = kzalloc(payload_len, GFP_KERNEL | GFP_DMA);
    if (!kernel_payload) {
        pr_err("TZ_TEE: 分配内核内存失败\n");
        return -ENOMEM;
    }

    if (copy_from_user(kernel_payload, buf + sizeof(cmd_args), payload_len)) {
        pr_err("TZ_TEE: 拷贝payload失败\n");
        kfree(kernel_payload);
        return -EFAULT;
    }

    pr_info("TZ_TEE: 开始调用TEE函数\n");
    pr_info("TZ_TEE: 会话=0x%llx, 命令ID=0x%llx, Payload长度=0x%zx\n",
            cmd_args[0], cmd_args[1], payload_len);
    pr_info("TZ_TEE: TEE函数地址=0x%lx\n", (unsigned long)TEE_INVOKE_CMD_ADDR);

    tee_invoke_command(
        cmd_args[0],
        cmd_args[1],
        cmd_args[2],
        cmd_args[3],
        (uint64_t)kernel_payload,
        cmd_args[5]
    );

    kfree(kernel_payload);
    return len;
}

static const struct file_operations tz_tee_fops = {
    .owner   = THIS_MODULE,
    .write   = tz_tee_write,
    .llseek  = no_llseek,
    .open    = nonseekable_open,
};

static int __init tz_tee_init(void) {
    int ret = 0;

    if (TEE_INVOKE_CMD_ADDR == 0) {
        pr_err("TZ_TEE: TEE函数地址未配置\n");
        return -EINVAL;
    }

    tz_tee_dev = MKDEV(DEV_MAJOR, DEV_MINOR);
    ret = register_chrdev_region(tz_tee_dev, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("TZ_TEE: 注册设备号失败 (ret=%d)\n", ret);
        return ret;
    }

    cdev_init(&tz_tee_cdev, &tz_tee_fops);
    tz_tee_cdev.owner = THIS_MODULE;
    ret = cdev_add(&tz_tee_cdev, tz_tee_dev, 1);
    if (ret < 0) {
        pr_err("TZ_TEE: 添加设备失败 (ret=%d)\n", ret);
        unregister_chrdev_region(tz_tee_dev, 1);
        return ret;
    }

    pr_info("TZ_TEE: 模块加载成功！TEE地址=0x%lx\n", (unsigned long)TEE_INVOKE_CMD_ADDR);
    pr_info("TZ_TEE: 设备节点: /dev/%s (主设备号=%d, 次设备号=%d)\n",
            DEVICE_NAME, DEV_MAJOR, DEV_MINOR);
    return 0;
}

static void __exit tz_tee_exit(void) {
    cdev_del(&tz_tee_cdev);
    unregister_chrdev_region(tz_tee_dev, 1);
    pr_info("TZ_TEE: 模块卸载成功\n");
}

module_init(tz_tee_init);
module_exit(tz_tee_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OnePlus 13T SM8750 TEE Call Module");
MODULE_VERSION("2.2");
