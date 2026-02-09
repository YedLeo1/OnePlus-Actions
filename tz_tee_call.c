#include <linux/module.h>
#include <linux/init.h>

#define TEE_INVOKE_CMD_ADDR 0xd842430cUL

typedef uint64_t (*tee_call_func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static int __init tz_tee_init(void)
{
    printk("TZ_TEE: module loaded, TEE addr = 0x%lx\n", (unsigned long)TEE_INVOKE_CMD_ADDR);
    return 0;
}

static void __exit tz_tee_exit(void)
{
    printk("TZ_TEE: module unloaded\n");
}

module_init(tz_tee_init);
module_exit(tz_tee_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TZ TEE CALL");
