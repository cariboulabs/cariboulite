obj-m+=bcm2835_smi.o
obj-m+=smi_stream_dev.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

put:
	rmmod smi_stream_dev 2>/dev/null; true
	rmmod bcm2835_smi_dev 2>/dev/null; true
	rmmod bcm2835_smi 2>/dev/null; true
	insmod ./bcm2835_smi.ko
	insmod ./smi_stream_dev.ko

modules_install: all
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install
	$(DEPMOD)
