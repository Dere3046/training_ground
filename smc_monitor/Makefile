obj-m := smc_monitor.o

VER_FLAG = $(if $(VERSION),-DVER=$(VERSION),)
CFLAGS_smc_monitor.o += $(VER_FLAG)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules \
		CFLAGS_MODULE="$(CFLAGS_smc_monitor.o)"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
