#
# qflow project source compile makefile
#

# Main compiler arguments
CFLAGS = -g -O2
DEFS = -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"\" -DSTDC_HEADERS=1 -DHAVE_SETENV=1 -DHAVE_PUTENV=1 -DTCLSH_PATH=\"tclsh\" -DQFLOW_MAGIC_PATH=\"/usr/local/bin/magic\" -DQFLOW_QROUTER_PATH=\"/usr/local/bin/qrouter\" -DQFLOW_TWRC_PATH=\"/home/tim/src/timberwolf-6.3/.twrc\" -DQFLOW_ODIN_PATH=\"/home/tim/src/vtr/ODIN_II/odin_II.exe\" -DQFLOW_ABC_PATH=\"/home/tim/src/vtr/abc_with_bb_support/abc\"
LIBS = 
LDFLAGS = 
INSTALL = /usr/bin/install -c

QFLOW_LIB_DIR = /usr/local/share/qflow
QFLOW_TW_PATH = /home/tim/src/timberwolf-6.3
QFLOW_QROUTER_PATH = /usr/local/bin/qrouter
QFLOW_ABC_PATH = /home/tim/src/vtr/abc_with_bb_support/abc
QFLOW_ODIN_PATH = /home/tim/src/vtr/ODIN_II/odin_II.exe
QFLOW_MAGIC_PATH = /usr/local/bin/magic

OBJECTS = AddIO2BDnet.o BDnet2BSpice.o BDnet2Verilog.o CleanUpBDnet.o BDnetFanout.o
OBJECTS += vpreproc.o dcombine.o vsplit.o blifrtl2bdnet.o
SOURCES := $(patsubst %.o,%.c,$(OBJECTS))
TARGETS := $(patsubst %.o,%$(EXEEXT),$(OBJECTS))

BININSTALL = ${QFLOW_LIB_DIR}/bin

all: $(TARGETS)

AddIO2BDnet$(EXEEXT): AddIO2BDnet.o
	$(CC) $(LDFLAGS) AddIO2BDnet.o -o $@ $(LIBS)

BDnet2BSpice$(EXEEXT): BDnet2BSpice.o
	$(CC) $(LDFLAGS) BDnet2BSpice.o -o $@ $(LIBS)

BDnet2Verilog$(EXEEXT): BDnet2Verilog.o
	$(CC) $(LDFLAGS) BDnet2Verilog.o -o $@ $(LIBS)

CleanUpBDnet$(EXEEXT): CleanUpBDnet.o
	$(CC) $(LDFLAGS) CleanUpBDnet.o -o $@ $(LIBS)

vpreproc$(EXEEXT): vpreproc.o
	$(CC) $(LDFLAGS) vpreproc.o -o $@ $(LIBS)

dcombine$(EXEEXT): dcombine.o
	$(CC) $(LDFLAGS) dcombine.o -o $@ $(LIBS)

vsplit$(EXEEXT): vsplit.o
	$(CC) $(LDFLAGS) vsplit.o -o $@ $(LIBS)

blifrtl2bdnet$(EXEEXT): blifrtl2bdnet.o
	$(CC) $(LDFLAGS) blifrtl2bdnet.o -o $@ $(LIBS)

install: $(TARGETS)
	@echo "Installing verilog and BDNET file format handlers"
	$(INSTALL) -d ${BININSTALL}
	@for target in $(TARGETS); do \
	   $(INSTALL) $$target ${BININSTALL} ;\
	done
	@echo "Installing links to TimberWolf, qrouter, odin_ii, and abc"
	(cd ${BININSTALL};  $(RM) -f twdir;  ln -s $(QFLOW_TW_PATH) twdir)
	(cd ${BININSTALL};  $(RM) -f qrouter;  ln -s $(QFLOW_QROUTER_PATH) qrouter)
	(cd ${BININSTALL};  $(RM) -f abc;  ln -s $(QFLOW_ABC_PATH) abc)
	(cd ${BININSTALL};  $(RM) -f odin_ii;  ln -s $(QFLOW_ODIN_PATH) odin_ii)
	(cd ${BININSTALL};  $(RM) -f magic;  ln -s $(QFLOW_MAGIC_PATH) magic)

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
