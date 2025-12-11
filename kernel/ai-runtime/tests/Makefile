# AI Runtime Test Suite Makefile

obj-m += ai-runtime-test.o

ai-runtime-test-objs := test_queue.o

ccflags-y += -I$(src)/..
ccflags-y += -Wall -Werror

# Debug support
ifeq ($(CONFIG_AI_RUNTIME_DEBUG),y)
    ccflags-y += -DDEBUG -g
endif

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

test: all
	@echo "Loading test module..."
	sudo insmod ai-runtime-test.ko
	@echo "Test results:"
	dmesg | tail -20 | grep "AI_TEST"
	@echo "Unloading test module..."
	sudo rmmod ai-runtime-test

.PHONY: all clean test
