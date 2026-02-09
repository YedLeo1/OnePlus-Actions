#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/capability.h>

#define DEVICE_NAME "tz_tee_call"
#define DEV_MAJOR   240
#define DEV_MINOR   0

#define TEE_INVOKE_CMD_ADDR 0xd842430cUL
#define MAX_PAYLOAD_LEN     0x1000UL

static struct cdev tz_tee_cdev;
static dev_t tz_tee_dev;

typedef uint64_t (*tee_call_func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t tee_invoke_command(uint64_t session_handle,
                                   uint64_t cmd_id,
                                   uint64_t param_types,
                                   uint64_t param_attrs,
                                   uint64_t params,
                                   uint64_t return_origin)
{
    if (TEE_INVOKE_CMD_ADDR == 0 || (TEE_INVOKE_CMD_ADDR & 3) != 0) {
        pr_err("TZ_TEE: 无效TEE地址 0x%lx\n", (unsigned long)TEE_INVOKE_CMD_ADDR);
        return 0xFFFFFFFFUL;
    }

    tee_call_func_t func = (tee_call_func_t)TEE_INVOKE_CMD_ADDR;
    uint64_t ret = func(session_handle, cmd_id, param_types, param_attrs, params, return_origin);

    pr_info("TZ_TEE: TEE调用返回 0x%llx (cmd_id=0x%llx)\n", ret, cmd_id);
    return ret;
}

static ssize_t tz_tee_write(struct file *filp, const char __user *buf,
                           size_t len, loff_t *off)
{
    uint64_t cmd_args[7];
    char *kernel_payload = NULL;
    size_t payload_len;

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    if (len < sizeof(cmd_args) || len > MAX_PAYLOAD_LEN) {
        pr_err("TZ_TEE: 无效长度 %zu (范围: %zu ~ %zu)\n",
               len, sizeof(cmd_args), (size_t)MAX_PAYLOAD_LEN);
        return -EINVAL;
    }

    if (copy_from_user(cmd_args, buf, sizeof(cmd_args)))
        return -EFAULT;

    payload_len = len - sizeof(cmd_args);
    kernel_payload = kzalloc(payload_len, GFP_KERNEL);
    if (!kernel_payload)
        return -ENOMEM;

    if (copy_from_user(kernel_payload, buf + sizeof(cmd_args), payload_len)) {
        kfree(kernel_payload);
        return -EFAULT;
    }

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
    .owner = THIS_MODULE,
    .write = tz_tee_write,
    .llseek = no_llseek,
    .open = nonseekable_open,
};

static int __init tz_tee_init(void)
{
    int ret;

    tz_tee_dev = MKDEV(DEV_MAJOR, DEV_MINOR);
    ret = register_chrdev_region(tz_tee_dev, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&tz_tee_cdev, &tz_tee_fops);
    tz_tee_cdev.owner = THIS_MODULE;
    ret = cdev_add(&tz_tee_cdev, tz_tee_dev, 1);
    if (ret < 0) {
        unregister_chrdev_region(tz_tee_dev, 1);
        return ret;
    }

    pr_info("TZ_TEE: 模块加载成功！地址=0x%lx\n", (unsigned long)TEE_INVOKE_CMD_ADDR);
    return 0;
}

static void __exit tz_tee_exit(void)
{
    cdev_del(&tz_tee_cdev);
    unregister_chrdev_region(tz_tee_dev, 1);
    pr_info("TZ_TEE: 模块卸载\n");
}

module_init(tz_tee_init);
module_exit(tz_tee_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TZ TEE Call Module");
