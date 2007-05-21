/*
 *  $Id: chinit.c,v 1.3 2002/04/17 21:02:58 ashton Exp $
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

/* 
    This file contains the routines that provide the basic information 
    on the device, and initialize it
 */

/* We put these include FIRST incase we are building the memory debugging
   version; since these includes may define malloc etc., we need to include 
   them before mpid.h 
 */
#ifndef HAVE_STDLIB_H
extern char *getenv();
#else
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "flow.h"
#include "chpackflow.h"
#include "packets.h"
#include <stdio.h>
#include <windows.h>

/* #define DEBUG(a) {a} */
#define DEBUG(a)

/*****************************************************************************
  Here begin the interface routines themselves
 *****************************************************************************/

/* Forward refs */
int MPID_CH_End ( MPID_Device * );
int MPID_CH_SHM_End( MPID_Device * );
int MPID_CH_Abort ( struct MPIR_COMMUNICATOR *, int, char * );
void MPID_CH_Version_name ( char * );
int MPID_NT_ipvishm_is_shm( int );

void MPID_NT_ipvishm_fixupdevpointers(MPID_Device *pDevice)
{
    int i;

    /* pDevice has the tcp device settings.
       pDevice->next hast the shm device settings. */
    for (i=0; i<g_nNproc; i++)
	MPID_devset->dev[i] = MPID_NT_ipvishm_is_shm(i) ? pDevice->next : pDevice;
}

/* 
    In addition, Chameleon processes many command-line arguments 

    This should return a structure that contains any relavent context
    (for use in the multiprotocol version)

    Returns a device.  
    This sets up a message-passing device (short/eager/rendezvous protocols)
 */
MPID_Device *MPID_CH_InitMsgPass( int *argc, char ***argv, int short_len, int long_len )
{
    MPID_Device *dev, *dev_shm;
	char pszTemp[100];
	int nMPID_PKT_MAX_DATA_SIZE = MPID_PKT_MAX_DATA_SIZE;
	int nMPID_PKT_DATA_LONG_LEN = 1024*100;
	int nMPID_PKT_DATA_LONG_LENshm = 1024*20;

    dev = (MPID_Device *)MALLOC( sizeof(MPID_Device) );
    dev_shm = (MPID_Device *)MALLOC( sizeof(MPID_Device) );
    if (!dev || !dev_shm) return 0;

    // get the environment variables for the short and long protocol break points
	if (GetEnvironmentVariable("MPICH_SHORTLONGTHRESH", pszTemp, 100))
	{
		nMPID_PKT_MAX_DATA_SIZE = atoi(pszTemp);
		if (nMPID_PKT_MAX_DATA_SIZE < 0)
			nMPID_PKT_MAX_DATA_SIZE = 0;
	}
	if (GetEnvironmentVariable("MPICH_LONGVLONGTHRESH", pszTemp, 100))
	{
		nMPID_PKT_DATA_LONG_LEN = atoi(pszTemp);
		if (nMPID_PKT_DATA_LONG_LEN < 0)
			nMPID_PKT_DATA_LONG_LEN = 0;
		nMPID_PKT_DATA_LONG_LENshm = nMPID_PKT_DATA_LONG_LEN;
	}
    // get the environment variables for the protocol specific long break point
	if (GetEnvironmentVariable("MPICH_TCPLONGVLONGTHRESH", pszTemp, 100))
	{
		nMPID_PKT_DATA_LONG_LEN = atoi(pszTemp);
		if (nMPID_PKT_DATA_LONG_LEN < 0)
			nMPID_PKT_DATA_LONG_LEN = 0;
	}
	if (GetEnvironmentVariable("MPICH_SHMLONGVLONGTHRESH", pszTemp, 100))
	{
		nMPID_PKT_DATA_LONG_LENshm = atoi(pszTemp);
		if (nMPID_PKT_DATA_LONG_LENshm < 0)
			nMPID_PKT_DATA_LONG_LENshm = 0;
	}

    /* The short protocol MUST be for messages no longer than 
       MPID_PKT_MAX_DATA_SIZE since the data must fit within the packet */
    //if (short_len < 0) short_len = nMPID_PKT_MAX_DATA_SIZE;
	if (short_len < 0) short_len = MPID_PKT_MAX_DATA_SIZE; // For now this is static and cannot be changed at run time.
    if (long_len < 0)  long_len  = nMPID_PKT_DATA_LONG_LEN;
    dev->long_len     = short_len;
    dev->vlong_len    = long_len;
    dev->short_msg    = MPID_CH_Short_setup();
    dev->long_msg     = MPID_CH_Eagerb_setup();
    dev->vlong_msg    = MPID_NT_Rndvn_setup();
	//dev->vlong_msg    = MPID_NT_Rndvb_setup();
    dev->eager        = dev->long_msg;
    dev->rndv         = dev->vlong_msg;
    dev->check_device = MPID_CH_Check_incoming;
    dev->terminate    = MPID_CH_End;
    dev->abort	      = MPID_CH_Abort;
    //dev->next	      = 0;
    dev->next         = dev_shm; // will this cause the dev_shm memory to be freed at finalize?

    dev_shm->long_len     = short_len;
    dev_shm->vlong_len    = nMPID_PKT_DATA_LONG_LENshm;
    dev_shm->short_msg    = MPID_CH_Short_setup();
    dev_shm->long_msg     = MPID_CH_Eagerb_setup();
    dev_shm->vlong_msg    = MPID_NT_Rndvn_setup();
    //dev_shm->vlong_msg    = MPID_NT_Rndvb_setup();
    dev_shm->eager        = dev_shm->long_msg;
    dev_shm->rndv         = dev_shm->vlong_msg;
    dev_shm->check_device = MPID_CH_Check_incoming;
    dev_shm->terminate    = MPID_CH_SHM_End;
    dev_shm->abort	      = MPID_CH_Abort;
    dev_shm->next	      = 0;
    /* Set the file for Debugging output.  The actual output is controlled
       by MPIDDebugFlag */
//#ifdef MPID_DEBUG_ALL
    if (MPID_DEBUG_FILE == 0) MPID_DEBUG_FILE = stdout;
//#endif

    PIiInit( argc, argv );
    DEBUG_PRINT_MSG("Finished init");

    MPID_DO_HETERO(MPID_CH_Init_hetero( argc, argv ));

#ifdef MPID_FLOW_CONTROL
    /* Try to get values for thresholds.  Note that everyone MUST have
     the same values for this to work */
    {int buf_thresh = 0, mem_thresh = 0;
     char *val;
     val = getenv( "MPI_BUF_THRESH" );
     if (val) buf_thresh = atoi(val);
     val = getenv( "MPI_MEM_THRESH" );
     if (val) mem_thresh = atoi(val);
    MPID_FlowSetup( buf_thresh, mem_thresh );
    }
#endif
#ifdef MPID_PACK_CONTROL
    MPID_PacketFlowSetup( );
#endif

    DEBUG_PRINT_MSG("Leaving MPID_CH_InitMsgPass");

    return dev;
}

/* Barry Smith suggests that this indicate who is aborting the program.
   There should probably be a separate argument for whether it is a 
   user requested or internal abort.
 */
int MPID_CH_Abort( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	int code, 
	char *msg )
{
    if (msg) {
	fprintf( stderr, "[%d] %s\n", MPID_MyWorldRank, msg );
    }
    else {
	fprintf( stderr, "[%d] Aborting program!\n", MPID_MyWorldRank );
    }
    fflush( stderr );
    fflush( stdout );
    /* Some systems (e.g., p4) can't accept a (char *)0 message argument. */
    SYexitall( "", code );
    return 0;
}

int MPID_CH_SHM_End(MPID_Device *dev)
{
    DEBUG_PRINT_MSG("Entering MPID_CH_SHM_End\n");

    (dev->short_msg->delete)( dev->short_msg );
    (dev->long_msg->delete)( dev->long_msg );
    (dev->vlong_msg->delete)( dev->vlong_msg );
    FREE( dev );

    DEBUG_PRINT_MSG("Leaving MPID_CH_End");
    return 0;
}

int MPID_CH_End( 
	MPID_Device *dev )
{
    DEBUG_PRINT_MSG("Entering MPID_CH_End\n");
    /* Finish off any pending transactions */
#ifdef MPID_PACK_CONTROL
#ifdef MPID_GET_LAST_PKT
    MPID_FinishRecvPackets(dev);
#endif
    MPID_PackDelete();
#endif
    MPID_FinishCancelPackets(dev);
    if (MPID_GetMsgDebugFlag()) {
	MPID_PrintMsgDebug();
    }
#ifdef MPID_HAS_HETERO /* #HETERO_START# */
    MPID_CH_Hetero_free();
#endif                 /* #HETERO_END# */
    (dev->short_msg->delete)( dev->short_msg );
    (dev->long_msg->delete)( dev->long_msg );
    (dev->vlong_msg->delete)( dev->vlong_msg );
    FREE( dev );

#ifdef MPID_FLOW_CONTROL
    MPID_FlowDelete();
#endif

    /* We should really generate an error or warning message if there 
       are uncompleted operations... */
    
    PIiFinish();
    DEBUG_PRINT_MSG("Leaving MPID_CH_End");
    return 0;
}

void MPID_CH_Version_name( char *name )
{
    sprintf( name, "ADI version %4.2f - transport %s", MPIDPATCHLEVEL, 
	     MPIDTRANSPORT );
}

