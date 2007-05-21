/* 
 * This file contains code private to the p4 implementation of the ADI device
 * Primarily, this contains the code to setup the initial environment
 * and terminate the program
 */
#include "mpid.h"
#include "packets.h"

int __NUMNODES, __MYPROCID;

void MPID_NX_Init( argc, argv )
int *argc;
char ***argv;
{
    __NUMNODES = numnodes();
    __MYPROCID = mynode();
    MPID_MyWorldSize = __NUMNODES;
    MPID_MyWorldRank = __MYPROCID;
}

void MPID_NX_End()
{
    fflush( stdout );
    fflush( stderr );
}
