# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y := -Werror $(call cc-disable-warning, unused-but-set-variable)

# scp smartpa related code
obj-$(CONFIG_MTK_AUDIO_SCP_SPKPROTECT_SUPPORT) += scp_spk/
obj-$(CONFIG_MTK_AUDIO_SCP_SPKPROTECT_SUPPORT) += utility/

# scp vow bargein related code
obj-$(CONFIG_MTK_VOW_SUPPORT) += scp_vow/

#scp ultra sound related code
obj-$(CONFIG_MTK_ULTRASND_PROXIMITY) += scp_ultra/

# dsp related code
ifeq ($(CONFIG_SND_SOC_MTK_AUDIO_DSP),y)
obj-y += audio_dsp/v2/
obj-y += common_int/
endif
#obj-$(CONFIG_MTK_VOW_BARGE_IN_SUPPORT) += scp_vow/
# btcvsd
ifeq ($(CONFIG_SND_SOC_MT6779_MT6359),m)
subdir-ccflags-y += -DASOC_TEMP_BYPASS
endif
obj-$(CONFIG_SND_SOC_MTK_BTCVSD) += common/
obj-$(CONFIG_SND_SOC_MEDIATEK) += common/
obj-y += mt6765/

# mtk new arch
ifeq ($(CONFIG_MTK_SND_SOC_NEW_ARCH),)
# MTK Codec Chip Support
MTK_CODEC := $(subst $(quote),,$(CONFIG_MTK_SND_SOC_CODEC))

# PMIC include path
ifneq  ($(MTK_CODEC),)
subdir-ccflags-y += -Icodec/$(MTK_CODEC)/
subdir-ccflags-y += -I$(src)/common_int

# Update pmic built flow to avoid codec init later than codec probe
obj-y += codec/$(MTK_CODEC)/
obj-y += common_int/

MTK_PLATFORM := $(subst $(quote),,$(CONFIG_MTK_PLATFORM))
# flags for not ready feature, uncomment after ready
subdir-ccflags-y += -D_GIT318_PMIC_READY
subdir-ccflags-y += -DAUDIO_USING_WRAP_DRIVER
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/mt6765/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/
endif
endif

# include path
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat

# adsp
ifeq ($(CONFIG_MTK_AURISYS_PHONE_CALL_SUPPORT),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/adsp/mt6765/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/audio_ipi/common_v2/framework/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/audio_ipi/common_v2/tasks/call/
subdir-ccflags-y += -I$(srctree)/sound/soc/mediatek/audio_dsp
endif

ifeq ($(CONFIG_SND_SOC_MTK_AUDIO_DSP),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/audio_ipi/common_v2/tasks/audplayback/
endif

ifeq ($(CONFIG_SND_SOC_MTK_SMART_PHONE),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
endif

# New architecture
ifeq ($(CONFIG_MTK_SND_SOC_NEW_ARCH),y)

# MTK Platform Support
MTK_PLATFORM := $(subst $(quote),,$(CONFIG_MTK_PLATFORM))

# MTK Codec Chip Support
MTK_CODEC := $(subst $(quote),,$(CONFIG_MTK_SND_SOC_CODEC))

# flags for not ready feature, uncomment after ready
subdir-ccflags-y += -DAUDIO_USING_WRAP_DRIVER
subdir-ccflags-y += -D_MT_IDLE_HEADER
subdir-ccflags-y += -D_REG_SYNC_WRITE

# include path
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/accdet/mt6358
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/mt6765/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/mt6765
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/dramc/mt6765/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/scp/mt6765/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/audio_ipi/mt6765/

# Common include path
subdir-ccflags-y += -I$(src)/common_int

# PMIC include path
ifneq ($(MTK_CODEC),)
subdir-ccflags-y += -I$(src)/codec/$(MTK_CODEC)
endif

subdir-ccflags-y += -Wno-incompatible-pointer-types

# Update pmic built flow to avoid codec init later than codec probe
ifneq ($(MTK_CODEC),)
obj-y += codec/$(MTK_CODEC)/
endif
obj-y += common_int/

obj-y += mt6765/
# AP include path
subdir-ccflags-y += -I$(src)/mt6765

endif
