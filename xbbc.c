/* XBBC.C   The X11-basic bytecode compiler         (c) Markus Hoffmann
*/

/* This file is part of X11BASIC, the basic interpreter for Unix/X
 * ======================================================================
 * X11BASIC is free software and comes with NO WARRANTY - read the file
 * COPYING for details
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef WINDOWS
#define EX_CANTCREAT	73	/* can't create (user) output file */
#define EX_NOINPUT	66	/* cannot open input */
#define EX_OK 0
#else
#include <sysexits.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "config.h"
#include "defs.h"
#include "globals.h"
#include "x11basic.h"
#include "functions.h"
#include "ptypes.h"
#include "io.h"
#include "file.h"

#include "bytecode.h"


char ifilename[128]="new.bas";   /* Standard inputfile  */
char ofilename[128]="b.b";       /* Standard outputfile     */
int verbose=0;
int loadfile=FALSE;
int dostrip=0;                   /* dont write symbol tables */

STRING bcpc;

STRING strings;     /* Holds comments and sybol names */

BYTECODE_SYMBOL *symtab;
int anzsymbols;

int donops=0;
int docomments=0;


extern void reset_input_mode(),x11basicStartup();
extern int param_anzahl;
extern char **param_argumente;
int programbufferlen=0;
char *programbuffer=NULL;
const char version[]="1.17"; /* Version Number. Put some useful information here */
const char vdate[]="2011-08-16";   /* Creation date.  Put some useful information here */
extern const char libversion[];
extern const char libvdate[];
char *program[MAXPRGLEN];
int prglen=0;

int save_bytecode(char *name,char *adr,int len,char *dadr,int dlen) {
  int fdis=creat(name,0644);
  
  BYTECODE_HEADER h;
  if(fdis==-1) return(-1);
  
  h.BRAs=BC_BRAs;
  h.offs=sizeof(BYTECODE_HEADER)-2;
  h.textseglen=len;
  h.dataseglen=dlen;
  h.bssseglen=0;
  if(dostrip) {h.symbolseglen=0;h.stringseglen=0;}
  else {
    h.symbolseglen=anzsymbols*sizeof(BYTECODE_SYMBOL);
    h.stringseglen=strings.len;
  }
  h.version=BC_VERSION;

  if(verbose) {
    printf("Info:\n");
    printf("  Size of   Text-Segment: %d\n",h.textseglen);
    printf("  Size of   Data-Segment: %d\n",h.dataseglen);
    printf("  Size of String-Segment: %d\n",h.stringseglen);
    printf("  Size of Symbol-Segment: %d (%d symbols)\n",h.symbolseglen,anzsymbols);
  }

  if(write(fdis,&h,sizeof(BYTECODE_HEADER))==-1) io_error(errno,"write");
  if(write(fdis,adr,len)==-1) io_error(errno,"write");
  if(write(fdis,dadr,dlen)==-1) io_error(errno,"write");
  if(write(fdis,strings.pointer,h.stringseglen)==-1) io_error(errno,"write");
  if(write(fdis,symtab,h.symbolseglen)==-1) io_error(errno,"write");
  return(close(fdis));
}

void doit(char *ausdruck) {
  PARAMETER *p;
  int n;
  bcpc.len=0;
  printf("Ausdruck: %s\n",ausdruck);
  bc_parser(ausdruck);
  memdump(bcpc.pointer,bcpc.len);
  printf("Virtual Machine: %d Bytes\n",bcpc.len);
  p=virtual_machine(bcpc,&n);
  dump_stack(p,n);
  free_pliste(n,p);
}

void intro(){
  puts("*************************************************************\n"
       "*           X11-Basic bytecode compilter                    *\n"
       "*                    by Markus Hoffmann 1997-2011 (c)       *");
  printf("* library V. %s date:   %30s    *\n",libversion,libvdate);
  puts("*************************************************************\n");
}
void usage(){
  printf("Usage: %s [-o <outputfile> -h] [<filename>] --- compile program [%s]\n\n","bytecode",ifilename);
  printf("-o <outputfile>\t--- put result in file [%s]\n",ofilename);
  puts("-h --help\t--- Usage\n"
       "-c\t\t--- also compile in comments\n"
       "-n\t\t--- also compile in nop's\n"
       "-s\t\t--- strip symbols\n"
       "-v\t\t--- be more verbose");
}


void kommandozeile(int anzahl, char *argumente[]) {
  int count;

  /* Kommandozeile bearbeiten   */
  for(count=1;count<anzahl;count++) {
    if (strcmp(argumente[count],"-o")==FALSE) {
      strcpy(ofilename,argumente[++count]);
    } else if (strcmp(argumente[count],"-h")==FALSE) {
      intro();
      usage();
    } else if (strcmp(argumente[count],"-e")==FALSE) {
      doit(argumente[++count]);
    } else if (strcmp(argumente[count],"-n")==FALSE) {
      donops=!donops;
    } else if (strcmp(argumente[count],"-c")==FALSE) {
      docomments=!docomments;
    } else if (strcmp(argumente[count],"-s")==FALSE) {
      dostrip=!dostrip;
    } else if (strcmp(argumente[count],"-v")==FALSE) {
      verbose++;
    } else if (strcmp(argumente[count],"-q")==FALSE) {
      verbose--;
    } else if (strcmp(argumente[count],"--help")==FALSE) {
      intro();
      usage();
    } else {
      if(!loadfile) {
        loadfile=TRUE;
        strcpy(ifilename,argumente[count]);
      }
    }
   }
}

#define MAX_CODE 256000

int main(int anzahl, char *argumente[]) {
  /* Initialize data segment buffer */
  if(anzahl<2) {    /* Kommandomodus */
    intro();usage();
  } else {
    bcpc.pointer=malloc(MAX_CODE);
    kommandozeile(anzahl, argumente);    /* Kommandozeile bearbeiten */
    if(loadfile) {
      if(exist(ifilename)) {
        int ret;
        loadprg(ifilename);
	compile();
	ret=save_bytecode(ofilename,(char *)bcpc.pointer,bcpc.len,databuffer,databufferlen);
	if(ret==-1) exit(EX_CANTCREAT);
      } else {
        printf("ERROR: %s not found !\n",ifilename);
        exit(EX_NOINPUT);
      }
    } else exit(EX_NOINPUT);
    free(bcpc.pointer);
  }
  return(EX_OK);
}
