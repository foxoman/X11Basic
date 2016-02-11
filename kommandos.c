/* kommandos.c  Sachen fuer die Kommandobearbeitung (c) Markus Hoffmann */


/* This file is part of X11BASIC, the basic interpreter for Unix/X
 * ============================================================
 * X11BASIC is free software and comes with NO WARRANTY - read the file
 * COPYING for details
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#if defined WINDOWS
#define EX_OK 0
#include <windows.h>
#include <io.h>
#else
#ifndef ANDROID
#include <sysexits.h>
#else 
#define EX_OK 0
#endif
#endif
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "defs.h"
#include "x11basic.h"
#include "parameter.h"
#include "variablen.h"
#include "xbasic.h"

#include "kommandos.h"
#include "array.h"
#include "sound.h"
#include "parser.h"
#include "wort_sep.h"
#include "io.h"
#include "file.h"
#include "graphics.h"
#include "aes.h"
#include "window.h"
#include "mathematics.h"
#include "gkommandos.h"
#include "sysVstuff.h"
#include "bytecode.h"


/*********************/
static int saveprg(char *fname) {
  char *buf=malloc(programbufferlen);
  int i=0;
  while(i<programbufferlen) {
    if(programbuffer[i]==0 || programbuffer[i]=='\n')
      buf[i]='\n';
    else
      buf[i]=programbuffer[i];
    i++;
  }
  bsave(fname,buf,programbufferlen);
  return(0);
}



/*****************************************/
/* Kommandos zur Programmablaufkontrolle */
static void bidnm(const char *n) {
  xberror(38,n); /* Befehl im Direktmodus nicht moeglich */
}

void c_stop()  {batch=0;} 

static void c_tron()  {echoflag=1;}
static void c_troff() {echoflag=0;}
static void c_beep()  {putchar('\007');}
 
static void c_clear(PARAMETER *plist,int e){
  clear_all_variables(); 
  graphics_setdefaults();
}

void c_new(const char *n) {
  c_stop();
  clear_program();
  free_pcode(prglen);
  programbufferlen=prglen=pc=0;
  strcpy(ifilename,"new.bas");
  graphics_setdefaults();
}
static void c_while(const char *n) {
  if(parser(n)==0) { 
    if(pc<=0) {bidnm("WHILE"); return;}
    int npc=pcode[pc-1].integer;
    if(npc==-1) xberror(36,"WHILE"); /*Programmstruktur fehlerhaft */
    pc=npc;
  } 
}

void c_gosub(const char *n) {
    char *buffer,*pos,*pos2;
    int pc2;
   
    buffer=indirekt2(n);
    pos=searchchr(buffer,'(');
    if(pos!=NULL) {
      pos[0]=0;pos++;
      pos2=pos+strlen(pos)-1;
      if(pos2[0]!=')') {
	xberror(32,n); /* Syntax error */
	free(buffer);
	return;
      }	
      else pos2[0]=0;
    } else pos=buffer+strlen(buffer);
    
    pc2=procnr(buffer,1);
    if(pc2==-1)   xberror(19,buffer); /* Procedure nicht gefunden */
    else {       
	if(do_parameterliste(pos,procs[pc2].parameterliste,procs[pc2].anzpar)) {
          restore_locals(sp+1);
	  xberror(42,buffer); /* Zu wenig Parameter */
	} else { batch=1;
	  pc2=procs[pc2].zeile;
	  if(sp<STACKSIZE) {stack[sp++]=pc;pc=pc2+1;}
	  else {
	    printf("Stack-Overflow ! PC=%d\n",pc); 
	    restore_locals(sp+1);
	    xberror(39,buffer); /* Program Error Gosub impossible */
	  }
	}
    }
    free(buffer);
}
/* Spawn soll eine Routine als separaten thread ausfuehren.
   Derzeit klappt as nur als separater Task. Das bedeutet, dass 
   die beiden Programmteile nicht ueber die Variablen reden koennen.
   Hierzu muesste man die XBASIC-Variablen in Shared-Memory auslagern.
   das waere aehnlich wie EXPORT ....
   */
static void c_spawn(const char *n) {
  char *buffer,*pos,*pos2;
  int pc2;
  
  buffer=indirekt2(n);
  pos=searchchr(buffer,'(');
  if(pos!=NULL) {
    pos[0]=0;pos++;
    pos2=pos+strlen(pos)-1;
    if(pos2[0]!=')') {
      puts("Syntax error bei Parameterliste");
      xberror(39,buffer); /* Program Error Gosub impossible */
    } 
    else pos2[0]=0;
  } else pos=buffer+strlen(buffer);
  
  pc2=procnr(buffer,1);
  if(pc2==-1)	xberror(19,buffer); /* Procedure nicht gefunden */
  else {
    #ifndef WINDOWS
    pid_t forkret=fork();
    if(forkret==-1) io_error(errno,"SPAWN");
    if(forkret==0) {
      if(do_parameterliste(pos,procs[pc2].parameterliste,procs[pc2].anzpar)) {
        restore_locals(sp+1);
        xberror(42,buffer); /* Zu wenig Parameter */
      } else { 
        batch=1;
        pc2=procs[pc2].zeile;
        if(sp<STACKSIZE) {stack[sp++]=pc;pc=pc2+1;}
        else {
          printf("Stack-Overflow ! PC=%d\n",pc); 
	  restore_locals(sp+1);
          xberror(39,buffer); /* Program Error Gosub impossible */
        }      
        programmlauf();
        exit(EX_OK);
      }
    }
    #endif
  }
  free(buffer);
}
      

static void c_local(PARAMETER *plist,int e) {
  int i;
  if(e) {
    for(i=0;i<e;i++) do_local(plist[i].integer,sp);
  }
}

static void c_goto(const char *n) {
  char *b=indirekt2(n);
  pc=labelzeile(b);
  if(pc==-1) {xberror(20,b);/* Label nicht gefunden */ batch=0;}
  else batch=1;
  free(b);
}


static void c_system(PARAMETER *plist,int e) {
  if(system(plist->pointer)==-1) io_error(errno,"system");
}


static void c_shell(PARAMETER *plist,int e) {
  char *argv[e+1];
  int i;
 // printf("e=%d\n",e);
  for(i=0;i<e;i++) {
    argv[i]=plist[i].pointer;
 //   printf("%d: %s\n",i,argv[i]);
  }
  argv[e]=NULL;

  if(spawn_shell(argv)==-1) io_error(errno,"shell");
}

static void c_edit(const char *n) {
#ifndef ANDROID
    char filename[strlen(ifilename)+8];
    char buffer[256];
    char *buffer2=strdup(ifilename);
    sprintf(filename,"%s.~~~",ifilename);
    saveprg(filename);
    sprintf(buffer,"$EDITOR %s",filename); 
    if(system(buffer)==-1) io_error(errno,"system");
    c_new(NULL);  
    strcpy(ifilename,buffer2);
    free(buffer2);
    mergeprg(filename);
    sprintf(buffer,"rm -f %s",filename); 
    if(system(buffer)==-1) io_error(errno,"system");
#else
    puts("The EDIT command is not available in the ANDROID version.\n"
    "Please use Menu --> Editor to edit the current program.");
#endif
}

static void c_after(PARAMETER *plist,int e) {
    everyflag=0;
    alarmpc=plist[1].integer; /*Proc nummer*/
    alarm(plist[0].integer);     /*Zeit in sec*/
}

static void c_every(PARAMETER *plist,int e) {
  everyflag=1;
  alarmpc=plist[1].integer; /*Proc nummer*/
  everytime=plist[0].integer; /*Zeit in sec*/
  alarm(everytime);
}


static void dodim(const char *w) {
  char w1[strlen(w)+1],w2[strlen(w)+1];
  int e=klammer_sep(w,w1,w2);
 
  if(e<2) xberror(32,"DIM"); /* Syntax nicht Korrekt */
  else {
    char *s,*t;
    int ndim=count_parameters(w2);
    int dimlist[ndim];
    int i,vnr,typ;
    char *r=varrumpf(w1);
    typ=type(w1)&(~CONSTTYP);  /* Typ Bestimmen  */

    /* Dimensionen bestimmen   */
     
    i=wort_sep_destroy(w2,',',TRUE,&s,&t);
    ndim=0;
    while(i) {
      xtrim(s,TRUE,s);
      dimlist[ndim++]=(int)parser(s);
      i=wort_sep_destroy(t,',',TRUE,&s,&t); 
    }
//  printf("DIM: <%s>: dim=%d typ=$%x\n",r,ndim,typ);
    vnr=add_variable(r,ARRAYTYP,typ);
      
    /*(Re-) Dimensionieren  */
    free_array(variablen[vnr].pointer.a); /*Alten Inhalt freigeben*/
    *(variablen[vnr].pointer.a)=create_array(typ,ndim,dimlist);
//    printf("created array of type: %d\n",(variablen[vnr].pointer.a)->typ);

    free(r);
  }
}
static inline void do_restore(int offset) {
  datapointer=offset;
 // printf("DO RESTORE %d\n",offset);
}

static void c_run(const char *n) {        /* Programmausfuehrung starten und bei 0 beginnen */
  do_run();
}
void do_run() {
  restore_all_locals(); /* sp=0; von einem vorherigen Abbruch koennten noch locale vars im Zwischenspeicher sein.*/
  clear_all_variables();
  pc=0;
  batch=1;
  do_restore(0);
}


void c_cont(const char *n) {
  if(batch==0) {
    if(pc>=0 && pc<=prglen) batch=1;
    else xberror(41,"");     /*CONT nicht moeglich !*/
  } else {
    /*hier koennte es ein CONTINUE in einer SWITCH Anweisung sein. */
    /*gehe zum (bzw. hinter) naechsten CASE oder DEFAULT*/
    
    int j,f=0,o;
    for(j=pc; (j<prglen && j>=0);j++) {
      o=pcode[j].opcode&PM_SPECIAL;
      if((o==P_CASE || o==P_DEFAULT || o==P_ENDSELECT)  && f<=0) break;
      if(o & P_LEVELIN) f++;
      if(o & P_LEVELOUT) f--;
    }
    if(j==prglen) xberror(36,"SELECT/CONTINUE"); /*Programmstruktur fehlerhaft !*/ 
    else pc=j+1;
  }
}

static void c_restore(PARAMETER *plist,int e) {
  if(e) {
    do_restore((int)labels[plist[0].integer].datapointer);
  //  printf("RESTORE: %d %s\n",plist[0].integer,labels[plist[0].integer].name);
  } else do_restore(0);
}

static char *get_next_data_entry() {
  char *ptr,*ptr2;
  char *ergebnis=NULL;
  if(databufferlen==0 || databuffer==NULL || datapointer>=databufferlen) return(NULL);
  ptr=databuffer+datapointer;
  ptr2=searchchr(ptr,',');
  if(ptr2==NULL) {
    ergebnis=malloc(databufferlen-datapointer+1);
    strncpy(ergebnis,ptr,databufferlen-datapointer);
    ergebnis[databufferlen-datapointer]=0;
    datapointer=databufferlen;
  } else {
    ergebnis=malloc(ptr2-ptr+1);
    strncpy(ergebnis,ptr,(int)(ptr2-ptr));
    datapointer+=(ptr2-ptr)+1;
    ergebnis[ptr2-ptr]=0;
  } 
  return(ergebnis);
}

static void c_read(PARAMETER *plist,int e) {
  int i;
  char *t;
  for(i=0;i<e;i++) {
    t=get_next_data_entry();
    if(t==NULL) xberror(34,""); /* Zu wenig Data */
    else {
    switch(plist[i].typ) {
    case PL_ARRAYVAR: 
      printf("Read array. Not yet implemented.\n");
      break;
    case PL_SVAR:
      free_string((STRING *)plist[i].pointer);
      if(*t=='\"') *((STRING *)(plist[i].pointer))=string_parser(t);
      else *((STRING *)(plist[i].pointer))=create_string(t);
      break;
    case PL_IVAR:
      *((int *)(plist[i].pointer))=(int)parser(t);
      break;
    case PL_FVAR:
      *((double *)(plist[i].pointer))=parser(t);
      break;
    default:
      printf("ERROR: READ, unknown var type.\n");
      dump_parameterlist(&plist[i],1);
    }
    free(t);
    }
  }
}


static void c_randomize(PARAMETER *plist, int e) {
  unsigned int seed;
  if(e) seed=plist[0].integer;
  else {
    seed=time(NULL);
    if(seed==-1) io_error(errno,"RANDOMIZE");
  }
  srand(seed);
}

static void c_list(PARAMETER *plist, int e) {
  int i,a=0,o=prglen;
  if(e==2) {
    a=min(max(plist[0].integer,0),prglen);
    o=max(min(plist[1].integer+1,prglen),0);
  } else if(e==1) {
    a=min(max(plist[0].integer,0),prglen);
    o=a+1;
  }
  if(is_bytecode && programbufferlen>sizeof(BYTECODE_HEADER)-2) {
    BYTECODE_HEADER *h=(BYTECODE_HEADER *)programbuffer;
    printf("Bytecode: %s (%d Bytes) Version: %04x\n",ifilename,programbufferlen,h->version);
    printf("Info:\n"
           "  Size of   Text-Segment: %d\n"
           "  Size of roData-Segment: %d\n"
           "  Size of   Data-Segment: %d\n",(int)h->textseglen,(int)h->rodataseglen,(int)h->sdataseglen);
    printf("  Size of    bss-Segment: %d\n"
           "  Size of String-Segment: %d\n",(int)h->bssseglen,(int)h->stringseglen);
    printf("  Size of Symbol-Segment: %d (%d symbols)\n",(int)h->symbolseglen,(int)h->symbolseglen/sizeof(BYTECODE_SYMBOL));
  } else {
    if(o<=prglen) for(i=a;i<o;i++) puts(program[i]);
  }
}

char *plist_paramter(PARAMETER *p) {
  static char ergebnis[256];
  switch(p->typ) {
  case PL_EVAL:
  case PL_KEY:
    strcpy(ergebnis,p->pointer);
    break;
  case PL_LEER:
    *ergebnis=0;
    break;
  case PL_FLOAT:
  case PL_NUMBER:
    sprintf(ergebnis,"%g",p->real);
    break;
  case PL_INT:
    sprintf(ergebnis,"%d",p->integer);
    break;
  case PL_FILENR:
    sprintf(ergebnis,"#%d",p->integer);
    break;
  case PL_STRING:
    sprintf(ergebnis,"\"%s\"",(char *)p->pointer);
    break;
  case PL_LABEL:
    strcpy(ergebnis,labels[p->integer].name);
    break;
  case PL_IVAR:
    sprintf(ergebnis,"%s%%",variablen[p->integer].name);
    break;
  case PL_SVAR:
    sprintf(ergebnis,"%s$",variablen[p->integer].name);
    break;
  case PL_FVAR:
    strcpy(ergebnis,variablen[p->integer].name);
    break;
  case PL_NVAR:
  case PL_VAR:
  case PL_ALLVAR:
  case PL_ARRAYVAR:
    switch(variablen[p->integer].typ) {
    case INTTYP:
      sprintf(ergebnis,"%s%%",variablen[p->integer].name);
      break;
    case FLOATTYP:
      sprintf(ergebnis,"%s",variablen[p->integer].name);
      break;
    case STRINGTYP:
      sprintf(ergebnis,"%s$",variablen[p->integer].name);
      break;
    case ARRAYTYP:
      switch(p->arraytyp) {
      case INTTYP:
        sprintf(ergebnis,"%s%%()",variablen[p->integer].name);
        break;
      case FLOATTYP:
        sprintf(ergebnis,"%s()",variablen[p->integer].name);
        break;
      case STRINGTYP:
        sprintf(ergebnis,"%s$()",variablen[p->integer].name);
        break;
      default:
        sprintf(ergebnis,"%s?()",variablen[p->integer].name);
        break;
      }
      break;
    }
  default: 
    sprintf(ergebnis,"<ups$%x>",p->typ);
  }
  if(p->panzahl) {
    strcat(ergebnis,"(");
        char *buf;
	int i;
        for(i=0;i<p->panzahl;i++) {
          buf=plist_paramter(&(p->ppointer[i]));
          strcat(ergebnis,buf);
	  free(buf);
	  if(i<p->panzahl-1) strcat(ergebnis,",");
        }
    
    strcat(ergebnis,")");
  }
  return(strdup(ergebnis));
}

static char *plist_zeile(P_CODE *code) {
  char *ergebnis=malloc(MAXLINELEN);
  int i;
  sprintf(ergebnis,"=?=> %d",(int)code->opcode);
  switch(code->opcode) {
  case P_DEFFN: 
    sprintf(ergebnis,"DEFFN %s",procs[code->integer].name); 
    if(procs[code->integer].anzpar) { 
      strcat(ergebnis,"(");
      for(i=0;i<procs[code->integer].anzpar;i++) {
    	strcat(ergebnis,variablen[procs[code->integer].parameterliste[i]].name);
        if(i<procs[code->integer].anzpar-1) strcat(ergebnis,",");
      }
      strcat(ergebnis,")");
    }
    strcat(ergebnis," = ");
    strcat(ergebnis,code->argument); 
    break;
  case P_PROC:
    if(procs[code->integer].typ==PROC_PROC)
         sprintf(ergebnis,"PROCEDURE %s",procs[code->integer].name); 
    else sprintf(ergebnis,"FUNCTION %s",procs[code->integer].name); 
    if(procs[code->integer].anzpar) { 
      strcat(ergebnis,"(");
      for(i=0;i<procs[code->integer].anzpar;i++) {
    	strcat(ergebnis,variablen[procs[code->integer].parameterliste[i]].name);
        if(i<procs[code->integer].anzpar-1) strcat(ergebnis,",");
      }
      strcat(ergebnis,")");
    }
    break;
  case P_REM:
    sprintf(ergebnis,"' %s",code->argument);
    break;
  case P_LABEL:
    sprintf(ergebnis,"%s:",labels[code->integer].name);
    break;
  default:
    if(code->opcode&P_INVALID) sprintf(ergebnis,"==> invalid ==> %d",(int)code->opcode);
    else if(code->opcode==(P_IGNORE|P_NOCMD)) ergebnis[0]=0;
    else if((code->opcode)&P_EVAL) {
      sprintf(ergebnis,"eval ---> %s",code->argument);
    } else if((code->opcode&P_ZUWEIS)==P_ZUWEIS) {
      sprintf(ergebnis,"zuweis %d %s ---> %s",code->integer,variablen[code->integer].name,code->argument); 
    } else if((code->opcode&PM_COMMS)<anzcomms) {
      sprintf(ergebnis,"%s",comms[(code->opcode&PM_COMMS)].name);
      if((code->opcode&PM_SPECIAL)==P_ARGUMENT) strcat(ergebnis,code->argument);
      else if((code->opcode&PM_SPECIAL)==P_PLISTE) {
        if(code->panzahl) {
          char *buf;
          strcat(ergebnis," ");
          for(i=0;i<code->panzahl;i++) {
            buf=plist_paramter(&(code->ppointer[i]));
            strcat(ergebnis,buf);
	    free(buf);
	    if(i<code->panzahl-1) strcat(ergebnis,",");
          }
        }
      }
    }  
  }
 
  /*Anfuegungen an Zeile hinten*/
 
  if(code->etyp==PE_COMMENT) {
    strcat(ergebnis," !");
    strcat(ergebnis,(char *)code->extra);
  }
  return(ergebnis);
}
static int plist_printzeile(P_CODE *code, int level) {
  int j;
  char *zeile=plist_zeile(code);
  if(code->opcode & P_LEVELOUT) level--;
  for(j=0;j<level;j++) printf("  ");
  printf("%s\n",zeile);
  if(code->opcode & P_LEVELIN) level++;
  free(zeile);
  return(level);
}

static void c_plist(const char *n) {
  int i,f=0;
  for(i=0;i<prglen;i++) { 
    printf("%4d: $%06x |",i,(unsigned int)pcode[i].opcode);
  #if DEBUG
    printf(" (%5d) |",ptimes[i]);
  #endif
    printf(" %3d,%d |",pcode[i].integer,pcode[i].panzahl);
    f=plist_printzeile(&pcode[i],f);
  }
}


static void c_save(PARAMETER *plist, int e) { 
  if(programbufferlen) {
    char *name;
    if(e) name=plist[0].pointer;
    else name=ifilename;
    if(strlen(name)==0) name=ifilename;
    if(exist(name)) {
      char buf[100];
      sprintf(buf,"mv %s %s.bak",name,name);
      if(system(buf)==-1) io_error(errno,"system");
    }
    saveprg(name);
  }
}

static void c_merge(PARAMETER *plist, int e){
  if(exist(plist[0].pointer)) {
    if(programbufferlen==0) strcpy(ifilename,plist[0].pointer);
    mergeprg(plist[0].pointer);
  } else printf("LOAD/MERGE: Datei %s nicht gefunden !\n",(char *)plist[0].pointer);
}
static void c_load(PARAMETER *plist, int e) { 
  programbufferlen=prglen=pc=0;
  c_merge(plist,e); 
}
static void c_chain(PARAMETER *plist,int e){ c_load(plist,e); do_run(); }

static void c_let(const char *n) {  
    char v[strlen(n)+1],w[strlen(n)+1];
    wort_sep(n,'=',TRUE,v,w);
    xzuweis(v,w);
}


void c_quit(PARAMETER *plist, int e) { 
  int ecode=0;
  if(e) ecode=plist[0].integer; 
  quit_x11basic(ecode); 
}

/* Linearer Fit (regression) optional mit Fehlerbalken in x und y Richtung  */

static void c_fit_linear(const char *n) {  
  char w1[strlen(n)+1],w2[strlen(n)+1];                  
  int e,typ,scip=0,i=0,mtw=0;  
  int vnrx,vnry,vnre,vnre2,ndata; 
  double a,b,siga,sigb,chi2,q;
  char *r;
  e=wort_sep(n,',',TRUE,w1,w2);
  while(e) {
    scip=0;
    if(strlen(w1)) {
       switch(i) {
         case 0: { /* Array mit x-Werten */     
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnrx=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnrx==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	   } else puts("FIT: Kein ARRAY.");
	   break;
	   }
	 case 1: {   /* Array mit y-Werten */
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnry=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnry==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	   } else puts("FIT: Kein ARRAY.");
	   break;
	   } 
	 case 2: {   /* Array mit err-Werten */
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnre=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnre==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	     else mtw=1;
	   } else {scip=1; mtw=0;}
	   break;
	   } 
	 case 3: {   /* Array mit err-Werten */
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnre2=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnre2==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	     else mtw=2;
	   } else {scip=1;}
	   break;
	   } 
	 case 4: {
	   ndata=(int)parser(w1); 
	   if(vnrx!=-1 && vnry!=-1) {
             if(mtw==2 && vnre!=-1 && vnre2!=-1) {
	       linear_fit_exy((double *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE),
		   (double *)(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE),ndata,
		   (double *)(variablen[vnre].pointer.a->pointer+variablen[vnre].pointer.a->dimension*INTSIZE),
		   (double *)(variablen[vnre2].pointer.a->pointer+variablen[vnre2].pointer.a->dimension*INTSIZE),
		   &a,&b,&siga,&sigb,&chi2,&q); 

	     } else {
	       linear_fit((double *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE),
		   (double *)(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE),ndata,(mtw)?(
		   (double *)(variablen[vnre].pointer.a->pointer+variablen[vnre].pointer.a->dimension*INTSIZE)):NULL,mtw,&a,&b,&siga,&sigb,&chi2,&q); 
             }
	   }
	   break; 
	 } 
	 case 5: { zuweis(w1,a); break; } 
	 case 6: { zuweis(w1,b); break;} 
	 case 7: { zuweis(w1,siga); break;} 
	 case 8: { zuweis(w1,sigb);  break;} 
	 case 9: { zuweis(w1,chi2);  break;} 
	 case 10: { zuweis(w1,q);  break;} 
	   
         default: break;
       }
    }
    if(scip==0) e=wort_sep(w2,',',TRUE,w1,w2);
    i++;
  }
}

/* Sort-Funktion (wie qsort() ), welche ausserdem noch ein integer-Array mitsortiert */

static void do_sort(void *a, size_t n,size_t size,int(*compar)(const void *, const void *), int *b) {
 // printf("sort: n=%d size=%d\n",n,size);
  if (n<2) return;
  if(b==NULL) qsort(a,n,size,compar);
  else { 
    void *rra=malloc(size);
    unsigned long i,ir,j,l;
    int index;

    l=(n>>1)+1;
    ir=n;
    for(;;) {
      if(l>1) {
        memcpy(rra,a+size*(l-2),size);
        l--;
        index=b[l-1];
      } else {
        memcpy(rra,a+size*(ir-1),size);
        index=b[ir-1];
        memcpy(a+size*(ir-1),a+size*(1-1),size);
        b[ir-1]=b[1-1];
        if (--ir==1) {
          memcpy(a,rra,size);
          *b=index;
          break;
        }
      }
      i=l;j=l+l;
      while(j<=ir) {
        if(j<ir && compar(a+size*(j-1),a+size*j)<0) j++;
        if(compar(rra,a+size*(j-1))<0) {
	  memcpy(a+size*(i-1),a+size*(j-1),size); 
	  b[i-1]=b[j-1];
	  i=j;
	  j<<=1;
        } else j=ir+1;
      }
      memcpy(a+size*(i-1),rra,size);
      b[i-1]=index;
    }
    free(rra);
  }
}


/*The sort functions for all variable types */
static int cmpstring(const void *p1, const void *p2) {
 // printf("cmpstring\n");
  return(memcmp(((STRING *)p1)->pointer,((STRING *)p2)->pointer,min(((STRING *)p1)->len,((STRING *)p2)->len)));
}
static int cmpdouble(const void *p1, const void *p2) {
  if(*(double *)p1==*(double *)p2) return(0);
  else if(*(double *)p1>*(double *)p2) return(1);
  else return(-1);
}
static int cmpint(const void *p1, const void *p2) {
  if((*(int *)p1)==(*(int *)p2)) return(0);
  else if((*(int *)p1)>(*(int *)p2)) return(1);
  else return(-1);
}

/* Sortierfunktion fuer ARRAYS 

Todo: 
* Umstellen auf pliste.
* Stringsortierung bei unterschiedlicher Laenge ist nicht optimal.
* Indexarray muss INTARRAYTYP sein. Das geht auch flexibler! (mit allarray)

*/


static void c_sort(PARAMETER *plist,int e) {  
  int subtyp;
  int vnrx,vnry=-1,ndata=0; 

  vnrx=plist->integer;
  ndata=anz_eintraege(variablen[vnrx].pointer.a);

  if(e>=2) ndata=plist[1].integer;
  if(e>=3) vnry=plist[2].integer;
  // int typ=variablen[vnrx].typ;
  subtyp=variablen[vnrx].pointer.a->typ;
  
 //  printf("c_sort vnr=%d ndata=%d vnry=%d\n",vnrx,ndata,vnry);


  if(subtyp==STRINGTYP) 
    do_sort((void *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE)
      ,ndata,sizeof(STRING),cmpstring,
      (int *)((vnry!=-1)?(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE):NULL));      
  else if(subtyp==INTTYP) 
      do_sort((void *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE)
      ,ndata,sizeof(int),cmpint,
      (int *)((vnry!=-1)?(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE):NULL));      
  else if(subtyp==FLOATTYP)  
      do_sort((void *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE)
      ,ndata,sizeof(double),cmpdouble,
      (int *)((vnry!=-1)?(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE):NULL));
}

/* Allgemeine Fit-Funktion  mit Fehlerbalken in y Richtung  */

static void c_fit(const char *n) {  
  char w1[strlen(n)+1],w2[strlen(n)+1];                  
  int e,typ,scip=0,i=0,mtw=0;  
  int vnrx,vnry,vnre,vnre2,ndata; 
  double a,b,siga,sigb,chi2,q;
  char *r;
  e=wort_sep(n,',',TRUE,w1,w2);
  xberror(9,"FIT"); /* Funktion noch nicht moeglich */
  while(e) {
    scip=0;
    if(strlen(w1)) {
       switch(i) {
         case 0: { /* Array mit x-Werten */     
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnrx=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnrx==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	   } else puts("FIT: Kein ARRAY.");
	   break;
	   }
	 case 1: {   /* Array mit y-Werten */
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnry=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnry==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	   } else puts("FIT: no ARRAY.");
	   break;
	   } 
	 case 2: {   /* Array mit err-Werten */
	   /* Typ bestimmem. Ist es Array ? */
           typ=type(w1)&(~CONSTTYP);
	   if(typ & ARRAYTYP) {
             r=varrumpf(w1);
             vnre=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
             free(r);
	     if(vnre==-1) xberror(15,w1); /* Feld nicht dimensioniert */
	     else mtw=1;
	   } else {scip=1; mtw=0;}
	   break;
	   } 
	 case 4: 
	   ndata=(int)parser(w1); 
           break;
	 case 5: {   /* Funktion mit Parameterliste */
	   }
	   break;
	 case 6: {   /* Ausdruck, der Angibt, welche Parameter zu fitten sind */  	 
	   if(vnrx!=-1 && vnry!=-1) {
             if(mtw==2 && vnre!=-1 && vnre2!=-1) {
	       linear_fit_exy((double *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE),
		   (double *)(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE),ndata,
		   (double *)(variablen[vnre].pointer.a->pointer+variablen[vnre].pointer.a->dimension*INTSIZE),
		   (double *)(variablen[vnre2].pointer.a->pointer+variablen[vnre2].pointer.a->dimension*INTSIZE),
		   &a,&b,&siga,&sigb,&chi2,&q); 

	     } else {
	       linear_fit((double *)(variablen[vnrx].pointer.a->pointer+variablen[vnrx].pointer.a->dimension*INTSIZE),
		   (double *)(variablen[vnry].pointer.a->pointer+variablen[vnry].pointer.a->dimension*INTSIZE),ndata,(vnre!=-1)?(
		   (double *)(variablen[vnre].pointer.a->pointer+variablen[vnre].pointer.a->dimension*INTSIZE)):NULL,mtw,&a,&b,&siga,&sigb,&chi2,&q); 
             }
	   }
	   break; 
	 } 
	 case 7: { zuweis(w1,chi2); break; } 
	 case 8: { zuweis(w1,b); break;} 
	 case 9: { zuweis(w1,siga); break;} 
	 case 10: { zuweis(w1,sigb);  break;} 
	 case 11: { zuweis(w1,chi2);  break;} 
	 case 12: { zuweis(w1,q);  break;}	   
         default: break;
       }
    }
    if(scip==0) e=wort_sep(w2,',',TRUE,w1,w2);
    i++;
  }
}

static void c_fft(const char *n) {
  char v[strlen(n)+1],w[strlen(n)+1];
  int isign=1;
  int e=wort_sep(n,',',TRUE,v,w);
  if(e>=1) {
    int typ,vnr;
    char *r;
     /* Typ bestimmem. Ist es Array ? */
 
    typ=type(v)&(~CONSTTYP);
    if(typ & ARRAYTYP) {
      r=varrumpf(v);
      vnr=var_exist(r,ARRAYTYP,typ&(~ARRAYTYP),0);
      free(r);
      if(vnr==-1) xberror(15,v); /* Feld nicht dimensioniert */ 
      else {
        if(typ & FLOATTYP) {
	  int nn=do_dimension(&variablen[vnr]);
	  double *varptr=(double  *)(variablen[vnr].pointer.a->pointer+variablen[vnr].pointer.a->dimension*INTSIZE);

	  if(e==2) isign=(int)parser(w);
	  realft(varptr,(nn-1)/2,isign);
        } else xberror(94,v); /* Parameter must be float Array */
      }
    } else xberror(95,v); /* Parameter must be Array */
  } else xberror(32,"FFT"); /* Syntax error */
}


static void c_arraycopy(PARAMETER *plist,int e) {
  int vnr1=plist[0].integer;
  int vnr2=plist[1].integer;
  ARRAY *arr1=variablen[vnr1].pointer.a;
  ARRAY *arr2=variablen[vnr2].pointer.a;
  ARRAY a;
  switch(arr1->typ) {
  case INTTYP:
      if(arr2->typ==INTTYP) a=double_array(arr2);
      else if(arr2->typ==FLOATTYP) a=convert_to_intarray(arr2);
      else xberror(96,variablen[vnr2].name); /* Array has wrong type */
      break;
  case FLOATTYP:
      if(arr2->typ==FLOATTYP) a=double_array(arr2);
      else if(arr2->typ==INTTYP) a=convert_to_floatarray(arr2);
      else xberror(96,variablen[vnr2].name); /* Array has wrong type */
      break;
  case STRINGTYP:
      if(arr2->typ==STRINGTYP) a=double_array(arr2);
      else xberror(96,variablen[vnr2].name); /* Array has wrong type */
      break;
  default:
    printf("ERROR: arraycopy : typ? $%x \n",variablen[vnr1].typ);
  }
  free_array(arr1);
  *arr1=a;
}

static void c_arrayfill(PARAMETER *plist,int e) {
  int vnr=plist[0].integer;
//  printf("ARRAYFILL: vnr=%d\n",vnr);
  ARRAY *arr=variablen[vnr].pointer.a;
  ARRAY a;
  
  switch(arr->typ) {
  case INTTYP:
    if(plist[1].typ==PL_FLOAT) plist[1].integer=(int)plist[1].real;
    else if(plist[1].typ!=PL_INT) xberror(96,variablen[vnr].name); /* Array has wrong type */
    a=create_int_array(arr->dimension,arr->pointer,plist[1].integer);
    break;
  case FLOATTYP:
    if(plist[1].typ==PL_INT) plist[1].real=(double)plist[1].integer;
    else if(plist[1].typ!=PL_FLOAT) xberror(96,variablen[vnr].name); /* Array has wrong type */
    a=create_float_array(arr->dimension,arr->pointer,plist[1].real);
    break;
  case STRINGTYP:
    if(plist[1].typ!=PL_STRING) xberror(96,variablen[vnr].name); /* Array has wrong type */
    a=create_string_array(arr->dimension,arr->pointer,(STRING *)&(plist[1].integer));
    break;  
  default:
    xberror(95,variablen[vnr].name); /* Parameter must be Array */
  }
  free_array(arr);
  *arr=a;
}
static void c_memdump(PARAMETER *plist,int e) {
  memdump((unsigned char *)plist[0].integer,plist[1].integer);
}

static char *varinfo(VARIABLE *v) {
  static char info[128];
  char *buf;
  char a;
  int i=0;
  switch(v->typ) {
    case INTTYP:   sprintf(info,"%s%%=%d",v->name,*(v->pointer.i));break;
    case FLOATTYP: sprintf(info,"%s=%.13g",v->name,*(v->pointer.f)); break;
    case STRINGTYP:
      buf=malloc(v->pointer.s->len+1);
      while(i<v->pointer.s->len && i<80) {
        a=(v->pointer.s->pointer)[i];
        if(isprint(a)) buf[i]=a;
	else buf[i]='.';
        i++;
      } 
      buf[i]=0;
      sprintf(info,"%s$=\"%s\" (len=%d)",v->name,buf,v->pointer.s->len);
      free(buf);
      break;
    default:
      sprintf(info,"?_var_?=?_? ");
  }
  return(info);
}

void c_dump(PARAMETER *plist,int e) {
  int i;
  char kkk=0;
  
  if(e) kkk=((char *)plist[0].pointer)[0];

  if(kkk==0 || kkk=='%') {/*  dump ints */
    for(i=0;i<anzvariablen;i++) {
      if(variablen[i].typ==INTTYP) 
        printf("%02d: %s\n",i,varinfo(&variablen[i]));
    }
  }
  if(kkk==0) {
    for(i=0;i<anzvariablen;i++) {/*  dump floats */
      if(variablen[i].typ==FLOATTYP) 
        printf("%02d: %s\n",i,varinfo(&variablen[i]));
    }
  }
  if(kkk==0 || kkk=='$') {/*  dump strings */
    for(i=0;i<anzvariablen;i++) {
      if(variablen[i].typ==STRINGTYP) {
        printf("%02d: %s\n",i,varinfo(&variablen[i]));
      }
    }
  }
  if(kkk==0 || kkk=='%' || kkk=='(') {/*  dump int arrays */
    for(i=0;i<anzvariablen;i++) {
      if(variablen[i].typ==ARRAYTYP && variablen[i].pointer.a->typ==INTTYP) {
        int j;
        printf("%02d: %s%%(",i,variablen[i].name);
        for(j=0;j<variablen[i].pointer.a->dimension;j++) {
          if(j>0) printf(",%d",((int *)variablen[i].pointer.a->pointer)[j]);
	  else  printf("%d",((int *)variablen[i].pointer.a->pointer)[j]);
        }
        puts(")");
      }
    }
  }
  
  if(kkk==0 || kkk=='(') {/*  dump arrays */
    for(i=0;i<anzvariablen;i++) {
      if(variablen[i].typ==ARRAYTYP && variablen[i].pointer.a->typ==FLOATTYP) {
        int j;
        printf("%02d: %s(",i,variablen[i].name);
        for(j=0;j<variablen[i].pointer.a->dimension;j++) {
          if(j>0) printf(",%d",((int *)variablen[i].pointer.a->pointer)[j]);
  	  else  printf("%d",((int *)variablen[i].pointer.a->pointer)[j]);
        }
        printf(")  [%d]\n",variablen[i].local);
      }
    }
  }
  if(kkk==0 || kkk=='$' || kkk=='(') {/*  dump string arrays */
    for(i=0;i<anzvariablen;i++) {
      if(variablen[i].typ==ARRAYTYP && variablen[i].pointer.a->typ==STRINGTYP) {
        int j;
        printf("%02d: %s$(",i,variablen[i].name);
        for(j=0;j<variablen[i].pointer.a->dimension;j++) {
          if(j>0) printf(",%d",((int *)variablen[i].pointer.a->pointer)[j]);
  	  else  printf("%d",((int *)variablen[i].pointer.a->pointer)[j]);
        }
        puts(")");
      }
    }
  }
  if(kkk==':') {/*  dump Labels */
    for(i=0;i<anzlabels;i++) {
      printf("%s: [%d]\n",labels[i].name,labels[i].zeile);
    }
  }
  if(kkk=='@') {/*  dump Procedures */
    for(i=0;i<anzprocs;i++) {
      printf("%d|%s [%d]\n",procs[i].typ,procs[i].name,procs[i].zeile);
    }
  }
  if(kkk=='#') {                   /*  dump Channels */
    for(i=0;i<ANZFILENR;i++) {
      if(filenr[i]==1) {
        printf("#%d: %s [FILE]\n",i,"");
      } else if(filenr[i]==2) {
        printf("#%d: %s [SHARED OBJECT]\n",i,"");
      }
    }
  }
  if(kkk=='C' || kkk=='K'|| kkk=='c'|| kkk=='k') { /*  dump commands */
    int j;
    for(i=0;i<anzcomms;i++) {
      printf("%3d: [%08x] %s ",i,(unsigned int)comms[i].opcode,comms[i].name);  
      if(comms[i].pmin) {
        for(j=0;j<comms[i].pmin;j++) {
	  switch(comms[i].pliste[j]) {
	    case PL_INT: printf("i%%"); break;
	    case PL_FILENR: printf("#n"); break;
	    case PL_STRING: printf("t$"); break;
	    case PL_NUMBER: printf("num"); break;
	    case PL_SVAR: printf("var$"); break;
	    case PL_NVAR: printf("var"); break;
	    case PL_KEY: printf("KEY"); break;
	    default: printf("???");
	  }
	  if(j<comms[i].pmin-1) printf(",");
	}
      }
      if(comms[i].pmax>comms[i].pmin || comms[i].pmax==-1) printf("[,");
      if(comms[i].pmax==-1) printf("...");
      else {
      for(j=comms[i].pmin;j<comms[i].pmax;j++) {
	  switch(comms[i].pliste[j]) {
	    case PL_INT: printf("i%%"); break;
	    case PL_FILENR: printf("#n"); break;
	    case PL_STRING: printf("t$"); break;
	    case PL_NUMBER: printf("num"); break;
	    case PL_SVAR: printf("var$"); break;
	    case PL_NVAR: printf("var"); break;
	    case PL_KEY: printf("KEY"); break;
	    default: printf("???");
	  }
	  if(j<comms[i].pmax-1) printf(",");
	}
      }
      if(comms[i].pmax>comms[i].pmin || comms[i].pmax==-1) printf("]");
      printf("\n");
    }
  }
  if(kkk=='F' || kkk=='f') { /*  dump functions */
    int j;
    for(i=0;i<anzpfuncs;i++) {
      printf("%3d: [%08x] %s(",i,(unsigned int) pfuncs[i].opcode,pfuncs[i].name);  
      if(pfuncs[i].pmin) {
        for(j=0;j<pfuncs[i].pmin;j++) {
	  switch(pfuncs[i].pliste[j]) {
	    case PL_INT: printf("i%%"); break;
	    case PL_FILENR: printf("#n"); break;
	    case PL_STRING: printf("t$"); break;
	    case PL_NUMBER: printf("num"); break;
	    case PL_SVAR: printf("var$"); break;
	    case PL_NVAR: printf("var"); break;
	    case PL_KEY: printf("KEY"); break;
	    default: printf("???");
	  }
	  if(j<pfuncs[i].pmin-1) printf(",");
	}
      }
      if(pfuncs[i].pmax>pfuncs[i].pmin || pfuncs[i].pmax==-1) printf("[,");
      if(pfuncs[i].pmax==-1) printf("...");
      else {
      for(j=pfuncs[i].pmin;j<pfuncs[i].pmax;j++) {
	  switch(pfuncs[i].pliste[j]) {
	    case PL_INT: printf("i%%"); break;
	    case PL_FILENR: printf("#n"); break;
	    case PL_STRING: printf("t$"); break;
	    case PL_NUMBER: printf("num"); break;
	    case PL_SVAR: printf("var$"); break;
	    case PL_NVAR: printf("var"); break;
	    case PL_KEY: printf("KEY"); break;
	    default: printf("???");
	  }
	  if(j<pfuncs[i].pmax-1) printf(",");
	}
      }
      if(pfuncs[i].pmax>pfuncs[i].pmin || pfuncs[i].pmax==-1) printf("]");
      printf(")\n");
    }    
    for(i=0;i<anzpsfuncs;i++) {
      printf("%3d: [%08x] %s(",i,(unsigned int)psfuncs[i].opcode,psfuncs[i].name);  
      if(psfuncs[i].pmin) {
        for(j=0;j<psfuncs[i].pmin;j++) {
	  switch(psfuncs[i].pliste[j]) {
	    case PL_INT: printf("i%%"); break;
	    case PL_FILENR: printf("#n"); break;
	    case PL_STRING: printf("t$"); break;
	    case PL_NUMBER: printf("num"); break;
	    case PL_SVAR: printf("var$"); break;
	    case PL_NVAR: printf("var"); break;
	    case PL_KEY: printf("KEY"); break;
	    default: printf("???");
	  }
	  if(j<psfuncs[i].pmin-1) printf(",");
	}
      }
      if(psfuncs[i].pmax>psfuncs[i].pmin || psfuncs[i].pmax==-1) printf("[,");
      if(psfuncs[i].pmax==-1) printf("...");
      else {
      for(j=psfuncs[i].pmin;j<psfuncs[i].pmax;j++) {
	  switch(psfuncs[i].pliste[j]) {
	    case PL_INT: printf("i%%"); break;
	    case PL_FILENR: printf("#n"); break;
	    case PL_STRING: printf("t$"); break;
	    case PL_NUMBER: printf("num"); break;
	    case PL_SVAR: printf("var$"); break;
	    case PL_NVAR: printf("var"); break;
	    case PL_KEY: printf("KEY"); break;
	    default: printf("???");
	  }
	  if(j<psfuncs[i].pmax-1) printf(",");
	}
      }
      if(psfuncs[i].pmax>psfuncs[i].pmin || psfuncs[i].pmax==-1) printf("]");
      printf(")\n");
    }
  }
}

static void c_end(const char *n) { batch=0; }

static void c_on(const char *n) {
  char w1[strlen(n)+1],w2[strlen(n)+1],w3[strlen(n)+1];
  int e=wort_sep(n,' ',TRUE,w1,w2);
  int mode=0;
  if(e==0) xberror(32,"ON"); /* Syntax error */
  else {
    wort_sep(w2,' ',TRUE,w2,w3);
    if(strcmp(w2,"CONT")==0) mode=1;
    else if(strcmp(w2,"GOTO")==0) mode=2;
    else if(strcmp(w2,"GOSUB")==0) mode=3;
    else mode=0;
    
    if(strcmp(w1,"ERROR")==0) {
      errcont=(mode>0);
      if(mode==2) errorpc=labelzeile(w3);
      else if(mode==3) {
        errorpc=procnr(w3,1);
	if(errorpc!=-1) errorpc=procs[errorpc].zeile;      
      }
    } else if(strcmp(w1,"BREAK")==0) {
      breakcont=(mode>0);
      if(mode==2) breakpc=labelzeile(w3);
      else if(mode==3) {
        breakpc=procnr(w3,1);
	if(breakpc!=-1) breakpc=procs[breakpc].zeile;
      }
#ifndef NOGRAPHICS 
    } else if(strcmp(w1,"MENU")==0) {
      if(mode==0)  c_menu("");  
      else if(mode==3) {
       int pc2=procnr(w3,1);
       if(pc2==-1) xberror(19,w3); /* Procedure nicht gefunden */
       else menuaction=pc2;
      } else  printf("Unbekannter Befehl: ON <%s> <%s>\n",w1,w2);  
#endif
    } else { /* on n goto ...  */
      if(mode<2) printf("Unbekannter Befehl: ON <%s> <%s>\n",w1,w2);
      else {
        int gi=max(0,(int)parser(w1));
	if(gi) {
	  while(gi) { e=wort_sep(w3,',',TRUE,w2,w3); gi--;}
	  if(e) {
            if(mode==3) c_gosub(w2);
	    else if(mode==2) c_goto(w2);
          }
	}
      }
    }
  }
}

static void c_add(PARAMETER *plist,int e) {
  int vnr=plist[0].integer;
  char *varptr=plist[0].pointer;
  int typ=variablen[vnr].typ;
  if(typ==ARRAYTYP) typ=variablen[vnr].pointer.a->typ;
  switch(typ) {
  case INTTYP:
    if(plist[1].typ==PL_INT) {
      *((int *)varptr)+=plist[1].integer;
    } else if(plist[1].typ==PL_FLOAT) {
      *((int *)varptr)+=(int)plist[1].real;    
    } else printf("ADD. Argument wrong, must be flt.\n");
    break;
  case FLOATTYP:
    if(plist[1].typ==PL_FLOAT) {
      *((double *)varptr)+=plist[1].real;
    } else if(plist[1].typ==PL_INT) {
      *((double *)varptr)+=(double)plist[1].integer;    
    } else printf("ADD. Argument wrong, must be flt.\n");
    break;
  case STRINGTYP:
    if(plist[1].typ==PL_STRING) {
      STRING *s=(STRING *)varptr;
      s->pointer=realloc(s->pointer,s->len+plist[1].integer+1);
      memcpy(s->pointer+s->len,plist[1].pointer,plist[1].integer);
      s->len+=plist[1].integer;
      (s->pointer)[s->len]=0;
    } else printf("ADD. Argument wrong, must be string.\n");
    break;
  default:
    printf("ADD: typ unbek. $%x\n",typ);
    xberror(32,""); /* Syntax error */
  }
}
static void c_sub(PARAMETER *plist,int e) {
  int vnr=plist[0].integer;
  char *varptr=plist[0].pointer;
  int typ=variablen[vnr].typ;
  if(typ==ARRAYTYP) typ=variablen[vnr].pointer.a->typ;
//  printf("SUB: vnr=%d\n",vnr);
//  dump_parameterlist(plist,e);
  switch(typ) {
  case INTTYP:
    if(plist[1].typ==PL_INT) {
      *((int *)varptr)-=plist[1].integer;
    } else if(plist[1].typ==PL_FLOAT || plist[1].typ==PL_NUMBER) {
      *((int *)varptr)-=(int)plist[1].real;    
    } else printf("SUB. Argument wrong, must be flt.\n");
    break;
  case FLOATTYP:
    if(plist[1].typ==PL_FLOAT || plist[1].typ==PL_NUMBER) {
      *((double *)varptr)-=plist[1].real;
    } else if(plist[1].typ==PL_INT) {
      *((double *)varptr)-=(double)plist[1].integer;    
    } else printf("SUB. Argument wrong, must be flt.\n");
    break;
  default:
    xberror(32,""); /* Syntax error */
  }
}
static void c_mul(PARAMETER *plist,int e) {
  int vnr=plist[0].integer;
  char *varptr=plist[0].pointer;
  int typ=variablen[vnr].typ;
  if(typ==ARRAYTYP) typ=variablen[vnr].pointer.a->typ;
//  printf("MUL: vnr=%d\n",vnr);
//  dump_parameterlist(plist,e);
  switch(typ) {
  case INTTYP:
    if(plist[1].typ==PL_INT) {
      *((int *)varptr)*=plist[1].integer;
    } else if(plist[1].typ==PL_FLOAT || plist[1].typ==PL_NUMBER) {
      *((int *)varptr)*=(int)plist[1].real;    
    } else printf("MUL. Argument wrong, must be flt.\n");
    break;
  case FLOATTYP:
    if(plist[1].typ==PL_FLOAT || plist[1].typ==PL_NUMBER) {
      *((double *)varptr)*=plist[1].real;
    } else if(plist[1].typ==PL_INT) {
      *((double *)varptr)*=(double)plist[1].integer;    
    } else printf("MUL. Argument wrong, must be flt.\n");
    break;
  default:
    xberror(32,""); /* Syntax error */
  }
}
static void c_div(PARAMETER *plist,int e) {
  int vnr=plist[0].integer;
  char *varptr=plist[0].pointer;
  int typ=variablen[vnr].typ;
  if(typ==ARRAYTYP) typ=variablen[vnr].pointer.a->typ;
//  printf("DIV: vnr=%d\n",vnr);
//  dump_parameterlist(plist,e);
  switch(typ) {
  case INTTYP:
    if(plist[1].typ==PL_INT) {
      *((int *)varptr)/=plist[1].integer;
    } else if(plist[1].typ==PL_FLOAT || plist[1].typ==PL_NUMBER) {
      *((int *)varptr)/=(int)plist[1].real;    
    } else printf("DIV. Argument wrong, must be flt.\n");
    break;
  case FLOATTYP:
    if(plist[1].typ==PL_FLOAT || plist[1].typ==PL_NUMBER) {
      *((double *)varptr)/=plist[1].real;
    } else if(plist[1].typ==PL_INT) {
      *((double *)varptr)/=(double)plist[1].integer;    
    } else printf("DIV. Argument wrong, must be flt.\n");
    break;
  default:
    xberror(32,""); /* Syntax error */
  }
}

static void c_swap(PARAMETER *plist,int e) {
  int vnr1=plist[0].integer;
  int vnr2=plist[1].integer;
  if(vnr1==vnr2 && plist[0].pointer==plist[1].pointer) return; // nix zu tun
  if(plist[0].typ!=plist[1].typ) {
     xberror(58,""); /* Variable %s ist vom falschen Typ */
     return;
   }
   /* Die p->pointer enthalten das ergebnis von varptr_indexliste() */
   switch(plist->typ) {
   case PL_IVAR:
     { int tmp=*(int *)plist[1].pointer;
       *(int *)plist[1].pointer=*(int *)plist[0].pointer;
       *(int *)plist[0].pointer=tmp;
     }
     break;
   case PL_FVAR:
     { double tmp=*(double *)plist[1].pointer;
       *(double *)plist[1].pointer=*(double *)plist[0].pointer;
       *(double *)plist[0].pointer=tmp;
     }
     break;
   case PL_SVAR:
     { STRING tmp=*(STRING *)plist[1].pointer;
       *(STRING *)plist[1].pointer=*(STRING *)plist[0].pointer;
       *(STRING *)plist[0].pointer=tmp;
     }
     break;
   case PL_FARRAYVAR:
   case PL_IARRAYVAR:
   case PL_SARRAYVAR:
     { ARRAY tmp=*(ARRAY *)plist[1].pointer;
       *(ARRAY *)plist[1].pointer=*(ARRAY *)plist[0].pointer;
       *(ARRAY *)plist[0].pointer=tmp;
     }
     break;
   default:
     printf("SWAP: vnr=%d %d ",vnr1,vnr2);
     printf("pointer=%p %p ",plist[0].pointer,plist[1].pointer);
     printf("Typ=%x\n",plist->typ);
     xberror(58,""); /* Variable %s ist vom falschen Typ */
   }
} 

static void c_do(const char *n) {   /* wird normalerweise ignoriert */
  if(*n==0) ; 
  else if(strncmp(n,"WHILE",5)==0) c_while(n);
  else if(strncmp(n,"UNTIL",5)==0) ;
  else xberror(32,n); /*Syntax nicht korrekt*/
}

static void c_dim(PARAMETER *plist,int e) {
  int i;
  for(i=0;i<e;i++) {
      switch(plist[i].typ) {
      case PL_EVAL:
      //  printf("arg: %s \n",(char *)plist[i].pointer);
	dodim(plist[i].pointer);
        break;
      default: 
        dump_parameterlist(plist,e);
        xberror(32,"DIM"); /* Syntax error */
	return;
      }
  }
}
static void c_erase(PARAMETER *plist,int e) {
  while(e) erase_variable(&variablen[plist[--e].integer]);
}


static void c_return(const char *n) {
  if(sp>0) {
    if(n && strlen(n)) {
      if(type(n) & STRINGTYP) {
        returnvalue.str=string_parser(n); /* eigentlich muss auch der Funktionstyp 
                                       	 abgefragt werden */
      } else returnvalue.f=parser(n);
    }
    restore_locals(sp);
    pc=stack[--sp];
  } else xberror(93,""); /*Stack-Error !*/
}

void c_void(const char *n) { 
  if(type(n) & STRINGTYP) {
    char *erg=s_parser(n);
    free(erg);
  } else parser(n);
}
static void c_nop(const char *n) { return; }

static void c_inc(PARAMETER *plist,int e) {
  int typ=variablen[plist->integer].typ;
  if(typ==ARRAYTYP) typ=variablen[plist->integer].pointer.a->typ;
  if(typ&FLOATTYP) (*((double *)plist->pointer))++;
  else if(typ&INTTYP) (*((int *)plist->pointer))++;
//    else printf("INC seltsam. $%x\n",variablen[plist->integer].typ);
}

static void c_dec(PARAMETER *plist,int e) { 
  int typ=variablen[plist->integer].typ;
  if(typ==ARRAYTYP)   typ=variablen[plist->integer].pointer.a->typ;
  if(typ&FLOATTYP)    (*((double *)plist->pointer))--;
  else if(typ&INTTYP) (*((int *)plist->pointer))--;
}
static void c_cls(const char *n) { 
#ifdef WINDOWS
  DWORD written; /* number of chars actually written */
  COORD coord; /* coordinates to start writing */
  CONSOLE_SCREEN_BUFFER_INFO coninfo; /* receives console size */ 
  HANDLE ConsoleOutput; /* handle for console output */
  ConsoleOutput=GetStdHandle(STD_OUTPUT_HANDLE); 
  GetConsoleScreenBufferInfo(ConsoleOutput,&coninfo);
#define  COLS coninfo.dwSize.X
#define  LINES coninfo.dwSize.Y
  coord.X=0;
  coord.Y=0;
  FillConsoleOutputCharacter(ConsoleOutput,' ',LINES*COLS,coord,&written);
  FillConsoleOutputAttribute(ConsoleOutput,
    FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE,LINES*COLS,
    coord,&written);

  SetConsoleCursorPosition(ConsoleOutput,coord);
#else
  printf("\033[2J\033[H");
#endif
}
static void c_home(const char *n) { 
#ifdef WINDOWS
  COORD coord;
  HANDLE ConsoleOutput; /* handle for console output */
  ConsoleOutput=GetStdHandle(STD_OUTPUT_HANDLE); 
  coord.X=0;
  coord.Y=0;
  SetConsoleCursorPosition(ConsoleOutput,coord);
#else
  printf("\033[H");
#endif
}
static void c_version(const char *n) { printf("X11-BASIC Version: %s %s\n",version,vdate);}

#ifndef WINDOWS
#include <fnmatch.h>
#else
#include "Windows.extension/fnmatch.h"
#endif
static void c_help(PARAMETER *plist,int e) {
  if(e==0) puts("HELP [topic]");
  else do_help(plist[0].pointer);
}  
void do_help(const char *w) {
    int j,i;
    for(i=0;i<anzcomms;i++) {
    
      if(fnmatch(w,comms[i].name,FNM_NOESCAPE)==0) {
        printf("%s ",comms[i].name);  
        if(comms[i].pmin) {
          for(j=0;j<comms[i].pmin;j++) {
	    switch(comms[i].pliste[j]) {
	      case PL_INT: printf("i%%"); break;
	      case PL_FILENR: printf("#n"); break;
	      case PL_STRING: printf("t$"); break;
	      case PL_NUMBER: printf("num"); break;
	      case PL_SVAR: printf("var$"); break;
	      case PL_IVAR: printf("var%%"); break;
	      case PL_NVAR: printf("var"); break;
	      case PL_KEY: printf("KEY"); break;
	      case PL_FARRAY: printf("a()"); break;
	      case PL_IARRAY: printf("h%%()"); break;
	      case PL_SARRAY: printf("f$()"); break;
	      case PL_LABEL: printf("<label>"); break;
	      case PL_PROC: printf("<procedure>"); break;
	      case PL_FUNC: printf("<function>"); break;
	      default: printf("???");
	    }
	    if(j<comms[i].pmin-1) printf(",");
	  }
        }
        if(comms[i].pmax>comms[i].pmin || comms[i].pmax==-1) printf("[,");
        if(comms[i].pmax==-1) printf("...");
        else {
        for(j=comms[i].pmin;j<comms[i].pmax;j++) {
	    switch(comms[i].pliste[j]) {
	      case PL_INT: printf("i%%"); break;
	      case PL_FILENR: printf("#n"); break;
	      case PL_STRING: printf("t$"); break;
	      case PL_NUMBER: printf("num"); break;
	      case PL_SVAR: printf("var$"); break;
	      case PL_NVAR: printf("var"); break;
	      case PL_IVAR: printf("var%%"); break;
	      case PL_KEY: printf("KEY"); break;
	      case PL_FARRAY: printf("a()"); break;
	      case PL_IARRAY: printf("h%%()"); break;
	      case PL_SARRAY: printf("f$()"); break;
	      case PL_LABEL: printf("<label>"); break;
	      case PL_PROC: printf("<procedure>"); break;
	      case PL_FUNC: printf("<function>"); break;
	      default: printf("???");
	    }
	    if(j<comms[i].pmax-1) printf(",");
	  }
        }
        if(comms[i].pmax>comms[i].pmin || comms[i].pmax==-1) printf("]");
        puts("");
      }
    }
    for(i=0;i<anzpfuncs;i++) {
      if(fnmatch(w,pfuncs[i].name,FNM_NOESCAPE)==0) {
        printf("%s(",pfuncs[i].name);
	if(pfuncs[i].pmin) printf("%d",pfuncs[i].pmin);
	if(pfuncs[i].pmax-pfuncs[i].pmin) printf("-%d",pfuncs[i].pmax);
        puts(")");
      }
    }
     for(i=0;i<anzpsfuncs;i++) {
      if(fnmatch(w,psfuncs[i].name,FNM_NOESCAPE)==0) {
        printf("%s(",psfuncs[i].name);  
	if(psfuncs[i].pmin) printf("%d",psfuncs[i].pmin);
	if(psfuncs[i].pmax-psfuncs[i].pmin) printf("-%d",psfuncs[i].pmax);
        puts(")");
      }
    }
     for(i=0;i<anzsysvars;i++) {
      if(fnmatch(w,sysvars[i].name,FNM_NOESCAPE)==0) {
        if(sysvars[i].opcode&INTTYP) printf("int ");
	else if(sysvars[i].opcode&FLOATTYP) printf("flt ");
	else printf("??? ");
        printf("%s\n",sysvars[i].name);          
      }
    }
      for(i=0;i<anzsyssvars;i++) {
      if(fnmatch(w,syssvars[i].name,FNM_NOESCAPE)==0) {
        printf("%s\n",syssvars[i].name);          
      }
    }
}
static void c_error(PARAMETER *plist,int e) {xberror(plist[0].integer,"");}
static void c_free(PARAMETER *plist,int e)  {free((char *)plist[0].integer);}
static void c_detatch(PARAMETER *plist,int e) {
  int r=shm_detatch(plist->integer);
  if(r!=0) io_error(r,"DETATCH");
}
static void c_shm_free(PARAMETER *plist,int e) {shm_free(plist->integer);}
static void c_pause(PARAMETER *plist,int e) {
#ifdef WINDOWS
  Sleep((int)(1000*plist[0].real));
#else
  double zeit=plist[0].real;
  int i=(int)zeit;
  if(i) sleep(i);
  zeit=zeit-(double)i;
  if(zeit>0) usleep((int)(1000000*zeit));
#endif
}

static void c_echo(PARAMETER *plist,int e) {
  char *n=plist->pointer;
  if(strcmp(n,"ON")==0) echoflag=TRUE; 
  else if(strcmp(n,"OFF")==0) echoflag=FALSE;
  else  echoflag=(int)parser(n);
}
static void c_gps(PARAMETER *plist,int e) {
#ifdef ANDROID
  char *n=plist->pointer;
  if(strcmp(n,"ON")==0) do_gpsonoff(1);  
  else if(strcmp(n,"OFF")==0) do_gpsonoff(0);
  else do_gpsonoff((int)parser(n));
#endif
}
static void c_sensor(PARAMETER *plist,int e) {
#ifdef ANDROID
  char *n=plist[0].pointer;
  if(strcmp(n,"ON")==0) do_sensoronoff(1); 
  else if(strcmp(n,"OFF")==0) do_sensoronoff(0); 
  else  do_sensoronoff((int)parser(n));
#endif
}


static void c_clr(PARAMETER *plist,int e) {
  while(--e>=0) {
    switch(plist[e].typ) {
    case PL_ALLVAR:
    case PL_ARRAYVAR: 
    case PL_IARRAYVAR: 
    case PL_FARRAYVAR: 
    case PL_SARRAYVAR: 
      clear_variable(&variablen[plist[e].integer]);
      break;
    case PL_SVAR:
      *(((STRING *)(plist[e].pointer))->pointer)=0;
      ((STRING *)(plist[e].pointer))->len=0;
      break;
    case PL_IVAR:
      *((int *)(plist[e].pointer))=0;
      break;
    case PL_FVAR:
      *((double *)(plist[e].pointer))=0;
      break;
    default:
      printf("ERROR: CLR, unknown var type $%x at argument %d in line pc=%d\n",plist[e].typ,e,pc);
      dump_parameterlist(plist,1);
    }
  }
}
static void c_break(const char *n) {
  if(pc<=0) {bidnm("BREAK"); return;}
  int i=pcode[pc-1].integer;
  if(i==-1) {
    int f=0,o;
    for(i=pc; (i<prglen && i>=0);i++) {
      o=pcode[i].opcode&PM_SPECIAL;
      if((o==P_LOOP || o==P_NEXT || o==P_WEND ||  o==P_UNTIL)  && f<=0) break;
      if(o & P_LEVELIN) f++;
      if(o & P_LEVELOUT) f--;
     }
    if(i==prglen) { xberror(36,"BREAK/EXIT IF"); /*Programmstruktur fehlerhaft */return;}
    pc=i+1;
  } else pc=i;
}

/*  EXIT                 --- same as return 
    EXIT IF <expression>  */

static void c_exit(const char *n) {
  char w1[strlen(n)+1],w2[strlen(n)+1];
  
  wort_sep(n,' ',TRUE,w1,w2);
  if(*w1==0) c_return(n); 
  else if(strcmp(w1,"IF")==0) {
    if(parser(w2)==-1) c_break(NULL);
  } else  printf("ERROR: Syntax error, unknown command: EXIT %s.\n",n);
}


static void c_if(const char *n) {
  if((int)parser(n)==0) {  
    int i,f=0,o;
  
    for(i=pc; (i<prglen && i>=0);i++) {
      o=pcode[i].opcode&PM_SPECIAL;
      if((o==P_ENDIF || o==P_ELSE|| o==P_ELSEIF)  && f==0) break;
      else if(o==P_IF) f++;
      else if(o==P_ENDIF) f--;
    }
    
    if(i==prglen) { xberror(36,"IF"); /*Programmstruktur fehlerhaft */return;}
    pc=i+1;
    if(o==P_ELSEIF) {
      char w1[strlen(program[i])+1],*w2,*w3,*w4;
      xtrim(program[i],TRUE,w1);
      wort_sep_destroy(w1,' ',TRUE,&w2,&w3);
      wort_sep_destroy(w3,' ',TRUE,&w3,&w4);
      if(strcmp(w3,"IF")==0) c_if(w4); 
    } 
  }
}

static void c_select(PARAMETER *plist,int e) {
  int wert2;
  char *w1=NULL,*w2,*w3;  
  if(pc<=0) {bidnm("SELECT"); return;}
  int npc=pcode[pc-1].integer;
  if(npc==-1) {
    xberror(36,"SELECT"); /*Programmstruktur fehlerhaft */
    return;
  }
  int wert=plist->integer;
 // printf("SELECT: value=%d  e=%d\n",wert,e);
  int l=0,l2;
  
  /* Case-Anweisungen finden */
  while(1) {
  //  printf("branch to line %d. <%s>\n",npc-1,program[npc-1]);
    pc=npc;
     if((pcode[pc-1].opcode&PM_SPECIAL)==P_CASE) {
       l2=strlen(program[pc-1])+1;
       if(l2>l || w1==NULL) {
         l=l2+256;
         w1=realloc(w1,l);
       }
       xtrim(program[pc-1],TRUE,w1);
       wort_sep_destroy(w1,' ',TRUE,&w2,&w3);
       
       e=wort_sep_destroy(w3,',',TRUE,&w2,&w3);
       while(e) {
         wert2=parser(w2);
       //  printf("wert2=%d\n",wert2);
	 if(wert==wert2) break;
	 e=wort_sep_destroy(w3,',',TRUE,&w2,&w3);
       }
       if(wert==wert2) break;
       else npc=pcode[pc-1].integer;
     } else break;
  } 
  free(w1);
}
static void c_case(const char *n) {  /* case und default */
  /*gehe zum naechsten ENDSELECT*/
    pc=suchep(pc,1,P_ENDSELECT,P_SELECT,P_ENDSELECT);
    if(pc==-1) xberror(36,"CASE"); /*Programmstruktur fehlerhaft !*/ 
    pc++;
}



/*Diese routine kann stark verbessert werden, wenn 
  Variablen-typ sowie DOWNTO flag schon beim laden in pass 1 bestimmt wird.*/

static void c_next(PARAMETER *plist,int e) {
  char w1[MAXSTRLEN],*w2,*w3,*w4,*var;
  double step, limit,varwert;
  int ss,f=0,hpc=pc,type=NOTYP;

  if(pc<=0) {bidnm("NEXT"); return;}

   pc=pcode[pc-1].integer; /*Hier sind wir beim FOR*/
   if(pc==-1) {xberror(36,"NEXT"); /*Programmstruktur fehlerhaft */return;}

//printf("c_next: das for befindet sich bei pc=%d\n",pc);
//printf("Argument dort ist: <%s>\n",pcode[pc].argument);


   strcpy(w1,pcode[pc].argument);
   wort_sep_destroy(w1,' ',TRUE,&w2,&w3);
 
   /* Variable bestimmem */
   if((var=searchchr(w2,'='))!=NULL) {
     *var++=0;
     var=w2;
     type=vartype(var);
     if(type!=INTTYP && type!=FLOATTYP) {printf("Syntax Error: FOR %s, illegal variable type.\n",w2);batch=0;return;}
   } else {printf("Syntax Error ! FOR %s\n",w2); batch=0;return;}
   wort_sep_destroy(w3,' ',TRUE,&w4,&w2);
   
   if(strcmp(w4,"TO")==0) ss=1; 
   else if(strcmp(w4,"DOWNTO")==0) ss=-1; 
   else {printf("Syntax Error ! FOR %s\n",w4); batch=0;return;}

   /* Limit bestimmem  */
   e=wort_sep_destroy(w2,' ',TRUE,&w4,&w3);
   if(e==0) {printf("Syntax Error ! FOR %s\n",w2); batch=0;return;}
   else {
     limit=parser(w4);
     if(e==1) step=1;
     else {
       /* Step-Anweisung auswerten  */
       wort_sep_destroy(w3,' ',TRUE,&w4,&w2);
       if(strcmp(w4,"STEP")==0)
         step=parser(w2);
       else {printf("Syntax Error ! FOR %s\n",w4); batch=0;return;}
     }
   }
   step*=ss;
   varwert=parser(var)+step;
 //  printf("var=<%s>\n",var);
   if(type==FLOATTYP) zuweis(var,varwert);
   else if(type==INTTYP) izuweis(var,(int)varwert);
   
   if(step<0) ss=-1;
   else ss=1;
//   printf("step=%g ss=%d\n",step,ss);
   /* Schleifenende ueberpruefen    */
   if(ss>0) {
     if(varwert>limit) f=TRUE;
   } else {
     if(varwert<limit) f=TRUE;
   } 
   if(f)  pc=hpc;          /* Schleifenende, gehe hinter NEXT */
   else pc++;
}
static void c_for(const char *n) {
  /* erledigt nur die erste Zuweisung  */
  char *buf=strdup(n);
  char *w1,*w2;

  wort_sep_destroy(buf,' ',TRUE,&w1,&w2);
  if((w2=searchchr(w1,'='))!=NULL) {
    *w2++=0;
    xzuweis(w1,w2);
  } else {printf("Syntax Error ! FOR %s\n",n); batch=0;}
  free(buf);
}
static void c_until(const char *n) {
  if(parser(n)==0) {
    if(pc<=0) {bidnm("UNTIL"); return;}
    int npc=pcode[pc-1].integer;
    if(npc==-1) xberror(36,"UNTIL"); /*Programmstruktur fehlerhaft */
    else pc=npc+1;
  }
}




/* Bei split wollen wir den optionalen int parameter ans ende setzen.
   ist aber noch nicht wegen kompatibilitaet.*/
static void c_split(PARAMETER *plist,int e) {
  STRING str1,str2;
  str1.pointer=malloc(plist->integer+1);
  str2.pointer=malloc(plist->integer+1);
  
  wort_sep2(plist[0].pointer,plist[1].pointer,plist[2].integer,str1.pointer,str2.pointer);
  str1.len=strlen(str1.pointer);
  str2.len=strlen(str2.pointer);
  varcaststring_and_free(plist[3].typ,plist[3].pointer,str1);  
  if(e>4)  varcaststring_and_free(plist[4].typ,(STRING *)plist[4].pointer,str2);
  else free_string(&str2);
}

/* GET_LOCATION lat,lon,alt,res,speed,....*/
/* globale veriablen, welche die GPS Informationen aufnehmen.*/
double gps_alt,gps_lat=-1,gps_lon=-1;
float gps_bearing,gps_accuracy,gps_speed;
double gps_time;
char *gps_provider;

static void c_getlocation(PARAMETER *plist,int e) {
#ifdef ANDROID
  /* mae sure, that the values get updated */
  ANDROID_get_location();
#endif
  if(e>0 && plist[0].typ!=PL_LEER) varcastfloat(plist[0].integer,plist[0].pointer,gps_lat);
  if(e>1 && plist[1].typ!=PL_LEER) varcastfloat(plist[1].integer,plist[1].pointer,gps_lon);
  if(e>2 && plist[2].typ!=PL_LEER) varcastfloat(plist[2].integer,plist[2].pointer,gps_alt);
  if(e>3 && plist[3].typ!=PL_LEER) varcastfloat(plist[3].integer,plist[3].pointer,gps_bearing);
  if(e>4 && plist[4].typ!=PL_LEER) varcastfloat(plist[4].integer,plist[3].pointer,gps_accuracy);
  if(e>5 && plist[5].typ!=PL_LEER) varcastfloat(plist[5].integer,plist[3].pointer,gps_speed);
  if(e>6 && plist[6].typ!=PL_LEER) varcastfloat(plist[6].integer,plist[6].pointer,gps_time);
  if(e>7 && plist[7].typ!=PL_LEER) {
    STRING a;
    a.pointer=gps_provider;
    if(a.pointer) a.len=strlen(a.pointer);
    else a.len=0;
    varcaststring(plist[7].integer,plist[7].pointer,a);
  }
}


static void c_poke(PARAMETER *plist,int e) {
  char *adr=(char *)(plist->integer);
  *adr=(char)plist[1].integer;
}
static void c_dpoke(PARAMETER *plist,int e) {
  short *adr=(short *)(plist[0].integer);
  *adr=(short)plist[1].integer;
}
static void c_lpoke(PARAMETER *plist,int e) {
  long *adr=(long *)plist[0].integer;
  *adr=(long)plist[1].integer;
}

/* SOUND channel,frequency [Hz],volume (0-1),duration (s)*/

static void c_sound(PARAMETER *plist,int e) { 
  double duration=-1;
  int c=-1;
  double frequency=-1;
  double volume=-1;
  if(plist[0].typ!=PL_LEER) c=plist[0].integer;
  if(e>=2 && plist[1].typ!=PL_LEER) frequency=plist[1].real;
  if(e>=3 && plist[2].typ!=PL_LEER) volume=plist[2].real;
  if(e>=4) duration=plist[3].real;
  sound_activate();
  do_sound(c,frequency,volume,duration);
}

/* PLAYSOUND channel,data$[,pitch,volume] */

static void c_playsound(PARAMETER *plist,int e) { 
  int pitch=0x100,volume=0xffff;
  int c=-1;
  sound_activate();
  if(plist[0].typ!=PL_LEER) c=plist[0].integer;
  if(e>=3) pitch= (int)(plist[2].real*0x100);
  if(e>=4) volume= (int)(plist[3].real*0xffff);
  do_playsound(c,plist[1].pointer,plist[1].integer,pitch,volume,0);
}

static void c_playsoundfile(PARAMETER *plist,int e) {
  if(exist(plist[0].pointer)) {
#ifdef ANDROID
  ANDROID_playsoundfile(plist[0].pointer);
#else
  char buffer[256];
  sprintf(buffer,"ogg123 %s &",(char *)plist[0].pointer); 
  if(system(buffer)==-1) io_error(errno,"system");
#endif    
  } else xberror(-33,plist[0].pointer); /* file not found*/
}


/* WAVE channel,...*/

static void c_wave(PARAMETER *plist,int e) { 
  int c=-1;
  int form=-1;
  double attack=-1;
  double decay=-1;
  double sustain=-1;
  double release=-1;
  
  if(plist[0].typ!=PL_LEER) c=plist[0].integer;
  if(e>=2 && plist[1].typ!=PL_LEER) form=plist[1].integer;
  if(e>=3 && plist[2].typ!=PL_LEER) attack=plist[2].real;
  if(e>=4 && plist[3].typ!=PL_LEER) decay=plist[3].real;
  if(e>=5 && plist[4].typ!=PL_LEER) sustain=plist[4].real;
  if(e>=6 && plist[5].typ!=PL_LEER) release=plist[5].real;
  sound_activate();
  do_wave(c,form,attack,decay,sustain,release);
}

#ifdef ANDROID
  extern void ANDROID_speek(char *,double,double,char *);
#endif

static void c_speak(PARAMETER *plist,int e) { 
#ifdef ANDROID
  double pitch=-1,rate=-1;
  char *enc=NULL;
  if(e>=2) pitch= plist[1].real;
  if(e>=3) rate=plist[2].real;
  if(e>=4) enc=plist[3].pointer;
  ANDROID_speek(plist[0].pointer,pitch,rate,enc);
#endif
}

static void c_eval(PARAMETER *plist,int e) { kommando(plist[0].pointer); }


/* Kommandoliste: muss alphabetisch sortiert sein !   */

const COMMAND comms[]= {

 { P_ARGUMENT,  " nulldummy", bidnm      , 0, 0},
 { P_REM,       "!"         , c_nop      , 0, 0},
 { P_PLISTE,    "?"         , c_print    , 0,-1,(unsigned short []){PL_EVAL}},

 { P_PLISTE,   "ADD"      , c_add        , 2, 2,(unsigned short []){PL_ANYVAR,PL_ANYVALUE}},
 { P_PLISTE,   "AFTER"    , c_after      , 2, 2,(unsigned short []){PL_INT,PL_PROC}},
#ifndef NOGRAPHICS
 { P_PLISTE,     "ALERT"    , c_alert    , 5, 6,(unsigned short []){PL_INT,PL_STRING,PL_INT,PL_STRING,PL_NVAR,PL_SVAR}},
#endif
 { P_PLISTE,   "ARRAYCOPY", c_arraycopy  , 2, 2,(unsigned short []){PL_ARRAYVAR,PL_ARRAYVAR}}, /*zweiter parameter muesste "PL_ARRAY sein, nicht ARRAYVAR*/
 { P_PLISTE,   "ARRAYFILL", c_arrayfill  , 2, 2,(unsigned short []){PL_ARRAYVAR,PL_ANYVALUE}},

 { P_SIMPLE,     "BEEP"     , c_beep      ,0, 0},
 { P_SIMPLE,     "BELL"     , c_beep      ,0, 0},
 { P_PLISTE,     "BGET"     , c_bget      ,3, 3,(unsigned short []){PL_FILENR,PL_INT,PL_INT}},
 { P_PLISTE,     "BLOAD"    , c_bload     ,2, 3,(unsigned short []){PL_STRING,PL_INT,PL_INT}},
 { P_PLISTE,     "BMOVE"    , c_bmove     ,3, 3,(unsigned short []){PL_INT,PL_INT,PL_INT} },
#ifndef NOGRAPHICS
 { P_PLISTE,     "BOTTOMW"  , c_bottomw,   0, 1,(unsigned short []){PL_FILENR}},
 { P_PLISTE,     "BOUNDARY" , c_boundary  ,1, 1,(unsigned short []){PL_INT}},
 { P_PLISTE,     "BOX"      , c_box       ,4, 4,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_PLISTE,     "BPUT"     , c_bput      ,3, 3,(unsigned short []){PL_FILENR,PL_INT,PL_INT}},
 { P_BREAK,      "BREAK"    , c_break     ,0, 0},
 { P_PLISTE,     "BSAVE"    , c_bsave     ,3, 3,(unsigned short []){PL_STRING,PL_INT,PL_INT}},

 { P_PLISTE,     "CALL"     , c_call      ,1,-1,(unsigned short []){PL_INT,PL_EVAL}},
 { P_CASE,       "CASE"     , c_case      ,1, 1,(unsigned short []){PL_NUMBER}},
 { P_PLISTE,     "CHAIN"    , c_chain     ,1, 1,(unsigned short []){PL_STRING}},
 { P_PLISTE,     "CHDIR"    , c_chdir     ,1, 1,(unsigned short []){PL_STRING}},
 { P_PLISTE,     "CHMOD"    , c_chmod,2,2,(unsigned short []){PL_STRING,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,     "CIRCLE"   , c_circle    ,3, 5,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_PLISTE,   "CLEAR"    , c_clear     ,0,-1,(unsigned short []){PL_ALLVAR}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "CLEARW"   , c_clearw      ,0, 1,(unsigned short []){PL_FILENR}},
 { P_PLISTE,   "CLIP"     , c_clip        ,4, 6,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_PLISTE,   "CLOSE"    , c_close     ,0,-1,(unsigned short []){PL_FILENR}},
#ifndef NOGRAPHICS
 { P_PLISTE,     "CLOSEW"   , c_closew    ,0, 1,(unsigned short []){PL_FILENR}},
#endif
 { P_PLISTE,   "CLR"      , c_clr       ,1,-1,(unsigned short []){PL_ALLVAR,PL_ALLVAR}},
 { P_SIMPLE,     "CLS"      , c_cls       ,0, 0},
#ifndef NOGRAPHICS
 { P_PLISTE,     "COLOR"    , c_color     ,1,2,(unsigned short []){PL_INT,PL_INT}},
#endif
 { P_PLISTE,     "CONNECT"  , c_connect   ,2,3,(unsigned short []){PL_FILENR,PL_STRING,PL_INT}},
 { P_CONTINUE,     "CONTINUE" , c_cont      ,0,0},
#ifndef NOGRAPHICS
 { P_PLISTE,     "COPYAREA"     , c_copyarea   ,6,6,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
/* Kontrollsystembefehle  */
#ifdef CONTROL
 { P_ARGUMENT,   "CSPUT"    , c_csput ,2,-1,(unsigned short []){PL_STRING,PL_VALUE}},
 { P_SIMPLE, "CSCLEARCALLBACKS"    , c_csclearcallbacks,0,0},
 { P_ARGUMENT,   "CSSET"    , c_csput,2,-1,(unsigned short []){PL_STRING,PL_VALUE}},
 { P_ARGUMENT,   "CSSETCALLBACK", c_cssetcallback,2,-1},
 { P_ARGUMENT,   "CSSWEEP"  , c_cssweep,2,-1},
 { P_ARGUMENT,   "CSVPUT"   , c_csvput,2,-1},
#endif
 { P_PLISTE,     "CURVE"     , c_curve,8,9,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},

 { P_DATA,     "DATA"     , c_nop ,0,-1 },
 { P_PLISTE, "DEC"      , c_dec, 1,1,(unsigned short []){PL_NVAR}},
 { P_DEFAULT,  "DEFAULT"  , c_case, 0,0},
#ifndef NOGRAPHICS
 { P_PLISTE,   "DEFFILL"  , c_deffill ,1,3,(unsigned short []){PL_INT,PL_INT,PL_INT}},
#endif
 { P_DEFFN,    "DEFFN"     , bidnm  ,0,0},
#ifndef NOGRAPHICS
 { P_PLISTE,   "DEFLINE"  , c_defline ,1,4,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,   "DEFMARK"  , c_defmark,1,3,(unsigned short []){PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,   "DEFMOUSE" , c_defmouse, 1,1,(unsigned short []){PL_INT}},
 { P_PLISTE,   "DEFTEXT"  , c_deftext,1,4,(unsigned short []){PL_INT,PL_NUMBER,PL_NUMBER,PL_NUMBER}},
#endif
 { P_PLISTE,   "DELAY"    , c_pause,      1,1,(unsigned short []){PL_NUMBER}},
 { P_PLISTE,   "DIM"      , c_dim ,1,-1,(unsigned short []){PL_EVAL,PL_EVAL}},
 { P_PLISTE,   "DIV"      , c_div ,2,2,(unsigned short []){PL_NVAR,PL_NUMBER}},
 { P_DO,     "DO"       , c_do  ,0,0},
#ifdef DOOCS
/* { P_ARGUMENT,   "TINEBROADCAST", c_tinebroadcast,1,-1,{PL_STRING}},
 { P_SIMPLE,     "TINECYCLE", c_tinecycle,0,0},
 { P_ARGUMENT,   "TINEDELIVER", c_tinedeliver,1,-1},   */
 { P_ARGUMENT,   "DOOCSCALLBACK", c_doocscallback,2,3, (unsigned short []){PL_VAR,PL_PROC,PL_PROC}},
 { P_ARGUMENT,   "DOOCSEXPORT", c_doocsexport,1,-1},
/* { P_ARGUMENT,   "TINELISTEN", c_tinelisten,1,-1,{PL_STRING}},
 { P_PLISTE,     "TINEMONITOR", c_tinemonitor,2,3,{PL_STRING,PL_PROC,PL_INT}},*/
 { P_ARGUMENT,   "DOOCSPUT"    , c_doocsput ,2,-1,(unsigned short []){PL_STRING}},
 { P_PLISTE,     "DOOCSSERVER" , c_doocsserver,0,2,(unsigned short []){PL_STRING,PL_INT}},
 { P_ARGUMENT,   "DOOCSSET"    , c_doocsput ,2,-1,(unsigned short []){PL_STRING}},
#endif
 { P_PLISTE,   "DPOKE"    , c_dpoke,       2,2,(unsigned short []){PL_INT,PL_INT}},
#ifndef NOGRAPHICS
 { P_ARGUMENT,   "DRAW"     , c_draw ,2,-1,(unsigned short []){PL_INT,PL_INT}},
#endif
 { P_PLISTE,   "DUMP"     , c_dump ,0,1,(unsigned short []){PL_STRING}},

 { P_PLISTE,   "ECHO"     , c_echo ,1,1,(unsigned short []){PL_KEY}},
 { P_SIMPLE,   "EDIT"     , c_edit ,0,0},
#ifndef NOGRAPHICS
 { P_PLISTE,   "ELLIPSE"  , c_ellipse,4,6,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_ELSE,   "ELSE"     , bidnm  ,0,2,(unsigned short []){PL_KEY,PL_CONDITION}},
 { P_SIMPLE, "END"      , c_end   ,0,0},
 { P_ENDPROC,"ENDFUNCTION", c_return,0,0},
 { P_ENDIF,  "ENDIF"       , bidnm  ,0,0},
 { P_ENDSELECT,"ENDSELECT" , bidnm  ,0,0},
 { P_PLISTE,   "ERASE"    , c_erase,1,-1,(unsigned short []){PL_ARRAYVAR,PL_ARRAYVAR}},
 { P_PLISTE,   "ERROR"    , c_error,1,1,(unsigned short []){PL_INT}},
 { P_PLISTE,   "EVAL"     , c_eval,1,1,(unsigned short []){PL_STRING}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "EVENT"    , c_allevent,0,9,(unsigned short []){PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_SVAR}},
#endif
 { P_PLISTE,   "EVERY"    , c_every,2,2,(unsigned short []){PL_INT,PL_PROC}},
 { P_PLISTE,   "EXEC"     , c_exec,1,3,(unsigned short []){PL_STRING,PL_STRING,PL_STRING}},
 { P_ARGUMENT,   "EXIT"     , c_exit,0,-1},
/*
 { P_ARGUMENT,   "EXPORT"     , c_export,1,2, {PL_ALLVAR, PL_NUMBER}},
*/
 { P_ARGUMENT,   "FFT"      , c_fft,1,2,(unsigned short []){PL_FARRAY,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "FILESELECT", c_fileselect,4,4,(unsigned short []){PL_STRING,PL_STRING,PL_STRING,PL_SVAR}},
 { P_PLISTE,   "FILL"     , c_fill,2,3,(unsigned short []){PL_INT,PL_INT,PL_INT}},
#endif
 { P_ARGUMENT,   "FIT",        c_fit,4,10,(unsigned short []){PL_FARRAY,PL_FARRAY}},
 { P_ARGUMENT,   "FIT_LINEAR", c_fit_linear,4,10,(unsigned short []){PL_FARRAY,PL_FARRAY}},
 { P_PLISTE,   "FLUSH"    , c_flush,0,1,(unsigned short []){PL_FILENR}},
 { P_FOR,    "FOR"      , c_for,1,-1,(unsigned short []){PL_EXPRESSION,PL_KEY,PL_NUMBER,PL_KEY,PL_NUMBER}},
 { P_PLISTE,    "FREE"      , c_free,1,1,(unsigned short []){PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "FULLW"    , c_fullw,0,1, (unsigned short []){PL_FILENR}},
#endif
 { P_PROC,   "FUNCTION" , c_end,1,-1,(unsigned short []){PL_EXPRESSION}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "GET"      , c_get,5,5,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_SVAR}},
 { P_PLISTE,   "GET_GEOMETRY" , c_getgeometry,2,7,(unsigned short []){PL_FILENR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR}},
 { P_PLISTE,   "GET_LOCATION" , c_getlocation,2,8,(unsigned short []){PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_SVAR}},
 
 { P_PLISTE,   "GET_SCREENSIZE" , c_getscreensize,1,5,(unsigned short []){PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR}},
#endif
 { P_GOSUB,     "GOSUB"    , c_gosub,1,1,(unsigned short []){PL_PROC}},
 { P_GOTO,       "GOTO"     , c_goto,1,1,(unsigned short []){PL_LABEL}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "GPRINT"    , c_gprint,       0,-1,(unsigned short []){PL_EVAL}},
#endif
 { P_PLISTE,   "GPS"     , c_gps ,1,1,(unsigned short []){PL_KEY}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "GRAPHMODE", c_graphmode,1,1,(unsigned short []){PL_INT}},
#endif
 { P_PLISTE,   "HELP"    , c_help,0,1,(unsigned short []){PL_KEY}},
#ifndef NOGRAPHICS
 { P_SIMPLE,     "HIDEM"     , c_hidem,0,0},
#endif
 { P_SIMPLE,     "HOME"     , c_home,0,0},

 { P_IF,         "IF"       , c_if,1,-1,(unsigned short []){PL_CONDITION}},
 { P_PLISTE,   "INC"      , c_inc,1,1,(unsigned short []){PL_NVAR}},
#ifndef NOGRAPHICS
 { P_PLISTE,	 "INFOW"    , c_infow,    2,2,(unsigned short []){PL_FILENR,PL_STRING}},
#endif
 { P_ARGUMENT,   "INPUT"    , c_input,1,-1,(unsigned short []){PL_ALLVAR}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "KEYEVENT" , c_keyevent,0,8,(unsigned short []){PL_NVAR,PL_NVAR,PL_SVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR}},
#endif
 { P_PLISTE,     "KILL"    , c_kill     ,1, 1,(unsigned short []){PL_STRING}},


 { P_ARGUMENT,   "LET"      , c_let,1,-1,(unsigned short []){PL_KEY}},
#ifndef NOGRAPHICS
 { P_PLISTE,     "LINE"     , c_line,4,4,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_ARGUMENT,   "LINEINPUT", c_lineinput,1,2, (unsigned short []){PL_FILENR,PL_STRING}},
 { P_PLISTE,     "LINK"     , c_link,       2,2,(unsigned short []){PL_FILENR,PL_STRING}},

 { P_PLISTE,     "LIST"     , c_list,0,2,(unsigned short []){PL_INT,PL_INT}},
 { P_PLISTE,     "LOAD"     , c_load,1,1,(unsigned short []){PL_STRING}},
 { P_PLISTE,     "LOCAL"    , c_local,1,-1,(unsigned short []){PL_ALLVAR,PL_ALLVAR}},
 { P_PLISTE,     "LOCATE"    , c_locate,2,2,(unsigned short []){PL_INT,PL_INT}},
 { P_LOOP,       "LOOP"     , bidnm,0,0},
 { P_PLISTE,     "LPOKE"    , c_lpoke,       2,2,(unsigned short []){PL_INT,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "LTEXT"     , c_ltext,3,3,(unsigned short []){PL_INT,PL_INT,PL_STRING}},
#endif

 { P_PLISTE,     "MEMDUMP"    , c_memdump,2,2,(unsigned short []){PL_INT,PL_INT}},
#ifndef NOGRAPHICS
 { P_SIMPLE,     "MENU"    , c_menu,0,0},
 { P_ARGUMENT,   "MENUDEF"  , c_menudef,1,2,(unsigned short []){PL_SARRAY,PL_PROC}},
 { P_SIMPLE,     "MENUKILL" , c_menukill,0,0},
 { P_PLISTE,     "MENUSET"  , c_menuset,2,2,(unsigned short []){PL_INT,PL_INT}},
#endif
 { P_PLISTE,    "MERGE"    , c_merge,1,1,(unsigned short []){PL_STRING}},
 { P_PLISTE,    "MFREE"      , c_free,1,1,(unsigned short []){PL_INT}},
 { P_PLISTE,    "MKDIR"    , c_mkdir     ,1, 2,(unsigned short []){PL_STRING,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "MOUSE"    , c_mouse,1,5,(unsigned short []){PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR}},
 { P_PLISTE,   "MOUSEEVENT" , c_mouseevent,0,6,(unsigned short []){PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR}},
 { P_PLISTE,   "MOTIONEVENT" , c_motionevent,0,6,(unsigned short []){PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR,PL_NVAR}},
 { P_PLISTE,   "MOVEW"    , c_movew,3,3, (unsigned short []){PL_FILENR,PL_INT,PL_INT}},
#endif
 { P_PLISTE,  "MSYNC"     , c_msync  ,2,2,(unsigned short []){PL_INT, PL_INT}},
 { P_PLISTE,   "MUL"      , c_mul,2,2,(unsigned short []){PL_NVAR,PL_NUMBER}},

 { P_SIMPLE, "NEW"      , c_new,0,0},
 { P_NEXT,   "NEXT"     , c_next,0,1,(unsigned short []){PL_NVAR}},
 { P_IGNORE|P_SIMPLE, "NOOP",         c_nop,         0,0},
 { P_IGNORE|P_SIMPLE, "NOP",          c_nop,         0,0},
#ifndef NOGRAPHICS
 { P_SIMPLE,"NOROOTWINDOW", c_norootwindow,0,0},
 { P_PLISTE,   "OBJC_ADD"    , c_objc_add,      3,3,(unsigned short []){PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,   "OBJC_DELETE"    , c_objc_delete,      2,2,(unsigned short []){PL_INT,PL_INT}},
#endif
 { P_ARGUMENT,   "ON"       , c_on,         1,-1,(unsigned short []){PL_KEY}},
 { P_PLISTE,     "OPEN"     , c_open,       3,4,(unsigned short []){PL_STRING,PL_FILENR,PL_STRING,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "OPENW"    , c_openw,      1,1,(unsigned short []){PL_FILENR}},
#endif
 { P_PLISTE,   "OUT"      , c_out,        2,-1,(unsigned short []){PL_FILENR,PL_EVAL,PL_EVAL}},

 { P_PLISTE,   "PAUSE"    , c_pause,      1,1,(unsigned short []){PL_NUMBER}},
#ifndef NOGRAPHICS
 { P_PLISTE,     "PBOX"     , c_pbox ,      4,4,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,     "PCIRCLE"  , c_pcircle,    3,5,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,     "PELLIPSE" , c_pellipse,   4,6,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_PLISTE,     "PIPE" , c_pipe,   2,2,(unsigned short []){PL_FILENR,PL_FILENR}},
 { P_PLISTE,     "PLAYSOUND",     c_playsound, 2,4,(unsigned short []){PL_INT,PL_STRING,PL_NUMBER,PL_NUMBER}},
 { P_PLISTE,     "PLAYSOUNDFILE",     c_playsoundfile, 1,1,(unsigned short []){PL_STRING}},
 { P_SIMPLE,     "PLIST"    , c_plist,      0,0},
#ifndef NOGRAPHICS
 { P_PLISTE,     "PLOT"     , c_plot,       2,2,(unsigned short []){PL_INT,PL_INT}},
#endif
 { P_PLISTE,   "POKE"     , c_poke,       2,2,(unsigned short []){PL_INT,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "POLYFILL" , c_polyfill,         3,7,(unsigned short []){PL_INT,PL_IARRAY,PL_IARRAY,PL_INT,PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,   "POLYLINE" , c_polyline,       3,7,(unsigned short []){PL_INT,PL_IARRAY,PL_IARRAY,PL_INT,PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,   "POLYMARK" , c_polymark,   3,7,(unsigned short []){PL_INT,PL_IARRAY,PL_IARRAY,PL_INT,PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,   "PRBOX"    , c_prbox ,      4,4,(unsigned short []){PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_PLISTE,  "PRINT"    , c_print,       0,-1,(unsigned short []){PL_EVAL}},
 { P_PROC,   "PROCEDURE", c_end  ,      0,0},
 { P_IGNORE, "PROGRAM"  , c_nop  ,      0,0},
 /* Ausdruck als Message queuen
  { P_ARGUMENT,   "PUBLISH"  , c_publish, 1,2,{PL_ALLVAR,PL_NUMBER}},
 */
#ifndef NOGRAPHICS
 { P_PLISTE,   "PUT"  , c_put,      3,4,(unsigned short []){PL_INT,PL_INT,PL_STRING,PL_NUMBER}},
#endif
 { P_PLISTE,   "PUTBACK"  , c_unget,      2,2,(unsigned short []){PL_FILENR,PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "PUT_BITMAP"  , c_put_bitmap, 5,5,(unsigned short []){PL_STRING,PL_INT,PL_INT,PL_INT,PL_INT}},
#endif

 { P_PLISTE, "QUIT"     , c_quit,       0,1,(unsigned short []){PL_INT}},

 { P_PLISTE, "RANDOMIZE", c_randomize  ,      0,1,(unsigned short []){PL_INT}},
#ifndef NOGRAPHICS
 { P_PLISTE,     "RBOX"      , c_rbox       ,4, 4,(unsigned short []) {PL_INT,PL_INT,PL_INT,PL_INT}},
#endif
 { P_PLISTE,   "READ"     , c_read,       1,-1,(unsigned short []){PL_ALLVAR,PL_ALLVAR}},
 { P_PLISTE,     "RECEIVE"  , c_receive,    2,3,(unsigned short []){PL_FILENR,PL_SVAR,PL_NVAR}},
 { P_PLISTE,     "RELSEEK"  , c_relseek,    2,2,(unsigned short []){PL_FILENR,PL_INT}},
 { P_REM,    "REM"      , c_nop  ,      0,0},
 { P_PLISTE,   "RENAME"     , c_rename,2,2,(unsigned short []){PL_STRING,PL_STRING}},
 { P_REPEAT, "REPEAT"   , c_nop  ,      0,0},
 { P_PLISTE,   "RESTORE"  , c_restore,    0,1,(unsigned short []){PL_LABEL}},
 { P_RETURN,   "RETURN"   , c_return,     0,1,(unsigned short []){PL_CONDITION}},
 { P_PLISTE,     "RMDIR"    , c_rmdir     ,1, 1,(unsigned short []){PL_STRING}},
#ifndef NOGRAPHICS
 { P_SIMPLE, "ROOTWINDOW", c_rootwindow,0,0},
 { P_SIMPLE, "RSRC_FREE", c_rsrc_free,0,0},
 { P_PLISTE, "RSRC_LOAD", c_rsrc_load,1,1,(unsigned short []){PL_STRING}},
#endif

 { P_SIMPLE, "RUN"      , c_run,        0,0},

 { P_PLISTE,   "SAVE"     , c_save,0,1,(unsigned short []){PL_STRING}},
#ifndef NOGRAPHICS
 { P_PLISTE,   "SAVESCREEN", c_savescreen,1,1,(unsigned short []){PL_STRING}},
 { P_PLISTE,   "SAVEWINDOW", c_savewindow,1,1,(unsigned short []){PL_STRING}},
 { P_ARGUMENT,   "SCOPE"    , c_scope,      1,6,(unsigned short []){PL_NARRAY,PL_ANYVALUE,PL_NUMBER,PL_NUMBER,PL_NUMBER,PL_NUMBER}},
#endif
 { P_PLISTE,   "SCREEN"    , c_screen,      1,1,(unsigned short []){PL_INT}},
 { P_PLISTE,   "SEEK"     , c_seek,       1,2,(unsigned short []){PL_FILENR,PL_INT}},
 { P_SELECT, "SELECT"   , c_select,     1,1,(unsigned short []){PL_INT}},
 /*
 { P_ARGUMENT,   "SEMGIVE"  , c_semgive, 1,2,{PL_NUMBER,PL_NUMBER}},
 { P_ARGUMENT,   "SEMTAKE"  , c_semtake, 1,2,{PL_NUMBER,PL_NUMBER}},
 */
 { P_PLISTE, "SEND"   , c_send,     2,4,(unsigned short []){PL_FILENR,PL_STRING,PL_INT,PL_INT}},
 { P_PLISTE, "SENSOR" , c_sensor ,1,1,(unsigned short []){PL_KEY}},
#ifndef NOGRAPHICS
 { P_PLISTE,	"SETFONT"  , c_setfont,    1,1,(unsigned short []){PL_STRING}},
 { P_PLISTE,	"SETMOUSE" , c_setmouse,   2,3,(unsigned short []){PL_INT,PL_INT,PL_INT}},
 { P_PLISTE,	"SGET" , c_sget,   1,1,(unsigned short []){PL_SVAR}},
#endif
 { P_PLISTE,	"SHELL"   , c_shell,     1,-1,(unsigned short []){PL_STRING}},
 { P_PLISTE,  "SHM_DETACH"      , c_detatch,1,1,(unsigned short []){PL_INT}},
 { P_PLISTE,    "SHM_FREE" , c_shm_free,1,1,(unsigned short []){PL_INT}},
#ifndef NOGRAPHICS
 { P_SIMPLE,     "SHOWM"     , c_showm,0,0},
 { P_SIMPLE,	 "SHOWPAGE" , c_vsync,      0,0},
 { P_PLISTE,	 "SIZEW"    , c_sizew,      3,3,(unsigned short []){PL_FILENR,PL_INT,PL_INT}},
#endif
 { P_PLISTE,    "SORT",      c_sort,        1,3,(unsigned short []){PL_ARRAYVAR,PL_INT,PL_IARRAYVAR}},
 { P_PLISTE,    "SOUND",     c_sound,        2,4,(unsigned short []){PL_INT,PL_NUMBER,PL_NUMBER,PL_NUMBER}},

 { P_GOSUB,     "SPAWN"    , c_spawn,1,1,(unsigned short []){PL_PROC}},
 { P_PLISTE,    "SPEAK",     c_speak, 1,4,(unsigned short []){PL_STRING,PL_NUMBER,PL_NUMBER,PL_STRING}},

 { P_PLISTE,	"SPLIT"    , c_split,  4,5,(unsigned short []){PL_STRING,PL_STRING,PL_INT,PL_SVAR,PL_SVAR}},
#ifndef NOGRAPHICS
 { P_PLISTE,	"SPUT"     , c_sput,      1,1,(unsigned short []){PL_STRING}},
#endif
 { P_SIMPLE,	"STOP"     , c_stop,       0,0},
 { P_PLISTE,	"SUB"      , c_sub,        2,2,(unsigned short []){PL_NVAR,PL_NUMBER}},
 { P_PLISTE,	"SWAP"     , c_swap,       2,2,(unsigned short []){PL_ALLVAR,PL_ALLVAR}},
 { P_PLISTE,	"SYSTEM"   , c_system,     1,1,(unsigned short []){PL_STRING}},

#ifndef NOGRAPHICS
 { P_PLISTE,	"TEXT"     , c_text,       3,3,(unsigned short []){PL_INT,PL_INT,PL_STRING}},
#endif
#ifdef TINE
 { P_ARGUMENT,   "TINEBROADCAST", c_tinebroadcast,1,-1,(unsigned short []){PL_STRING}},
 { P_SIMPLE,     "TINECYCLE", c_tinecycle,0,0},
 { P_ARGUMENT,   "TINEDELIVER", c_tinedeliver,1,-1},
 { P_ARGUMENT,   "TINEEXPORT", c_tineexport,1,-1},
 { P_ARGUMENT,   "TINELISTEN", c_tinelisten,1,-1,(unsigned short []){PL_STRING}},
 { P_PLISTE,     "TINEMONITOR", c_tinemonitor,2,3,(unsigned short []){PL_STRING,PL_PROC,PL_INT}},
 { P_ARGUMENT,   "TINEPUT"    , c_tineput ,2,-1,(unsigned short []){PL_STRING}},
 { P_PLISTE,     "TINESERVER" , c_tineserver,0,2,(unsigned short []){PL_STRING,PL_INT}},
 { P_ARGUMENT,   "TINESET"    , c_tineput ,2,-1,(unsigned short []){PL_STRING}},
#endif
#ifndef NOGRAPHICS
 { P_PLISTE,	"TITLEW"   , c_titlew,     2,2,(unsigned short []){PL_FILENR,PL_STRING}},
 { P_PLISTE,    "TOPW"     , c_topw,       0,1,  (unsigned short []) {PL_FILENR}},
#endif
 { P_PLISTE,    "TOUCH"    , touch,1,1,(unsigned short []){PL_FILENR}},
 { P_SIMPLE,	"TROFF"    , c_troff,      0,0},
 { P_SIMPLE,	"TRON"     , c_tron,       0,0},

 { P_PLISTE,  "UNLINK"   , c_close  ,     1,-1,(unsigned short []){PL_FILENR,PL_FILENR}},
 { P_PLISTE,    "UNMAP"    , c_unmap      ,2,2,(unsigned short []){PL_INT, PL_INT}},
 { P_UNTIL,	"UNTIL"    , c_until,      1,1,(unsigned short []){PL_CONDITION}},
#ifndef NOGRAPHICS
 { P_PLISTE,	"USEWINDOW", c_usewindow,  1,1,(unsigned short []){PL_FILENR}},
#endif

 { P_SIMPLE,	"VERSION"  , c_version,    0,0},
 { P_ARGUMENT,	"VOID"     , c_void,       1,1,(unsigned short []){PL_EVAL}},
#ifndef NOGRAPHICS
 { P_SIMPLE,	"VSYNC"    , c_vsync,      0,0},
#endif
 { P_PLISTE,     "WATCH"     , c_watch,  1,1,(unsigned short []){PL_STRING}},
 { P_PLISTE,    "WAVE",     c_wave,      2,6,(unsigned short []){PL_INT,PL_INT,PL_NUMBER,PL_NUMBER,PL_NUMBER,PL_NUMBER}},

 { P_WEND,	"WEND"     , bidnm,      0,0},
 { P_WHILE,	"WHILE"    , c_while,    1,1,(unsigned short []){PL_CONDITION}},
 { P_PLISTE,	"WORT_SEP" , c_split,    4,5,(unsigned short []){PL_STRING,PL_STRING,PL_INT,PL_SVAR,PL_SVAR}},
#ifndef NOGRAPHICS
 { P_SIMPLE,	"XLOAD"    , c_xload,    0,0},
 { P_SIMPLE,	"XRUN"     , c_xrun,     0,0},
#endif

};
const int anzcomms=sizeof(comms)/sizeof(COMMAND);













