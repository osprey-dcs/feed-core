TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

# abuse .db rules to install screens

INSTALL_DB = $(INSTALL_LOCATION)/opi

DB += feed_base.bob
DB += feed_wf_acquire_base.bob
DB += feed_wf_acquire_pair.bob
DB += hires_main.bob
DB += hires_odata.bob
DB += hires_odata_iq.bob
DB += hires_pair.bob
DB += hires_registers.bob
DB += hires_signal.bob

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

.PRECIOUS: %_logic.substitutions %_registers.substitutions
