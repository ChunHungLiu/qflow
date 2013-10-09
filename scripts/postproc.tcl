#!/usr/bin/tclsh
#-------------------------------------------------------------------------
# postproc --- post-process a mapped .blif file with the contents of the
# "init" file produced by "vpreproc".  Each signal from the "init"
# file is tracked down as the output to a flop, and that flop is replaced
# by a set or reset flop accordingly.  Information about cells to use
# and their pins, etc., are picked up from "variables_file".
#
# This script also handles some aspects of output produced by Odin-II
# and ABC.  It transforms indexes in the form "~index" to "<index>",
# removes "top^" from top-level signal names, and for hieararchical
# names in the form "module+instance", removes the module name (after
# using it to track down any reset signals).
#
# The "init_file" name passed to the script is the top-level init file.
# By taking the root of this name, the file searches <name>.dep for
# dependencies, and proceeds to pull in additional initialization files
# for additional modules used.
#
# Note that this file handles mapped blif files ONLY.  The only statements
# allowed in the file are ".model", ".inputs", ".outputs", ".latch",
# ".gate", and ".end".  The line ".default_input_arrival", if present,
# is ignored.  In the output file, all ".latch" statements are replaced
# with ".gate" statements for the specific flop gate.
#-------------------------------------------------------------------------
# Written by Tim Edwards October 8, 2013
# Open Circuit Design
#-------------------------------------------------------------------------

if {$argc < 3} {
   puts stderr \
	"Usage:  postproc.tcl mapped_blif_file init_file variables_file"
   exit 1
}

set mbliffile [lindex $argv 0]
set cellname [file rootname $mbliffile]
if {"$cellname" == "$mbliffile"} {
   set mbliffile ${cellname}.blif
}

set outfile ${cellname}_tmp.blif

set initfile [lindex $argv 1]
set initname [file rootname $initfile]
if {"$initname" == "$initfile"} {
   set initfile ${initname}.init
}

set varsfile [lindex $argv 2]

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $mbliffile r} bnet] {
   puts stderr "Error: can't open file $mbliffile for reading!"
   exit 1
}

if [catch {open $initfile r} inet] {
   puts stderr "Error: can't open file $initfile for reading!"
   exit 1
}

if [catch {open $varsfile r} vfd] {
   puts stderr "Error: can't open file $varsfile for reading!"
   exit 1
}

if [catch {open $outfile w} onet] {
   puts stderr "Error: can't open file $outfile for writing!"
   exit 1
}

#-------------------------------------------------------------
# The variables file is a UNIX tcsh script, but it can be
# processed like a Tcl script if we substitute space for '='
# in the "set" commands.  Then all the variables are in Tcl
# variable space.
#-------------------------------------------------------------

while {[gets $vfd line] >= 0} {
   set tcmd [string map {= \ } $line]
   eval $tcmd
}

#-------------------------------------------------------------
# Read the file of initialization signals and their values
#-------------------------------------------------------------

set resetlist {}
set flopsigs {}
set floptypes {}
set flopresetnet {}

while {[gets $inet line] >= 0} {
   if [regexp {^([^ \t]+)[ \t]+[^ \t]+[ \t]+[^ \t]+} $line lmatch resetnet] {
      set resetnet [string map {! not_ ~ not_} $resetnet]
      lappend resetlist $resetnet
   } elseif [regexp {^([^ \t]+)[ \t]+([^ \t]+)} $line lmatch signal initcond] {
      lappend flopsigs ${signal}_FF_NODE
      lappend floptypes $initcond
      lappend flopresetnet $resetnet
   } else {
      set resetnet $line
      set resetnet [string map {! not_ ~ not_} $resetnet]
      lappend resetlist $resetnet
   }
}

close $inet

#-------------------------------------------------------------
# Now post-process the blif file
# The main thing to remember is that internal signals will be
# outputs of flops, but external pin names have to be translated
# to their internal names by looking at the OUTPUT section.
#-------------------------------------------------------------

set cycle 0
while {[gets $bnet line] >= 0} {
   if [regexp {^.gate} $line lmatch] {
      break
   } elseif [regexp {^.latch} $line lmatch] {
      break
   } elseif [regexp {^.default_input_arrival} $line lmatch] {
      continue
   }
   puts $onet $line
}

# Add a reset inverter/buffer to the netlist
# Add two inverters if the reset signal was inverted

foreach resetnet [lsort -uniq $resetlist] {
   if {[string first "not_" $resetnet] == 0} {
      set rstorig [string range $resetnet 4 end]
      puts $onet ".gate ${inverter} ${invertpin_in}=${rstorig} ${invertpin_out}=${resetnet}"
   }
   puts $onet ".gate ${inverter} ${invertpin_in}=${resetnet} ${invertpin_out}=pp_${resetnet}bar"
}

# Replace all .latch statements with .gate, with the appropriate gate type and pins,
# and copy all .gate statements as-is.

set sridx 0
while {1} {
   if [regexp {^\.latch[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+[^ \t]+[ \t]+([^ \t]+)} \
		$line lmatch dname qname cpname] {
       set srnames ""
       set rqidx [string first "^" $qname]
       if {$rqidx >= 0} {
	  set qname [string range $qname ${rqidx}+1 end]
       }
       set idx [lsearch $flopsigs $qname]
       if {$idx >= 0} {
	  set flopt [lindex $floptypes $idx]
	  set resetnet [lindex $flopresetnet $idx]
	  if {$setpininvert == 1} {
	     set setresetnet pp_${resetnet}bar
	     set setpinstatic ${vddnet}
	  } else {
	     set setresetnet ${resetnet}
	     set setpinstatic ${gndnet}
	  }
	  if {$resetpininvert == 1} {
	     set resetresetnet pp_${resetnet}bar
	     set resetpinstatic ${vddnet}
	  } else {
	     set resetresetnet ${resetnet}
	     set resetpinstatic ${gndnet}
	  }
	  if {$flopt == 1} {
	     if {[catch {set flopset}]} {
		set gname ".gate ${flopsetreset}"
		set srnames "${setpin}=${setresetnet} ${resetpin}=${resetpinstatic}"
	     } else {
		set gname ".gate ${flopset}"
		set srnames "${setpin}=${setresetnet}"
	     }
	  } elseif {$flopt == 0} {
	     if {[catch {set flopreset}]} {
		set gname ".gate ${flopsetreset}"
		set srnames "${setpin}=${setpinstatic} ${resetpin}=${resetresetnet}"
	     } else {
		set gname ".gate ${flopreset}"
		set srnames "${resetpin}=${resetresetnet}"
	     }
	  } else {
	     # Set signal to another signal.
	     set net1 sr_net_${sridx}
	     incr sridx
	     set net2 sr_net_${sridx}
	     incr sridx

	     if {$setpininvert == 0 || $resetpininvert == 1} {
		puts $onet ".gate ${inverter} ${invertpin_in}=${flopt} ${invertpin_out}=pp_${flopt}_bar"
	     }

	     if {$setpininvert == 1} {
		puts -nonewline $onet ".gate ${nandgate} ${nandpin_in1}=${resetnet} "
		puts $onet "${nandpin_in2}=${flopt} ${nandpin_out}=${net1}"
	     } else {
		puts -nonewline $onet ".gate ${norgate} ${norpin_in1}=pp_${resetnet}bar "
		puts $onet "${norpin_in2}=pp_${flopt}bar ${norpin_out}=${net1}"
	     }

	     if {$resetpininvert == 1} {
		puts -nonewline $onet ".gate ${nandgate} ${nandpin_in1}=${resetnet} "
		puts $onet "${nandpin_in2}=pp_${flopt}bar ${nandpin_out}=${net2}"
	     } else {
		puts -nonewline $onet ".gate ${norgate} ${norpin_in1}=pp_${resetnet}bar "
		puts $onet "${norpin_in2}=${flopt} ${norpin_out}=${net2}"
	     }

	     set gname ".gate ${flopsetreset}"
	     set srnames "${setpin}=${net1} ${resetpin}=${net2}"
	  }

       } else {
	   # No recorded init state, use plain flop
	   set gname ".gate ${flopcell}"
	   set srnames ""
       }
       puts $onet "$gname ${floppinin}=$dname ${floppinclk}=$cpname $srnames ${floppinout}=$qname"

   } else {
       puts $onet $line
   }
   if {[gets $bnet line] < 0} break
}
