TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY += feed
DBD += feed.dbd

LIB_LIBS += $(EPICS_BASE_IOC_LIBS)
LIB_SYS_LIBS += z

SRC_DIRS += $(TOP)/src/util
LIB_SRCS += jblob.cpp
LIB_SRCS += zpp.cpp
LIB_SRCS += rom.cpp
LIB_SRCS += utils.cpp

SRC_DIRS += $(TOP)/src/sim
LIB_SRCS += simulator.cpp

SRC_DIRS += $(TOP)/src/driver
LIB_SRCS += device.cpp
LIB_SRCS += devmain.cpp
LIB_SRCS += devutil.cpp
LIB_SRCS += devreg.cpp
LIB_SRCS += devsignal.cpp
LIB_SRCS += devwatch.cpp
LIB_SRCS += iocsh.cpp
LIB_SRCS += asub.c

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

