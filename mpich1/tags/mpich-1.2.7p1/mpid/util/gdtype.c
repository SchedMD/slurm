/*
 *  $Id: gdtype.c,v 1.1.1.1 1997/09/17 20:39:34 gropp Exp $
 *
 *  (C) 1995 by Argonne National Laboratory and Mississipi State University.
 *      All rights reserved.  See COPYRIGHT in top-level directory.
 */


#ifndef lint
static char vcid[] = "$Id: gdtype.c,v 1.1.1.1 1997/09/17 20:39:34 gropp Exp $";
#endif /* lint */

#include "mpid.h"

/*
 * This file contains an implementation of the general datatype routines
 * for the ADI interm of the contiguous routines.
 *
 * This also handles the simple heterogeneous case, where all data 
 * formatting is done on a communicator basis (no special cases for
 * particular targets).
 *
 * This version simply places the non-contiguous data into a single
 * array of memory and calls the device routines.  More sophisticated
 * versions can better manage the use of storage.
 *
 * Note that these require that the field 'tmpbuf' be set to null for 
 * 'contiguous' sends and that it be tested in each nonblocking 
 * completion.
 */


void MPID_SendDatatype( comm, buf, count, datatype, src_lrank, tag, 
		        context_id, dest_grank, error_code )
MPI_Comm     comm;
void         *buf;
MPI_Datatype datatype;
int          count, src_lrank, tag, context_id, dest_grank, *error_code;
{
void *tmpbuf;
int  len;

if (datatype->dte_type == MPIR_PACKED) {
    MPID_SendContig( comm, buf, count, src_lrank, tag, 
		     context_id, dest_grank, error_code );
    }
else {
    MPIR_Pack_size( count, datatype, comm, &len );
    if (len > 0) {
	tmpbuf = (void *)MALLOC(len); 
	if (!buf) {
	    *error_code = MPIR_ERROR(comm,MPI_ERR_EXHAUSTED,"");
	    return;
	    }
	*error_code = MPIR_Pack( comm, buf, count, datatype, tmpbuf, 
				 len, &len );
	}
    else 
	tmpbuf = 0;
    MPID_SendContig( comm, tmpbuf, len, src_lrank, tag, 
		    context_id, dest_grank, error_code );
    if (tmpbuf) {
	FREE( tmpbuf );
	}
    }

return;
}

void MPID_RecvDatatype( comm, buf, maxcount, datatype, src_lrank, tag, 
		        context_id, status,  error_code )
MPI_Comm     comm;
void         *buf;
MPI_Datatype datatype;
int          maxcount, src_lrank, tag, context_id, dest_grank, *error_code;
MPI_Status   *status;
{
void *tmpbuf;
int  len;

MPIR_Pack_size( maxcount, datatype, comm, &len );
if (len > 0) {
    tmpbuf = (void *)MALLOC(len); 
    if (!buf) {
	*error_code = MPIR_ERROR(comm,MPI_ERR_EXHAUSTED,"");
	return;
	}
    }
else 
    tmpbuf = 0;
MPID_RecvContig( comm, tmpbuf, len, src_lrank, tag, 
		       context_id, dest_grank, status, error_code );
if (tmpbuf) {
    /* MPI_PACKED data should be handled in MPIR_UnPack (it isn't currently) */
    len = status->count;
    *error_code = MPIR_UnPack( comm, tmpbuf, len, maxcount, datatype, 
                               msgrep?, buf, 
			       &status->count, &status->count );
    FREE( tmpbuf );
    }
return;
}


/* 
 * Nonblocking versions.
 *
 * These are like the blocking versions, except they must save the temporary
 * buffers in the request.
 */
void MPID_IsendDatatype( comm, buf, count, datatype, src_lrank, tag, 
		         context_id, dest_grank, request, error_code )
MPI_Comm     comm;
void         *buf;
MPI_Datatype datatype;
MPI_Request  request;
int          count, src_lrank, tag, context_id, dest_grank, *error_code;
{
void *tmpbuf;
int  len;

MPIR_Pack_size( count, datatype, comm, &len );
if (len > 0) {
    tmpbuf = (void *)MALLOC(len); 
    if (!buf) {
	*error_code = MPIR_ERROR(comm,MPI_ERR_EXHAUSTED,"");
	return;
	}
    *error_code = MPIR_Pack( comm, buf, count, datatype, tmpbuf, len, &len );
    }
else 
    tmpbuf = 0;

request->dev_shandle.tmpbuf = tmpbuf;
MPID_IsendContig( comm, tmpbuf, len, src_lrank, tag, 
		        context_id, dest_grank, request, error_code );
return;
}

void MPID_IrecvDatatype( comm, buf, maxcount, datatype, src_lrank, tag, 
		         context_id, request,  error_code )
MPI_Comm     comm;
void         *buf;
MPI_Datatype datatype;
MPI_Request  request;
int          maxcount, src_lrank, tag, context_id, dest_grank, *error_code;
{
void *tmpbuf;
int  len;

MPIR_Pack_size( maxcount, datatype, comm, &len );
if (len > 0) {
    tmpbuf = (void *)MALLOC(len); 
    if (!buf) {
	*error_code = MPIR_ERROR(comm,MPI_ERR_EXHAUSTED,"");
	return;
	}
    }
else 
    tmpbuf = 0;
request->dev_rhandle.tmpbuf = tmpbuf;
MPID_IrecvContig( comm, tmpbuf, len, src_lrank, tag, 
		       context_id, dest_grank, request, error_code );
return;
}

void MPID_SendDatatypeLong( comm, buf, count, datatype, src_lrank, tag, 
			    context_id, dest_grank, error_code )
MPI_Comm     comm;
void         *buf;
MPI_Datatype datatype;
int          count, src_lrank, tag, context_id, dest_grank, *error_code;
{
void *tmpbuf;
int  len;

MPIR_Pack_size( count, datatype, comm, &len );
if (len > 0) {
    tmpbuf = (void *)MALLOC(len); 
    if (!buf) {
	*error_code = MPIR_ERROR(comm,MPI_ERR_EXHAUSTED,"");
	return;
	}
    *error_code = MPIR_Pack( comm, buf, count, datatype, tmpbuf, len, &len );
    }
else 
    tmpbuf = 0;
MPID_SendContigLong( comm, tmpbuf, len, src_lrank, tag, 
		       context_id, dest_grank, error_code );
if (tmpbuf) {
    FREE( tmpbuf );
    }
return;
}

/*
??? Should the request type OR the completer OR some data item contain the
information on whether any extra cleanup code is needed?
 */


void MPID_RecvDatatypeCmpl( request )
MPI_Request request;
{
void *tmpbuf = request->rhandle.dev_rhandle.tmpbuf;
if (tmpbuf) {
    /* MPI_PACKED data should be handled in MPIR_UnPack (it isn't currently) */
    len = status->count;
    /* All of this is saved in the request by the RecvDatatype routine */
    *error_code = MPIR_UnPack( request->comm, tmpbuf, len, maxcount, 
			       datatype, 
                               request->rhandle.dev_rhandle.msgrep, 
			       request->rhandle.buf, 
			       &request->rhandle.s.count, 
			       &request->rhandle.s.count );
    FREE( tmpbuf );
    }
}

/*
 * Completing a send is simpler - just free the temp buf, if it was set.
 */
void MPID_SendDatatypeCmpl( request )
MPI_Request request;
{
void *tmpbuf = request->shandle.dev_shandle.tmpbuf;
if (tmpbuf) {
    FREE( tmpbuf );
    }
}
