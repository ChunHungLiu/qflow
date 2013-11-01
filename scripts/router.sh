#!/bin/tcsh -f
#----------------------------------------------------------
# Route script using qrouter
#----------------------------------------------------------
# Tim Edwards, 5/16/11, for Open Circuit Design
# Modified April 2013 for use with qflow
#----------------------------------------------------------

if ($#argv < 2) then
   echo Usage:  router.sh [options] <project_path> <source_name>
   exit 1
endif

# Split out options from the main arguments
set argline=(`getopt "nr" $argv[1-]`)

set options=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $1}'`
set cmdargs=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $2}'`
set argc=`echo $cmdargs | wc -w`

if ($argc == 2) then
   set argv1=`echo $cmdargs | cut -d' ' -f1`
   set argv2=`echo $cmdargs | cut -d' ' -f2`
else
   echo Usage:  router.sh [options] <project_path> <source_name>
   echo   where
   echo       <project_path> is the name of the project directory containing
   echo                 a file called qflow_vars.sh.
   echo       <source_name> is the root name of the verilog file
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

if (! ${?qrouter_options} ) then
   set qrouter_options = ${options}
endif

#----------------------------------------------------------
# Done with initialization
#----------------------------------------------------------

cd ${layoutdir}

#------------------------------------------------------------------
# Create the detailed route.  Monitor the output and print errors
# to the output, as well as writing the "commit" line for every
# 100th route, so the end-user can track the progress.
#------------------------------------------------------------------

echo "Running qrouter"
${bindir}/qrouter -c ${rootname}.cfg -p ${vddnet} -g ${gndnet} \
		${qrouter_options} ${rootname} |& tee -a ${synthlog} | \
		grep - -e fail -e Progess -e TotalRoutes.\*00\$

#---------------------------------------------------------------------
# Spot check:  Did qrouter produce file ${rootname}_route.def?
#---------------------------------------------------------------------

if ( !( -f ${rootname}_route.def || ( -M ${rootname}_route.def \
		< -M ${rootname}.def ))) then
   echo "qrouter failure:  No file ${rootname}_route.def." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   exit 1
endif

mv ${rootname}.def ${rootname}_unroute.def
mv ${rootname}_route.def ${rootname}.def

#------------------------------------------------------------
# Done!
#------------------------------------------------------------
