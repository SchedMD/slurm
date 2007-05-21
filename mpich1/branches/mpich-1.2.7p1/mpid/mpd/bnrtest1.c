#include <stdio.h>
#include <unistd.h>
#include "bnr.h"

int main( int argc, char *argv[] )
{
  BNR_Group my_bnr_group;
    int i, rc, my_bnr_group_size, my_bnr_rank;
    char attr[100], val[100];

    rc = BNR_Init( );
    rc = BNR_Get_group( &my_bnr_group );
    rc = BNR_Get_rank( my_bnr_group, &my_bnr_rank );
    rc = BNR_Get_size( my_bnr_group, &my_bnr_group_size );

    sprintf( attr, "rank_%d", my_bnr_rank );
    sprintf( val, "%d", getpid() );
    rc = BNR_Put( my_bnr_group, attr, val, -1 );

    rc = BNR_Fence( my_bnr_group );
    
    for ( i=0; i < my_bnr_group_size; i++ ) {
	sprintf( attr, "rank_%d", i );
	rc = BNR_Get( my_bnr_group, attr, val ); 
	printf( "bnrtest %d: rank=%s pid=%s\n", my_bnr_rank, attr, val );  fflush( stdout );
    }

    rc = BNR_Get( 0, "SHMEMKEY", val );
    printf( "bnrtest %d: SHMEMKEY=%s\n", my_bnr_rank, val );  fflush( stdout );
    return( 0 );
}

