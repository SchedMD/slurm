#ifndef _GETOPTS_H
#define _GETOPTS_H

void SYArgSqueeze( int *Argc, char **argv );
int SYArgFindName( int argc, char **argv, char *name );
int SYArgGetInt( int *Argc, char **argv, int rflag, char *name, int *val );
int SYArgGetDouble( int *Argc, char **argv, int rflag, char *name, 
		    double *val );
int SYArgGetString( int *Argc, char **argv, int rflag, char *name, char *val, 
		    int vallen );
int SYArgHasName( int *Argc, char **argv, int rflag, char *name );
int SYArgGetIntVec( int *Argc, char **argv, int rflag, char *name, int n, 
		    int *val );
int SYArgGetIntList( int *Argc, char **argv, int rflag, char *name, int n, 
		     int *val );

#endif
