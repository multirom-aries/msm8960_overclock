KERNEL_BUILD := ~/android_kernel_htc_msm8960/
KERNEL_CROSS_COMPILE := /home/jmz/cm/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin/arm-eabi-

obj-m += krait_oc.o

all:
	make -C $(KERNEL_BUILD) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) M=$(PWD) modules
	$(KERNEL_CROSS_COMPILE)strip --strip-debug krait_oc.ko

clean:
	make -C $(KERNEL_BUILD) M=$(PWD) clean 2> /dev/null
	rm -f modules.order *~
