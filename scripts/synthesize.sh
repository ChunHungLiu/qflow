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
echo "Running verilog preprocessor" |& tee -a ${synthlog}
${bindir}/verilogpp ${rootname}.v >>& ${synthlog}

# Hack for Odin-II bug:  to be removed when bug is fixed
${scriptdir}/vmunge.tcl ${rootname}.v >>& ${synthlog}
mv ${rootname}_munge.v ${rootname}_tmp.v

#---------------------------------------------------------------------
# Create first part of the XML config file for Odin_II
#---------------------------------------------------------------------

cat > ${rootname}.xml << EOF
<config>
   <verilog_files>
	<verilog_file>${rootname}_tmp.v</verilog_file>
EOF

#---------------------------------------------------------------------
# Recursively run preprocessor on files in ${rootname}.dep
# NOTE:  This assumes a 1:1 correspondence between filenames and
# module names;  we need to search source files for the indicated
# module names instead!
#---------------------------------------------------------------------

set alldeps = `cat ${rootname}.dep`

while ( "x${alldeps}" != "x" )
    set newdeps = $alldeps
    set alldeps = ""
    foreach subname ( $newdeps )
	${bindir}/verilogpp ${subname}.v >>& ${synthlog}
	set alldeps = "${alldeps} `cat ${subname}.dep`"
	echo "      <verilog_file>${subname}_tmp.v</verilog_file>" \
		>> ${rootname}.xml
    end
end

#---------------------------------------------------------------------
# Finish the XML file
#---------------------------------------------------------------------

cat >> ${rootname}.xml << EOF
   </verilog_files>
   <output>
      <output_type>blif</output_type>
      <output_path_and_name>${rootname}.blif</output_path_and_name>
   </output>
   <optimizations>
   </optimizations>
   <debug_outputs>
   </debug_outputs>
</config>
EOF

#---------------------------------------------------------------------
# Spot check:  Did vpreproc produce file ${rootname}.init?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.init || ( -M ${rootname}.init < -M ${rootname}.v ))) then
   echo "Verilog preprocessor failure:  No file ${rootname}.init." \
		|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Run odin_ii on the verilog source to get a BLIF output file
#---------------------------------------------------------------------

echo "Running Odin_II for verilog parsing and synthesis" |& tee -a ${synthlog}
eval ${bindir}/odin_ii -U0 -c ${rootname}.xml |& tee -a ${synthlog}

#---------------------------------------------------------------------
# Check for out-of-date Odin-II version.
# NOTE:  Should get qflow's configure script to run this test at
# compile time. . .
#---------------------------------------------------------------------

set errline=`cat ${synthlog} | grep "invalid option" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Odin-II version error.  Odin-II needs updating."
   echo "Try code.google.com/p/vtr-verilog-to-routing/"
   echo "-----------------------------------------------"
   echo ""
   exit 1
endif

#---------------------------------------------------------------------
# Check for Odin-II compile-time errors
#---------------------------------------------------------------------

set errline=`cat ${synthlog} | grep "core dumped" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Odin-II core dumped:"
   echo "See file ${synthlog} for details."
   echo ""
   exit 1
endif

set errline=`cat ${synthlog} | grep "error in parsing" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Odin-II verilog preprocessor errors occurred:"
   echo "See file ${synthlog} for details."
   echo ""
   exit 1
endif

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
# Spot check:  Did Odin-II produce file ${rootname}.blif?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.blif || ( -M ${rootname}.blif < -M ${rootname}.init ))) then
   echo "Odin-II synthesis failure:  No file ${rootname}.blif." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Logic optimization with abc, using the standard "resyn2" script from
# the distribution (see file abc.rc).  Map to the standard cell set and
# write a netlist-type BLIF output file of the mapped circuit.
#---------------------------------------------------------------------

echo "Running abc for logic optimization" |& tee -a ${synthlog}
${bindir}/abc >>& ${synthlog} << EOF
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
# Spot check:  Did ABC produce file ${rootname}_mapped.blif?
#---------------------------------------------------------------------

if ( !( -f ${rootname}_mapped.blif || ( -M ${rootname}_mapped.blif \
	< -M ${rootname}.init ))) then
   echo "ABC synthesis/mapping failure:  No file ${rootname}_mapped.blif." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

set errline=`cat ${synthlog} | grep "Assertion" | grep "failed" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "ABC exited due to failure:"
   echo "See file ${synthlog} for details."
   echo ""
   exit 1
endif

#---------------------------------------------------------------------
# Odin_II appends "top^" to top-level signals, we want to remove these.
# It also replaces vector indexes with "~" which we want to recast to
# <>
#---------------------------------------------------------------------

echo "Cleaning Up blif file syntax" |& tee -a ${synthlog}

# The following definitions will replace "LOGIC0" and "LOGIC1"
# with buffers from gnd and vdd, respectively.  This takes care
# of technologies where tie-low and tie-high cells are not
# defined.

if ( "$tielo" == "") then
   set subs0a="/LOGIC0/s/O=/${bufpin_in}=gnd ${bufpin_out}=/"
   set subs0b="/LOGIC0/s/LOGIC0/${bufcell}/"
else
   set subs0a=""
   set subs0b=""
endif

if ( "$tiehi" == "") then
   set subs1a="/LOGIC1/s/O=/${bufpin_in}=vdd ${bufpin_out}=/"
   set subs1b="/LOGIC1/s/LOGIC1/${bufcell}/"
else
   set subs1a=""
   set subs1b=""
endif

cat ${rootname}_mapped.blif | sed -e "s/top^//g" \
	-e "s/~\([0-9]*\)/<\1>/g" \
	-e "$subs0a" -e "$subs0b" -e "$subs1a" -e "$subs1b" \
	> ${rootname}_tmp.blif

#---------------------------------------------------------------------
# Generate a BDNET netlist from the BLIF output, place it in synthdir
#---------------------------------------------------------------------

echo "Creating BDNET netlist" |& tee -a ${synthlog}
${bindir}/blifrtl2bdnet ${rootname}_tmp.blif > ${synthdir}/${rootname}_tmp.bdnet

#---------------------------------------------------------------------
# Spot check:  Did blifrtl2bdnet produce file ${rootname}_tmp.bdnet?
#---------------------------------------------------------------------

if ( !( -f ${synthdir}/${rootname}_tmp.bdnet || \
	( -M ${synthdir}/${rootname}_tmp.bdnet < -M ${rootname}.init ))) then
   echo "blifrtl2bdnet failure:  No file ${rootname}_tmp.bdnet." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

# Switch to synthdir for processing of the BDNET netlist
cd ${synthdir}

#---------------------------------------------------------------------
# Add initial conditions with set and reset flops
#---------------------------------------------------------------------

echo "Generating resets for register flops" |& tee -a ${synthlog}
${scriptdir}/postproc.tcl ${rootname}_tmp.bdnet \
 	${sourcedir}/${rootname}.init \
	${techdir}/${techname}.sh

echo "Restoring original names on internal DFF outputs" |& tee -a ${synthlog}
${scriptdir}/outputprep.tcl ${rootname}_tmp.bdnet ${rootname}.bdnet

#---------------------------------------------------------------------
# Spot check:  Did postproc and outputprep produce file ${rootname}.bdnet?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.bdnet || ( -M ${rootname}.bdnet \
	< -M ${rootname}_tmp.bdnet ))) then
   echo "postproc or outputprep failure:  No file ${rootname}.bdnet." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

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

echo "Running BDnetFanout (iterative)" |& tee -a ${synthlog}
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
      echo "nchanged=$nchanged" |& tee -a ${synthlog}
   end
else
   set nchanged=0
endif

#---------------------------------------------------------------------
# Spot check:  Did BDnetFanout produce an error?
#---------------------------------------------------------------------

if ( $nchanged < 0 ) then
   echo "BDnetFanout failure.  See file ${synthlog} for error messages." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

echo "" >> ${synthlog}
echo "Generating RTL verilog and SPICE netlist file in directory" \
		|& tee -a ${synthlog}
echo "	 ${synthdir}" |& tee -a ${synthlog}
echo "Files:" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtl.v" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtlnopwr.v" |& tee -a ${synthlog}
echo "   Spice:   ${synthdir}/${rootname}.spc" |& tee -a ${synthlog}
echo "" >> ${synthlog}

echo "Running BDnet2Verilog." |& tee -a ${synthlog}
${bindir}/BDnet2Verilog -v ${vddnet} -g ${gndnet} ${rootname}.bdnet \
	> ${rootname}.rtl.v

${bindir}/BDnet2Verilog -p ${rootname}.bdnet > ${rootname}.rtlnopwr.v

echo "Running BDnet2BSpice." |& tee -a ${synthlog}
${bindir}/BDnet2BSpice -p ${vddnet} -g ${gndnet} -l ${techdir}/${spicefile} \
	${rootname}.bdnet > ${rootname}.spc

#---------------------------------------------------------------------
# Spot check:  Did BDnet2Verilog or BDnet2BSpice exit with an error?
# Note that these files are not critical to the main synthesis flow,
# so if they are missing, we flag a warning but do not exit.
#---------------------------------------------------------------------

if ( !( -f ${rootname}.rtl.v || \
        ( -M ${rootname}.rtl.v < -M ${rootname}.bdnet ))) then
   echo "BDnet2Verilog failure:  No file ${rootname}.rtl.v created." \
                |& tee -a ${synthlog}
endif

if ( !( -f ${rootname}.rtlnopwr.v || \
        ( -M ${rootname}.rtlnopwr.v < -M ${rootname}.bdnet ))) then
   echo "BDnet2Verilog failure:  No file ${rootname}.rtlnopwr.v created." \
                |& tee -a ${synthlog}
endif

if ( !( -f ${rootname}.spc || \
        ( -M ${rootname}.spc < -M ${rootname}.bdnet ))) then
   echo "BDnet2BSpice failure:  No file ${rootname}.spc created." \
                |& tee -a ${synthlog}
endif

#-------------------------------------------------------------------------
# Create the .cel file for TimberWolf
#-------------------------------------------------------------------------

cd ${projectpath}

echo "Running bdnet2cel.tcl" |& tee -a ${synthlog}
${scriptdir}/bdnet2cel.tcl ${synthdir}/${rootname}.bdnet \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}.cel >>& ${synthlog}

#---------------------------------------------------------------------
# Spot check:  Did bdnet2cel produce file ${rootname}.cel?
#---------------------------------------------------------------------

if ( !( -f ${layoutdir}/${rootname}.cel || ( -M ${layoutdir}/${rootname}.cel \
	< -M ${rootname}.bdnet ))) then
   echo "bdnet2cel failure:  No file ${rootname}.cel." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

#-------------------------------------------------------------------------
# Don't overwrite an existing .par file.
#-------------------------------------------------------------------------

if (!(-f ${layoutdir}/${rootname}.par)) then
   cp ${techdir}/${techname}.par ${layoutdir}/${rootname}.par
endif

#-------------------------------------------------------------------------
# Notice about editing should be replaced with automatic handling of
# user directions about what to put in the .par file, like number of
# rows or cell aspect ratio, etc., etc.
#-------------------------------------------------------------------------
echo "Edit ${layoutdir}/${rootname}.par, then run placement and route" \
		|& tee -a ${synthlog}
