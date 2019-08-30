TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

vpath %.bob $(COMMON_DIR)

# abuse .db rules to install screens

INSTALL_DB = $(INSTALL_LOCATION)/opi

DB += feed_base.bob
DB += feed_wf_acquire_base.bob
DB += feed_wf_acquire_pair.bob
DB += hires_main.bob
DB += hires_odata.bob
DB += hires_odata_iq.bob
DB += hires_abuf.bob
DB += hires_adc.bob
DB += hires_pair.bob
DB += hires_signal.bob

# generated
DB += hires_registers.bob

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

$(COMMON_DIR)/hires_registers.bob: ../../Db/hires_registers.json
	PYTHONPATH="$(TOP)/src/python:${PYTHONPATH}" $(PYTHON) -m leep.eng $@ $<

%_registers.bob$(DEP): $(COMMON_DIR)/%_registers.bob ../Makefile
	echo "$(COMMONDEP_TARGET) : ../Makefile" > $@

.PRECIOUS: %_registers.bob %_registers.bob$(DEP)
