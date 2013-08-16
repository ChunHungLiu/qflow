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
set argline=`getopt "c:b:f:v:nx" $argv[1-]`

# Corrected 9/9/08; quotes must be added or "-n" disappears with "echo".
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
   echo	  options:
   echo			Options are passed to AddIOtoBDNet
   exit 1
endif

set projectpath=$argv1
set sourcename=$argv2
set rootname=${sourcename:h}

set options=""
eval set argv=\($argline:q\)
while ($#argv > 0)
   switch (${1:q})
      case --:
         break
      default:
	 set options="$options ${1:q}"
	 breaksw
   endsw
   shift
end

#---------------------------------------------------------------------
# This script is called with the first argument <project_path>, which should
# have file "qflow_vars.sh".  Get all of our standard variable definitions
# from the qflow_vars.sh file.
#---------------------------------------------------------------------

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

#---------------------------------------------------------------------
# Preprocessor removes reset value initialization blocks
# from the verilog source and saves the initial states in
# a ".init" file for post-processing
#---------------------------------------------------------------------

cd ${sourcedir}
${bindir}/vpreproc ${rootname}.v

# Hack for Odin-II bug:  to be removed when bug is fixed
${scriptdir}/vmunge.tcl ${rootname}.v
mv ${rootname}_munge.v ${rootname}_tmp.v

#---------------------------------------------------------------------
# Run odin_ii on the verilog source to get a BLIF output file
#---------------------------------------------------------------------

echo "Running Odin_II for verilog parsing and synthesis"
eval ${bindir}/odin_ii -U0 -V ${rootname}_tmp.v -o ${rootname}.blif >>& ${synthlog}

#---------------------------------------------------------------------
# Check for Odin-II compile-time errors
#---------------------------------------------------------------------

set errline=`cat ${synthlog} | grep "Odin has decided you have failed" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Verilog compile errors occurred:"
   echo "See file ${synthlog} for details."
   echo "----------------------------------"
   cat ${synthlog} | grep "^line"
   echo ""
   exit 1
endif

#---------------------------------------------------------------------
# Logic optimization with abc, using the standard "resyn2" script from
# the distribution (see file abc.rc).  Map to the standard cell set and
# write a netlist-type BLIF output file of the mapped circuit.
#---------------------------------------------------------------------

echo "Running abc for logic optimization"
${bindir}/abc >> ${synthlog} << EOF
read_blif ${rootname}.blif
read_library ${techdir}/${techname}.genlib
read_super ${techdir}/${techname}.super
balance; rewrite; refactor; balance; rewrite; rewrite -z
balance; refactor -z; rewrite -z; balance
map
write_blif ${rootname}_mapped.blif
quit
EOF

#---------------------------------------------------------------------
# Odin_II appends "top^" to top-level signals, we want to remove these.
# It also replaces vector indexes with "~" which we want to recast to
# <>
#---------------------------------------------------------------------

echo "Cleaning Up blif file syntax"
cat ${rootname}_mapped.blif | sed -e "s/top^//g" \
	-e "s/~\([0-9]*\)/<\1>/g" \
	> ${rootname}_tmp.blif

endif

#---------------------------------------------------------------------
# Generate a BDNET netlist from the BLIF output, place it in synthdir
#---------------------------------------------------------------------

echo "Creating BDNET netlist"
${bindir}/blifrtl2bdnet ${rootname}_tmp.blif > ${synthdir}/${rootname}_tmp.bdnet

# Switch to synthdir for processing of the BDNET netlist
cd ${synthdir}

#---------------------------------------------------------------------
# Add initial conditions with set and reset flops
#---------------------------------------------------------------------

echo "Generating resets for register flops"
${scriptdir}/postproc.tcl ${rootname}_tmp.bdnet \
 	${sourcedir}/${rootname}.init \
	${techdir}/${techname}.sh

echo "Restoring original names on internal DFF outputs"
${scriptdir}/outputprep.tcl ${rootname}_tmp.bdnet ${rootname}.bdnet

#---------------------------------------------------------------------
# NOTE:  To be restored, want to handle specific user instructions
# to either double-buffer the outputs or to latch or double-latch
# the inputs (asynchronous ones, specifically)
#---------------------------------------------------------------------
# echo "Running AddIO2BDnet"
# ${bindir}/AddIO2BDnet -t ${techdir}/${techname}.genlib \
# 	-b ${bufcell} -f ${flopcell} $options \
# 	${rootname}_tmp.bdnet > ${rootname}_buf.bdnet

# Make a copy of the original bdnet file, as this will be overwritten
# by the fanout handling process
cp ${rootname}.bdnet ${rootname}_bak.bdnet

#---------------------------------------------------------------------
# Check all gates for fanout load, and adjust gate strengths as
# necessary.  Iterate this step until all gates satisfy drive
# requirements.
#
# Use option "-o value" to force a value for the (maximum expected)
# output load, in fF.  Currently, there is no way to do this from the
# command line (default is 18fF). . .
#---------------------------------------------------------------------

rm -f ${rootname}_nofanout
touch ${rootname}_nofanout
if ($?gndnet) then
   echo $gndnet >> ${rootname}_nofanout
endif
if ($?vddnet) then
   echo $vddnet >> ${rootname}_nofanout
endif

echo "Running BDnetFanout (iterative)"
echo "" >> ${synthlog}
if (-f ${techdir}/gate.cfg && -f ${bindir}/BDnetFanout ) then
   set nchanged=1000
   while ($nchanged > 0)
      mv ${rootname}.bdnet tmp.bdnet
      ${bindir}/BDnetFanout -l 75 -c 25 -f ${rootname}_nofanout \
		-p ${techdir}/gate.cfg -s ${separator} \
		-b ${bufcell} -i ${bufpin_in} -o ${bufpin_out} \
		tmp.bdnet ${rootname}.bdnet >>& ${synthlog}
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
${bindir}/BDnet2Verilog -v ${vddnet} -g ${gndnet} ${rootname}.bdnet \
	> ${rootname}.rtl.v

${bindir}/BDnet2Verilog -p ${rootname}.bdnet > ${rootname}.rtlnopwr.v

echo "Running BDnet2BSpice."
${bindir}/BDnet2BSpice -p ${vddnet} -g ${gndnet} -l ${techdir}/${spicefile} \
	${rootname}.bdnet > ${rootname}.spc

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
${scriptdir}/bdnet2cel.tcl ${synthdir}/${rootname}.bdnet \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}.cel

# Don't overwrite an existing .par file.
if (!(-f ${layoutdir}/${rootname}.par)) then
   cp ${techdir}/${techname}.par ${layoutdir}/${rootname}.par
endif

#-------------------------------------------------------------------------
# Notice about editing should be replaced with automatic handling of
# user directions about what to put in the .par file, like number of
# rows or cell aspect ratio, etc., etc.
#-------------------------------------------------------------------------
echo "Edit ${layoutdir}/${rootname}.par, then run placement and route"
