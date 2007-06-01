/*
 *
 *
 *  (C) 1993 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */

#include "mpid.h"
#include "mpiddev.h"
/* For MPID_ArgSqueeze */
#include "../util/cmnargs.h"
#include "mpimem.h"

/* #define DEBUG(a) {a} */
#define DEBUG(a)

#include "chhetero.h"

MPID_INFO *MPID_procinfo = 0;
MPID_H_TYPE MPID_byte_order;
#ifdef FOO
static char *(ByteOrderName[]) = { "None", "LSB", "MSB", "XDR" };
#endif
int MPID_IS_HETERO = 0;

/* Local definitions */
int MPID_GetByteOrder (void);
void MPID_ByteSwapInt ( int*, int );

/* 
 * This routine is called to initialize the information about datatype
 * representation at other processors.
 */
int MPID_CH_Init_hetero( int *argc, char **argv[] )
{
    int  i, use_xdr;
    char *work;

/* This checks for word sizes, so that systems
   with 32 bit ints can interoperate with 64 bit ints, etc. 
   We can check just the basic signed types: short, int, long, float, double 
   Eventually, we should probably also check the long long and long double
   types.
   We do this by collecting an array of word sizes, with each processor
   contributing the 5 lengths.  This is combined with the "byte order"
   field.

   We still need to identify IEEE and non-IEEE systems.  Perhaps we'll
   just use a field from configure (only CRAY vector systems are non IEEE
   of the systems we interact with).

   We ALSO need to identify Fortran sizes, at least sizeof(REAL), 
   and change the heterogenous code to handle Fortran LOGICALs
 */
/*
   We look for the argument -mpixdr to force use of XDR for debugging and
   timing comparision.
 */
    DEBUG_PRINT_MSG("Checking for heterogeneous systems...");
    MPID_procinfo = (MPID_INFO *)MALLOC( MPID_MyWorldSize * sizeof(MPID_INFO) );
    if (!MPID_procinfo) {
	return MPI_ERR_INTERN;
    }
    for (i=0; i<MPID_MyWorldSize; i++) {
	MPID_procinfo[i].byte_order	  = MPID_H_NONE;
	MPID_procinfo[i].short_size	  = 0;
	MPID_procinfo[i].int_size	  = 0;
	MPID_procinfo[i].long_size	  = 0;
	MPID_procinfo[i].float_size	  = 0;
	MPID_procinfo[i].double_size	  = 0;
	MPID_procinfo[i].long_double_size = 0;
	MPID_procinfo[i].float_type	  = 0;
    }
/* Set my byte ordering and convert if necessary.  */

/* Set the floating point type.  IEEE is 0, Cray is 2, others as we add them
   (MPID_FLOAT_TYPE?) Not yet set: VAX floating point,
   IBM 360/370 floating point, and other. */
#ifdef MPID_FLOAT_CRAY
    MPID_procinfo[MPID_MyWorldRank].float_type = 2;
#endif
    use_xdr = 0;
    /* PRINTF ("Checking args for -mpixdr\n" ); */
    /* PRINTF( "[%d] 1\n", MPID_MyWorldRank ); fflush(stdout); */
    for (i=1; i<*argc; i++) {
	/* PRINTF( "Arg[%d] is %s\n", i, (*argv)[i] ? (*argv)[i] : "<NULL>" ); */
	/* 
        PRINTF( "[%d] about to test arg value %d\n", MPID_MyWorldRank, i );
	PRINTF( "[%d] argv is %lx\n", MPID_MyWorldRank, argv ); 
	if (!argv) PRINTF( "argv not set\n" );
	PRINTF( "[%d] *argv is %lx\n", MPID_MyWorldRank, *argv );
	if (!(*argv)) PRINTF( "*argv not set\n" );
	PRINTF( "[%d] (*argv)[%d] is %lx\n", MPID_MyWorldRank, i, (*argv)[i] );
	if (!(*argv)[i]) PRINTF( "(*argv)[%d] not set\n", i );
	PRINTF( "arg is %s\n", (*argv)[i] ); fflush( stdout );
         */
	if ((*argv)[i] && strcmp( (*argv)[i], "-mpixdr" ) == 0) {
	    /* PRINTF( "Found -mpixdr\n" ); */
	    use_xdr = 1;
	    /* PRINTF( "[%d] about to set argv[%d]\n", 
	       MPID_MyWorldRank, i ); */
	    (*argv)[i] = 0;
	    /* PRINTF( "[%d] about squeeze args\n", 
		    MPID_MyWorldRank ); */
	    MPID_ArgSqueeze( argc, *argv );
	    /* PRINTF( "[%d] done squeeze args\n", 
		    MPID_MyWorldRank ); */
	    break;
	}
    }
    /* PRINTF( "[%d] 2\n", MPID_MyWorldRank ); fflush(stdout); */
    if (use_xdr) 
	MPID_byte_order = MPID_H_XDR;
    else {
	i = MPID_GetByteOrder( );
#ifdef MPID_DEBUG_ALL   /* #DEBUG_START# */
	DEBUG(FPRINTF(MPID_DEBUG_FILE,"[%d] Byte order is %d\n",MPID_MyWorldRank, i ););
#endif                  /* #DEBUG_END# */
	if (i == 1)      MPID_byte_order = MPID_H_LSB;
	else if (i == 2) MPID_byte_order = MPID_H_MSB;
	else             MPID_byte_order = MPID_H_XDR;
    }
    MPID_procinfo[MPID_MyWorldRank].byte_order	     = MPID_byte_order;
    MPID_procinfo[MPID_MyWorldRank].short_size	     = sizeof(short);
    MPID_procinfo[MPID_MyWorldRank].int_size	     = sizeof(int);
    MPID_procinfo[MPID_MyWorldRank].long_size	     = sizeof(long);
    MPID_procinfo[MPID_MyWorldRank].float_size	     = sizeof(float);
    MPID_procinfo[MPID_MyWorldRank].double_size	     = sizeof(double);
#if defined(HAVE_LONG_DOUBLE)
    MPID_procinfo[MPID_MyWorldRank].long_double_size = sizeof(long double);
    /* Otherwise leave as zero */
#endif

/* Everyone uses the same format (MSB) */
/* This should use network byte order OR the native collective operation 
   with heterogeneous support */
/* if (i == 1) 
   MPID_ByteSwapInt( (int*)&MPID_procinfo[MPID_MyWorldRank].byte_order, 1 );
 */
/* Get everyone else's */
    work = (char *)MALLOC( MPID_MyWorldSize * sizeof(MPID_INFO) );
    if (!work) 
	return MPI_ERR_INTERN;
/* ASSUMES MPID_INFO is ints */
    PIgimax( MPID_procinfo, 
	     (sizeof(MPID_INFO)/sizeof(int)) * MPID_MyWorldSize, 
	     work, PSAllProcs );
    FREE( work );
    /* PRINTF( "3\n" ); */
/* See if they are all the same and different from XDR*/
    MPID_IS_HETERO = MPID_procinfo[0].byte_order == MPID_H_XDR;
    for (i=1; i<MPID_MyWorldSize; i++) {
	if (MPID_procinfo[0].byte_order  != MPID_procinfo[i].byte_order ||
	    MPID_procinfo[i].byte_order  == MPID_H_XDR ||
	    MPID_procinfo[0].short_size  != MPID_procinfo[i].short_size ||
	    MPID_procinfo[0].int_size    != MPID_procinfo[i].int_size ||
	    MPID_procinfo[0].long_size   != MPID_procinfo[i].long_size ||
	    MPID_procinfo[0].float_size  != MPID_procinfo[i].float_size ||
	    MPID_procinfo[0].double_size != MPID_procinfo[i].double_size ||
	    MPID_procinfo[0].long_double_size != 
	    MPID_procinfo[i].long_double_size ||
	    MPID_procinfo[0].float_type  != MPID_procinfo[i].float_type) {
	    MPID_IS_HETERO = 1;
	    break;
        }
    }
/* 
   When deciding to use XDR, we need to check for size as well (if 
   [myid].xxx_size != [j].xxx_size, set [j].byte_order = XDR).  Note that 
   this is reflexive; if j decides that i needs XDR, i will also decide that
   j needs XDR;

   Note that checking for long double is also sort of strange; original
   XDR is pre-ANSI C and has no long double conversion.  So checking for
   this only causes us to fail more deterministically.  
 */
    if (MPID_IS_HETERO) {
	for (i=0; i<MPID_MyWorldSize; i++) {
	    if (i == MPID_MyWorldRank) continue;
	    if (MPID_procinfo[MPID_MyWorldRank].short_size  != 
		MPID_procinfo[i].short_size ||
		MPID_procinfo[MPID_MyWorldRank].int_size    != 
		MPID_procinfo[i].int_size ||
		MPID_procinfo[MPID_MyWorldRank].long_size   != 
		MPID_procinfo[i].long_size ||
		MPID_procinfo[MPID_MyWorldRank].float_size  != 
		MPID_procinfo[i].float_size ||
		MPID_procinfo[MPID_MyWorldRank].double_size != 
		MPID_procinfo[i].double_size ||
		MPID_procinfo[MPID_MyWorldRank].long_double_size != 
		MPID_procinfo[i].long_double_size ||
		MPID_procinfo[MPID_MyWorldRank].float_type !=
		MPID_procinfo[i].float_type) {
		MPID_procinfo[i].byte_order = MPID_H_XDR;
	    }
	}
    }
#ifdef FOO
    if (MPID_IS_HETERO && MPID_MyWorldRank == 0) {
	printf( "Warning: heterogenity only partially supported\n" );
	printf( 
	"Ordering short int long float double longdouble sizes float-type\n" );
	for (i=0; i<MPID_MyWorldSize; i++) {
	    PRINTF( "<%d> %s %d %d %d %d %d %d %d\n", i,
		    ByteOrderName[MPID_procinfo[i].byte_order],
		    MPID_procinfo[i].short_size, 
		    MPID_procinfo[i].int_size, 
		    MPID_procinfo[i].long_size, 
		    MPID_procinfo[i].float_size, 
		    MPID_procinfo[i].double_size,
		    MPID_procinfo[i].long_double_size,
		    MPID_procinfo[i].float_type );
	}
    }
#endif
    return MPI_SUCCESS;
}

#ifdef FOO
/* 
 * Note that this requires an ABSOLUTE destination; ANY or RELATIVE 
 * are not valid.
 */
int MPID_CH_Dest_byte_order( int dest )
{
if (MPID_IS_HETERO)
    return MPID_procinfo[dest].byte_order;
else 
    return MPID_H_NONE;
}
#endif

/*
 * This routine takes a communicator and determines the message representation
 * field for it
 */
#ifdef MPID_HAS_HETERO

int MPID_CH_Comm_msgrep( struct MPIR_COMMUNICATOR *comm_ptr )
{
    MPID_H_TYPE my_byte_order;
    int i;

    /* If a null communicator, just return success.  This is needed in 
       case MPID_CommInit simply calls this routine; since MPID_CommInit
       is collective over the *old* communicator (is it?), it may be invoked 
       with a null new communicator */
    if (!comm_ptr) return MPI_SUCCESS;

/* We must compare the rep of the rank in the local group to
   the members of the remote group.  This works for both intra and inter 
   communicators. 
   
   Note that in the intracommunicator case, we COULD use a receiver rep, if
   all receivers had the same representation.  The current code assumes
   that the sender is a potential receiver, so it can't use receiver order.
*/
    if (!MPID_IS_HETERO) {
	comm_ptr->msgform = MPID_MSG_OK;
    }
    
    my_byte_order = MPID_procinfo[MPID_MyWorldRank].byte_order;

    if (my_byte_order == MPID_H_XDR) {
	comm_ptr->msgform = MPID_MSG_XDR;
	return MPI_SUCCESS;
    }
  
    /* This uses the "cached" attributes; this also allows an 
       implementation to use a simplified communicator */
    for (i = 0; i < comm_ptr->np; i++) {
	if (MPID_procinfo[comm_ptr->lrank_to_grank[i]].byte_order !=
	    my_byte_order) {
	    comm_ptr->msgform = MPID_MSG_XDR;
	    return MPI_SUCCESS;
	}
    }
    
/* receiver is == 0, so this says "no change" (sender and receiver have
   same format).  This needs to change... */
    comm_ptr->msgform = MPID_MSG_OK;
    return MPI_SUCCESS;
}
#endif

/* This routine is ensure that the elements in the packet HEADER can
   be read by the receiver without further processing (unless XDR is
   used, in which case we use network byte order)
   This routine is defined ONLY for heterogeneous systems

   Note that different packets have different lengths and layouts; 
   this makes the conversion more troublesome.  I'm still thinking
   about how to do this.
 */
#include <sys/types.h>
#include <netinet/in.h>
/* These need to use 32bit ints.  The 4's here are sizeof(int32) */

void MPID_CH_Pkt_pack( 
	void *in_pkt, 
	int size, 
	int dest )
{
MPID_PKT_T *pkt = (MPID_PKT_T *)in_pkt;
int i;
unsigned int *d;
if (MPID_IS_HETERO &&
    (MPID_procinfo[dest].byte_order != MPID_byte_order ||
     MPID_byte_order == MPID_H_XDR)) {
    
    if (MPID_procinfo[dest].byte_order == MPID_H_XDR ||
	MPID_byte_order == MPID_H_XDR) {
	d = (unsigned int *)pkt;
	for (i=0; i<size/4; i++) {
	    *d = htonl(*d);
	    d++;
	    }
	}
    else {
	/* Need to swap to receiver's order.  We ALWAYS reorder at the
	   sender's end (this is because a message can be received with 
	   MPI_Recv instead of MPI_Recv/MPI_Unpack, and hence requires us 
	   to use a format that matches the receiver's ordering without 
	   requiring a user-unpack.  */
	MPID_ByteSwapInt( (int*)pkt, size / 4 );
	}
    }
}

void MPID_CH_Pkt_unpack( 
	void *in_pkt,
	int size, 
	int from)
{
MPID_PKT_T *pkt = (MPID_PKT_T *)in_pkt;
int i;
unsigned int *d;

if (MPID_IS_HETERO &&
    (MPID_procinfo[from].byte_order != MPID_byte_order ||
     MPID_byte_order == MPID_H_XDR)) {
    
    if (MPID_procinfo[from].byte_order == MPID_H_XDR ||
	MPID_byte_order == MPID_H_XDR) {
	d = (unsigned int *)pkt;
	for (i=0; i<size/4; i++) {
	    *d = ntohl(*d);
	    d++;
	    }
	}
    }
}

int MPID_CH_Hetero_free()
{
    if (MPID_procinfo) 
	FREE( MPID_procinfo );
    return MPI_SUCCESS;
}

/* The following routine uses unions to satisfy VERY picky C compilers).
   Thanks to Martin Frost for the following code */
int MPID_GetByteOrder( )
{
    union { 
	int l;
	char b[sizeof(int)];
    } u;

    u.l = 1;

    if (u.b[0] == 1) return 1;
    if (u.b[sizeof(int)-1] == 1) return 2;
    return 0;
}

void MPID_ByteSwapInt(
	int *buff,
	int n)
{
    int  i,j,tmp;
    char *ptr1,*ptr2 = (char *) &tmp;

    for ( j=0; j<n; j++ ) {
	ptr1 = (char *) (&buff[j]);
	for (i=0; i<sizeof(int); i++) {
	    ptr2[i] = ptr1[sizeof(int)-1-i];
	}
	buff[j] = tmp;
    }
}
