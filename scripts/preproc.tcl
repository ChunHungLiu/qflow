#!/usr/bin/tclsh8.5
#-------------------------------------------------------------------------
# preproc --- preprocess a verilog file to remove any blocks marked by
# "always @(signal) begin . . . end", where "signal" is assumed to be
# a reset signal.  Such a block is not synthesizable, but may be represented
# in a bdnet file by re-defining the flops that generate the signals defined
# in this block as set or reset flops.
#
# The contents of the block is dropped into a temporary file so that
# it may be used to define reset conditions on all flops in the circuit.
# The first line of the temporary file is the name of the signal used for
# reset, followed by "signal value" pairs, where signal is always one bit
# (vectors are decomposed) and "value" is either 1 (set) or 0 (reset).
#
# December 22, 2008:  Extended to parse lines of the form
#	signal1 = signal2
#
# where both signal1 and signal2 are declared variables.  If vectors, they
# must both be the same length.
#-------------------------------------------------------------------------
# Written by Tim Edwards May 6, 2007
# MultiGiG, Inc.
#-------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

set verilogfile [lindex $argv 0]
set cellname [file rootname $verilogfile]
if {"$cellname" == "$verilogfile"} {
   set verilogfile ${cellname}.v
}

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $verilogfile r} vnet] {
   puts stderr "Error: can't open file $verilogfile for reading!"
   exit 0
}

if [catch {open ${cellname}_tmp.v w} vtmp] {
   puts stderr "Error: can't open file ${cellname}_tmp.v for writing!"
   exit 0
}

if [catch {open ${cellname}.init w} itmp] {
   puts stderr "Error: can't open file ${cellname}.init for writing!"
   exit 0
}

#-------------------------------------------------------------

while {[gets $vnet line] >= 0} {

   # Ensure that we know about all registers that are vectors
   # For right-hand-side assignments, we keep a list of both
   # registers and inputs.

   if [regexp {^[ \t]*reg[ \t]+\[([^ \t]+):([^ \t]+)\][ \t]+([^ \t]+)[ \t]*;} $line \
		lmatch vechigh veclow vecname] {
       set vectors($vecname) [list $veclow $vechigh]		;# create an array
       set knownsigs($vecname) [list $veclow $vechigh]		;# create an array
   }
   if [regexp {^[ \t]*input[ \t]+\[([^ \t]+):([^ \t]+)\][ \t]+([^ \t]+)[ \t]*;} $line \
		lmatch vechigh veclow vecname] {
       set knownsigs($vecname) [list $veclow $vechigh]		;# create an array
   }
   if [regexp {^[ \t]*parameter[ \t]+([^ \t]+)[ \t]*=[ \t]*([^ \t]+)[ \t]*;} $line \
		lmatch paramname paramval] {
       set parameters($paramname) $paramval 	;# save the parameter name and value
   }

   if [regexp {^always @[ \t]*\([ \t]*([^ \t]+)[ \t]*\)} $line lmatch resetpin] {
      puts $itmp $resetpin
      while {[gets $vnet line] >= 0} {
         if [regexp {^end} $line lmatch] {
	    break
	 } elseif [regexp {^[ \t]*/[/\*]} $line lmatch] {
	    # Ignore comment lines
	    continue;
	 } else {
	    # Parse lines with syntax A = B
            if [regexp {([^ \t]+)[ \t]*=[ \t]*([^ \t]+);} $line lmatch lhs rhs] {

	       # If the right-hand-side is a parameter, make the substitution
	       if {![catch {set pval $parameters($rhs)}]} {
	          set rhs $pval
	       }

               if [regexp {[^ \t]+[ \t]*=[ \t]*([^ \t\[]+)\[([^ :\t]+):([^ \]\t]+)\];} \
			$line lmatch rhv rhh rhl] {
		  # Handle partial vectors on right-hand side
		  set vrange $vectors($lhs)
		  set veclow [lindex $vrange 0]
		  set vechigh [lindex $vrange 1]
		  if {![catch {set vr2 $knownsigs($rhv)}]} {
		     if {[expr {abs($rhh - $rhl)}] == \
				[expr {abs($vechigh - $veclow)}]} {
		        for {set i $veclow; set j $rhl} {$i <= $vechigh} \
				{incr i} {
			   puts $itmp "${lhs}<$i> ${rhv}<$j>"
			   if {$rhh > $rhl} {
			      incr j
			   } else {
			      incr j -1
			   }
		        }
		     } else {
			puts $itmp "$lhs $rhs"
		     }
	          }
	       } elseif [regexp {([0-9]+)'b([0-9]+)} $rhs lmatch ndig digits] { 

	          # Handle binary vectors
		  # The following two lines ensure that valid numbers like "8'b0"
		  # are properly zero-padded out.

		  set remain [expr $ndig - [string length $digits]]
		  set digits [string repeat 0 $remain]$digits
		     
		  for {set i 0} {$i < $ndig} {incr i} {
		     set j [expr $ndig - 1 - $i]
		     set idig [string index $digits $j]
	             puts $itmp "${lhs}<$i> $idig"
		  }
	       } elseif {![catch {set vrange $vectors($lhs)}]} {
		  # This is a known vector but has simply been set to
		  # a decimal number.  Turn the number into binary and 
		  # create an entry for each bit.  If the right-hand
		  # side is not a digit, then it should be a known
		  # signal with the same number of bits as the left-hand
		  # side.

		  set veclow [lindex $vrange 0]
		  set vechigh [lindex $vrange 1]

		  if {![catch {set vr2 $knownsigs($rhs)}]} {
		     set v2low [lindex $vr2 0]
		     set v2high [lindex $vr2 1]
		     if {[expr {abs($v2high - $v2low)}] == \
				[expr {abs($vechigh - $veclow)}]} {
		        for {set i $veclow; set j $v2low} {$i <= $vechigh} \
				{incr i} {
			   puts $itmp "${lhs}<$i> ${rhs}<$j>"
			   if {$v2high > $v2low} {
			      incr j
			   } else {
			      incr j -1
			   }
		        }
		     } else {
			puts $itmp "$lhs $rhs"
		     }
		  } else {
		     for {set i $veclow} {$i <= $vechigh} {incr i} {
			set idig [expr $rhs & 1]
	                puts $itmp "${lhs}<$i> $idig"
		        set rhs [expr $rhs >> 1]
		     }
		  }
	       } else {
	          puts $itmp "$lhs $rhs"
	       }
	    }
	 }
      }
   } else {
      # Another preprocessing step:  Look for assignments containing the
      # syntax "wirename = vecname1[vecname2]".  Expand this into a decoder
      # of the type:  wirename = (vecname2 == 0) ? vecname1[0] : ...
      # to the limit of values in vecname2.  For simplicity, we (for now,
      # at least) restrict the preprocessing to simple assignments only.

      if [regexp {^[ \t]*//} $line lmatch] {
	 puts $vtmp $line
      } elseif [regexp {([^=]+)=[ \t]*([^ \[\t]+)\[([^ \]\t]+)\][ \t]*;} $line \
		lmatch lhs rhvec rhidx] {
	 if {![catch {set vr1 $knownsigs($rhvec); set vr2 $knownsigs($rhidx)}]} {
	    set veclow [lindex $vr2 0]
	    set vechigh [lindex $vr2 1]
	    set vrange [+ [- $vechigh $veclow] 1]
	    set vmax [- [** 2 $vrange] 1]
	    puts $vtmp "$lhs = "
	    for {set i 0} {$i < $vmax} {incr i} {
	       puts $vtmp "($rhidx == $i) ? $rhvec\[$i\] :"
	    }
	    puts $vtmp "$rhvec\[$i\];"
	 } else {
            puts $vtmp $line
         }
      } else {
         puts $vtmp $line
      }
   }
}

