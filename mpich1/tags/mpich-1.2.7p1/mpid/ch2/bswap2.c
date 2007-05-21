/* Routines to swap integral types of different byte sizes */
/* OR to use XDR (why isn't this in a separate file!?) */
#include <stdio.h>
#include "mpid.h"
#include "mpidmpi.h"
#ifndef MPID_NO_FORTRAN
/* mpifort.h is needed for conversions to/from Logical data */
#include "mpifort.h"
#endif

#ifdef MPID_HAS_HETERO
#if defined(HAS_XDR) && !defined(INCLUDED_RPC_RPC_H)
#include "rpc/rpc.h"
#define INCLUDED_RPC_RPC_H
#endif

/* Macro to swap 2 bytes without using a temp (XOR trick) */
#define SWAP2(a, b) { (a) ^= (b); (b) ^= (a); (a) ^= (b); } 

/* Byte swap an array of length n of N byte integal elements */
/* A good compiler should unroll the inner loops. Letting the compiler do it
   gives us portability.  Note that we might want to isolate the 
   cases N = 2, 4, 8 (and 16 for long double and perhaps long long)
 */
void 
MPID_BSwap_N_inplace(
	unsigned char *b,
	int N, 
	int n)
{
  int i, j;
  for (i = 0; i < n*N; i += N)
    for (j = 0; j < N/2; j ++)
      SWAP2(b[i + j], b[i + N - j - 1]);
  
}

/* Byte swap an array of length n of N byte integal elements */

void 
MPID_BSwap_N_copy(
	unsigned char *d, 
	unsigned char *s, 
	int N, 
	int n)
{
  int i, j;
  for (i = 0; i < n * N; i += N)
    for (j = 0; j < N; j++)
      d[i+j] =  s[i + N - j - 1];
  
}

int MPID_Type_swap_copy(
	unsigned char *d, 
	unsigned char *s, 
	struct MPIR_DATATYPE *t, 
	int N, 
	void *ctx) 
{
    int len;

    len = t->size * N;

    /* There are some 0-sized basic types which we can just ignore */
    if (len == 0) return MPI_SUCCESS;

  /* Some compilers issue warnings for using sizeof(...) as an int (!) */
  switch (t->dte_type) {
      case MPIR_CHAR:
      case MPIR_UCHAR:
      case MPIR_BYTE:
      case MPIR_PACKED:
      memcpy( d, s, len );
      break;
      case MPIR_SHORT:
      case MPIR_USHORT:
      MPID_BSwap_N_copy( d, s, (int)sizeof(short), N );
      break;
      case MPIR_INT:
      case MPIR_UINT:
      MPID_BSwap_N_copy( d, s, (int)sizeof(int), N );
      break;
      case MPIR_LONG:
      case MPIR_ULONG:
      MPID_BSwap_N_copy( d, s, (int)sizeof(long), N );
      break;
      case MPIR_FLOAT:
      MPID_BSwap_N_copy( d, s, (int)sizeof(float), N );
      break;
      case MPIR_COMPLEX:
      MPID_BSwap_N_copy( d, s, (int)sizeof(float), 2*N );
      break;
      case MPIR_DOUBLE:
      MPID_BSwap_N_copy( d, s, (int)sizeof(double), N );
      break;
      case MPIR_DOUBLE_COMPLEX:
      MPID_BSwap_N_copy( d, s, (int)sizeof(double), 2*N );
      break;
#ifdef HAVE_LONG_DOUBLE
      case MPIR_LONGDOUBLE:
      MPID_BSwap_N_copy( d, s, (int)sizeof(long double), N );
      break;
#endif
#ifdef HAVE_LONG_LONG_INT
      case MPIR_LONGLONGINT:
      MPID_BSwap_N_copy( d, s, (int)sizeof(long long), N );
      break;
#endif
      /* Does not handle Fortran int/float/double/logical */
      default:
      MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_INTERN, 
	       "Tried to swap unsupported type"); 
      memcpy(d, s, len);
      break;
      }
  return len;
}

void MPID_Type_swap_inplace(
	unsigned char *b, 
	struct MPIR_DATATYPE *t, 
	int N)
{

    switch (t->dte_type) {
      case MPIR_CHAR:
      case MPIR_UCHAR:
      case MPIR_BYTE:
      case MPIR_PACKED:
      break;
      case MPIR_SHORT:
      case MPIR_USHORT:
      MPID_BSwap_N_inplace( b, (int)sizeof(short), N );
      break;
      case MPIR_INT:
      case MPIR_UINT:
      MPID_BSwap_N_inplace( b, (int)sizeof(int), N );
      break;
      case MPIR_LONG:
      case MPIR_ULONG:
      MPID_BSwap_N_inplace( b, (int)sizeof(long), N );
      break;
      case MPIR_FLOAT:
      MPID_BSwap_N_inplace( b, (int)sizeof(float), N );
      break;
      case MPIR_COMPLEX:
      MPID_BSwap_N_inplace( b, (int)sizeof(float), 2*N );
      break;
      case MPIR_DOUBLE:
      MPID_BSwap_N_inplace( b, (int)sizeof(double), N );
      break;
      case MPIR_DOUBLE_COMPLEX:
      MPID_BSwap_N_inplace( b, (int)sizeof(double), 2*N );
      break;
#ifdef HAVE_LONG_DOUBLE
      case MPIR_LONGDOUBLE:
      MPID_BSwap_N_inplace( b, (int)sizeof(long double), N );
      break;
#endif
#ifdef HAVE_LONG_LONG_INT
      case MPIR_LONGLONGINT:
      MPID_BSwap_N_inplace( b, (int)sizeof(long long), N );
      break;
#endif
      /* Does not handle Fortran int/float/double/logical */
      default:
      MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_INTERN, 
	       "Tried to convert unsupported type"); 
      break;
    }
}

int MPID_Mem_convert_len( 
	MPID_Msgrep_t dest_type, 
	struct MPIR_DATATYPE *dtype_ptr, 
	int count )
{
#ifdef HAS_XDR
    if (dest_type == MPID_MSGREP_XDR) 
	return MPID_Mem_XDR_Len( dtype_ptr, count );
#endif
/* This works for both no conversion and for byte-swap/extend */
    return dtype_ptr->size * count;
}

#ifdef HAS_XDR
/* XXXX- Both of these routines need a way of returning the true size
   in bytes to the device, so it knows how much to send! */

/* 
   Here are the lengths that XDR uses for encoding data.  Note that
   everything takes at least 4 bytes 
 */
#define XDR_PAD 4
#define XDR_INT_LEN 4
#define XDR_LNG_LEN 4
#define XDR_FLT_LEN 4
#define XDR_DBL_LEN 8
#define XDR_CHR_LEN 4

int MPID_Mem_XDR_Len( 
	struct MPIR_DATATYPE *dtype_ptr, 
	int count )
{
/* XDR buffer must be a multiple of 4 in size! */
/* This is a very, very pessimistic value.  It assumes that the data 
   consists entirely of MPI_CHAR or MPI_BYTE.  Eventually, we must 
   run through the data and get the correct xdr size (or store it in the
   datatype structure for heterogeneous systems).
   4 is basically the maximum expansion size...
 */
    return (4) * count * dtype_ptr->size;
}

/* initialize an xdr buffer */
void MPID_Mem_XDR_Init( 
	char *buf, 
	int size, 
	enum xdr_op xdr_dir, 
	XDR *xdr_ctx )
{
	xdrmem_create(xdr_ctx, buf, size, xdr_dir );
}

void MPID_Mem_XDR_Free( 
	XDR *xdr_ctx )
{
	xdr_destroy( xdr_ctx );
}

/* 
   XDR has the strange design that the number of elements (but not the
   type!) preceeds a set of values.  This makes it hard to use directly
   with MPI, which requires that only the type signature of the 
   individual elements matches.  

   Because of this, we can't use the xdr_array routines.  Instead,
   we use the individual routines.
 */

/* CHANGE: This routine used to return success or failure (the value of
   which was ignored).  It has been changed to return
   number of bytes in dest (if successful)
   MPI_ERR_INTERN if error
 */
/* 
   Note that the destination is actually encoded in the xdr_ctx, and the
   parameter "d" is ignored.
 */
int MPID_Mem_XDR_Encode(
	unsigned char *d, unsigned char *s, /* dest, source */
	xdrproc_t t, /* type */
	int N, int elsize, /* count and element size */
	XDR *xdr_ctx) 
{ 
    int   rval;
    int   total; 
    int   i;

    if (!xdr_ctx) {
	fprintf( stderr, "NULL XDR CONTEXT!\n" );
	return -1;
	}
    total = xdr_getpos( xdr_ctx );
    for (i=0; i<N; i++) {
	rval = t( xdr_ctx, s );
	if (!rval) return MPI_ERR_INTERN;
	s += elsize;
	}
    total = xdr_getpos( xdr_ctx ) - total;
    return total;
}

/* Special byte version */
int MPID_Mem_XDR_ByteEncode(
	unsigned char *d, unsigned char *s, /* dest, source */
	int N, /* count */
	XDR *xdr_ctx ) 
{ 
    int   rval;
    int   total; 

    if (!xdr_ctx) {
	fprintf( stderr, "NULL XDR CONTEXT!\n" );
	return -1;
	}
    total = 0;
    total = xdr_getpos( xdr_ctx );
    rval = xdr_opaque( xdr_ctx, (char *)s, N ); 
    if (!rval) { 
	return -1;
	}
    total = xdr_getpos( xdr_ctx ) - total;
    return total;
}

#ifndef MPID_NO_FORTRAN
/* Special version for Fortran LOGICAL data */
int MPID_Mem_XDR_Encode_Logical(
	unsigned char *d, unsigned char *s, /* dest, source */
	xdrproc_t t, /* type */
	int N, /* count and element size */
	XDR *xdr_ctx) 
{ 
    int   rval;
    int   total; 
    int   i;
    int   tmpval;
    MPI_Fint *s1 = (MPI_Fint *)s;

    if (!xdr_ctx) {
	fprintf( stderr, "NULL XDR CONTEXT!\n" );
	return -1;
	}
    total = xdr_getpos( xdr_ctx );
    for (i=0; i<N; i++) {
	/* Convert Fortran logical value into C logical value */
	tmpval = MPIR_FROM_FLOG(*s1);
	rval = t( xdr_ctx, (unsigned char *)&tmpval );
	if (!rval) return MPI_ERR_INTERN;
	s1 ++;
	}
    total = xdr_getpos( xdr_ctx ) - total;
    return total;
}
#endif

/* 
 * We need to return two different lengths:
 * Length advanced in the destination (N * size) (destlen)
 * Length advanced in the source (N * xdrsize)   (srclen)
 * Note that we don't just use N * <appropriate size> because if act_bytes
 * is shorter than N * xdrsize, we'll stop early.
 */
int MPID_Mem_XDR_Decode(
	unsigned char *d, unsigned char *s, /* dest and source */
	xdrproc_t t, /* type */
	int N, int size, /* count and element size */
	int act_bytes, /* Number of bytes in message */
	int *srclen, int *destlen, 
	XDR *xdr_ctx ) 
{ 
    int rval = 1;
    int total; 
    int i;
    
    if (!xdr_ctx) {
	fprintf( stderr, "NULL XDR CONTEXT!\n" );
	return MPI_ERR_INTERN;
	}
    if (N > 0 && d == 0) {
	return MPI_ERR_BUFFER;
    }
    total = 0;
    *srclen  = xdr_getpos(xdr_ctx);
    for (i=0; i<N; i++) {
	rval = t( xdr_ctx, d );
	/* Break if at end of buffer or error */
	if (!rval) break;
	total += size;
	d    += size;
	}
    *destlen = total;
    *srclen  = xdr_getpos(xdr_ctx) - *srclen;
    /* if (!rval) error; */
    return MPI_SUCCESS;
}

/* Special byte version */
int MPID_Mem_XDR_ByteDecode(
	unsigned char *d, unsigned char *s, /* dest and source */
	int N, /* count */
	int act_bytes, /* Number of bytes in message */
	int *srclen, int *destlen, 
	XDR *xdr_ctx ) 
{ 
    int rval = 1;
    int total; 
    int xdr_len;
    
    if (!xdr_ctx) {
	fprintf( stderr, "NULL XDR CONTEXT!\n" );
	return MPI_ERR_INTERN;
	}

    total = xdr_getpos( xdr_ctx );
    xdr_len = N;
    /* printf( "xdr_len in decode is %d with a size of %d\n", xdr_len, N ); */
    rval = xdr_opaque( xdr_ctx, (char *)d, xdr_len );
    /* printf( "after decode, xdr_len = %d\n", xdr_len ); */
    *srclen = xdr_getpos( xdr_ctx ) - total;
    *destlen = N;
    if (!rval) return MPI_ERR_INTERN;
    return MPI_SUCCESS;
}

#ifndef MPID_NO_FORTRAN
int MPID_Mem_XDR_Decode_Logical(
	unsigned char *d, unsigned char *s, /* dest and source */
	xdrproc_t t, /* type */
	int N, int size, /* count and element size */
	int act_bytes, /* Number of bytes in message */
	int *srclen, int *destlen, 
	XDR *xdr_ctx ) 
{ 
    int rval = 1;
    int total; 
    int i;
    MPI_Fint tmpval;
    MPI_Fint *d1 = (MPI_Fint *)d;
    
    if (!xdr_ctx) {
	fprintf( stderr, "NULL XDR CONTEXT!\n" );
	return MPI_ERR_INTERN;
	}
    if (N > 0 && d == 0) {
	return MPI_ERR_BUFFER;
    }
    total = 0;
    *srclen  = xdr_getpos(xdr_ctx);
    for (i=0; i<N; i++) {
	rval = t( xdr_ctx, (unsigned char *)&tmpval );
	/* Break if at end of buffer or error */
	if (!rval) break;
	(*d1) = MPIR_TO_FLOG(tmpval);
	total += size;
	d1    ++;
	}
    *destlen = total;
    *srclen  = xdr_getpos(xdr_ctx) - *srclen;
    /* if (!rval) error; */
    return MPI_SUCCESS;
}
#endif

int MPID_Type_XDR_encode(
	unsigned char *d, unsigned char *s, 
	struct MPIR_DATATYPE *t, 
	int N, 
	void *ctx) 
{
  int len; 
  XDR *xdr_ctx = (XDR *)ctx;

  if (N == 0 || t->size == 0) return 0;

  switch (t->dte_type) {
      case MPIR_CHAR:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_char, N, (int)sizeof(char), xdr_ctx);
      break;
      case MPIR_UCHAR:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_u_char, N, (int)sizeof(unsigned char), xdr_ctx);
      break;

      /* Packed data is already encoded in XDR, so just copy it.  This needs
         to insert data into the xdr stream and to reset the position */
      case MPIR_PACKED:
      len = MPID_Mem_XDR_ByteEncode(d, s, N, xdr_ctx);
      break;

      /* See mpich-maint req 8262 for why we don't use ByteEncode (XDR 
	 likes rounding up to multiples of 4 bytes) */
      case MPIR_BYTE:
	  len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_u_char, N, 
				    (int)sizeof(char), xdr_ctx);
      break;

      case MPIR_SHORT:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_short, N, (int)sizeof(short), xdr_ctx);
      break;
      case MPIR_USHORT:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_u_short, N, (int)sizeof(unsigned short), xdr_ctx);
      break;
      case MPIR_INT:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_int, N, (int)sizeof(int), xdr_ctx);
      break;
      case MPIR_UINT:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_u_int, N, (int)sizeof(unsigned int), xdr_ctx);
      break;
      case MPIR_LONG:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_long, N, (int)sizeof(long), xdr_ctx);
      break;
      case MPIR_ULONG:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_u_long, N, (int)sizeof(unsigned long), xdr_ctx);
      break;
      case MPIR_FLOAT:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_float, N, (int)sizeof(float), xdr_ctx);
      break;
      case MPIR_COMPLEX:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_float, 2*N, (int)sizeof(float), xdr_ctx);
      break;
      case MPIR_DOUBLE:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_double, N, (int)sizeof(double), xdr_ctx);
      break;
      case MPIR_DOUBLE_COMPLEX:
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_double, 2*N, (int)sizeof(double), xdr_ctx);
      break;
#ifndef MPID_NO_FORTRAN
      /* Fortran logicals */
      case MPIR_LOGICAL:
      len = MPID_Mem_XDR_Encode_Logical(d, s, (xdrproc_t)xdr_int, N, xdr_ctx );
      break;
#endif
      case MPIR_LONGDOUBLE:
      MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_TYPE, 
          "Unfortuantely, XDR does not support the long double type. Sorry.");
      len = MPID_Mem_XDR_Encode(d, s, (xdrproc_t)xdr_char, N, (int)sizeof(char), xdr_ctx);
      break;
      default:
	  len = 0;
      MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_INTERN, 
		 "Tried to encode unsupported type");
      break;
    }
/* printf( "XDR encoded %d items of type %d into %d bytes\n", 
        N, t->dte_type, len ); */
return len;
}

/* 
   act_bytes is the number of bytes provided on input, and size of destination
   on output.

   Returns error code.
   Number of bytes read from s is set in srclen; number of bytes
   stored in d is set in destlen.
   Needs to return the other as well.
 */
int MPID_Type_XDR_decode(
	unsigned char *s, 
	int N, 
	struct MPIR_DATATYPE *t, 
	int elm_size, 
	unsigned char *d, 
	int srclen, 
	int *srcreadlen, 
	int *destlen, 
	void *ctx )
{
  int act_size;
  int mpi_errno = MPI_SUCCESS;
  XDR *xdr_ctx = (XDR *)ctx;

  /* printf( "Decoding %d items of type %d\n", N, t->dte_type ); */
  if (N == 0 || t->size == 0) {
      /* For example, this might be a TYPE_UB or LB, whose effect 
	 is handled by the containing struct */
      *srcreadlen = 0;
      *destlen	  = 0;
      return 0;
      }

  /* The assumption in unpack is that the user does not exceed the limits of
     the input buffer THIS IS A BAD ASSUMPTION! */
  act_size = 12 * (N + 1);
  switch (t->dte_type) {
      case MPIR_CHAR:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_char, N, (int)sizeof(char), 
				    act_size, srcreadlen, destlen, xdr_ctx );
      break;
      case MPIR_UCHAR:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_u_char, N, 
				      (int)sizeof(unsigned char), act_size, 
				      srcreadlen, destlen, xdr_ctx  );    
      break;
      case MPIR_BYTE:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_char, N, 
				      (int)sizeof(char), 
				      act_size, srcreadlen, destlen, 
				      xdr_ctx  );
      break;
      case MPIR_PACKED:
      mpi_errno = MPID_Mem_XDR_ByteDecode(d, s, N, act_size, 
					  srcreadlen, destlen, 
					  xdr_ctx  );
      break;
      case MPIR_SHORT:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_short, N, (int)sizeof(short), 
				      act_size, srcreadlen, destlen, xdr_ctx  );
      break;
      case MPIR_USHORT:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_u_short, N, 
				      (int)sizeof(unsigned short), act_size, 
				      srcreadlen, destlen, xdr_ctx  );
      break;
      case MPIR_INT:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_int, N, (int)sizeof(int), act_size,
				      srcreadlen, destlen, xdr_ctx  );
      break;
      case MPIR_UINT:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_u_int, N, 
				      (int)sizeof(unsigned int), act_size, 
				      srcreadlen, destlen, xdr_ctx  );
      break;
      case MPIR_LONG:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_long, N, (int)sizeof(long), 
				      act_size, srcreadlen, 
				      destlen, xdr_ctx  );
      break;
      case MPIR_ULONG:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_u_long, N, 
				      (int)sizeof(unsigned long), act_size, 
				      srcreadlen, destlen, xdr_ctx  );
      break;
      case MPIR_FLOAT:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_float, N, (int)sizeof(float), 
				      act_size, srcreadlen, 
				      destlen, xdr_ctx  );
      break;
      case MPIR_COMPLEX:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_float, 2*N, 
				      (int)sizeof(float), 
				      act_size, srcreadlen, 
				      destlen, xdr_ctx  );
      break;
      case MPIR_DOUBLE:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_double, N, (int)sizeof(double), 
				    act_size, srcreadlen, destlen, xdr_ctx  );
      break;
      case MPIR_DOUBLE_COMPLEX:
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_double, 2*N, 
				      (int)sizeof(double), 
				    act_size, srcreadlen, destlen, xdr_ctx  );
      break;
#ifndef MPID_NO_FORTRAN
      /* Fortran logicals */
      case MPIR_LOGICAL:
      mpi_errno = MPID_Mem_XDR_Decode_Logical(d, s, (xdrproc_t)xdr_int, N, 
					      (int)sizeof(int), 
				     act_size, srcreadlen, destlen, xdr_ctx );
      break;
#endif
      case MPIR_LONGDOUBLE:
      MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_TYPE, 
          "Unfortuantely, XDR does not support the long double type. Sorry.");
      mpi_errno = MPID_Mem_XDR_Decode(d, s, (xdrproc_t)xdr_char, N, (int)sizeof(char), 
				      act_size, srcreadlen, 
				      destlen, xdr_ctx  );
      break;
      default:
      *srcreadlen = 0;
      *destlen	  = 0;
      MPIR_ERROR(MPIR_COMM_WORLD, MPI_ERR_INTERN, 
		 "Tried to decode unsupported type");
      break;
      }
  if (mpi_errno && mpi_errno != MPI_ERR_BUFFER) {
      MPIR_ERROR( MPIR_COMM_WORLD, MPI_ERR_INTERN,
		 "Error converting data sent with XDR" );
      }
return mpi_errno;
}

#endif /* HAS_XDR */

#endif /* MPID_HAS_HETERO */
