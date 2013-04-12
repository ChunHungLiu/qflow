#!/bin/tcsh
#---------------------------------------------------------------
# Shell script setting up all variables used by the qflow scripts
# for this project
#---------------------------------------------------------------

# The LEF file containing standard cell macros

set leffile=osu035_stdcells.lef

# If there is another LEF file containing technology information
# that is separate from the file containing standard cell macros,
# set this.  Otherwise, leave it defined as an empty string.

set techleffile=""

# All cells below should be the lowest output drive strength value,
# if the standard cell set has multiple cells with different drive
# strengths.  Use the name of the set-reset flop for any of the
# flop types that don't exist.  Otherwise, use the empty list {}
# for any cells that do not exist in the standard cell set.

set flopcell=DFFPOSX1	;# Standard positive-clocked DFF, no set or reset
set flopset=DFFSR	;# DFF with preset
set setpin=S		;# The name of the set pin on the DFF
set flopreset=DFFSR	;# DFF with clear
set resetpin=R		;# The name of the clear/reset pin on the DFF
set flopsetreset=DFFSR	;# DFF with both set and clear

set bufcell=BUFX2	;# Minimum drive strength buffer cell
set bufpin_in=A		;# Name of input port to buffer cell
set bufpin_out=Y	;# Name of output port to buffer cell
set inverter=INVX1	;# Minimum drive strength inverter cell
set orgate=OR2X1	;# 2-input OR gate, minimum drive strength
set andgate=AND2X1	;# 2-input AND gate, minimum drive strength
set fillcell=FILL	;# Spacer (filler) cell (prefix, if more than one)

set separator=X		;# Separator between gate names and drive strengths
set techfile=SCN4M_SUBM.20	;# magic techfile
set magicrc=osu035.magicrc	;# magic startup script
set gdsfile=osu035_stdcells.gds	;# GDS database of standard cells
