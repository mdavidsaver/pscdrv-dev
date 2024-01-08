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

ifdef BASE_7_0

DBDDEPENDS_FILES += testIOC.dbd$(DEP)
testIOC_DBD = base.dbd pscCore.dbd
TESTFILES += $(COMMON_DIR)/testIOC.dbd

TESTPROD_HOST += testIOCUDP
testIOCUDP_SRCS += testIOCUDP.cpp
testIOCUDP_SRCS += testIOC_registerRecordDeviceDriver.cpp
TESTS += testIOCUDP
TESTFILES += testudp.db

endif # BASE_7_0

PROD_LIBS += pscCore
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_SYS_LIBS += event_core event_extra
PROD_SYS_LIBS_DEFAULT = event_pthreads
PROD_SYS_LIBS_WIN32 =
TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

