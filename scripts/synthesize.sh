#!/bin/tcsh -f
#
# synthesize.sh:
#-------------------------------------------------------------------------
#
# This script synthesizes verilog files for qflow using VIS and SIS,
# and various preprocessors to handle verilog syntax that is beyond
# the capabilities of the simple VIS/SIS synthesis to handle.
#
#-------------------------------------------------------------------------
# November 2006
# Steve Beccue and Tim Edwards
# MultiGiG, Inc.
# Scotts Valley, CA
# Updated 2013 Tim Edwards
# Open Circuit Design
#-------------------------------------------------------------------------

if ($#argv < 2) then
   echo Usage:  synthesize.sh [options] <project_path> <source_name>
   exit 1
endif

# Split out options from the main arguments
set argline=`getopt "c:b:f:nx" $argv[1-]`

# Corrected 9/9/08; quotes must be added or "-n" disappears with "echo".
set options=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $1}'`
set cmdargs=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $2}'`
set argc=`echo $cmdargs | wc -w`

if ($argc == 2) then
   set argv1=`echo $cmdargs | cut -d' ' -f1`
   set argv2=`echo $cmdargs | cut -d' ' -f2`
else
   echo Usage:  synthesize.sh [options] <project_path> <source_name>
   echo   where
   echo	      <project_path> is the name of the project directory containing
   echo			a file called qflow_vars.sh.
   echo	      <source_name> is the root name of the verilog file, and
   echo	      [options] are passed verbatim to the AddIOToBDNet program.
   exit 1
endif

set projectpath=$argv1
set sourcename=$argv2
set rootname=${sourcename:h}

# This script is called with the first argument <project_path>, which should
# have file "qflow_vars.sh".  Get all of our standard variable definitions
# from the qflow_vars.sh file.

if (! -f ${projectpath}/qflow_vars.sh ) then
   echo "Error:  Cannot find file qflow_vars.sh in path ${projectpath}"
   exit 1
endif

source ${projectpath}/qflow_vars.sh
source ${techdir}/${techname}.sh
cd ${projectpath}

# Reset the logfile
rm -f ${synthlog} >& /dev/null
touch ${synthlog}

# Preprocessor removes "initial" blocks from the verilog
# source and saves the initial states in a ".init" file
# for post-processing

cd ${sourcedir}
${bindir}/vpreproc ${rootname}.v

# Stuff ideosyncratic to VIS.  We will force syntax
# that VIS can handle, and write it back to the same
# filename as was created by vpreproc

${scriptdir}/vispreproc.tcl ${rootname}_tmp.v
if ( -f ${rootname}_tmp_vis.v ) then
   mv ${rootname}_tmp_vis.v ${rootname}_tmp.v
endif

${bindir}/vis >>& ${synthlog} << EOF
read_verilog ${rootname}_tmp.v
write_blif ${rootname}.blif
quit
EOF

# /home/tim/src/odin_ii/ODIN_II/odin_II.exe \
#	-V ${sourcedir}/${rootname}.v -o ${sourcedir}/${rootname}.blif >>& ${synthlog}

#---------------------------------------------------------------------
# Check for verilog compile-time errors
#---------------------------------------------------------------------

set errline=`cat ${synthlog} | grep "No file has been read in" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Verilog compile errors occurred:"
   echo "See file ${synthlog} for details."
   echo "----------------------------------"
   cat ${synthlog} | grep "^line"
   echo ""
   exit 1
endif

${bindir}/sis >> ${synthlog} << EOF
read_blif ${rootname}.blif
read_library ${techdir}/${techname}.genlib

sweep; eliminate -1
simplify -m nocomp
eliminate -1

sweep; eliminate 5
simplify -m nocomp
resub -a

fx
resub -a; sweep

eliminate -1; sweep
full_simplify -m nocomp

map -n 0 -s
write_bdnet ${rootname}.bdnet
quit
EOF

echo ""
echo "Please check ${sourcedir}/${rootname}.bdnet with a text editor."
echo "Right column numbers must be unique."
echo ""

#---------------------------------------------------------------------
# Check whether any clocks are used before automatically inserting one...
#---------------------------------------------------------------------

set clkpins=`cat ${rootname}.bdnet | grep -c UNCONNECTED`

if ( "$clkpins" > 0 ) then

    # force spaces inside the parentheses, then count by field to get clock name
    set clkname=`cat ${rootname}.v | grep always | grep posedge |\
	sed -e '/)/s/)/ )/' -e '/(/s/(/( /' -e '/ /s/  / /g' | cut -d' ' -f 4`

    cat ${rootname}.bdnet |\
	sed -e /UNCONNECTED/s/UNCONNECTED/\"$clkname\"/ |\
	sed -e '/^INPUT/a\\
	"@@" : "@@"' |\
	sed -e /@@/s/@@/$clkname/g > ${synthdir}/${rootname}.bdnet
else
    cp ${rootname}.bdnet ${synthdir}/${rootname}.bdnet
endif

# Switch to synthdir for processing of the BDNET netlist
cd ${synthdir}

# NOTE:  CleanUpBDnet is only to be used with SIS/VIS flow.
# Other flows create their own unique problems needing cleanup.

echo "Running CleanUpBDnet"
${bindir}/CleanUpBDnet -f -b ${rootname}.bdnet > ${rootname}_tmp.bdnet
#cp ${synthdir}/${rootname}.bdnet ${synthdir}/${rootname}_tmp.bdnet

#---------------------------------------------------------------------
# Add initial conditions with set and reset flops
#---------------------------------------------------------------------

echo "Running postproc"
${scriptdir}/postproc.tcl ${rootname}_tmp.bdnet \
 	${sourcedir}/${rootname}.init \
	${techdir}/${techname}.sh

echo "Running AddIO2BDnet"
${bindir}/AddIO2BDnet -t ${techdir}/${techname}.genlib \
	-b ${bufcell} -f ${flopcell} $options \
	${rootname}_tmp.bdnet > ${rootname}_buf.bdnet

#---------------------------------------------------------------------
# Check all gates for fanout load, and adjust gate strengths as
# necessary.  Iterate this step until all gates satisfy drive
# requirements.
#
# Use option "-o value" to force a value for the (maximum expected)
# output load, in fF.  Currently, there is no way to do this from the
# command line (default is 18fF). . .
#---------------------------------------------------------------------

rm -f ${rootname}_buf_nofanout
touch ${rootname}_buf_nofanout
if ($?gndnet) then
   echo $gndnet >> ${rootname}_buf_nofanout
endif
if ($?vddnet) then
   echo $vddnet >> ${rootname}_buf_nofanout
endif

echo "Running BDnetFanout (iterative)"
echo "" >> ${synthlog}
if (-f ${techdir}/gate.cfg && -f ${bindir}/BDnetFanout ) then
   set nchanged=1000
   while ($nchanged > 0)
      mv ${rootname}_buf.bdnet tmp.bdnet
      ${bindir}/BDnetFanout -l 75 -c 25 -f ${rootname}_buf_nofanout \
		-p ${techdir}/gate.cfg -s ${separator} \
		-b ${bufcell} -i ${bufpin_in} -o ${bufpin_out} \
		tmp.bdnet ${rootname}_buf.bdnet >>& ${synthlog}
      set nchanged=$status
      echo "nchanged=$nchanged"
   end
endif

echo ""
echo "Generating RTL verilog and SPICE netlist file in directory ${synthdir}"
echo "Files:"
echo "   Verilog: ${synthdir}/${rootname}.rtl.v"
echo "   Verilog: ${synthdir}/${rootname}.rtlnopwr.v"
echo ""

echo "Running BDnet2Verilog."
${bindir}/BDnet2Verilog -v ${vddnet} -g ${gndnet} ${rootname}_buf.bdnet \
	> ${rootname}.rtl.v

${bindir}/BDnet2Verilog -p ${rootname}_buf.bdnet > ${rootname}.rtlnopwr.v

echo "Running BDnet2BSpice."
${bindir}/BDnet2BSpice ${rootname}_buf.bdnet > ${rootname}_buf.spc

#-------------------------------------------------------------------------
# Clean up after myself!
#-------------------------------------------------------------------------

cd ${projectpath}

# rm -f *.enc >& /dev/null
# rm -f /tmp/vis.* >& /dev/null
# rm -f ${synthdir}/${rootname}_tmp.bdnet >& /dev/null
# rm -f ${synthdir}/${rootname}_tmp_tmp.bdnet >& /dev/null
# rm -f ${synthdir}/tmp.bdnet >& /dev/null
# rm -f ${sourcedir}/${rootname}.bdnet
# rm -f ${sourcedir}/${rootname}_tmp.v
# rm -f ${sourcedir}/${rootname}.init
# rm -f ${sourcedir}/${rootname}.blif

#-------------------------------------------------------------------------
# Create the .cel file for TimberWolf
#-------------------------------------------------------------------------

echo "Running bdnet2cel.tcl"
${scriptdir}/bdnet2cel.tcl ${synthdir}/${rootname}_buf.bdnet \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}_buf.cel

# Don't overwrite an existing .par file.
if (!(-f ${layoutdir}/${rootname}_buf.par)) then
   cp ${techdir}/${techname}.par ${layoutdir}/${rootname}_buf.par
endif

echo "Edit ${layoutdir}/${rootname}_buf.par, then run placement and route"
