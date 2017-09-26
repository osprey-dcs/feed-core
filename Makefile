TOP = .
include $(TOP)/configure/CONFIG

DIRS += configure

DIRS += src
src_DEPEND_DIRS = configure

DIRS += sim
sim_DEPEND_DIRS = configure src

DIRS += tests
tests_DEPEND_DIRS = configure src

include $(TOP)/configure/RULES_TOP
