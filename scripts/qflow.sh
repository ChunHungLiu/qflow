#!/bin/tcsh -f
#
#------------------------------------------------------------------
# qflow.sh --- main program shell script
#------------------------------------------------------------------

# Environment variable overrides the tech type in all cases except
# when the technology is specified on the command line by -T.  If
# the environment variable is not set, the technology defaults to
# the technology that is issued with the qflow distribution.

set tech=`printenv QFLOW_TECH`
if ( $tech == "" ) then
   set tech=osu035
endif

# Environment variable overrides the project root path in all cases
# except when the project root path is specified on the command line
# by -p.  If the environment variable does not exist, the project
# root directory is assumed to be the current working directory.

set project=`printenv QFLOW_PROJECT_ROOT`
if ( $project == "" ) then
   set project=`pwd`
endif

# Source file is not specified unless given on the command line,
# or if there is only one source file in the source directory.
set vsource=""

# Don't do anything unless told to on the command line
set actions=0
set dohelp=0
set dosynth=0
set doplace=0
set dobuffer=0
set doroute=0
set doclean=0
set dodisplay=0

while ($#argv > 0)
   switch($argv[1]:q)
      case -T:
      case --tech:
	 shift
	 set tech=$argv[1]
	 shift
	 breaksw
      case -p:
      case --project:
	 shift
	 set project=$argv[1]
	 shift
	 breaksw
      case -h:
      case --help:
	 set dohelp=1
	 shift
	 breaksw
      case synth:
      case synthesize:
	 set dosynth=1
	 set actions=1
	 shift
	 breaksw
      case place:
	 set doplace=1
	 set actions=1
	 shift
	 breaksw
      case buffer:
	 set dobuffer=1
	 set actions=1
	 shift
	 breaksw
      case route:
	 set doroute=1
	 set actions=1
	 shift
	 breaksw
      case clean:
      case cleanup:
	 set doclean=1
	 shift
	 breaksw
      case display:
	 set dodisplay=1
	 shift
	 breaksw
      default:
	 if ($vsource != "") then
	    break
	 else
	    set vsource=$argv[1]
	    shift
	 endif
	 breaksw
   endsw
end

if ($dohelp == 1 || $#argv != 0) then
   echo "Usage: qflow [processes] [options] <module_name>"
   echo "Processes:  synthesize			Synthesize verilog source"
   echo "            place			Run initial placement"
   echo "            buffer			Buffer large fanout nets"
   echo "            route			Run final placement and route"
   echo "            clean			Remove temporary working files"
   echo "            display			Display routed result"
   echo ""
   echo "Options:    -T, --tech <name>		Use technology <name>"
   echo "	     -p, --project <name>	Project root directory is <name>"
   if ($dohelp == 1) then
      exit 0
   else
      exit 1
   endif
endif

source /usr/local/share/qflow/scripts/checkdirs.sh ${tech} ${project}

if ($vsource == "") then
   if (`ls ${sourcedir}/*.v | wc -l` == 1) then
      set vsource=`ls ${sourcedir}/*.v`
   else
      echo "Error:  No verilog source file or module name has been specified"
      echo "and directory ${sourcedir} contains multiple verilog files."
      exit 1
   endif
endif

# Module name is the root name of the verilog source file.
set modulename=${vsource:r}

#------------------------------------------------------------------
# Source the technology initialization script
#------------------------------------------------------------------

if ( -x ${techdir}/${tech}.sh ) then
   source $techdir/${tech}.sh
else
   echo "Error:  Cannot find tech init script ${techdir}/${tech}.sh to source"
   exit 1
endif

#------------------------------------------------------------------
# Prepare the script file to run in the project directory.  We
# specify all steps of the process and comment out those that
# have not been selected.  Finally, source the script.
#------------------------------------------------------------------

set varfile=${projectpath}/qflow_vars.sh
set execfile=${projectpath}/qflow_exec.sh

echo "#\!/bin/tcsh" > ${varfile}
echo "#-------------------------------------------" >> ${varfile}
echo "# qflow variables for project ${project}" >> ${varfile}
echo "#-------------------------------------------" >> ${varfile}
echo "" >> ${varfile}

echo "set projectpath=${projectpath}" >> ${varfile}
echo "set techdir=${techdir}" >> ${varfile}
echo "set sourcedir=${sourcedir}" >> ${varfile}
echo "set synthdir=${synthdir}" >> ${varfile}
echo "set layoutdir=${layoutdir}" >> ${varfile}
echo "set techname=${techname}" >> ${varfile}
echo "set scriptdir=${scriptdir}" >> ${varfile}
echo "set bindir=${bindir}" >> ${varfile}
echo "set synthlog=${projectpath}/synth.log" >> ${varfile}
echo "#-------------------------------------------" >> ${varfile}
echo "" >> ${varfile}

tail -n +2 $techdir/${tech}.sh >> ${varfile}

echo "#\!/bin/tcsh" > ${execfile}
echo "#-------------------------------------------" >> ${execfile}
echo "# qflow exec script for project ${project}" >> ${execfile}
echo "#-------------------------------------------" >> ${execfile}
echo "" >> ${execfile}

if ($dosynth == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/synthesize.sh ${projectpath} ${modulename}" >> ${execfile}

if ($doplace == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/placement.sh ${projectpath} ${modulename}" >> ${execfile}

if ($dobuffer == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/resynthesize.sh ${projectpath} ${modulename}" >> ${execfile}

if ($dobuffer == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/placement.sh -d ${projectpath} ${modulename}" >> ${execfile}

if ($doroute == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/router.sh ${projectpath} ${modulename}" >> ${execfile}

if ($doclean == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/cleanup.sh ${projectpath} ${modulename}" >> ${execfile}

if ($dodisplay == 0) then
   echo -n "# " >> ${execfile}
endif
echo "${scriptdir}/display.sh ${projectpath} ${modulename}" >> ${execfile}

if ( $actions == 0 ) then
   echo "No actions specified.  Creating flow script ${execfile} only."
endif

chmod u+x ${execfile}
exec ${execfile}

exit 0
