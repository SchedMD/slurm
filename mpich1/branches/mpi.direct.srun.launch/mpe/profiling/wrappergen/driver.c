#ifndef LINT
static char vcid[] = "$Id: driver.c,v 1.1.1.1 1997/09/17 20:38:46 gropp Exp $";
#endif

#include <ctype.h>
#include "tools.h"
#include "doc.h"
#include "expandingList.h"

/* 
   This is designed to work with comments in C programs.  
   It uses the standardized documentation to issue dummy routine
   definitions to allow the creation of a Fortran to C library.

   This version of "header.c" is a modification of the file taken 
   from "~gropp/tools.n/c2fort" on 10/7/93.  Modifications have been 
   introduced so that elements of type void * in the files "nlfunc_v.h", 
   and "nlspmat_v.h", and "nlsles_v.h" are translated as pointers to 
   structures in the Fortran version (instead of the default, which is
   no translation).  Note that pointers to void functions retain the 
   usual translation.  

   An additional flag (flag2) is used in the calling sequence of 
   ProcessArgDefs() to indicate the files which require the modified 
   translation.   Also, an additional element, void_function, is added 
   to the structure ARG_LIST to distinguish pointers to void functions 
   from pointers to void structures.  
 */
extern char GetSubClass();

static int NoFortMsgs = 1;
/* NoFortWarnings turns off messages about things not being available in
   Fortran */
static int NoFortWarnings = 1;

/* This says to convert char **a to int*a, and cast to (char **)*a */
static int MultipleIndirectAreInts    = 1;
static int MultipleIndirectsAreNative = 0;
/* static int MultipleIndirects */

/* Keep the file name to simplify finding files containing problems */
static char *CurrentFilename = 0;

/* Whether to replace pointers with indices to a mapping of pointers */
static int MapPointers = 0;

/* If this is set to true, "void *" are translated as pointers to structures */
/* NOT YET IMPLEMENTED */
static int TranslateVoidStar = 0;

/* If true, add a last integer argument to int functions and return its
   value in the last parameter */
static int useFerr = 0;

/* Enable the MPI definitions */
static int isMPI = 0;

/*D
  bfort - program to extract short definitions for a Fortran to C interface

  Input:
. filenames - Names the files from which lint definitions are to be extracted
. -nomsgs   - Do not generate messages for routines that can not be converted
              to Fortran.
. -nofort   - Generate messages for all routines/macros without a Fortran
              counterpart.
. -dir name - Directory for output file
. -I name   - file that contains common includes
. -mapptr   - translate pointers to integer indices
. -ferr     - Fortran versions return the value of the routine as the last
              argument (an integer).  This is used in MPI and is a not 
	      uncommon approach for handling error returns.
. -mpi      - Handle MPI datatypes (some things are pointers by definition)
. -mnative  - Multiple indirects are native datatypes (no coercion)

  Note:
  We really need a way to specify a general type as a pointer, so that it
  will be handled as a pointer.  The -mpi option is a kludge for a a pressing
  need.  Eventually should provide a "-ptr name" option and keep in a 
  search space when looking for known types.

  Author: 
  Bill Gropp
D*/
main( argc, argv )
int  argc;
char **argv;
{
char routine[MAX_ROUTINE_NAME];
char *infilename;
char outfilename[1024];
char dirname[1024];
char fname[1024], *p;
FILE *fd, *fout, *incfd;
int  nread;
char kind;
char incfile[MAX_FILE_SIZE];
char incbuffer[1024];
char prefix [100];
xpandList wrapperFiles;
int argnum;

ListCreate( wrapperFiles, char *, 5 );
while ((argnum = SYArgFindName( argc, argv, "-w" )) > 0) {
  ListAddItem( wrapperFiles, char *, argv[argnum+1] );
#if DEBUG
  fprintf( stderr, "Adding %s as a wrapper file.\n", argv[argnum+1] );
#endif
  argv[argnum] = argv[argnum+1] = 0;
  SYArgSqueeze( &argc, argv );
}
/* gather all the wrapper definition filenames */


/* process all of the files */
strcpy( dirname, "." );
incfile[0]  = 0;


if (SYArgHasName( &argc, argv, 1, "-h" ) || argc<3) {
  PrintHelp( argv );
}


StoreFunctionInit( ListHeadPtr( wrapperFiles, char * ),
		   ListSize( wrapperFiles, char * ),
		   argv[1], argv[2] ); argc-=3; argv+=3;
  /* get ready to start storing function information */
  /* argv[1] should be the function list file */
  /* argv[2] should be the output file */

while (argc--) {
    /* Input filename */
    infilename = *argv++;
    fd = fopen( infilename, "r" );
    if (!fd) {
      fprintf( stderr, "Could not open file %s\n", infilename );
      continue;
    } else
    /* Remember file name */
    CurrentFilename = infilename;

    /* Set the output filename */
    SYGetRelativePath( infilename, fname, 1024 );
    /* Strip the trailer */
    p = fname + strlen(fname) - 1;
    while (p > fname && *p != '.') p--;
    *p = 0;
    /* Add an extra h to include files */
    if (p[1] == 'h') {
	p[0] = 'h';
	p[1] = 0;
	}
    sprintf( outfilename, "%s/%s_prof.c", dirname, fname );
    /* Don't open the filename yet (wait until we know that we'll have
       some output for it) */
    fout = NULL;

    while (FoundLeader( fd, routine, &kind )) {
	/* We need this test first to avoid creating an empty file, 
	   particularly for initf.c */
	if (kind == ROUTINE) {
	    OutputRoutine( fd, fout, routine, infilename, kind, prefix );
	    }
	}
    fclose( fd );
    if (fout) {
	fclose( fout );
	}
    }
FunctionOutput();
return 0;
}



/* We also need to make some edits to the types occasionally.  First, note
   that double indirections are often bugs */
typedef struct {
    char *name;
    int  has_star, is_char, is_native, type, is_FILE, void_function;
    int  implied_star;
    } ARG_LIST;
typedef struct {
    char type[60];
    } TYPE_LIST;
typedef struct {
    char name[60];
    int  num_stars;
    } RETURN_TYPE;

OutputRoutine( fin, fout, name, filename, kind, prefix )
FILE *fin, *fout;
char *name, *filename, kind, *prefix;
{
int         is_function;
ARG_LIST    args[512];
TYPE_LIST   types[60];
RETURN_TYPE rt;
int         nargs, nstrings;
int         ntypes;
int         flag2 = 0;


/* Skip to trailer */
SkipText( fin, name, filename, kind );

/* Get the call to the routine, including finding the argument names */
SkipWhite( fin );

ProcessArgList( fin, fout, filename, &is_function, name, 
	        args, &nargs, &rt, 0 );

SkipWhite( fin );
ProcessArgDefs( fin, fout, args, nargs, types, &ntypes, &nstrings, 0, 
                name, flag2 );

StoreFunction( name, args, nargs, types, &rt );
}




/*
    This routine skips the text part of a text page.
 */        
SkipText( fin, name, filename, kind )
FILE *fin;
char *name, *filename;
char kind;
{
int  c;
char lineBuffer[MAX_LINE], *lp;
	
lineBuffer[0] = '+';   /* Sentinal on lineBuffer */
while (1) {
    lp = lineBuffer + 1;
    c  = getc( fin );
    if (c == EOF) break;
    if (c == ARGUMENT || c == VERBATIM)
	SkipLine( fin );
    else if (c == '\n')
	;
    else {
	if (isspace(c) && c != '\n')
	    SkipWhite( fin );
	else 
	    *lp++ = c;
    	/* Copy to end of line; do NOT include the EOL */
    	while ((c = getc( fin )) != EOF && c != '\n') 
    	    *lp++ = c;
    	lp--;
    	while (isspace(*lp)) lp--;
    	lp[1] = '\0';    /* Add the trailing null */
    	if (lineBuffer[1] == kind && strcmp(lineBuffer+2,"*/") == 0)
    	    break;
        }
    }
}




#if 0
/* Convert a string to lower case, in place */
LowerCase( s )
char *s;
{
char c;
while (*s) {
    c = *s;
    if (isascii(c) && isupper(c)) *s = tolower(c);
    s++;
    }
}
#endif


/* Find the next space delimited token; put the text into token.
   The number of leading spaces is kept in nsp.
   Alpha-numeric tokens are terminated by a non-alphanumeric character
   (_ is allowed in alpha-numeric tokens) */
int FindNextANToken( fd, token, nsp )
FILE *fd;
char *token;
int  *nsp;
{
int fc, c, Nsp;

Nsp = SkipWhite( fd );

fc = c = getc( fd );
if (fc == EOF) return fc;
*token++ = c;
if (isalnum(c) || c == '_') {
    while ((c = getc( fd )) != EOF) {
	if (c != '\n' && (isalnum(c) || c == '_')) *token++ = c;
	else break;
	}
    ungetc( (char)c, fd );
    }
*token++ = '\0';
*nsp     = Nsp;
return fc;
}




/* Read the arg list and function type */
ProcessArgList( fin, fout, filename, is_function, name, args, Nargs, rt, flag )
FILE        *fin, *fout;
char        *filename, *name;
ARG_LIST    args[512];
RETURN_TYPE *rt;
int         *Nargs;
int         *is_function, flag;
{
int             c;
char            token[1024], *p;
int             i, nsp, bl, leadingm;
static char     rcall[1024];
int             nargs, ln, in_args;
int             reading_function_type = 0;
int             found_name;

SkipWhite( fin );
nargs       = 0;
in_args     = 0;
p           = rcall;
c           = FindNextANToken( fin, p, &nsp );
/* 
   We check for routines that return (functions) versus ones that don't
   by looking for "void".  A special case is functions that return 
   pointers to void; we check for these by looking at the first character
   of the first token after the void.

   We also want to defer generating the function type incase we need to 
   replace a pointer ref with an integer.
 */
strcpy( rt->name, p );
rt->num_stars = 0;
*is_function          = strcmp( p, "void" );
reading_function_type = 1; /* !*is_function; */

p += strlen( p );
*p++ = ' ';
leadingm = 0;    /* If a newline is encountered before this is one, AND
                    this is a macro, insert one and exit */
found_name = 0;
while (1) {
    c = FindNextANToken( fin, p, &nsp );
    if (c == EOF) {
	fprintf( stderr, "Unexpected EOF in %s\n", filename );
	return;
	}
    if (reading_function_type) {
	if (nsp > 0) strcat( rt->name, " " );
	if (strcmp( p, name ) != 0 && p[0] != '(')
	    strcat( rt->name, p );
	if (c == '*') {
	    *is_function = 1;
	    rt->num_stars++;
	    }
	}
    if (flag && c == '\n' && leadingm == 0) {
	break;
	}
    if (c == '\n') leadingm = 1;
    if (c == '(') {
	reading_function_type = 0;
	in_args += 1;
	}
    if (c == ')') {
	in_args -= 1;
	if (in_args == 0) {
	    break;
	    }
	}
    if (in_args == 0) {
	if (strcmp( p, name ) == 0) {
	    /* Convert to Fortran name.  For now, this just does the
	       lowercase_ version */
	    found_name = 1;
	    }
	else {
	    if (p[0] != '*') 
		fprintf( stderr, "%s:Did not find matching name: %s != %s\n", 
			 filename, p, name );
	    }
	}
    if (in_args == 1) {
	if (c != ',' && c != '(' && c != '\n') {
	    /* Assume that it is a name and remember it */
	    args[nargs].name	     = p;
	    args[nargs].has_star     = 0;
	    args[nargs].implied_star = 0;
	    args[nargs].is_char	     = 0;
	    args[nargs].is_FILE	     = 0;
	    args[nargs].is_native    = 1;   /* Unspecified args are ints */
	    args[nargs].type	     = 0;
	    nargs++;
	    }
	}
    if (in_args) {
	p += strlen( p );
	*p++ = 0;
	}
    }

if (!found_name) {
    fprintf( stderr, "%s:Did not find routine name (may be untyped): %s \n", 
 	     filename, name );
    }

/* Handle definitions of the form "type (*Name( args, ... ))()" (this is
   function returns pointer to function returning type). */
SkipWhite( fin );
c = getc( fin );
if (c == '(') {
    SkipWhite( fin );
    c = getc(fin);
    if (c == ')') 
	fputs( "()", fout );
    else 
	ungetc( (char)c, fin );
    }
else 
    ungetc( (char)c, fin );

*Nargs = nargs;

}



/* if flag == 1, stop on empty line rather than { */
/* This needs to distinguish between pointers and values, since all
   parameters are passed by reference in Fortran.  Just to keep things
   lively, there are two ways to indicate a pointer in C:
     type *foo;
     type foo[];

   Needed to add a change that definitions are terminated by ;, not by \n.
 */
int ProcessArgDefs( fin, fout, args, nargs, types, Ntypes, Nstrings, flag, 
		    name, flag2 )
FILE      *fin, *fout;
ARG_LIST  *args;
int       nargs;
TYPE_LIST *types;
int       *Ntypes, *Nstrings, flag, flag2;
char      *name;
{
int      c;
char     token[1024], *p;
int      i, nsp, bl, newline, newstmt;
char     rcall[1024];
int      is_function, in_function;
int      ln, in_args, has_star, implied_star, n_strings, is_char, nstrings,
         is_native, has_array, is_FILE;
int      ntypes, set_void, void_function;
int      done = 0;         /* set to 1 if ate end-of-definition */
int      type_has_star;

newline		  = 1;
newstmt		  = 1;
if (flag) newline = 0;
has_star	  = 0;
type_has_star     = 0;
implied_star	  = 0;
has_array	  = 0;
is_char		  = 0;
is_FILE		  = 0;
nstrings	  = 0;
/* The default type is int */
ntypes		  = 1;
strcpy( types[0].type, "int" );
is_function	  = 0;
in_function	  = 0;
set_void	  = 0;
void_function	  = 0;
while (1) {
    c = FindNextANToken( fin, token, &nsp );
    if (c == EOF || token[0] == '{') break;
    /* Check for empty line; if found, exit.  Otherwise, 
       check for M * / (Macro definition) and handle that case */
    if (flag) {
	if (newline && c == '\n') break;
	if (c == MACRO) {
	    c = getc( fin );
	    if (c == '*') {
		c = getc( fin );
		if (c == '/') {
		    done = 1;
		    break;
		    }
		else { 
		    /* This won't work on all systems. */
		    ungetc( '*', fin );
		    ungetc( (char)c, fin );
		    }
		}
	    else 
		ungetc( (char)c, fin );
	    }
	}

    /* Don't output register */
    if (strcmp( token, "register" ) == 0) continue;
    /* Handle various argument features */
    if (c == '*')                  has_star++;
    else if (c == ',') {
	has_star = type_has_star; has_array = 0; in_function = 0; 
	is_function = 0; set_void = 0; void_function = 0; 
	/* implied_star = 0; */
	}
    else if (c == ';') {
	is_char  = 0; 	is_FILE  = 0; 	has_star = 0; implied_star = 0;
	has_array= 0;   is_native= 0;   is_function = 0; 
	in_function = 0;  set_void = 0; void_function = 0;
	type_has_star = 0;
	newstmt     = 1;
	}
    else if (c == '(') {
         in_function = 1;
         /* If set_void is activated, set the void function indicator */
         if (set_void) {        
            set_void = 0;
            void_function = 1;
            }
         }
    else if (c == ')' && in_function) {
	is_function = 1;
	}
    else if (c == '\n') {
	/* New lines have little meaning in declarations.
	   However, they are necessary to handle blanks lines */
	newline = 1;
	}
#ifdef OLD_CODE
    else if (c == '\n') {
	newline  = 1; 	is_char  = 0; 	is_FILE  = 0; 	has_star = 0;
	has_array= 0;   is_native= 0;   is_function = 0; implied_star = 0;
	in_function = 0;  set_void = 0; void_function = 0;
	}
#endif
    else if (newstmt) {
	if (strcmp( token, "char" ) == 0) is_char = 1;
	if (strcmp( token, "FILE" ) == 0) is_FILE = 1;
	is_native = 0;
	if (strcmp( token, "double" ) == 0 ||
	    strcmp( token, "int"    ) == 0 ||
	    strcmp( token, "float"  ) == 0 ||
	    strcmp( token, "char"   ) == 0 ||
	    strcmp( token, "complex") == 0 ||
	    strcmp( token, "dcomplex")== 0 ||
	    strcmp( token, "MPI_Status") == 0 ||
	    strcmp( token, "BCArrayPart") == 0)
	    is_native = 1;
	if (isMPI) {
	    /* Some things need to be considered ints in the declarations.
	       That is, these are "implicit" pointer objects; often they
	       are pointers to be returned from the calling routine. 
	       These tests set these up as being pointer objects */
	    if (strcmp( token, "MPI_Comm" ) == 0 ||
		strcmp( token, "MPI_Request" ) == 0 ||
		strcmp( token, "MPI_Group" ) == 0 || 
		strcmp( token, "MPI_Intercomm_request" ) == 0 ||    
		strcmp( token, "MPI_Op" ) == 0 ||    
		strcmp( token, "MPI_Datatype" ) == 0) {
 		has_star      = 1;
		type_has_star = 1;
		implied_star  = 1;
		}
	    }
	if (strcmp( token, "void"   ) == 0) {
           /* Activate set_void only for the files specified by flag2 */
           if (!flag2) is_native = 1;
           else set_void = 1;    
           }
	newline = 0;
	newstmt = 0;
	strcpy( types[ntypes].type, token );
	if (strcmp( token, "struct" ) == 0 || 
	    strcmp( token, "unsigned") == 0) {
	    /* Flush struct to the output */
	    for (i=0; i<nsp; i++) putc( ' ', fout );
	    fputs( token, fout );
	    c = FindNextANToken( fin, token, &nsp );
	    strcat( types[ntypes].type, " " );
	    strcat( types[ntypes].type, token );
	    }
	ntypes++;
	}
    else 
	in_function = 0;

    /* Check for "[]".  This won't work for [][3], for example */
    c = getc( fin );
    if (c == '[') {
	has_star++;
	while ((c = getc(fin)) != EOF && c != ']') ;
	has_array = 1;
	}
    else
	ungetc( c, fin );

    /* Look up name */
    for (i=0; i<nargs; i++) {
	if (strcmp( token, args[i].name ) == 0) {
	    args[i].has_star	  = has_star;
	    args[i].implied_star  = implied_star;
	    args[i].is_char	  = is_char;
	    args[i].is_FILE	  = is_FILE;
	    args[i].type	  = ntypes-1;
	    args[i].is_native	  = is_native;
	    args[i].void_function = void_function;
	    break;
	    }
	}
    }


*Ntypes   = ntypes;
*Nstrings = nstrings;


return done;
}


PrintBody( fout, is_function, name, nstrings, nargs, args, types, rt, prefix )
FILE        *fout;
char        *name, *prefix;
int         is_function, nstrings, nargs;
ARG_LIST    *args;
TYPE_LIST   *types;
RETURN_TYPE *rt;
{
int  i;
fprintf( fout, "Start of function\n" );
fprintf( fout, "%s %s( ", rt->name, name ); /* return type and function name */
if (nargs) {
  fprintf( fout, "%s", args[0].name );
}
for (i=1; i<nargs; i++) {
  fprintf( fout, ", %s", args[i].name ); /* argument names */
}
fprintf( fout, " )\n" );

for (i=0; i<nargs; i++) {
  fprintf( fout, "%s%s %s;\n",	/* argument definitions */
	  types[args[i].type].type, args[i].has_star ? " *" : "",
	  args[i].name);
}

fprintf( fout, "{\n" );

fprintf( fout, "    %s returnVal;\n\n", rt->name ); /* declare returnVal */
fprintf( fout, "    returnVal = %s%s( ", prefix, name );
  /* start function call */
if (nargs) {
  fprintf( fout, "%s", args[0].name );
}
for (i=1; i<nargs; i++) {
  fprintf( fout, ", %s", args[i].name ); /* function arguments */
}
fprintf( fout, " );\n\n" );
fprintf( fout, "    return returnVal;\n" ); /* return returnVal */
fprintf( fout, "}\n\n" );	/* close the function definition */

fprintf( fout, "End of function\n" );

}
