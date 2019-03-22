
obj-m		+= test_queue.o
test_queue-objs := main.o fileops.o
KERN_SRC	:= /lib/modules/$(shell uname -r)/build/
PWD			:= $(shell pwd)

all: 
	gcc test_queued.c -o test_queued
	make -C $(KERN_SRC) M=$(PWD) modules
	mkdir -p dist
	cp test_queued ./dist/
	cp test_queue.ko ./dist/
	cp write_mess.sh ./dist/
	cp install_module.sh ./dist/
	
modules:
	make -C $(KERN_SRC) M=$(PWD) modules

install:
	make -C $(KERN_SRC) M=$(PWD) modules_install
	depmod -a

clean:
	make -C $(KERN_SRC) M=$(PWD) clean
	rm -rf test_queued
	rm -rf dist
