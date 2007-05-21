/*
 *
 *
 *  (C) 1996 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
#include "mpimem.h"
#include "flow.h"

MPID_Flow *MPID_flow_info = 0;
int        MPID_DebugFlow = 0;

void MPID_FlowDebug( int flag )
{
    MPID_DebugFlow = flag;
}

/*
 * This routine sends an update packet indicating data read
 */
void MPID_SendFlowPacket( int partner )
{
#ifdef MPID_USE_SHMEM
    MPID_PKT_FLOW_T *pkt;
#else
    MPID_PKT_FLOW_T pkt;
#endif

    DEBUG_PRINT_MSG("- Sending flow control packet");
#ifdef MPID_USE_SHMEM
    pkt = (MPID_PKT_FLOW_T *) MPID_SHMEM_GetSendPkt(0);
    pkt->mode = MPID_PKT_FLOW;
    MPID_FLOW_MEM_ADD(pkt,partner);
    MPID_SendControl( (MPID_PKT_T *)pkt, sizeof(MPID_PKT_FLOW_T), partner );
#else
    pkt.mode = MPID_PKT_FLOW;
    MPID_PKT_PACK( &pkt, sizeof(MPID_PKT_HEAD_T), partner );
    MPID_FLOW_MEM_ADD(&pkt,partner);
    MPID_SendControl( &pkt, sizeof(MPID_PKT_FLOW_T), partner );
#endif

}

void MPID_RecvFlowPacket( 
	MPID_PKT_T *in_pkt,
	int        partner)
{
#ifdef MPID_FLOW_CONTROL
    MPID_PKT_FLOW_T  *pkt = (MPID_PKT_FLOW_T *)in_pkt;
#endif

    DEBUG_PRINT_MSG("- Receiving flow control packet");
    MPID_FLOW_MEM_GET(pkt,partner);
#ifdef MPID_USE_SHMEM
    MPID_SHMEM_FreeRecvPkt( (MPID_PKT_T *)in_pkt );
#endif
}

void MPID_FlowSetup( 
	int buf_thresh, 
	int mem_thresh )
{
    int i;

    MPID_flow_info = (MPID_Flow *)MALLOC( 
	MPID_MyWorldSize * sizeof(MPID_Flow) );
    if (!MPID_flow_info) {
	exit(1);
    }
    if (buf_thresh <= 0) buf_thresh = 16384;
    if (mem_thresh <= 0) mem_thresh = MPID_FLOW_BASE_THRESH;
    memset( MPID_flow_info, 0, sizeof(MPID_Flow) * MPID_MyWorldSize );
    for (i=0; i<MPID_MyWorldSize; i++) {
	MPID_flow_info[i].mem_thresh = mem_thresh;
	MPID_flow_info[i].buf_thresh = buf_thresh;
    }
    if (MPID_DebugFlow) {
	fprintf( stdout, "Setup flow control with thresholds mem %d buf %d\n",
		 mem_thresh, buf_thresh );
    }

}

void MPID_FlowDelete( void )
{
    FREE( MPID_flow_info );
}

#include <stdio.h>

void MPID_FlowDump( FILE *fp )
{
    int i;
    for (i=0; i<MPID_MyWorldSize; i++) {
	FPRINTF( fp, 
      "[%d]%d: Buf used = %d, thresh = %d, Mem used = %d, thresh = %d\n",
		 MPID_MyWorldRank, i, MPID_flow_info[i].buf_use, 
		 MPID_flow_info[i].buf_thresh, 
		 MPID_flow_info[i].mem_use, 
		 MPID_flow_info[i].mem_thresh );
	FPRINTF( fp, 
      "[%d]%d Buf read = %d, mem read = %d, need update = %c\n",
		 MPID_MyWorldRank, i, 
		 MPID_flow_info[i].buf_read, 
		 MPID_flow_info[i].mem_read, 
		 (MPID_flow_info[i].need_update)?'Y':'N' );
    }
}
