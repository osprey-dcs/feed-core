TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY += feed

LIB_LIBS += $(EPICS_BASE_IOC_LIBS)
LIB_SYS_LIBS += z

SRC_DIRS += $(TOP)/src/util
LIB_SRCS += jblob.cpp
LIB_SRCS += zpp.cpp

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

