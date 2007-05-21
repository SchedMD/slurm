#include "mpi.h"
#include <stdio.h>

main( argc, argv )
int argc;
char **argv;
{
int dest = 0, *buffer=&dest;
 
MPI_Init( &argc, &argv );
/* MPE_Errors_call_xdbx( argv[0], (char *)0 );  */
MPE_Errors_call_dbx_in_xterm( argv[0], (char *)0 );
MPE_Signals_call_debugger();
/* Make erroneous call... */
if (argc > 0) {
    buffer = 0;
    *buffer = 3; 
    }
else 
     MPI_Send(buffer, 20, MPI_INT, dest, 1, NULL);
MPI_Finalize();
}
