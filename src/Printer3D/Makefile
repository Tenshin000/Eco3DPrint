CONTIKI_PROJECT = device
all: $(CONTIKI_PROJECT)
upload: $(CONTIKI_PROJECT).dfu-upload

CONTIKI=../../../..

MODULES += os/net/app-layer/coap

# Add external source files (relative to project dir)
PROJECT_SOURCEFILES += utility/coap_module.c
PROJECT_SOURCEFILES += utility/mqtt_module.c
PROJECT_SOURCEFILES += utility/sensors.c

include $(CONTIKI)/Makefile.dir-variables

# Include CoAP/MQTT modules from contiki-ng dirs (remove if not needed)
MODULES += $(CONTIKI_NG_APP_LAYER_DIR)/mqtt
MODULES += $(CONTIKI_NG_APP_LAYER_DIR)/coap
MODULES += os/lib/json


# For ML Model
MODULES_REL += /home/iot_ubuntu_intel/.local/lib/python3.10/site-packages/emlearn
INC += /home/iot_ubuntu_intel/.local/lib/python3.10/site-packages/emlearn
CFLAGS += -Wno-error=unused-variable

include $(CONTIKI)/Makefile.identify-target

MODULES_REL += arch/platform/$(TARGET)

TARGET_LIBFILES = -lm
include $(CONTIKI)/Makefile.include
