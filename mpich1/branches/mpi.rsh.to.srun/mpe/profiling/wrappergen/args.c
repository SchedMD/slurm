#include "args.h"
#include "expandingList.h"
#include <string.h>

#ifndef DEBUG
#define DEBUG 0
#endif

GetIntArg( argc, argv, switchName, val )
int *argc, *val;
char **argv, *switchName;
{
  int i, nremove, j;		/* nremove - number of arguments to remove */

  for (i=1; i<*argc; i++) {		  /* loop through all args */
    if (!strcmp( switchName, argv[i] )) { /* if this is the switch we want, */
      if (i+1<*argc) {			  /* make sure there is one more */
	if (sscanf( argv[i+1], "%d", val )) {
	  nremove = 2;			 /* got valid value */
	} else {
	  nremove = 1;			 /* didn't get valid value, but */
					 /* don't throw away the next arg. */
	}
      } else {
	nremove = 1;
      }
      for (i+=nremove; i<*argc; i++) {	 /* move everyone else down */
	argv[i-nremove]=argv[i];
      }
      (*argc)-=nremove;
      if (nremove==2) return 1;		 /* if we got a value, return */
      i--;
    }
  }
  return 0;
}



GetDoubleArg( argc, argv, switchName, val )
int *argc;
double *val;
char **argv, *switchName;
{
  int i, nremove, j;		/* nremove - number of arguments to remove */

  for (i=1; i<*argc; i++) {		  /* loop through all args */
    if (!strcmp( switchName, argv[i] )) { /* if this is the switch we want, */
      if (i+1<*argc) {			  /* make sure there is one more */
	if (sscanf( argv[i+1], "%lf", val )) {
	  nremove = 2;			 /* got valid value */
	} else {
	  nremove = 1;			 /* didn't get valid value, but */
					 /* don't throw away the next arg. */
	}
      } else {
	nremove = 1;
      }
      for (i+=nremove; i<*argc; i++) {	 /* move everyone else down */
	argv[i-nremove]=argv[i];
      }
      (*argc)-=nremove;
      if (nremove==2) return 1;		 /* if we got a value, return */
      i--;
    }
  }
  return 0;
}



GetStringArg( argc, argv, switchName, val )
int *argc;
char **argv, *switchName, **val;
{
  int i, nremove, j;		/* nremove - number of arguments to remove */
  char *readPtr;

  for (i=1; i<*argc; i++) {		  /* loop through all args */
    if (!strcmp( switchName, argv[i] )) { /* if this is the switch we want, */
      if (i+1<*argc) {			  /* make sure there is one more */
	*val = argv[i+1];
	nremove = 2;
      } else {
	nremove = 1;
      }
      for (i+=nremove; i<*argc; i++) {	 /* move everyone else down */
	argv[i-nremove]=argv[i];
      }
      (*argc)-=nremove;
      if (nremove==2) return 1;		 /* if we got a value, return */
      i--;
    }
  }
  return 0;
}



IsArgPresent( argc, argv, switchName )
int *argc;
char **argv, *switchName;
{
  int i, returnVal;

  returnVal = 0;
  for (i=1; i<*argc; i++) {		 /* loop through all args */
/*
    printf( "Comparing :%s: and :%s:\n", switchName, argv[i] );
*/
    if (!strcmp( switchName, argv[i] )) { /* if this is the switch we want, */
/*
      printf( "YUP!" );
*/
      for (i++; i<*argc; i++) {	 /* slide everything on down */
	argv[i-1]=argv[i];
      }
      (*argc)--;
      i--;
      returnVal = 1;
    }
  }
  return returnVal;
}




GetArgAdjacentString( argc, argv, switchName, value )
int *argc;
char **argv, *switchName, **value;
{
  int argNum, i, str_len;
  xpandList listStr;
  char *readPtr, *start, *end, *theString;

  ListCreate( listStr, char, 10 );

  for (argNum=1; argNum<*argc; argNum++) {
    readPtr = strstr( argv[argNum], switchName );
    if (readPtr==argv[argNum]) {
      /* we want to find the switch at the beginning of an argument */
#if DEBUG
      fprintf( stderr, "Found %s in %s\n", switchName, argv[argNum] );
#endif
      readPtr = argv[argNum] + strlen( switchName );
      while (*readPtr) {
	ListAddItem( listStr, char, *readPtr ); /* add a character */
	readPtr++;
      }	/* done copying list */
      ListAddItem( listStr, char, '\0' ); /* terminate the string */

      for (argNum++; argNum < *argc; argNum++) {
	argv[argNum-1] = argv[argNum]; /* shift remaining arguments down */
      }
      (*argc)--;

      ListClose( listStr, char, theString, str_len );
#if DEBUG
      fprintf( stderr, "Returning string: %s\n", theString );
#endif
      *value = theString;
      return 1;
    } /* if strstr(... switch ) */
  } /* keep looking for the switch */
  return 0;			/* didn't even find the switch */
}




GetIntListArg( argc, argv, switchName, intList, listLen )
int *argc;
char **argv, *switchName;
int **intList, *listLen;
{
  char *list, *token;
  xpandList tempIntList;
  int temp_int;
  
  ListCreate( tempIntList, int, 10 );

  if (!GetArgAdjacentString( argc, argv, switchName, &list )) {
    return 0;
  }
  token = strtok( list, "," );
  while (token) {
    if (sscanf( token, "%d", &temp_int ))
      ListAddItem( tempIntList, int, temp_int );
    token = strtok( (char*)0, "," );
  }

  ListClose( tempIntList, int, *intList, *listLen );
  free( list );
  return 1;
}

GetStringListArg( argc, argv, switchName, strList, listLen )
int *argc, *listLen;
char **argv, *switchName, ***strList;
{
  char *list, *token, *str_dup;
  int temp_int;
  xpandList tempStrList;
  
  ListCreate( tempStrList, char *, 10 );
  if (!GetArgAdjacentString( argc, argv, switchName, &list )) {
    return 0;
  }
  token = strtok( list, "," );
  while (token) {
    str_dup = (char *) malloc( (strlen(token) + 1) * sizeof( char ) );
    strcpy( str_dup, token );
    ListAddItem( tempStrList, char *, str_dup );
#if DEBUG
    fprintf( stderr, "arg: get string list item :%s:\n", token );
#endif
    token = strtok( (char*)0, "," );
  }
  ListClose( tempStrList, char *, *strList, *listLen );
  free( list );
  return 1;
}

