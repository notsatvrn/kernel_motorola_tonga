#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

################################################################################
# COMMON PART
# 1. xxx.h/c: Source files that is common for all chips
# 2. IPvx/: Common part for a Chipset Groups
#
# When one new chip coming, you can select an existing common part, ex. IPv1 or
# you can create a new group, ex. IPv2
################################################################################

obj-y += pwm10/
obj-y += corr10/
obj-y += color20/
obj-y += aal30/
obj-y += layering_rule_base/

ccflags-y += \
				-I$(srctree)/drivers/misc/mediatek/video/include        \
				-I$(srctree)/drivers/misc/mediatek/video/mt6765/dispsys \
				-I$(srctree)/drivers/misc/mediatek/video/mt6765/videox  \
				-I$(srctree)/drivers/misc/mediatek/sync/

ccflags-y += -I$(srctree)/drivers/misc/mediatek/cmdq/v3 \
             -I$(srctree)/drivers/misc/mediatek/cmdq/v3/mt6765

ifneq ($(CONFIG_MTK_FB), y)
obj-y += mtkfb_dummy.o
endif

subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/mmp/

subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/m4u/mt6765

ccflags-$(CONFIG_MTK_LCM) += -I$(srctree)/drivers/misc/mediatek/lcm/inc

ifeq ($(CONFIG_MTK_ION),y)
subdir-ccflags-y += -I$(srctree)/drivers/staging/android/mtk_ion
ccflags-y += -I$(srctree)/drivers/staging/android/mtk_ion \
             -I$(srctree)/drivers/staging/android/mtk_ion/mtk
else
subdir-ccflags-y += -I$(srctree)/drivers/staging/android/aosp_ion
ccflags-y += -I$(srctree)/drivers/staging/android/aosp_ion \
             -I$(srctree)/drivers/staging/android/aosp_ion/mtk
endif


ifeq ($(CONFIG_MTK_INTERNAL_HDMI_SUPPORT), y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hdmi/
endif
ifeq ($(CONFIG_MTK_INTERNAL_MHL_SUPPORT), y)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/mhltx/
endif
ifeq ($(CONFIG_MTK_DITHERING_SUPPORT), y)
ccflags-y += -DDITHERING_SUPPORT
endif
