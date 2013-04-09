#!/usr/bin/tclsh
#---------------------------------------------------------------------------
# addspacers.tcl ---
#
# Read TimberWolf .pl1 and .pl2 cell placement output files, and write
# a text file of positions to put spacer cells.
#
#---------------------------------------------------------------------------

if {$argc == 0} {
   puts stdout "Usage:  addspacers <project_name> [-p]"
   puts stdout "   option -p:  add spacers on left for power buses"
   exit 0
}

puts stdout "Running addspacers.tcl"

set powerbus 0
if {$argc == 2} {
   if {[lindex $argv 1] == "-p"} {
      set powerbus 1
      puts stdout "Adding VDD/GND buses"
   }
}

# NOTE:  There is no scaling.  TimberWolf values are in centimicrons,
# as are DEF values (UNITS DISTANCE MICRONS 100)

set topname [file rootname [lindex $argv 0]]

set pl1name ${topname}.pl1
set pl2name ${topname}.pl2
set txtname ${topname}.txt

set scriptdir [file dirname $argv0]
set libdir "${scriptdir}/../lib"

#-----------------------------------------------------------------
# Pick up the width of a feedthrough cell from the .par file
#-----------------------------------------------------------------

if [catch {open $pl1name r} fpl1] {
   puts stderr "Error: can't open file $pl1name for input"
   return
}

if [catch {open $pl2name r} fpl2] {
   puts stderr "Error: can't open file $pl2name for input"
   return
}

if [catch {open $txtname w} ftxt] {
   puts stderr "Error: can't open file $txtname for output"
   return
}

#-----------------------------------------------------------------
# Read the .pl2 file and get the maximum X value
#-----------------------------------------------------------------

set pitch 56
set xmax 0
set xmin 0

set pitch4 [expr {$pitch * 4}]
set pitch2 [expr {$pitch * 2}]
set halfpitch [expr {$pitch / 2}]

while {[gets $fpl2 line] >= 0} {
   regexp {^[ \t]*([^ ])+[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+[^ ]+[ \t]+([^ ]+)} \
		$line lmatch rowidx llx lly urx ury align
   set pline [regexp {^[ \t]*twpin} $line lmatch]
   if {$pline <= 0} {
     if {$urx > $xmax} {
	 set xmax $urx
     }
     if {$llx < $xmin} {
	 set xmin $llx
     }
  }
}
close $fpl2

#-----------------------------------------------------------------
# Read the .pl1 component file
#-----------------------------------------------------------------

set lastrow -1
set rowmaxx 0
set rowy 0

puts $ftxt "set top \[cellname list window\]"
puts $ftxt "addpath spacers"
puts $ftxt "snap internal"
puts $ftxt "load SPACER1"
puts $ftxt "load SPACER2"
puts $ftxt "load SPACER4"
puts $ftxt "load \$top"

while {[gets $fpl1 line] >= 0} {
   # Each line in the file is <instance> <llx> <lly> <urx> <ury> <orient> <row>
   regexp \
   {^[ \t]*([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)} \
	$line lmatch instance llx lly urx ury orient row

   if {$lastrow < 0} {set lastrow $row}

   # If we're at the last cell in a row, process.

   if {$row != $lastrow} {
      set rowxdiff [expr {$xmax - $rowmaxx}]
      set xunits [expr {$rowxdiff / $pitch}]
      set x4units [expr {$xunits / 4}]
      set xunits [expr {$xunits % 4}]
      set x2units [expr {$xunits / 2}]
      set x1units [expr {$xunits % 2}]

      for {set i 0} {$i < $x1units} {incr i} {
	 puts $ftxt "box position $rowmaxx $rowy"
	 puts $ftxt "getcell SPACER1 $ostr child -${halfpitch} -${halfpitch}"
	 set rowmaxx [expr {$rowmaxx + $pitch}]
      }
      for {set i 0} {$i < $x2units} {incr i} {
	 puts $ftxt "box position $rowmaxx $rowy"
	 puts $ftxt "getcell SPACER2 $ostr child -${halfpitch} -${halfpitch}"
	 set rowmaxx [expr {$rowmaxx + $pitch2}]
      }
      for {set i 0} {$i < $x4units} {incr i} {
	 puts $ftxt "box position $rowmaxx $rowy"
	 puts $ftxt "getcell SPACER4 $ostr child -${halfpitch} -${halfpitch}"
	 set rowmaxx [expr {$rowmaxx + $pitch4}]
      }

      # Add 2 4x-width spacers to the left side to accomodate VDD/GND buses
      if {$powerbus} {
	 set rowx [expr {$xmin - $pitch4}]
	 puts $ftxt "box position $rowx $rowy"
	 puts $ftxt "getcell SPACER4 $ostr child -${halfpitch} -${halfpitch}"
	 set rowx [expr {$rowx - $pitch4}]
	 puts $ftxt "box position $rowx $rowy"
	 puts $ftxt "getcell SPACER4 $ostr child -${halfpitch} -${halfpitch}"
      }

      set lastrow $row
      set rowmaxx 0
   }

   switch $orient {
      0 {set ostr ""}
      1 {set ostr "v"}
      2 {set ostr ""}
      3 {set ostr "v"}
   }

   # Ignore any "cells" named "twpin_*".

   if {![string equal -length 6 $instance twpin_]} {
      if {$urx > $rowmaxx} {
	 set rowmaxx $urx
      }
      set rowy $lly
   }
}

close $fpl1
close $ftxt

puts stdout "Done with addspacers.tcl"
