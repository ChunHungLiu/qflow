#
# qflow project tech makefile
#

# Main compiler arguments
CFLAGS = -g -O2
DEFS = -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"\" -DSTDC_HEADERS=1 -DHAVE_SETENV=1 -DHAVE_PUTENV=1 -DTCLSH_PATH=\"tclsh\" -DQFLOW_MAGIC_PATH=\"/usr/local/bin/magic\" -DQFLOW_QROUTER_PATH=\"/usr/local/bin/qrouter\" -DQFLOW_TWRC_PATH=\"/home/tim/src/timberwolf-6.3/.twrc\" -DQFLOW_YOSYS_PATH=\"/home/tim/src/yosys/yosys\" -DQFLOW_ODIN_PATH=\"/home/tim/src/vtr/ODIN_II/odin_II.exe\" -DQFLOW_ABC_PATH=\"/home/tim/src/vtr/abc_with_bb_support/abc\"
LIBS = 
LDFLAGS = 
INSTALL = /usr/bin/install -c

QFLOW_LIB_DIR = /usr/local/share/qflow

TECH_DIRS = osu035 osu050

TECHINSTALL = ${QFLOW_LIB_DIR}/tech

all:
	@echo "Making all in tech directories"
	for target in $(TECH_DIRS); do \
	   (cd $$target ; $(MAKE) all) ;\
	done

install:
	@echo "Installing built-in tech files"
	$(INSTALL) -d ${TECHINSTALL}
	for target in $(TECH_DIRS); do \
	   (cd $$target ; $(MAKE) install) ;\
	done

clean:
	@echo "Cleaning up built-in tech files"
	for target in $(TECH_DIRS); do \
	   (cd $$target ; $(MAKE) clean) ;\
	done


uninstall:
	@echo "Uninstalling built-in tech files
	$(RM) -rf ${TECHINSTALL}

