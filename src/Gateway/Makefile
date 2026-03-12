CONTIKI_PROJECT = border-router
all: $(CONTIKI_PROJECT)

CONTIKI = ../../../..

PLATFORMS_EXCLUDE = z1

include $(CONTIKI)/Makefile.dir-variables
MODULES += $(CONTIKI_NG_SERVICES_DIR)/rpl-border-router
MODULES_REL += webserver

include $(CONTIKI)/Makefile.include


