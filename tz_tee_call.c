#include <linux/module.h>

static int __init tz_init(void) {
    pr_info("tz_tee_call: loaded\n");
    return 0;
}

static void __exit tz_exit(void) {
    pr_info("tz_tee_call: unloaded\n");
}

module_init(tz_init);
module_exit(tz_exit);
MODULE_LICENSE("GPL");
