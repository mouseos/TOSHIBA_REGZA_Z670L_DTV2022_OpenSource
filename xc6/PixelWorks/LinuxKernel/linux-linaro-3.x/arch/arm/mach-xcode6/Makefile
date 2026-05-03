#
# Makefile for the linux kernel.
#

# Common support
obj-y := io.o timer-gp.o prcm.o

# SMP support
obj-$(CONFIG_SMP)			+= xcode6-smp.o xcode6-headsmp.o
obj-$(CONFIG_LOCAL_TIMERS)		+= timer-mpu.o
obj-$(CONFIG_ARCH_XCODE6)		+= xcode6-smc.o xcode6-common.o xcode6-dmab.o
obj-$(CONFIG_INCLUDE_JUMP_VECTOR_AT_START) += xcode6-vinit.o

plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_xcode6-headsmp.o			:=-Wa,-march=armv7-a$(plus_sec)
AFLAGS_xcode6-smc.o			:=-Wa,-march=armv7-a$(plus_sec)
AFLAGS_xcode6-vinit.o := -DTEXT_OFFSET=$(TEXT_OFFSET)

# Specific board support
obj-$(CONFIG_MACH_XCODE6_SDK)		+= board-xcode6sdk.o 
obj-$(CONFIG_PCI)					+= pci.o pci-ops.o
