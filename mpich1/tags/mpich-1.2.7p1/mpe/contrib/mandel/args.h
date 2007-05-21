/* args.h - argument handling stuff */

#ifndef _ARGS_H_
#define _ARGS_H_

#ifdef __STDC__

int GetIntArg( int *argc, char **argv, char *switchName, int *val );
int GetDoubleArg( int *argc, char **argv, char *switchName, double *val );
int GetStringArg( int *argc, char **argv, char *switchName, char **val );
int IsArgPresent( int *argc, char **argv, char *switchName );
int GetArgAdjacentString( int *argc, char **argv, char *switchName,
			  char **value );
int GetIntListArg( int *argc, char **argv, char *switchName,
	       int **intList, int *listLen );
int GetStringListArg( int *argc, char **argv, char *switchName,
		  char ***strList, int *listLen );
#else
int GetIntArg( );
int GetDoubleArg( );
int IsArgPresent( );
int GetArgAdjacentString( );
int GetIntListArg();
int GetStringListArg( );
#endif


#endif
/* _ARGS_H_ */
