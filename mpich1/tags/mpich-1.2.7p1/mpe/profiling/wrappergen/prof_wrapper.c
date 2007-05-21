#include <stdio.h>
#include <string.h>
#include "expandingList.h"
#include "wrappergen.h"


#define DEBUG 0

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

static xpandList fn_list;
static char **wrapperdef_files, **todo_fn_list, *outfile;
static int num_todo_fn, num_wrapperdef_files;

/*
void StrToLower( str )
char *str;
{
  while (*str) {
    if (*str >= 'A' && *str <= 'Z') {
      *str = tolower( *str );
    }
    str++;
  }
}
*/


StoreFunctionInit( wrapperdef_files_, nwrapperdefs, fn_list_file, outfile_ )
char **wrapperdef_files_, *fn_list_file, *outfile_;
int nwrapperdefs;
{
  xpandList todo_fn;
  FILE *todo_file;
  char fn_name[50];

  ListCreate( fn_list, fn_def, 10 );
    /* initalize the list that will receive all the function prototypes */
  outfile = outfile_;
  wrapperdef_files = wrapperdef_files_;
  num_wrapperdef_files = nwrapperdefs;

  if (!fn_list_file) {
    num_todo_fn = -1;
  } else {
    /* make sure the function list file is readable */
    todo_file = fopen( fn_list_file, "r" );
    if (!todo_file) {
      fprintf( stderr, "Cannot open %s.  Exiting.\n", fn_list_file );
      exit( -1 );
    }
    
    /* create the list of function names */
    ListCreate( todo_fn, char *, 10 );
    while (!feof(todo_file)) {
      if (fscanf( todo_file, "%s", fn_name )) {
	/* StrToLower( fn_name ); */
#if DEBUG
	fprintf( stderr, "todo: %s\n", fn_name );
#endif
	ListAddItem( todo_fn, char *, STRDUP(fn_name) );
      }
    }
    ListClose( todo_fn, char *, todo_fn_list, num_todo_fn );
#if DEBUG
    {
      int i;
      
      fprintf( stderr, "Functions todo:\n" );
      if (
      for (i=0; i<num_todo_fn; i++) {
	fprintf( stderr, "  %s\n", todo_fn_list[i] );
      }
    }
#endif
  }
}


int IsNameInList( name, list, nitems )
char *name, **list;
int nitems;
{
  int i;
  /* char *nameCopy; */

  if (nitems==-1) return 1;

  /* nameCopy = STRDUP( name ); */
  /* StrToLower( nameCopy ); */
  for (i=0; i<nitems; i++) {
#if DEBUG>1
    fprintf( stderr, "Comparing %s and %s\n", name, list[i] );
#endif
    if (!strcmp( name, list[i] )) {
      /* free( nameCopy ); */
      return 1;
    }
  }
  /* free( nameCopy ); */
  return 0;
}



StoreFunction( name, args, nargs, types, rt )
     char *name;
     ARG_LIST *args;
     int nargs;
     TYPE_LIST *types;
     RETURN_TYPE *rt;
{
  fn_def new_fn;
  int argnum, i;
  char *typename;

#if DEBUG
  fprintf( stderr, "Got code for function: %s\n", name );
#endif
  if (!IsNameInList( name, todo_fn_list, num_todo_fn )) return;
    /* if this function is not on the todo list */
#if DEBUG
  fprintf( stderr, "It's in the list.\n" );
#endif
  for (i=0; i<ListSize( fn_list, fn_def ); i++) {
    if (!strcmp( name, ListItem( fn_list, fn_def, i ).name )) return;
  }  /* if this function has already been defined */
#if DEBUG
  fprintf( stderr, "It hasn't already been defined.\n" );
#endif

  new_fn.name = STRDUP( name );
  /* new_fn.lowerName = STRDUP( name ); */
  /* StrToLower( new_fn.lowerName ); */
  new_fn.nargs = nargs;
  new_fn.argNames = (char **) malloc( sizeof( char * ) * nargs );
  new_fn.argTypePrefix = (char **) malloc( sizeof( char * ) * nargs );
  new_fn.argTypeSuffix = (char **) malloc( sizeof( char * ) * nargs );
  for (argnum=0; argnum<nargs; argnum++) {
    new_fn.argNames[argnum] = STRDUP( args[argnum].name );
    typename = (char *) malloc( strlen( types[args[argnum].type].type ) +
			        3 );
    strcpy( typename, types[args[argnum].type].type);
    if (args[argnum].has_star) strcat( typename, " *" );
    new_fn.argTypePrefix[argnum] = typename;
    new_fn.argTypeSuffix[argnum] = "";
  }
  new_fn.returnType =(char *) malloc( strlen( rt->name ) + rt->num_stars+2 );
  strcpy( new_fn.returnType, rt->name );
  if (rt->num_stars) strcat( new_fn.returnType, " " );
  for (i=0; i<rt->num_stars; i++) {
    strcat( new_fn.returnType, "*" );
  }
  ListCreate( new_fn.wrapperdefs, wrapperdef, 2 );
    /* create list for inserting wrapperdef indices */

#if DEBUG
  fprintf( stderr, ":%s: :%s:\n", new_fn.returnType, new_fn.name );
  for (i=0; i<new_fn.nargs; i++) {
    fprintf( stderr, "Arg[%d] :%s: :%s:\n", i, new_fn.argTypes[i],
	    new_fn.argNames[i] );
  }
#endif
  ListAddItem( fn_list, fn_def, new_fn );
}
     
FunctionOutput()
{
  fn_def *functionList;
  int i, argNum, n_fn;
  FILE *outf;

  outf = fopen( outfile, "w" );
  if (!outf) {
    fprintf( stderr, "Could not write to %s.\n", outfile );
    return -1;
  }

  ListClose( fn_list, fn_def, functionList, n_fn );
  WriteWrappers( outf, wrapperdef_files, num_wrapperdef_files,
		 functionList, n_fn );
  free( functionList );
  fclose( outf );
}
