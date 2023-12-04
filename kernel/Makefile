#
# Makefile for the vtunerc device driver
#

VTUNERC_MAX_ADAPTERS ?= 4

vtunerc-objs = vtunerc_main.o vtunerc_ctrldev.o vtunerc_proxyfe.o

CONFIG_DVB_VTUNERC ?= m

obj-$(CONFIG_DVB_VTUNERC) += vtunerc.o

ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends
ccflags-y += -Idrivers/media/tuners
ccflags-y += -Iinclude/media
ccflags-y += -DVTUNERC_MAX_ADAPTERS=$(VTUNERC_MAX_ADAPTERS)

# for external compilation
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

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
