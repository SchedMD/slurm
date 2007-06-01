/* Prototypes for the mpe extension routines */
#ifndef MPE_EXTENSION_INC
#define MPE_EXTENSION_INC
/*
 * In order to provide better compile time error checking for the 
 * implementation, we use a union to store the actual copy/delete functions
 * for the different languages
 */
#include <stdio.h>
int MPE_Print_datatype_unpack_action ( FILE *, int, MPI_Datatype, int, int );
int MPE_Print_datatype_pack_action ( FILE *, int, MPI_Datatype, int, int );

void MPE_Comm_global_rank ( MPI_Comm, int, int * );

/* dbxerr.c */
void MPE_Errors_call_debugger ( char *, char *, char ** );
void MPE_Errors_call_dbx_in_xterm ( char *, char * );
void MPE_Errors_call_gdb_in_xterm ( char *, char * );
void MPE_Signals_call_debugger ( void );

#endif
