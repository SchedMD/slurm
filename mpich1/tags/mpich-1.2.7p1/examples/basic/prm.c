/* prm from SUT, in MPI */
#include "mpi.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#define CMDSIZE    80

int main( int argc, char *argv[] )
{
    int myrank;
    char controlmsg[1024], cmd[1024];

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &myrank );

    if ( myrank == 0 )
	strcpy(controlmsg,argv[1]);
    MPI_Bcast( controlmsg, CMDSIZE, MPI_CHAR, 0, MPI_COMM_WORLD );

    sprintf( cmd, "/bin/rm -rf %s\n", controlmsg );
    system(cmd);

    MPI_Finalize();
    return 0;
}
