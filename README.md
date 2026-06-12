# no_oddo_kernelfuse

A kernel module that captures Qualcomm SCM SMC function IDs via kprobe on `__arm_smccc_smc` for Android GKI devices.

Captures SMC function calls into a ring buffer and dumps them on module unload. This module only observes SMC traffic. Going beyond observation — intercepting, blocking, or modifying specific calls — is deliberately left as an exercise. Possible directions include redirecting the SMC function ID at the `__arm_smccc_smc` level, hooking `qcom_scm_call`, or intercepting individual SCM API functions such as the anti-rollback update:

https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/commit/730abe20ed39120f3eaa47c2b7683cc9069aa053

## License

GPL-2.0-only
