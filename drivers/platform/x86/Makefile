
obj-m := thinkpad-wmi.o

KVER  ?= $(shell uname -r)

KDIR ?= /lib/modules/$(KVER)/build
MDIR ?= /lib/modules/$(KVER)/drivers/platform/x86

PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $(obj-m:.o=.ko)

install:
	install -d $(MDIR)
	install -m 644 -c $(obj-m:.o=.ko) $(MDIR)
	depmod -a

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $(obj-m:.o=.ko) clean
	rm -rf *.mod.c *.o *.ko .tmp_versions Module.symvers .$(obj-m:.o=)*
	rm -rf linux/.*o.cmd linux/*.o
