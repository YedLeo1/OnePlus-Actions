#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/capability.h>
#include <asm/barrier.h>
#include <asm/io.h>

#define DEVICE_NAME "tz_tee_call"
#define DEV_MAJOR   240
#define DEV_MINOR   0

#define TEE_INVOKE_CMD_ADDR 0xd842430cUL
#define MAX_PAYLOAD_LEN     0x1000UL

static struct cdev tz_tee_cdev;
static dev_t tz_tee_dev;

typedef uint64_t (*tee_call_func_t)(
    uint64_t session,
    uint64_t cmd_id,
    uint64_t param_types,
    uint64_t param_attrs,
    uint64_t params_ptr,
    uint64_t return_origin
);

static uint64_t tee_invoke_command(
    uint64_t session,
    uint64_t cmd_id,
    uint64_t param_types,
    uint64_t param_attrs,
    uint64_t params_ptr,
    uint64_t return_origin
) {
    // 一加/高通这类地址已经在内核线性映射里，直接用，不要 ioremap！
    tee_call_func_t func = (tee_call_func_t)TEE_INVOKE_CMD_ADDR;
    uint64_t ret;

    // 保证指令同步
    isb();
    ret = func(session, cmd_id, param_types, param_attrs, params_ptr, return_origin);

    pr_info("TZ_TEE: invoke return 0x%llx\n", ret);
    return ret;
}

static ssize_t tz_tee_write(struct file *filp, const char __user *buf,
                           size_t len, loff_t *off) {
    uint64_t cmd_args[6];  // 真正只需要 6 个参数

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    if (len < sizeof(cmd_args))
        return -EINVAL;

    if (copy_from_user(cmd_args, buf, sizeof(cmd_args)))
        return -EFAULT;

    // 直接调用，不再多分配无用 payload
    tee_invoke_command(
        cmd_args[0],   // x0: session
        cmd_args[1],   // x1: cmd_id
        cmd_args[2],   // x2: param_types
        cmd_args[3],   // x3: param_attrs
        cmd_args[4],   // x4: params_ptr
        cmd_args[5]    // x5: return_origin
    );

    return len;
}

static const struct file_operations tz_tee_fops = {
    .owner   = THIS_MODULE,
    .write   = tz_tee_write,
    .llseek  = no_llseek,
    .open    = nonseekable_open,
};

static int __init tz_tee_init(void) {
    int ret;

    tz_tee_dev = MKDEV(DEV_MAJOR, DEV_MINOR);
    ret = register_chrdev_region(tz_tee_dev, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&tz_tee_cdev, &tz_tee_fops);
    ret = cdev_add(&tz_tee_cdev, tz_tee_dev, 1);
    if (ret < 0) {
        unregister_chrdev_region(tz_tee_dev, 1);
        return ret;
    }

    pr_info("TZ_TEE: module loaded\n");
    return 0;
}

static void __exit tz_tee_exit(void) {
    cdev_del(&tz_tee_cdev);
    unregister_chrdev_region(tz_tee_dev, 1);
    pr_info("TZ_TEE: module unloaded\n");
}

module_init(tz_tee_init);
module_exit(tz_tee_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SM8750 TEE direct call module");
