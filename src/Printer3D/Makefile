# Define the main project
CONTIKI_PROJECT = device
all: $(CONTIKI_PROJECT)
upload: $(CONTIKI_PROJECT).dfu-upload

# Set Contiki-NG base directory
CONTIKI = /home/user/contiki-ng

# Include basic directory variables
include $(CONTIKI)/Makefile.dir-variables

# Define custom project directories
PROJECTDIRS += utility

# Add external source files
# IMPORTANT: Renamed to printer_sensors.c to avoid conflicts with Contiki-NG core files
PROJECT_SOURCEFILES += coap_module.c
PROJECT_SOURCEFILES += mqtt_module.c
PROJECT_SOURCEFILES += printer_sensors.c

# Include required Contiki-NG system modules
MODULES += $(CONTIKI_NG_APP_LAYER_DIR)/mqtt
MODULES += $(CONTIKI_NG_APP_LAYER_DIR)/coap
MODULES += os/lib/json
MODULES += os/dev

# Configure include paths for Machine Learning Model (emlearn)
CFLAGS += -I/usr/local/lib/python3.10/dist-packages/emlearn
CFLAGS += -Wno-error=unused-variable

# Identify target platform
include $(CONTIKI)/Makefile.identify-target

# AUTOMATIC PLATFORM DETECTION
# Set CFLAGS based on the identified TARGET
ifeq ($(TARGET),cooja)
  CFLAGS += -DDEV_COOJA=1
else ifeq ($(TARGET),nrf52840)
  CFLAGS += -DDEV_DONGLE=1
endif

# Link the standard math library needed for specific formulas
TARGET_LIBFILES += -lm

# Include standard compilation rules
include $(CONTIKI)/Makefile.include