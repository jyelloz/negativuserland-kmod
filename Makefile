KDIR ?= /lib/modules/$(shell uname -r)/build

ifneq ($(CONFIG_OF),)
obj-m += snd-soc-nul-bbb.o
endif

default: modules
	exit

%::
	$(MAKE) -C $(KDIR) M=$(PWD) $@
