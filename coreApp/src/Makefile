TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -DBUILDING_pscdrv_API

LIBRARY_IOC = pscCore

DBD += pscCore.dbd

pscCore_SRCS += evbase.cpp
pscCore_SRCS += pscbase.cpp
pscCore_SRCS += psc.cpp
pscCore_SRCS += pscudp.cpp
pscCore_SRCS += pscwrap.cpp
pscCore_SRCS += util.c
pscCore_SRCS += devcommon.cpp
pscCore_SRCS += devPSCString.cpp
pscCore_SRCS += devPSCWf.cpp
pscCore_SRCS += devPSCReg.cpp
pscCore_SRCS += devPSCSingle.cpp
pscCore_SRCS += devPSCCtrl.cpp

pscCore_LIBS += $(EPICS_BASE_IOC_LIBS)

pscCore_SYS_LIBS += event_core event_extra
pscCore_SYS_LIBS_DEFAULT = event_pthreads
pscCore_SYS_LIBS_WIN32 =

ifdef BASE_7_0
PSCDBD = ../pscCore-new.dbd
else
PSCDBD = ../pscCore-old.dbd
endif

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

../O.Common/pscCore.dbd: $(PSCDBD)
	$(CP) $< $@

pscCore.dbd$(DEP):
	@$(RM) $@
	@echo "$(COMMONDEP_TARGET): ../Makefile" > $@

