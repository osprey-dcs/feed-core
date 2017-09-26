TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/common

PROD_LIBS += feedcom Com
PROD_SYS_LIBS += z

TESTPROD_HOST += testjson
testjson_SRCS += testjson
TESTS += testjson

TESTPROD_HOST += testz
testz_SRCS += testz.cpp
TESTS += testz

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


