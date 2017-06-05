ifeq ($(KVER),)
ifeq ($(KDIR),)
	KVER = $(shell uname -r)
	KDIR := /lib/modules/$(KVER)/build
endif
else
	KDIR := /lib/modules/$(KVER)/build
endif

export PWD    := $(shell pwd)

MODULE_NAME = lo_jg

ifneq ($(KERNELRELEASE),)
obj-m := lo_jg.o
lo_jg-y := lo_jg.o
else
all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(shell pwd) BUILD_INI=m

clean:
	rm -f *.o *.ko .*.cmd *.mod.c .*.d .depend *~ Modules.symvers \
		Module.symvers Module.markers modules.order
	rm -rf .tmp_versions
endif
