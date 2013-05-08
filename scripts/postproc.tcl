#!/usr/bin/tclsh
#-------------------------------------------------------------------------
# postproc --- post-process a bdnet file with the contents of the
# "init" file produced by "vpreproc".  Each signal from the "init"
# file is tracked down as the output to a flop, and that flop is replaced
# by a set or reset flop accordingly.  Information about cells to use
# and their pins, etc., are picked up from "variables_file".
#-------------------------------------------------------------------------
# Written by Tim Edwards May 6, 2007
# MultiGiG, Inc.
#-------------------------------------------------------------------------

if {$argc < 3} {
   puts stderr \
	"Usage:  postproc.tcl bdnet_file init_file variables_file"
   exit 1
}

set bdnetfile [lindex $argv 0]
set cellname [file rootname $bdnetfile]
if {"$cellname" == "$bdnetfile"} {
   set bdnetfile ${cellname}.bdnet
}

set outfile ${cellname}_tmp.bdnet

set initfile [lindex $argv 1]
set initname [file rootname $initfile]
if {"$initname" == "$initfile"} {
   set initfile ${initname}.init
}

set varsfile [lindex $argv 2]

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $bdnetfile r} bnet] {
   puts stderr "Error: can't open file $bdnetfile for reading!"
   exit 1
}

if [catch {open $initfile r} inet] {
   puts stderr "Error: can't open file $initfile for reading!"
   exit 1
}

if [catch {open $varsfile r} vfd] {
   puts stderr "Error: can't open file $outfile for writing!"
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

if {[gets $inet line] >= 0} {
   set resetnet $line
   set resetnet [string map {! not_ ~ not_} $resetnet]
   lappend resetlist $resetnet
} else {
   # Empty init file---therefore, no initialization to do.
   file copy -force $bdnetfile $outfile
   exit 0
}

set flopsigs {}
set floptypes {}
set flopresetnet {}

while {[gets $inet line] >= 0} {
   if [regexp {^([^ \t]+)[ \t]+([^ \t]+)} $line lmatch signal initcond] {
      lappend flopsigs $signal
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
# Now post-process the bdnet file
# The main thing to remember is that internal signals will be
# outputs of flops, but external pin names have to be translated
# to their internal names by looking at the OUTPUT section.
#-------------------------------------------------------------

set cycle 0
while {[gets $bnet line] >= 0} {
   if [regexp {^INPUT} $line lmatch] {
      set cycle 1
   } elseif [regexp {^OUTPUT} $line lmatch] {
      set cycle 2
   } elseif [regexp {^INSTANCE} $line lmatch] {
      break
   }
   if {$cycle == 2} {
      if [regexp {"(.+)"[ \t]*:[ \t]*"(.+)"} $line lmatch sigin sigout] {
	 set idx [lsearch $flopsigs $sigin]
	 if {$idx >= 0} {
	    set sigout [string map {\[ << \] >>} $sigout]
	    set flopsigs [lreplace $flopsigs $idx $idx $sigout]
	 }
      }
   }
   puts $onet $line
}

# Add a reset inverter/buffer to the netlist
# Add two inverters if the reset signal was inverted

foreach resetnet $resetlist {
   if {[string first "not_" $resetnet] == 0} {
      set rstorig [string range $resetnet 4 end]
      puts $onet ""
      puts $onet "INSTANCE \"${inverter}\":\"physical\""
      puts $onet "\t\"${invertpin_in}\" : \"${rstorig}\";"
      puts $onet "\t\"${invertpin_out}\" : \"${resetnet}\";"
      puts $onet ""
   }
   puts $onet ""
   puts $onet "INSTANCE \"${inverter}\":\"physical\""
   puts $onet "\t\"${invertpin_in}\" : \"${resetnet}\";"
   puts $onet "\t\"${invertpin_out}\" : \"pp_${resetnet}bar\";"
   puts $onet ""
}

set sridx 0
while {1} {
   if [regexp [subst {^INSTANCE "${flopcell}":"physical"}] $line lmatch] {
       gets $bnet dline
       gets $bnet cpline
       gets $bnet qline
       set srline ""
       
       if [regexp [subst {"${floppinout}"\[ \\t\]+:\[ \\t\]+"(.+)"}] \
			$qline lmatch signame] {
	  set sigtest [string map {\[ << \] >>} $signame]
	  set idx [lsearch $flopsigs $sigtest]
          if {$idx < 0} {
	     # signal names with '0' appended are an artifact of VIS/SIS
	     if {[string index $signame end] == 0} {
	        set idx [lsearch $flopsigs [string range $sigtest 0 end-1]]
	     }
	  }
          if {$idx >= 0} {
	     set flopt [lindex $floptypes $idx]
	     set resetnet [lindex $flopresetnet $idx]
	     if {$setpininvert == 1} {
	        set setresetnet pp_${resetnet}bar
	     } else {
	        set setresetnet ${resetnet}
	     }
	     if {$resetpininvert == 1} {
	        set resetresetnet pp_${resetnet}bar
	     } else {
	        set resetresetnet ${resetnet}
	     }
	     if {$flopt == 1} {
		if {[catch {set $flopset}]} {
		   set line "INSTANCE \"${flopsetreset}\":\"physical\""
		   set srline "\t\"${setpin}\" : \"${setresetnet}\";\
				\n\t\"${resetpin}\" : \"${gndnet}\";"
		} else {
		   set line "INSTANCE \"${flopset}\":\"physical\""
		   set srline "\t\"${setpin}\" : \"${setresetnet}\""
		}
	     } elseif {$flopt == 0} {
		if {[catch {set $flopreset}]} {
		   set line "INSTANCE \"${flopsetreset}\":\"physical\""
		   set srline "\t\"${setpin}\" : \"${gndnet}\";\
				\n\t\"${resetpin}\" : \"${resetresetnet}\";"
		} else {
		   set line "INSTANCE \"${flopreset}\":\"physical\""
		   set srline "\t\"${resetpin}\" : \"${resetresetnet}\""
		}
	     } else {
		# Set signal to another signal.
		set net1 sr_net_${sridx}
		incr sridx
		set net2 sr_net_${sridx}
		incr sridx

		if {$setpininvert == 0 || $resetpininvert == 1} {
		   set line "INSTANCE \"${inverter}\":\"physical\""
		   puts $onet $line
		   set line "\t\"${invertpin_in}\" : \"${flopt}\";"
		   puts $onet $line
		   set line "\t\"${invertpin_out}\" : \"pp_${flopt}bar\";"
		   puts $onet $line
		}

		if {$setpininvert == 1} {
		   set line "INSTANCE \"${nandgate}\":\"physical\""
		   puts $onet $line
		   set line "\t\"${nandpin_in1}\" : \"${resetnet}\";"
		   puts $onet $line
		   set line "\t\"${nandpin_in2}\" : \"${flopt}\";"
		   puts $onet $line
		   set line "\t\"${nandpin_out}\" : \"${net1}\";\n"
		   puts $onet $line
		} else {
		   set line "INSTANCE \"${norgate}\":\"physical\""
		   puts $onet $line
		   set line "\t\"${norpin_in1}\" : \"pp_${resetnet}bar\";"
		   puts $onet $line
		   set line "\t\"${norpin_in2}\" : \"pp_${flopt}bar\";"
		   puts $onet $line
		   set line "\t\"${norpin_out}\" : \"${net1}\";\n"
		   puts $onet $line
		}

		if {$resetpininvert == 1} {
		   set line "INSTANCE \"${nandgate}\":\"physical\""
		   puts $onet $line
		   set line "\t\"${nandpin_in1}\" : \"${resetnet}\";"
		   puts $onet $line
		   set line "\t\"${nandpin_in2}\" : \"pp_${flopt}bar\";"
		   puts $onet $line
		   set line "\t\"${nandpin_out}\" : \"${net2}\";\n"
		   puts $onet $line
		} else {
		   set line "INSTANCE \"${norgate}\":\"physical\""
		   puts $onet $line
		   set line "\t\"${norpin_in1}\" : \"pp_${resetnet}bar\";"
		   puts $onet $line
		   set line "\t\"${norpin_in2}\" : \"${flopt}\";"
		   puts $onet $line
		   set line "\t\"${norpin_out}\" : \"${net2}\";\n"
		   puts $onet $line
		}

		set line "INSTANCE \"${flopsetreset}\":\"physical\""
		set srline \
		   "\t\"${setpin}\" : \"${net1}\";\n\t\"${resetpin}\" : \"${net2}\";"
	     }
	  }
       }

       puts $onet $line
       puts $onet $dline
       puts $onet $cpline
       if {[string length $srline] > 0} {
          puts $onet $srline
       }
       puts $onet $qline
   } else {
       puts $onet $line
   }
   if {[gets $bnet line] < 0} break
}

# Overwrite the original file
file rename -force $outfile $bdnetfile
