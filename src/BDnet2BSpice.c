//--------------------------------------------------------------
// BDnet2BSpice
//
// Revision 0, 2006-11-11: First release by R. Timothy Edwards.
// Revision 1, 2009-07-13: Minor cleanups by Philipp Klaus Krause.
// Revision 2, 2013-05-10: Modified to take a library of subcell
//		definitions to use for determining port order.
//
//--------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <float.h>

#define	EXIT_SUCCESS	0
#define	EXIT_FAILURE	1
#define	EXIT_HELP	2
#define TRUE 		1
#define FALSE		0
#define NMOS		1
#define PMOS		0
#define LengthOfNodeName  100
#define LengthOfLine    200
#define MaxNumberOfInputs 100
#define MaxNumberOfOutputs 100
#define NumberOfUniqueConnections 6

/* getopt stuff */
extern	int	optind, getopt();
extern	char	*optarg;

void ReadNetlistAndConvert(FILE *, FILE *, FILE *, char *, char *, char *);
void CleanupString(char text[]);
float getnumber(char *strpntbegin);
int loc_getline( char s[], int lim, FILE *fp);
void helpmessage();

//--------------------------------------------------------
// Structures for maintaining port order for subcircuits
// read from a SPICE library
//--------------------------------------------------------

typedef struct _portrec *portrecp;

typedef struct _portrec {
   portrecp next;
   char *name;
   char signal[LengthOfNodeName];	// Instance can write signal name here
} portrec;

typedef struct _subcircuit *subcircuitp;

typedef struct _subcircuit {
   subcircuitp next; 
   char *name;
   portrecp ports;
} subcircuit;

//--------------------------------------------------------

int main ( int argc, char *argv[])
{
	FILE *NET1, *NET2 = NULL, *OUT;
	struct Resistor *ResistorData;
	int i,AllMatched,NetsEqual;

	char Net1name[LengthOfNodeName];
	char Net2name[LengthOfNodeName];

	char *vddnet = NULL;
	char *gndnet = NULL;
	char *subnet = NULL;

	Net2name[0] = '\0';

	// Use implicit power if power and ground nodes are global in SPICE
	// Otherwise, use "-p".

        while( (i = getopt( argc, argv, "hHl:p:g:s:" )) != EOF ) {
	   switch( i ) {
	   case 'p':
	       vddnet = strdup(optarg);
	       break;
	   case 'g':
	       gndnet = strdup(optarg);
	       break;
	   case 's':
	       subnet = strdup(optarg);
	       break;
	   case 'l':
	       strcpy(Net2name,optarg);
	       break;
	   case 'h':
	   case 'H':
	       helpmessage();
	       break;
	   default:
	       fprintf(stderr,"\nbad switch %d\n", i );
	       helpmessage();
	       break;
	   }
        }

        if( optind < argc )	{
           strcpy(Net1name,argv[optind]);
	   optind++;
	}
	else	{
	   fprintf(stderr,"Couldn't find a filename as input\n");
	   exit(EXIT_FAILURE);
	}
        optind++;
	NET1=fopen(Net1name,"r");
	if (NET1 == NULL ) {
		fprintf(stderr,"Couldn't open %s for reading\n",Net1name);
		exit(EXIT_FAILURE);
	}

	if (Net2name[0] != '\0') {
	    NET2 = fopen(Net2name, "r");
	    if (NET2 == NULL)
		fprintf(stderr, "Couldn't open %s for reading\n", Net2name);
	}

	OUT=stdout;
	ReadNetlistAndConvert(NET1, NET2, OUT, vddnet, gndnet, subnet);
        return 0;
}

/*--------------------------------------------------------------*/
/*C *Alloc - Allocates memory for linked list elements		*/
/*								*/
/*         ARGS: 
        RETURNS: 1 to OS
   SIDE EFFECTS: 
\*--------------------------------------------------------------*/

void ReadNetlistAndConvert(FILE *NETFILE, FILE *libfile, FILE *OUT, 
		char *vddnet, char *gndnet, char *subnet)
{
	int i,Found,NumberOfInputs,NumberOfOutputs,NumberOfInstances;

	float length, width, Value;

	char *node2pnt,*lengthpnt,*widthpnt,*FirstblankafterNode2;
        char line[LengthOfLine];
	char InputName[LengthOfNodeName],InputEquivalent[LengthOfNodeName];
	char OutputName[LengthOfNodeName],OutputEquivalent[LengthOfNodeName];
	char InputNodes[MaxNumberOfInputs][2][LengthOfNodeName];
	char OutputNodes[MaxNumberOfOutputs][2][LengthOfNodeName];
        char MainSubcktName[LengthOfNodeName],node1[LengthOfNodeName];
	char InstanceName[LengthOfNodeName],InstancePortName[LengthOfNodeName];
	char InstancePortWire[LengthOfNodeName];
        char node2[LengthOfNodeName],nodelabel[LengthOfNodeName];
        char body[LengthOfNodeName],model[LengthOfNodeName];

	subcircuitp subcktlib = NULL, tsub;
	portrecp tport;

	int uniquenode = 1000;

	// Read a SPICE library of subcircuits

	if (libfile != NULL) {
	    char *sp, *sp2;
	    subcircuitp newsubckt;
	    portrecp newport, lastport;

	    /* If we specify a library, then we need to make sure that	*/
	    /* "vddnet" and "gndnet" are non-NULL, so that they will be	*/
	    /* filled in correctly.  If not specified on the command	*/
	    /* line, they default to "vdd" and "vss".			*/

	    if (vddnet == NULL) vddnet = strdup("vdd");
	    if (gndnet == NULL) gndnet = strdup("gnd");

	    /* Read SPICE library of subcircuits, if one is specified.	*/
	    /* Retain the name and order of ports passed to each	*/
	    /* subcircuit.						*/
	    while (loc_getline(line, sizeof(line), libfile) > 0) {
		if (!strncasecmp(line, ".subckt", 7)) {
		   /* Read cellname */
		   sp = line + 7;
		   while (isspace(*sp) && (*sp != '\n')) sp++;
		   sp2 = sp;
		   while (!isspace(*sp2) && (*sp2 != '\n')) sp2++;
		   *sp2 = '\0';

		   newsubckt = (subcircuitp)malloc(sizeof(subcircuit));
		   newsubckt->name = strdup(sp);
		   newsubckt->next = subcktlib;
		   subcktlib = newsubckt;
		   newsubckt->ports = NULL;

		   sp = sp2 + 1;
		   while (isspace(*sp) && (*sp != '\n') && (*sp != '\0')) sp++;
		   while (1) {

		      /* Move string pointer to next port name */

		      if (*sp == '\n' || *sp == '\0') {
			 loc_getline(line, sizeof(line), libfile);
			 if (*line == '+') {
			    sp = line + 1;
			    while (isspace(*sp) && (*sp != '\n')) sp++;
			 }
			 else
			    break;
		      }

		      /* Terminate port name and advance pointer */
		      sp2 = sp;
		      while (!isspace(*sp2) && (*sp2 != '\n')) sp2++;
		      *sp2 = '\0';

		      /* Get next port */

		      newport = (portrecp)malloc(sizeof(portrec));
		      newport->next = NULL;
		      newport->name = strdup(sp);
	
		      /* This is a bit of a hack.  It's difficult to	*/
		      /* tell what the standard cell set is going to	*/
		      /* use for power bus names.  It's okay to fill in	*/
		      /* signals with similar names here, as they will	*/
		      /* (should!) match up to port names in the BDNET	*/
		      /* file, and will be overwritten later.  Well	*/
		      /* connections are not considered here, but maybe	*/
		      /* they should be?				*/

		      if (!strncasecmp(sp, "vdd", 3))
			 strcpy(newport->signal, vddnet);
		      else if (!strncasecmp(sp, "vss", 3))
			 strcpy(newport->signal, gndnet);
		      else if (!strncasecmp(sp, "gnd", 3))
			 strcpy(newport->signal, gndnet);
		      else if (!strncasecmp(sp, "sub", 3)) {
			 if (subnet != NULL)
			    strcpy(newport->signal, subnet);
		      }
		      else 
		         newport->signal[0] = '\0';

		      if (newsubckt->ports == NULL)
			 newsubckt->ports = newport;
		      else
			 lastport->next = newport;

		      lastport = newport;

		      sp = sp2 + 1;
		   }

		   /* Read input to end of subcircuit */

		   if (strncasecmp(line, ".ends", 4)) {
		      while (loc_getline(line, sizeof(line), libfile) > 0)
		         if (!strncasecmp(line, ".ends", 4))
			    break;
		   }
		}
	    }
	}

	/* Read in line by line */

	NumberOfInstances=0;
        while(loc_getline(line, sizeof(line), NETFILE)>0 ) {
	   if(strstr(line,"MODEL") != NULL ) {
              if(sscanf(line,"MODEL %s",MainSubcktName)==1) {
	         CleanupString(MainSubcktName);
		 fprintf(OUT, "*SPICE netlist created from BDNET module "
			"%s by BDnet2BSpice\n", MainSubcktName);
		 fprintf(OUT,"");

		 if (subcktlib != NULL) {
		    /* Write out the subcircuit library file verbatim */
		    rewind(libfile);
		    while (loc_getline(line, sizeof(line), libfile) > 0)
		       fputs(line, OUT);
		    fclose(libfile);
		    fprintf(OUT,"");
		 }

	         fprintf(OUT,".subckt %s ",MainSubcktName);
	         if (vddnet == NULL)
		    fprintf(OUT,"vdd ");
		 else
		    fprintf(OUT,"%s ", vddnet);

	         if (gndnet == NULL)
		    fprintf(OUT,"vss ");
		 else
		    fprintf(OUT,"%s ", gndnet);

		 if ((subnet != NULL) && strcasecmp(subnet, gndnet))
		    fprintf(OUT,"%s ", subnet);
	      }
	      else if(strstr(line,"ENDMODEL") != NULL) {
                 fprintf(OUT,".ends %s\n ",MainSubcktName);
              }
	   }
	   if(strstr(line,"INPUT") != NULL ) {
	      NumberOfInputs=0;
	      while(loc_getline(line, sizeof(line), NETFILE)>1 ) {
                 if(sscanf(line,"%s :  %s",InputName,InputEquivalent)==2) {
	            CleanupString(InputName);
	            CleanupString(InputEquivalent);
	            strcpy(InputNodes[NumberOfInputs][0],InputName);
	            strcpy(InputNodes[NumberOfInputs][1],InputEquivalent);
	            fprintf(OUT,"%s ",InputName);
	            NumberOfInputs+=1; 
	         }
		 else if (strstr(line, "OUTPUT")) break;
	      }
	   }
	   if(strstr(line,"OUTPUT") != NULL ) {
	      NumberOfOutputs=0;
	      while(loc_getline(line, sizeof(line), NETFILE)>1 ) {
                 if(sscanf(line,"%s :  %s",OutputName,OutputEquivalent)==2) {
	            CleanupString(OutputName);
	            CleanupString(OutputEquivalent);
	            strcpy(OutputNodes[NumberOfOutputs][0],OutputName);
	            strcpy(OutputNodes[NumberOfOutputs][1],OutputEquivalent);
	            fprintf(OUT,"%s ",OutputName);
	            NumberOfOutputs+=1; 
	         }
		 else if (strstr(line, "INSTANCE")) break;
	      }
	      fprintf(OUT,"\n");
	   }
	   if(strstr(line,"INSTANCE") != NULL ) {
	      NumberOfInstances+=1;
	      fprintf(OUT,"x%d ",NumberOfInstances);
	      if(sscanf(line,"INSTANCE %s:",InstanceName)==1) {
	         CleanupString(InstanceName);

		 /* Search library records for subcircuit */
		 if (subcktlib != NULL) {
		    for (tsub = subcktlib; tsub; tsub = tsub->next) {
		       if (!strcasecmp(InstanceName, tsub->name))
			  break;
		    }
		 }

	         if (tsub == NULL) {
	            if(vddnet == NULL) fprintf(OUT,"vdd ");
	            if(gndnet == NULL) fprintf(OUT,"vss ");
	         }

	         while(loc_getline(line, sizeof(line), NETFILE)>1 ) {
	            if(sscanf(line,"%s :  %s",InstancePortName,InstancePortWire)==2) {
	               CleanupString(InstancePortWire);
	               for(i=0;i<NumberOfInputs;i++) {
	                  if(strcmp(InstancePortWire,InputNodes[i][1]) == 0) {
	                     strcpy(InstancePortWire,InputNodes[i][0]);
	                  }
	               }
	               for(i=0;i<NumberOfOutputs;i++) {
	                  if(strcmp(InstancePortWire,OutputNodes[i][1]) == 0) {
	                     strcpy(InstancePortWire,OutputNodes[i][0]);
	                  }
	               }

		       if (tsub == NULL)
	                  fprintf(OUT,"%s ",InstancePortWire);
		       else {
			  // Find port name in list
	                  CleanupString(InstancePortName);
			  for (tport = tsub->ports; tport; tport = tport->next) {
			     if (!strcmp(tport->name, InstancePortName)) {
				sprintf(tport->signal, InstancePortWire);
				break;
			     }
			  }
			  if (tport == NULL)
			     /* This will likely screw everything up. . . */
	                     fprintf(OUT,"%s ",InstancePortWire);
		       }
	            }
	         }
	      }

	      /* Done with I/O section, add instance name to subckt line */

	      if (tsub != NULL) {
		 /* Write out all ports in proper order */
		 for (tport = tsub->ports; tport; tport = tport->next) {
		    if (tport->signal[0] == '\0')
		       fprintf(OUT,"%d ", uniquenode++);
		    else
		       fprintf(OUT,"%s ", tport->signal);
		 }
	      }
              fprintf(OUT,"%s\n",InstanceName);
	   } 
	}
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

void CleanupString(char text[LengthOfNodeName])
{
	int i;
	char *CitationPnt;
	
	CitationPnt=strchr(text,'"');
	if( CitationPnt != NULL) {
	   i=0;
	   while( CitationPnt[i+1] != '"' ) {
	      CitationPnt[i]=CitationPnt[i+1];
	      i+=1;
	   }
	   CitationPnt[i]='\0';
           CitationPnt=strchr(text,'[');
	   if(CitationPnt != NULL) {
              i=0;
              while( CitationPnt[i+1] != ']' ) {
                 CitationPnt[i]=CitationPnt[i+1];
                 i+=1;
              }
              CitationPnt[i]='\0';
	   }
	}
}

/*--------------------------------------------------------------*/
/*C getnumber - gets number pointed by strpntbegin		*/
/*								*/
/*         ARGS: strpntbegin - number expected after '='        */ 
/*        RETURNS: 1 to OS
   SIDE EFFECTS: 
\*--------------------------------------------------------------*/
float getnumber(char *strpntbegin)
{       int i;
        char *strpnt,magn1,magn2;
        float number;

        strpnt=strpntbegin;
        strpnt=strchr(strpntbegin,'=');
        if(strpnt == NULL) {
           fprintf(stderr,"Error: getnumber: Didn't find '=' in string "
                               "%s\n",strpntbegin);
           return DBL_MAX;
        }
        strpnt=strpnt+1;
        
	if(sscanf(strpnt,"%f%c%c",&number,&magn1, &magn2)!=3) {
            if(sscanf(strpnt,"%f%c",&number,&magn1)!=2) {
               fprintf(stderr,"Error: getnumber : Couldn't read number in "
                      "%s %s\n",strpntbegin,strpnt);
               return DBL_MAX;
            }
        }

        switch( magn1 ) {
        case 'f':
           number *= 1e-15;
           break;          
        case 'p':
           number *= 1e-12;
           break;          
        case 'n':
           number *= 1e-9;
           break;          
        case 'u':
           number *= 1e-6;
           break;          
        case 'm':
           if(magn2='e') number *= 1e6;
           else number *= 1e-3;
           break;          
        case 'k':
           number *= 1e3;
           break;          
        case 'g':
           number *= 1e9;
           break;
        case ' ':
           default:
           return number;
        }
        return number;
}          

/*--------------------------------------------------------------*/
/*C loc_getline: read a line, return length		*/
/*								*/
/*         ARGS: 
        RETURNS: 1 to OS
   SIDE EFFECTS: 
\*--------------------------------------------------------------*/
int loc_getline( char s[], int lim, FILE *fp)
{
	int c, i;
	
	i=0;
	while(--lim > 0 && (c=getc(fp)) != EOF && c != '\n')
		s[i++] = c;
	if (c == '\n');
		s[i++] = c;
	s[i] = '\0';
	if ( c == EOF ) i=0; 
	return i;
}

/*--------------------------------------------------------------*/
/*C helpmessage - tell user how to use the program		*/
/*								*/
/*         ARGS: 
        RETURNS: 1 to OS
   SIDE EFFECTS: 
\*--------------------------------------------------------------*/

void helpmessage()
{

    fprintf(stderr,"BDnet2BSpice [-options] netlist \n");
    fprintf(stderr,"\n");
    fprintf(stderr,"BDnet2BSpice converts a netlist in bdnet format \n");
    fprintf(stderr,"to BSpice subcircuit format. Output on stdout\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"option, -h this message\n");    
    fprintf(stderr,"option, -p means: don't add power nodes to instances\n");
    fprintf(stderr,"        only nodes present in the INSTANCE statement used\n");

    exit( EXIT_HELP );	
} /* helpmessage() */

