/* This is Duff's device for doing copies with an unrolled loop.
   This uses a little-known feature of C to handle, with a single loop,
   both the unrolled code and the boundary case.

   This is included as a macro so that we can experiment with some
   variations, including ones that use longer datatypes for loads and
   stores

   For best performance, this should be compiled with options to exploit
   aligned word accesses (for example, use load-double and store-double
   for 8-byte objects).  On a Sun4, this is "-O4 -dalign".

   On an Rs6000, using xlC, try -O3 
 */    

#define DUFFCOPY8(dest,src,n) \
switch ((n) & 0x7) {\
	do {\
		case 0: *dest++ = *src++;\
		case 7: *dest++ = *src++;\
		case 6: *dest++ = *src++;\
		case 5: *dest++ = *src++;\
		case 4: *dest++ = *src++;\
		case 3: *dest++ = *src++;\
		case 2: *dest++ = *src++;\
		case 1: *dest++ = *src++;\
		n -= 8;\
	} while (n > 0);\
}
#define DUFFCOPY4(dest,src,n) \
switch ((n) & 0x3) {\
	do {\
		case 0: *dest++ = *src++;\
		case 3: *dest++ = *src++;\
		case 2: *dest++ = *src++;\
		case 1: *dest++ = *src++;\
		n -= 4;\
	} while (n > 0);\
}

/* These type defs need to use configure-determined values for lengths */
#define INT_SIZE_4
#define DOUBLE_SIZE_8

/* 4 byte type */
#ifdef INT_SIZE_4
typedef int MPIR_int32;
#elif defined(SHORT_SIZE_4)
typedef short MPIR_int32;
#else
 ERROR - no 4 byte type
#endif

/* 8 byte type */
#if defined(INT_SIZE_8)
typedef int MPIR_dbl64;
#elif defined(LONG_SIZE_8)
typedef long MPIR_dbl64;
#elif defined(DOUBLE_SIZE_8)
typedef double MPIR_dbl64;
#elif defined(FLOAT_SIZE_8)
typedef float MPIR_dbl64;
#else
 ERROR - no 8 byte type
#endif

/* Other typedefs ... 
typedef long long int MPIR_int128;
*/

/* Select disjoint pragma if possible */
#ifdef MPI_rs6000
#define HAVE_DISJOINT
#endif

/*
 * This is a version of memcpy that tries to use longer loads and stores,
 * as well as unrolled copy loops.
 */
MPIR_memcpy( dest, src, n )
void *dest, *src;
register int  n;
{
char *d8 = (char *)dest, *s8 = (char *)src;
#ifdef HAVE_DISJOINT
/* This pragma fails.  */
/* #pragma disjoint (*d8,*s8) */
#endif
MPI_Aint idest = (MPI_Aint)d8, isrc = (MPI_Aint)s8;
/* MPIR_int128 *d128, *s128; */

/*
   Perform a few tests for data lengths and alignments.  Two possibilities:
   Only handle case where data starts on appropriate alignements
   Handle all cases (where alignment is conformable)
 */
switch (n & 0x7) {
    case 0:
    	/* Eight byte alignment */
     	if ( (idest & 0x7) == 0 && (isrc & 0x7) == 0 ) {
	    register MPIR_dbl64 *d64, *s64;
#ifdef HAVE_DISJOINT
#pragma disjoint (*d64,*s64)
#endif
     	    d64 = (MPIR_dbl64 *)dest;
     	    s64 = (MPIR_dbl64 *)src;
     	    n >>= 3;
	    DUFFCOPY8( d64, s64, n );
	    return;
    	    }
    	/* Otherwise, fall through and try next alignment */
    case 4:
    	/* Four byte alignment */
     	if ( (idest & 0x3) == 0 && (isrc & 0x3) == 0 ) {
	    register MPIR_int32 *d32, *s32;
#ifdef HAVE_DISJOINT
#pragma disjoint (*d32,*s32)
#endif
     	    d32 = (MPIR_int32 *)dest;
     	    s32 = (MPIR_int32 *)src;
     	    n >>= 2;
	    DUFFCOPY8( d32, s32, n );
	    return;
    	    }
    	/* Otherwise, fall through and try next alignment */
    /* Could do 2 byte alignment .... */
    default:
    	/* Everything else */
    	DUFFCOPY8( d8, s8, n );
    }
}
