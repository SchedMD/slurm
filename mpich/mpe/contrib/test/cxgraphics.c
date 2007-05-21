#include <stdio.h>
#include <stdlib.h>
#include "mpe.h"
#include "mpe_graphics.h"

int main( int argc, char** argv )
{
    MPE_XGraph graph;
    int ierr, mp_size, my_rank;
	MPE_Color my_color;
    char ckey;
    /*
    char displayname[MPI_MAX_PROCESSOR_NAME+4] = "";
    */

    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &mp_size );
    MPI_Comm_rank( MPI_COMM_WORLD, &my_rank );

    /*
    if ( my_rank == 0 )
        strcpy( displayname, getenv( "DISPLAY" ) );

    MPI_Bcast( displayname, MPI_MAX_PROCESSOR_NAME+4, MPI_CHAR, 
               0, MPI_COMM_WORLD );
    fprintf( stdout, "%d : $DISPLAY at process 0 = %s\n",
             my_rank, displayname );
    fflush( stdout );

    ierr = MPE_Open_graphics( &graph, MPI_COMM_WORLD, displayname,
                              -1, -1, 400, 400, 0 );
    */

    ierr = MPE_Open_graphics( &graph, MPI_COMM_WORLD, NULL,
                              -1, -1, 400, 400, 0 );
    if ( ierr != MPE_SUCCESS ) {
        fprintf( stderr, "%d : MPE_Open_graphics() fails\n", my_rank );
        ierr = MPI_Abort( MPI_COMM_WORLD, 1 );
        exit(1);
    }
    my_color = (MPE_Color) (my_rank + 1);
    if ( my_rank == 0 )
        ierr = MPE_Draw_string( graph, 187, 205, MPE_BLUE, "Hello" );
    ierr = MPE_Draw_circle( graph, 200, 200, 20+my_rank*5, my_color );
    ierr = MPE_Update( graph );

    if ( my_rank == 0 ) {
        fprintf( stdout, "Hit any key then return to continue  " );
        fscanf( stdin, "%s", &ckey );
        fprintf( stdout, "\n" );
    }
    MPI_Barrier( MPI_COMM_WORLD );

    ierr = MPE_Close_graphics( &graph );

    MPI_Finalize();
    
    return 0;
}
