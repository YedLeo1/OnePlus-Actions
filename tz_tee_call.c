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
)
{
    pr_info("=== TZ_TEE: 调用成功 ===");

    // 直接调用，不加 ioremap
    tee_call_func_t func = (tee_call_func_t)TEE_INVOKE_CMD_ADDR;
    isb();
    uint64_t ret = func(session, cmd_id, param_types, param_attrs, params_ptr, return_origin);

    pr_info("TZ_TEE: return 0x%016llx", ret);
    return ret;
}

static ssize_t tz_tee_write(struct file *file, const char __user *buf,
                            size_t count, loff_t *ppos)
{
    uint64_t args[6];

    pr_info("TZ_TEE: write 触发，长度=%zu", count);

    if (count < sizeof(args))
        return -EINVAL;

    if (copy_from_user(args, buf, sizeof(args)))
        return -EFAULT;

    tee_invoke_command(
        args[0],
        args[1],
        args[2],
        args[3],
        args[4],
        args[5]
    );

    return count;
}

// 这是关键！老内核必须用 .llseek 而不是 llseek 简写
static const struct file_operations tz_tee_fops = {
    .owner = THIS_MODULE,
    .write = tz_tee_write,
    .open = nonseekable_open,
    .llseek = no_llseek,
};

static int __init tz_tee_init(void)
{
    int ret;

    pr_info("TZ_TEE: 模块加载成功！");

    tz_tee_dev = MKDEV(DEV_MAJOR, DEV_MINOR);
    ret = register_chrdev_region(tz_tee_dev, 1, DEVICE_NAME);
    if (ret) {
        pr_err("register_chrdev_region failed");
        return ret;
    }

    cdev_init(&tz_tee_cdev, &tz_tee_fops);
    tz_tee_cdev.owner = THIS_MODULE;
    ret = cdev_add(&tz_tee_cdev, tz_tee_dev, 1);
    if (ret) {
        pr_err("cdev_add failed");
        unregister_chrdev_region(tz_tee_dev, 1);
        return ret;
    }

    return 0;
}

static void __exit tz_tee_exit(void)
{
    cdev_del(&tz_tee_cdev);
    unregister_chrdev_region(tz_tee_dev, 1);
    pr_info("TZ_TEE: 卸载");
}

module_init(tz_tee_init);
module_exit(tz_tee_exit);
MODULE_LICENSE("GPL");
