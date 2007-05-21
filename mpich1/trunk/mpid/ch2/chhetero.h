#ifndef MPID_CHHETERO
#define MPID_CHHETERO

/* 
 * The following is for support of heterogeneous systems and can be ignored
 * by homogeneous implementations
 */
typedef enum { MPID_H_NONE = 0, 
		   MPID_H_LSB, MPID_H_MSB, MPID_H_XDR } MPID_H_TYPE;
/* 
   The MPID_INFO structure is acquired from each node and used to determine
   the format for data that is sent.
 */
typedef struct {
    MPID_H_TYPE byte_order;
    int         short_size, 
                int_size,
                long_size,
                float_size,
                double_size,
	        long_double_size,
                float_type;
    } MPID_INFO;

extern MPID_INFO *MPID_procinfo;
extern MPID_H_TYPE MPID_byte_order;
extern int MPID_IS_HETERO;

#ifndef MPID_HAS_HETERO
/* Msgrep is simply OK for Homogeneous systems */
#define MPID_CH_Comm_msgrep( comm ) \
    ((comm?((struct MPIR_COMMUNICATOR *)comm)->msgform = MPID_MSG_OK:0),MPI_SUCCESS)
#define MPID_Msgrep_from_comm( comm ) MPID_MSGREP_RECEIVER
#else
extern int MPID_CH_Comm_msgrep ( struct MPIR_COMMUNICATOR * );
/* The value of the ? operation is an INT, even though both members come from
   the SAME enumerated type.  At least on SGI... */
#define MPID_Msgrep_from_comm( comm ) \
(MPID_Msgrep_t)(((comm)->msgform == MPID_MSG_OK) ? MPID_MSGREP_RECEIVER : MPID_MSGREP_XDR)
#endif
extern int MPID_CH_Hetero_free (void);

#ifdef MPID_DEVICE_CODE
#if defined(HAS_XDR) && defined(MPID_HAS_HETERO)
/* HP-UX does not properly guard rpc/rpc.h from multiple inclusion */
#if !defined(INCLUDED_RPC_RPC_H)
#include "rpc/rpc.h"
#define INCLUDED_RPC_RPC_H
#endif
void MPID_Mem_XDR_Init ( char *, int, enum xdr_op, XDR * );
int MPID_Mem_XDR_len  ( struct MPIR_DATATYPE *, int );
void MPID_Mem_XDR_Free ( XDR * );
int MPID_Mem_XDR_Encode ( unsigned char *, unsigned char *,
				    xdrproc_t, int, int, XDR * );
int MPID_Mem_XDR_Encode_Logical ( unsigned char *, unsigned char *,
				    xdrproc_t, int, XDR * );
int MPID_Mem_XDR_ByteEncode ( unsigned char *, unsigned char *, int, XDR * );
int MPID_Mem_XDR_Decode ( unsigned char *, unsigned char *,
				    xdrproc_t, int, int, int, int *, int *, 
				    XDR * );
int MPID_Mem_XDR_Decode_Logical ( unsigned char *, unsigned char *,
				    xdrproc_t, int, int, int, int *, int *, 
				    XDR * );
int MPID_Mem_XDR_ByteDecode ( unsigned char *, unsigned char *,
					int, int, int *, int *, XDR * );
#endif
int MPID_Type_XDR_encode (unsigned char *, unsigned char *, 
				    struct MPIR_DATATYPE*, int, void * );
int MPID_Type_swap_copy (unsigned char *, unsigned char *, 
				    struct MPIR_DATATYPE*, int, void * );
#endif /* MPID_DEVICE_CODE */
#endif
