/*------------------------------------------------------*/
/* vpreproc.c						*/
/*							*/
/* Tokenize a verilog source file and parse it for	*/
/* structures that the Odin-II/abc flow cannot handle.	*/
/* Generally, this means handling things that are	*/
/* relevant to an ASIC flow but not to an FPGA flow.	*/
/*							*/
/* 1) Reset signals are pulled out of the code, and	*/
/*    the reset conditions are saved to a file named	*/
/*    "<rootname>.init".  These are picked up later	*/
/*    and used to replace standard flops with set,	*/
/*    reset, or both flops.				*/
/*							*/
/* 2) Parameter and `define substitutions are done by	*/
/*    vpreproc.						*/
/*							*/
/* 3) Inverted clocks are made non-inverting, with a	*/
/*    file called "<rootname>.clk" made to track which	*/
/*    clocks are inverted.  Inverters will be put in	*/
/*    front of the clock networks in the final netlist	*/
/*    (This has not been done yet, need to check the	*/
/*    capability of Odin-II and abc to handle negedge	*/
/*    clocks).						*/
/*							*/
/* 4) (Temporary) Record all clocks signal that are 	*/
/*    not in the input list of the module in a file	*/
/*    called "<rootname>.clk".  This is needed to work	*/
/*    around a bug in Odin-II.				*/
/*------------------------------------------------------*/

#define DEBUG 1

#include <stdio.h>
#include <string.h>
#include <ctype.h>		// For isalnum()
#include <malloc.h>
#include <stdlib.h>		// For exit(n)

// Signal (single-bit) data structure

typedef struct _sigact *sigactptr;

typedef struct _sigact {
   char *name;			// Name of clock, reset, or enable signal
   char edgetype;		// POSEDGE or NEGEDGE
   sigactptr next;
} sigact;

typedef struct _signal *sigptr;

typedef struct _signal {
   char *name;
   char value;			// reset value: 0, 1; or -1 if signal is not registered
   sigptr link;			// signal representing reset value if not constant, NULL
				// if signal is constant, and reset value is "value".
   sigactptr reset;		// reset signal that resets this bit
} signal;

// Vector data structure
typedef struct _vector *vecptr;

typedef struct _vector {
   char *name;
   int vector_size;		// zero means signal is not a vector
   int vector_start;
   int vector_end;
   vecptr next;
} vector;

// Module data structure
typedef struct _module {
   char *name;
   vector *iolist;		// Vectors/signals that are part of the I/O list
   vector *reglist;		// Vectors/signals that are registered
   vector *wirelist;		// Non-registered vectors/signals
   sigact *clocklist;		// List of clock signals
   sigact *resetlist;		// List of reset signals
} module;

// Parameter data structure

typedef struct _parameter *parameterp;

typedef struct _parameter {
   parameterp next;
   char *name;
   char *value;
} parameter;

// States during reading
#define  HEADER_STUFF	0x0001		// Before reading "module"
#define  MODULE_VALID	0x0002		// Read "module"
#define  INPUT_OUTPUT	0x0004		// Section with "input" and "output"
#define  MAIN_BODY	0x0008		// Main body of module, after I/O list
#define  SENS_LIST	0x0010		// Parsing a sensitivity list
#define  IN_CLKBLOCK	0x0020		// Within a begin...end block after always()
#define  PEND_CLKBLOCK	0x0040		// Within a begin...end block after always()
#define  IN_IFTEST	0x0080		// Within an "if" test condition
#define  IF_PENDING	0x0100		// Waiting for the end of the "if" test
#define  IN_IFBLOCK	0x0200		// Within a begin...end block after if()
#define  COMMENT	0x0400		// Within a C-type comment
#define  ASSIGNMENT_LHS	0x0800		// In a multi-line assignment
#define  ASSIGNMENT_RHS	0x1000		// In a multi-line assignment
#define  WIRE		0x2000		// In a wire declaration
#define  REGISTER	0x4000		// In a register declaration

// Edge types
#define  NEGEDGE	1
#define  POSEDGE	2

// Condition types
#define  UNKNOWN	-1
#define  EQUAL		1
#define  NOT_EQUAL	2

// Read a verilog bit value, either a 1 or 0, optionally preceeded by "1'b"
// Return the bit value, if found, or -1, if no bit value could be parsed

int
get_bitval(char *token)
{
   int bval;

   if (!strncmp(token, "1'b", 3))
      token += 3;
   if (sscanf(token, "%d", &bval) == 1) {
      if (bval == 0 || bval == 1)
	 return bval;
      else
	 return -1;
   }
   return -1;
}

/*----------------------------------------------------------------------*/
/* Read a bit value out of a vector.  Note that the vector may be	*/
/* constructed from parts using {...} notation, and the bit may be	*/
/* numeric, or it may be a signal name.					*/
/*----------------------------------------------------------------------*/

char *
parse_bit(int line_num, module *topmod, char *vstr, int idx)
{
   int vsize, vval, realsize;
   static char bitval[2] = "0";
   char *bptr, typechar, *vptr, *fullvec = NULL;
   vector *testvec;
   int locidx;
   static char *fullname = NULL;
   
   if (idx < 0) locidx = 0;

   if (vstr[0] == '{') {
      /* To-do:  Deal with vectors constructed from sub-vectors */
   }	
   else {
      if (sscanf(vstr, "%d'%c", &vsize, &typechar) == 2) {
	 bptr = strchr(vstr, typechar);
	 vptr = bptr + 1;
	 while (isalnum(*vptr)) vptr++;

	 // Handle the case where the bit vector needs zero padding, e.g.,
	 // "9'b0".  For decimal, octal, hex we pad more than necessary,
	 // but who cares?

	 realsize = vptr - bptr - 1;
	 if (realsize < vsize) {
	    fullvec = (char *)malloc((vsize + 1) * sizeof(char));
	    for (vptr = fullvec; vptr < fullvec + vsize; vptr++) *vptr = '0';
	    *vptr = '\0';
	    vptr = bptr + 1;
	    while (*vptr != '\0') {
	       *(fullvec + vsize - realsize) = *vptr;
	       vptr++;
	       realsize--;
	    }
	    vptr = fullvec + vsize - 1;		// Put vptr on lsb
	 }
	 else
	    vptr--;	// Put vptr on lsb

	 if (vsize > locidx) {
	    switch (typechar) {
	       case 'b':
		  *bitval = *(vptr - locidx);
	 	  bptr = bitval;
		  if (fullvec) free(fullvec);
	 	  return bptr;
	       case 'd':		// Interpret decimal
		  vstr = bptr + 1;	// Move to start of decimal value and continue
		  break;	  
	       case 'h':		// To-do:  Interpret hex
		  *bitval = *(vptr - (locidx / 4));
		  sscanf(bitval, "%x", &vval);
		  vval >>= (locidx % 4);
		  vval &= 0x1;
	 	  bptr = bitval;
		  *bptr = (vval == 0) ? '0' : '1';
		  if (fullvec) free(fullvec);
	 	  return bptr;
	       case 'o':		// To-do:  Interpret octal
		  *bitval = *(vptr - (locidx / 3));
		  sscanf(bitval, "%o", &vval);
		  vval >>= (locidx % 3);
		  vval &= 0x1;
	 	  bptr = bitval;
		  *bptr = (vval == 0) ? '0' : '1';
		  if (fullvec) free(fullvec);
	 	  return bptr;
	    }
	 }
	 else {
	    fprintf(stderr, "Line %d:  Not enough bits for vector.\n", line_num);
	    return NULL;
	 }
      }
      if (sscanf(vstr, "%d", &vval) == 1) {
	 vval >>= locidx;
	 vval &= 0x1;
	 bptr = bitval;
	 *bptr = (vval == 0) ? '0' : '1';
	 return bptr;
      }	
      else {
	 // Could be a signal name.  If so, check that it has a size
	 // compatible with idx.

	 char *is_indexed = strchr(vstr, '[');
	 if (is_indexed) *is_indexed = '\0';

	 for (testvec = topmod->wirelist; testvec != NULL; testvec = testvec->next)
	    if (!strcmp(testvec->name, vstr))
	       break;
	 if (testvec == NULL)
	    for (testvec = topmod->iolist; testvec != NULL; testvec = testvec->next)
	       if (!strcmp(testvec->name, vstr))
	          break;
	 if (testvec == NULL)
	    for (testvec = topmod->reglist; testvec != NULL; testvec = testvec->next)
	       if (!strcmp(testvec->name, vstr))
	          break;
	 
	 if (is_indexed) *is_indexed = '[';	/* Restore index delimiter */
	 if (testvec == NULL) {
	    fprintf(stderr, "Line %d: Cannot parse signal name \"%s\" for reset\n",
			line_num, vstr);
	    return NULL;
	 }
	 else {
	    /* To-do:  Need to make sure all vector indices are aligned. . . */
	    if (idx == 0 && testvec->vector_size == 0) {
	       return testvec->name;
	    }
	    else if (idx >= testvec->vector_size) {
	       fprintf(stderr, "Line %d:  Vector LHS exceeds dimensions of RHS.\n",
			line_num);
	       return NULL;
	    }
	    else {
	       int j, jstart, jend;
	       char *is_range = NULL;

	       if (is_indexed) {
		  sscanf(is_indexed + 1, "%d", &jstart);
		  if ((is_range = strchr(is_indexed + 1, ':')) != NULL) {
		     sscanf(is_range + 1, "%d", &jend);
	             if (jstart > jend)
			j = jend + idx;
		     else
			j = jstart + idx;

		     if (testvec->vector_start > testvec->vector_end) {
			if (j < testvec->vector_end) {
			   fprintf(stderr, "Line %d:  Vector RHS is outside of range"
					" %d to %d.\n", line_num,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_end;
			}
			else if (j > testvec->vector_start) {
			   fprintf(stderr, "Line %d:  Vector RHS is outside of range"
					" %d to %d.\n", line_num,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_start;
			}
		     }
		     else {
			if (j > testvec->vector_end) {
			   fprintf(stderr, "Line %d:  Vector RHS is outside of range"
					" %d to %d.\n", line_num,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_end;
			}
			else if (j < testvec->vector_start) {
			   fprintf(stderr, "Line %d:  Vector RHS is outside of range"
					" %d to %d.\n", line_num,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_start;
			}
		     }
		  }
		  else {
		     // RHS is a single bit
		     j = jstart;

		     if (idx != 0) {
			fprintf(stderr, "Line %d:  Vector LHS is set by single bit"
				" on RHS.  Padding by repetition.\n", line_num);
		     }
		  }
	       }
	       else {
	          if (testvec->vector_start > testvec->vector_end)
		     j = testvec->vector_end + idx;
	          else
		     j = testvec->vector_start + idx;
	       }

	       if (fullname != NULL) free(fullname);
	       fullname = (char *)malloc(strlen(testvec->name) + 10);
	       
	       sprintf(fullname, "%s<%d>", testvec->name, j);
	       return fullname;
	    }
	 }
      }
   }
}

/*------------------------------------------------------*/
/* Copy a line of code with parameter substitutions 	*/
/*------------------------------------------------------*/

void
paramcpy(char *dest, char *source, parameter *params)
{
   char temp[1024];
   char *sptr, *dptr, *vptr;
   int plen;
   parameter *pptr;

   strcpy(dest, source);

   for (pptr = params; pptr; pptr = pptr->next) {
      strcpy(temp, dest);
      plen = strlen(pptr->name);
      sptr = temp;
      dptr = dest;
      while (*sptr != '\0') {
	 if (!strncmp(sptr, pptr->name, plen)) {
	    vptr = pptr->value;
	    while (*vptr != '\0') {
	       *dptr++ = *vptr++;
	    }
	    sptr += plen;
	 }
	 else
	    *dptr++ = *sptr++;
      }
      *dptr = '\0';
   }
}

/*------------------------------------------------------*/
/* Main verilog preprocessing code			*/
/*------------------------------------------------------*/

int
main(int argc, char *argv[])
{
    FILE *fsource = NULL;
    FILE *finit = NULL;
    FILE *fclk = NULL;
    FILE *ftmp = NULL;

    char comment_pending;
    char *xp, *fptr, *token, *sptr, *bptr, *filename;
    char locfname[512];
    char linebuf[2048];
    char linecopy[2048];
    const char *toklist;

    module *topmod;
    sigact *clocksig;
    sigact *resetsig, *testreset, *testsig;
    vector *initvec, *testvec, *newvec;
    parameter *params = NULL;

    int state, nextstate;
    int line_num;
    int start, end, ival, i, j, k;
    int blocklevel;
    int iflevel, ifcancel;
    int no_new_token;
    int parm;
    int condition;
    char edgetype;
    char suspend;		/* When =1, suspend output */
    char multidomain;
 
    /* Only one argument, which is the source filename */

    if (argc != 2) {
	fprintf(stderr, "Usage:  vpreproc <source_file.v>\n");
	exit(1);
    }

    /* Copy argv[1] to filename and remove any extension */
    filename = strdup(argv[1]);
    xp = strrchr(filename, '.');

    /* "filename" will be the 1st argument without any file extension.	*/
    /*		It will be used to generate all output filenames.	*/
    /* "locfname" will be the full 1st argument if it has an extension	*/
    /* 		or else ".v" will be added if it doesn't.		*/

    if (xp != NULL) {
	strcpy(locfname, filename);
	*xp = '\0';
    }
    else
        snprintf(locfname, 511, "%s.v", filename);

    fsource = fopen(locfname, "r");
    if (fsource == NULL) {
	fprintf(stderr, "Error:  No such file or cannot open file \"%s\"\n", locfname);
	exit(1);
    }

    /* Okay, got the file handle, start reading */

    if (fgets(linebuf, 2047, fsource) == NULL) {
	fprintf(stderr, "Error reading source file \"%s.v\"", locfname);
	exit(1);
    }

    /* Create the files we want to write to for this file 		*/
    /* There will only be one of each file for this verilog source,	*/
    /* not one of each file per module.					*/

    sprintf(locfname, "%s.init", filename);
    finit = fopen(locfname, "w");
    if (finit == NULL) {
       fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    sprintf(locfname, "%s.clk", filename);
    fclk = fopen(locfname, "w");
    if (fclk == NULL) {
       fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    sprintf(locfname, "%s_tmp.v", filename);
    ftmp = fopen(locfname, "w");
    if (ftmp == NULL) {
	fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    fptr = linebuf;
    state = HEADER_STUFF;
    suspend = (char)0;
    line_num = 1;
    no_new_token = 0;	/* If 1, don't move to next token */
    blocklevel = 0;	/* Nesting of begin...end blocks outside a conditional */
    iflevel = 0;	/* Nesting of begin...end blocks inside a conditional */
    ifcancel = 0;	/* Remove "if" statements on this level */
    condition = UNKNOWN;
    multidomain = 0;
    toklist = " \t\n";	/* Default token list at beginning (state HEADER_STUFF) */

    while (1) {		/* Read continuously and break loop when input is exhausted */

	paramcpy(linecopy, linebuf, params);	// Substitute parameters
	strcpy(linebuf, linecopy);	// Keep a copy of the line we're processing

	if (no_new_token == 0)
	   token = strtok(fptr, toklist);
	no_new_token = 0;

	while (token != NULL) {

	    /* State-independent processing */

	    if (!strncmp(token, "/*", 2)) {
		state |= COMMENT;
	    }
	    else if (!strncmp(token, "*/", 2)) {
		if ((state & COMMENT) != 0) state &= ~COMMENT;
	    }
	    if ((state & COMMENT) == 0) {
		if (!strncmp(token, "//", 2)) {
		    break;	// Forget rest of line and read next line
		}

		if (!strcmp(token, "parameter") || !(parm = strcmp(token, "`define"))) {
		    toklist = " \t\n=;";
		    /* Get parameter name */
	            token = strtok(NULL, toklist);
		    if (token != NULL) {
			parameter *newparam = (parameter *)malloc(sizeof(parameter));
			if (!parm) {
			    newparam->name = (char *)malloc(2 + strlen(token));
			    sprintf(newparam->name, "`%s", token);
			}
			else {
			    newparam->name = strdup(token);
			}
			newparam->next = NULL;

			/* Get parameter value */
			token = strtok(NULL, toklist);
			newparam->value = strdup(token);

			/* Sort tokens by size to avoid substituting	*/
			/* part of a parameter (e.g., "START" and	*/
			/* "START1" should not be ambiguous!)		*/

			if (params == NULL)
			    params = newparam;
			else {
			    parameter *pptr, *lptr;
			    lptr = NULL;
			    for (pptr = params; pptr; pptr = pptr->next) {
				if (strlen(pptr->name) < strlen(newparam->name)) {
				    if (lptr == NULL) {
					newparam->next = params;
					params = newparam;
				    }
				    else {
					newparam->next = lptr->next;
					lptr->next = newparam;
				    }
				    break;
				}
				lptr = pptr;
			    }
			    if (pptr == NULL) lptr->next = newparam;
			}
		    }
		    suspend = 2;	/* Don't output this line */
		    break;		/* No further processing on this line */
		}
	    }

	    /* State-dependent processing */

	    switch (state) {
		case HEADER_STUFF:
		    if (!strcmp(token, "module")) {
		        state &= ~HEADER_STUFF;
		        state |= MODULE_VALID;
			topmod = (module *)malloc(sizeof(module));
			if (DEBUG) printf("Found module in source\n");
		    }
		    break;

		case MODULE_VALID:
		    /* Get name of module and fill module structure*/
		    topmod->name = strdup(token);
		    topmod->iolist = NULL;
		    topmod->reglist = NULL;
		    topmod->wirelist = NULL;
		    topmod->clocklist = NULL;
		    topmod->resetlist = NULL;
		    if (DEBUG) printf("Module name is \"%s\"\n", topmod->name);

		    state |= INPUT_OUTPUT;
		    break;

		case MODULE_VALID | INPUT_OUTPUT:
		    if (!strcmp(token, ";")) {
		        state = MAIN_BODY;
		    }
		    // Don't process I/O here;  find it in the "input" and "output"
		    // statements.  Preprocessor not responsible for making sure
		    // these two lists match.
		    break;

		case MAIN_BODY | INPUT_OUTPUT:
		case MAIN_BODY | WIRE:
		case MAIN_BODY | REGISTER:
		    if (!strcmp(token, ";")) {
		        state = MAIN_BODY;		// Return to main body processing
		    }
		    else if (sscanf(token, "%d", &ival) == 1) {
		       char *aptr;
		       int aval;

		       // Quick check for simple arithmetic (+/-) in
		       // vector size specification.  Need to
		       // expand this to include processing (+/-) as
		       // individual tokens. . .

		       if ((aptr = strchr(token, '-')) != NULL) {
			  if (sscanf(aptr + 1, "%d", &aval) == 1)
			     ival -= aval;
		       }
		       else if ((aptr = strchr(token, '+')) != NULL) {
			  if (sscanf(aptr + 1, "%d", &aval) == 1)
			     ival += aval;
		       }

		       if (start == -1)
			  start = ival;
		       else if (end == -1)
			  end = ival;
		    }
		    else {
		       if ((sptr = strchr(token, ';')) != NULL) {
			  *sptr = '\0';
			  nextstate = MAIN_BODY;
		       }
		       else
			  nextstate = state;
		       newvec = (vector *)malloc(sizeof(vector));

		       if (state & INPUT_OUTPUT) {
		          newvec->next = topmod->iolist;
		          topmod->iolist = newvec;
		          if (DEBUG) printf("Adding new I/O signal \"%s\"\n", token);
		       }
		       else if (state & WIRE) {
			  if (*token == '=') {
			     // This is a statement "wire <name> = <assignment>"
			     state = MAIN_BODY | ASSIGNMENT_LHS;
			     no_new_token = 1;
			     break;
			  }
			  else {
		             newvec->next = topmod->wirelist;
		             topmod->wirelist = newvec;
		             if (DEBUG) printf("Adding new wire \"%s\"\n", token);
			  }
		       }
		       else if (state & REGISTER) {
		          newvec->next = topmod->reglist;
		          topmod->reglist = newvec;
		          if (DEBUG) printf("Adding new register \"%s\"\n", token);
		       }

		       newvec->name = strdup(token);
		       if (start != end) {
		          newvec->vector_size = end - start;
			  if (newvec->vector_size < 0)
			     newvec->vector_size = -newvec->vector_size;
			  newvec->vector_size++;
		       }
		       else
		          newvec->vector_size = -1;
		       newvec->vector_start = start;
		       newvec->vector_end = end;
		       state = nextstate;
		    }
		    break;

		case MAIN_BODY | ASSIGNMENT_LHS:
		    if ((sptr = strchr(token, '=')) != NULL) {
		       if (*(sptr + 1) == '\0') {
			  state = MAIN_BODY | ASSIGNMENT_RHS;
			  break;
		       }
		       else
			   token = sptr + 1;	// And drop through to RHS assignment
		    }
		    else {
		       /* Record this assignment in case it is used as a clock or reset */
		       if (DEBUG) printf("Processing assignment of \"%s\". . .\n", token);
		    }

		case MAIN_BODY | ASSIGNMENT_RHS:
		    if ((sptr = strchr(token, ';')) != NULL) {
		       *sptr = '\0';
		       state = MAIN_BODY;
		       if (DEBUG) printf("Done with assignment.\n");
		    }
		    break;

		case MAIN_BODY:
		    start = end = 0;
		    if (!strcmp(token, "input")) {
			start = end = -1;
			state |= INPUT_OUTPUT;
		    }
		    else if (!strcmp(token, "output")) {
			start = end = -1;
			state |= INPUT_OUTPUT;
		    }
		    else if (!strcmp(token, "wire")) {
			// Add to list of known wires
			start = end = -1;
			state |= WIRE;
		    }
		    else if (!strcmp(token, "reg")) {
			// Add to list of known registers
			start = end = -1;
			state |= REGISTER;
		    }
		    else if (!strcmp(token, "assign")) {
			// Track assignments in case they belong to clocks
			// or resets.
			state |= ASSIGNMENT_LHS;
		    }
		    else if (!strncmp(token, "always", 6)) {
			state &= ~MAIN_BODY;
			state |= SENS_LIST;
			edgetype = 0;
			clocksig = NULL;
			resetsig = NULL;
			condition = UNKNOWN;
			initvec = NULL;
			suspend = 1;
		    }
		    else if (!strcmp(token, "endmodule")) {
			if (DEBUG) printf("End of module \"%s\" found.\n", topmod->name);
			state = HEADER_STUFF;
		    }
		    break;

		case SENS_LIST:
		    if (!strcmp(token, "posedge")) {
		        edgetype = POSEDGE;
		    }
		    else if (!strcmp(token, "negedge")) {
		        edgetype = NEGEDGE;
		    }
		    else if (!strcmp(token, "or")) {
		       // ignore this
		    }
		    else if (!strcmp(token, "@")) {
		       // ignore this, too
		    }
		    else {
		       // Catch the end of the sensitivity list
		       bptr = strchr(token, ')');
		       if (bptr != NULL) *bptr = '\0';	

		       if (edgetype == POSEDGE || edgetype == NEGEDGE) {
		          /* Parse this signal */
		          if (clocksig == NULL) {
			     clocksig = (sigact *)malloc(sizeof(sigact));
			     clocksig->next = topmod->clocklist;
			     topmod->clocklist = clocksig;
			     clocksig->name = strdup(token);
			     clocksig->edgetype = edgetype;

			     // Check if this clock is in the module's input
			     // list.  Add it to the <module_name>.clk
			     // file, and indicate if it is in the module's
			     // input list, and if it is a wire or a register.
			     //
			     // Note that if the clock is in the module's
			     // output list it will be called an input;  this
			     // is okay for our purpose (see vmunge.c).

			     for (testvec = topmod->iolist; testvec;
						testvec = testvec->next)
				if (!strcmp(testvec->name, clocksig->name))
				    break;

			     if (testvec != NULL)
				fprintf(fclk, "%s input wire\n", clocksig->name);
			     else {
				 for (testvec = topmod->wirelist; testvec;
						testvec = testvec->next)
				    if (!strcmp(testvec->name, clocksig->name))
				       break;

				 if (testvec != NULL)
				    fprintf(fclk, "%s internal wire\n", clocksig->name);
				 else
				    fprintf(fclk, "%s internal register\n",
						clocksig->name);
			     }
		          }
		          else {
			     resetsig = (sigact *)malloc(sizeof(sigact));
			     resetsig->next = topmod->resetlist;
			     topmod->resetlist = resetsig;
			     resetsig->name = strdup(token);
			     resetsig->edgetype = edgetype;
		          }
		          if (DEBUG) printf("Adding clock or reset signal \"%s\"\n",
					token);

			  if (bptr != NULL) {
			     *bptr = ')';
			     state = PEND_CLKBLOCK;

			     // Regenerate always @() line with posedge clock only,
			     // no reset, and no termination (yet)
			     if (ftmp != NULL && clocksig != NULL) {
				fprintf(ftmp, "always @( posedge %s ) ",
						clocksig->name);
			     }
			     suspend = 1;
			  }
		       }
		       else {
			  // This is one of those blocks where a sensitivity
			  // list is used to make a wire assignment look like
			  // a register, in defiance of all logic.
			  state = MAIN_BODY;
			  suspend = 0;
		       }
		    }
		    break;
		
		case PEND_CLKBLOCK:
		    if (!strcmp(token, "begin")) {
			state = IN_CLKBLOCK;
			testreset = NULL;
			resetsig = NULL;
			blocklevel++;

			// NOTE:  To-do:  Generate inverted clock signal for
			// use with negedge statements!

			// Finish always @() line
			if (ftmp != NULL && clocksig != NULL) {
			    fprintf(ftmp, " begin\n");
			}
			suspend = 2;
		    }
		    else if (!strcmp(token, "if")) {
			state = IN_CLKBLOCK | IN_IFTEST;
			testreset = NULL;
			resetsig = NULL;

			// Regenerate always @() line with posedge clock only,
			// no reset, and no "begin" line
			if (ftmp != NULL && clocksig != NULL) {
			    fprintf(ftmp, "\n");
			}
			suspend = 1;
		    }
		    else {
			if (ftmp != NULL) fprintf(ftmp, "\n");
			state = IN_CLKBLOCK;
			testreset = NULL;
			resetsig = NULL;
			initvec = NULL;
			condition = -1;
			suspend = 0;
		    }
		    break;

		case IN_CLKBLOCK:

		    if (!strcmp(token, "begin")) {
			blocklevel++;
		    }
		    else if (!strcmp(token, "end")) {
			blocklevel--;
			if (blocklevel == 0) {
			    state = MAIN_BODY;		// done with this domain
			}
		    }
		    else if (!strcmp(token, "if")) {
			state |= IN_IFTEST;
			suspend = 1;			// suspend pending
		    }
		    else if (!strcmp(token, "else")) {
			if (suspend == 1) {		// done with reset clause
			    state = IN_CLKBLOCK | IF_PENDING;
			    suspend = 2;
			    testreset = NULL;
			}
			else
			    suspend = 1;		// suspend pending
		    }
		    break;

		    // Otherwise, the next token should be a statement
		    // of some sort.  If it's a reset condition assignment,
		    // break it out of the code.

		case (IN_CLKBLOCK | IN_IFTEST) :

		       // Pick up RHS/LHS

		       if ((sptr = strrchr(token, ')')) != NULL) {
			  *sptr = '\0';
		       }
		       if (testreset == NULL) {
			  /* Look for signal in list of reset signals */
			  for (testsig = topmod->resetlist; testsig != NULL;
					testsig = testsig->next) {
			     if (!strncmp(testsig->name, token, strlen(testsig->name)))
				break;
			  }
			  if (testsig != NULL) {
			     testreset = testsig;
			     suspend = 1;		// No output until end of block
			     if (DEBUG) printf("Parsing reset conditions for \"%s\"\n",
					testreset->name);

			     // Check here for condition if no space has been put between
			     // the signal name and the condition
			     if (strlen(token) > strlen(testreset->name)) {
			        token += strlen(testreset->name);
			        if (!strncmp(token, "==", 2))
				   condition = EQUAL; 
			        else if (!strncmp(token, "!=", 2))
				   condition = NOT_EQUAL; 
			     }
			  }
			  else {
			     // This is not a reset signal assignment, so we
			     // should cancel the suspend state.
			     suspend = 0;
			  }
		       }
		       else {
			   if (condition == -1) {
			      // We are looking for == or !=
			      if (!strncmp(token, "==", 2))
				 condition = EQUAL; 
			      else if (!strncmp(token, "!=", 2))
				 condition = NOT_EQUAL; 

			      if (strlen(token) > 2) token += 2;
			   }

			   // pick up RHS 
			   if ((ival = get_bitval(token)) != -1) {
			      // Generate reset name in init file
			      // TO-DO:  Handle negedge types by adding inverted signal
			      // For now, we assume posedge

			      if ((condition == EQUAL && ival == 1) ||
				  (condition == NOT_EQUAL && ival == 0)) {
				 fprintf(finit, "%s\n", testreset->name);
				 state = IN_CLKBLOCK | IF_PENDING;
			      }
			   }
		       }
		       if (sptr != NULL) {
			  *sptr = ')';
			  // And fall through
		       }
		       else
			  break;

		case (IN_CLKBLOCK | IF_PENDING) :

		    // Waiting for the ')' that ends the "if" test

		    if ((sptr = strchr(token, ')')) != NULL) {
			*sptr = '\0';
			state = IN_CLKBLOCK | IN_IFBLOCK;
			iflevel = 0;
		    }
		    else if (!strcmp(token, "begin")) {
			// Plain "else begin"
			iflevel++;
			state = IN_CLKBLOCK | IN_IFBLOCK;
		    }
		    break;

		case (IN_CLKBLOCK | IN_IFBLOCK):
		    if ((sptr = strchr(token, ';')) != NULL) {
			*sptr = '\0';
			// Use sptr to check for end-of-statement, below
		    }

		    if (!strcmp(token, "end")) {
			iflevel--;
			if (iflevel == 0) {
			    state &= ~IN_IFBLOCK;
			    if (ifcancel == 1 && suspend != 1) {
				ifcancel = 0;
				suspend = 2;
			    }
			    else if (suspend == 1)
				ifcancel = 1;
		        }
		    }
		    else if (!strcmp(token, "begin")) {		// Track nested begin/end
			iflevel++;
		    }
		    else if (testreset != NULL && suspend == 1) {

			// In the case that we have, e.g., "if (reset)", then
			// "condition" will be unknown.  Therefore assume a
			// positive condition and output the reset signal
			if (condition == UNKNOWN) {
			    condition = EQUAL;
			    fprintf(finit, "%s\n", testreset->name);
			}

			// This is a signal to add to init list.  Parse LHS, RHS
			if (initvec == NULL) {

			   // Remove any trailing index range from token
			   char *is_indexed = strchr(token, '[');
			   if (is_indexed != NULL) *is_indexed = '\0';

			   for (testvec = topmod->reglist; testvec != NULL;
					testvec = testvec->next) {
			      if (!strcmp(testvec->name, token))
				 break;
			   }
			   if (is_indexed) *is_indexed = '[';
			   if (testvec == NULL) {
			      fprintf(stderr, "Error, line %d:  Reset condition is not an"
					" assignment to a known registered signal.\n",
					line_num);
			   }
			   else {
			      initvec = testvec;
			   }
			}
			else {
			   if (!strcmp(token, "<=")) break;
			   else if (!strcmp(token, "=")) break;
			   else if (!strncmp(token, "<=", 2))
			      token += 2;
			   else if (!strncmp(token, "=", 1))
			      token++;  // Not supposed to have blocking assignments
					// here, but we will handle them anyway.

			   if (strlen(token) > 0) {
			      if (DEBUG) printf("Reset \"%s\" to \"%s\"\n",
					initvec->name, token);
			      j = initvec->vector_start;

			      if ((bptr = parse_bit(line_num, topmod, token, j)) != NULL) {

				 // NOTE:  The finit file uses signal<idx> notation
				 // compatible with the .bdnet file, not the signal[idx]
				 // notation compatible with verilog.

				 if (initvec->vector_size > 0)
				    fprintf(finit, "%s<%d> %s\n", initvec->name, j, bptr);
				 else
				    fprintf(finit, "%s %s\n", initvec->name, bptr);
				 for (i = 1; i < initvec->vector_size; i++) {
				    if (initvec->vector_start > initvec->vector_end)
				       j--;
				    else
				       j++;
			            bptr = parse_bit(line_num, topmod, token, j);
				    if (bptr != NULL)
				       fprintf(finit, "%s<%d> %s\n", initvec->name,
						j, bptr);
				 }
			      }
			      initvec = NULL;
			   }
			}
		    }
		    if (sptr != NULL) {
			*sptr = ';';
			if (iflevel == 0) {
			    if (blocklevel == 0)
				state = MAIN_BODY;
			    else
				state = IN_CLKBLOCK;
			}
		    }
		    break;

		case COMMENT:
		    /* Pass comment lines to output */
		    if (ftmp != NULL) fputs(linebuf, ftmp);
		    break;

		default:
		    break;
	    }

	    /* Proceed to next token */

	    switch(state) {
	        /* State-dependent token processing */
		case MODULE_VALID:
		case MODULE_VALID | INPUT_OUTPUT:
		   toklist = " \t\n(),";
		   break;

		case MAIN_BODY | INPUT_OUTPUT:
		case MAIN_BODY | WIRE:
		case MAIN_BODY | REGISTER:
		   toklist = " \t\n[:],";
		   break;

		case MAIN_BODY:
		   toklist = " \t\n@(";
		   break;

		case SENS_LIST:
		   toklist = " \t\n(";
		   break;

		case IN_CLKBLOCK:
		   toklist = " \t\n;(";
		   break;

		case PEND_CLKBLOCK:
		case IN_CLKBLOCK | IF_PENDING:
		case IN_CLKBLOCK | IN_IFTEST:
		case IN_CLKBLOCK | IN_IFBLOCK:
		   toklist = " \t\n(";
		   break;

		default:
		   toklist = " \t\n";
		   break;
	    }

	    if (no_new_token == 0)
	       token = strtok(NULL, toklist);
	    no_new_token = 0;
	}

	/* Proceed to next line;  if we get back NULL, we're at EOF */

	if (suspend == 0 && ftmp != NULL) fputs(linecopy, ftmp);

	// Resolve pending exit from suspended state
	if (suspend == 2) suspend = 0;

	line_num++;

	if (fgets(linebuf, 2047, fsource) == NULL)
	    break;
    }

    /* Done! */

    fclose(fsource);
    if (finit != NULL) fclose(finit);
    if (fclk != NULL) fclose(fclk);
    if (ftmp != NULL) fclose(ftmp);
    exit(0);
}
