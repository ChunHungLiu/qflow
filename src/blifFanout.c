/*
 *---------------------------------------------------------------------------
 * blifFanout blif_input [blif_output]
 *
 * blifFanout[.c] parses a .blif netlist (typically, but not necessarily,
 * from synthesized verilog).  The fanout is analyzed, and fanout of each gate
 * is counted.  A value to parameterize the driving cell will be output.
 * Eventually, a critical path can be identified, and the gates sized to
 * improve it.
 *
 * Original:  fanout.c by Steve Beccue
 * New: blifFanout.c by Tim Edwards.
 *     changes: 1) Input and output format changed from RTL verilog to BDNET
 *		 for compatibility with the existing digital design flow.
 *	        2) Gate format changed to facilitate entering IBM data
 *		3) Code changed/optimized in too many ways to list here.
 *
 * Update 4/8/2013:
 *	Removing dependence upon the naming convention of the original
 *	technology used with this tool.  Instead, the format of the naming
 *	convention is passed to the tool using "-s <separator>", and parsing
 *	the remaining name for unique identifiers.
 *
 * Update 10/8/2013:
 *	Changed input file format from BDNET to BLIF
 *---------------------------------------------------------------------------
 *
 * Revision 1.3  2008/09/09 21:24:30  steve_beccue
 * changed gate strengths in code.  Inserted inverters.  2nd pass on
 * insert inverters does not work.
 *
 * Revision 1.2  2008/09/04 14:25:59  steve_beccue
 * added helpmessage and id
 *
 *---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>	/* for getopt() */
#include <string.h>
#include <ctype.h>	/* for isdigit() */
#include <math.h>

#define  FALSE	     0
#define  TRUE        1
#define  MAXLINE     2000
#define  MAXGATENAME 64
#define  MAXINPUTS   20
#define  MAXTYPES    64		// Different drive strength categories

char Inputfname[MAXLINE];
char Outputfname[MAXLINE];
char Gatename[MAXGATENAME];
char Nodename[MAXGATENAME];
char *Buffername = NULL;
char *buf_in_pin;
char *buf_out_pin;
char *Gatepath = NULL;
char *Ignorepath = NULL;
char *Separator = NULL;
char SuffixIsNumeric;
int  Input_node_num = 0;
int  GateCount = 0;
int  GatePrintFlag = 0;
int  NodePrintFlag = 0;
int  VerboseFlag = 0;
char DriveTypes[MAXTYPES][10];
static char default_sep = '\0';
static char default_gatepath[] = "gate.cfg";

int  Ngates[2][MAXTYPES];
int  Topfanout = 0;
double Topload = 0.0;
double Topratio = 0.0;
double Strength = 0.0;
int    Changed_count = 0;	// number of gates changed
int stren_err_counter = 0;

double MaxLatency = 100.0;	// Maximum variable latency (ps) for which we
				// are solving.  Represents the largest delay
				// allowed for any gate caused by total load
				// capacitance.  This is empirically derived.
double MaxOutputCap = 18.0;	// Maximum capacitance for an output node (fF).
				// Outputs should be able to drive this much
				// capacitance within MaxLatency time (ps).
double WireCap = 10.0;		// Base capacitance for an output node, estimate
				// of average wire capacitance (fF).

struct Gatelist {
   struct Gatelist *next;
   char   *gatename;
   int    num_inputs;
   double Cpin[MAXINPUTS];
   double Cint;
   double delay;
   double strength;
} Gatelist_;

struct Gatelist *Gatel;

struct Nodelist {
   struct Nodelist *next;
   char	  ignore;
   char   *nodename;
   char   *outputgatename;
   double outputgatestrength;
   char   is_outputpin;
   int    num_inputs;
   double total_load;
   double ratio;                // drive strength to total_load ratio
} Nodelist_;

struct Nodelist *Nodel;

enum states_ {NONE, OUTPUTS, GATENAME, PINNAME, INPUTNODE, OUTPUTNODE, ENDMODEL};
enum nodetype_ {INPUT, OUTPUT, OUTPUTPIN, UNKNOWN};

void read_gate_file(char *gate_file_name);
void read_ignore_file(char *ignore_file_name);
struct Gatelist* GatelistAlloc();
struct Nodelist* NodelistAlloc();
void showgatelist(void);
void helpmessage(void);
void registernode(char *nodename, int type);
void shownodes(void);
void write_output(FILE *infptr, FILE *outfptr);
char *best_size(char *gatename, double amount, char *overload);
char *find_size(char *gatename);
char *max_size(char *gatename);
void count_gatetype(char *name);

/*
 *---------------------------------------------------------------------------
 * find_suffix ---
 *
 *	Given a gate name, return the part of the name corresponding to the
 *	suffix.  That is, find the last occurrance of the string Separator
 *	(global variable), and return a pointer to the following character.
 *---------------------------------------------------------------------------
 */

char *find_suffix(char *gatename)
{
   char *tsuf, *gptr;
   char *suffix = NULL;

   if (Separator == NULL) {
      suffix = gatename + strlen(gatename) - 1;
      while (isdigit(*suffix)) suffix--;
      suffix++;
   }
   else {
      gptr = gatename;
      while ((tsuf = strstr(gptr, Separator)) != NULL) {
         suffix = tsuf;
         gptr = tsuf + 1;
      }
      if (suffix != NULL)
	 suffix += strlen(Separator);
   }
   return suffix;
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

int main (int argc, char *argv[])
{
   int i, j, k;
   int state;
   int inputcount;
   int gateinputs;
   char *test;
   char *s, *t;
   FILE *infptr, *outfptr;
   char line[MAXLINE];
   struct Gatelist *gl;
   struct Nodelist *nl;

   SuffixIsNumeric = TRUE;	// By default, assume numeric suffixes
   Separator = NULL;		// By default, assume no separator

   for (i = 0; i < MAXTYPES; i++) {
      Ngates[0][i] = 0;
      Ngates[1][i] = 0;
      DriveTypes[i][0] = '\0';
   }
   
   Nodel = NodelistAlloc();
   nl = Nodel;

   while ((i = getopt(argc, argv, "gnhvl:c:b:i:o:p:s:f:")) != EOF) {
      switch (i) {
	 case 'b':
	    Buffername = strdup(optarg);
	    break;
	 case 'i':
	    buf_in_pin = strdup(optarg);
	    break;
	 case 'o':
	    buf_out_pin = strdup(optarg);
	    break;
         case 'p':
	    Gatepath = strdup(optarg);
	    break;
	 case 'f':
	    Ignorepath = strdup(optarg);
	    break;
         case 'l':
	    MaxLatency = atof(optarg);
	    break;
	 case 'c':
	    MaxOutputCap = atof(optarg);
	    break;
	 case 's':
	    Separator = strdup(optarg);
	    break;
         case 'g':
	    GatePrintFlag = 1;
	    break;
         case 'n':
	    NodePrintFlag = 1;
 	    break;
         case 'v':
	    VerboseFlag = 1;
	    break;
         case 'h':
	    helpmessage();
	    break;
         default:
	    break;
      }
   }

   Inputfname[0] = Outputfname[0] = 0;
   infptr = stdin;
   outfptr = stdout;
   i = optind;

   if (i < argc) {
      strcpy(Inputfname, argv[i]);
      if (!(infptr = fopen(Inputfname, "r"))) {
	  fprintf(stderr, "blifFanout: Couldn't open %s for reading.\n", Inputfname);
	  exit(-1);
      }
   }
   i++;
   if (i < argc) {
      strcpy(Outputfname, argv[i]);
      if (!(outfptr = fopen(Outputfname, "w"))) {
	 fprintf(stderr, "blifFanout: Couldn't open %s for writing.\n", Outputfname);
	 exit(-1);
      }
   }
   i++;

   // Make sure we have a valid gate file path
   if (Gatepath == NULL) Gatepath = (char *)default_gatepath;

   read_gate_file(Gatepath);

   // Make sure we have a non-NULL separator.
   if (Separator == NULL) Separator = &default_sep;

   // Determine if suffix is numeric or alphabetic
   if (GateCount > 0) {
      char *suffix, *tsuf, *gptr;
      gl = Gatel;
      gptr = gl->gatename;
      if ((suffix = find_suffix(gptr)) != NULL) {
	 if (suffix && !isdigit(*suffix))
	    SuffixIsNumeric = FALSE;
      }
   }

   if (GateCount == 0) {
      fprintf(stderr, "blifFanout:  No gates found in %s file!\n", Gatepath);
      exit(-1);
   }

   if (GatePrintFlag) showgatelist();    // then exit

   if (Buffername == NULL || buf_in_pin == NULL || buf_out_pin == NULL) {
      fprintf(stderr, "blifFanout:  Need name of buffer cell, and input/output pins.\n");
      exit(-1);
   }

   state = NONE;
   while ((s = fgets(line, MAXLINE, infptr)) != NULL) {
      t = strtok(s, " \t=\n");
      while (t) {
	 switch (state) {
	    case GATENAME:
	       for (gl = Gatel; gl->next; gl = gl->next) {
		  if (!(strcmp(gl->gatename, t))) {
		     if (VerboseFlag) printf("\n\n%s", t);
		     gateinputs = gl->num_inputs;
		     Input_node_num = 0;
		     strcpy(Gatename, t);
		     state = PINNAME;
		  }
	       }
	       break;

	    case OUTPUTS:
	       if (!strcmp(t, ".gate"))
		  state = GATENAME;
	       else if (t) {
	          if (VerboseFlag) printf("\nOutput pin %s", t);
	          strcpy(Nodename, t);
	          registernode(Nodename, OUTPUTPIN);
	       }
	       break;

	    case PINNAME:
	       if (!strcmp(t, ".gate")) state = GATENAME;  // new gate
	       else if (!strcmp(t, ".end")) state = ENDMODEL;  // last gate
	       else if (Input_node_num == gateinputs) state = OUTPUTNODE;
	       else state = INPUTNODE;
	       break;

	    case INPUTNODE:
	       if (VerboseFlag) printf("\nInput node %s", t);
	       strcpy(Nodename, t);
	       registernode(Nodename, INPUT);
	       Input_node_num++;
	       state = PINNAME;
	       break;

	    case OUTPUTNODE:
	       if (VerboseFlag) printf("\nOutput node %s", t);
	       strcpy(Nodename, t);
	       registernode(Nodename, OUTPUT);
	       state = PINNAME;
	       break;

	    default:
	       if (!strcmp(t, ".gate")) state = GATENAME;
	       else if (!strcmp(t, ".outputs")) state = OUTPUTS;
	       break;
	 }
	 t = strtok(NULL, " \t=\\\n");
      }
   }

   /* get list of nets to ignore, if there is one, and mark nets to ignore */
   if (Ignorepath != NULL) read_ignore_file(Ignorepath);

   /* input nodes are parsed, and Nodel is loaded */
   if (NodePrintFlag) shownodes();

   /* show top fanout gate */
   for (nl = Nodel; nl->next; nl = nl->next) {
      if (nl->outputgatestrength != 0.0) {
	 nl->ratio = nl->total_load / nl->outputgatestrength;
      }
      if (nl->ignore == FALSE) {
         if (nl->num_inputs >= Topfanout && nl->outputgatestrength != 0.0) {
	    Topfanout = nl->num_inputs;
	    strcpy(Nodename, nl->nodename);
	    strcpy(Gatename, nl->outputgatename);
	    Strength = nl->outputgatestrength;
         }
         if (nl->ratio >= Topratio && nl->outputgatestrength != 0.0) {
	    Topratio = nl->ratio;
         }
         if (nl->total_load >= Topload && nl->outputgatestrength != 0.0) {
	    Topload = nl->total_load;
	 }
      }
   }

   if (VerboseFlag) printf("\n");
   fflush(stdout);

   fprintf(stderr,
	"Top fanout is %d (load %g) from node %s,\n"
	"driven by %s with strength %g\n",
	 Topfanout, Topload, Nodename, Gatename, Strength);

   fprintf(stderr, "Top fanoutratio is %g\n", Topratio);
  
   write_output(infptr, outfptr);

   fprintf(stderr,"%d gates changed.\n", Changed_count);

   fprintf(stderr, "\nIn:\n");
   j = 0;
   for (i = 0; i < MAXTYPES; i++) {
      if (DriveTypes[i][0] == '\0') break;	/* No more drive types */
      if (Ngates[0][i] > 0) {
	 fprintf(stderr, "%d\t%s%s gate%c\t", Ngates[0][i], Separator,
		DriveTypes[i], Ngates[0][i] > 1 ? 's' : ' ');
	 j++;
	 if (j > 3) {
	    fprintf(stderr, "\n");
	    j = 0;
	 }
      }
   }
   if (j != 0) fprintf(stderr, "\n");

   fprintf(stderr, "\nOut:\n");
   j = 0;
   for (i = 0; i < MAXTYPES; i++) {
      if (DriveTypes[i][0] == '\0') break;	/* No more drive types */
      if (Ngates[1][i] > 0) {
	 fprintf(stderr, "%d\t%s%s gate%c\t", Ngates[1][i], Separator,
		DriveTypes[i], Ngates[1][i] > 1 ? 's' : ' ');
	 j++;
	 if (j > 3) {
	    fprintf(stderr, "\n");
	    j = 0;
	 }
      }
   }
   if (j != 0) fprintf(stderr, "\n");

   if (infptr != stdin) fclose(infptr);
   if (outfptr != stdout) fclose(outfptr);

   return Changed_count;	// exit with number of gates changed
				// so we can iterate until this is zero.
}

/*
 *---------------------------------------------------------------------------
 * Read a file of nets for which we should ignore fanout.  Typically this
 * would include the power and ground nets, but may include other static
 * nets with non-critical timing.
 *---------------------------------------------------------------------------
 */

void read_ignore_file(char *ignore_file_name)
{
   struct Nodelist *nl;
   FILE *ignorefptr;
   char line[MAXLINE];
   char *s, *sp;

   if (!(ignorefptr = fopen(ignore_file_name, "r"))) {
      fprintf(stderr, "blifFanout:  Couldn't open %s as ignore file.\n",
		ignore_file_name);
      fflush(stderr);
      return;
      // This is only a warning.  It will not stop executaion of blifFanout
   }

   while ((s = fgets(line, MAXLINE, ignorefptr)) != NULL) {
      // One net name per line
      while (isspace(*s)) s++;
      sp = s;
      while (*sp != '\0' && *sp != '\n' && !isspace(*sp)) sp++;
      *sp = '\0';

      for (nl = Nodel; nl->next; nl = nl->next) {
         if (!(strcmp(nl->nodename, s))) {
	    nl->ignore = (char)1;
	    break;
         }
      }
   }
   fclose(ignorefptr);
}

/*
 *---------------------------------------------------------------------------
 *
 * Read a file "gate.cfg" that has a list of gates.
 * # or \n are comment lines.
 * 1st word is gate name
 * 2cd double is gate drive strength
 * 3rd int is number on inputs (one output is assumed)
 * 4th double is PMOS cap load (unitless) of 1st input
 * 5th double is NMOS cap load of 1st input
 * 6th double is PMOS cap load of 2cd input 
 *    .
 *    .
 *    .
 * 2N    double is PMOS cap load of (N-2)/2 input
 * 2N+1  double is NMOS cap load of (N-2)/2 input
 *
 *---------------------------------------------------------------------------
 */

void read_gate_file(char *gate_file_name)
{
   FILE *gatefptr;
   int i, j, k, format = -1;
   char line[MAXLINE];
   char *s, *t, *ss;
   struct Gatelist *gl;

   Gatel = GatelistAlloc();
   gl = Gatel;

   if (!(gatefptr = fopen(gate_file_name, "r"))) {
      fprintf(stderr, "blifFanout:  Couldn't open %s as gate file. exiting.\n",
		 gate_file_name);
      fflush(stderr);
      exit(-2);
   }

   while ((s = fgets(line, MAXLINE, gatefptr)) != NULL) {
      j = 0;
      t = strtok(s, " \t");
      if (t && (t[0] != '#') && (t[0] != '\n')) {
	 if (!strcmp(t, "FORMAT")) {
            t = strtok(NULL, " \t\n");
	    if (t && !strcmp(t, "D0"))
	       format = 0;
	    continue;
	 }

	 if (format < 0) {
	    fprintf(stderr, "Unknown format %s!  No gate configuration read!\n", t);
	    return;
	 }

	 strcpy(gl->gatename, t);
	 do { 
	    t = strtok( NULL, " \t");
	    if (t) {
	       switch (j) {
		  case 0:
		     gl->delay = (double)atof(t);
		     break;
		  case 1:
		     gl->num_inputs = (int)atoi(t);
		     break;
		  case 2:
		     gl->Cint = (double)atof(t);
		     break;
		  default:
		     gl->Cpin[(j - 3)] = (double)atof(t);
		     break;
	       }
	       j++;
	    }
	 } while (t);

	 /* The "MaxLatency" is empirically derived.  Since gl->delay	*/
	 /* is in ps/fF, and strength is compared directly to total	*/
	 /* load capacitance, MaxLatency is effectively in units of ps	*/
	 /* and represents the maximum latency due to load capacitance	*/
	 /* for any gate in the circuit after making gate strength	*/
	 /* substitutions (this does not include internal, constant	*/
	 /* delays in each gate).					*/

	 gl->strength = MaxLatency / gl->delay;

	 gl->next = GatelistAlloc();
	 gl = gl->next;
	 GateCount++;
      }
   }

   fclose(gatefptr);
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

struct Gatelist* GatelistAlloc()
{
   struct Gatelist *gl;

   gl = (struct Gatelist*)malloc(sizeof(struct Gatelist));
   gl->next = NULL;
   gl->gatename = calloc(MAXGATENAME, 1);
   return gl;
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

struct Nodelist* NodelistAlloc()
{
   struct Nodelist *nl;

   nl = (struct Nodelist *)malloc(sizeof(struct Nodelist));
   nl->next = NULL;
   nl->nodename = calloc(MAXGATENAME, 1);
   nl->ignore = FALSE;
   nl->outputgatename = calloc(MAXGATENAME, 1);
   nl->outputgatestrength = 0.0;
   nl->is_outputpin = FALSE;
   nl->total_load = 0.0;
   nl->num_inputs = 0;
   return nl;
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

void showgatelist(void)
{
   struct Gatelist *gl;
   int i;

   for (gl = Gatel; gl->next; gl = gl->next) {
      printf("\n\ngate: %s with %d inputs and %g drive strength\n",
		gl->gatename, gl->num_inputs, gl->strength);
      printf("%g ", gl->Cint);
      for (i = 0; i < gl->num_inputs && i < MAXINPUTS; i++) {
	 printf("%g   ", gl->Cpin[i]);
      }
   }
   exit(0);
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

void registernode(char *nodename, int type)
{
   struct Nodelist *nl;
   struct Gatelist *gl;

   for (nl = Nodel; nl->next; nl = nl->next) {
      if (!(strcmp(nl->nodename, nodename))) {
	 break;
      }
   }

   if (!nl->next)
      strcpy(nl->nodename, Nodename);

   if (type == OUTPUT) {
      strcpy(nl->outputgatename, Gatename);
      for (gl = Gatel; gl->next; gl = gl->next) {
	 if (!(strcmp(Gatename, gl->gatename))) {
	    nl->outputgatestrength = gl->strength;
	    nl->total_load += gl->Cint;
	    count_gatetype(Gatename);
	    break;
	 }
      }
   }
   else if (type == INPUT) {
      for (gl = Gatel; gl->next; gl = gl->next) {
	 if (!(strcmp(Gatename, gl->gatename))) {
	    nl->total_load += gl->Cpin[Input_node_num];
	    nl->num_inputs++;
	    break;
	 }
      }
   }
   else if (type == OUTPUTPIN) {
      nl->is_outputpin = TRUE;
   }

   if ((nl->is_outputpin == FALSE) && !gl->next) {
      fprintf(stderr, "\nError: gate %s not found\n", Gatename);
      fflush(stderr);
   }

   if (!nl->next) {
      nl->next = NodelistAlloc();
      nl = nl->next;
   }
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

void count_gatetype(char *name)
{
   char *s, *nptr, *tsuf;
   int g;

   if ((s = find_suffix(name)) == NULL)
      return;

   for (g = 0; g < MAXTYPES; g++) {
      if (DriveTypes[g][0] == '\0') {
	 strncpy(DriveTypes[g], s, 9);		// New drive type found
	 break;
      }
      else if (!strcmp(DriveTypes[g], s))
	 break;
   }

   if (g < MAXTYPES) {
      Ngates[0][g]++;
      Ngates[1][g]++;
   }
   else {
      fprintf(stderr, "Don't know gatetype %s", name);
   }
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

void shownodes(void)
{
   struct Nodelist *nl;
   int i;

   for (nl = Nodel; nl->next; nl = nl->next) {
      printf("\n\nnode: %s with %d fanout and %g cap",
		nl->nodename, nl->num_inputs, nl->total_load);
      printf("\ndriven by %s, with %g strength.\n",
		nl->outputgatename, nl->outputgatestrength);
   }
   exit(0);
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

void write_output(FILE *infptr, FILE *outfptr)
{
   char *s, *t;
   char line[MAXLINE];
   char inputline[MAXLINE];
   char gateline[MAXLINE * 10];
   char bufferline[MAXLINE];
   char *cbest, *cend, *stren, *orig;
   int  state, i;
   int gateinputs;
   int needscorrecting;
   struct Gatelist *gl;
   struct Nodelist *nl;
   double inv_size;

   Changed_count = 0;
   state = NONE;
   needscorrecting = 0;

   rewind(infptr);

   gateline[0] = '\0';
   while ((s = fgets(line, MAXLINE, infptr)) != NULL) {
      strcpy(inputline, s);		// save this for later
      t = strtok(s, " \t=\n");
      while (t) { 
	 switch (state) {
	    case GATENAME:
	       for (gl = Gatel; gl->next; gl = gl->next) {
		  if (!(strcmp(gl->gatename, t))) {
		     gateinputs = gl->num_inputs;
		     Input_node_num = 0;
		     needscorrecting = 0;
		     strcpy(Gatename, t);
		     state = PINNAME;
		  }
	       }
	       break;
	  
	    case PINNAME:
	       if (!strcmp(t, ".gate")) state = GATENAME;  // new gate
	       else if (!strcmp(t, ".end")) state = ENDMODEL;  // last gate
	       else if (Input_node_num == gateinputs) state = OUTPUTNODE;
	       else state = INPUTNODE; // We ignore the pin names
	       break;

	    case INPUTNODE:
	       if (VerboseFlag) printf("\nInput node %s", t);
	       strcpy(Nodename, t);
	       Input_node_num++;
	       state = PINNAME;
	       break;

	    case OUTPUTNODE:
	       if (VerboseFlag) printf("\nOutput node %s", t);
	       strcpy(Nodename, t);

	       for (nl = Nodel; nl->next ; nl = nl->next) {
		  if (!strcmp(Nodename, nl->nodename)) {
		     if ((nl->ignore == FALSE) && (nl->ratio > 1.0)) {
			if (VerboseFlag)
			   printf("\nGate should be %g times stronger", nl->ratio);
			needscorrecting = TRUE;
			orig = find_size(Gatename);
			stren = best_size(Gatename, nl->total_load + WireCap, NULL);
			if (VerboseFlag)
			   printf("\nGate changed from %s to %s\n", Gatename, stren);
			inv_size = nl->total_load;
		     }

		     // Is this node an output pin?  Check required output drive.
		     if ((nl->ignore == FALSE) && (nl->is_outputpin == TRUE)) {
		        orig = find_size(Gatename);
			stren = best_size(Gatename, nl->total_load + MaxOutputCap
					+ WireCap, NULL);
			if (stren && strcmp(stren, Gatename)) {
			   needscorrecting = TRUE;
			   if (VerboseFlag)
			      printf("\nOutput Gate changed from %s to %s\n",
					Gatename, stren);
			}
		     }
		  }
	       }
	       state = PINNAME;
	       break;

	    default:
	       if (!strcmp(t, ".gate"))
		  state = GATENAME;
	       break;
	 }
         t = strtok(NULL, " \t=\\\n");

         if (state == GATENAME || state == ENDMODEL) {

	    /* dump output for the last gate */

            bufferline[0] = 0;
            if (needscorrecting) {
	       if (stren == NULL) {      // return val to insert inverters

		  if (VerboseFlag)
	             printf("\nInsert buffers %s - %g\n", s, inv_size);
	          s = strstr(gateline, Nodename);	// get output node
	          s = strtok(s, " \\\t");		// strip it clean
		  if (*s == '[') {
		     char *p = strchr(s, ']');
		     if (p != NULL)
	                strcpy(p, "_buf]\";\n");            // rename it
		     else
	                strcat(s, "_buf\";\n");
		  }
		  else
	             strcat(s, "_buf\";\n");            // rename it

		  cbest = best_size(Buffername, inv_size + WireCap, NULL);

		  /* If cend is NULL, then we will have to break up this network */
		  /* Buffer trees will be inserted by downstream tools, after	*/
		  /* analyzing the placement of the network.  This error needs	*/
		  /* to be passed down to those tools. . .			*/

		  if (cbest == NULL) {
		     fprintf(stderr, "Fatal error:  No gates found for %s\n", Buffername);
		     fprintf(stderr, "May need to add information to gate.cfg file\n");
		  }
		  cend = find_size(cbest);

		  // This really ought to be done with hash tables. . .
		  for (i = 0; i < MAXTYPES; i++) {
		     if (DriveTypes[i][0] == '\0') break;
		     else if (!strcmp(DriveTypes[i], cend)) {
			Ngates[1][i]++;
			break;
		     }
		  }

		  /* Recompute size of the gate driving the buffer */
		  sprintf(bufferline, "%s", cbest);
	          for (nl = Nodel; nl->next ; nl = nl->next) {
		     if (!strcmp(Nodename, nl->nodename)) {
	                for (gl = Gatel; gl->next; gl = gl->next) {
		           if (!(strcmp(gl->gatename, bufferline))) {
		              nl->total_load = gl->Cpin[0];
			      break;
			   }
			}
	                for (gl = Gatel; gl->next; gl = gl->next) {
		           if (!(strcmp(gl->gatename, Gatename))) {
		              nl->total_load += gl->Cint;
			      break;
			   }
			}
			break;
		     }
		  }
		  orig = find_size(Gatename);
		  stren = best_size(Gatename, nl->total_load + WireCap, NULL);

		  sprintf(bufferline, 
			".gate %s %s=%s %s=%s\n",
			cbest, buf_in_pin, s, buf_out_pin, Nodename);
	       }
	       if (stren != NULL && strcmp(Gatename, stren) != 0) {
		  s = strstr(gateline, Gatename);
	          if (s) {
		     int glen = strlen(Gatename);
		     int slen = strlen(stren);
		     char *repl = find_size(stren);

		     if (glen != slen)
			memmove(s + slen, s + glen, strlen(s + glen) + 1);

		     strncpy(s, stren, slen);
	             Changed_count++;

		     /* Adjust the gate count for "in" and "out" types */
		     for (i = 0; i < MAXTYPES; i++) {
		        if (DriveTypes[i][0] == '\0') break;
		        else if (!strcmp(DriveTypes[i], orig))
			   Ngates[1][i]--;
		        else if (!strcmp(DriveTypes[i], repl))
			   Ngates[1][i]++;
		     }
		  }
	       }
            }
            else {
	       stren = NULL;
            }

            if (strlen(gateline) > 0) gateline[strlen(gateline) - 1] = 0;
            fprintf(outfptr, "%s\n", gateline);
            fprintf(outfptr, "%s", bufferline); 

            bufferline[0] = '\0';
	    gateline[0] = '\0';		/* Starting a new gate */

	    if (state == ENDMODEL)
	       fprintf(outfptr, "%s", inputline); 
         }
	 else if (state == NONE) {
	    /* Print line and reset so we don't overflow gateline */
	    fprintf(outfptr, "%s", gateline);
	    gateline[0] = '\0';		/* Start a new line */
	 }
      }
      strcat(gateline, inputline);	/* Append input line to gate */
   }
   if (VerboseFlag) printf("\n");
   fflush(stdout);
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

char *find_size(char *gatename)
{
   char *s;
   char stren;
   int ind;
   double amax = 10000.0;	// Assuming no gate is this big!

   return (find_suffix(gatename));
}

/*
 *---------------------------------------------------------------------------
 * Return the character suffix of the gate that has the highest drive
 * strength of the type of gate passed in "gatename"
 *---------------------------------------------------------------------------
 */

char *max_size(char *gatename)
{
   char *s, *stren = NULL;
   int ind;
   double amax = 0.0;
   struct Gatelist *gl;

   if ((s = find_suffix(gatename)) == NULL) return NULL;
   ind = (int)(s - gatename + 1);	// Compare out to and including the suffix

   for (gl = Gatel; gl->next; gl = gl->next) {
      if (!(strncmp(gl->gatename, gatename, ind))) {
	 if (amax <= gl->strength) {
	    s = find_suffix(gl->gatename);
	    if (s) {
	       stren = s;
	       amax = gl->strength;
	    }
	 }
      }
   }
   return stren;
}


/*
 *---------------------------------------------------------------------------
 * Return the gate name of the gate with the drive strength that is the
 * minimum necessary to drive the load "amount".
 *
 * If the load exceeds the available gate sizes, then return the maximum
 * strength gate available, and set "overload" to TRUE.  Otherwise, FALSE
 * is returned in "overload".
 *---------------------------------------------------------------------------
 */

char *best_size(char *gatename, double amount, char *overload)
{
   char *s, *stren;
   int ind;
   double amax = 1.0E10;		// Assuming no gate is this big!
   double gmax = 0.0;
   struct Gatelist *gl, *glsave = NULL;

   if (overload) *overload = FALSE;
   if ((s = find_suffix(gatename)) == NULL) return NULL;
   ind = (int)(s - gatename);	// Compare out to and including the suffix

   for (gl = Gatel; gl->next; gl = gl->next) {
      if (!(strncmp(gl->gatename, gatename, ind))) {
	 if (gl->strength >= gmax) {
	    gmax = gl->strength;
	    glsave = gl;
	 }
	 if (amount <= gl->strength) {
	    if (gl->strength < amax) {
		s = find_suffix(gl->gatename);
		if (s) {
	           stren = gl->gatename;
		   amax = gl->strength;
		}
	    }
	 }
      }
   }

   if (amax == 1.0E10) {
      stren_err_counter++;
      if (overload) *overload = TRUE;
      if (glsave != NULL)
	 stren = glsave->gatename;
      else
         stren = NULL;

      if (gmax > 0.0)
         fprintf(stderr, "Warning %d: load of %g is %g times greater than strongest gate %s\n",
		stren_err_counter, amount, (double)(amount / gmax), glsave->gatename);
   }
   return stren;
}

/*
 *---------------------------------------------------------------------------
 *---------------------------------------------------------------------------
 */

void helpmessage(void)
{
   printf("\nblifFanout:\n\n");
   printf("blifFanout looks at a synthesized BLIF netlist.\n");
   printf("Node fanout is measured, and gate size is adjusted.\n");
   printf("File \"gate.cfg\" is used to describe the RTL gates.\n\n");

   printf("\tUsage: blifFanout [-switches] blif_in [blif_out].\n\n");

   printf("blifFanout returns the number of gate substitutions made.\n");
   printf("Typically, it will be iterated until convergence (return value 0).\n\n");
    
   printf("valid switches are:\n");
   printf("\t-g\t\tDebug mode: parse and print the gate.cfg table\n");
   printf("\t-n\t\tDebug mode: parse and print the node list\n");
   printf("\t-v\t\tDebug mode: verbose output\n");
   printf("\t-l latency\tSet the maximum variable latency (ps).  (default %g)\n",
		MaxLatency);
   printf("\t-b buffername\tSet the name of a buffer gate\n");
   printf("\t-s separator\tGate names have \"separator\" before drive strength\n");
   printf("\t-o value\tSet the maximum output capacitance (fF).  (default %g)\n",
		MaxOutputCap);
   printf("\t-p filepath\tSpecify an alternate path and filename for gate.cfg\n");
   printf("\t-f filepath\tSpecify a path and filename for list of nets to ignore\n");
   printf("\t-h\t\tprint this help message\n\n");

   printf("This will not work at all for tristate gates.\n");
   printf("Nodes with multiple outputs are assumed to be in parallel.\n");
   exit(-3);
}

/* end of blifFanout.c */
