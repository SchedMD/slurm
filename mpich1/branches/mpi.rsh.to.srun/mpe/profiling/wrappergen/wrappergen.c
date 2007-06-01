#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "wrappergen.h"

#ifndef DEBUG
#define DEBUG 0
#endif

#define RETURN_VAR_NAME "returnVal"


void WriteWrappers( outf, wrapperFiles, nwrapperFiles, fn_list, n_fn )
FILE *outf;
char **wrapperFiles;
fn_def *fn_list;
int n_fn, nwrapperFiles;
{
  int fn_num, argNum, fileNum, i;
  fn_def fn;
  wrapperinfo winfo;

  ListCreate( winfo.wrapperDefs, wrapperdef, 10 );

  for (fn_num=0; fn_num<n_fn; fn_num++)
    ListCreate( fn_list[fn_num].wrapperdefs, int, 2 );

#if DEBUG
  for (i=0; i<nwrapperFiles; i++) {
    printf( "wrapper file: %s\n", wrapperFiles[i] );
  }

  for (i=0; i<n_fn; i++) {
    printf( ":%s: :%s:\n", fn_list[i].returnType, fn_list[i].name );
    for (argNum=0; argNum<fn_list[i].nargs; argNum++) {
      printf( "Arg[%d] :%s: :%s: :%s:\n", argNum,
	      fn_list[i].argTypePrefix[argNum],
	      fn_list[i].argNames[argNum],
	      fn_list[i].argTypeSuffix[argNum] );
    }
    putchar( '\n' );
  }
#endif

  for (fileNum=0; fileNum<nwrapperFiles; fileNum++) {
    ReadWrapperFile( outf, wrapperFiles[fileNum], fileNum,
		     fn_list, n_fn, &winfo );
    /* read all wrapper definitions and write external info */
  }


  WriteFunctionCalls( outf, fn_list, n_fn, &winfo );

  return;
}


void ReadWrapperFile( outf, fileName, filenum, fn_list, n_fn, winfo )
FILE *outf;
char *fileName;
fn_def *fn_list;
int n_fn;
wrapperinfo *winfo;
{
  FILE *inf;
  fileinfo finfo;
  rpcinfo rinfo;
  replacement rpc;
  char *tmpStr;

  if (!(inf = fopen( fileName, "r" ))) {
    fprintf( stderr, "Cannot open wrapper definition file \"%s\".\n",
	     fileName );
    return;
  }

  finfo.str = ReadFileIntoString( inf );
  finfo.name = fileName;
  finfo.filenum = filenum;
  finfo.lineno = 1;
  rinfo.fn_list = fn_list;
  rinfo.n_fn = n_fn;
  ListCreate( rinfo.rpc, replacement, 5 );
  
  /* create replacement for the file number */
  rpc.from = "fileno";
  rpc.to = (char *)malloc( 10 );
  sprintf( rpc.to, "%d", filenum );
  ListAddItem( rinfo.rpc, replacement, rpc );
  
  ProcessString( outf, &finfo, &rinfo, winfo );

}



void ProcessString( outf, finfo, rinfo, winfo )
FILE *outf;
fileinfo *finfo;
rpcinfo *rinfo;
wrapperinfo *winfo;
{
  int escBodyLen, i, startingLine;
  char **escBodyList, *escBody, *ptr, *preceding;

#if DEBUG
  fprintf( stderr, "Process String %s in file %s (%d) on line %d\n",
	   finfo->str, finfo->name, finfo->filenum, finfo->lineno );
#endif

  while (ReadUntilEscape( finfo, &preceding,
			  &escBodyList, &escBodyLen,
			  &escBody, &startingLine )) {
#if DEBUG>1
    printf( "Escape on line %d.  body is:\n", startingLine );
    for (i=0; i<escBodyLen; i++) {
      printf( "  %s\n", escBodyList[i] );
    }
    printf( "Or:%s:\n", escBody );
#endif

    fprintf( outf, "%s", preceding );
    ProcessEscape( outf, finfo, rinfo, winfo, escBodyList,
		   escBodyLen, escBody, startingLine );

    free( escBodyList );
    free( escBody );
    free( preceding );
    preceding = 0;
  }

  if (preceding)
    fprintf( outf, "%s", preceding );

  return;
}

static int fn_num = 0;

void ProcessEscape( outf, finfo, rinfo, winfo,
		    escBodyList,
		    escBodyLen, escBody,
		    startingLine )
FILE *outf;
fileinfo *finfo;
rpcinfo *rinfo;
wrapperinfo *winfo;
int escBodyLen, startingLine;
char **escBodyList, *escBody;
{
  char c, repNo, format[20];
  char *body, *varName, *space;
  xpandList fnNames;
  int strRead;

  /* if the excape sequence ("{{...") does not start with an
     identifier or the identifier is not recognized, assume it's
     not prof_wrapper stuff, and spit it out */

  /* if null body, just copy it out and return */
  if (!escBodyLen) {
    fprintf( outf, "%s", escBody );
    return;
  }

#if DEBUG>1
    fprintf( stderr, "command is '%s' on line %d\n", escBodyList[0],
	     startingLine );
#endif


  /* check for simple replacement */
  for (repNo=0; repNo<ListSize( rinfo->rpc, replacement );
       repNo++) {
    if (!strcmp( escBodyList[0],
		 ListItem( rinfo->rpc, replacement, repNo).from )) {
      /* successful replacement */
#if DEBUG
      fprintf( stderr, "replacing with '%s'\n",
	       ListItem( rinfo->rpc, replacement, repNo).to );
#endif
      fprintf( outf, "%s", ListItem( rinfo->rpc, replacement, repNo).to );
      return;
    }
  }

  if (!strcmp( escBodyList[0], "foreachfn" )) {
    if (escBodyLen>2) {
      /* if a variable name was found (syntax check) */
      
      if (ReadUntilMatch( finfo, "foreachfn", "endforeachfn", &body,
			  startingLine ))
	DoForEach( outf, finfo, rinfo, escBodyList, escBodyLen, escBody,
		   startingLine, body );

    } else { /* if valid syntax */
      fprintf( stderr, "(file %s, line %d) foreachfn needs a function name \
replacement string and at least one function name.\n", finfo->name,
	       startingLine );
    }

  } else if (!strcmp( escBodyList[0], "forallfn" )) {
    if (escBodyLen>1) {
      /* if a variable name was found (syntax check) */
      if (ReadUntilMatch( finfo, "forallfn",
			  "endforallfn", &body, startingLine ))
	DoForAll( outf, finfo, rinfo, escBodyList, escBodyLen, escBody,
		  startingLine, body );

    } else { /* if valid syntax */
      fprintf( stderr, "(file %s, line %d) forallfn needs a function name \
replacement string.\n", finfo->name, startingLine );
    }


    /* if winfo is NULL, ProcessString has been called within
       a function wrapper, and thus a function wrapper should not
       be encountered */

  } else if (!strcmp( escBodyList[0], "fnall" )) {
    if (!winfo) {
      fprintf( stderr,
	  "Illegal nested function definition in file %s, line %d.\n",
	       finfo->name, startingLine );
    } else if (escBodyLen>1) {  /* syntax check */
      if (ReadUntilMatch( finfo, "fnall", "endfnall", &body, startingLine )) {
	DoFnAll( finfo, rinfo, winfo, escBodyList, escBodyLen, body,
		 startingLine );
      }
    } else { /* if valid syntax */
      fprintf( stderr, "(file %s, line %d) fnall needs a function name \
replacement string.\n", finfo->name, startingLine );
    }
	  
  } else if (!strcmp( escBodyList[0], "fn" )) {
    if (!winfo) {
      fprintf( stderr,
	  "Illegal nested function definition in file %s, line %d.\n",
	      finfo->name, startingLine );
    } else if (escBodyLen>2) {
      if (ReadUntilMatch( finfo, "fn", "endfn", &body, startingLine )) {
	DoFn( finfo, rinfo, winfo, escBodyList, escBodyLen, body,
	      startingLine );
      }
    } else {
      fprintf( stderr, "(file %s, line %d) fn needs a function name \
replacement string and at least one function name.\n",
	       finfo->name, startingLine );
    }
  } else if (!strcmp( escBodyList[0], "fn_num" )) {
      fprintf( outf, "%d", fn_num++ );
  } else {
    fprintf( stderr, "Unrecognized escape '%s' in file %s, line %d.\n",
	     escBody, finfo->name, startingLine );
    fprintf( outf, "%s", escBody );
  }

}




int ReadUntilEscape( finfo, preceding,
		     escBodyList,
		     escBodyLen, escBodyLiteral,
		     startingLine )
fileinfo *finfo;
char ***escBodyList, **escBodyLiteral, **preceding;
int *escBodyLen, *startingLine;
{
  /* anything read before reaching an escape sequence is returned in
     'preceding' */

  xpandList escBodyChars, escBodyIdx, escBodyLit;
  char *escBegin, *escEnd;
  char c;
  int escapeFound, tmpInt;

  /* first job, read until {{ is found */

#if DEBUG>1
  fprintf( stderr, "Reading until escape at line %d\n", finfo->lineno );
#endif

  escBegin = strstr( finfo->str, "{{" );

  /* {{}} escape not found */
  if (!escBegin) {
    finfo->lineno += CountNewlines( finfo->str,
				    finfo->str+strlen(finfo->str) );
    *preceding = STRDUP( finfo->str );
  } else {
  
    *startingLine = finfo->lineno += CountNewlines( finfo->str, escBegin );

    *preceding = (char *)malloc( escBegin - finfo->str + 1 );
    strncpy( *preceding, finfo->str, escBegin - finfo->str );
    (*preceding)[escBegin-finfo->str] = '\0';

    escEnd = strstr( escBegin+2, "}}" );

    /* }} not found */
    if (!escEnd) {
      finfo->lineno += CountNewlines( escBegin, escBegin+strlen( escBegin ) );
      fprintf( stderr, "No matching '}}' for '{{' in file %s, line %d.\n",
	      finfo->name, *startingLine );
    } else {


      /* count the lines in the body */
      finfo->lineno += CountNewlines( escBegin, escEnd );

      /* make copy of the body */
      *escBodyLiteral = (char *)malloc( escEnd+2 - escBegin + 1 );
      strncpy( *escBodyLiteral, escBegin, escEnd+2 - escBegin );
      (*escBodyLiteral)[escEnd+2 - escBegin] = '\0';
      
      /* break up the string into words, inserting \0 here and there */
      c = *escEnd;
      *escEnd = '\0';
      ListizeString( escBegin+2, escBodyList, escBodyLen );
      *escEnd = c;
      
      finfo->str = escEnd+2;
      return 1;
    }
  }

  return 0;
}


int CountNewlines( ptr, end )
char *ptr, *end;
{
  int n;
  n = 0;
  while (ptr<end) {
    if (*ptr == '\n') n++;
    ptr++;
  }

  return n;
}



/* chop a string up into a list of whitespace separated elements */
void ListizeString( str, list, len )
char *str, ***list;
int *len;
{
  char *ptr, *copy;
  int i;
  xpandList strList;

    /* create list of strings */
  ListCreate( strList, char *, 5 );

    /* make a copy of the string */
  copy = STRDUP( str );

    /* break the copy up into whitespace delimited strings */
  ptr = strtok( copy, " \n\t" );
  while (ptr) {
    ListAddItem( strList, char *, ptr );
    ptr = strtok( (char *)0, " \n\t" );
  }

  ListClose( strList, char *, *list, *len );

#if DEBUG>1
  fprintf( stderr,"broke %s up into:\n", str );
  for (i=0; i<*len; i++) fprintf( stderr, "  %s\n", (*list)[i] );
#endif
  return;
}




  /* returns 1 if found match */
int ReadUntilMatch( finfo, start, end, escbody, initialLine )
fileinfo *finfo;
int initialLine;
char  *start, *end, **escbody;
{
  char **bodyList, *bodyLit, *preceding;
  int stackLevel, i, bodyLen, reachedEnd, startingLine;
  xpandList body;

  stackLevel = 1;
  ListCreate( body, char, 100 );
  reachedEnd = 0;

#if DEBUG>1
  fprintf( stderr, "ReadUntilMatch looking for %s to match %s\n",
	   end, start );
#endif

  while (!reachedEnd && ReadUntilEscape( finfo, &preceding,
					 &bodyList, &bodyLen,
					 &bodyLit, &startingLine )) {

    /* save the stuff before the escape was reached */
    for (i=0; preceding[i]; i++)
      ListAddItem( body, char, preceding[i] );

    /* if the body is non-null */
    if (bodyLen) {

#if DEBUG
      fprintf( stderr, "ReadUntilmatch hit :%s:\n", bodyList[0] );
#endif

	/* go one level deeper; found {{foreach}} within {{foreach}} */
      if (!strcmp( bodyList[0], start )) {
#if DEBUG>1
	fprintf( stderr, "level deeper...\n" );
#endif
	stackLevel++;
	for (i=0; bodyLit[i]; i++)
	  ListAddItem( body, char, bodyLit[i] );

      } else if (!strcmp( bodyList[0], end )) {
#if DEBUG>1
	fprintf( stderr, "level higher...\n" );
#endif
	stackLevel--;
	if (!stackLevel) {
	  reachedEnd = 1;
	} else {
	  for (i=0; bodyLit[i]; i++)
	    ListAddItem( body, char, bodyLit[i] );
	}
	
	/* unrecognized escape, doesn't matter */
      } else {
	for (i=0; bodyLit[i]; i++)
	  ListAddItem( body, char, bodyLit[i] );
      }
      
      /* null body, add directly */
    } else {
      for (i=0; bodyLit[i]; i++)
	ListAddItem( body, char, bodyLit[i] );
    }

    free( bodyList );  free( bodyLit );  free( preceding );

  }

  if (reachedEnd) {
    ListAddItem( body, char, '\0' );
    ListClose( body, char, *escbody, i );
  } else {
    fprintf( stderr, "No matching '%s' for '%s' in file %s, line %d.\n",
	     end, start, finfo->name, initialLine );
    ListDestroy( body, char );
  }

  return reachedEnd;
}



void DoForEach( outf, finfo, rinfo, bodyList, bodyLen, escBody,
	        startLine, body )
FILE *outf;
fileinfo *finfo;
rpcinfo *rinfo;
char *body, **bodyList, *escBody;
int  bodyLen, startLine;
{
  int i, fn_num;
  replacement rpc;
  fileinfo my_finfo;

#if DEBUG>1
  printf( "For each occurrence of %s in :%s:, replace with:\n",
	  bodyList[1], body );
  for (i=2; i<bodyLen; i++) {
    printf( "%s\n", bodyList[i] );
  }
#endif

  rpc.from = bodyList[1];
  my_finfo.name = finfo->name;
  my_finfo.filenum = finfo->filenum;

  for (i=2; i<bodyLen; i++) {
    my_finfo.lineno = startLine;
    my_finfo.str = body;
    /* StrToLower( bodyList[i] ); */
    fn_num = IsFnInList( bodyList[i], rinfo->fn_list, rinfo->n_fn );
    if (fn_num != -1) {
      rpc.to = rinfo->fn_list[fn_num].name;
      ListAddItem( rinfo->rpc, replacement, rpc );
#if DEBUG
      fprintf( stderr, "foreach replacement of %s with %s.\n",
	       bodyList[1], rinfo->fn_list[fn_num].name );
#endif

      /* wrappers should not be defined within a foreach or forall */
      ProcessString( outf, &my_finfo, rinfo, (wrapperinfo *)0 );

      ListRemoveItems( rinfo->rpc, replacement, 1 );
    }
  }
}



void DoForAll( outf, finfo, rinfo, bodyList, bodyLen, escBody, startLine,
	       body )
FILE *outf;
fileinfo *finfo;
rpcinfo *rinfo;
char *body, **bodyList, *escBody;
int  bodyLen, startLine;
{
  int i, fn_num;
  replacement rpc;
  fileinfo my_finfo;

#if DEBUG>1
  printf( "For each occurrence of %s in :%s:, replace with:\n",
	  bodyList[1], body );
  for (i=2; i<bodyLen; i++) {
    printf( "%s\n", bodyList[i] );
  }
#endif

  rpc.from = bodyList[1];
  my_finfo.name = finfo->name;
  my_finfo.filenum = finfo->filenum;

  for (fn_num=0; fn_num < rinfo->n_fn; fn_num++) {
      /* don't expand for the functions named */
    if (!IsNameInList( rinfo->fn_list[fn_num].name, bodyList+2, bodyLen-2 )) {
      my_finfo.lineno = startLine;
      my_finfo.str = body;
      rpc.to = rinfo->fn_list[fn_num].name;
      ListAddItem( rinfo->rpc, replacement, rpc );
#if DEBUG
      fprintf( stderr, "foreach replacement of %s with %s.\n",
	      bodyList[1], rinfo->fn_list[fn_num].name );
#endif

      /* wrappers should not be defined within a foreach or forall */
      ProcessString( outf, &my_finfo, rinfo, (wrapperinfo *)0 );
      
      ListRemoveItems( rinfo->rpc, replacement, 1 );
    } else {
#if DEBUG
      fprintf( stderr, "Don't foreach replace %s.\n",
	      rinfo->fn_list[fn_num].name );
#endif
    }
  }
}





void DoFnAll( finfo, rinfo, winfo, argv, argc, body, startLine )
fileinfo *finfo;
rpcinfo *rinfo;
wrapperinfo *winfo;
char *body, **argv;
int argc, startLine;
{
#if DEBUG
  fprintf( stderr, "fnall :%s:\n", body );
#endif

  ReadFnDef( finfo, rinfo, winfo, argv, argc, body, startLine, 1 );
}


void DoFn( finfo, rinfo, winfo, argv, argc, body, startLine )
fileinfo *finfo;
rpcinfo *rinfo;
wrapperinfo *winfo;
char *body, **argv;
int argc, startLine;
{
#if DEBUG
  fprintf( stderr, "fn :%s:\n", body );
#endif

  ReadFnDef( finfo, rinfo, winfo, argv, argc, body, startLine, 0 );
}


void ReadFnDef( finfo, rinfo, winfo, argv, argc, body, startLine, allFn )
fileinfo *finfo;
rpcinfo *rinfo;
wrapperinfo *winfo;
char *body, **argv;
int argc, startLine, allFn;
{
  int i, fn_num, lineNo, escLen, escStartLine;
  replacement rpc;
  char *bodyCopy, *escBody, **escList, *preceding,
       *prefixEnd, *suffixStart;
  wrapperdef newWrapper;
  variable newVar;
  xpandList vars, varStr;
  fileinfo my_finfo;

  lineNo = startLine;
  bodyCopy = body;
  ListCreate( vars, variable, 5 );
  ListCreate( varStr, char, 100 );

  newWrapper.prefixCode = newWrapper.suffixCode = 0;
  newWrapper.prefixLineNo = startLine;
  newWrapper.finfo = *finfo;
  newWrapper.firstLine = startLine;
    /* copies over the filename, file number */
  
  my_finfo = *finfo;
  my_finfo.str = body;
  my_finfo.lineno = startLine;

  while (ReadUntilEscape( &my_finfo, &preceding, &escList, &escLen, &escBody,
			  &escStartLine )) {
    for (i=0; preceding[i]; i++)
      ListAddItem( varStr, char, preceding[i] );

    if (escLen) {

      /* if variable declaration found */
      if (!strcmp( escList[0], "vardecl" )) {

	ReadVardecl( finfo, escStartLine, escBody, winfo, vars );

	/* if the {{callfn}} escape, */
      } else if (!strcmp( escList[0], "callfn" )) {

	  /* make sure there aren't more than one calls */
	if (!newWrapper.prefixCode) {
	    /* close out the preceding string */
	  ListAddItem( varStr, char, '\0' );
	    /* close out the preceding stuff */
	  ListClose( varStr, char, newWrapper.prefixCode, i );
	    /* open up new list for the suffix code */
	  ListCreate( varStr, char, 100 );
	  newWrapper.suffixLineNo = escStartLine;
	} else {
	  fprintf( stderr, "multiple {{callfn}} in file %s, line %d\n",
		   finfo->name, escStartLine );
	}

	/* unrecognized escape, probably an argument or variable name */
	/* just copy it out directly */
      } else {
	for (i=0; escBody[i]; i++)
	  ListAddItem( varStr, char, escBody[i] );
      }
    }
  }  /* while */

  for (i=0; preceding[i]; i++)
    ListAddItem( varStr, char, preceding[i] );

    /* close out the code copy */
  ListAddItem( varStr, char, '\0' );
  if (newWrapper.prefixCode) {
    ListClose( varStr, char, newWrapper.suffixCode, i );
  } else {
    fprintf( stderr, "warning: no {{callfn}} in wrapper definition starting \
inf file %s at line %d.\n", finfo->name, startLine );
    ListClose( varStr, char, newWrapper.prefixCode, i );
  }

  ListClose( vars, variable, newWrapper.vars, newWrapper.nvars );
  newWrapper.nameEscape = argv[1];

  if (allFn) {
    for (fn_num=0; fn_num<rinfo->n_fn; fn_num++) {
        /* keep out exclusions */
      if (!IsNameInList( rinfo->fn_list[fn_num].name, argv+2, argc-2 )) {
	ListAddItem( rinfo->fn_list[fn_num].wrapperdefs, int,
		     ListSize( winfo->wrapperDefs, wrapperdef ) );
      }
    }
  } else {
    for (i=2; i<argc; i++) {
      /* StrToLower( argv[i] ); */
      fn_num = IsFnInList( argv[i], rinfo->fn_list, rinfo->n_fn );
      if (fn_num != -1) {
	ListAddItem( rinfo->fn_list[fn_num].wrapperdefs, int,
		    ListSize( winfo->wrapperDefs, wrapperdef ) );
      }
    }
  }

  ListAddItem( winfo->wrapperDefs, wrapperdef, newWrapper );

}



void ReadVardecl( finfo, startLine, body, winfo, vars )
fileinfo *finfo;
char *body;
wrapperinfo *winfo;
xpandList vars;
int startLine;
{
  char *basetype, *readPt, *varPrefix, *varName, *varSuffix;
  variable newVar;

  /* skip "vardecl", read the base type ("static unsigned long"),
     and return a pointer to just after the base type */
  if (ReadVardeclBasetype( finfo->name, startLine, body,
			   &basetype, &readPt )) {

    while (ReadVardeclVarname( &readPt, &varPrefix, &varName,
			       &varSuffix )) {
      newVar.rqName = varName;
        /* one extra for '\0', one for the space between
	   basetype and varPrefix */
      newVar.typePrefix = (char *)malloc( strlen( basetype ) +
					  strlen( varPrefix ) + 2 );
      sprintf( newVar.typePrefix, "%s %s", basetype, varPrefix );
      free( varPrefix );
      newVar.typeSuffix = varSuffix;
      ListAddItem( vars, variable, newVar );
    }
  }
}


int ReadVardeclBasetype( filename, lineno, body, basetype, end)
char *body, **basetype, **end, *filename;
int lineno;
{
  char *typeEnd1, *typeEnd2, *ptr, *typeStart;
  int readingIdent;

  ptr = body+2;			/* skip over "{{" */
  body[strlen(body)-2] = '\0';	/* chop off "}}" */
  while (isspace(*ptr)) ptr++;

  if (strncmp( ptr, "vardecl", 7)) {
    /* doesn't start with vardecl? */
    fprintf( stderr, "'vardecl' syntax error (no vardecl?) in %s, line %d.\n",
	     filename, lineno );
    return 0;
  }
  ptr += 7;
  if (!isspace(*ptr)) {
    /* no space following vardecl */
    fprintf( stderr, "'vardecl' syntax error (no space?) in %s, line %d.\n",
	     filename, lineno );
    return 0;
  }

  /* skip to the good stuff */
  while (isspace(*ptr)) ptr++;
  typeEnd1 = typeEnd2 = 0;
  typeStart = ptr;
  readingIdent = 0;

  while (*ptr!=0 && *ptr!=',') {
    if (readingIdent && !(isalnum(*ptr) || *ptr=='_')) {
      /* if end of an identifier */
      typeEnd2 = typeEnd1;
      typeEnd1 = ptr;
    }
    readingIdent =  (isalnum(*ptr) || *ptr=='_');
    ptr++;
  }
  if (readingIdent) {
    typeEnd2 = typeEnd1;
    typeEnd1 = ptr;
  }

  if (!typeEnd2) {
    fprintf( stderr, "(%d)No variable names found in vardecl in %s, line %d.\n",
	     __LINE__, filename, lineno );
    return 0;
  }
  STR_RANGE_DUP( *basetype, typeStart, typeEnd2 );
  *end = typeEnd2;

#if DEBUG
  fprintf( stderr, "ReadVardeclBasetype: '%s'\n", *basetype );
#endif

  return 1;
}
  

int ReadVardeclVarname( readPt, varPrefix,
		        varName, varSuffix )
char **readPt, **varPrefix, **varName, **varSuffix;
{
  char *start;

  while (**readPt && (isspace( **readPt ) || **readPt==',')) {
    (*readPt)++;
  }
  if (!**readPt) return 0;

  start = *readPt;	/* start prefix */

    /* read until first letter: the variable name */
  while (**readPt && !isalpha(**readPt)) (*readPt)++;

  if (!**readPt) return 0;
  
  STR_RANGE_DUP( *varPrefix, start, *readPt );

  start = *readPt;

    /* read until first non letter, digit, or _ : suffix */
  while (**readPt && (isalnum(**readPt) || **readPt=='_')) (*readPt)++;
  
  STR_RANGE_DUP( *varName, start, *readPt );

  start = *readPt;
  
    /* read until first comma or end of string */
  while (**readPt && **readPt!=',') (*readPt)++;
  
  STR_RANGE_DUP( *varSuffix, start, *readPt );

#if DEBUG
  fprintf( stderr, "ReadVardeclVarname: :%s:%s:%s:\n",
	   *varPrefix, *varName, *varSuffix );
#endif
  /* ? return ? */
  return 1;
}


char *ReadFileIntoString( inf )
FILE *inf;
{
  char *str, *ptr;
  int size, c;
  struct stat filestat;

  fstat( fileno( inf ), &filestat );

  size = filestat.st_size;

  str = (char *) malloc( (size+1) * sizeof( char ) );

  if (!str) {
    fprintf( stderr,
	 "Not enough free memory to read in wrapper definition file.\n" );
    return 0;
  }

  ptr = str;
  while ( (c = getc( inf )) != EOF) *ptr++ = c;
  *--ptr = '\0';

  return str;
}


int IsFnInList( fn, fn_list, n_fn )
char *fn;
fn_def *fn_list;
int n_fn;
{
  int i;
  for (i=0; i<n_fn; i++) {
    if (!strcmp( fn, fn_list[i].name )) return i;
  }
  return -1;
}


int IsReservedName( name )
char *name;
{
  if (!strcmp( name, RETURN_VAR_NAME )) return 1;
  if (!strcmp( name, "fileno" )) return 1;
  return 0;
}



void WriteFunctionCalls( outf, fn_list, n_fn, winfo )
FILE *outf;
fn_def *fn_list;
int n_fn;
wrapperinfo *winfo;
{
  int i, j, w, v, wrapperNum, nwpr, nvar;
  char ***uniqueVarLists;
  wrapperdef *wpr;

  wpr = ListHeadPtr( winfo->wrapperDefs, wrapperdef );
  nwpr = ListSize( winfo->wrapperDefs, wrapperdef );

#if DEBUG
  fprintf( stderr, "\n\nWRITE FUNCTION CALLS\n\n" );

  fprintf( stderr, "Wrappers: \n" );
  for (i=0; i<nwpr; i++) {
    fprintf( stderr, "Wrapper %d\n", i );
    fprintf( stderr, "nameEscape %s\n",wpr[i].nameEscape );
    fprintf( stderr, "Vars:\n" );
    for (j=0; j<wpr[i].nvars; j++)
      fprintf( stderr, "  %s%s%s\n", wpr[i].vars[j].typePrefix,
	       wpr[i].vars[j].rqName, wpr[i].vars[j].typeSuffix );
    fprintf( stderr, "prefix code :%s:\n", wpr[i].prefixCode );
    fprintf( stderr, "suffix code :%s:\n", wpr[i].suffixCode );
  }

  fprintf( stderr, "Functions: \n" );
  for (i=0; i<n_fn; i++) {
    fprintf( stderr, "Function: %s (%s)\n",
	     fn_list[i].name, fn_list[i].returnType );
    for (j=0; j<fn_list[i].nargs; j++)
      fprintf( stderr, "arg: %s %s%s;\n", fn_list[i].argTypePrefix[j],
	       fn_list[i].argNames[j], fn_list[i].argTypeSuffix[j] );
    for (j=0; j<ListSize( fn_list[i].wrapperdefs, int ); j++)
      fprintf( stderr, "uses wrapper %d\n",
	       ListItem( fn_list[i].wrapperdefs, int, j ) );
  }
#endif

  uniqueVarLists = CreateUniqueVarNames( wpr, nwpr );
  

  for (i=0; i<n_fn; i++) {

#if DEBUG
    fprintf( stderr, "Profiling %s\n", fn_list[i].name );
#endif

    if (ListSize( fn_list[i].wrapperdefs, int )) {
        /* print function return type and name */
      fprintf( outf, "\n%s %s( ", fn_list[i].returnType, fn_list[i].name);

      if (oldstyle_function()) {
	  /* Pre-prototype C */
	  if (fn_list[i].nargs)	/* print first arg */
	      fprintf( outf, "%s", fn_list[i].argNames[0] );

	  /* print comma separated args */
	  for (j=1; j<fn_list[i].nargs; j++)
	      fprintf( outf, ", %s", fn_list[i].argNames[j] );
	  fprintf( outf, " )\n" );

	  /* print args and types */
	  for (j=0; j<fn_list[i].nargs; j++)
	      fprintf( outf, "%s %s%s;\n", fn_list[i].argTypePrefix[j],
		       fn_list[i].argNames[j], fn_list[i].argTypeSuffix[j] );
      }
      else {
	  /* print args and types */
	  for (j=0; j<fn_list[i].nargs; j++)
	      fprintf( outf, "%s %s%s%c", fn_list[i].argTypePrefix[j],
		       fn_list[i].argNames[j], fn_list[i].argTypeSuffix[j], 
		       (j<fn_list[i].nargs-1)? ',' : ' ' );
	  fprintf( outf, " )\n" );
      }
        /* declare return type */
      fprintf( outf, "{\n  %s %s;\n", fn_list[i].returnType,
	       RETURN_VAR_NAME );
      
        /* declare variables for each wrapper */
      for (w=0; w<ListSize( fn_list[i].wrapperdefs, int ); w++) {
	wrapperNum = ListItem( fn_list[i].wrapperdefs, int, w );
	for (v=0; v<wpr[wrapperNum].nvars; v++) {
	  fprintf( outf, "  %s%s%s;\n",
		   wpr[wrapperNum].vars[v].typePrefix,
		   uniqueVarLists[wrapperNum][v],
		   wpr[wrapperNum].vars[v].typeSuffix );
	}
      }

      PrintWrapperCode( outf, fn_list, n_fn, winfo,
		        uniqueVarLists, i, 0 );

        /* return */
      fprintf( outf, "\n  return %s;\n}\n", RETURN_VAR_NAME );
    }
  }
}


char ***CreateUniqueVarNames( wrapperList, nwrappers )
wrapperdef *wrapperList;
int nwrappers;
{
  int wrapperNum, varNum, varLen, uniqueLevel;
  char *tmpStr, ***uniqueNames;

  /* allocate space for pointers to each list */
  uniqueNames = (char ***)malloc( sizeof( char **)*nwrappers );

  for (wrapperNum=0; wrapperNum<nwrappers; wrapperNum++) {

    /* allocate space for each list */
    uniqueNames[wrapperNum] = (char **)malloc( sizeof( char *)*
					   wrapperList[wrapperNum].nvars );
    for (varNum=0; varNum<wrapperList[wrapperNum].nvars; varNum++) {
      tmpStr = wrapperList[wrapperNum].vars[varNum].rqName;
      varLen = strlen( tmpStr );
      uniqueLevel = 0;		/* original name, not uniquified */
      
      /* search through all the variables that have been uniquified so far */
      while (!IsUnique( tmpStr, wrapperList, nwrappers, uniqueNames,
		        wrapperNum )) {

	/* if this guy matches someone, change name and try again */
	if (uniqueLevel) free( tmpStr );
	uniqueLevel++;
	tmpStr = (char *)malloc( sizeof( char )*(varLen+5) );
	sprintf( tmpStr, "%s%d", wrapperList[wrapperNum].vars[varNum].
		 rqName, uniqueLevel );
      }
      uniqueNames[wrapperNum][varNum] = tmpStr;
#if DEBUG
      fprintf( stderr, "name for %s in wrapper %d is %s\n",
	       wrapperList[wrapperNum].vars[varNum].rqName,
	       wrapperNum, tmpStr );
#endif
    }
  }
  
  return uniqueNames;
}

int IsUnique( str, wrapperList, nwrappers, others, wrapperNum )
char *str;
char ***others;
int wrapperNum, nwrappers;
wrapperdef *wrapperList;
{
  int w, v;

  if (!strcmp( str, RETURN_VAR_NAME )) return 0;

  /* search through unique names created for previous wrappers */
  for (w=0; w<wrapperNum; w++) {
    for (v=0; v<wrapperList[w].nvars; v++) {
      if (!strcmp( str, others[w][v] )) return 0;
    }
  }
  return 1;
}


void PrintWrapperCode( outf, fn_list, n_fn, winfo, 
		       varNames, fn_num, wrapperNumIdx )
FILE *outf;
int n_fn, fn_num, wrapperNumIdx;
fn_def *fn_list;
wrapperinfo *winfo;
char ***varNames;
{
  rpcinfo rinfo;
  replacement rpc;
  int i, nvars, line, wrapperNum, nwpr;
  char *argNum;
  wrapperdef *wpr;
  fileinfo finfo;

  wpr = ListHeadPtr( winfo->wrapperDefs, wrapperdef ); 
  nwpr = ListSize( winfo->wrapperDefs, wrapperdef ); 

    /* set up replacement information (rpc list, function list) */
  ListCreate( rinfo.rpc, replacement, 10 );
  rinfo.fn_list = fn_list;
  rinfo.n_fn = n_fn;

  /* if this is one past the last wrapper, print the actual call */
  if (wrapperNumIdx==ListSize( fn_list[fn_num].wrapperdefs, int )) {
    fprintf( outf, "\n  %s = P%s( ", RETURN_VAR_NAME, fn_list[fn_num].name );
    if (fn_list[fn_num].nargs)
      fprintf( outf, "%s", fn_list[fn_num].argNames[0] );
    for (i=1; i<fn_list[fn_num].nargs; i++)
      fprintf( outf, ", %s", fn_list[fn_num].argNames[i] );
    fprintf( outf, " );\n" );
    return;
  }

  /* get the wrapper index from this functions description */
  wrapperNum = ListItem( fn_list[fn_num].wrapperdefs, int, wrapperNumIdx );
  nvars = wpr[wrapperNum].nvars;

#if DEBUG
  fprintf( stderr, "print wrapper %d (absolute %d)\n", wrapperNumIdx,
	   wrapperNum );
#endif

  /* Check if a variable used in a wrapper clashes with an argument
  ** name.  If so, complain, but the variable get precedence in the
  ** wrapper.  The argument can still be referenced by its index.
  */

  CheckForHiddenArgs( fn_list, fn_num, winfo, wrapperNum );

  /* set the replacement list to take care of variable, argument,
     file number, return variable, and function name replacements */

    /* replacements for variable names */
  for (i=0; i<nvars; i++ ) {
    rpc.from = wpr[wrapperNum].vars[i].rqName;
    rpc.to = varNames[wrapperNum][i];
#if DEBUG
    fprintf( stderr, "converting variable %s to %s\n", rpc.from,
	     rpc.to );
#endif
    ListAddItem( rinfo.rpc, replacement, rpc );
  }

    /* create replacements for argument names and indices */
    /* Note: indices start at 0, and 0 is not the name of the function */
  for (i=0; i<fn_list[fn_num].nargs; i++) {
    rpc.from = rpc.to = fn_list[fn_num].argNames[i];
    ListAddItem( rinfo.rpc, replacement, rpc );
    argNum = (char *)malloc( 10 );
    sprintf( argNum, "%d", i );
    rpc.from = argNum;
    rpc.to = fn_list[fn_num].argNames[i];
    ListAddItem( rinfo.rpc, replacement, rpc );
  }

    /* create replacement for function name */
  rpc.from = wpr[wrapperNum].nameEscape;
  rpc.to = fn_list[fn_num].name;
  ListAddItem( rinfo.rpc, replacement, rpc );

    /* create replacement for return variable */
  rpc.from = RETURN_VAR_NAME;
  rpc.to = RETURN_VAR_NAME;
  ListAddItem( rinfo.rpc, replacement, rpc );

    /* create replacement for filenum */
  rpc.from = "fileno";
  rpc.to = (char *)malloc( 10 );
  sprintf( rpc.to, "%d", wpr[wrapperNum].finfo.filenum );
  ListAddItem( rinfo.rpc, replacement, rpc );

#if DEBUG
  for (i=0; i<ListSize( rinfo.rpc, replacement ); i++) 
    fprintf( stderr, "replace '%s' with '%s'\n",
	     ListItem( rinfo.rpc, replacement, i ).from,
	     ListItem( rinfo.rpc, replacement, i ).to );
#endif

  wpr[wrapperNum].finfo.str = wpr[wrapperNum].prefixCode;
  wpr[wrapperNum].finfo.lineno = wpr[wrapperNum].prefixLineNo;
  ProcessString( outf, &wpr[wrapperNum].finfo, &rinfo, winfo );

  PrintWrapperCode( outf, fn_list, n_fn, winfo, 
		    varNames, fn_num, wrapperNumIdx+1 );

  if (wpr[wrapperNum].suffixCode) {
    wpr[wrapperNum].finfo.str = wpr[wrapperNum].suffixCode;
    wpr[wrapperNum].finfo.lineno = wpr[wrapperNum].suffixLineNo;
    ProcessString( outf, &wpr[wrapperNum].finfo, &rinfo, winfo );
  }

  return;
}


void CheckForHiddenArgs( fn_list, fn_num, winfo, wrapperNum )
fn_def *fn_list;
wrapperinfo *winfo;
int fn_num, wrapperNum;
{
  int argNum, varNum;
  wrapperdef *wpr;
  fn_def *fn;
  
  fn = fn_list + fn_num;
  wpr   = ListHeadPtr( winfo->wrapperDefs, wrapperdef ) + wrapperNum;

  for (argNum=0; argNum < fn_list[fn_num].nargs; argNum++) {
    for (varNum=0; varNum < wpr->nvars; varNum++ ) {
      if (!strcmp( fn->argNames[argNum], wpr->vars[varNum].rqName )) {
	fprintf( stderr, "Variable '%s' declared in the wrapper starting\n",
		 wpr->vars[varNum].rqName );
	fprintf( stderr, "on line %d in file %s hides argument %d in\n",
		 wpr->firstLine, wpr->finfo.name, argNum );
	fprintf( stderr, "the function %s.\n", fn->name );
      }
    }
  }
}
