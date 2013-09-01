/*--------------------------------------------------------------*/
/* liberty2tech.c ---						*/
/*								*/
/*	This program converts a liberty timing file into the	*/
/*	"genlib" format used by ABC for standard cell mapping,	*/
/*	and the "gate.cfg" file used by the BDNetFanout program	*/
/*	for load balancing and delay minimization		*/
/*--------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
 
#define LIB_LINE_MAX  65535

int libCurrentLine;

#define INIT		0
#define LIBBLOCK	1
#define CELLDEF		2
#define PINDEF		3
#define TIMING		4

// Pin types
#define	UNKNOWN		-1
#define INPUT		0
#define OUTPUT		1

/*--------------------------------------------------------------*/
/* Database							*/
/*--------------------------------------------------------------*/

typedef struct _lutable *lutableptr;

typedef struct _lutable {
    char *name;
    char *var1;
    char *var2;
    char *idx1;
    char *idx2;
    lutableptr next;
} lutable;

typedef struct _pin *pinptr;

typedef struct _pin {
    char *name;
    int	type;
    double cap;
    double maxtrans;
    pinptr next;
} pin;

typedef struct _cell *cellptr;

typedef struct _cell {
    char *name;
    char *function;
    pin	 *pins;
    double area;
    double slope;
    double mintrans;
    lutable *reftable;
    char *tablevals;
    cellptr next;
} cell;

/*--------------------------------------------------------------*/
/* Grab a token from the input					*/
/* Return the token, or NULL if we have reached end-of-file.	*/
/*--------------------------------------------------------------*/

char *
advancetoken(FILE *flib, char delimiter)
{
    static char token[LIB_LINE_MAX];
    static char line[LIB_LINE_MAX];
    static char *linepos = NULL;

    char *lineptr = linepos;
    char *lptr, *tptr;
    char *result;
    int commentblock, concat, nest;

    commentblock = 0;
    concat = 0;
    nest = 0;
    while (1) {		/* Keep processing until we get a token or hit EOF */

	if (lineptr != NULL && *lineptr == '/' && *(lineptr + 1) == '*') {
	    commentblock = 1;
	}

	if (commentblock == 1) {
	    if ((lptr = strstr(lineptr, "*/")) != NULL) {
		lineptr = lptr + 2;
		commentblock = 0;
	    }
	    else lineptr = NULL;
	}

	if (lineptr == NULL || *lineptr == '\n' || *lineptr == '\0') {
	    result = fgets(line, LIB_LINE_MAX + 1, flib);
	    libCurrentLine++;
	    if (result == NULL) return NULL;

	    /* Keep pulling stuff in if the line ends with a continuation character */
 	    lptr = line;
	    while (*lptr != '\n' && *lptr != '\0') {
		if (*lptr == '\\') {
		    result = fgets(lptr, LIB_LINE_MAX + 1 - (lptr - line), flib);
		    libCurrentLine++;
		    if (result == NULL) break;
		}
		else
		    lptr++;
	    }	
	    if (result == NULL) return NULL;
	    lineptr = line;
	}

	if (commentblock == 1) continue;

	while (isspace(*lineptr)) lineptr++;
	if (concat == 0)
	    tptr = token;

	// Find the next token and return just the token.  Update linepos
	// to the position just beyond the token.  All delimiters like
	// parentheses, quotes, etc., are returned as single tokens

	// If delimiter is declared, then we stop when we reach the
	// delimiter character, and return all the text preceding it
	// as the token.  If delimiter is 0, then we look for standard
	// delimiters, and separate them out and return them as tokens
	// if found.

	while (1) {
	    if (*lineptr == '\n' || *lineptr == '\0')
		break;
	    if (*lineptr == '/' && *(lineptr + 1) == '*')
		break;
	    if (delimiter != 0 && *lineptr == delimiter) {
		if (nest > 0)
		    nest--;
		else
		    break;
	    }

	    // Watch for nested delimiters!
	    if (delimiter == '}' && *lineptr == '{') nest++;
	    if (delimiter == ')' && *lineptr == '(') nest++;

	    if (delimiter == 0)
		if (*lineptr == ' ' || *lineptr == '\t')
		    break;

	    if (delimiter == 0) {
		if (*lineptr == '(' || *lineptr == ')') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '{' || *lineptr == '}') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '\"' || *lineptr == ':' || *lineptr == ';') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
	    }

	    *tptr++ = *lineptr++;
	}
	*tptr = '\0';
	if ((delimiter != 0) && (*lineptr != delimiter))
	    concat = 1;
	else if ((delimiter != 0) && (*lineptr == delimiter))
	    break;
	else if (tptr > token)
	    break;
    }
    if (delimiter != 0) lineptr++;

    while (isspace(*lineptr)) lineptr++;
    linepos = lineptr;
    return token;
}

/*--------------------------------------------------------------*/
/* Main program							*/
/*--------------------------------------------------------------*/

int
main(int objc, char *argv[])
{
    FILE *flib;
    FILE *fgen;
    FILE *fcfg;
    char *token;
    char *libname = NULL;
    int section = INIT;
    lutable *tables = NULL;
    cell *cells = NULL;

    lutable *newtable, *reftable;
    cell *newcell, *lastcell;
    pin *newpin, *lastpin;
    char *curfunc;

    if (objc != 4) {
	fprintf(stderr, "Usage:  liberty2tech <name.lib> <name.genlib> <gate.cfg>\n");
	exit (1);
    }

    flib = fopen(argv[1], "r");
    if (flib == NULL) {
	fprintf(stderr, "Cannot open %s for reading\n", argv[1]);
	exit (1);
    }

    /* Read the file.  This is not a rigorous parser! */

    libCurrentLine = 0;
    lastcell = NULL;

    /* Read tokens off of the line */
    token = advancetoken(flib, 0);

    while (token != NULL) {

	switch (section) {
	    case INIT:
		if (!strcasecmp(token, "library")) {
		    token = advancetoken(flib, 0);
		    if (strcmp(token, "("))
			fprintf(stderr, "Library not followed by name\n");
		    else
			token = advancetoken(flib, ')');
		    fprintf(stderr, "Parsing library \"%s\"\n", token);
		    libname = strdup(token);
		    token = advancetoken(flib, 0);
		    if (strcmp(token, "{")) {
			fprintf(stderr, "Did not find opening brace "
					"on library block\n");
			exit(1);
		    }
		    section = LIBBLOCK;
		}
		else
		    fprintf(stderr, "Unknown input \"%s\", looking for "
					"\"library\"\n", token);
		break;

	    case LIBBLOCK:
		// Here we check for the main blocks, again not rigorously. . .

		if (!strcasecmp(token, "}")) {
		    section = INIT;			// End of library block
		}
		else if (!strcasecmp(token, "delay_model")) {
		    token = advancetoken(flib, 0);
		    if (strcmp(token, ":"))
			fprintf(stderr, "Input missing colon\n");
		    token = advancetoken(flib, ';');
		    if (strcasecmp(token, "table_lookup")) {
			fprintf(stderr, "Sorry, only know how to "
					"handle table lookup!\n");
			exit(1);
		    }
		}
		else if (!strcasecmp(token, "lu_table_template")) {
		    // Read in template information;
		    newtable = (lutable *)malloc(sizeof(lutable));
		    newtable->next = tables;
		    tables = newtable;

		    token = advancetoken(flib, 0);
		    if (strcmp(token, "("))
			fprintf(stderr, "Input missing open parens\n");
		    else
			token = advancetoken(flib, ')');
		    newtable->name = strdup(token);
		    while (*token != '}') {
			token = advancetoken(flib, 0);
			if (!strcasecmp(token, "variable_1")) {
			    token = advancetoken(flib, 0);
			    token = advancetoken(flib, ';');
			    newtable->var1 = strdup(token);
			}
			else if (!strcasecmp(token, "variable_2")) {
			    token = advancetoken(flib, 0);
			    token = advancetoken(flib, ';');
			    newtable->var2 = strdup(token);
			}
			else if (!strcasecmp(token, "index_1")) {
			    token = advancetoken(flib, 0);	// Open parens
			    token = advancetoken(flib, 0);	// Quote
			    if (!strcmp(token, "\""))
				token = advancetoken(flib, '\"');
			    newtable->idx1 = strdup(token);
			    token = advancetoken(flib, ';'); // EOL semicolon
			}
			else if (!strcasecmp(token, "index_2")) {
			    token = advancetoken(flib, 0);	// Open parens
			    token = advancetoken(flib, 0);	// Quote
			    if (!strcmp(token, "\""))
				token = advancetoken(flib, '\"');
			    newtable->idx2 = strdup(token);
			    token = advancetoken(flib, ';'); // EOL semicolon
			}
		    }
		}
		else if (!strcasecmp(token, "cell")) {
		    newcell = (cell *)malloc(sizeof(cell));
		    newcell->next = NULL;
		    if (lastcell != NULL)
			lastcell->next = newcell;
		    else
			cells = newcell;
		    lastcell = newcell;
		    token = advancetoken(flib, 0);	// Open parens
		    if (!strcmp(token, "("))
			token = advancetoken(flib, ')');	// Cellname
		    newcell->name = strdup(token);
		    token = advancetoken(flib, 0);	// Find start of block
		    if (strcmp(token, "{"))
			fprintf(stderr, "Error: failed to find start of block\n");
		    newcell->reftable = NULL;
		    newcell->function = NULL;
		    newcell->pins = NULL;
		    newcell->tablevals = NULL;
		    newcell->area = 1.0;
		    newcell->slope = 1.0;
		    newcell->mintrans = 0.0;
		    lastpin = NULL;
		    section = CELLDEF;
		}
		else {
		    // For unhandled tokens, read in tokens.  If it is
		    // a definition or function, read to end-of-line.  If
		    // it is a block definition, read to end-of-block.
		    while (1) {
			token = advancetoken(flib, 0);
			if (token == NULL) break;
			if (!strcmp(token, ";")) break;
			if (!strcmp(token, "\""))
			    token = advancetoken(flib, '\"');
			if (!strcmp(token, "{")) {
			    token = advancetoken(flib, '}');
			    break;
			}
		    }
		}
		break;

	    case CELLDEF:

		if (!strcmp(token, "}")) {
		    section = LIBBLOCK;			// End of cell def
		}
		else if (!strcasecmp(token, "pin")) {
		    token = advancetoken(flib, 0);	// Open parens
		    if (!strcmp(token, "("))
			token = advancetoken(flib, ')');	// Close parens
		    newpin = (pin *)malloc(sizeof(pin));
		    newpin->name = strdup(token);

		    newpin->next = NULL;
		    if (lastpin != NULL)
			lastpin->next = newpin;
		    else
			newcell->pins = newpin;
		    lastpin = newpin;

		    token = advancetoken(flib, 0);	// Find start of block
		    if (strcmp(token, "{"))
			fprintf(stderr, "Error: failed to find start of block\n");
		    newpin->type = UNKNOWN;
		    newpin->cap = 0.0;
		    newpin->maxtrans = 1.0;
		    section = PINDEF;
		}		
		else if (!strcasecmp(token, "area")) {
		    token = advancetoken(flib, 0);	// Colon
		    token = advancetoken(flib, ';');	// To end-of-statement
		    sscanf(token, "%lg", &newcell->area);
		}
		else {
		    // For unhandled tokens, read in tokens.  If it is
		    // a definition or function, read to end-of-line.  If
		    // it is a block definition, read to end-of-block.
		    while (1) {
			token = advancetoken(flib, 0);
			if (token == NULL) break;
			if (!strcmp(token, ";")) break;
			if (!strcmp(token, "\""))
			    token = advancetoken(flib, '\"');
			if (!strcmp(token, "{")) {
			    token = advancetoken(flib, '}');
			    break;
			}
		    }
		}
		break;

	    case PINDEF:

		if (!strcmp(token, "}")) {
		    section = CELLDEF;			// End of pin def
		}
		else if (!strcasecmp(token, "capacitance")) {
		    token = advancetoken(flib, 0);	// Colon
		    token = advancetoken(flib, ';');	// To end-of-statement
		    sscanf(token, "%lg", &newpin->cap);
		}
		else if (!strcasecmp(token, "function")) {
		    token = advancetoken(flib, 0);	// Colon
		    token = advancetoken(flib, 0);	// Open quote
		    if (!strcmp(token, "\""))
			token = advancetoken(flib, '\"');	// Find function string
		    if (newpin->type == OUTPUT) {
		        newcell->function = malloc(strlen(token) +
					strlen(newpin->name) + 4);
		        sprintf(newcell->function, "%s = %s", newpin->name, token);
		    }
		    token = advancetoken(flib, 0);
		    if (strcmp(token, ";"))
			fprintf(stderr, "Expected end-of-statement.\n");
		}
		else if (!strcasecmp(token, "direction")) {
		    token = advancetoken(flib, 0);	// Colon
		    token = advancetoken(flib, ';');
		    if (!strcasecmp(token, "input")) {
			newpin->type = INPUT;
		    }
		    else if (!strcasecmp(token, "output")) {
			newpin->type = OUTPUT;
		    }
		}
		else if (!strcasecmp(token, "max_transition")) {
		    token = advancetoken(flib, 0);	// Colon
		    token = advancetoken(flib, ';');	// To end-of-statement
		    sscanf(token, "%lg", &newpin->maxtrans);
		}
		else if (!strcasecmp(token, "timing")) {
		    token = advancetoken(flib, 0);	// Arguments, if any
		    if (strcmp(token, "("))
			fprintf(stderr, "Error: failed to find start of block\n");
		    else
		       token = advancetoken(flib, ')');	// Arguments, if any
		    token = advancetoken(flib, 0);	// Find start of block
		    if (strcmp(token, "{"))
			fprintf(stderr, "Error: failed to find start of block\n");
		    section = TIMING;
		}
		else {
		    // For unhandled tokens, read in tokens.  If it is
		    // a definition or function, read to end-of-line.  If
		    // it is a block definition, read to end-of-block.
		    while (1) {
			token = advancetoken(flib, 0);
			if (token == NULL) break;
			if (!strcmp(token, ";")) break;
			if (!strcmp(token, "\""))
			    token = advancetoken(flib, '\"');
			if (!strcmp(token, "{")) {
			    token = advancetoken(flib, '}');
			    break;
			}
		    }
		}
		break;

	    case TIMING:

		if (!strcmp(token, "}")) {
		    section = PINDEF;			// End of timing def
		}
		else if (!strcasecmp(token, "cell_rise")) {
		    token = advancetoken(flib, 0);	// Open parens
		    if (!strcmp(token, "("))
			token = advancetoken(flib, ')');
		    if (strcmp(token, "scalar")) {
			
		        for (reftable = tables; reftable; reftable = reftable->next)
			    if (!strcmp(reftable->name, token))
			        break;
		        if (reftable == NULL)
			    fprintf(stderr, "Failed to find a valid table \"%s\"\n",
				    token);
		        else if (newcell->reftable == NULL)
			    newcell->reftable = reftable;
		    }

		    token = advancetoken(flib, 0);
		    if (strcmp(token, "{"))
			fprintf(stderr, "Failed to find start of value block\n");
		    token = advancetoken(flib, 0);
		    if (strcasecmp(token, "values"))
			fprintf(stderr, "Failed to find keyword \"values\"\n");
		    token = advancetoken(flib, 0);
		    if (strcmp(token, "("))
			fprintf(stderr, "Failed to find start of value table\n");
		    token = advancetoken(flib, ')');

		    if (newcell->tablevals == NULL)
			newcell->tablevals = strdup(token);

		    token = advancetoken(flib, 0);
		    if (strcmp(token, ";"))
			fprintf(stderr, "Failed to find end of value table\n");
		    token = advancetoken(flib, 0);
		    if (strcmp(token, "}"))
			fprintf(stderr, "Failed to find end of timing block\n");
		}
		else {
		    // For unhandled tokens, read in tokens.  If it is
		    // a definition or function, read to end-of-line.  If
		    // it is a block definition, read to end-of-block.
		    while (1) {
			token = advancetoken(flib, 0);
			if (token == NULL) break;
			if (!strcmp(token, ";")) break;
			if (!strcmp(token, "\""))
			    token = advancetoken(flib, '\"');
			if (!strcmp(token, "{")) {
			    token = advancetoken(flib, '}');
			    break;
			}
		    }
		}
		break;
	}
	token = advancetoken(flib, 0);
    }
    fprintf(stdout, "Lib Read:  Processed %d lines.\n", libCurrentLine);

    if (flib != NULL) fclose(flib);

    /* Temporary:  Print information gathered */

/*-------------------------------------------------------------------
    for (newtable = tables; newtable; newtable = newtable->next) {
	fprintf(stdout, "Table: %s\n", newtable->name);
    }

    for (newcell = cells; newcell; newcell = newcell->next) {
	fprintf(stdout, "Cell: %s\n", newcell->name);
	fprintf(stdout, "   Function: %s\n", newcell->function);
	for (newpin = newcell->pins; newpin; newpin = newpin->next) {
	    if (newpin->type == INPUT)
		fprintf(stdout, "   Pin: %s  cap=%g\n", newpin->name, newpin->cap);
	}
	fprintf(stdout, "\n");
    }
-------------------------------------------------------------------*/

    /* Now generate the output files */

    /* ----------- */

    fcfg = fopen(argv[3], "w");
    if (fcfg == NULL) {
	fprintf(stderr, "Cannot open %s for reading\n", argv[3]);
	exit (1);
    }

    fprintf(fcfg, "# comments begin with #\n\n");
    fprintf(fcfg, "# Format is propagation delay with internal and pin capacitances.\n");
    fprintf(fcfg, "# Only format D0 is supported for now.\n");
    fprintf(fcfg, "FORMAT D0\n\n");
    fprintf(fcfg, "#----------------------------------------------------------------\n");
    fprintf(fcfg, "# Gate drive strength information for library %s\n",
			(libname == NULL) ? "" : libname);
    fprintf(fcfg, "#----------------------------------------------------------------\n");
    fprintf(fcfg, "# \"delay\" is propagation delay in ps/fF of load capacitance\n");
    fprintf(fcfg, "# \"Cint\", \"Cin1\", ... are all in fF.\n");
    fprintf(fcfg, "#----------------------------------------------------------------\n");
    fprintf(fcfg, "# This file generated by liberty2tech\n\n");
    fprintf(fcfg, "# gatename delay num_inputs Cint Cpin1 Cpin2...\n\n");

    for (newcell = cells; newcell; newcell = newcell->next) {
	int i, cellinputs, tablevals;
	char *tptr, *eptr;
	double mintrans, mincap, maxcap, mintrise, maxtrise;
	double loaddelay, intcap;

	// If this cell does not have a timing table or timing values, ignore it.
	if (newcell->reftable == NULL || newcell->tablevals == NULL) continue;

	// Count the number of input pins on the cell

	cellinputs = 0;
	for (newpin = newcell->pins; newpin; newpin = newpin->next) {
	    if (newpin->type == INPUT)
		cellinputs++;
	}

	// Find the smallest value in the input net transition table.
	// Assume it is the first value, therefore we want to parse the
	// first row of the cell table.  If that's not true, then we need
	// to add more sophisticated parsing code here!

	sscanf(newcell->reftable->idx1, "%lg", &mintrans);

	// Find the smallest and largest values in the output net capacitance table
	sscanf(newcell->reftable->idx2, "%lg", &mincap);

	tptr = newcell->reftable->idx2;
	tablevals = 1;
	while ((tptr = strchr(tptr, ',')) != NULL) {
	    tablevals++;
	    tptr++;
	    eptr = tptr;
	}
	sscanf(eptr, "%lg", &maxcap);

	// Pick up values for rise time under maximum and minimum loads in
	// the template.

	sscanf(newcell->tablevals + 1, "%lg", &mintrise);
	tptr = newcell->tablevals + 1;
	for (i = 0; i < tablevals - 1; i++) {
	   tptr = strchr(tptr, ',');
	   tptr++;
	}
	sscanf(tptr, "%lg", &maxtrise);
	
	// Calculate delay per load
	loaddelay = (maxtrise - mintrise) / (maxcap - mincap);
	newcell->slope = loaddelay;
	newcell->mintrans = mintrise;

	// Calculate internal capacitance
	intcap = 1000 * ((mintrise / loaddelay) - mincap);

	// Print out all values so far.
	fprintf(fcfg, "%s  %g %d %g  ", newcell->name, loaddelay,
		cellinputs, intcap);

	// Finally, print out the capacitances for each cell, in fF

	for (newpin = newcell->pins; newpin; newpin = newpin->next) {
	    if (newpin->type == INPUT)
		fprintf(fcfg, " %g", 1000 * newpin->cap);
	}
	fprintf(fcfg, "\n");
    }

    fprintf(fcfg, "# end of gate.cfg\n");
    fclose(fcfg);

    /* ----------- */

    fgen = fopen(argv[2], "w");
    if (fgen == NULL) {
	fprintf(stderr, "Cannot open %s for reading\n", argv[2]);
	exit (1);
    }

    fprintf(fgen, "# Genlib file created by liberty2tech\n");
    if (libname != NULL) 
	fprintf(fgen, "# from library %s\n", libname);
    fprintf(fgen, "\n");
    
    curfunc = NULL;
    for (newcell = cells; newcell; newcell = newcell->next) {
	if (newcell->function == NULL) continue;
	if ((curfunc == NULL) || (strcmp(newcell->function, curfunc))) {
	    curfunc = newcell->function;
	    fprintf(fgen, "GATE %s %g %s;\n", newcell->name,
		newcell->area, newcell->function);

	    for (newpin = newcell->pins; newpin; newpin = newpin->next) {
		if (newpin->type == INPUT)
		    fprintf(fgen, "   PIN %s %s %g %g %g %g %g %g\n",
				newpin->name, "UNKNOWN", newpin->cap,
				newpin->maxtrans / newcell->slope,
				newcell->mintrans, newcell->slope,
				newcell->mintrans, newcell->slope);
	    }
	    fprintf(fgen, "\n");
	}
    }
    fclose(fgen);

    /* ----------- */

    return 0;
}
