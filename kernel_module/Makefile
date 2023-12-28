	
obj-m += phymem_fragmenter.o 
DEBUG_CFLAGS += -g
ccflags-y += ${DBUG_CFLAGS}
CC += ${DEBUG_CFLAGS}

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

debug:
	$(MAKE) -C $(KDIR) M=$(PWD) modules EXTRA_CFLAGS="$(DEBUG_CFLAGS)"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
