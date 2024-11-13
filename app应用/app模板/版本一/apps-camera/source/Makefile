#
# Makefile
#
CC ?= gcc
CUR_APP_DIR ?= ${shell pwd}

SYS_DIR = -I$(CUR_APP_DIR)/hccamera_app \
		-I$(CUR_APP_DIR)/hccamera_app/include \
		-I$(STAGING_DIR)/usr/include \
		-I$(STAGING_DIR)/usr/include/hcuapi

LVGL_HEADERS = -I$(STAGING_DIR)/usr/include/lvgl/ \
		-I$(STAGING_DIR)/usr/include/lvgl/lvgl


MY_DEF =

DLNA_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o DLNA_SUPPORT)
MIRACAST_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o MIRACAST_SUPPORT)
AIRCAST_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o AIRCAST_SUPPORT)
USBMIRROR_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o USBMIRROR_SUPPORT)
BLUETOOTH_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o BLUETOOTH_SUPPORT)
FFMPEG_SWSCALE_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o FFMPEG_SWSCALE_SUPPORT)
AUDIO_SPECTRUM_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o AUDIO_SPECTRUM_SUPPORT)
APPS_LIB_CURL_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o APPS_LIB_CURL_SUPPORT)
LIB_OPENSSL_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o LIB_OPENSSL_SUPPORT)
APPS_RESOLUTION_240P_SUPPORT := $(shell echo $(HCCAMERAAPP_DEF) | grep -o APPS_RESOLUTION_240P_SUPPORT)

CFLAGS ?= -O3 $(SYS_DIR)/ -Wall -Wshadow -Wundef -Wmissing-prototypes -Wno-discarded-qualifiers -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wtype-limits -Wshift-negative-value -Wstack-usage=2048 -Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual -Wmissing-prototypes -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wno-discarded-qualifiers -Wformat-security -Wno-ignored-qualifiers -Wno-sign-compare
CFLAGS += $(LVGL_HEADERS) -include $(AUTO_CONFIG)
LDFLAGS ?= -lm -llvgl -Wl,-Map,$@.map

DEMO_LIBS = -lffplayer -lglist -lpthread -lge -Wl,--start-group -lstdc++ -Wl,--end-group
DEMO_LIBS += -lhcfota

USB_CAST_LIB = 
WIFI_CAST_LIB = 

ifeq ($(DLNA_SUPPORT),DLNA_SUPPORT)
	#MY_DEF += -DDLNA_SUPPORT
	DEMO_LIBS += -ldlna
	WIFI_CAST_LIB = 1
endif

ifeq ($(MIRACAST_SUPPORT),MIRACAST_SUPPORT)
	#MY_DEF += -DMIRACAST_SUPPORT
	DEMO_LIBS += -lmiracast
	WIFI_CAST_LIB = 1
endif

ifeq ($(AIRCAST_SUPPORT),AIRCAST_SUPPORT)
	#MY_DEF += -DAIRCAST_SUPPORT
	#DEMO_LIBS += -laircast
	WIFI_CAST_LIB = 1
endif

ifeq ($(USBMIRROR_SUPPORT),USBMIRROR_SUPPORT)
	#MY_DEF += -DUSBMIRROR_SUPPORT
	USB_CAST_LIB = 1
	DEMO_LIBS += -lhccast-um -lusb-1.0 -lssl
endif

ifeq ($(WIFI_CAST_LIB),1)
	DEMO_LIBS += -lhccast-com
else	
	ifeq ($(USB_CAST_LIB),1)
		DEMO_LIBS += -lhccast-com
	endif
endif	


ifeq ($(WIFI_CAST_LIB),1)
	DEMO_LIBS += -lmdns -lcjson -lwpa_client -lhccast-net -lhccast-wl
endif	


ifeq ($(FFMPEG_SWSCALE_SUPPORT),FFMPEG_SWSCALE_SUPPORT)
	#MY_DEF += -DFFMPEG_SWSCALE_SUPPORT
	DEMO_LIBS += -lswscale
endif

ifeq ($(AUDIO_SPECTRUM_SUPPORT),AUDIO_SPECTRUM_SUPPORT)
	DEMO_LIBS += -lspectrum
endif

ifeq ($(APPS_LIB_CURL_SUPPORT),APPS_LIB_CURL_SUPPORT)
	DEMO_LIBS += -lcurl
endif

ifeq ($(LIB_OPENSSL_SUPPORT),LIB_OPENSSL_SUPPORT)
	DEMO_LIBS += -lcrypto
endif

ifeq ($(BLUETOOTH_SUPPORT),BLUETOOTH_SUPPORT)
	DEMO_LIBS += -lbluetooth
endif

include $(CUR_APP_DIR)/hccamera_app/camera.mk
DEMO_NAME = hccamera

OBJEXT ?= .o

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
HCCAMERA_OBJS = $(HCCAMERA_SRC:.c=$(OBJEXT))
HCCAMERA_OBJS += $(HCCAMERA_CPP_SRC:.cpp=$(OBJEXT))

## MAINOBJ -> OBJFILES


all_depends := $(patsubst %.c,%.d,$(CSRCS) $(HCCAMERA_SRC))
all_depends +=$(patsubst %.cpp,%.d,$(HCCAMERA_CPP_SRC))
all_depends += $(patsubst %.S,%.d,$(ASRCS))

all: $(DEMO_NAME)

%.o: %.c
	@$(CC) $(CFLAGS) $(MY_DEF) -c $< -o $@
	@echo "Compiling $@"
	
.PHONY: FORCE

%.o: %.cpp
	@$(CC) $(CFLAGS) $(MY_DEF) -c $< -o $@
	@echo "Compiling $@"

%.d: %.c
	@set -e;rm -rf $@;$(CC) $(CFLAGS) $(MY_DEF) -MM $^ > $@.$$$$;cp $@.$$$$ $@;rm -rf $@.$$$$

%.d: %.cpp
	@set -e;rm -rf $@;$(CC) $(CFLAGS) $(MY_DEF) -MM $^ > $@.$$$$;cp $@.$$$$ $@;rm -rf $@.$$$$

-include $(all_depends)

$(DEMO_NAME):	$(COBJS) $(HCCAMERA_OBJS)
	@$(CC) -o $@ $(HCCAMERA_OBJS) $(COBJS) $(LDFLAGS) $(DEMO_LIBS)
	
clean: 
	rm -f $(AOBJS) $(COBJS) $(HCCAMERA_OBJS)
	@find ./ -type f -name "*.o" |xargs rm -rf
	rm -rf $(DEMO_NAME)

install:
	@echo "install ......"
