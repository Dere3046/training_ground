// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/moduleparam.h>

typedef unsigned long (*kln_fn)(const char *name);
typedef int (*reg_kp_fn)(struct kprobe *);
typedef void (*unreg_kp_fn)(struct kprobe *);

static kln_fn my_kallsyms_lookup_name;
static reg_kp_fn my_register_kprobe;
static unreg_kp_fn my_unregister_kprobe;

static unsigned long kern_base;

enum {
    SMC_UPDATE_ROLLBACK = 0x4200011e,
};

static kln_fn get_kallsyms_by_kprobe(void)
{
    struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
    int ret = register_kprobe(&kp);
    if (ret)
        return NULL;
    kln_fn fn = (kln_fn)kp.addr;
    unregister_kprobe(&kp);
    return fn;
}

static void *sprint_lookup_gap_safe(const char *target)
{
    char buf[256];
    unsigned long addr = (unsigned long)&sprintf;

    for (int i = 0; i < 100000; i++) {
        sprintf(buf, "%pSb", (void *)addr);
        if (strstr(buf, target))
            return (void *)addr;

        char *p = strrchr(buf, '+');
        if (p) {
            unsigned long off;
            sscanf(p + 1, "%lx/%*lx", &off);
            addr = (addr - off) - 1;
        } else {
            addr -= 4;
        }
        if (addr < kern_base)
            break;
    }
    return NULL;
}

static int resolve_kallsyms(void)
{
    my_kallsyms_lookup_name = get_kallsyms_by_kprobe();
    if (!my_kallsyms_lookup_name) {
        pr_warn("smc_monitor: kprobe failed, falling back to %%pSb\n");
        my_kallsyms_lookup_name = sprint_lookup_gap_safe(
            "kallsyms_lookup_name");
    }
    if (!my_kallsyms_lookup_name) {
        pr_err("smc_monitor: kallsyms_lookup_name not found\n");
        return -ENOENT;
    }
    pr_info("smc_monitor: kallsyms_lookup_name at %px\n",
            my_kallsyms_lookup_name);

    my_register_kprobe =
        (reg_kp_fn)my_kallsyms_lookup_name("register_kprobe");
    if (!my_register_kprobe) {
        pr_err("smc_monitor: register_kprobe not found\n");
        return -ENOENT;
    }

    my_unregister_kprobe =
        (unreg_kp_fn)my_kallsyms_lookup_name("unregister_kprobe");
    if (!my_unregister_kprobe) {
        pr_err("smc_monitor: unregister_kprobe not found\n");
        return -ENOENT;
    }

    return 0;
}

static atomic_t smc_count = ATOMIC_INIT(0);
static atomic_t intercepted_count = ATOMIC_INIT(0);
#define BUF_SIZE 256
static u64 smc_buf[BUF_SIZE];
static struct kprobe smc_kp;

static int pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    u64 fn = regs->regs[0];
    int idx = (unsigned int)atomic_fetch_add_unless(&smc_count, 1, INT_MAX);
    if (idx < BUF_SIZE)
        smc_buf[idx] = fn;

    if (fn == SMC_UPDATE_ROLLBACK) {
        atomic_inc(&intercepted_count);
        pr_info("smc_monitor: INTERCEPT rollback update fn=0x%llx\n", fn);
        // to block: regs->regs[0] = 0; regs->pc = regs->compat_lr; return 1;
    }

    return 0;
}

#ifdef SMC_DEBUG
static void dump_fns(void)
{
    int total = atomic_read(&smc_count);
    int n = total < BUF_SIZE ? total : BUF_SIZE;
    if (n == 0)
        return;

    for (int i = 0; i < n; i++) {
        u64 f = smc_buf[i];
        pr_info("smc_monitor: [%d] fn=0x%llx owner=%d svc=0x%lx cmd=0x%lx"
                " type=%s conv=%s",
                i, f,
                (u32)((f >> 24) & 0x3F),
                (unsigned long)((f >> 8) & 0xFF),
                (unsigned long)(f & 0xFF),
                ((f >> 31) & 1) ? "FAST" : "STD",
                ((f >> 30) & 1) ? "64" : "32");
    }
}
#endif

static int __init smc_monitor_init(void)
{
    kern_base = (unsigned long)&sprintf & 0xffffffffff000000ull;

    int ret = resolve_kallsyms();
    if (ret)
        return ret;

    unsigned long fn = my_kallsyms_lookup_name("__arm_smccc_smc");
    if (!fn) {
        pr_err("smc_monitor: __arm_smccc_smc not found\n");
        return -ENOENT;
    }

    smc_kp.symbol_name = "__arm_smccc_smc";
    smc_kp.pre_handler = pre_handler;

    ret = my_register_kprobe(&smc_kp);
    if (ret) {
        pr_err("smc_monitor: register_kprobe failed: %d\n", ret);
        return ret;
    }

#ifdef VER
    pr_info("smc_monitor: kprobe on __arm_smccc_smc r%d\n", VER);
#else
    pr_info("smc_monitor: kprobe on __arm_smccc_smc\n");
#endif
    return 0;
}

static void __exit smc_monitor_exit(void)
{
    if (my_unregister_kprobe && smc_kp.addr)
        my_unregister_kprobe(&smc_kp);

    int total = atomic_read(&smc_count);
    int intercepted = atomic_read(&intercepted_count);
    pr_info("smc_monitor: %d SMC calls, %d intercepted\n",
            total, intercepted);

#ifdef SMC_DEBUG
    dump_fns();
#else
    int n = total < BUF_SIZE ? total : BUF_SIZE;
    for (int i = 0; i < n; i++) {
        u64 f = smc_buf[i];
        if (((f >> 24) & 0x3F) != 2)
            continue;
        pr_info("smc_monitor: [%d] fn=0x%llx type=%s conv=%s"
                " owner=SIP svc=0x%lx cmd=0x%lx",
                i, f,
                ((f >> 31) & 1) ? "FAST" : "STD",
                ((f >> 30) & 1) ? "64" : "32",
                (unsigned long)((f >> 8) & 0xFF),
                (unsigned long)(f & 0xFF));
    }
#endif
}

module_init(smc_monitor_init);
module_exit(smc_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SMC call monitor + interception");
