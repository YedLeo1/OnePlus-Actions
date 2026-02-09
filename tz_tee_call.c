#include <linux/module.h>

static int __init tz_init(void) { return 0; }
static void __exit tz_exit(void) {}

module_init(tz_init);
module_exit(tz_exit);
MODULE_LICENSE("GPL");
