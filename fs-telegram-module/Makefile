obj-m += fs_telegram.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: module client

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

client: telegram_client.c
	gcc --static -o telegram_client telegram_client.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f telegram_client
