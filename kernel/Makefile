#
# Makefile for the vtunerc device driver
#

VTUNERC_MAX_ADAPTERS ?= 8

vtunerc-objs = vtunerc_main.o vtunerc_ctrldev.o vtunerc_proxyfe.o

CONFIG_DVB_VTUNERC ?= m

obj-$(CONFIG_DVB_VTUNERC) += vtunerc.o

ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends
ccflags-y += -Idrivers/media/tuners
ccflags-y += -Iinclude/media
ccflags-y += -DVTUNERC_MAX_ADAPTERS=$(VTUNERC_MAX_ADAPTERS)

# for external compilation
ifeq ($(shell make -sC /usr/src/linux kernelversion 2> /dev/null),)
       KVER := $(shell uname -r)
else
       KVER := $(shell make -sC /usr/src/linux kernelversion)
endif

KDIR := /lib/modules/$(KVER)
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR)/build M=$(PWD) modules

clean:
	rm -f *.a
	rm -f *.o
	rm -f *.ko
	rm -f *.mod.c
	rm -f *.mod
	rm -f .*.cmd
	rm -f *~
	rm -f *.symvers
	rm -f *.order
	
install:
	cp vtunerc.ko $(KDIR)/kernel/drivers/media/tuners
	depmod -a $(KVER)
