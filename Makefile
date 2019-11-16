# external parameters
DEBUG = 1
OPT_LEVEL = 2

# judge if is debug mode
ifeq ($(DEBUG), 0)
	DEBUG_ARG = -DNDEBUG
	OPT_ARG = -O$(OPT_LEVEL)
else
	DEBUG_ARG = -g
	OPT_ARG = 
endif

# directories
export TOP_DIR = $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
export TARGET_DIR = $(TOP_DIR)/build
export OBJ_DIR = $(TOP_DIR)/build/obj
export SRC_DIR = $(TOP_DIR)/src
export TEST_DIR = $(TOP_DIR)/test
export EXAMPLE_DIR = $(TOP_DIR)/example

# toolchain
include toolchain.mk

# helper functions
export rwildcard = $$(foreach d, $$(wildcard $$(1:=/*)), $$(call rwildcard, $$d, $$2) $$(filter $$(subst *, %, $$2), $$d))

# all sub-makes
SUB_MAKE := $(SRC_DIR)/compiler
SUB_MAKE += $(SRC_DIR)/utility/elfinfo
SUB_MAKE += $(EXAMPLE_DIR)/serial


.PHONY: all clean $(SUB_MAKE)

all: $(TARGET_DIR) $(SUB_MAKE)

clean: $(SUB_MAKE)

$(SUB_MAKE):
	$(MAKE) -C $@ $(MAKECMDGOALS)

$(EXAMPLE_DIR)/serial: $(SRC_DIR)/compiler

$(TARGET_DIR):
	mkdir $@
