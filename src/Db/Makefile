TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

DB += feed_base.template
DB += feed_reg_r_array.template
DB += feed_reg_r_scalar.template
DB += feed_reg_rw_array.template
DB += feed_reg_rw_scalar.template
DB += feed_reg_w_array.template

DB += feed_wf_acquire_base.template
DB += feed_wf_acquire_signal.template
DB += feed_wf_acquire_pair.template
DB += feed_wf_acquire_stats.template

DB += rfs_base.template
DB += rfs_slow_scalar16.template

# install expanded and unexpanded

DB += rfs_logic.substitutions
DB += rfs_logic.db

DB += rfs_registers.substitutions
DB += rfs_registers.db

# 'short' = abbreviated register PV names
DB += rfs_registers_short.substitutions
DB += rfs_registers_short.db

# RF support databases
DB += chas.template
DB += cav.template

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
