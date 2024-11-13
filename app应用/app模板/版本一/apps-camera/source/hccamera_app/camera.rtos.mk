HCCAMERA_DIR_NAME ?= hccamera_app

CSRCS += $(wildcard $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/*.c)
CSRCS += $(shell find -L $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/channel -name "*.c")
CSRCS += $(wildcard $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/public/*.c)
CSRCS += $(wildcard $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/camera/*.c)
# CSRCS += $(shell find -L $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/setup -name "*.c")
# CSRCS += $(wildcard $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/volume/*.c)

#ifeq ($(CONFIG_APPS_CAMERA_BATTERY_MONITOR),y)
#	CSRCS += $(wildcard $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/battery_monitor/*.c)
#endif

#ifeq ($(CONFIG_APPS_CAMERA_LVGL_RESOLUTION_240P),y)
#	CSRCS += $(shell find -L $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/ui_rsc/320x240 -name "*.c")
#else
#	CSRCS += $(shell find -L $(CAMERA_DIR)/$(HCCAMERA_DIR_NAME)/ui_rsc/1280x720 -name "*.c")
#endif


CFLAGS += -I$(CAMERA_DIR)

