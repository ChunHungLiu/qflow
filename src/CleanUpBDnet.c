// CleanUpBDnet
//
// Revision 0, version 1   2006-11-11: First release by R. Timothy Edwards.
// Revision 1, version 1.1 2009-07-13: Minor cleanups by Philipp Klaus Krause.
//
// This program is written in ISO C99.

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
#define LengthOfInOutString 10000
#define NumberOfUniqueConnections 6

#define VERSIONSTRING "1.1"
#define VERSIONDATE "2009-07-13"

/* getopt stuff */
extern	int	optind, getopt();
extern	char	*optarg;

struct Vect {
	struct Vect *next;
	char name[LengthOfLine];
	char direction[LengthOfNodeName];
	int Max;
};

void ReadNetlistAndConvert(FILE *, FILE *, int);
void CleanupString(char text[]);
void ToLowerCase( char *text);
float getnumber(char *strpntbegin);
int loc_getline( char s[], int lim, FILE *fp);
void helpmessage();
int ParseNumber( char *test);
struct Vect *VectorAlloc(void);

int BussesLeftAlone=FALSE;

int main ( int argc, char *argv[])
{

	FILE *NET1, *NET2, *OUT;
	struct Resistor *ResistorData;
	int i,AllMatched,NetsEqual,CleanUpInternal;

	char Net1name[LengthOfNodeName];
	


	CleanUpInternal=FALSE;
        while( (i = getopt( argc, argv, "bvfhH" )) != EOF ) {
	   switch( i ) {
           case 'v':
              fprintf(stderr,"Version %s date: %s\n",VERSIONSTRING,VERSIONDATE);
              exit(0);
              break;
	   case 'f':
	       CleanUpInternal=TRUE;
	       break;
	   case 'b':
	       BussesLeftAlone=TRUE;
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
		fprintf(stderr,"Couldn't open %s for read\n",Net1name);
		exit(EXIT_FAILURE);
	}
	OUT=stdout;
	ReadNetlistAndConvert(NET1,OUT, CleanUpInternal);
        return 0;
} /* main() */




/*--------------------------------------------------------------*/
/*C ReadNetlistAndConvert                        		*/
/*								*/
/*         ARGS: 
        RETURNS: 1 to OS
   SIDE EFFECTS: 
\*--------------------------------------------------------------*/
void ReadNetlistAndConvert(FILE *NETFILE, FILE *OUT, int CleanUpInternal)
{

	struct Vect *Vector,*VectorPresent;
	int i,Found,NumberOfInputs,NumberOfOutputs,NumberOfInstances;
	int First,VectorIndex,ItIsAnInput,ItIsAnOutput,Done,DoneIO;
	int EndOfInput, EndOfOutput, CutThis, FirstOut;

	float length, width, Value;


	char *node2pnt,*lengthpnt,*widthpnt,*FirstblankafterNode2, *Weirdpnt;
	char *charpnt;
        char line[LengthOfLine];
	char allinputs[LengthOfInOutString];
	char alloutputs[LengthOfInOutString];
	char InputName[LengthOfNodeName],InputEquivalent[LengthOfNodeName];
	char OutputName[LengthOfNodeName],OutputEquivalent[LengthOfNodeName];
	char InputNodes[MaxNumberOfInputs][2][LengthOfNodeName];
	char OutputNodes[MaxNumberOfOutputs][2][LengthOfNodeName];
        char MainSubcktName[LengthOfNodeName],node1[LengthOfNodeName];
	char InstanceName[LengthOfNodeName],InstancePortName[LengthOfNodeName];
	char InstancePortWire[LengthOfNodeName];
        char node2[LengthOfNodeName],nodelabel[LengthOfNodeName];
        char body[LengthOfNodeName],model[LengthOfNodeName];
	char temp[LengthOfNodeName];

/* Read in line by line */

	NumberOfInstances=0;
	First=TRUE;
	Vector=VectorAlloc();
	Vector->next=NULL;
	Done=FALSE;

        while(loc_getline(line, sizeof(line), NETFILE)>0 && !Done) {
	   if(strstr(line,"MODEL") != NULL ) {
 	         fprintf(OUT,"%s",line);
	   }
	   if(strstr(line,"TECHNOLOGY") != NULL ) {
	         fprintf(OUT,"%s",line);
	   }
	   if(strstr(line,"VIEWTYPE") != NULL ) {
	         fprintf(OUT,"%s",line);
	   }
	   if(strstr(line,"EDITSTYLE") != NULL ) {
	         fprintf(OUT,"%s",line);
	   }
	   if(strstr(line,"INPUT") != NULL ) {
	      fprintf(OUT,"%s",line);
	      NumberOfInputs=0;
	      EndOfInput=FALSE;
	      FirstOut = TRUE;
	      while(loc_getline(line, sizeof(line), NETFILE)>1 && strstr(line,"OUTPUT") == NULL) {
		 CutThis = FALSE;
                 if(sscanf(line,"%s :  %s",InputName,InputEquivalent)==2) {
	            CleanupString(InputName);
	            if(strchr(InputEquivalent,';') != NULL) EndOfInput=TRUE;
	            CleanupString(InputEquivalent);

		    /* Don't know why SIS writes weird nameless vectors in input list */
		    if (InputName[1] == '\[' && InputEquivalent[1] == '\[')
			CutThis = TRUE;

		    if (!CutThis) {
		       if (!FirstOut) fprintf(OUT, "\n");
		       FirstOut = FALSE;
	               strcpy(InputNodes[NumberOfInputs][0],InputName);
	               strcpy(InputNodes[NumberOfInputs][1],InputEquivalent);
	               fprintf(OUT,"\t%s\t:\t%s",InputName,InputEquivalent);
		    }
	            if (EndOfInput) fprintf(OUT,";\n\n");

		    if (!CutThis) NumberOfInputs+=1; 
	         }
	      }
	   }
	   if(strstr(line,"OUTPUT") != NULL ) {
	      fprintf(OUT,"%s",line);
	      NumberOfOutputs=0;
	      EndOfOutput=FALSE;
	      FirstOut = TRUE;
	      while(loc_getline(line, sizeof(line), NETFILE)>1 && strstr(line,"INSTANCE") == NULL) {
		 CutThis = FALSE;
                 if(sscanf(line,"%s :  %s",OutputName,OutputEquivalent)==2) {
	            CleanupString(OutputName);
	            if(strchr(OutputEquivalent,';') != NULL) EndOfOutput=TRUE;
	            CleanupString(OutputEquivalent);

		    /* Not sure SIS does this to output lists, but just to be sure. . .*/
		    if (OutputName[1] == '\[' && OutputEquivalent[1] == '\[')
			CutThis = TRUE;

		    if (!CutThis) {
		       if (!FirstOut) fprintf(OUT, "\n");
		       FirstOut = FALSE;
	               strcpy(OutputNodes[NumberOfOutputs][0],OutputName);
	               strcpy(OutputNodes[NumberOfOutputs][1],OutputEquivalent);
	               fprintf(OUT,"\t%s\t:\t%s",OutputName,OutputEquivalent);
		    }
	            if(EndOfOutput) fprintf(OUT,";\n\n");
	            else if (!CutThis) fprintf(OUT,"\n");

	            if (!CutThis) NumberOfOutputs+=1; 
	         }
	      }
	   }
	   if(strstr(line,"INSTANCE") != NULL ) {
	      fprintf(OUT,"%s",line);
	      NumberOfInstances+=1;
	      while(loc_getline(line, sizeof(line), NETFILE)>0 && strstr(line,"ENDMODEL") == NULL) {
	            if(sscanf(line,"%s :  %s;",InstancePortName,InstancePortWire)==2) {
	               ItIsAnInput=FALSE;
	               ItIsAnOutput=FALSE;
	               strcpy(temp,InstancePortWire);
	               CleanupString(temp);
	               DoneIO=FALSE;
	               if( (charpnt=strchr(temp,';')) != NULL) *charpnt='\0';
	               for(i=0;i<NumberOfInputs && !DoneIO;i++) {
	                  if(strcmp(temp,InputNodes[i][1]) == 0) {
	                     DoneIO=TRUE;
	                     ItIsAnInput=TRUE;
	                     fprintf(OUT,"\t%s\t:\t%s;\n",InstancePortName,InputNodes[i][1]);
	                  }
	               }
	               Done=FALSE;
	               for(i=0;i<NumberOfOutputs && !DoneIO;i++) {
	                  if(strcmp(temp,OutputNodes[i][1]) == 0) {
	                     DoneIO=TRUE;
	                     ItIsAnOutput=TRUE;
	                     fprintf(OUT,"\t%s\t:\t%s;\n",InstancePortName,OutputNodes[i][1]);
	                  }
	               }
	               if(!ItIsAnInput && !ItIsAnOutput) {
	                  if(CleanUpInternal) {
	                     charpnt=strrchr(InstancePortWire,'"');
	                     if(charpnt != NULL) { // This assumes the end looks like ...";
	                        if(*(charpnt-1) == '0') {
	                           *(charpnt-1) = '"';
	                           *(charpnt) = ';';
	                           *(charpnt+1) = '\0';
	                        }
	                     } 
	                     charpnt=strrchr(InstancePortWire,'<');
	                     if(charpnt != NULL && !BussesLeftAlone) *(charpnt) = '_'; 
	                     charpnt=strrchr(InstancePortWire,'>');
	                     if(charpnt != NULL) 
				 if( !BussesLeftAlone ) *(charpnt) = '_'; 
	                  }
	                  fprintf(OUT,"\t%s\t:\t%s\n",InstancePortName,InstancePortWire);
	               }
	            }
	            else fprintf(OUT,"%s",line);
	      }
	      Done=TRUE;
	      fprintf(OUT,"ENDMODEL;\n");
	   } 
	}
}


int ParseNumber( char *text)
{
	char *begin, *end;
// Assumes *text is a '['
	begin=(text+1);
	end=strchr(begin,']');
	*end='\0';
	*text='\0';
	return atoi(begin);
}
	

	

void ToLowerCase( char *text)
{
        int i=0;

        while( text[i] != '\0' ) {
           text[i]=tolower(text[i]);
           i++;
        }
}


void CleanupString(char text[LengthOfNodeName])
{
	char *CitationPnt, *Weirdpnt;
	
	CitationPnt=strrchr(text,'"');
	if(CitationPnt!=NULL) {
	   if( *(CitationPnt-1) == '0' ) {
	       *(CitationPnt-1) = '"';
	       *(CitationPnt)='\0';
	   }
	   else if(*(CitationPnt+1) == ';') *(CitationPnt+1)='\0';

	   /* Handle buffered inputs */
	   if ((strlen(text) > 5) && !strcmp(CitationPnt - 5, "0_ext")) {
	      sprintf(CitationPnt - 5, "_ext");
	   }
	}
	Weirdpnt=strchr(text,'<');
	if(Weirdpnt != NULL && !BussesLeftAlone) *Weirdpnt='_';
	Weirdpnt=strchr(text,'>');
	if(Weirdpnt != NULL && !BussesLeftAlone) *Weirdpnt='_';
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
           fprintf(stderr,"Error: getnumber: Didn't find '=' in string %s\n",
			strpntbegin);
           return DBL_MAX;
        }
        strpnt=strpnt+1;
        
	if(sscanf(strpnt,"%f%c%c",&number,&magn1, &magn2)!=3) {
            if(sscanf(strpnt,"%f%c",&number,&magn1)!=2) {
               fprintf(stderr,"Error: getnumber : Couldn't read number in %s %s\n",
			strpntbegin,strpnt);
               return DBL_MAX;
            }
        }
/*if(*strpntbegin =='m') */
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


struct Vect *VectorAlloc(void)
{
	return (struct Vect *) malloc(sizeof(struct Vect));
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

    fprintf(stderr,"CleanUpBDnet [-options] netlist \n");
    fprintf(stderr,"\n");
    fprintf(stderr,"CleanUpBDnet removes superfluous 0's and replaces"
	" <> with _ in the BDNET netlist\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"option, -b leave busses alone.  (busses stay busses.)\n");
    fprintf(stderr,"option, -f also cleans up internal nets\n");
    fprintf(stderr,"option, -h this message\n");    

    exit( EXIT_HELP );	
} /* helpmessage() */

