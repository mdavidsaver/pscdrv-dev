TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY_IOC = pscSig

DBD += pscSig.dbd

pscSig_SRCS += asubwf.c

ifeq ($(USE_FFTW),YES)

pscSig_SRCS += fftwrap.cpp
pscSig_SRCS += fftcalc.cpp

pscSig_SYS_LIBS += fftw3

else
pscSig_SRCS += noop.cpp
endif

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

