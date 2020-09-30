##############################################################################
# Build global options
# NOTE: Can be overridden externally.
#

# Compiler options here.
# -Wdouble-promotion -fno-omit-frame-pointer

DEBUG := 1
OPT_SPEED := 2
OPT_SIZE := 3

EXECMODE := $(DEBUG)
#EXECMODE := $(OPT_SPEED)
#EXECMODE := $(OPT_SIZE)


GCCVERSIONGTEQ7 := $(shell expr `arm-none-eabi-gcc -dumpversion | cut -f1 -d.` \>= 7)
GCC_DIAG =  -Werror -Wno-error=unused-variable -Wno-error=format \
            -Wno-error=cpp \
            -Wno-error=unused-function \
            -Wunused -Wpointer-arith \
            -Werror=sign-compare \
            -Wshadow -Wparentheses -fmax-errors=5 \
            -ftrack-macro-expansion=2 -Wno-error=strict-overflow -Wstrict-overflow=2


ifeq ($(EXECMODE),$(DEBUG)) 
  USE_OPT =  -O0  -ggdb3  -Wall -Wextra \
	    -falign-functions=16 -fomit-frame-pointer \
	    $(GCC_DIAG)
endif

ifeq ($(EXECMODE),$(OPT_SPEED)) 
    USE_OPT =  -Ofast -flto  -Wall -Wextra \
	    -falign-functions=16 -fomit-frame-pointer \
	     $(GCC_DIAG)
endif

ifeq ($(EXECMODE),$(OPT_SIZE)) 
    USE_OPT =  -Os  -flto  -Wall -Wextra \
	    -falign-functions=16 -fomit-frame-pointer \
            --specs=nano.specs \
            -DCH_DBG_STATISTICS=0 -DCH_DBG_SYSTEM_STATE_CHECK=0 -DCH_DBG_ENABLE_CHECKS=0 \
	    -DCH_DBG_ENABLE_ASSERTS=0 -DCH_DBG_ENABLE_STACK_CHECK=0 -DCH_DBG_FILL_THREADS=0 \
	    -DCH_DBG_THREADS_PROFILING=0 -DNOSHELL=1 -DNOTESTGEN=1\
	     $(GCC_DIAG)
endif


# C specific options here (added to USE_OPT).
ifeq ($(USE_COPT),)
  USE_COPT = -std=gnu11  -Wunsuffixed-float-constants 
endif

# C++ specific options here (added to USE_OPT).
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -std=c++2a -fno-rtti -fno-exceptions -fno-threadsafe-statics
endif

# Enable this if you want the linker to remove unused code and data
ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

# Linker extra options here.
ifeq ($(USE_LDOPT),)
  USE_LDOPT = 
endif

# Enable this if you want link time optimizations (LTO).
ifeq ($(USE_LTO),)
  USE_LTO = no
endif


# Enable this if you want to see the full log while compiling.
ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

# If enabled, this option makes the build process faster by not compiling
# modules not used in the current configuration.
ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

#
# Build global options
##############################################################################

##############################################################################
# Architecture or project specific options
#

# Stack size to be allocated to the Cortex-M process stack. This stack is
# the stack used by the main() thread.
ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

# Stack size to the allocated to the Cortex-M main/exceptions stack. This
# stack is used for processing interrupts and exceptions.
ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

# Enables the use of FPU (no, softfp, hard).
ifeq ($(USE_FPU),)
  USE_FPU = no
endif

# FPU-related options.
ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv4-sp-d16
endif

#
# Architecture or project specific options
##############################################################################

##############################################################################
# Project, target, sources and paths
#

# Define project name here
PROJECT = ch
BOARD = DEVBM4

# Target settings.
MCU  = cortex-m4


CHIBIOS  := ../chibios_svn_mirror
CONFDIR  := ./cfg
BUILDDIR := ./build
DEPDIR   := ./.dep
VARIOUS  := ../chibios_enac_various_common
USBD_LIB := $(VARIOUS)/Chibios-USB-Devices
TOOLDIR  := $(VARIOUS)/TOOLS

# Licensing files.
include $(CHIBIOS)/os/license/license.mk
# Startup files.
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32f4xx.mk
# HAL-OSAL files (optional).
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32F4xx/platform.mk
include cfg/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
# RTOS files (optional).
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMCMx/compilers/GCC/mk/port_v7m.mk
# Auto-build files in ./source recursively.
include $(CHIBIOS)/tools/mk/autobuild.mk
# Other files (optional).


# Define linker script file here
LDSCRIPT= $(STARTUPLD)/STM32F407xG.ld

# C sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CSRC = $(ALLCSRC) \
       $(CHIBIOS)/os/various/syscalls.c \
       $(VARIOUS)/stdutil.c \
       $(VARIOUS)/printf.c \
       $(VARIOUS)/microrl/microrlShell.c \
       $(VARIOUS)/microrl/microrl.c \
       $(VARIOUS)/adcHelper.c


# C++ sources that can be compiled in ARM or THUMB mode depending on the global
# setting.
CPPSRC = $(ALLCPPSRC)

# List ASM source files here.
ASMSRC = $(ALLASMSRC)

# List ASM with preprocessor source files here.
ASMXSRC = $(ALLXASMSRC)

# Inclusion directories.
INCDIR = $(CONFDIR) $(ALLINC) $(VARIOUS) $(STMSRC) $(USBD_LIB) 

# Define C warning options here.
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes

# Define C++ warning options here
CPPWARN = -Wall -Wextra -Wundef

#
# Compiler settings
##############################################################################

##############################################################################
# Start of user section
#

# List all user C define here, like -D_DEBUG=1
UDEFS = -DTRACE

# Define ASM defines here
UADEFS =

# List all user directories here
UINCDIR =

# List the user directory to look for the libraries here
ULIBDIR =

# List all user libraries here
ULIBS =

#
# End of user defines
##############################################################################

##############################################################################
# Common rules
#

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk
$(OBJS): $(CONFDIR)/board.h


$(CONFDIR)/board.h: $(CONFDIR)/board.cfg
	$(TOOLDIR)/boardGen.pl --no-adcp-in --no-pp-line $<  $@ || \
        cp -v cfg/board.generated.h cfg/board.h

stflash: all
	@echo write $(BUILDDIR)/$(PROJECT).bin to flash memory
	st-flash write  $(BUILDDIR)/$(PROJECT).bin 0x08000000
	@echo Done

flash: all
	@echo write $(BUILDDIR)/$(PROJECT).bin to flash memory
	$(TOOLDIR)/bmpflash  $(BUILDDIR)/$(PROJECT).elf
	@echo Done
