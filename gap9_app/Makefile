# Copyright (C) 2017 GreenWaves Technologies
# All rights reserved.

# This software may be modified and distributed under the terms
# of the BSD license.  See the LICENSE file for details.

ifndef GAP_SDK_HOME
  $(error Source sourceme in gap_sdk first)
endif

include common.mk
include $(RULES_DIR)/at_common_decl.mk

io?=host

$(info Building NNTOOL model)
NNTOOL_EXTRA_FLAGS ?=

include common/model_decl.mk
IMAGE=$(CURDIR)/images/coco_test_image_gray_320_240.ppm

# pulpChip = GAP
# PULP_APP = $(MODEL_PREFIX)

APP = $(MODEL_PREFIX)
APP_SRCS += $(MODEL_PREFIX).c $(MODEL_GEN_C) $(MODEL_EXPRESSIONS) $(MODEL_COMMON_SRCS) $(CNN_LIB)

APP_CFLAGS += -g -O3 -mno-memcpy -fno-tree-loop-distribute-patterns
APP_CFLAGS += -I. -I$(AT_MODEL_DIR) -I$(MODEL_COMMON_INC) -I$(TILER_EMU_INC) -I$(TILER_INC) $(CNN_LIB_INCLUDE) -I$(MODEL_BUILD)
APP_CFLAGS += -DPERF -DAT_MODEL_PREFIX=$(MODEL_PREFIX) $(MODEL_SIZE_CFLAGS)
APP_CFLAGS += -DSTACK_SIZE=$(CLUSTER_STACK_SIZE) -DSLAVE_STACK_SIZE=$(CLUSTER_SLAVE_STACK_SIZE)
APP_CFLAGS += -DAT_IMAGE=$(IMAGE) -DFREQ_FC=$(FREQ_FC) -DFREQ_CL=$(FREQ_CL) -DFREQ_PE=$(FREQ_PE)

CONFIG_ILI9341       = 1
CONFIG_DISPLAY       = 1

USE_CAMERA?=1
ifeq ($(USE_CAMERA), 0)
  APP_CFLAGS += -DFROM_FILE
endif

DISPLAY?=1
ifeq ($(DISPLAY), 1)
  APP_CFLAGS += -DDISPLAY
endif

READFS_FILES=$(abspath $(MODEL_TENSORS))

USE_PRIVILEGED_FLASH_MEMORY?=1
ifeq ($(USE_PRIVILEGED_FLASH_MEMORY), 0)
PRIVILEGED_FLASH_FLAG=  
else
runner_args += --flash-property=$(MODEL_SEC_TENSORS)@mram:readfs:files
PRIVILEGED_FLASH_FLAG=--use_privileged_flash_device
endif

# build depends on the model
build:: model

clean:: clean_model

include common/model_rules.mk
$(info APP_SRCS... $(APP_SRCS))
$(info APP_CFLAGS... $(APP_CFLAGS))
include $(RULES_DIR)/pmsis_rules.mk

