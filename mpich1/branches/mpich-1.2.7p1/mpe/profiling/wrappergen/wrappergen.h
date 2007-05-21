#ifndef _WRAPPERGEN_H_
#define _WRAPPERGEN_H_

#include "expandingList.h"

#define MAX_IDENT_LEN 256

typedef struct fn_def_struct {
  char *name;
  char **argTypePrefix, **argTypeSuffix;
  char **argNames;
  int  nargs;
  char *returnType;
  xpandList wrapperdefs;
    /* list of integer indices pointing to the wrappers used on this fn */
} fn_def;

#ifndef STRDUP
#define STRDUP(str) strcpy( (char *) malloc( strlen( str ) + 1 ), (str) )
#endif

#ifndef STR_RANGE_DUP
#define STR_RANGE_DUP( cpy, start, end ) { \
  (cpy) = (char *)malloc( (end) - (start) + 1 ); \
  strncpy( (cpy), (start), (end) - (start) ); \
  (cpy)[(end) - (start)] = '\0'; }
#endif

typedef struct variable_ {
  char *typePrefix, *typeSuffix;
  char *rqName;  /* requested name */
} variable;

typedef struct fileinfo_ {
  char *name, *str;    /* str is the string to parse */
  int filenum, lineno;
    /* lineno should be set to the line number in the file that str
       starts on */
} fileinfo;

typedef struct wrapperdef_ {
  char *nameEscape, *prefixCode, *suffixCode;
  /* prefix/suffix code - code to go before/after the call */
  /* if no {{callfn}}, suffixCode will be null */
  variable *vars;
  int nvars, prefixLineNo, suffixLineNo, firstLine;
  fileinfo finfo;
    /* finfo - set *name and filenum so we know who to blame
       if this wrapper is goofy */
    /* when this wrapper is being written out, fill in lineno and string
       and just pass &winfo[i].finfo */
} wrapperdef;


typedef struct wrapperinfo_ {
  xpandList wrapperDefs;
} wrapperinfo;


typedef struct replacement_ {
  char *from, *to;
} replacement;


typedef struct rpcinfo_ {
  fn_def *fn_list;
  xpandList rpc;
  int n_fn;
} rpcinfo;

void WriteWrappers ( FILE *outf, char **wrapperFiles,
				 int nwrapperFiles, fn_def *fn_list,
				 int n_fn );

void ReadWrapperFile ( FILE *outf, char *fileName, int filenum,
				   fn_def *fn_list, int n_fn,
				   wrapperinfo *winfo );

char *ReadFileIntoString ( FILE *inf );


void ProcessString ( FILE *outf,
				 fileinfo *finfo,
				 rpcinfo *rinfo,
				 wrapperinfo *winfo );

/* either substitute with for{each,all}fn (outf), or define a new
   wrapper with fn[all] (winfo) */
/* escStartLine is set to the first line of the start of the escape */
void ProcessEscape ( FILE *outf,
				 fileinfo *finfo,
				 rpcinfo *rinfo,
				 wrapperinfo *winfo,
				 char **escBodyList, int escBodyLen,
				 char *escBody, int escStartLine );

/* finfo->lineno set to the first line of what is to be read */
/* is returned set to whatever is after what was read */
/* escStartLine tells what line the escape starts on */
int ReadUntilMatch ( fileinfo *finfo,
                                 char *start,
				 char *end, char **escbody,
				 int escStartLine );

int ReadUntilEscape ( fileinfo *finfo,
				  char **preceding, char ***escBodyList,
				  int *escBodyLen, char **escBodyLiteral,
				  int *escStartLine );

int CountNewlines ( char *start, char *end );

int RegisterVarType ( char *type, xpandList varTypes );

/* makes a copy of the string, and freeing will be difficult */
void ListizeString ( char *str, char ***list, int *len );

int IsReservedName ( char *name );

void OutChar ( int c, int where, void *outputForm );

void DoForEach ( FILE *outf,
			     fileinfo *finfo,
			     rpcinfo *rinfo,
			     char **argv,
			     int argc, char *escBody, int startLine,
			     char *body );

void DoForAll ( FILE *outf,
			    fileinfo *finfo,
			    rpcinfo *rinfo,
			    char **argv,
			    int argc, char *escBody, int startLine,
			    char *body );

void DoFn ( fileinfo *finfo,
		        rpcinfo *rinfo,
		        wrapperinfo *winfo,
		        char **argv,
			int argc,
                        char *body,
		        int startingLine );

void DoFnAll ( fileinfo *finfo,
			   rpcinfo *rinfo,
			   wrapperinfo *winfo,
			   char **argv,
			   int argc,
			   char *body,
			   int startingLine );

void ReadFnDef ( fileinfo *finfo,
			     rpcinfo *rinfo,
			     wrapperinfo *winfo,
			     char **argv,
			     int argc,
			     char *body,
			     int startingLine,
			     int allFn );

void WriteFunctionCalls ( FILE *outf,
				      fn_def *fn_list,
				      int n_fn,
				      wrapperinfo *winfo );

int IsFnInList ( char *fn, fn_def *fn_list, int n_fn );

char ***CreateUniqueVarNames ( wrapperdef *wrapperList,
					   int nwrappers );

void ReadVardecl ( fileinfo *finfo, int startLine,
			       char *body, wrapperinfo *winfo,
			       xpandList vars );

int ReadVardeclBasetype ( char *filename, int lineno,
				      char *body, char **basetype,
				      char **end );

int ReadVardeclVarname ( char **readPt, char **varPrefix,
				     char **varName, char **varSuffix );

void CheckForHiddenArgs ( fn_def *fn_list, int fn_num,
				      wrapperinfo *winfo, int wrapperNum );

int IsUnique ( char *str, wrapperdef* wrapperList, int nwrappers,
			   char ***others,
			   int wrapperNum );

void PrintWrapperCode ( FILE *outf,
				    fn_def *fn_list,
				    int n_fn,
				    wrapperinfo *winfo,
				    char ***varNames,
				    int fn_num,
				    int wrapperNumIdx );

#endif
