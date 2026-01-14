# SPDX-License-Identifier: GPL-2.0
#
# Makefile for oplus_bsp_sched_ext
#

obj-$(CONFIG_OPLUS_BSP_SCHED_EXT) += oplus_bsp_sched_ext.o
ccflags-y += -I$(src) -I$(srctree)

ifdef CONFIG_ARCH_MEDIATEK
    ccflags-y += -DCONFIG_OPLUS_SYSTEM_KERNEL_MTK
endif

ifdef CONFIG_ARCH_QCOM
    ccflags-y += -DCONFIG_OPLUS_SYSTEM_KERNEL_QCOM
endif

sched_ext_srcs := $(wildcard $(src)/*.c)
sched_ext_srcs += $(wildcard $(src)/hmbird_II/*.c)
sched_ext_srcs += $(wildcard $(src)/hmbird_CameraScene/*.c)

oplus_bsp_sched_ext-y := $(patsubst $(src)/%.c, %.o, $(sched_ext_srcs))
