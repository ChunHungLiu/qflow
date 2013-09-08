#!/usr/bin/tclsh
#
# Usage:
#	bdnet2cel.tcl [-f <fillcell> <value>] <bdnet_filename>
#			<lef_filename> [<cel_filename>]
#
# If cel_filename is not specified, then name will be the root name
# of the .bdnet file with the .cel extension.
#
# If "-f" is specified, it must be followed by the name of the fill
# cell and a value which is a percentage by which to increase the
# layout area of the cell using fill cells.
#
# NOTE:	Cells must be placed relative to the LEF bounding box during
#	physical placement!
#
#------------------------------------------------------------
#   Format translation between:
#	1) LEF library file (input),
#	2) .bdnet netlist file (input), and
#	3) .cel (output) file for TimberWolf place & route
#------------------------------------------------------------
#
# Written by Tim Edwards July 25, 2006
# MultiGiG, Inc.
# Updated 9/8/2013 to incorporate fill cells to increase the
# layout size and reduce routing congestion.
#------------------------------------------------------------
# LEF dimensions are microns unless otherwise stated.

set units 100
set pitchx 160		;# value overridden from LEF file
set pitchy 200		;# value overridden from LEF file

set fillp 0
set fillc ""
set fillcell ""
if {$argc > 3} {
   set farg [lindex $argv 0]
   if {"$farg" == "-f"} {
      set fillc [lindex $argv 1]
      set fillp [lindex $argv 2]
      incr argc -3
      set argv [lrange $argv 3 end]
   }
}

set bdnetfile [lindex $argv 0]
set cellname [file rootname [file tail $bdnetfile]]
if {"$cellname" == "$bdnetfile"} {
   set bdnetfile ${cellname}.bdnet
}

if {$argc > 1} {
   set leffile [lindex $argv 1]
} else {
   set leffile "-"
}

if {$argc == 3} {
   set celfile [lindex $argv 2]
} else {
   set celfile ${cellname}.cel
}

#-------------------------------------------------------------

set lefname [file rootname $leffile]
if {"$lefname" == "$leffile"} {
   set leffile ${lefname}.lef
}

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $leffile r} flef] {
   puts stderr "Error: second argument is not a LEF file!"
   exit 0
}

if [catch {open $bdnetfile r} fnet] {
   puts stderr "Error: can't open file $bdnetfile for reading!"
   exit 0
}

if [catch {open $celfile w} fcel] {
   puts stderr "Error: can't open file $celfile for writing!"
   exit 0
}

#----------------------------------------------------------------
# First, parse the contents of the .bdnet file and get a list
# of all macro names used.
#----------------------------------------------------------------

puts stdout "1st pass of bdnet file ${bdnetfile}. . ."
flush stdout

set macrolist {}
while {[gets $fnet line] >= 0} {
   if [regexp {^INSTANCE[ \t]+"([^"]+)"} $line lmatch macro] {
      lappend macrolist [string toupper $macro]
   }
}
set macrolist [lsort -unique $macrolist]
close $fnet

#----------------------------------------------------------------
# Parse port information for a macro pin from the LEF MACRO block
#
# Note that all of the geometry of each port gets whittled down
# to a single point.  Maybe TimberWolf can be made to work on
# more complicated port geometry?
#----------------------------------------------------------------

proc parse_port {pinname macroname leffile ox oy} {
   global $macroname units

   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*LAYER[ \t]+(.+)[\t ]*;} $line lmatch layername] {
	 if {![regexp {.*(\d).*} $layername lmatch layernum]} {set layernum 0}
	 set ${macroname}(${pinname},layer) $layernum
      } elseif [regexp {[ \t]*RECT[ \t]+(.+)[ \t]+(.+)[ \t]+(.+)[ \t]+(.+)[ \t]*;} \
		$line lmatch llx lly urx ury] {
	 set llx [expr {int($llx * $units)}]
	 set lly [expr {int($lly * $units)}]
	 set urx [expr {int($urx * $units)}]
	 set ury [expr {int($ury * $units)}]
	 set xp [expr {(($llx + $urx) / 2) - $ox}]
	 set yp [expr {(($lly + $ury) / 2) - $oy}]
	 set ${macroname}(${pinname},xp) $xp
	 set ${macroname}(${pinname},yp) $yp
      } elseif [regexp {[ \t]*END[ \t]*$} $line lmatch] { break }
   }
   puts -nonewline stdout "${pinname}"
   puts -nonewline stdout " [set ${macroname}(${pinname},xp)]"
   puts -nonewline stdout " [set ${macroname}(${pinname},yp)]"
   puts -nonewline stdout " [set ${macroname}(${pinname},layer)]"
   puts stdout ""
}

#----------------------------------------------------------------
# Parse pin information from the LEF MACRO block
#----------------------------------------------------------------

proc parse_pin {pinname macroname leffile ox oy} {
   global $macroname

   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*PORT} $line lmatch] {
	 parse_port $pinname $macroname $leffile $ox $oy
      } elseif [regexp {[ \t]*DIRECTION[ \t]+(.+)[ \t]*;} $line lmatch porttype] {
	 set ${macroname}(${pinname},type) $porttype
      } elseif [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch pintest] {
	 if {"$pintest" == "$pinname"} {
	    break
	 } else {
	    puts stdout "Unexpected END statement $line while parsing pin $pinname"
	 }
      }
   }
}

#----------------------------------------------------------------
# Read through a section that we don't care about.
#----------------------------------------------------------------

proc skip_section {leffile sectionname} {
   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch sectiontest] {
	 if {"$sectiontest" != "$sectionname"} {
	    puts -nonewline stderr "Unexpected END statement $line "
	    puts stderr "while reading section $sectionname"
	 }
	 break
      }
   }
}

#----------------------------------------------------------------
# Parse a layer section for routing information
#----------------------------------------------------------------

proc parse_layer {leffile layername} {
   global pitchx pitchy units

   set pitch 0
   while {[gets $leffile line] >= 0} {
      regexp {[ \t]*TYPE[ \t]+(.+)[ \t]*;} $line lmatch type
      regexp {[ \t]*DIRECTION[ \t]+(.+)[ \t]*;} $line lmatch direc
      regexp {[ \t]*PITCH[ \t]+(.+)[ \t]*;} $line lmatch pitch
      regexp {[ \t]*WIDTH[ \t]+(.+)[ \t]*;} $line lmatch width
      regexp {[ \t]*SPACING[ \t]+(.+)[ \t]*;} $line lmatch space
      if [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch layertest] {
	 if {"$layertest" != "$layername"} {
	    puts stderr "Unexpected END statement $line while reading layer $layername"
	 }
	 break
      }
   }

   # All we want to do here is determine the horizontal and vertical
   # route pitches

   if {$pitch > 0} {
      if {"$direc" == "HORIZONTAL"} {
         set pitchy [expr $units * $pitch]
      } else {
         set pitchx [expr $units * $pitch]
      }
   }
}

#----------------------------------------------------------------
# Parse the macro contents of the LEF file and retain the information
# about cell size and pin positions.
#----------------------------------------------------------------

proc parse_macro {leffile macroname} {
   global $macroname units

   puts stderr "Parsing macro $macroname:  Ports are:"
   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*SYMMETRY[ \t]+(.+)[ \t]*;} $line lmatch symmetry] {
	 set ${macroname}(symmetry) $symmetry
      } elseif [regexp {[ \t]*ORIGIN[ \t]+(.+)[ \t]+(.+)[ \t]*;} $line lmatch x y] {
	 set x [expr {int($x * $units)}]
	 set y [expr {int($y * $units)}]
	 set ${macroname}(x) $x
	 set ${macroname}(y) $y
      } elseif [regexp {[ \t]*SIZE[ \t]+(.+)[ \t]+BY[ \t]+(.+)[ \t]*;} \
			$line lmatch w h] {
	 set w [expr {int($w * $units)}]
	 set h [expr {int($h * $units)}]
	 set ${macroname}(w) $w
	 set ${macroname}(h) $h

	 # Compute derived values
	 # ox, oy are the LEF coordinates where TimberWolf expects the "origin"
	 set ox [expr {$x + ($w / 2)}]
	 set oy [expr {$y + ($h / 2)}]
	 set left [expr {-($w / 2)}]
	 set right [expr {$left + $w}]
	 set bottom [expr {-($h / 2)}]
	 set top [expr {$bottom + $h}]
	 set ${macroname}(ox) $ox
	 set ${macroname}(oy) $oy
	 set ${macroname}(left) $left
	 set ${macroname}(right) $right
	 set ${macroname}(top) $top
	 set ${macroname}(bottom) $bottom
      } elseif [regexp {[ \t]*PIN[ \t]+(.+)[ \t]*$} $line lmatch pinname] {
	 parse_pin $pinname $macroname $leffile $ox $oy
      } elseif [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch macrotest] {
	 if {"$macrotest" == "$macroname"} {
	    break
	 } else {
	    puts stderr "Unexpected END statement $line while reading macro $macroname"
	 }
      }
   }
}

puts stdout "Reading macros from LEF file. . ."
flush stdout

while {[gets $flef line] >= 0} {
   if [regexp {[ \t]*LAYER[ \t]+(.+)[ \t]*$} $line lmatch layername] {
      parse_layer $flef $layername
   } elseif [regexp {[ \t]*MACRO[ \t]+(.+)[ \t]*$} $line lmatch macroname] {
      if {[lsearch $macrolist $macroname] >= 0} {
	 # Parse the "macro" statement 
	 parse_macro $flef $macroname
      } elseif {[string match "${fillc}*" $macroname]} {
	 # Use "fillc" as a prefix for the fill cell.  Find the largest	
	 # matching fill cell to use for adding padding to the layout
	 parse_macro $flef $macroname
	 if {$fillcell == ""} {
	    set fillcell $macroname
	 } elseif {${macroname}(w) > ${fillcell}(w)} {
	    set fillcell $macroname
	 }
      } else {
	 # This macro is not used. . . skip to end of macro
	 while {[gets $flef line] >= 0} {
	    if [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch macrotest] {
	       if {"$macroname" == "$macrotest"} {
	          break
	       }
	    }
	 }
      }
   } elseif [regexp {[ \t]*VIA[ \t]+([^ \t]+)} $line lmatch vianame] {
      skip_section $flef $vianame
   } elseif [regexp {[ \t]*VIARULE[ \t]+([^ \t]+)} $line lmatch viarulename] {
      skip_section $flef $viarulename
   } elseif [regexp {[ \t]*SITE[ \t]+(.+)[ \t]*$} $line lmatch sitename] {
      skip_section $flef $sitename
   } elseif [regexp {[ \t]*UNITS[ \t]*$} $line lmatch] {
      skip_section $flef UNITS
   } elseif [regexp {[ \t]*END[ \t]+LIBRARY[ \t]*$} $line lmatch] {
      break
   } elseif [regexp {^[ \t]*#} $line lmatch] {
      # Comment line, ignore.
   } elseif ![regexp {^[ \t]*$} $line lmatch] {
      # Other things we don't care about
      set matches 0
      if [regexp {[ \t]*NAMESCASESENSITIVE} $line lmatch] {
	 incr matches
      } elseif [regexp {[ \t]*VERSION} $line lmatch] {
	 incr matches
      } elseif [regexp {[ \t]*BUSBITCHARS} $line lmatch] {
	 incr matches
      } elseif [regexp {[ \t]*DIVIDERCHAR} $line lmatch] {
	 incr matches
      } elseif [regexp {[ \t]*USEMINSPACING} $line lmatch] {
	 incr matches
      } elseif [regexp {[ \t]*CLEARANCEMEASURE} $line lmatch] {
	 incr matches
      } elseif [regexp {[ \t]*MANUFACTURINGGRID} $line lmatch] {
	 incr matches
      } else {
         puts stderr "Unexpected input in LEF file:  Only macro defs were expected!"
         puts stdout "Line is: $line"
      }
   }
}

#----------------------------------------------------------------
# Parse the contents of the .bdnet file again and dump each cell
# instance to the .cel file output.

puts stdout "2nd pass of bdnet file. . ."
flush stdout

set fnet [open $bdnetfile r]
set mode none
set i 0
set atotal 0
while {[gets $fnet line] >= 0} {
   if [regexp {^INSTANCE[ \t]+"([^"]+)"[ \t]*:[ \t]*"([^"]+)"} $line \
		lmatch macroname macrotype] {
      set mode [string toupper $macroname]
      set left [set ${mode}(left)]
      set right [set ${mode}(right)]
      set width [set ${mode}(w)]
      set atotal [expr {$atotal + $width}]
      set top [set ${mode}(top)]
      set bottom [set ${mode}(bottom)]
      if {[catch {incr ${mode}(count)}]} {set ${mode}(count) 0}
      set j [set ${mode}(count)]
      puts $fcel "cell $i ${mode}_$j"
      puts $fcel "left $left right $right bottom $bottom top $top"
      incr i
   } elseif [regexp {^INPUT} $line lmatch] {
      set mode "pins"
   } elseif [regexp {^OUTPUT} $line lmatch] {
      set mode "pins"
   } elseif [regexp {^MODEL[ \t]+"([^"]+)"} $line lmatch cellverify] {
      if {"$cellname" != "$cellverify"} {
	 puts -nonewline stderr "WARNING:  MODEL name ${cellverify} does not"
	 puts stderr " match filename ${cellname}!"
      }
   } elseif {"$mode" == "pins"} {
      if [regexp {"([^"]+)"[ \t]*:[ \t]*"([^"]+)"} $line lmatch pinname netname] {
	 # Don't know what to do with these yet. . . output a pad statement?
      }
   } else {
      # In the middle of parsing an instance;  mode = instance name (in lowercase).
      if [regexp {"([^"]+)"[ \t]*:[ \t]*"([^"]+)"} $line lmatch pinname netname] {
	 set pinx [set ${mode}(${pinname},xp)]
	 set piny [set ${mode}(${pinname},yp)]
	 set pinlayer [set ${mode}(${pinname},layer)]
	 puts $fcel "pin name $pinname signal $netname layer $pinlayer $pinx $piny"
      }
   }
}
close $fnet

if {($fillp > 0) && ("${fillcell}" != "")} {
   set left [set ${fillcell}(left)]
   set right [set ${fillcell}(right)]
   set top [set ${fillcell}(top)]
   set bottom [set ${fillcell}(bottom)]
   set afill [set ${fillcell}(w)]
   if {$afill > 0} {

      # "atotal" is the total width of all the cells combined.
      # "aextra" is the total width of fill cells needed to add "fillp"
      #	    percentage extra area to the layout.
      # "nfill" is the number of fill cells needed to generate the extra
      #	    area.

      set aextra [expr {($fillp * 0.01) * $atotal}]
      set nfill [expr {int($aextra / $afill + 0.5)}]

      puts stdout "Adding ${nfill} filler cells to pad layout by ${fillp} percent"
      # puts stdout "Layout area is ${atotal}"
      # puts stdout "Fill cell area is ${afill}, added area is ${aextra}"

      # Add fill cells as if the .bdnet file had a bunch of "INSTANCE" lines
      # for the fill cell

      for {set k 0} {$k < $nfill} {incr k} {
         if {[catch {incr ${fillcell}(count)}]} {set ${fillcell}(count) 0}
         set j [set ${fillcell}(count)]
         puts $fcel "cell $i ${fillcell}_$j"
         puts $fcel "left $left right $right bottom $bottom top $top"
         incr i
      }
   }
}

#----------------------------------------------------------------
# Parse the contents of the .bdnet file again and dump each input or output
# to the .cel file as a "pad".

puts stdout "3rd pass of bdnet file. . ."
flush stdout

set px [expr int($pitchx / 2)]
set py [expr int($pitchy / 2)]

set fnet [open $bdnetfile r]
set mode none
set padnum 1
while {[gets $fnet line] >= 0} {
   if [regexp {^INPUT} $line lmatch] {
      set mode inputs
   } elseif [regexp {^OUTPUT} $line lmatch] {
      set mode outputs
   } elseif [regexp {^INSTANCE} $line lmatch] {
      set mode none
      break;
   } elseif {$mode == "inputs" || $mode == "outputs"} {
      if [regexp {[ \t]+"([^"]+)"[ \t]*:[ \t]*"([^"]+)"} $line \
		lmatch pinname netname] {
         puts $fcel "pad $padnum name twpin_$pinname"
         puts $fcel "corners 4 -$px -$py -$px $py $px $py $px -$py"
         puts $fcel "pin name $pinname signal $netname layer 1 0 0"
         puts $fcel ""
         incr padnum
      }
   }
}

puts stdout "Done!"
