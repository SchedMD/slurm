#ifndef ADITEST
#define ADITEST

int CheckData ( char *, char *, int );
int CheckDataS ( short *, short *, int, char * );
int CheckStatus ( MPI_Status *, int, int, int );
void SetupArgs ( int, char **, int *, int *, int * );
void SetupTests ( int, char **, int *, int *, int *, char **, char ** );
void SetupTestsS ( int, char **, int *, int *, int *, short **, short ** );
void EndTests ( void *, void * );
#endif
