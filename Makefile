TOP = .
include $(TOP)/configure/CONFIG

DIRS += configure

DIRS += src
src_DEPEND_DIRS = configure

DIRS += src/Db

DIRS += sim
sim_DEPEND_DIRS = configure src

DIRS += feedApp
feedApp_DEPEND_DIRS = configure src

DIRS += tests
tests_DEPEND_DIRS = configure src src/Db

DIRS += slacRfApp
slacRfApp_DEPEND_DIRS = configure src

DIRS += iocBoot

include $(TOP)/configure/RULES_TOP
