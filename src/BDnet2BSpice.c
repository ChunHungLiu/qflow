// BDnet2BSpice
//
// Revision 0, 2006-11-11: First release by R. Timothy Edwards.
// Revision 1, 2009-07-13: Minor cleanups by Philipp Klaus Krause.
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
#define NumberOfUniqueConnections 6

/* getopt stuff */
extern	int	optind, getopt();
extern	char	*optarg;

void ReadNetlistAndConvert(FILE *, FILE *, int);
void CleanupString(char text[]);
float getnumber(char *strpntbegin);
int loc_getline( char s[], int lim, FILE *fp);
void helpmessage();

int main ( int argc, char *argv[])
{

	FILE *NET1, *NET2, *OUT;
	struct Resistor *ResistorData;
	int i,AllMatched,NetsEqual,ImplicitPower;

	char Net1name[LengthOfNodeName];
	






	ImplicitPower=TRUE;
        while( (i = getopt( argc, argv, "phH" )) != EOF ) {
	   switch( i ) {
	   case 'p':
	       ImplicitPower=FALSE;
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
	ReadNetlistAndConvert(NET1,OUT,ImplicitPower);
        return 0;


}





/*--------------------------------------------------------------*/
/*C *Alloc - Allocates memory for linked list elements		*/
/*								*/
/*         ARGS: 
        RETURNS: 1 to OS
   SIDE EFFECTS: 
\*--------------------------------------------------------------*/
void ReadNetlistAndConvert(FILE *NETFILE, FILE *OUT, int ImplicitPower)
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

/* Read in line by line */

	NumberOfInstances=0;
        while(loc_getline(line, sizeof(line), NETFILE)>0 ) {
	   if(strstr(line,"MODEL") != NULL ) {
              if(sscanf(line,"MODEL %s",MainSubcktName)==1) {
	         CleanupString(MainSubcktName);
	         fprintf(OUT,".subckt %s ",MainSubcktName);
	         if(ImplicitPower) fprintf(OUT,"vdd vss ");
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
	      }
	      fprintf(OUT,"\n");
	   }
	   if(strstr(line,"INSTANCE") != NULL ) {
	      NumberOfInstances+=1;
	      fprintf(OUT,"x%d ",NumberOfInstances);
	      if(ImplicitPower) fprintf(OUT,"vdd vss ");
	      if(sscanf(line,"INSTANCE %s:",InstanceName)==1) {
	         CleanupString(InstanceName);
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
	               fprintf(OUT,"%s ",InstancePortWire);
	            }
	         }
	      }
	      /* Done with I/O section, add instance name to subckt line */
              fprintf(OUT,"%s\n",InstanceName);
	   } 
	}
}

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


