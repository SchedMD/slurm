/* 
 * This file contains code private to the mpl implementation of the ADI device
 * Primarily, this contains the code to setup the initial environment
 * and terminate the program
 */
#include "mpid.h"
#include "packets.h"

int __MPLFROM, __MPLLEN, __MPLTYPE, __NUMNODES, __MYPROCID;

void MPID_MPL_Init( argc, argv )
int *argc;
char ***argv;
{
    int narg,nlen,i,*arglen;
    char *p,*argstr;

    mp_environ( &__NUMNODES, &__MYPROCID );
    MPID_MyWorldSize = __NUMNODES;
    MPID_MyWorldRank = __MYPROCID;
}

void MPID_MPL_End()
{
    fflush( stdout );
    fflush( stderr );
    mpc_sync( ALLGRP );
}
