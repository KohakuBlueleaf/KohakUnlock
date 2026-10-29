obj-m += tu102_execute.o tu102_diag.o
tu102_execute-y := tu102_execute_main.o tu102_dma.o
ifeq ($(ENABLE_EXPERIMENT),1)
ccflags-y += -DKOH_UNLOCK_REVIEWED_PROFILE
endif
KDIR ?= /lib/modules/$(shell uname -r)/build
.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
