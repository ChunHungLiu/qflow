#
# qflow project source compile makefile
#

# Main compiler arguments
CFLAGS = -g -O2
DEFS = -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"\" -DSTDC_HEADERS=1 -DHAVE_SETENV=1 -DHAVE_PUTENV=1 -DTCLSH_PATH=\"tclsh\" -DQFLOW_MAGIC_PATH=\"/usr/local/bin/magic\" -DQFLOW_QROUTER_PATH=\"/usr/local/bin/qrouter\" -DQFLOW_TWRC_PATH=\"/home/tim/src/timberwolf-6.3/.twrc\" -DQFLOW_YOSYS_PATH=\"/home/tim/src/yosys/yosys\" -DQFLOW_ODIN_PATH=\"/home/tim/src/vtr/ODIN_II/odin_II.exe\" -DQFLOW_ABC_PATH=\"/home/tim/src/vtr/abc_with_bb_support/abc\"
LIBS = 
LDFLAGS = 
INSTALL = /usr/bin/install -c

QFLOW_LIB_DIR = /usr/local/share/qflow
QFLOW_TW_PATH = /home/tim/src/timberwolf-6.3
QFLOW_QROUTER_PATH = /usr/local/bin/qrouter
QFLOW_ABC_PATH = /home/tim/src/vtr/abc_with_bb_support/abc
QFLOW_ODIN_PATH = /home/tim/src/vtr/ODIN_II/odin_II.exe
QFLOW_MAGIC_PATH = /usr/local/bin/magic
QFLOW_YOSYS_PATH = /home/tim/src/yosys/yosys

HAVE_ABC = 1
HAVE_YOSYS = 1
HAVE_ODIN_II = 1

OBJECTS = AddIO2blif.o blif2BSpice.o blif2Verilog.o blifFanout.o
OBJECTS += verilogpp.o dcombine.o liberty2tech.o
SOURCES := $(patsubst %.o,%.c,$(OBJECTS))
TARGETS := $(patsubst %.o,%$(EXEEXT),$(OBJECTS))

BININSTALL = ${QFLOW_LIB_DIR}/bin

all: $(TARGETS)

AddIO2blif$(EXEEXT): AddIO2blif.o
	$(CC) $(LDFLAGS) AddIO2blif.o -o $@ $(LIBS)

blif2BSpice$(EXEEXT): blif2BSpice.o
	$(CC) $(LDFLAGS) blif2BSpice.o -o $@ $(LIBS)

blifFanout$(EXEEXT): blifFanout.o
	$(CC) $(LDFLAGS) blifFanout.o -o $@ $(LIBS)

blif2Verilog$(EXEEXT): blif2Verilog.o
	$(CC) $(LDFLAGS) blif2Verilog.o -o $@ $(LIBS)

verilogpp$(EXEEXT): verilogpp.o
	$(CC) $(LDFLAGS) verilogpp.o -o $@ $(LIBS)

dcombine$(EXEEXT): dcombine.o
	$(CC) $(LDFLAGS) dcombine.o -o $@ $(LIBS)

liberty2tech$(EXEEXT): liberty2tech.o
	$(CC) $(LDFLAGS) liberty2tech.o -o $@ $(LIBS)

install: $(TARGETS)
	@echo "Installing verilog and BDNET file format handlers"
	$(INSTALL) -d ${BININSTALL}
	@for target in $(TARGETS); do \
	   $(INSTALL) $$target ${BININSTALL} ;\
	done
	@echo "Installing links to TimberWolf, qrouter, odin_ii, and abc"
	(cd ${BININSTALL};  $(RM) -f twdir;  ln -s $(QFLOW_TW_PATH) twdir)
	(cd ${BININSTALL};  $(RM) -f qrouter;  ln -s $(QFLOW_QROUTER_PATH) qrouter)
	@if test "${HAVE_ABC}" = "1"; then \
	(cd ${BININSTALL};  $(RM) -f abc;  ln -s $(QFLOW_ABC_PATH) abc); \
	fi
	@if test "${HAVE_ODIN_II}" = "1"; then \
	(cd ${BININSTALL};  $(RM) -f odin_ii;  ln -s $(QFLOW_ODIN_PATH) odin_ii); \
	fi
	(cd ${BININSTALL};  $(RM) -f magic;  ln -s $(QFLOW_MAGIC_PATH) magic)
	@if test "${HAVE_YOSYS}" = "1"; then \
	(cd ${BININSTALL};  $(RM) -f yosys;  ln -s $(QFLOW_YOSYS_PATH) yosys); \
	fi

uninstall:
	$(RM) -rf ${BININSTALL}

clean:
	$(RM) -f $(OBJECTS)
	$(RM) -f $(TARGETS)

veryclean:
	$(RM) -f $(OBJECTS)
	$(RM) -f $(TARGETS)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEFS) -c $< -o $@
