TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC += feedioc

DBD += feedioc.dbd

feedioc_DBD += base.dbd
feedioc_DBD += feed.dbd

feedioc_SRCS += feedMain.cpp
feedioc_SRCS += feedioc_registerRecordDeviceDriver.cpp

feedioc_LIBS += feed
feedioc_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE