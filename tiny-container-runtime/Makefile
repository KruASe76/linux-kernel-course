CC = gcc
CFLAGS = -static -Wall -Wextra -O2


obj-m += tiny_monitor.o

KDIR ?= /lib/modules/$(shell uname -r)/build


all: tiny module

tiny: tiny.c
	$(CC) $(CFLAGS) -o tiny tiny.c

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -f tiny
	$(MAKE) -C $(KDIR) M=$(PWD) clean
