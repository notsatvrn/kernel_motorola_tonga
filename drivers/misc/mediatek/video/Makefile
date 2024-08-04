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
subdir-ccflags-y += -Wno-error
################################################################################
# FrameBuffer Driver for Display
#
#
################################################################################

subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include

ifneq ($(CONFIG_MTK_LCM), y)
	obj-y += mtdummy/
else
	obj-$(CONFIG_MTK_FB) += mt6765/
	obj-y += common/
endif
