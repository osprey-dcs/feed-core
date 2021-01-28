TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/src/util
USR_CPPFLAGS += -I$(TOP)/src/sim
USR_CPPFLAGS += -I$(TOP)/src/driver

USR_CPPFLAGS += -DTOP=\""$(TOP)"\"

DBDDEPENDS_FILES += testfeed.dbd$(DEP)
TESTFILES += $(COMMON_DIR)/testfeed.dbd
testfeed_DBD += base.dbd
testfeed_DBD += feed.dbd

PROD_LIBS += feed

ifeq ($(USE_FFTW),YES)
PROD_LIBS += fftw3 
else ifeq ($(USE_SYS_FFTW),YES)
PROD_SYS_LIBS += fftw3 
else ifeq ($(USE_PKG_FFTW),YES)
fftw3_DIR = $(FFTW_LIB)
PROD_LIBS += fftw3 
endif

PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
PROD_SYS_LIBS += z

TESTPROD_HOST += testjson
testjson_SRCS += testjson
TESTS += testjson

TESTPROD_HOST += testz
testz_SRCS += testz.cpp
TESTS += testz

TESTPROD_HOST += testrom
testrom_SRCS += testrom.cpp
TESTS += testrom

TESTPROD_HOST += testdevice
testdevice_SRCS += testdevice.cpp
testdevice_SRCS += testfeed_registerRecordDeviceDriver.cpp
TESTS += testdevice

TESTPROD_HOST += testsub
testsub_SRCS += testsub.cpp
testsub_SRCS += testfeed_registerRecordDeviceDriver.cpp
TESTS += testsub

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


