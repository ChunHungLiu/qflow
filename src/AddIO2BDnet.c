// AddIO2BDnet
//
// Revision 0, 2006-11-11: Version 0.03 by R. Timothy Edwards, SB.
// Revision 1, 2009-07-13: Version 0.04 minor cleanups by Philipp Klaus Krause.
//
// Warning: This program uses strdup(), which is a Unix-specific function, not standard C.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <float.h>

#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1
#define EXIT_HELP       2
#define TRUE            1
#define FALSE           0
#define NMOS            1
#define PMOS            0
#define LengthOfNodeName  100
#define LengthOfLine    200
#define MaxNumberOfInputs 100
#define MaxNumberOfOutputs 100
#define NumberOfUniqueConnections 6

/* getopt stuff */
extern  int     optind, getopt();
extern  char    *optarg;

struct Buffers {
        struct Buffers *next;
        char in[LengthOfLine],out[LengthOfLine];
};

struct Clocks {
        struct Clocks *next;
        char in[LengthOfLine],out[LengthOfLine];
};

struct InputPorts {
        struct InputPorts *next;
        char port[LengthOfLine];
};

struct FlopCell {
	char *name;
	char *pin_in;
	char *pin_out;
	char *pin_clock;
};

struct BufCell {
	char *name;
	char *pin_in;
	char *pin_out;
};

int loc_getline( char s[], int lim, FILE *fp);
struct Buffers *BufferAlloc();
struct Clocks *ClockAlloc();
struct InputPorts *CIAlloc();
void AddClocks( FILE *BDNETFILE ,struct Clocks *Clock, struct Buffers *Buffer,
	int, int, struct InputPorts *, struct BufCell *, struct FlopCell *);
void ReadClockInput(FILE *INPUT, struct InputPorts *ClockedInput);
void ReadGenlib(char *, struct BufCell *, struct FlopCell *);
void helpmessage();

int   NoClockInputs = TRUE;

int main ( int argc, char *argv[])
{

        FILE *NET1, *NET2, *OUT, *INPUTFILE;
	struct Buffers *Buffer;
	struct Clocks *Clock;
	struct InputPorts *ClockedInput;
        int i,AllMatched,NetsEqual,ImplicitPower, BuffersOn;

        char Net1name[LengthOfNodeName];
	char *ClockedInputsFile = NULL;
	char *TechFile = NULL;
	struct BufCell bufCell;
	struct FlopCell flopCell;

	bufCell.name = NULL;
	flopCell.name = NULL;

	BuffersOn=TRUE;
        while( (i = getopt( argc, argv, "c:b:f:t:nxhH" )) != EOF ) {
           switch( i ) {
	   case 't':
	       TechFile = strdup(optarg);
	       break;
	   case 'b':
	       bufCell.name = strdup(optarg);
	       break;
	   case 'f':
	       flopCell.name = strdup(optarg);
	       break;
	   case 'n':
	       BuffersOn=FALSE;
	       break;
	   case 'c':
	       ClockedInputsFile = strdup(optarg);
	       break;
	   case 'x':
	       NoClockInputs=FALSE;
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

        if( optind < argc )     {
           strcpy(Net1name,argv[optind]);
           optind++;
        }
        else    {
           fprintf(stderr,"Couldn't find a filename as input\n");
           exit(EXIT_FAILURE);
        }
        optind++;
        NET1=fopen(Net1name,"r");
        if (NET1 == NULL ) {
                fprintf(stderr,"Couldn't open %s for read\n",Net1name);
                exit(EXIT_FAILURE);
        }
	ClockedInput=CIAlloc();
	ClockedInput->next=FALSE;
	if (ClockedInputsFile) {
	   INPUTFILE=fopen(ClockedInputsFile,"r");
	   if(INPUTFILE == NULL) {
		fprintf(stderr,"Couldn't find file with inputs to clock: %s\n",
				ClockedInputsFile);
		exit(EXIT_FAILURE);
	   }
	   else {
	      NoClockInputs=FALSE;
	      ReadClockInput(INPUTFILE,ClockedInput);
	   }
	}
        OUT=stdout;
	Clock=ClockAlloc();
	Buffer=BufferAlloc();

	if (!TechFile) TechFile = strdup("/pub/tech/Jazz/verilog/jazzA35.genlib");

	ReadGenlib(TechFile, &bufCell, &flopCell);

	AddClocks(NET1, Clock, Buffer, BuffersOn, (ClockedInputsFile) ? 1 : 0,
		ClockedInput, &bufCell, &flopCell);

	if (ClockedInputsFile)	free(ClockedInputsFile);
	if (TechFile)		free(TechFile);

	if (bufCell.name)	free(bufCell.name);
	if (bufCell.pin_in)	free(bufCell.pin_in);
	if (bufCell.pin_out)	free(bufCell.pin_out);

	if (flopCell.name)	free(flopCell.name);
	if (flopCell.pin_in)	free(flopCell.pin_in);
	if (flopCell.pin_out)	free(flopCell.pin_out);
	if (flopCell.pin_clock)	free(flopCell.pin_clock);

        return 0;

}

/*------------------------------------------------------------------------------*/
/* Parse the genlib file to get the pin names for the buffer and flop/latch	*/
/* cells.  NOTE:  This is deprecated with the use of "abc", which doesn't	*/
/* accept LATCH	entries in the genlib file.  Instead, we'll have to rewrite	*/
/* this to read a LEF file.  For now, we expect to find LATCH entries in the	*/
/* genlib file, but commented out.						*/
/*------------------------------------------------------------------------------*/

void ReadGenlib(char *techName, struct BufCell *bufCell, struct FlopCell *flopCell)
{
    FILE *inFile;
    char line[LengthOfLine];
    char type[LengthOfLine];
    char cellname[LengthOfLine];
    char pin1[LengthOfLine];
    char pin2[LengthOfLine];
    char *scptr;
    int have_buffer = FALSE;
    int have_flop = FALSE;
    int need_control = FALSE;

    inFile = fopen(techName, "r");
    if (inFile == NULL) {
	fprintf(stderr, "Error: Genlib file '%s' not found\n", techName);
	return;
    }

    while (loc_getline(line, sizeof(line), inFile) > 0) {
	char *lineptr = line;
	if (*lineptr == '#') lineptr++;
	while (*lineptr == ' ') lineptr++;
	if (sscanf(lineptr, "%s %s", type, cellname) == 2) {
	    if (!have_buffer && !strcmp(type, "GATE")) {
	       if (need_control) need_control = FALSE;
	       if (!strcmp(cellname, bufCell->name)) {
		   if (sscanf(lineptr, "%*s %*s %*g %s = %s", pin1, pin2) == 2) {
		      bufCell->pin_out = strdup(pin1);
		      if ((scptr = strchr(pin2, ';')) != NULL) *scptr = '\0';
		      bufCell->pin_in = strdup(pin2);
		      have_buffer = TRUE;
		   }
		   else {
		      fprintf(stderr, "Error:  Gate %s found in %s, but definition:\n",
				bufCell->name, techName);
		      fprintf(stderr, "  '%s'\n", lineptr);
		      fprintf(stderr, "doesn't match expected syntax:\n");
		      fprintf(stderr, "  'GATE %s <value> <pin_out> = <pin_in>;\n",
				bufCell->name);
		      break;
		   }
	       }
	    }
	    else if (!strcmp(type, "LATCH")) {
	       if (need_control) need_control = FALSE;
	       if (!have_flop && !strcmp(cellname, flopCell->name)) {
		   if (sscanf(lineptr, "%*s %*s %*g %s = %s", pin1, pin2) == 2) {
		      flopCell->pin_out = strdup(pin1);
		      if ((scptr = strchr(pin2, ';')) != NULL) *scptr = '\0';
		      flopCell->pin_in = strdup(pin2);
		      need_control = TRUE;
		   }
		   else {
		      fprintf(stderr, "Error:  Latch %s found in %s, but definition:\n",
				flopCell->name, techName);
		      fprintf(stderr, "  '%s'\n", lineptr);
		      fprintf(stderr, "doesn't match expected syntax:\n");
		      fprintf(stderr, "  'LATCH %s <value> <pin_out> = <pin_in>;\n",
				bufCell->name);
		      break;
		   }
	       }
	    }
	    else if (!strcmp(type, "CONTROL")) {
	    	if (need_control) {
		    flopCell->pin_clock = strdup(cellname);
		    need_control = FALSE;
		    have_flop = TRUE;
		}
	    }
	}
    }

    if (!have_buffer) {
	free(bufCell->name);
	bufCell->name = NULL;
    }
    if (!have_flop) {
	free(flopCell->name);
	flopCell->name = NULL;
    }
}

void ReadClockInput(FILE *INPUT, struct InputPorts *ClockedInput)
{
	struct InputPorts *ClockedInputPresent;
	char line[LengthOfLine];
	char port[LengthOfLine];

	ClockedInputPresent=ClockedInput;
        while(loc_getline(line, sizeof(line), INPUT)>0 ) {
	   if(line[0] != '*') {
	      if(sscanf(line,"%s",port) != 1) {
	         fprintf(stderr,"problem reading clock input file :%s\n",line);
	      }
	      ClockedInputPresent->next=CIAlloc();
	      strcpy(ClockedInputPresent->port,port);
	      ClockedInputPresent=ClockedInputPresent->next;
	      ClockedInputPresent->next=NULL;
	   }
	}
}





/*
void AddBuffers( FILE *BDNETFILE, struct Buffers *Buffer )
{
	struct Buffers *BuffPresent;
	char line[LengthOfLine];
	char outnode[LengthOfLine],outlabel[LengthOfLine];
	int OUTPUTSection;


	BuffPresent=Buffer;
        while(loc_getline(line, sizeof(line), BDNETFILE)>0 ) {
	   if(strcmp(line,"OUTPUT") ==0 ) OUTPUTSection=TRUE;
	   if(OUTPUTSection == TRUE) {
	      if( sscanf(line,"%s : %s",outlabel,outnode) ==2) {
	         BuffPresent->next=BuffAlloc();
	         strcpy(BuffPresent->in,outnode);
	         strcpy(BuffPresent->out,outlabel);
	         BuffPresent=BuffPresent->next;
	         BuffPresent->next=NULL;
	         fprintf(stdout,"%s : %s",outlabel,outlabel);
	      }
	      else fprintf(stderr,"Read Error in output \n"); 
	   else fprintf(stdout,"%s",line);

} */

/* Check for net names changed due to an inserted flop */

void CheckClock( char *name, struct Clocks *Clock)
{
 	struct Clocks *ClockPresent;
	int nlen, NeedSemi = FALSE;

	nlen = strlen(name);
	if (name[nlen - 1] == ';') {
	   NeedSemi = TRUE;
	   name[nlen - 1] = '\0';
	}
	    
	    
	for (ClockPresent = Clock; ClockPresent != NULL; ClockPresent =
		ClockPresent->next) {
	    if (!strncmp(name, ClockPresent->in, nlen)) {
		sprintf(name, ClockPresent->out);
		if (NeedSemi) strcat(name, ";");
		break;
	    }
	}
}

/* Add output and (optionally) input buffers buffers */

void AddClocks( FILE *BDNETFILE ,struct Clocks *Clock, struct Buffers *Buffer,
	int BuffersOn, int ClockSomeInputs, struct InputPorts *ClockedInput,
	struct BufCell *bufCell, struct FlopCell *flopCell)
{
	struct InputPorts *ClockedInputPresent;
	struct Clocks *ClockPresent;	
	struct Buffers *BuffPresent;
	int INPUTSection,OUTPUTSection, ADDIO;
	int AddClock2ThisOne;
	int LastInput = FALSE;
	int LastOutput = FALSE;
        char line[LengthOfLine];
	char inlabel[LengthOfLine],innode[LengthOfLine];
	char outlabel[LengthOfLine],outnode[LengthOfLine];

	INPUTSection=FALSE;
        OUTPUTSection=FALSE;
	ADDIO=FALSE;
	ClockPresent=Clock;
	BuffPresent=Buffer;
	if(ClockSomeInputs) ClockedInputPresent=ClockedInput;
        while(loc_getline(line, sizeof(line), BDNETFILE)>0 ) {
	   if(strncmp(line,"INPUT",5) ==0 ) {
	        fprintf(stdout,"INPUT\n");
	        INPUTSection=TRUE;
	        OUTPUTSection=FALSE;
	   }
	   if(strncmp(line,"OUTPUT",6) ==0) {
	        fprintf(stdout,"OUTPUT\n");
		INPUTSection=FALSE;
                OUTPUTSection=TRUE;
	   }
	   if(strncmp(line,"INSTANCE",8) ==0) {
                INPUTSection=FALSE;
                OUTPUTSection=FALSE;
           }
	   if(strncmp(line,"ENDMODEL",8) ==0 ) {
		if (bufCell->name != NULL && flopCell->name != NULL)
	            ADDIO=TRUE;
		else
		    fprintf(stderr,
			"Warning:  No techfile information; cannot add buffers!\n");
	   }
	   if(INPUTSection == TRUE) {
	      AddClock2ThisOne = (NoClockInputs) ? FALSE : TRUE;
	      if( sscanf(line,"%s : %s",inlabel,innode) ==2) {
	         if(ClockSomeInputs) {
	            ClockedInputPresent=ClockedInput;
	            AddClock2ThisOne=TRUE;
	            while(ClockedInputPresent->next != NULL) {
	               if(strstr(inlabel,ClockedInputPresent->port) != NULL) {
	                  AddClock2ThisOne=FALSE;
	               }
	               ClockedInputPresent=ClockedInputPresent->next;
	            }
	         }
	         if(strcmp(inlabel,"\"clock\"") !=0 && AddClock2ThisOne) {
	            ClockPresent->next=ClockAlloc();
	            if( innode[strlen(innode)-1] == ';' ) {
		       LastInput = TRUE;
	               innode[strlen(innode)-1]='\0';
		    }

		    if (!strcmp(inlabel, innode)) {
			/* Input node and label are the same.  Change	*/
			/* the input node whereever it occurs.		*/
			char tempnode[256];
			sprintf(tempnode, "\"int_%s", innode + 1);
			strcpy(innode, tempnode);
		    }

	            fprintf(stdout,"        %s\t :\t %s",inlabel,inlabel); 
		    if (LastInput) fprintf(stdout, ";");
		    fprintf(stdout, "\n");

	            strcpy(ClockPresent->in,inlabel);
	            strcpy(ClockPresent->out,innode);
	            ClockPresent=ClockPresent->next;
	            ClockPresent->next=NULL;
	         }
	         else fprintf(stdout,"%s",line);
	      }
	      else if( sscanf(line,"%s",inlabel) ==1) ;
	   //   else fprintf(stderr,"Read Error in input \n"); 
	   }
	   else if(OUTPUTSection == TRUE && BuffersOn) {
              if( sscanf(line,"%s : %s",outlabel,outnode) ==2) {
	         if( outnode[strlen(outnode)-1] == ';') LastOutput = TRUE;
		 CheckClock(outnode, Clock);
                 BuffPresent->next=BufferAlloc();
                 strcpy(BuffPresent->in,outnode);
                 strcpy(BuffPresent->out,outlabel);
                 BuffPresent=BuffPresent->next;
                 BuffPresent->next=NULL;
	         fprintf(stdout,"        %s\t :\t %s", outlabel,outlabel);
		 if (LastOutput) fprintf(stdout, ";\n");
                 fprintf(stdout,"\n");
              }
	      else if( sscanf(line,"%s",outlabel) ==1) ;
           //   else fprintf(stderr,"Read Error in output \n");
	   }
	   else if(ADDIO == TRUE) {
	      BuffPresent=Buffer;
              while(BuffPresent->next != NULL) {
                 fprintf(stdout,"INSTANCE \"%s\":\"physical\"\n", bufCell->name);
                 fprintf(stdout,"\t\"%s\"\t : \t%s;\n",
			bufCell->pin_in, BuffPresent->in);
                 fprintf(stdout,"\t\"%s\"\t : \t%s;\n",
			bufCell->pin_out, BuffPresent->out);
                 fprintf(stdout,"\n");
                 BuffPresent=BuffPresent->next;
              }
	      if( NoClockInputs == FALSE) {
		  ClockPresent=Clock;
		  while(ClockPresent->next != NULL) {
		      fprintf(stdout,"INSTANCE \"%s\":\"physical\"\n", flopCell->name);
		      fprintf(stdout,"\t\"%s\"\t : \t%s;\n",
				flopCell->pin_in, ClockPresent->in);
		      fprintf(stdout,"\t\"%s\"\t : \t\"clock\";\n",
				flopCell->pin_clock);
		      fprintf(stdout,"\t\"%s\"\t : \t%s;\n",
				flopCell->pin_out, ClockPresent->out);
		      fprintf(stdout,"\n");
		      ClockPresent=ClockPresent->next;
		  }
	      }
	      fprintf(stdout,"ENDMODEL;\n");
	   } 
	   else {
              if( sscanf(line,"%s : %s",outlabel,outnode) == 2) {
		 CheckClock(outnode, Clock);
		 fprintf(stdout,"%s : %s\n",outlabel, outnode);
	      }
	      else
		 fprintf(stdout,"%s",line);
	   }
	}	

}

/*--------------------------------------------------------------*/
/*C loc_getline: read a line, return length         */
/*                                                              */
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



struct Buffers *BufferAlloc()
{
	return (struct Buffers *) malloc( sizeof(struct Buffers));
}

struct Clocks *ClockAlloc()
{
	return (struct Clocks *) malloc(sizeof(struct Clocks));
}


struct InputPorts *CIAlloc()
{
	return (struct InputPorts *) malloc(sizeof(struct InputPorts));
}



void helpmessage()
{
    fprintf(stderr,"AddIO2BDnet [-options] bdnetfile\n");
    fprintf(stderr,"takes a BDNET file as input and adds double buffers\n");

    fprintf(stderr,"to the outputs and D-flops to all inputs. Output on stdout.\n");
    fprintf(stderr,"\nThe option -b 'buffername' uses the cell named 'buffername'\n");
    fprintf(stderr, "for buffer cells.\n");
    fprintf(stderr,"\nThe option -f 'flopname' uses the cell named 'flopname'\n");
    fprintf(stderr, "for clocked latches or flip-flops.\n");
    fprintf(stderr,"\nThe option -n does not add buffers to the output.\n");
    fprintf(stderr,"The option -x adds clocked latches to all inputs.\n");
    fprintf(stderr,"\nThe option -c 'filename', only clocks those inputs found "
	"in 'filename'\n");
    fprintf(stderr,"The format in filename is one input port per line and "
	"comments are\n");
    fprintf(stderr,"allowed on lines starting with a *\n");
    fprintf(stderr,"Furthermore, an input port found in 'filename' that partially\n");
    fprintf(stderr,"matches an input port in the netlist will not be clocked\n");
    fprintf(stderr,"This means that for buses, only the body need to be in 'filename'\n");
    fprintf(stderr,"and the whole bus will be excluded\n");
    fprintf(stderr,"\nVersion 0.04  SB/TE/PkK  2009-07-13\n");
    exit(EXIT_HELP);
}

