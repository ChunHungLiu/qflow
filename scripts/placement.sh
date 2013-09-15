#!/bin/tcsh -f
#----------------------------------------------------------
# Placement script using TimberWolf
#
# This script assumes the existence of the pre-TimberWolf
# ".cel" and ".par" files.  It will run TimberWolf for the
# placement.
#----------------------------------------------------------
# Tim Edwards, 5/16/11, for Open Circuit Design
# Modified April 2013 for use with qflow
#----------------------------------------------------------

if ($#argv < 2) then
   echo Usage:  placement.sh <project_path> <source_name>
   exit 1
endif

# Split out options from the main arguments
set argline=(`getopt "kd" $argv[1-]`)
set cmdargs=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $2}'`
set argc=`echo $cmdargs | wc -w`

if ($argc == 2) then
   set argv1=`echo $cmdargs | cut -d' ' -f1`
   set argv2=`echo $cmdargs | cut -d' ' -f2`
else
   echo Usage:  placement.sh [options] <project_path> <source_name>
   echo   where
   echo       <project_path> is the name of the project directory containing
   echo                 a file called qflow_vars.sh.
   echo       <source_name> is the root name of the verilog file, and
   echo       [options] are:
   echo			-k	keep working files
   echo			-d	generate DEF file for routing
   echo
   echo	  Options to specific tools can be specified with the following
   echo	  variables in project_vars.sh:
   echo
   echo
   exit 1
endif

set keep=0
set makedef=0

foreach option (${argline})
   switch (${option})
      case -k:
         set keep=1
         breaksw
      case -d:
         set makedef=1
         breaksw
      case --:
         break
   endsw
end

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

#----------------------------------------------------------
# Done with initialization
#----------------------------------------------------------

cd ${layoutdir}

# Check if a .cel2 file exists and needs to be appended to .cel
# If the .cel2 file is newer than .cel, then truncate .cel and
# re-append.

if ( -f ${rootname}.cel2 ) then
   echo "Preparing pin placement hints from ${rootname}.cel2" |& tee -a ${synthlog}
   if ( `grep -c padgroup ${rootname}.cel` == "0" ) then
      cat ${rootname}.cel2 >> ${rootname}.cel
   else if ( -M ${rootname}.cel2 > -M ${rootname}.cel ) then
      # Truncate .cel file to first line containing "padgroup"
      cat ${rootname}.cel | sed -e "/padgroup/Q" > ${rootname}_tmp.cel
      cat ${rootname}_tmp.cel ${rootname}.cel2 > ${rootname}.cel
      rm -f ${rootname}_tmp.cel
   endif
else
   echo -n "No ${rootname}.cel2 file found for project. . . " \
		|& tee -a ${synthlog}
   echo "continuing without pin placement hints" |& tee -a ${synthlog}
endif

#-----------------------------------------------
# 1) Run TimberWolf
#-----------------------------------------------

echo "Running TimberWolf placement" |& tee -a ${synthlog}
( pushd ${bindir}/twdir > /dev/null ;\
  source .twrc >>& ${synthlog} ;\
  popd > /dev/null ;\
  TimberWolf $rootname >>& ${synthlog} )

#---------------------------------------------------------------------
# Spot check:  Did TimberWolf produce file ${rootname}.pin?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.pin || ( -M ${rootname}.pin < -M ${rootname}.cel ))) then
   echo "TimberWolf failure:  No file ${rootname}.pin." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

if (! ${?via_stacks} ) then
   set via_stacks = 2
endif

#---------------------------------------------------
# 2) Prepare DEF and .cfg files for qrouter
#---------------------------------------------------

if ($makedef == 1) then
   if ( "$techleffile" == "" ) then
      ${scriptdir}/place2def.tcl $rootname ${bindir}/qrouter \
                $fillcell $via_stacks ${techdir}/$leffile >>& ${synthlog}
   else
      ${scriptdir}/place2def.tcl $rootname ${bindir}/qrouter \
		$via_stacks $fillcell ${techdir}/$techleffile \
		${techdir}/$leffile >>& ${synthlog}
   endif
endif

#---------------------------------------------------------------------
# Spot check:  Did place2def produce file ${rootname}.def?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.def || ( -M ${rootname}.def < -M ${rootname}.pin ))) then
   echo "place2def failure:  No file ${rootname}.def." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

#---------------------------------------------------
# 3) Add spacer cells to create a straight border on
#    the right side
#---------------------------------------------------

if ($makedef == 1) then
   if ( -f ${scriptdir}/addspacers.tcl ) then
      echo "Running addspacers.tcl"
      ${scriptdir}/addspacers.tcl ${rootname} ${techdir}/$leffile \
		$fillcell >>& ${synthlog}
      if ( -f ${rootname}_filled.def ) then
	 mv ${rootname}_filled.def ${rootname}.def
      endif
   endif
endif

#---------------------------------------------------
# 4) Remove working files (except for the main
#    output files .pin, .pl1, and .pl2
#---------------------------------------------------

if ($keep == 0) then
   rm -f ${rootname}.blk ${rootname}.gen ${rootname}.gsav ${rootname}.history
   rm -f ${rootname}.log ${rootname}.mcel ${rootname}.mdat ${rootname}.mgeo
   rm -f ${rootname}.mout ${rootname}.mpin ${rootname}.mpth ${rootname}.msav
   rm -f ${rootname}.mver ${rootname}.mvio ${rootname}.stat ${rootname}.out
   rm -f ${rootname}.pth ${rootname}.sav ${rootname}.scel ${rootname}.txt
endif

#------------------------------------------------------------
# Done!
#------------------------------------------------------------
