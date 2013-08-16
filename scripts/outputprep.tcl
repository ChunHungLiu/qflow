#!/usr/bin/tclsh8.5
#-------------------------------------------------------------------------
# outputprep.tcl --- 
#
#    Odin_II modifies all DFF output signal names by adding "_FF_NODE"
#    to the name.  This is good for module input/output, as it
#    differentiates the I/O name and the DFF output name, allowing
#    buffers to be inserted between the two.  However, other internal
#    signals don't get buffered, and so the original node name from
#    the verilog source is corrupted with respect to simulations, etc.,
#    which expect to see the original node name.  This script searches
#    for all DFF outputs that are NOT I/O and removes "_FF_NODE" from
#    the name whereever it occurs.
#
# usage:
#	  outputprep.tcl <bdnetfile> <outfile>
#
#-------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

set bdnetfile [lindex $argv 0]
set cellname [file rootname $bdnetfile]
if {"$cellname" == "$bdnetfile"} {
   set bdnetfile ${cellname}.bdnet
}

set outfile [lindex $argv 1]
set outname [file rootname $outfile]
if {"$outname" == "$outfile"} {
   set outfile ${outname}.bdnet
}

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $bdnetfile r} bnet] {
   puts stderr "Error: can't open file $bdnetfile for reading!"
   exit 0
}

if [catch {open $outfile w} bout] {
   puts stderr "Error: can't open file $outfile for writing!"
   exit 0
}

#-------------------------------------------------------------
# On the premise that flop outputs cannot be module inputs, we
# look only at the "output" list.
#-------------------------------------------------------------

set outputsigs {}
set state 0
while {[gets $bnet line] >= 0} {

   if {$state == 0} {
      if [regexp {^[ \t]*OUTPUT} $line lmatch] {
         set state 1
      }
      puts $bout $line
   } elseif {$state == 1} {

      if [regexp {^[ \t]*"([^ \t:]+)"[ \t:]+"([^ \t:]+)"(.*)$} $line \
		lmatch name_in name_out rest] {
	 if [string match $name_in $name_out] {
	    lappend outputsigs $name_in
	 }
      }
      puts $bout $line

      if [regexp {^[ \t]*INSTANCE} $line lmatch] {
	 # Proceed to the net rewriting part
	 set state 2
      }
   } elseif {$state == 2} {

      # For any signal name containing "_FF_NODE", if the text preceding
      # "_FF_NODE" is not in the output list, then remove the "_FF_NODE" to
      # restore the original signal name.

      if [regexp {^[ \t]*"([^ \t:]+)"[ \t:]+"([^ \t:]+)_FF_NODE"(.*)$} $line \
		lmatch pin_name sig_name rest] {
	 if {[lsearch -exact $outputsigs $sig_name] < 0} {
	    puts $bout "\t\"$pin_name\" : \"$sig_name\"$rest"
	 } else {
	    puts $bout $line
	 }
      } else {
	 puts $bout $line
      }
   }
}

close $bnet 
close $bout
