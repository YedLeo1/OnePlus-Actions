obj-m += tz_tee_call.o
ccflags-y += -Wno-declaration-after-statement
ccflags-y += -Wno-unused-function
ccflags-y += -falign-functions=64
ccflags-y += -Wframe-larger-than=1024
