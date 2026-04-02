CONTIKI_PROJECT = device
all: $(CONTIKI_PROJECT)
upload: $(CONTIKI_PROJECT).dfu-upload

CONTIKI=../../../..

MODULES += os/net/app-layer/coap

PROJECTDIRS += utility

# Add external source files (relative to project dir)
PROJECT_SOURCEFILES += coap_module.c
PROJECT_SOURCEFILES += mqtt_module.c
PROJECT_SOURCEFILES += printer_sensors.c

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

# Set CFLAGS based on the identified TARGET
ifeq ($(TARGET),cooja)
  CFLAGS += -DDEV_COOJA=1
else ifeq ($(TARGET),nrf52840)
  CFLAGS += -DDEV_DONGLE=1
endif

TARGET_LIBFILES = -lm
include $(CONTIKI)/Makefile.include
