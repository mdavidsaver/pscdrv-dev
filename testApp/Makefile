TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/coreApp/src

PROD_LIBS += Com

TESTPROD_HOST += testValues
testValues_SRCS += testValues.cpp
TESTS += testValues


PROD_LIBS += pscCore
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_SYS_LIBS += event_core event_extra
PROD_SYS_LIBS_Linux += event_pthreads
TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

