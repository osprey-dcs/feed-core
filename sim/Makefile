TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/src/util
USR_CPPFLAGS += -I$(TOP)/src/sim

PROD_HOST += feedsim

PROD_LIBS += feed
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_SYS_LIBS += z

feedsim_SRCS += simmain.cpp

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
