# no_oddo_kernelfuse

Kernel module for monitoring Qualcomm SCM SMC calls on Android GKI,
with init_boot injection tooling.

## Components

smc_monitor.ko   kprobe on __arm_smccc_smc, captures function IDs
smcpatch         inject .ko into init_boot ramdisk
init-wrapper     chain-loader, loads module before real init
loader           userspace CLI: insmod / rmmod / status

## Build

CI via DDK container. Artifacts: ko, loader, init-wrapper, smcpatch.

## Rollback

The anti-rollback SMC (QCOM_SCM_SVC_BOOT, cmd=0x1E) is function_id
0x4200011e. The module detects and logs this call.

Blocking is left as an exercise — the source contains a commented
one-liner showing how to skip the SMC at __arm_smccc_smc. Other
approaches include hooking qcom_scm_call or intercepting individual
SCM API functions.

Source: https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/commit/730abe20ed39120f3eaa47c2b7683cc9069aa053

## License

GPL-2.0-only
