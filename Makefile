obj-m   += math.o

# Standard path for kernel headers.
# If it doesn't work, check whether your kernel headers are installed
# or add KDIR=<path to kernel headers> to "make" arguments
KDIR    = /lib/modules/$(shell uname -r)/build
PWD     = $(shell pwd)

default: math_ctl math

math:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

math_ctl: math_ctl.c math.h
	$(CC) -Wall -Warray-bounds math_ctl.c -o math_ctl

math-2014.tar.gz: math_ctl.c math.h math_sample.c
	rm -f math-2014.tar.gz
	tar hczf math-2014.tar.gz math

insert_module:
	rmmod math || :
	insmod ./math.ko
	@echo "Module loaded.  Now you should create a device node with" \
		"\"mknod /dev/math c `grep math /proc/devices | cut -d' ' -f1` 0\" as root."
	@echo "\"c\" means character device, the numbers mean major and minor numbers of your device."

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
	rm math_ctl
