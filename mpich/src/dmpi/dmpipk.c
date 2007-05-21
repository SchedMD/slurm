/*
 *  $Id: dmpipk.c,v 1.5 1999/08/20 02:26:49 ashton Exp $
 *
 *  (C) 1994 by Argonne National Laboratory and Mississipi State University.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpidmpi.h"

/* 
   This file contains the first pass at routines to pack and unpack datatypes
   for the ADI.  THESE WILL CHANGE

   In order to aid in debugging, it is possible to cause the datatype
   pack/unpack actions to be written out.
 */

/* Pack for a send.  Eventually, this will need to handle the Heterogeneous 
   case - XXXX.  

   It also fails to detect an overrun error, or inadequate input data.
*/
void MPIR_Pack_Hvector( 
	struct MPIR_COMMUNICATOR *comm_ptr, 
	char *buf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	int dest, 
	char *outbuf )
{
int count1 = datatype->count,           /* Number of blocks */
    blocklen = datatype->blocklen;      /* Number of elements in each block */
MPI_Aint    stride   = datatype->stride;  /* Bytes between blocks */
int extent = datatype->old_type->extent;  /* Extent of underlying type */
int blen   = blocklen * extent;
int c, i, j;

/* We can't use c = count * count1 since that moves the location of the second
   of the count elements after the stride from the first, rather than after the
   last element */
c = count1;

/* Handle the special case of 4 or 8 byte items, with appropriate 
   alignment.  We do this to avoid the cost of a memcpy call for each
   element.
 */
if (blen == 4 && ((MPI_Aint)buf & 0x3) == 0 && (stride & 0x3) == 0 && 
    sizeof(int) == 4 && ((MPI_Aint)outbuf & 0x3) == 0) {
    register int *outb = (int *)outbuf, *inb = (int *)buf;
    stride = stride >> 2;
    for (j=0; j<count; j++) {
	for (i=0; i<c; i++) {
	    outb[i] = *inb;
	    inb    += stride;
	    }
	inb  -= stride;
	inb  += 1;
	outb += c;
	}
    }
else if (blen == 8 && ((MPI_Aint)buf & 0x7) == 0 && (stride & 0x7) == 0 && 
	 sizeof(double) == 8 && ((MPI_Aint)outbuf & 0x7) == 0) {
    register double *outb = (double *)outbuf, *inb = (double *)buf;
    stride = stride >> 3;
    for (j=0; j<count; j++) {
	for (i=0; i<c; i++) {
	    outb[i] = *inb;
	    inb    += stride;
	    }
	inb -= stride;
	inb += 1;
	outb += c;
	}
    }
else {
    for (j=0; j<count; j++) {
	for (i=0; i<c; i++) {
	    memcpy( outbuf, buf, blen );
	    outbuf += blen; 
	    buf    += stride;
	    }
	buf -= stride;
	buf += blen;
	}
    }
}

void MPIR_UnPack_Hvector( 
	char *inbuf, 
	int count, 
	struct MPIR_DATATYPE *datatype, 
	int source, 
	char *outbuf )
{
int count1 = datatype->count,            /* Number of blocks */
    blocklen = datatype->blocklen;       /* Number of elements in each block */
MPI_Aint    stride   = datatype->stride; /* Bytes between blocks */
int extent = datatype->old_type->extent;  /* Extent of underlying type */
int blen   = blocklen * extent;
register int c, i;
int          j;

/* We can't use c = count * count1 since that moves the location of the second
   of the count elements after the stride from the first, rather than after the
   last element */
c = count1;
if (blen == 4 && ((MPI_Aint)inbuf & 0x3) == 0 && (stride & 0x3) == 0 && 
    sizeof(int) == 4 && ((MPI_Aint)outbuf & 0x3) == 0 ) {
    register int *outb = (int *)outbuf, *inb = (int *)inbuf;
    stride = stride >> 2;
    for (j=0; j<count; j++) {
	for (i=0; i<c; i++) {
	    *outb = inb[i];
	    outb  += stride;
	    }
	outb -= stride;
	outb += 1;
	inb  += c;
	}
    }
else if (blen == 8 && ((MPI_Aint)inbuf & 0x7) == 0 && (stride & 0x7) == 0 && 
	 sizeof(double) == 8 && ((MPI_Aint)outbuf & 0x7) == 0) {
    register double *outb = (double *)outbuf, *inb = (double *)inbuf;
    stride = stride >> 3;
    for (j=0; j<count; j++) {
	for (i=0; i<c; i++) {
	    *outb   = inb[i];
	    outb    += stride;
	    }
	outb -= stride;
	outb += 1;
	inb += c;
	}
    }
else {
    for (j=0; j<count; j++) {
	for (i=0; i<c; i++) {
	    memcpy( outbuf, inbuf, blen );
	    outbuf += stride;
	    inbuf  += blen;
	    }
	outbuf -= stride;
	outbuf += blen;
	}
    }
}

/* Get the length needed for the Hvector as a contiguous lump */
int MPIR_HvectorLen( 
	int count,
	struct MPIR_DATATYPE *datatype)
{
    return datatype->size * count;
}
