obj-m += tu102_execute.o tu102_diag.o
tu102_execute-y := tu102_execute_main.o tu102_dma.o
EXEC_PROFILE ?= historical
ifeq ($(EXEC_PROFILE),nvidia-1e07-12a4-trial)
ifneq ($(ENABLE_EXPERIMENT),1)
$(error The target trial requires ENABLE_EXPERIMENT=1)
endif
target_trial_cflags := -DTX_PROFILE_NVIDIA_1E07_12A4 -DKOH_TARGET_NVIDIA_TRIAL
CFLAGS_tu102_execute_main.o += $(target_trial_cflags)
CFLAGS_tu102_dma.o += $(target_trial_cflags)
else ifneq ($(EXEC_PROFILE),historical)
$(error Unknown EXEC_PROFILE; use historical or nvidia-1e07-12a4-trial)
endif
DIAG_PROFILE ?= historical
ifeq ($(DIAG_PROFILE),nvidia-1e07-12a4)
CFLAGS_tu102_diag.o += -DKOH_DIAG_NVIDIA_1E07_12A4
else ifneq ($(DIAG_PROFILE),historical)
$(error Unknown DIAG_PROFILE; use historical or nvidia-1e07-12a4)
endif
# Detect the PCI field rather than a release number: distributions can backport
# the move to struct device independently of the upstream kernel version.
ifneq ($(KERNELRELEASE),)
ifeq ($(shell grep -Ec 'const char[[:space:]]*\*[[:space:]]*driver_override;' $(srctree)/include/linux/pci.h),0)
ccflags-y += -DKOH_DEVICE_DRIVER_OVERRIDE
endif
endif
ifeq ($(ENABLE_EXPERIMENT),1)
ccflags-y += -DKOH_UNLOCK_REVIEWED_PROFILE
endif
KDIR ?= /lib/modules/$(shell uname -r)/build
.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
