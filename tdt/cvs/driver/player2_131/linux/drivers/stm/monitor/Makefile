CCFLAGSY += -I$(TREE_ROOT)/linux/include/linux/stm/

CONFIG_MONITOR_HAVANA=m

CCFLAGSY += -DDEBUG_MONITOR

ifneq (,$(findstring 2.6.3,$(KERNELVERSION)))
	ccflags-y += $(CCFLAGSY)
else
	CFLAGS += $(CCFLAGSY)
endif

obj-$(CONFIG_MONITOR_HAVANA)+= stm_monitor.o

stm_monitor-objs:=      monitor_module.o \
			monitor_device.o \
			monitor_mme.o


