
export QUARTUS_64BIT     = 1

WORK_DIR		?= output_files
QWORK_DIR		?= $(WORK_DIR)
QWORK_DIR_DEP	?= $(QWORK_DIR)/_dir

QUARTUS_SH	= quartus_sh
OUTPUT_MASTER_LOG	= $(*F).master.outlog

all: ultimate_run.qflow

$(QWORK_DIR_DEP):
	@$(call print_cmd_info,"MK QWORK DIR",$(dir $@))
	@mkdir -p $(dir $@)
	@touch $@

.PHONY: %.qflow

%.qflow: $(QWORK_DIR_DEP)
	@echo "QSH FLOW - COMPILE" $(*F)
	@$(QUARTUS_SH) --flow compile $(*F) > $(QWORK_DIR)/$(OUTPUT_MASTER_LOG)
	@cp $(QWORK_DIR)/$(OUTPUT_MASTER_LOG) $(QWORK_DIR)/$(@F).outlog
	@! grep "Critical Warning" $(QWORK_DIR)/$(*F).*.rpt
	@echo "QSH FLOW - COMPLETE" $(*F)

