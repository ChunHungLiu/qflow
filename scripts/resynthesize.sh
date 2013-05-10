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

if (-f ${layoutdir}/${rootname}_buf.pin ) then
   echo "" >> synth.log
   ${scriptdir}/clocktree.tcl ${rootname}_buf ${synthdir} \
		${layoutdir} ${techdir}/${leffile} ${bufcell}
else
   echo "Error:  No pin file ${layoutdir}/${rootname}_buf.pin."
   echo "Did you run initial_placement.sh on this design?"
   exit 1
endif

if (!(-f ${synthdir}/${rootname}_buf_tmp.bdnet)) then
   echo "Error in clocktree.tcl:  No modified netlist was created."
   exit 1
endif

# Make a backup of the original .bdnet file, then copy the modified
# one over it.

cd ${synthdir}
cp ${rootname}_buf.bdnet ${rootname}_buf_orig.bdnet
cp ${rootname}_buf_tmp.bdnet ${rootname}_buf.bdnet
rm -f ${rootname}_buf_tmp.bdnet

#---------------------------------------------------------------------
# Check all gates for fanout load, and adjust gate strengths as
# necessary.  Iterate this step until all gates satisfy drive
# requirements.
#
# Use option "-o value" to force a value for the (maximum expected)
# output load, in fF.  Currently, there is no way to do this from the
# command line (default is 18fF). . .
#---------------------------------------------------------------------

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
${bindir}/BDnet2BSpice ${rootname}_buf.bdnet -p ${vddnet} -g ${gndnet} \
	-l ${techdir}/${spicefile} > ${rootname}_buf.spc

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

echo "Running bdnet2cel.tcl"
${scriptdir}/bdnet2cel.tcl ${synthdir}/${rootname}_buf.bdnet \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}_buf.cel

echo "Now re-run placement, and route"
