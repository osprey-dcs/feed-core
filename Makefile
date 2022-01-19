TOP = .
include $(TOP)/configure/CONFIG

DIRS += configure

DIRS += src
src_DEPEND_DIRS = configure

DIRS += src/Db
DIRS += src/opi

DIRS += sim
sim_DEPEND_DIRS = configure src

DIRS += common/wf/Db
common/wf/Db_DEPEND_DIRS = configure

DIRS += common/wf/opi
common/wf/opi_DEPEND_DIRS = configure

DIRS += common/rf/Db
common/rf/Db_DEPEND_DIRS = common/wf/Db

DIRS += common/injector/Db
common/rf/Db_DEPEND_DIRS = common/wf/Db

DIRS += board/rfs/Db
board/rfs/Db_DEPEND_DIRS = src src/Db common/rf/Db

DIRS += board/prc/Db
board/prc/Db_DEPEND_DIRS = src src/Db board/rfs/Db

DIRS += board/res/Db
board/res/Db_DEPEND_DIRS = src src/Db common/rf/Db board/rfs/Db

DIRS += board/hires/Db
board/hires/Db_DEPEND_DIRS = src src/Db

DIRS += board/hires/opi
board/hires/opi_DEPEND_DIRS = src

DIRS += feedApp
feedApp_DEPEND_DIRS = configure src

DIRS += tests
tests_DEPEND_DIRS = configure src src/Db

DIRS += slacRfApp
slacRfApp_DEPEND_DIRS = configure src board/rfs/Db board/prc/Db board/res/Db

DIRS += iocBoot

UNINSTALL_DIRS += $(INSTALL_LOCATION)/opi

include $(TOP)/configure/RULES_TOP
