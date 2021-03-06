#!/usr/bin/tclsh
#---------------------------------------------------------------------------
# place2net2.tcl ---
#
# Read a TimberWolf .pin netlist file and produce a netlist file for
# use with the Magic router.  The nets are sorted by minor net number.
# This makes use of TimberWolf's feedthroughs and the result is good for
# strict channel routing (no routes are outside any channel).
#
# This version (place2net2) differs from the original (place2net) in that
# it assumes that feedthroughs will NOT be used.  It puts together all
# subnets belonging to a single network, and removes all of the feedthrough
# entries.
#
# This version also changes the output from ".net" to ".list" to avoid having
# TimberWolf choke the next time it's run.
#---------------------------------------------------------------------------

if {$argc == 0} {
   puts stdout "Usage:  place2net2 <project_name>"
   exit 0
}

set topname [file rootname [lindex $argv 0]]
set pinfile ${topname}.pin
set netfile  ${topname}.list

if [catch {open $pinfile r} fpin] {
   puts stderr "Error: can't open file $pinfile for input"
   exit 0
}

if [catch {open $netfile w} fnet] {
   puts stderr "Error: can't open file $netfile for output"
   exit 0
}

#--------------------------------------------------------------

puts -nonewline $fnet " Netlist File"

# Parse the .pin file, writing one line of output for each line of input.

set curnet {}
set netblock {}

while {[gets $fpin line] >= 0} {
   # Each line in the file is:
   #     <netname> <subnet> <macro> <pinname> <x> <y> <row> <orient> <layer>
   regexp {^([^ ]+)[ \t]+(\d+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+[^ ]+[ \t]+[^ ]+[ \t]+([^ ]+)} \
		$line lmatch netname subnet instance pinname px py layer
   if {"$netname" != "$curnet"} {
      set curnet $netname
      puts $fnet ""
   }
   if {[string first twfeed ${instance}] == -1} { 
      if {[string first twpin_ ${instance}] == 0} { 
         puts $fnet ${pinname}
      } elseif {$instance != "PSEUDO_CELL"} {
         puts $fnet ${instance}/${pinname}
      }
   }
}

close $fpin
close $fnet
