################################################################################
# Makefile for dss camera bootloader
# Author: Elric Hindy
# Date: 25 Aug 2016
# Version 1.0
################################################################################

SHELL := /bin/bash
TC_PATH := ../../thirdparty/avrgcc7.1
CC := $(TC_PATH)/bin/avr-gcc
AVR_OBJCOPY := $(TC_PATH)/bin/avr-objcopy
AVR_OBJDUMP := $(TC_PATH)/bin/avr-objdump
AVR_SIZE := $(TC_PATH)/bin/avr-size
PROJECT_FILE = bootloader
RELEASE_NAME = firmware-bootloader
SRC_DIR := src/
ROOT_DIR := $(shell pwd)
RM := rm -rf
F_CPU = 7372800UL
#MMCU = atmega168
MMCU = atmega1284p

# programmer settings
PROG_CONF_NAME := prog.cnf
HFUSE := 0xd4
LFUSE := 0xdd
EFUSE := 0xfd
LOCKBIT := 0x3f
LOCKMASK := 0xc0
DEVICE := m1284p
PROGRAMMER := avrispmkII
PORT := usb
NAME := 


# Include directory
IDIR := -I $(TC_PATH)/avr/include -I ../../lib -I $(SRC_DIR)

# General FLags
G_FLAGS := -funsigned-char -funsigned-bitfields

# Definition Flags
override DEFINES += -D F_CPU=$(F_CPU)

# CPU Flag
CPU_FLAG = -mmcu=$(MMCU)

# Optimisation Flags
OPT_FLAGS := -Os -ffunction-sections -fdata-sections -fpack-struct -fshort-enums 

# Warning Flags
W_FLAGS := -Wall -Wextra -Wno-expansion-to-defined 

# Miscellaneous Flags
M_FLAGS := -gdwarf-2

# Combined plus extra Compiler Flags
CFLAGS := -x c $(G_FLAGS) $(DEFINES) $(OPT_FLAGS) $(W_FLAGS) $(CPU_FLAG) -std=gnu99 --save-temps $(M_FLAGS) $(IDIR)
OBJ_DIR := obj/
OUTPUT_DIR := output/
VERSION := $(shell cat $(SRC_DIR)/version)
PROG_CONF_FILE := $(OUTPUT_DIR)/$(PROG_CONF_NAME)

QUOTE := "

SUBDIRS := $(wildcard */) $(wildcard */*/) $(wildcard */*/*/) $(wildcard */*/*/*/) $(wildcard */*/*/*/*/) $(wildcard */*/*/*/*/*/) $(wildcard */*/*/*/*/*/*/) $(wildcard */*/*/*/*/*/*/*/)

SOURCES =$(wildcard $(addsuffix *.c,$(SUBDIRS)))

OBJECTS = $(patsubst %.c,%.o, $(SOURCES))

FILE_ELF = $(OUTPUT_DIR)$(PROJECT_FILE).elf
FILE_HEX = $(OUTPUT_DIR)$(PROJECT_FILE).hex 
FILE_LSS = $(OUTPUT_DIR)$(PROJECT_FILE).lss 
FILE_SREC = $(OUTPUT_DIR)$(PROJECT_FILE).srec 
FILE_EEP = $(OUTPUT_DIR)$(PROJECT_FILE).eep 
FILE_MAP = $(OUTPUT_DIR)$(PROJECT_FILE).map
FILE_BINARY = $(OUTPUT_DIR)$(PROJECT_FILE).bin

# Add inputs and outputs from these tool invocations to the build variables 
OBJS := $(addprefix $(OBJ_DIR),$(OBJECTS))
OBJ_DIRS := $(sort $(dir $(OBJS)))
C_DEPS = $(patsubst %.o,%.d, $(OBJS))

OUTPUT_FILE_PATH := $(FILE_ELF) 

# Release Target generates m5sum and creates release filename format
release: $(FILE_HEX) $(PROG_CONF_FILE) release-force
	cp $(FILE_HEX) $(OUTPUT_DIR)$(RELEASE_NAME)_$(VERSION)_$(shell md5sum $(FILE_HEX) | cut -c 1-32).hex

# Release Target generates m5sum and creates release filename format
release-force: $(FILE_HEX) $(PROG_CONF_FILE)
	cp $(FILE_HEX) $(OUTPUT_DIR)$(RELEASE_NAME)_$(VERSION)_$(shell md5sum $(FILE_HEX) | cut -c 1-32)_force.hex

programmer-config: $(PROG_CONF_FILE) $(OUTPUT_DIR)

$(PROG_CONF_FILE):
	@echo creating programming config file
	@echo "name = $(NAME)" > $(PROG_CONF_FILE)
	@echo "firmware = $(FILE_HEX)" >> $(PROG_CONF_FILE)
	@echo "device = $(DEVICE)" >> $(PROG_CONF_FILE)
	@echo "programmer = $(PROGRAMMER)" >> $(PROG_CONF_FILE)
	@echo "port = $(PORT)" >> $(PROG_CONF_FILE)
	@echo "hfuse = $(HFUSE)" >> $(PROG_CONF_FILE)
	@echo "lfuse = $(LFUSE)" >> $(PROG_CONF_FILE)
	@echo "efuse = $(EFUSE)" >> $(PROG_CONF_FILE)
	@echo "lockbit = $(LOCKBIT)" >> $(PROG_CONF_FILE)
	@echo "lockmask = $(LOCKMASK)" >> $(PROG_CONF_FILE)


$(FILE_HEX): $(OUTPUT_FILE_PATH)

# debug build
debug: DEFINES += -DDEBUG
debug: CFLAGS := -x c $(G_FLAGS) $(CFLAGS) $(OPT_FLAGS) $(W_FLAGS) $(CPU_FLAG) $(DEFINES) -std=gnu99 $(M_FLAGS) $(IDIR)
debug: releasedebug
	
releasedebug: $(FILE_HEX)
	cp $(FILE_HEX) $(OUTPUT_DIR)$(RELEASE_NAME)_$(VERSION)_$(shell md5sum $(FILE_HEX) | cut -c 1-32)_debug.hex

# All Target
all: $(OUTPUT_FILE_PATH)

$(OBJ_DIR)src/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)src/
	@echo Building file: $<
	@echo Invoking: AVR/GNU compiler
	$(QUOTE)$(CC)$(QUOTE) $(CFLAGS) $(IDIR) -c $(M_FLAGS) -MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)"   -o "$@" "$<" 
	@echo Finished building: $<



$(OBJ_DIRS): 
	mkdir -p $@


# AVR32/GNU Assembler

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

# Add inputs and outputs from these tool invocations to the build variables




$(OUTPUT_FILE_PATH): $(OBJS) | $(OUTPUT_DIR)
	@echo Building target: $@
	@echo Invoking: AVR/GNU Linker 
	$(QUOTE)$(CC)$(QUOTE) -o $(OUTPUT_FILE_PATH) $(OBJS) -Wl,-Map=$(FILE_MAP) -Wl,--start-group  -Wl,--end-group -Wl,--gc-sections $(CPU_FLAG) 
	@echo Finished building target: $@
	$(AVR_OBJCOPY) -O ihex -R .eeprom -R .fuse -R .lock -R .signature -R .user_signatures $(FILE_ELF) $(FILE_HEX)
	$(AVR_OBJCOPY) -j .eeprom  --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0  --no-change-warnings -O ihex $(FILE_ELF) $(FILE_EEP) || exit 0
	$(AVR_OBJDUMP) -h -S $(FILE_ELF) > $(FILE_LSS)
	$(AVR_OBJCOPY) -O srec -R .eeprom -R .fuse -R .lock -R .signature -R .user_signatures $(FILE_ELF) $(FILE_SREC)
	$(AVR_OBJCOPY) -I ihex -O binary $(FILE_HEX) $(FILE_BINARY)
	$(AVR_SIZE) $(FILE_ELF)


$(OUTPUT_DIR):
	mkdir -p $@	

# Other Targets
clean:
	-$(RM) $(OBJ_DIR) 
	-$(RM) $(OUTPUT_DIR)
	
	


# Printing of variables for testing can use with `make print-<variable name>` i.e. `make print-OBJS`
print-%  : ; @echo $* = $($*)	
