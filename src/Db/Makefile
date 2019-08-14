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

DB += launcher.template

DB += rfs_test.template

# install expanded and unexpanded

DB += rfs_logic.substitutions
DB += rfs_logic.db

DB += rfs_registers.substitutions
DB += rfs_registers.db

DB += res_logic.db

DB += res_registers.substitutions
DB += res_registers.db

# 'short' = abbreviated register PV names
DB += rfs_registers_short.substitutions
DB += rfs_registers_short.db

DB += res_registers_short.substitutions
DB += res_registers_short.db

# RF support databases
DB += chas.template
DB += cav.template

DB += hires_logic.db
DB += hires_registers.substitutions
DB += hires_registers.db

hires_registers_FLAGS = -M long

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

%_logic.substitutions: ../%_logic.json ../feed_logic.py
	$(PYTHON) ../feed_logic.py $< $@

%_registers.substitutions: ../%_registers.json
	echo $(*)_registers_FLAGS
	PYTHONPATH="$(TOP)/src/python:${PYTHONPATH}" $(PYTHON) -m leep.cli file://$< template $($(notdir $(*))_registers_FLAGS) $@

%_registers.substitutions$(DEP): ../%_registers.json ../Makefile
	echo "$(COMMONDEP_TARGET) : ../Makefile" > $@

.PRECIOUS: %.substitutions
