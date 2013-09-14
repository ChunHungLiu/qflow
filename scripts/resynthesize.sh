#!/bin/tcsh -f
#
# resynthesize.sh:
#-------------------------------------------------------------------------
#
# This script runs the clock tree insertion (which load-balances all
# high-fanout networks), then re-runs parts of the synthesis script to
# regenerate the netlist for placement and routing.
#
#-------------------------------------------------------------------------
# June 2009
# Tim Edwards
# MultiGiG, Inc.
# Scotts Valley, CA
#
# Updated April 2013 Tim Edwards
# Open Circuit Design
#-------------------------------------------------------------------------

if ($#argv < 2) then
   echo Usage:  resynthesize.sh [options] <project_path> <module_name>
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
   echo Usage:  resynthesize.sh [options] <project_path> <module_name>
   echo   where
   echo	      <project_path> is the name of the project directory containing
   echo			a file called qflow_vars.sh.
   echo	      <module_name> is the root name of the verilog file, and
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

touch ${synthlog}

#---------------------------------------------------------------------
# Ensure that there is a .pin file in the layout directory.  Run the
# "clock tree insertion tool" (which actually buffers all high-fanout
# nets, not just the clock).
#---------------------------------------------------------------------

if (! ${?clocktree_options}) then
   set clocktree_options = ""
endif

if (-f ${layoutdir}/${rootname}.pin ) then
   echo "Running clocktree"
   echo "" |& tee -a ${synthlog}
   ${scriptdir}/clocktree.tcl ${rootname} ${synthdir} \
		${layoutdir} ${techdir}/${leffile} ${bufcell} \
		${clocktree_options} >> ${synthlog}
else
   echo "Error:  No pin file ${layoutdir}/${rootname}.pin." |& tee -a ${synthlog}
   echo "Did you run initial_placement.sh on this design?" |& tee -a ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Spot check:  Did clocktree produce file ${rootname}.cel?
#---------------------------------------------------------------------

if ( !( -f ${synthdir}/${rootname}_tmp.bdnet || \
	( -M ${synthdir}/${rootname}_tmp.bdnet \
        < -M ${layoutdir}/${rootname}.cel ))) then
   echo "clocktree failure:  No file ${rootname}_tmp.bdnet." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

# Make a backup of the original .bdnet file, then copy the modified
# one over it.

cd ${synthdir}
cp ${rootname}.bdnet ${rootname}_orig.bdnet
cp ${rootname}_tmp.bdnet ${rootname}.bdnet
rm -f ${rootname}_tmp.bdnet

#---------------------------------------------------------------------
# Check all gates for fanout load, and adjust gate strengths as
# necessary.  Iterate this step until all gates satisfy drive
# requirements.
#
# Use option "-o value" to force a value for the (maximum expected)
# output load, in fF.  Currently, there is no way to do this from the
# command line (default is 18fF). . .
#---------------------------------------------------------------------

if (! ${?fanout_options} ) then
   set fanout_options = "-l 75 -c 25"
endif

echo "Running BDnetFanout (iterative)" |& tee -a ${synthlog}
echo "" |& tee -a ${synthlog}
if (-f ${techdir}/gate.cfg && -f ${bindir}/BDnetFanout ) then
   set nchanged=1000
   while ($nchanged > 0)
      mv ${rootname}.bdnet tmp.bdnet
      ${bindir}/BDnetFanout ${fanout_options} -f ${rootname}_nofanout \
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
# Spot check:  Did BDnetFanout exit with an error?
#---------------------------------------------------------------------

if ( $nchanged < 0 ) then
   echo "BDnetFanout failure:  See ${synthlog} for error messages"
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

echo "" |& tee -a ${synthlog}
echo "Generating RTL verilog and SPICE netlist file in directory" \
	|& tee -a ${synthlog}
echo "   ${synthdir}" |& tee -a ${synthlog}
echo "Files:" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtl.v" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtlnopwr.v" |& tee -a ${synthlog}
echo "   Spice:   ${synthdir}/${rootname}.spc" |& tee -a ${synthlog}
echo "" |& tee -a ${synthlog}

echo "Running BDnet2Verilog." |& tee -a ${synthlog}
${bindir}/BDnet2Verilog -v ${vddnet} -g ${gndnet} ${rootname}.bdnet \
	> ${rootname}.rtl.v

${bindir}/BDnet2Verilog -p ${rootname}.bdnet > ${rootname}.rtlnopwr.v

echo "Running BDnet2BSpice." |& tee -a ${synthlog}
${bindir}/BDnet2BSpice ${rootname}.bdnet -p ${vddnet} -g ${gndnet} \
	-l ${techdir}/${spicefile} > ${rootname}.spc

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
# Clean up after myself!
#-------------------------------------------------------------------------

cd ${projectpath}

rm -f ${synthdir}/${rootname}_tmp.bdnet >& /dev/null
rm -f ${synthdir}/${rootname}_tmp_tmp.bdnet >& /dev/null
rm -f ${synthdir}/tmp.bdnet >& /dev/null
rm -f ${sourcedir}/${rootname}.bdnet

#-------------------------------------------------------------------------
# Regenerate the .cel file for TimberWolf
# (Assume that the .par file was already made the first time through)
#-------------------------------------------------------------------------

echo "Running bdnet2cel.tcl" |& tee -a ${synthlog}
${scriptdir}/bdnet2cel.tcl ${synthdir}/${rootname}.bdnet \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}.cel >>& ${synthlog}

#---------------------------------------------------------------------
# Spot check:  Did bdnet2cel produce file ${rootname}.cel?
#---------------------------------------------------------------------

if ( !( -f ${layoutdir}/${rootname}.cel || \
	( -M ${layoutdir}/${rootname}.cel \
        < -M ${synthdir}/${rootname}.bdenet ))) then
   echo "bdnet2cel failure:  No file ${rootname}.cel." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------

echo "Now re-run placement, and route" |& tee -a ${synthlog}
