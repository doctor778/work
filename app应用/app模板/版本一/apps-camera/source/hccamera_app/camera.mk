HCCAMERA_DIR_NAME ?= hccamera_app
HCCAMERA_SRC += $(wildcard $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/*.c)
HCCAMERA_SRC += $(shell find -L $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/channel -name "*.c")
HCCAMERA_SRC += $(wildcard $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/public/*.c)
HCCAMERA_SRC += $(wildcard $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/camera/*.c)
# HCCAMERA_SRC += $(shell find -L $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/setup -name "*.c")
# HCCAMERA_SRC += $(wildcard $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/volume/*.c)

#ifeq ($(CONFIG_APPS_CAMERA_BATTERY_MONITOR),CONFIG_APPS_CAMERA_BATTERY_MONITOR)
#	HCCAMERA_SRC += $(wildcard $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/battery_monitor/*.c)
#endif

#ifeq ($(APPS_RESOLUTION_240P_SUPPORT),APPS_RESOLUTION_240P_SUPPORT)
#	HCCAMERA_SRC += $(shell find -L $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/ui_rsc/320x240 -name "*.c")
#else
#	HCCAMERA_SRC += $(shell find -L $(CUR_APP_DIR)/$(HCCAMERA_DIR_NAME)/ui_rsc/1280x720 -name "*.c")
#endif

#HCCAMERA_CPP_SRC += $(shell find -L $(CUR_APP_DIR)/hccamera_app -name "*.cpp")

