TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/coreApp/src
USR_CPPFLAGS += -D_FILE_OFFSET_BITS=64

DBD += pscudp.dbd

LIBRARY += pscudp

# library uses features recvmmsg() not found in winsock, RTEMS <5, or (likely) vxWorks
# not likely to find a 10G NIC with RTEMS/vxWorks anyway.
ifdef BASE_7_0
pscudp_SRCS += udpdrv.cpp
pscudp_SRCS += devudp.cpp
pscudp_dbd = ../pscudp-7.dbd
else
pscudp_SRCS += empty.c
pscudp_dbd = ../pscudp-dummy.dbd
endif

pscudp_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

pscudp.dbd$(DEP): $(pscudp_dbd)
	@$(RM) $@
	@echo "$(COMMONDEP_TARGET): ../Makefile" > $@

$(COMMON_DIR)/pscudp.dbd: $(pscudp_dbd)
	$(CAT) $(pscudp_dbd) > $@
