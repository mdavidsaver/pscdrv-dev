TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

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

INC += psc/device.h
INC += psc/evbase.h
INC += psc/cblist.h
INC += psc/util.h
INC += psc/devcommon.h

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

