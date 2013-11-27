#
# qflow project scripts makefile
#

# Main compiler arguments
CFLAGS = -g -O2
DEFS = -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"\" -DSTDC_HEADERS=1 -DHAVE_SETENV=1 -DHAVE_PUTENV=1 -DTCLSH_PATH=\"tclsh\" -DQFLOW_MAGIC_PATH=\"/usr/local/bin/magic\" -DQFLOW_QROUTER_PATH=\"/usr/local/bin/qrouter\" -DQFLOW_TWRC_PATH=\"/home/tim/src/timberwolf-6.3/.twrc\" -DQFLOW_YOSYS_PATH=\"/usr/local/bin/yosys\" -DQFLOW_ODIN_PATH=\"/home/tim/src/vtr/ODIN_II/odin_II.exe\" -DQFLOW_ABC_PATH=\"/home/tim/src/vtr/abc_with_bb_support/abc\"
LIBS = 
LDFLAGS = 
INSTALL = /usr/bin/install -c

QFLOW_LIB_DIR = /usr/local/share/qflow
QFLOW_BIN_DIR = /usr/local/bin

DEFAULTPARSER = @QFLOW_DEFAULT_PARSER@

TCL_SCRIPTS = blif2cel.tcl clocktree.tcl place2def.tcl place2lef2.tcl
TCL_SCRIPTS += place2net2.tcl postproc.tcl ypostproc.tcl ybuffer.tcl
TCL_SCRIPTS += setreset.tcl vmunge.tcl decongest.tcl
TCL_SCRIPTS += addspacers.tcl vispreproc.tcl outputprep.tcl
SHELL_SCRIPTS = placement.sh router.sh vesta.sh
SHELL_SCRIPTS += synthesize_odin.sh synthesize_yosys.sh
SHELL_SCRIPTS += resynthesize.sh qflow.sh checkdirs.sh
SHELL_SCRIPTS += cleanup.sh display.sh createGDS.sh
MAIN_SCRIPT = qflow

SCRIPTINSTALL = ${QFLOW_LIB_DIR}/scripts
TECHINSTALL = ${QFLOW_LIB_DIR}/tech
QFLOWEXECPATH = ${QFLOW_LIB_DIR}/bin
EXECINSTALL = ${QFLOW_BIN_DIR}

all:	$(MAIN_SCRIPT).in qflow.sh
	$(MAKE) launcher

launcher:  $(MAIN_SCRIPT).in
		sed -e '/QFLOW_SCRIPT_DIR/s#QFLOW_SCRIPT_DIR#$(SCRIPTINSTALL)#' \
		$(MAIN_SCRIPT).in > $(MAIN_SCRIPT)

checkdirs.sh:  checkdirs.sh.in
		sed -e '/SUBST_TECH_DIR/s#SUBST_TECH_DIR#$(TECHINSTALL)#' \
		-e '/SUBST_SCRIPT_DIR/s#SUBST_SCRIPT_DIR#$(SCRIPTINSTALL)#' \
		-e '/SUBST_BIN_DIR/s#SUBST_BIN_DIR#$(QFLOWEXECPATH)#' \
		checkdirs.sh.in > checkdirs.sh

qflow.sh:  qflow.sh.in
		sed -e '/QFLOW_SCRIPT_DIR/s#QFLOW_SCRIPT_DIR#$(SCRIPTINSTALL)#' \
		-e '/QFLOW_DEFAULT_PARSER/s#QFLOW_DEFAULT_PARSER#$(DEFAULTPARSER)#' \
		qflow.sh.in > qflow.sh
	
install: $(TCL_SCRIPTS) $(SHELL_SCRIPTS) $(MAIN_SCRIPT)
	@echo "Installing qflow scripts"
	$(INSTALL) -d ${SCRIPTINSTALL}
	for target in $(TCL_SCRIPTS); do \
	   $(INSTALL) $$target ${SCRIPTINSTALL} ;\
	done
	for target in $(SHELL_SCRIPTS); do \
	   $(INSTALL) $$target ${SCRIPTINSTALL} ;\
	done
	@echo "Installing qflow executable"
	$(INSTALL) -d ${EXECINSTALL}
	$(INSTALL) ${MAIN_SCRIPT} ${EXECINSTALL}

clean:
	$(RM) $(MAIN_SCRIPT)
	$(RM) checkdirs.sh
	$(RM) qflow.sh

uninstall:
	$(RM) -rf ${SCRIPTINSTALL}
	$(RM) ${EXECINSTALL}/${MAIN_SCRIPT}

