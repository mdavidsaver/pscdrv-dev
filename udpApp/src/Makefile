TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/coreApp/src
USR_CPPFLAGS += -D_FILE_OFFSET_BITS=64

DBD += pscUDPFast.dbd

LIBRARY += pscUDPFast

BUILD_FAST_UDP = YES
ifndef BASE_7_0
BUILD_FAST_UDP = NO
endif
ifneq (Linux,$(OS_CLASS))
ifneq (Darwin,$(OS_CLASS))
BUILD_FAST_UDP = NO
endif
endif

# library uses features recvmmsg() not found in winsock, RTEMS <5, or (likely) vxWorks
# not likely to find a 10G NIC with RTEMS/vxWorks anyway.
ifeq (YES,$(BUILD_FAST_UDP))
pscUDPFast_SRCS += udpdrv.cpp
pscUDPFast_SRCS += devudp.cpp
pscUDPFast_dbd = ../pscUDPFast-7.dbd
else
pscUDPFast_SRCS += empty.c
pscUDPFast_dbd = ../pscUDPFast-dummy.dbd
endif

pscUDPFast_LIBS += pscCore
pscUDPFast_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

pscUDPFast.dbd$(DEP): $(pscUDPFast_dbd)
	@$(RM) $@
	@echo "$(COMMONDEP_TARGET): ../Makefile" > $@

$(COMMON_DIR)/pscUDPFast.dbd: $(pscUDPFast_dbd)
	$(CAT) $(pscUDPFast_dbd) > $@
