/*------------------------------------------------------*/
/* vsplit.c						*/
/*							*/
/* Tokenize a verilog source file and parse it for	*/
/* structures that our simple VIS/SIS flow cannot	*/
/* handle.						*/
/*							*/
/* 1) Reset signals are pulled out of the code, and	*/
/*    the reset conditions are saved to a file named	*/
/*    "<rootname>.init".  These are picked up later	*/
/*    and used to replace standard flops with set,	*/
/*    reset, or both flops.				*/
/*							*/
/* 2) Inverted clocks are made non-inverting, with a	*/
/*    file called "<rootname>.clk" made to track which	*/
/*    clocks are inverted.  Inverters will be put in	*/
/*    front of the clock networks in the final netlist.	*/
/*							*/
/* 3) The file is broken up into multiple source files	*/
/*    for each clock found, such that each file has	*/
/*    only one clock signal.  The resulting netlists	*/
/*    will be stitched together at the end, and the	*/
/*    original source file will be used to determine	*/
/*    which pins are the I/O of the whole module.	*/
/*							*/
/* 4) Because VIS does not understand the "parameter"	*/
/*    statement, parameters substitutions are done by	*/
/*    vsplit.						*/
/*------------------------------------------------------*/

#define DEBUG 1

#include <stdio.h>
#include <string.h>
#include <ctype.h>		// For isalnum()
#include <malloc.h>
#include <stdlib.h>		// For exit(n)

// Meta-structure for storing vector lists
typedef struct _vlist *vlistptr;

// Signal (single-bit) data structure
typedef struct _sigact *sigactptr;

typedef struct _sigact {
   char *name;			// Name of clock, reset, or enable signal
   char edgetype;		// POSEDGE or NEGEDGE
   vlistptr depend;		// signals that are used in this domain
   sigactptr next;
} sigact;

// Vector data structure
typedef struct _vector *vecptr;

typedef struct _vector {
   char *name;
   int vector_size;		// zero means signal is not a vector
   int vector_start;
   int vector_end;
   sigactptr clock;		// pointer to clock that updates this vector
   vecptr next;
} vector;

typedef struct _vlist {
   vecptr depend;
   char registered;		// Is signal registered in this domain?
   char output;			// Has signal been written to the output? (see below)
   vlistptr next;
} vlist;

// Definition of values used for "output" variable in vlist:
#define NO_OUTPUT	0	// Signal has not been written to I/O list
#define OUTPUT_OK	1	// Signal has been written to I/O list
#define IS_INPUT	2	// Signal will need "input" line
#define IS_OUTPUT	3	// Signal will need "output" line


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
#define  ALWAYS_BLOCK	0x0010		// Inside "always" block (domain splitting)
#define  SENS_LIST	0x0010		// Parsing a sensitivity list
#define  IN_CLKBLOCK	0x0020		// Within a begin...end block after always()
#define  IN_IFELSE	0x0040		// Within an if...else condition
#define  IN_IFBLOCK	0x0080		// Within a begin...end block after if()
#define  COMMENT	0x0100		// Within a C-type comment
#define  ASSIGNMENT_LHS	0x0200		// In a multi-line assignment
#define  ASSIGNMENT	0x0200		// In an assignment (during domain splitting)
#define  ASSIGNMENT_RHS	0x0400		// In a multi-line assignment
#define  WIRE		0x0800		// In a wire declaration
#define  REGISTER	0x1000		// In a register declaration

// Edge types
#define  NEGEDGE	1
#define  POSEDGE	2

// Condition types
#define	 UNKNOWN	-1
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
				" on RHS.  Padding by repitition.\n", line_num);
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
/* Add a dependency to a clock domain			*/
/*------------------------------------------------------*/

void
add_dependency(vector *testvec, sigact *clocksig, char is_registered)
{
    vlistptr newdepend, tdepend;

    // Check clocksig's depend list.  If testvec is on the list, then
    // ignore it.  However, if this dependency shows that the signal
    // is registered in this domain, and the testvec is not marked
    // as registered, then mark it before returning.

    for (tdepend = clocksig->depend; tdepend; tdepend = tdepend->next)
	if (tdepend->depend == testvec) {
	    if (tdepend->registered == (char)0 && is_registered != (char)0)
		tdepend->registered = is_registered;    
	    return;
	}

    // Otherwise prepend the signal to the dependency
    // list for the clock domain.

    newdepend = (vlistptr)malloc(sizeof(vlist));
    newdepend->next = clocksig->depend;
    clocksig->depend = newdepend;
    newdepend->depend = testvec;
    newdepend->registered = is_registered;
    newdepend->output = NO_OUTPUT;
}

/*------------------------------------------------------*/
/* Record any signal found in "token" as a dependency	*/
/* of the module with clock "clocksig".			*/
/*							*/
/* NOTE:  This routine should be heavily optimized,	*/
/* since it is used to search every token in any 	*/
/* "always" block for a match to known signal names.	*/
/* Hashing would be a good idea.			*/
/*------------------------------------------------------*/

void
check_depend(module *thismod, char *token, sigact *clocksig, char is_registered)
{
    char tcopy[256];
    char *aptr, *bptr;
    vector *testvec;

    strncpy(tcopy, token, 255);		/* Don't mess with token */
    bptr = tcopy;

    // NOTE:  Valid verilog names must begin with a letter or underscore,
    // and may contain only letters, numbers, underscores, and dollar signs.
 
    while (!isalpha(*bptr) && (*bptr != '\0') && (*bptr != '_')) bptr++;
    if (isalpha(*bptr) || (*bptr == '_')) {
	aptr = bptr;
	while (*aptr != '\0') {
	    if (!isalnum(*aptr) && (*aptr != '_') && (*aptr != '$')) {
		*aptr = '\0';
		break;
	    }
	    else
		aptr++;
	}

	// Check RHS token for a wire name
	for (testvec = thismod->wirelist; testvec != NULL;
				testvec = testvec->next) {
	    if (!strcmp(testvec->name, bptr)) {
		// Add testvec to the list of dependencies for clocksig
		add_dependency(testvec, clocksig, is_registered);
		break;
	    }
	}
	if (testvec == NULL) {
	    // Check RHS token for a register name
	    for (testvec = thismod->reglist; testvec != NULL;
				testvec = testvec->next) {
		if (!strcmp(testvec->name, bptr)) {
		    // Add testvec to the list of dependencies for clocksig
		    add_dependency(testvec, clocksig, is_registered);
		    break;
		}
	    }
	}
	if (testvec == NULL) {
	    // Check RHS token for an I/O name, since it's
	    // possible for inputs not to be declared as wires,
	    // so they might not be in the wire list
	    for (testvec = thismod->iolist; testvec != NULL;
				testvec = testvec->next) {
		if (!strcmp(testvec->name, bptr)) {
		    // Add testvec to the list of dependencies for clocksig
		    add_dependency(testvec, clocksig, is_registered);
		    break;
		}
	    }
	}

	// Move aptr past the testvec name and continue searching,
	// since verilog does not require whitespace and the token
	// may contain multiple name references, e.g., "a+b+c".

	if (testvec)
	    aptr = bptr + strlen(testvec->name);
	else {
	    while (isalnum(*aptr) || (*aptr == '_') || (*aptr == '$')) {
		aptr++;
		if (*aptr == '\0') break;
	    }
	}
    }
}

/*------------------------------------------------------*/
/* Return a pointer to the clock signal of the domain	*/
/* in which signal "testv" is registered.  If there is	*/
/* none, then "testv" is a wire and NULL is returned.	*/
/*------------------------------------------------------*/

sigact *
signal_registered_in(module *topmod, vlistptr testv, vlistptr *testvr)
{
    vlistptr testv2;
    sigact *clocksig;

    for (clocksig = topmod->clocklist; clocksig; clocksig = clocksig->next)
	for (testv2 = clocksig->depend; testv2; testv2 = testv2->next)
	    if ((testv2->depend == testv->depend) && (testv2->registered == (char)1)) {
		if (testvr) *testvr = testv2; 
		return clocksig;
	    }

    return NULL;	/* Signal is not registered, must be a wire */	
}

/*------------------------------------------------------*/
/* Check if a signal is used in a module other than	*/
/* clocksig (where it is assumed to be registered).	*/
/* Return 1 if true, 0 if false.			*/
/*							*/
/* If clocksig is not combdomain (assignments are not	*/
/* made in the domain of clocksig), then we need to	*/
/* check the dependency list for wiredomain as well.	*/
/*------------------------------------------------------*/

char
signal_used_in(module *topmod, vlistptr testv, sigact *clocksig,
		sigact *combdomain, sigact *wiredomain)
{
    sigact *testclk;
    vlistptr testv2;

    for (testclk = topmod->clocklist; testclk; testclk = testclk->next) {
	if (testclk == clocksig) continue;
	for (testv2 = testclk->depend; testv2; testv2 = testv2->next)
	    if (testv2->depend == testv->depend)
		return (char)1;
    }

    if (clocksig != combdomain)
    {
	for (testv2 = wiredomain->depend; testv2; testv2 = testv2->next)
	    if (testv2->depend == testv->depend)
		return (char)1;
    }

    return (char)0;
}

/*------------------------------------------------------*/
/* Write a single clock domain output verilog file	*/
/*							*/
/* "clocksig" points to the clock domain to be output	*/
/* on this call.  It may be NULL if we are writing a	*/
/* combinatorial-only output.				*/
/*							*/
/* "combdomain" points to the clock domain to which	*/
/* combinatorial signals have been assigned.  It may	*/
/* be NULL if combinatorial signals go in their own	*/
/* domain, without any clock.				*/
/*							*/
/* "wiredomain" is a pointer to the combinatorial	*/
/* dependency list.  These dependencies should be added	*/
/* to those for "clocksig" when "clocksig" is equal to	*/
/* "combdomain".					*/
/*------------------------------------------------------*/

void
write_single_domain(FILE *fsource, FILE *ftmp, sigact *clocksig,
	module *topmod, sigact *combdomain, sigact *wiredomain)
{
    char linebuf[2048];
    char linecopy[2048];
    const char *toklist;
    char *token, *fptr, *aptr;
    int state, line_num, ival;
    char suspend, check_clock, iolist_complete, printed_last;
    vector *testvec;
    vlistptr testv, testv2;
    sigact *otherdomain;

    token = NULL;
    toklist = " \t\n";
    state = HEADER_STUFF;
    suspend = (char)0;
    check_clock = (char)0;
    iolist_complete = (char)0;
    printed_last = (char)0;
    line_num = 1;

    rewind(fsource);

    fgets(linebuf, 2047, fsource);
    fptr = linebuf;

    while (1) {		/* Read continuously and break loop when input is exhausted */

	strcpy(linecopy, linebuf);	// Keep a copy of the line we're processing
	token = strtok(fptr, toklist);

	while (token != NULL) {

	    /* State-independent processing */
	    /* Note that parameters have already been removed from this file */

	    if (!strncmp(token, "//", 2)) {
		break;	// Forget rest of line and read next line
	    }

	    /* State-dependend processing */

	    switch (state) {
		case HEADER_STUFF:
		    if (!strcmp(token, "module")) {
		        state &= ~HEADER_STUFF;
		        state |= MODULE_VALID;
		    }
		    break;

		case MODULE_VALID:
		    state |= INPUT_OUTPUT;
		    break;

		case MODULE_VALID | INPUT_OUTPUT:
		    if (!strcmp(token, ";")) {
			// Analyze the signals for the following two cases:
			// 1) If a signal is used in Y, but registered in X,
			//	it is an input of Y and an output of X.
			// 2) If a signal is used in Y, not registered, and
			//	Y is not wiredomain, then it is an input of
			//	Y and an output of wiredomain
			for (testv = clocksig->depend; testv; testv = testv->next) {
			    if (testv->output == IS_OUTPUT) {
				if (printed_last) fprintf(ftmp, ",");
				fprintf(ftmp, "%s\n", testv->depend->name);
				printed_last = (char)1;
			    }
			    otherdomain = signal_registered_in(topmod, testv, &testv2);
			    if (otherdomain != NULL && otherdomain != clocksig) {
				if (testv->output == NO_OUTPUT) {
				    testv->output = IS_INPUT;
				    if (printed_last) fprintf(ftmp, ",");
				    fprintf(ftmp, "%s\n", testv->depend->name);
				    printed_last = (char)1;
				}
				if (testv2->output == NO_OUTPUT)
				    testv2->output = IS_OUTPUT;
			    }
			    else if (otherdomain == clocksig && testv->output ==
					NO_OUTPUT) {
				if (signal_used_in(topmod, testv, clocksig,
						combdomain, wiredomain)) {
				    testv->output = IS_OUTPUT;
				    if (printed_last) fprintf(ftmp, ",");
				    fprintf(ftmp, "%s\n", testv->depend->name);
				    printed_last = (char)1;
				}
			    }
			    else if (otherdomain == NULL && clocksig != combdomain &&
					testv->output == NO_OUTPUT) {
				if (printed_last) fprintf(ftmp, ",");
				fprintf(ftmp, "%s\n", testv->depend->name);
				printed_last = (char)1;
				testv->output = IS_INPUT;

				// Find this signal in wiredomain and make it an
				// output.

				for (testv2 = wiredomain->depend; testv2; testv2 =
						testv2->next) {
				    if (testv2->depend == testv->depend) {
					testv2->output = IS_OUTPUT;
					break;
				    }
				}
			    }
			}
			if (clocksig == combdomain) {
			    for (testv = wiredomain->depend; testv; testv = testv->next) {
				if (testv->output == IS_OUTPUT) {
				    if (printed_last) fprintf(ftmp, ",");
				    fprintf(ftmp, "%s\n", testv->depend->name);
				    printed_last = (char)1;
				}
				otherdomain = signal_registered_in(topmod, testv, &testv2);
				if (otherdomain != NULL && otherdomain != clocksig
					&& otherdomain != wiredomain) {
				    if (testv->output == NO_OUTPUT) {
					testv->output = IS_INPUT;
					if (printed_last) fprintf(ftmp, ",");
					fprintf(ftmp, "%s\n", testv->depend->name);
					printed_last = (char)1;
				    }
				    if (testv2->output == NO_OUTPUT)
					testv2->output = IS_OUTPUT;
				}
			    }

			    // If any clock signal appears in wirelist,
			    // then it is an output of wiredomain.

			    for (otherdomain = topmod->clocklist; otherdomain;
						otherdomain = otherdomain->next) {
				if (otherdomain != clocksig) {
				    for (testvec = topmod->wirelist; testvec;
						testvec = testvec->next) {
					if (!strcmp(testvec->name,
							otherdomain->name)) {
					    // We found a clock signal that is
					    // an assigned value.  Find it again
					    // in the dependency list for
					    // wiredomain.
					    for (testv = wiredomain->depend;
							testv; testv = testv->next)
						if (testv->depend == testvec)
						    if (testv->output != OUTPUT_OK) {
							testv->output = IS_OUTPUT;
							if (printed_last)
							    fprintf(ftmp, ",");
							fprintf(ftmp, "%s\n",
								testv->depend->name);
							printed_last = (char)1;
							break;
						    }
					}
				    }
				}
			    }
			}
	
			suspend = 0;
		        state = MAIN_BODY;
			break;
		    }

		    // Parse the clocksig depend list and copy only
		    // those signals on the list.
		    for (testv = clocksig->depend; testv; testv = testv->next)
			if (!strcmp(token, testv->depend->name))
			    break;

		    if ((testv == NULL) && (clocksig == combdomain))
			for (testv = wiredomain->depend; testv; testv = testv->next)
			    if (!strcmp(token, testv->depend->name))
				break;

		    if (testv == NULL) {
			suspend = 2;	/* Don't output this line */
			printed_last = (char)0;
		    }
		    else {
			testv->output = OUTPUT_OK;	// Signal has been output
			printed_last = (char)1;		// in module's I/O list
		    }

		    break;

		case MAIN_BODY | WIRE:
		case MAIN_BODY | REGISTER:
		case MAIN_BODY | INPUT_OUTPUT:
		    // Remove any semicolon but remember that it was there
		    if ((aptr = strchr(token, ';')) != NULL)
			*aptr = '\0';

		    // Ignore array bounds
		    if (sscanf(token, "%d", &ival) != 1) {

			// Parse the clocksig depend list and copy only
			// those signals on the list.
			for (testv = clocksig->depend; testv; testv = testv->next)
			    if (!strcmp(token, testv->depend->name))
				break;

			if ((testv == NULL) && (clocksig == combdomain))
			    for (testv = wiredomain->depend; testv; testv = testv->next)
				if (!strcmp(token, testv->depend->name))
				    break;

			if (testv == NULL)
			    suspend = 2;	/* Don't output this line */

			else if (state & REGISTER) {
			    if (testv->registered == (char)0)
				suspend = 2;	/* Not registered in this domain */
			}
		    }
		    if (aptr != NULL) {
			*aptr = ';';
		        state = MAIN_BODY;	// Return to main body processing
		    }
		    break;

		case MAIN_BODY | ASSIGNMENT:
		    // Assignments are only output when clocksig matches combdomain
		    // (both may be NULL)
		    if (clocksig != combdomain) suspend = 2;

		    // Remove any semicolon but remember that it was there
		    if ((aptr = strchr(token, ';')) != NULL)
			state = MAIN_BODY;	// Return to main body processing

		    break;

		case MAIN_BODY:
		    if (!strcmp(token, "input")) {
			state |= INPUT_OUTPUT;
		    }
		    else if (!strcmp(token, "output")) {
			state |= INPUT_OUTPUT;
		    }
		    else if (!strcmp(token, "wire")) {
			// Add to list of known wires
			state |= WIRE;
		    }
		    else if (!strcmp(token, "reg")) {
			// Add to list of known registers
			state |= REGISTER;
		    }
		    else if (!strcmp(token, "assign")) {
			state |= ASSIGNMENT;
		    }
		    else if (!strncmp(token, "always", 6)) {
			state &= ~MAIN_BODY;
			state |= ALWAYS_BLOCK;
		    }
		    else if (!strcmp(token, "endmodule")) {
			if (DEBUG) printf("End of module \"%s\" found.\n", topmod->name);
			state = HEADER_STUFF;
		    }

		    // The first time we encounter a wire or register,
		    // we need to add the rest of the I/O to the list of
		    // inputs and outputs.

		    if ((state & WIRE) || (state & REGISTER)) {
			if (iolist_complete == (char)0) {
			    iolist_complete = (char)1;

			    // Find all domain dependencies that have been
			    // marked IS_INPUT or IS_OUTPUT, and write an
			    // output line for each.

			    for (testv = clocksig->depend; testv; testv = testv->next) {
				if (testv->output == IS_INPUT
						|| testv->output == IS_OUTPUT) {
				    if (testv->output == IS_INPUT)
					fprintf(ftmp, "input ");
				    else if (testv->output == IS_OUTPUT)
					fprintf(ftmp, "output ");
				    if (testv->depend->vector_size > 1)
					fprintf(ftmp, "[%d:%d] ",
						testv->depend->vector_start,
						testv->depend->vector_end);
				    fprintf(ftmp, "%s;\n", testv->depend->name);
				    testv->output = OUTPUT_OK;
				}
			    }
			    if (clocksig == combdomain) {
				for (testv = wiredomain->depend; testv;
						testv = testv->next) {
				    if (testv->output == IS_INPUT
						|| testv->output == IS_OUTPUT) {
					if (testv->output == IS_INPUT)
				            fprintf(ftmp, "input ");
					else if (testv->output == IS_OUTPUT)
				            fprintf(ftmp, "output ");
					if (testv->depend->vector_size > 1)
					    fprintf(ftmp, "[%d:%d] ",
						testv->depend->vector_start,
						testv->depend->vector_end);
					fprintf(ftmp, "%s;\n", testv->depend->name);
					testv->output = OUTPUT_OK;
				    }
				}
			    }
			}
		    }

		    break;

		case ALWAYS_BLOCK:
		    if (!strcmp(token, "posedge") || !strcmp(token, "negedge")) {
			check_clock = (char)1;
		    }
		    else if (check_clock == (char)1) {
			// This token is a clock signal.  If it matches the
			// domain, we're good.  If not, we suspend output
			// until the next "always" block or "endmodule".

			if (strcmp(token, clocksig->name)) suspend = 1;
			check_clock = (char)0;
		    }
		    else if (!strcmp(token, "always") || !strcmp(token, "endmodule"))
			suspend = 0;
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
		   toklist = " \t\n()";
		   break;

		case IN_CLKBLOCK | IN_IFELSE:
		   toklist = " \t\n(";
		   break;

		case IN_CLKBLOCK | IN_IFELSE | IN_IFBLOCK:
		   toklist = " \t\n;";
		   break;

		default:
		   toklist = " \t\n";
		   break;
	    }
	    token = strtok(NULL, toklist);
	}

	/* Proceed to next line;  if we get back NULL, we're at EOF */

	if (suspend == 0 && ftmp != NULL) fputs(linecopy, ftmp);
	if (suspend == 2) suspend = 0;
	line_num++;

	if (fgets(linebuf, 2047, fsource) == NULL)
	    break;
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
    FILE *ftmp = NULL;

    char comment_pending;
    char *xp, *fptr, *token, *sptr, *bptr, *lasttok, *sigp, *aptr;
    char locfname[512];
    char linebuf[2048];
    char linecopy[2048];
    const char *toklist;

    module *topmod;
    sigact *clocksig, wiredomain;
    sigact *resetsig, *testreset, *testsig;
    vector *initvec, *testvec, *newvec, *lhssig;
    vlist  *newdepend;
    parameter *params = NULL;

    int state, nextstate;
    int line_num;
    int start, end, ival, i, j, k;
    int blocklevel;
    int no_new_token;
    int condition;
    char edgetype;
    char suspend;		/* When =1, suspend output */
    char multidomain;
    sigact *combdomain;
 
    /* Only one argument, which is the source filename */

    if (argc != 2) {
	fprintf(stderr, "Usage:  vsplit <source_file.v>\n");
	exit(1);
    }

    /* If argv[1] does not have a .v extension, make one */

    xp = strrchr(argv[1], '.');
    if (xp != NULL)
        snprintf(locfname, 511, "%s", argv[1]);
    else
        snprintf(locfname, 511, "%s.v", argv[1]);

    fsource = fopen(locfname, "r");
    if (fsource == NULL) {
	fprintf(stderr, "Error:  No such file or cannot open file \"%s\"\n", locfname);
	exit(1);
    }

    /* Okay, got all the file handles, start reading */

    if (fgets(linebuf, 2047, fsource) == NULL) {
	fprintf(stderr, "Error reading source file \"%s.v\"", locfname);
	exit(1);
    }
    fptr = linebuf;

    /* wiredomain is a fixed sigact structure that we use to assign all	*/
    /* signal dependencies used by "assign" statements.  This can	*/
    /* either be assigned to an independent domain or merged with one	*/
    /* of the clock domains.						*/

    wiredomain.name = NULL;
    wiredomain.next = NULL;
    wiredomain.depend = NULL;
    wiredomain.edgetype = 0;

    state = HEADER_STUFF;
    suspend = (char)0;
    line_num = 1;
    no_new_token = 0;	/* If 1, don't move to next token */
    blocklevel = 0;	/* Nesting of begin...end blocks */
    condition = UNKNOWN;
    multidomain = 0;
    combdomain = NULL;
    lasttok = NULL;
    lhssig = NULL;
    clocksig = NULL;
    token = NULL;
    toklist = " \t\n";	/* Default token list at beginning (state HEADER_STUFF) */

    while (1) {		/* Read continuously and break loop when input is exhausted */

	paramcpy(linecopy, linebuf, params);	// Substitute parameters
	strcpy(linebuf, linecopy);	// Keep a copy of the line we're processing

	if (no_new_token == 0) {
	   if (token != NULL) {
	      if (lasttok != NULL) free(lasttok);
	      lasttok = strdup(token);
	   }
	   token = strtok(fptr, toklist);
	}
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
	    }
	    if (!strcmp(token, "parameter") || !strcmp(token, "`define")) {
		toklist = " \t\n=;";
		/* Get parameter name */
	        token = strtok(NULL, toklist);
		if (token != NULL) {
		   parameter *newparam = (parameter *)malloc(sizeof(parameter));
		   newparam->name = strdup(token);
		   newparam->next = NULL;

		   /* Get parameter value */
	           token = strtok(NULL, toklist);
		   newparam->value = strdup(token);

		   /* Sort tokens by size to avoid substituting	part	*/
		   /* of a parameter (e.g., "START" and "START1" should	*/
		   /* not be ambiguous!)				*/

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
		suspend = 2;		/* Don't output this line */
		break;			/* No further processing on this line */
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

		    /* Create the files we want to write to for this module */
		    sprintf(locfname, "%s.init", topmod->name);
		    finit = fopen(locfname, "w");
		    if (finit == NULL) {
		       fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
		    }

		    sprintf(locfname, "%s_tmp.v", topmod->name);
		    ftmp = fopen(locfname, "w");
		    if (ftmp == NULL) {
			fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
		    }

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
		       newvec->clock = NULL;	// To be filled later

		       if (state & INPUT_OUTPUT) {
		          newvec->next = topmod->iolist;
		          topmod->iolist = newvec;
		          if (DEBUG) printf("Adding new I/O signal \"%s\"\n", token);
		       }
		       else if (state & WIRE) {
		          newvec->next = topmod->wirelist;
		          topmod->wirelist = newvec;
		          if (DEBUG) printf("Adding new wire \"%s\"\n", token);
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
		       start = -1;
		       end = -1;
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
		    else if (lhssig == NULL) {
		       /* Record this assignment in case it is used as a clock or reset */
		       if (DEBUG) printf("Processing assignment of \"%s\". . .\n", token);
		       bptr = strchr(token, '[');
		       if (bptr != NULL) *bptr = '\0';
		       for (testvec = topmod->wirelist; testvec != NULL;
					testvec = testvec->next) {
			  if (!strcmp(testvec->name, token)) {
			     lhssig = testvec;
			     break;
			  }
		       }
		    }
		    // No break here;  drop through.

		case MAIN_BODY | ASSIGNMENT_RHS:
		    // Check for a terminating semicolon here.
		    sptr = strchr(token, ';');

		    // Record any dependencies for the LHS signal.
		    check_depend(topmod, token, &wiredomain, (char)0);

		    if (sptr != NULL) {
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
		        lhssig = NULL;
			state |= ASSIGNMENT_LHS;
		    }
		    else if (!strncmp(token, "always", 6)) {
			state &= ~MAIN_BODY;
			state |= SENS_LIST;
			edgetype = 0;
			clocksig = NULL;
			resetsig = NULL;
			condition = UNKNOWN;
		        lhssig = NULL;
			initvec = NULL;
			suspend = 1;
		    }
		    else if (!strcmp(token, "endmodule")) {
			if (DEBUG) printf("End of module \"%s\" found.\n", topmod->name);
			state = HEADER_STUFF;
		    }
		    break;

		case SENS_LIST:
		    if (!strcmp(token, "begin")) {
			state &= ~SENS_LIST;
			state |= IN_CLKBLOCK;
			testreset = NULL;
			resetsig = NULL;
			blocklevel++;

			// NOTE:  To-do:  Generate inverted clock signal for
			// use with negedge statements!

			// Regenerate always @() line with posedge clock only
			if (ftmp != NULL && clocksig != NULL) {
			    fprintf(ftmp, "always @( posedge %s ) begin\n",
					clocksig->name);
			}
			suspend = 1;
		    }
		    else if (!strcmp(token, "if")) {
			state &= ~SENS_LIST;
			state |= IN_CLKBLOCK | IN_IFELSE;
			testreset = NULL;
			resetsig = NULL;
			blocklevel++;

			// Regenerate always @() line with posedge clock only,
			// no reset, and no "begin" line
			if (ftmp != NULL && clocksig != NULL) {
			    fprintf(ftmp, "always @( posedge %s ) \n",
					clocksig->name);
			}
			suspend = 1;
		    }
		    else if (!strcmp(token, "posedge")) {
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
		       if (edgetype == POSEDGE || edgetype == NEGEDGE) {
		          /* Parse this signal */
		          if (clocksig == NULL) {
			     clocksig = (sigact *)malloc(sizeof(sigact));
			     clocksig->next = topmod->clocklist;
			     topmod->clocklist = clocksig;
			     clocksig->name = strdup(token);
			     clocksig->edgetype = edgetype;
			     clocksig->depend = NULL;
		          }
		          else {
			     resetsig = (sigact *)malloc(sizeof(sigact));
			     resetsig->next = topmod->resetlist;
			     topmod->resetlist = resetsig;
			     resetsig->name = strdup(token);
			     resetsig->edgetype = edgetype;
			     resetsig->depend = NULL;
		          }
		          if (DEBUG) printf("Adding clock or reset signal \"%s\"\n",
					token);
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
		
		case IN_CLKBLOCK:
		    if (!strncmp(token, "if", 2)) {
			state |= IN_IFELSE;
			suspend = 1;		// Re-enter suspended state on "if"
		    }
		    else if (!strcmp(token, "else")) {
			state |= IN_IFELSE;
			if (suspend == 1)
			   suspend = 2;		// Pending exit suspended state
		    }
		    else if (!strcmp(token, "end")) {
			state = MAIN_BODY;	// Exit "always" block
			blocklevel--;		// Blocklevel goes back to zero
		    }
		    break;

		case (IN_CLKBLOCK | IN_IFELSE):
		    if (!strcmp(token, "begin")) {
			state |= IN_IFBLOCK;
			blocklevel++;
		    }
		    else if (!strcmp(token, "if")) {
		       /* Ignore "if" in "else if" */
		    }

		    // An "always" block may contain only if..else
		    // and omit the "begin" and "end" block delimiters.
		    // If so, then another "always" or an "endmodule"
		    // forces the end of the "always" block.

		    else if (!strcmp(token, "always")) {
			state = MAIN_BODY;
			no_new_token = 1;
			blocklevel = 0;
			break;
		    } else if (!strcmp(token, "endmodule")) {
			state = MAIN_BODY;
			no_new_token = 1;
			blocklevel = 0;
			break;
		    }

		    // Otherwise, the next token should be a statement
		    // of some sort.  If it's a reset condition assignment,
		    // break it out of the code.

		    else {	// Pick up RHS/LHS
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
			     state &= ~IN_IFELSE;
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
			      }
			      else {
				 /* This is a normal code block */
				 suspend = 0;
				 if (DEBUG) printf("Processing standard code block.\n");
			      }
			   }
		       }
		    }

		    break;

		case (IN_CLKBLOCK | IN_IFELSE | IN_IFBLOCK):

		    if (!strcmp(token, "end")) {
			blocklevel--;
			if (blocklevel == 1) {
			    state &= ~IN_IFBLOCK;
			    state &= ~IN_IFELSE;
			    suspend = 2;		// Pending suspend exit
			    testreset = NULL;
			    initvec = NULL;
			    condition = -1;
		        }
		    }
		    else if (!strcmp(token, "begin")) {		// Track nested begin/end
			blocklevel++;
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
		    break;

		case (IN_IFELSE):
		    if (!strcmp(token, "begin")) {
			state &= ~IN_IFELSE;
			state |= IN_IFBLOCK;
		    }
		    else if (!strcmp(token, "if")) {
			/* In an "else if" block */
		    }
		    else {
			/* Single-line statement, no block used */
		    }
		    break;

		case (IN_IFBLOCK):
		    if (!strcmp(token, "end")) {
			state &= ~IN_IFBLOCK;
		    }
		    break;

		case COMMENT:
		    /* Pass comment lines to output */
		    if (ftmp != NULL) fputs(linebuf, ftmp);
		    break;

		default:
		    break;
	    }

	    /* Track all registered assignments in clock blocks */

	    if (!(state & COMMENT) && token && (clocksig != NULL)) {
		if ((aptr = strstr(token, "<=")) != NULL) {
		    if (aptr == token) {
			sigp = lasttok;
		    }
		    else {
			*aptr = '\0';
	 		sigp = token;
		    }

		    // Find sigp in the register list
		    if ((bptr = strchr(sigp, '[')) != NULL)
			*bptr = '\0';
		    for (testvec = topmod->reglist; testvec != NULL; testvec =
				testvec->next) {
			if (!strcmp(testvec->name, sigp)) {
			    if (testvec->clock != NULL && testvec->clock != clocksig)
			        fprintf(stderr, "Error:  Register signal %s is "
					"assigned in two clock domain %s and %s\n",
					sigp, testvec->clock->name, clocksig->name);
			    else
				testvec->clock = clocksig;
			    break;
			}
		    }
		    if (testvec == NULL) {
			fprintf(stderr, "Error, line %d:  LHS %s is not a known"
				" registered signal\n", line_num, sigp);
		    }
		    else {
			if (token == sigp)	// Where "<=" was attached to name
			    check_depend(topmod, sigp + 2, clocksig, (char)1);
			else
			    check_depend(topmod, sigp, clocksig, (char)1);
		    }
		}
		else {
		    // This is compute-intensive:  Check all tokens inside
		    // clock blocks and outside of a comment for a match
		    // to a known signal.  If we find one, it is added to
		    // the list of signals used by the clock domain, recorded
		    // in the structure for "clocksig".
		    check_depend(topmod, token, clocksig, (char)0);
		}
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
		   toklist = " \t\n()";
		   break;

		case IN_CLKBLOCK | IN_IFELSE:
		   toklist = " \t\n(";
		   break;

		case IN_CLKBLOCK | IN_IFELSE | IN_IFBLOCK:
		   toklist = " \t\n;";
		   break;

		default:
		   toklist = " \t\n";
		   break;
	    }

	    if (no_new_token == 0) {
	       if (token != NULL) {
	          if (lasttok != NULL) free(lasttok);
	          lasttok = strdup(token);
	       }
	       token = strtok(NULL, toklist);
	    }
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
    if (ftmp) fclose(ftmp);

    /* Next step:  Handling multiple clock domains.			*/
    /* First, determine if there is a need for splitting up the file.	*/
    /* If not, then we're done.						*/

    /* (1) Get a count of independent clock domains.	*/

    if (topmod->clocklist) {
	sigact *clocklist;
	for (clocklist = topmod->clocklist; clocklist != NULL; clocklist =
		clocklist->next)
	    multidomain++;

	if (multidomain > 1) {
	   fprintf(stderr, "WARNING: System has multiple clock domains: ");
	   for (clocklist = topmod->clocklist; clocklist != NULL; clocklist =
			clocklist->next) {
	       fprintf(stderr, "%s ", clocklist->name);
	   }
	   fprintf(stderr, "\n");
	   // fprintf(stderr, "This condition is not handled by the preprocessor!\n");
	}
    }

    /* (2) Check how many of the clock signals are assignments instead	*/
    /*	   of a system input.  If all clock signals are assignments,	*/
    /*	   then we need an extra domain where we generate all the	*/
    /*	   assignments.  Otherwise, all assignments will be put in the	*/
    /*     first clock domain found for with the clock signal is a	*/
    /*	   system input.						*/

    if (topmod->clocklist) {
	sigact *clocklist;
	char   *clockname;
	vector *clknet;

	for (clocklist = topmod->clocklist; clocklist != NULL; clocklist =
		clocklist->next) {
	    clockname = clocklist->name;

	    clknet = topmod->iolist;
	    while (clknet != NULL) {
		if (clknet->vector_size == 0) {
		    if (!strcmp(clknet->name, clockname))
			break;
		}
		else {
		    if (!strncmp(clknet->name, clockname, strlen(clknet->name)))
			break;
		}
		clknet = clknet->next;
	    }
	    if (clknet != NULL) {
	 	combdomain = clocklist;
		break;
	    }
	}

	if (combdomain == NULL) {
	    /* All clocks were not found in the I/O list */
	    if (multidomain == 1) {
	       fprintf(stderr, "WARNING: Clock net %s is an assigned value.\n",
			topmod->clocklist->name);
	       // fprintf(stderr, "This condition is not handled by the preprocessor!\n");
	    }
	}
    }

    if (multidomain == 0) return 0;				// We're done
    if (multidomain == 1 && combdomain != NULL) return 0;	// Also done

    /* From this point, the file that was just written is re-read,	*/
    /* split into individual files, each with one module having a	*/
    /* single clock signal;  with possibly one additional module	*/
    /* containing everything (e.g., wire assignments) that is outside	*/
    /* of any clock block.						*/

    sprintf(locfname, "%s_tmp.v", topmod->name);

    fsource = fopen(locfname, "r");
    if (fsource == NULL) {
	fprintf(stderr, "Error:  Cannot open \"%s\" for reading.\n", locfname);
	return 0;
    }

    k = 0;
    for (clocksig = topmod->clocklist; clocksig; clocksig = clocksig->next) {
	if (clocksig == combdomain) continue;	// Save until last

	/* Open a new file for the single domain output */

	sprintf(locfname, "_domain_%d.v", ++k);
	ftmp = fopen(locfname, "w");

	write_single_domain(fsource, ftmp, clocksig, topmod, combdomain, &wiredomain);

	fclose(ftmp);
    }

    // The clock domain that also defines all combinatorial values is output
    // last (or if there is none, then a combinatorial-only domain is output)

    sprintf(locfname, "_domain_%d.v", ++k);
    ftmp = fopen(locfname, "w");
    write_single_domain(fsource, ftmp, combdomain, topmod, combdomain, &wiredomain);
    fclose(ftmp);
    fclose(fsource);
    return 0;
}
