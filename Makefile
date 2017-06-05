KVER = $(shell uname -r)
KDIR := /lib/modules/$(KVER)/build

obj-m = lo_jg.o
all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(shell pwd) BUILD_INI=m

clean:
	rm -f *.o *.ko .*.cmd *.mod.c .*.d .depend *~ Modules.symvers \
		Module.symvers Module.markers modules.order
	rm -rf .tmp_versions
