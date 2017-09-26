TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/common

TESTPROD_HOST += testjson
testjson_SRCS += testjson
testjson_LIBS += feedcom Com
TESTS += testjson

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE


