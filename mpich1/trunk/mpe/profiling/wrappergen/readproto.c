#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "args.h"
#include "expandingList.h"
#include "wrappergen.h"

/*
#define PROTO_FILE "/home/karrels/wrappers/mpi_proto"
*/

#ifndef DEBUG
#define DEBUG 0
#endif

int PrintHelp();

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

/* search for 'searchChar', overwrite it with '\0', and return pointer to
** just after the lask searchChar that was \0'd.
*/

char *my_strtok( str, searchChar )
char *str, searchChar;
{
  static char *ptr;

  if (str) {
    ptr = str;
  } else {
    str = ptr;
  }
  
  while (*ptr && *ptr != searchChar) ptr++;
  *ptr = '\0';
  ptr++;

  return str;
}


int main( argc, argv )
int argc;
char *argv[];
{
  char *proto_file, *proto_file_name, *filename, *fn_list_file;
  char *output_file_name, **fn_names;
  xpandList wrapperFiles;
  int n_fn_names, n_fn;
  fn_def *fn_list;
  FILE *outf;

  if (IsArgPresent( &argc, argv, "-h" )) PrintHelp();

  ListCreate( wrapperFiles, char *, 3 );

  while (GetStringArg( &argc, argv, "-w", &filename ))
    ListAddItem( wrapperFiles, char *, filename );

  if (!ListSize( wrapperFiles, char *)) {
    fprintf( stderr, "No wrapper files specified.  Quitting.\n" );
    return 1;
  }

  if (!GetStringArg( &argc, argv, "-f", &fn_list_file )) {
    fprintf( stderr,
	     "No function list file.  Assuming all functions profiled.\n" );
    fn_list_file = 0;
  }

  if (!GetStringArg( &argc, argv, "-p", &proto_file_name )) {
    fprintf( stderr, "No function prototype file.  Assuming %s.\n",
	     PROTO_FILE );
    proto_file_name = PROTO_FILE;
  }
  
  if (!GetStringArg( &argc, argv, "-o", &output_file_name )) {
    fprintf( stderr, "No output file.  Assuming standard out.\n" );
    outf = stdout;
  } else {
    outf = fopen( output_file_name, "w" );
    if (!outf) {
      fprintf( stderr, "Could not open %s.  Quitting.\n",
	       output_file_name );
    }
  }
  
  if (ReadFnList( fn_list_file, &fn_names, &n_fn_names )) {
    if (ReadFnProto( proto_file_name, fn_names, n_fn_names, &fn_list,
		     &n_fn )) {
      WriteWrappers( outf, ListHeadPtr( wrapperFiles, char *),
		     ListSize( wrapperFiles, char *), fn_list, n_fn );
    }
  }
  fclose( outf );
  return 0;
}


int ReadFnProto( fn, fn_names, n_fn_names, fn_list, n_fn )
char *fn, **fn_names;
int n_fn_names, *n_fn;
fn_def **fn_list;
{
  int fn_num, argnum, i;
  FILE *inf;
  char *filestr, *ptr;
  
  inf = fopen( fn, "r" );
  if (!inf) {
    fprintf( stderr, "Could not read %s.  Quitting.\n", fn );
    return 0;
  }

  filestr = ReadFileIntoString( inf );
  
  ptr = my_strtok( filestr, '\n' );
  sscanf( ptr, "%d", n_fn );

#if DEBUG
  fprintf( stderr, "%d functions\n", *n_fn );
#endif

  *fn_list = (fn_def *)malloc( sizeof( fn_def ) * *n_fn );

  fn_num = 0;
  for (i=0; i < *n_fn; i++) {
      /* goto function type */
    ptr = my_strtok( (char *)0, '\n' );
    (*fn_list)[fn_num].returnType = ptr;

      /* goto function name */
    ptr = my_strtok( (char *)0, '\n' );
    (*fn_list)[fn_num].name = ptr;

    if (IsNameInList( ptr, fn_names, n_fn_names )) {

      /* (*fn_list)[fn_num].lowerName = STRDUP( ptr ); */
      /* StrToLower( (*fn_list)[fn_num].lowerName ); */
      
        /* read # of args */
      ptr = my_strtok( (char *)0, '\n' );
      sscanf( ptr, "%d", &(*fn_list)[fn_num].nargs );
      
#if DEBUG
      fprintf( stderr, "%s %d args\n",
	      (*fn_list)[fn_num].name,
	      (*fn_list)[fn_num].nargs );
#endif
      
      (*fn_list)[fn_num].argTypePrefix =
	(char **)malloc( (*fn_list)[fn_num].nargs * sizeof( char * ) );
      (*fn_list)[fn_num].argTypeSuffix =
	(char **)malloc( (*fn_list)[fn_num].nargs * sizeof( char * ) );
      (*fn_list)[fn_num].argNames =
	(char **)malloc( (*fn_list)[fn_num].nargs * sizeof( char * ) );
      
      /* read args */
      for (argnum=0; argnum < (*fn_list)[fn_num].nargs; argnum++) {
        /* skip whitespace */
	ptr = my_strtok( (char *)0, '\n' );
	(*fn_list)[fn_num].argTypePrefix[argnum] = ptr;
	ptr = my_strtok( (char *)0, '\n' );
	(*fn_list)[fn_num].argNames[argnum] = ptr;
	ptr = my_strtok( (char *)0, '\n' );
	(*fn_list)[fn_num].argTypeSuffix[argnum] = ptr;
#if DEBUG
	fprintf( stderr, "Arg %d: %s %s%s;\n", argnum,
		(*fn_list)[fn_num].argTypePrefix[argnum],
		(*fn_list)[fn_num].argNames[argnum],
		(*fn_list)[fn_num].argTypeSuffix[argnum] );
#endif
      }
      fn_num++;
    } else {

      /* just skip over everything */

        /* read # of args */
      ptr = my_strtok( (char *)0, '\n' );
      sscanf( ptr, "%d", &(*fn_list)[fn_num].nargs );
      
      /* read args */
      for (argnum=0; argnum < (*fn_list)[fn_num].nargs; argnum++) {
        /* skip whitespace */
	ptr = my_strtok( (char *)0, '\n' );
	ptr = my_strtok( (char *)0, '\n' );
	ptr = my_strtok( (char *)0, '\n' );
      }
    }

  }
  *n_fn = fn_num;
}

int ReadFnList( fn, fn_names, n_fn )
char *fn, ***fn_names;
int *n_fn;
{
  FILE *inf;
  xpandList fnlist;
  char *filestr, *ptr;;

  if (!fn) {
    *n_fn = -1;
    return 1;
  }

  inf = fopen( fn, "r" );
  if (!inf) {
    fprintf( stderr, "Could not open %s.  Quitting.\n", fn );
    return 0;
  }

  ListCreate( fnlist, char *, 10 );
  filestr = ReadFileIntoString( inf );

#if DEBUG
  fprintf( stderr, "reading functions\n" );
#endif

  ptr = strtok( filestr, " \n\t" );
  while (ptr) {
#if DEBUG
    fprintf( stderr, "fn: %s\n", ptr );
#endif
    /* StrToLower( ptr ); */
    ListAddItem( fnlist, char *, ptr );
    ptr = strtok( (char *)0, " \n\t" );
  }

  ListClose( fnlist, char *, *fn_names, *n_fn );

  fclose( inf );
  return 1;
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

static int is_oldstyle = 0;
int oldstyle_function( void )
{
    return is_oldstyle;
}


int PrintHelp() {
  printf( "\n" );
  printf( "options:\n\n" );
  printf( "  -w <file>  Add <file> to the list of wrapper files to use.\n" );
  printf( "  -f <file>  <file> contains a whitespace separated list of function\n" );
  printf( "             names to profile.\n" );
  printf( "  -p <file>  <file> contains the special function prototype declarations\n" );
  printf( "             for prof_wrapper.\n" );
printf( "  -o <file>  Send output to <file>.\n\n\n" );
  exit( 0 );
}

