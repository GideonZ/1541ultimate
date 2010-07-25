/*

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#define _MISC_C_
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#ifndef WIN32
#include <search.h>
#include <argp.h>
#endif
#include "misc.h"
#include "opcodes.h"
#include <string.h>

struct arguments_t arguments={1,0,0,0,NULL,"a.out",OPCODES_6502,NULL,NULL,1,1,0,0,1};

static void *label_tree=NULL;
static void *macro_tree=NULL; 
static void *file_tree1=NULL;
#ifndef WIN32
static void *file_tree2=NULL;
#endif
struct serrorlist *errorlist=NULL,*errorlistlast=NULL;
struct sfilenamelist *filenamelist=NULL;
int encoding;

unsigned char tolower_tab[256];

unsigned char whatis[256]={
    WHAT_EOL,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_HASHMARK,WHAT_EXPRESSION,WHAT_EXPRESSION,0,0,WHAT_EXPRESSION,0,WHAT_STAR,WHAT_EXPRESSION,WHAT_COMA,WHAT_EXPRESSION,WHAT_COMMAND,0,
    WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,WHAT_EXPRESSION,0,0,WHAT_EXPRESSION,WHAT_EQUAL,WHAT_EXPRESSION,0,
    WHAT_EXPRESSION,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,
    WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,0,0,0,0,WHAT_LBL,
    0,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,
    WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,WHAT_CHAR,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

unsigned char petascii(unsigned char ch) {
    if (arguments.toascii) {
        if (ch>='A' && ch<='Z') ch+=0x80;
        if (ch>='a' && ch<='z') ch-=0x20;
    }
    switch (encoding) {
    case 1:
        if (ch<=0x1F) return ch+0x80;
        if (ch>=0x20 && ch<=0x3F) return ch;
        if (ch>=0x40 && ch<=0x5F) return ch-0x40;
        if (ch>=0x60 && ch<=0x7F) return ch-0x20;
        if (ch>=0x80 && ch<=0x9F) return ch;
        if (ch>=0xA0 && ch<=0xBF) return ch-0x40;
        if (ch>=0xC0 && ch<=0xFE) return ch-0x80;
        if (ch==0xFF) return 0x5E;
    }
    return ch;
}

void adderror(char *s) {
    struct serrorlist *b;

    b=malloc(sizeof(struct serrorlist)+strlen(s));

    if (!b) {fprintf(stderr,"Out of memory\n");exit(1);}

    b->next=NULL;
    strcpy(&b->name,s);

    if (!errorlist)
        errorlist=b;
    else
        errorlistlast->next=b;

    errorlistlast=b;
}

void freeerrorlist(int print) {
    struct serrorlist *b;

    while (errorlist) {
        b=errorlist->next;
        if (print) fprintf(stderr,"%s",&errorlist->name);
        free(errorlist);
        errorlist=b;
    }
}

void enterfile(char *s, long l) {
    struct sfilenamelist *b;

    b=malloc(sizeof(struct sfilenamelist)+strlen(s));

    if (!b) {fprintf(stderr,"Out of memory\n");exit(1);}

    b->next=filenamelist;
    strcpy(&b->name,s);
    b->line=l;

    filenamelist=b;
}

void exitfile() {
    struct sfilenamelist *b;

    b=filenamelist;
    filenamelist=b->next;
    free(b);
}

#define linelength 4096

static char *terr_warning[]={
	"Top of memory excedeed",
	"Possibly incorrectly used A",
	"Memory bank excedeed",
	"Possible jmp ($xxff) bug",
        "Long branch used"
//	"%s\n",
};
static char *terr_error[]={
	"Double defined %s",
	"Not defined %s",
	"Extra characters on line",
	"Constant too large",
	"General syntax",
	"%s expected",
	"Expression syntax",
	"Branch too far",
        "Missing argument",
        "Illegal operand",
        "Unknown encoding: %s",
        "Requirements not met: %s",
        "Conflict: %s",
        "Division by zero",
};
static char *terr_fatal[]={
	"Can't locate file: %s\n",
	"Out of memory\n",
	"Can't write object file: %s\n",
	"Line too long\n",
	"Can't write listing file: %s\n",
	"Can't write label file: %s\n",
	"%s\n",
	"File recursion\n",
	"Macro recursion too deep\n",
        "Unknown CPU: %s\n"
};

void err_msg(unsigned char no, char* prm) {
    char line[linelength];
    struct sfilenamelist *b=filenamelist->next, *b2=filenamelist;
    char *p;

    snprintf(line,linelength,"%s:%ld: ",&filenamelist->name,sline);
    adderror(line);

    while (b) {
        snprintf(line,linelength,"(%s:%ld) ",&b->name,b2->line);
        adderror(line);
        b2=b;
        b=b->next;
    }

    if (no<0x40) {
        if (arguments.warning) {
            snprintf(line,linelength,"warning: %s",(no==ERROR_WUSER_DEFINED)?prm:terr_warning[no]);
            warnings++;
	}
    }
    else if (no<0x80) {
        if (no==ERROR____PAGE_ERROR) {
            snprintf(line,linelength,"Page error at $%06lx",l_address);
            conderrors++;
        }
        else {
            snprintf(line,linelength,terr_error[no & 63],prm);
            if (no==ERROR_BRANCH_TOOFAR) conderrors++;
            else errors++;
        }
    }
    else {
        adderror("[**Fatal**] ");
        snprintf(line,linelength,terr_fatal[no & 63],prm);
        if (no==ERROR__USER_DEFINED) conderrors++; else
        {
            adderror(line);
            errors++;
            status();exit(1);
        }
    }

    adderror(line);
    p=pline;
    while (*p==0x20) p++;

    snprintf(line,linelength," \"%s\"\n",p);
    adderror(line);

    if (errors==100) {adderror("Too many errors\n"); status(); exit(1);}
}

//----------------------------------------------------------------------
#ifndef WIN32
struct ize {char *name;};
void freetree(void *a)
{
    free(((struct ize *)a)->name);
    free(a);
}

void freemacrotree(void *a)
{
    free(((struct smacro *)a)->name);
    free(((struct smacro *)a)->file);
    free(a);
}

void freefiletree(void *a)
{
    if (((struct sfile *)a)->f) fclose(((struct sfile *)a)->f);
}

int label_compare(const void *aa,const void *bb)
{
    return strcmp(((struct ize *)aa)->name,((struct ize *)bb)->name);
}

int file_compare(const void *aa,const void *bb)
{
    return ((long)((struct sfile *)aa)->f)-((long)((struct sfile *)bb)->f);
}
#endif

#ifndef WIN32
struct slabel* find_label(char* name) {
    struct slabel **b;
    struct ize a;
    a.name=name;
    if (!(b=tfind(&a,&label_tree,label_compare))) return NULL;
    return *b;
}
#else
struct slabel* find_label(char* name) {
    struct slabel *b=label_tree;
    int i;
    
    while (b) {
        i=strcmp(b->name,name);
        if (!i) return b;
        if (i<0) b=b->kis; else b=b->nagy;
    }
    
    return NULL;
}
#endif

// ---------------------------------------------------------------------------
#ifndef WIN32
struct slabel* lastlb=NULL;
struct slabel* new_label(char* name) {
    struct slabel **b;
    if (!lastlb)
	if (!(lastlb=malloc(sizeof(struct slabel)))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    lastlb->name=name;
    if (!(b=tsearch(lastlb,&label_tree,label_compare))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    if (*b==lastlb) { //new label
	if (!(lastlb->name=malloc(strlen(name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
        strcpy(lastlb->name,name);
	labelexists=0;lastlb=NULL;
    }
    else labelexists=1;
    return *b;            //already exists
}
#else
struct slabel* new_label(char* name) {
    struct slabel *b=label_tree;
    struct slabel *ob=NULL;
    int i;
    
    while (b) {
        i=strcmp(b->name,name);
	if (!i) {
	    labelexists=1;
	    return b;
	}
        ob=b;
        if (i<0) b=ob->kis; else b=ob->nagy;
    }
    
    if (!(b=malloc(sizeof (struct slabel)))) err_msg(ERROR_OUT_OF_MEMORY,NULL);

    if (!(b->name=malloc(strlen(name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    strcpy(b->name,name);
    labelexists=0;
    b->nagy=NULL;
    b->kis=NULL;
    if (!label_tree) label_tree=b; else {
        if (i<0) ob->kis=b; else ob->nagy=b;
    }
    return b;
}
#endif

// ---------------------------------------------------------------------------

#ifndef WIN32
struct smacro* find_macro(char* name) {
    struct smacro **b;
    struct ize a;
    a.name=name;
    if (!(b=tfind(&a,&macro_tree,label_compare))) return NULL;
    return *b;
}
#else
struct smacro* find_macro(char* name) {
    struct smacro *b=macro_tree;
    
    while (b) {
	if (!strcmp(b->name,name)) return b;
	b=b->next;
    }
    
    return NULL;
}
#endif

// ---------------------------------------------------------------------------
#ifndef WIN32
struct smacro* lastma=NULL;
struct smacro* new_macro(char* name) {
    struct smacro **b;
    if (!lastma)
	if (!(lastma=malloc(sizeof(struct smacro)))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    lastma->name=name;
    if (!(b=tsearch(lastma,&macro_tree,label_compare))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    if (*b==lastma) { //new label
	if (!(lastma->name=malloc(strlen(name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
        strcpy(lastma->name,name);
	if (!(lastma->file=malloc(strlen(&filenamelist->name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
        strcpy(lastma->file,&filenamelist->name);
	labelexists=0;lastma=NULL;
    }
    else labelexists=1;
    return *b;            //already exists
}
#else
struct smacro* new_macro(char* name) {
    struct smacro *b=macro_tree;
    struct smacro *ob=NULL;
    
    while (b) {
	if (!strcmp(b->name,name)) {
	    labelexists=1;
	    return b;
	}
	ob=b;
	b=ob->next;
    }
    
    b=malloc(sizeof (struct smacro));

    if (!(b->name=malloc(strlen(name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    strcpy(b->name,name);
    if (!(b->file=malloc(strlen(&filenamelist->name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    strcpy(b->file,&filenamelist->name);
    labelexists=0;
    b->next=NULL;
    if (!macro_tree) macro_tree=b; else ob->next=b;
    return b;
}
#endif
// ---------------------------------------------------------------------------

#ifndef WIN32
struct sfile* lastfi=NULL;
FILE* openfile(char* name,char* volt) {
    struct sfile **b;
    if (!lastfi)
	if (!(lastfi=malloc(sizeof(struct sfile)))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    lastfi->name=name;
    if (!(b=tsearch(lastfi,&file_tree1,label_compare))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    if (*b==lastfi) { //new label
	if (!(lastfi->name=malloc(strlen(name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
        strcpy(lastfi->name,name);
	lastfi->f=fopen(name,"rb");
	if (!(b=tsearch(lastfi,&file_tree2,file_compare))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
	lastfi=NULL;
        *volt=0;
        (*b)->num=curfnum++;
    } else *volt=(*b)->open;
    (*b)->open=1;
    reffile=(*b)->num;
    return (*b)->f;
}
#else
FILE* openfile(char* name,char* volt) {
    struct sfile *b=file_tree1;
    struct sfile *ob=NULL;
    
    while (b) {
	if (!strcmp(b->name,name)) {
	    *volt=b->open;
            b->open=1;
            reffile=b->num;
	    return b->f;
	}
	ob=b;
	b=ob->next;
    }
    b=malloc(sizeof (struct sfile));

    if (!(b->name=malloc(strlen(name)+1))) err_msg(ERROR_OUT_OF_MEMORY,NULL);
    strcpy(b->name,name);
    b->f=fopen(name,"rb");
    *volt=0;
    b->open=1;
    b->num=curfnum++;
    if (!file_tree1) file_tree1=b; else ob->next=b;
    b->next=NULL;
    reffile=b->num;
    return b->f;
}
#endif

void closefile(FILE* f) {
#ifndef WIN32
    struct sfile **b,a;
    a.f=f;
    if (!(b=tfind(&a,&file_tree2,file_compare))) return;
    (*b)->open=0;
#else
    struct sfile *b=file_tree1;

    while (b) {
	if (!memcmp(&b->f,&f,sizeof (f))) {
	    b->open=0;
	    return;
	}
	b=b->next;
    }
    return;
#endif
}

#ifndef WIN32
void tfree() {
    tdestroy(label_tree,freetree);
    tdestroy(macro_tree,freemacrotree);
    tdestroy(file_tree1,freefiletree);
    tdestroy(file_tree2,freetree);
}
#else
void rlabeltree(struct slabel *a) {
    void *old;
    while (a) {
	free(a->name);
        old=a;
        rlabeltree(a->nagy);
	a=a->kis;
	free(old);
    }
}

void tfree() {
    struct smacro *b=macro_tree;
    struct sfile *c=file_tree1;
    void *old;

    rlabeltree(label_tree);

    while (b) {
	free(b->name);
	free(b->file);
	old=b;
	b=b->next;
	free(old);
    }

    while (c) {
	free(c->name);
	if (c->f) fclose(c->f);
	old=c;
	c=c->next;
	free(old);
    }
}
#endif

#ifdef WIN32
typedef enum
{
  preorder,
  postorder,
  endorder,
  leaf
}
VISIT;
#endif

FILE *flab;
void kiir(const void *aa,VISIT value,int level)
{
    long val;
    if (value==leaf || value==postorder) {
#ifdef VICE
	fprintf(flab,"al %04lx .l%s\n",(*(struct slabel **)aa)->value,(*(struct slabel **)aa)->name);
#else
        if (strchr((*(struct slabel **)aa)->name,'-') ||
            strchr((*(struct slabel **)aa)->name,'+')) return;
        if (strchr((*(struct slabel **)aa)->name,'.')) fputc(';',flab);
	fprintf(flab,"%-16s= ",(*(struct slabel **)aa)->name);
	if ((val=(*(struct slabel **)aa)->value)<0) fprintf(flab,"-");
	val=(val>=0?val:-val);
	if (val<0x100) fprintf(flab,"$%02lx",val);
	else if (val<0x10000l) fprintf(flab,"$%04lx",val);
	else if (val<0x1000000l) fprintf(flab,"$%06lx",val);
	else fprintf(flab,"$%08lx",val);
        if (!(*(struct slabel **)aa)->used) {
            if (val<0x100) fprintf(flab,"  ");
            if (val<0x10000l) fprintf(flab,"  ");
            if (val<0x1000000l) fprintf(flab,"  ");
            fprintf(flab,"; *** unused");
        }
        fprintf(flab,"\n");
#endif
    }
}

#ifndef WIN32
void labelprint() {
    if (arguments.label) {
	if (!(flab=fopen(arguments.label,"wt"))) err_msg(ERROR_CANT_DUMP_LBL,arguments.label);
        twalk(label_tree,kiir);
	fclose(flab);
    }
}
#else
void plabeltree(struct slabel *a) {
    while (a) {
        plabeltree(a->nagy);
        kiir(&a,leaf,postorder);
	a=a->kis;
    }
}

void labelprint() {
    if (arguments.label) {
	if (!(flab=fopen(arguments.label,"wt"))) err_msg(ERROR_CANT_DUMP_LBL,arguments.label);

        plabeltree(label_tree);

	fclose(flab);
    }
}
#endif

// ------------------------------------------------------------------
#ifndef WIN32
const char *argp_program_version="6502/65C02/65816/CPU64/DTV TASM 1.45";
const char *argp_program_bug_address="<soci@c64.rulez.org>";
const char doc[]="64tass Turbo Assembler Macro";
const char args_doc[]="SOURCE";
const struct argp_option options[]={
    {"no-warn"	, 	'w',		0,     	0,  "Suppress warnings"},
    {"nonlinear",	'n',		0,     	0,  "Generate nonlinear output file"},
    {"nostart" 	,	'b',		0,     	0,  "Strip starting address"},
    {"wordstart",	'W',		0,     	0,  "Force 2 byte start address"},
    {"ascii" 	,	'a',		0,     	0,  "Convert ASCII to PETASCII"},
    {"case-sensitive",	'C',		0,     	0,  "Case sensitive labels"},
    {		0,	'o',"<file>"	,      	0,  "Place output into <file>"},
    {		0,	'D',"<label>=<value>",     	0,  "Define <label> to <value>"},
    {"long-branch",	'B',		0,     	0,  "Automatic bxx *+3 jmp $xxxx"},
    {		0,  	0,		0,     	0,  "Target selection:"},
    {"m65xx"  	,     	1,		0,     	0,  "Standard 65xx (default)"},
    {"m6502"  	,     	'i',		0,     	0,  "NMOS 65xx"},
    {"m65c02"  	,     	'c',		0,     	0,  "CMOS 65C02"},
    {"m65816"  	,     	'x',		0,     	0,  "W65C816"},
    {"mcpu64" 	,     	'X',		0,     	0,  "CPU64"},
    {"m65dtv02"	,     	't',		0,     	0,  "65DTV02"},
    {		0,  	0,		0,     	0,  "Source listing:"},
    {"labels"	,	'l',"<file>"	,      	0,  "List labels into <file>"},
    {"list"	,	'L',"<file>"	,      	0,  "List into <file>"},
    {"no-monitor",	'm',		0,      0,  "Don't put monitor code into listing"},
    {"no-source",	's',		0,      0,  "Don't put source code into listing"},
    {		0,  	0,		0,     	0,  "Misc:"},
    { 0 }
};

static error_t parse_opt (int key,char *arg,struct argp_state *state)
{
    switch (key)
    {
    case 'w':arguments.warning=0;break;
    case 'W':arguments.wordstart=0;break;
    case 'n':arguments.nonlinear=1;break;
    case 'b':arguments.stripstart=1;break;
    case 'a':arguments.toascii=1;break;
    case 'o':arguments.output=arg;break;
    case 'D':
    {
	struct slabel* tmp;
	int i=0;
	while (arg[i] && arg[i]!='=') {
            if (!arguments.casesensitive) arg[i]=lowcase(arg[i]);
	    i++;
	}
	if (arg[i]=='=') {
            arg[i]=0;
            tmp=new_label(arg);tmp->proclabel=0;
            tmp->requires=0;
            tmp->conflicts=0;
	    tmp->ertelmes=1;tmp->value=atoi(&arg[i+1]);
	}
	break;
    }
    case 'B':arguments.longbranch=1;break;
    case 1:arguments.cpumode=OPCODES_6502;break;
    case 'i':arguments.cpumode=OPCODES_6502i;break;
    case 'c':arguments.cpumode=OPCODES_65C02;break;
    case 'x':arguments.cpumode=OPCODES_65816;break;
    case 'X':arguments.cpumode=OPCODES_CPU64;break;
    case 't':arguments.cpumode=OPCODES_65DTV02;break;
    case 'l':arguments.label=arg;break;
    case 'L':arguments.list=arg;break;
    case 'm':arguments.monitor=0;break;
    case 's':arguments.source=0;break;
    case 'C':arguments.casesensitive=1;break;
    case ARGP_KEY_ARG:if (state->arg_num) argp_usage(state);arguments.input=arg;break;
    case ARGP_KEY_END:if (!state->arg_num) argp_usage(state);break;
    default:return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

const struct argp argp={options,parse_opt,args_doc,doc};

void testarg(int argc,char *argv[]) {
    argp_parse(&argp,argc,argv,0,0,&arguments);
}
#else
void testarg(int argc,char *argv[]) {
    int j,out=0;
    for (j=1;j<argc;j++)
    {
	if (!strcmp(argv[j],"-?") || !strcmp(argv[j],"--help")) {
	    printf(
		"Usage: 64tass [OPTION...] SOURCE\n"
		"64tass Turbo Assembler Macro\n"
		"\n"
		"  -a, --ascii\t\t     Convert ASCII to PETASCII\n"
		"  -b, --nostart\t\t     Strip starting address\n"
		"  -B, --long-branch\t     Automatic bxx *+3 jmp $xxxx\n"
		"  -C, --case-sensitive\t     Case sensitive labels\n"
		"  -D <label>=<value>\t     Define <label> to <value>\n"
		"  -n, --nonlinear\t     Generate nonlinear output file\n"
		"  -o <file>\t\t     Place output into <file>\n"
		"  -w, --no-warn\t\t     Suppress warnings\n"
		"  -W, --wordstart\t     Force 2 byte start address\n"
		"\n"
		" Target selection:\n"
		"  -c, --m65c02\t\t     CMOS 65C02\n"
		"  -i, --m6502\t\t     NMOS 65xx\n"
		"      --m65xx\t\t     Standard 65xx (default)\n"
		"  -t, --m65dtv02\t     65DTV02\n"
		"  -x, --m65816\t\t     W65C816\n"
		"  -X, --mcpu64\t\t     CPU64\n"
		"\n"
		" Source listing:\n"
		"  -l <file>\t\t     List labels into <file>\n"
		"  -L <file>\t\t     List into <file>\n"
		"  -m, --no-monitor\t     Don't put monitor code into listing\n"
		"  -s, --no-source\t     Don't put source code into listing\n"
		"\n"	      
		" Misc:\n"
		"\n"
		"  -?, --help\t\t     Give this help list\n"
		"      --usage\t\t     Give a short usage message\n"
		"  -V, --version\t\t     Print program version\n"
		"\n"
		"Mandatory or optional arguments to long options are also mandatory or optional\n"
		"for any corresponding short options.\n"
		"\n"
		"Report bugs to <soci@c64.rulez.org>.\n");
	    exit(1);
	}
	if (!strcmp(argv[j],"--usage")) {
	    printf(
		"Usage: 64tass [-abBCnwWcitxXms?V] [-D <label>=<value>] [-o <file>] [-l <file>]\n"
		"\t    [-L <file>] [--ascii] [--nostart] [--long-branch]\n"
		"\t    [--case-sensitive] [--nonlinear] [--no-warn] [--wordstart]\n"
		"\t    [--m65c02] [--m6502] [--m65xx] [--m65dtv02] [--m65816] [--mcpu64]\n"
		"\t    [--no-monitor] [--no-source] [--help] [--usage] [--version] SOURCE\n");
	    exit(1);
	}
	if (!strcmp(argv[j],"-V") || !strcmp(argv[j],"--version")) {
	    printf("6502/65C02/65816/CPU64/DTV TASM 1.45\n");
	    exit(1);
	}
	if (!strcmp(argv[j],"-w") || !strcmp(argv[j],"--no-warn")) {arguments.warning=0;continue;}
	if (!strcmp(argv[j],"-W") || !strcmp(argv[j],"--wordstart")) {arguments.wordstart=0;continue;}
	if (!strcmp(argv[j],"-n") || !strcmp(argv[j],"--nonlinear")) {arguments.nonlinear=1;continue;}
	if (!strcmp(argv[j],"-b") || !strcmp(argv[j],"--nostart")) {arguments.stripstart=1;continue;}
	if (!strcmp(argv[j],"-a") || !strcmp(argv[j],"--ascii")) {arguments.toascii=1;continue;}
	if (!strcmp(argv[j],"-B") || !strcmp(argv[j],"--long-branch")) {arguments.longbranch=1;continue;}
        if (!strcmp(argv[j],"--m65xx")) {arguments.cpumode=OPCODES_6502;continue;}
        if (!strcmp(argv[j],"-i") || !strcmp(argv[j],"--m6502")) {arguments.cpumode=OPCODES_6502i;continue;}
        if (!strcmp(argv[j],"-c") || !strcmp(argv[j],"--m65c02")) {arguments.cpumode=OPCODES_65C02;continue;}
        if (!strcmp(argv[j],"-x") || !strcmp(argv[j],"--m65816")) {arguments.cpumode=OPCODES_65816;continue;}
        if (!strcmp(argv[j],"-X") || !strcmp(argv[j],"--mcpu64")) {arguments.cpumode=OPCODES_CPU64;continue;}
        if (!strcmp(argv[j],"-t") || !strcmp(argv[j],"--m65dtv02")) {arguments.cpumode=OPCODES_65DTV02;continue;}
	if (!strcmp(argv[j],"-l")) {
	    j++;if (j>=argc) goto ide2;
	    if (arguments.label) goto ide3;
	    arguments.label=malloc(strlen(argv[j])+1);
	    strcpy(arguments.label,argv[j]);
	    continue;
	}
	if (!strcmp(argv[j],"-L")) {
	    j++;if (j>=argc) goto ide2;
	    if (arguments.list) goto ide3;
	    arguments.list=malloc(strlen(argv[j])+1);
	    strcpy(arguments.list,argv[j]);
	    continue;
	}
	if (!strcmp(argv[j],"-m") || !strcmp(argv[j],"--no-monitor")) {arguments.monitor=0;continue;}
	if (!strcmp(argv[j],"-s") || !strcmp(argv[j],"--no-source")) {arguments.source=0;continue;}
	if (!strcmp(argv[j],"-C") || !strcmp(argv[j],"--case-sensitive")) {arguments.casesensitive=1;continue;}
        if (!strcmp(argv[j],"-o")) {
	    j++;if (j>=argc) goto ide2;
	    if (out) goto ide3;
	    arguments.output=malloc(strlen(argv[j])+1);
	    strcpy(arguments.output,argv[j]);
	    out=1;
	    continue;
	}
        if (!strcmp(argv[j],"-D")) {
	    struct slabel* tmp;
	    int i=0;
	    j++;if (j>=argc) {
		ide2:
		printf("64tass: option requires an argument -- %c\n",argv[j-1][1]);
		goto ide;
	    }

	    while (argv[j][i] && argv[j][i]!='=') {
        	if (!arguments.casesensitive) argv[j][i]=lowcase(argv[j][i]);
		i++;
	    }
	    if (argv[j][i]=='=') {
        	argv[j][i]=0;
                tmp=new_label(argv[j]);tmp->proclabel=0;
                tmp->requires=0;
                tmp->conflicts=0;
		tmp->ertelmes=1;tmp->value=atoi(&argv[j][i+1]);
	    }
	    continue;
	    }
	if (arguments.input) goto ide3;
	arguments.input=malloc(strlen(argv[j])+1);
	strcpy(arguments.input,argv[j]);
    }
    if (!arguments.input) {
	ide3:
	printf("Usage: 64tass [OPTION...] SOURCE\n");
	ide:
	printf("Try `64tass --help' or `64tass --usage' for more information.\n");
        if (arguments.list) free(arguments.list);
        if (arguments.label) free(arguments.label);
	if (arguments.input) free(arguments.input);
	if (out) free(arguments.output);
	exit(1);
    }
}
#endif
