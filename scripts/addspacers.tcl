#!/usr/bin/tclsh
#---------------------------------------------------------------------------
# addspacers.tcl ---
#
# Read LEF file and parse for fill cells;  get the number and width of
#	the different fill cells available.
# Read the DEF file once, to find the number of rows and the endpoint
#	of each row.  This is not a general purpose script. . . we assume
#	output is from TimberWolf and place2def, so cells are aligned on
#	the left, and components appear in order, row by row, from left
#	to right.
# Read the DEF file again, up to the COMPONENTS section.
# Modify the COMPONENTS section to add the spacer cells, and write out
#	the annotated DEF file.
#
#---------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

if {$argc < 3} {
   puts stdout "Usage:  addspacers <project_name> <lef_file> <fill_cell>"
   exit 0
}

puts stdout "Running addspacers.tcl"

# NOTE:  There is no scaling.  TimberWolf values are in centimicrons,
# as are DEF values (UNITS DISTANCE MICRONS 100)

set topname [file rootname [lindex $argv 0]]
set lefname [lindex $argv 1]
set fillcell [lindex $argv 2]

set defname ${topname}.def
set defoutname ${topname}_filled.def

set units 100		;# write centimicron units into the DEF file

#-----------------------------------------------------------------
# Open all files for reading and writing
#-----------------------------------------------------------------

if [catch {open $lefname r} flef] {
   puts stderr "Error: can't open file $lefname for input"
   return
}

if [catch {open $defname r} fdef] {
   puts stderr "Error: can't open file $defname for input"
   return
}

#----------------------------------------------------------------
# Read through a LEF file section that we don't care about.
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
# Parse the macro contents of the LEF file and retain the information
# about cell size and pin positions.
#----------------------------------------------------------------

proc parse_macro {leffile macroname} {
   global $macroname units

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

      } elseif [regexp {[ \t]*PIN[ \t]+(.+)[ \t]*$} $line lmatch pinname] {
	 # The fill cell is not expected to have any usable pins
	 skip_section $leffile $pinname
      } elseif [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch macrotest] {
         if {"$macrotest" == "$macroname"} {
            break
         } else {
            puts stderr "Unexpected END statement $line while reading macro $macroname"
         }
      }
   }
}

#-----------------------------------------------------------------
# Read the lef macro file and get the fill cells and their widths
#-----------------------------------------------------------------

puts stdout "Reading ${fillcell} macros from LEF file."
flush stdout

set fillcells {}

while {[gets $flef line] >= 0} {
   if [regexp {[ \t]*MACRO[ \t]+(.+)[ \t]*$} $line lmatch macroname] {
      # Parse the "macro" statement
      parse_macro $flef $macroname
      if {[string first $fillcell $macroname] == 0} {
	 # Remember this for later if it's a fill cell
	 lappend fillcells $macroname
      }
   } elseif [regexp {[ \t]*LAYER[ \t]+([^ \t]+)} $line lmatch layername] {
      skip_section $flef $layername
   } elseif [regexp {[ \t]*VIA[ \t]+([^ \t]+)} $line lmatch vianame] {
      skip_section $flef $vianame
   } elseif [regexp {[ \t]*VIARULE[ \t]+([^ \t]+)} $line lmatch viarulename] {
      skip_section $flef $viarulename
   } elseif [regexp {[ \t]*SITE[ \t]+(.+)[ \t]*$} $line lmatch sitename] {
      skip_section $flef $sitename
   } elseif [regexp {[ \t]*UNITS[ \t]*$} $line lmatch] {
      skip_section $flef UNITS
   } elseif [regexp {[ \t]*SPACING[ \t]*$} $line lmatch] {
      skip_section $flef SPACING
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
         puts -nonewline stdout "Line is: $line"
	 flush stdout
      }
   }
}

# If the macro file doesn't define any fill cells, there's not a
# whole lot we can do. . .

if {[llength $fillcells] == 0} {
   puts stdout "No fill cells (${fillname}) found in macro file ${lefname}!"
   exit 1
}

#-----------------------------------------------------------------
# Parse the COMPONENTS section of the DEF file
# Assuming this file was generated by place2def, each component
# should be on a single line.
#-----------------------------------------------------------------

proc parse_components {deffile rows} {
   upvar $rows rdict
   while {[gets $deffile line] >= 0} {
      if [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch sectiontest] {
         if {"$sectiontest" != "COMPONENTS"} {
            puts -nonewline stderr "Unexpected END statement $line "
            puts stderr "while reading section COMPONENTS"
         }
         break
      } elseif [regexp {[ \t]*-[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+\+[ \t]+PLACED[ \t]+\([ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+\)[ \t]+([^ \t]+)[ \t]+;} $line lmatch \
		instance macro px py orient] {
	 if [catch {set row [dict get $rdict $py]}] {
	    dict set rdict $py [list $px $instance $macro $orient]
	 } else {
	    set rowmax [lindex $row 0]
	    if {$px > $rowmax} {
	       dict set rdict $py [list $px $instance $macro $orient]
	    }
	 }
      } else {
	 puts -nonewline stderr "Unexpected statement $line "
	 puts stderr "while reading section COMPONENTS"
      }
   }
}

#-----------------------------------------------------------------
# Read the DEF file once to get the number of rows and the length
# of each row
#-----------------------------------------------------------------

puts stdout "Reading DEF file ${defname}. . ."
flush stdout

while {[gets $fdef line] >= 0} {
   if [regexp {[ \t]*COMPONENTS[ \t]+([^ \t]+)[ \t]*;} $line lmatch number] {
 	 set rows [dict create]
         # Parse the "COMPONENTS" statement
         parse_components $fdef rows
   } elseif [regexp {[ \t]*NETS[ \t]+([^ \t]+)} $line lmatch netnums] {
      skip_section $fdef NETS
   } elseif [regexp {[ \t]*SPECIALNETS[ \t]+([^ \t]+)} $line lmatch netnums] {
      skip_section $fdef SPECIALNETS
   } elseif [regexp {[ \t]*PINS[ \t]+([^ \t]+)} $line lmatch pinnum] {
      skip_section $fdef PINS
   } elseif [regexp {[ \t]*VIARULE[ \t]+([^ \t]+)} $line lmatch viarulename] {
      skip_section $fdef $viarulename
   } elseif [regexp {[ \t]*VIA[ \t]+(.+)[ \t]*$} $line lmatch sitename] {
      skip_section $fdef $sitename
   } elseif [regexp {[ \t]*END[ \t]+DESIGN[ \t]*$} $line lmatch] {
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
      } elseif [regexp {[ \t]*UNITS} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*DESIGN} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*DIEAREA} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*TRACKS} $line lmatch] {
         incr matches
      } else {
         puts stderr "Unexpected input in DEF file:"
         puts stdout "Line is: $line"
      }
   }
}

close $flef
close $fdef

# Sort array of fill cells by width

set fillwidths {}
foreach macro $fillcells {
   lappend fillwidths [list $macro [subst \$${macro}(w)]]
}
set fillwidths [lsort -decreasing -index 1 $fillwidths]

# For each row, add the width of the last cell in that row
# to get the row end X value

dict for {row rowvals} $rows {
   set xmax [lindex $rowvals 0]
   set macro [lindex $rowvals 2]
   set xmax [+ $xmax [subst \$${macro}(w)]]
   set rowvals [lreplace $rowvals 0 0 $xmax]
   dict set rows $row $rowvals
}
   
# Find longest row
set rowmax 0
dict for {row rowvals} $rows {
   set xmax [lindex $rowvals 0]
   if {$xmax > $rowmax} {set rowmax $xmax}
}
puts stdout "Longest row is width $rowmax"

# Now, for each row, find the difference between the row end and row max,
# and create a list of how many of each fill macro it takes to fill the
# row out to the maximum distance

set numfills 0
dict for {row rowvals} $rows {
   set xmax [lindex $rowvals 0]
   set xd [- $rowmax $xmax]
   set fills {}
   foreach fillset $fillwidths {
      set fw [lindex $fillset 1]
      set fn [floor [/ $xd $fw]]
      lappend fills [list [lindex $fillset 0] [int $fn]]
      set xd [- $xd [* $fn $fw]]
      incr numfills [int $fn]
   }
   lappend rowvals $fills
   dict set rows $row $rowvals
}
set numcomps [+ $number $numfills]

# Diagnostic
puts stdout "Analysis of DEF file:"
puts stdout "Number of components = $number"
puts stdout "New number of components = $numcomps"
puts stdout "Number of rows = [llength [dict keys $rows]]"

set fdef [open $defname r]

if [catch {open $defoutname w} fanno] {
   puts stderr "Error: can't open file $defoutname for output"
   return
}

#-----------------------------------------------------------------
# Read the DEF file a second time to get the number of rows and the length
# of each row
#-----------------------------------------------------------------

while {[gets $fdef line] >= 0} {
   if [regexp {[ \t]*COMPONENTS[ \t]+([^ \t]+)[ \t]*;} $line lmatch number] {
      puts $fanno "COMPONENTS $numcomps ;"
      set r 0
      while {[gets $fdef line] >= 0} {
	 puts $fanno $line
         if [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch sectiontest] {
            break
         } elseif [regexp {[ \t]*-[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+\+[ \t]+PLACED[ \t]+\([ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+\)[ \t]+([^ \t]+)[ \t]+;} $line lmatch \
		instance macro px py orient] {
	    # Check if there is a match to the last instance in the row
	    set rowvals [dict get $rows $py]
	    set rowinst [lindex $rowvals 1]
	    if {[string equal $instance $rowinst]} {
	       incr r
	       set xpos [lindex $rowvals 0]
	       set fills [lindex $rowvals 4]
	       # Get orientation of row (N or S);
	       # remove "F" if last cell was flipped
	       set orient [string index [lindex $rowvals 3] end]
	       foreach fpair $fills {
		  set fmacro [lindex $fpair 0]
		  set fw [subst \$${fmacro}(w)]
		  set fn [lindex $fpair 1]
		  for {set i 1} {$i <= $fn} {incr i} {
		     puts $fanno "- ${fmacro}_${r}_${i} ${fmacro} + PLACED ( $xpos $py ) $orient ;"
		     set xpos [+ $xpos $fw]
		  }
	       }
	    }
 	 }
      }
   } else {
      puts $fanno $line
   }
}

close $fanno
close $fdef

puts stdout "Done with addspacers.tcl"
