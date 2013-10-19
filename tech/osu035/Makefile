#
# qflow project included technology osu035 files
#

# Main compiler arguments
CFLAGS = -g -O2
DEFS = -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"\" -DSTDC_HEADERS=1 -DHAVE_SETENV=1 -DHAVE_PUTENV=1 -DTCLSH_PATH=\"tclsh\" -DQFLOW_MAGIC_PATH=\"/usr/local/bin/magic\" -DQFLOW_QROUTER_PATH=\"/usr/local/bin/qrouter\" -DQFLOW_TWRC_PATH=\"/home/tim/src/timberwolf-6.3/.twrc\" -DQFLOW_YOSYS_PATH=\"/home/tim/src/yosys/yosys\" -DQFLOW_ODIN_PATH=\"/home/tim/src/vtr/ODIN_II/odin_II.exe\" -DQFLOW_ABC_PATH=\"/home/tim/src/vtr/abc_with_bb_support/abc\"
LIBS = 
LDFLAGS = 
INSTALL = /usr/bin/install -c

QFLOW_LIB_DIR = /usr/local/share/qflow

TECH_FILES = osu035.genlib osu035.super osu035.par osu035_stdcells.lef
TECH_FILES += osu035.sh osu035_stdcells.sp osu035.magicrc osu035.prm
TECH_FILES += gate.cfg SCN4M_SUBM.20.tech osu035_stdcells.lib

TECHINSTALL = ${QFLOW_LIB_DIR}/tech

# Substitute the target qflow tech directory name in .magicrc so that magic
# can find the OSU035 techfile

all: osu035.magicrc.in
	$(RM) -f osu035.magicrc
	cat osu035.magicrc.in | sed -e \
		'/QFLOW_LIB_DIR/s#QFLOW_LIB_DIR#$(QFLOW_LIB_DIR)#' \
		> osu035.magicrc

install: ${TECH_FILES}
	@echo "Installing osu035 tech files"
	$(INSTALL) -d $(TECHINSTALL)/osu035
	for target in $(TECH_FILES); do \
	   $(INSTALL) $$target $(TECHINSTALL)/osu035 ;\
	done

clean:
	$(RM) -f osu035.magicrc

uninstall:

