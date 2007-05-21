#ifndef MPITEST_DTYPES
#define MPITEST_DTYPES

void GenerateData ( MPI_Datatype *, void **, void **, int *, int *,
			      char **, int * );
void AllocateForData ( MPI_Datatype **, void ***, void ***, 
				 int **, int **, char ***, int * );
int CheckData ( void *, void *, int );
int CheckDataAndPrint ( void *, void *, int, char *, int );
void FreeDatatypes ( MPI_Datatype *, void **, void **, 
			       int *, int *, char **, int );
void BasicDatatypesOnly( void );
#endif
