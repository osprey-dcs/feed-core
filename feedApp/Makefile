TOP=..

include $(TOP)/configure/CONFIG
ifneq ($(PYDEVSUP),)
include $(PYDEVSUP)/configure/CONFIG_PY
endif
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC += feedioc

DBD += feedioc.dbd

feedioc_DBD += base.dbd
feedioc_DBD += system.dbd
feedioc_DBD += feed.dbd

feedioc_SRCS += feedMain.cpp
feedioc_SRCS += feedioc_registerRecordDeviceDriver.cpp

ifneq ($(BUSY),)
feedioc_DBD += busySupport.dbd
feedioc_LIBS += busy asyn
endif

ifneq ($(AUTOSAVE),)
feedioc_DBD += asSupport.dbd
feedioc_LIBS += autosave

DB_INSTALLS += $(firstword $(wildcard $(AUTOSAVE)/asApp/Db/save_restoreStatus.db $(AUTOSAVE)/db/save_restoreStatus.db))
endif

ifneq ($(PYDEVSUP),)
feedioc_DBD += pyDevSup.dbd
feedioc_LIBS += pyDevSup$(PY_LD_VER)
endif

feedioc_LIBS += feed
feedioc_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
ifneq ($(PYDEVSUP),)
include $(PYDEVSUP)/configure/RULES_PY
endif
#----------------------------------------
#  ADD RULES AFTER THIS LINE
