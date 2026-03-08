obj-m := input_helper.o
input_helper-objs := kmd.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	sudo insmod input_helper.ko

uninstall:
	sudo rmmod input_helper

.PHONY: all clean install uninstall
