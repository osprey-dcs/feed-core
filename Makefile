TOP = .
include $(TOP)/configure/CONFIG

DIRS += configure

DIRS += common
common_DEPEND_DIRS = configure

DIRS += tests
tests_DEPEND_DIRS = configure common

include $(TOP)/configure/RULES_TOP
