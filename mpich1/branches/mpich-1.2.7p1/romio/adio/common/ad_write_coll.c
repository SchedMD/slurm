/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"
#ifdef PROFILE
#include "mpe.h"
#endif

/* prototypes of functions used for collective writes only. */
static void ADIOI_Exch_and_write(ADIO_File fd, void *buf, MPI_Datatype
                         datatype, int nprocs, int myrank, ADIOI_Access
                         *others_req, ADIO_Offset *offset_list,
                         int *len_list, int contig_access_count, ADIO_Offset
                         min_st_offset, ADIO_Offset fd_size,
                         ADIO_Offset *fd_start, ADIO_Offset *fd_end,
                         int *buf_idx, int *error_code);
static void ADIOI_W_Exchange_data(ADIO_File fd, void *buf, char *write_buf,
                         ADIOI_Flatlist_node *flat_buf, ADIO_Offset 
                         *offset_list, int *len_list, int *send_size, 
                         int *recv_size, ADIO_Offset off, int size,
                         int *count, int *start_pos, int *partial_recv, 
                         int *sent_to_proc, int nprocs, 
                         int myrank, int
                         buftype_is_contig, int contig_access_count,
                         ADIO_Offset min_st_offset, ADIO_Offset fd_size,
                         ADIO_Offset *fd_start, ADIO_Offset *fd_end, 
                         ADIOI_Access *others_req, 
                         int *send_buf_idx, int *curr_to_proc,
                         int *done_to_proc, int *hole, int iter, 
                         MPI_Aint buftype_extent, int *buf_idx, int *error_code);
static void ADIOI_Fill_send_buffer(ADIO_File fd, void *buf, ADIOI_Flatlist_node
                           *flat_buf, char **send_buf, ADIO_Offset 
                           *offset_list, int *len_list, int *send_size, 
                           MPI_Request *requests, int *sent_to_proc, 
                           int nprocs, int myrank, 
                           int contig_access_count, ADIO_Offset
                           min_st_offset, ADIO_Offset fd_size,
                           ADIO_Offset *fd_start, ADIO_Offset *fd_end, 
                           int *send_buf_idx, int *curr_to_proc, 
                           int *done_to_proc, int iter, 
                           MPI_Aint buftype_extent);
static void ADIOI_Heap_merge(ADIOI_Access *others_req, int *count, 
                      ADIO_Offset *srt_off, int *srt_len, int *start_pos,
                      int nprocs, int nprocs_recv, int total_elements);


void ADIOI_GEN_WriteStridedColl(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
/* Uses a generalized version of the extended two-phase method described
   in "An Extended Two-Phase Method for Accessing Sections of 
   Out-of-Core Arrays", Rajeev Thakur and Alok Choudhary,
   Scientific Programming, (5)4:301--317, Winter 1996. 
   http://www.mcs.anl.gov/home/thakur/ext2ph.ps */

    ADIOI_Access *my_req; 
    /* array of nprocs access structures, one for each other process in
       whose file domain this process's request lies */
    
    ADIOI_Access *others_req;
    /* array of nprocs access structures, one for each other process
       whose request lies in this process's file domain. */

    int i, filetype_is_contig, nprocs, nprocs_for_coll, myrank;
    int contig_access_count=0, interleave_count = 0, buftype_is_contig;
    int *count_my_req_per_proc, count_my_req_procs, count_others_req_procs;
    ADIO_Offset orig_fp, start_offset, end_offset, fd_size, min_st_offset, off;
    ADIO_Offset *offset_list = NULL, *st_offsets = NULL, *fd_start = NULL,
	*fd_end = NULL, *end_offsets = NULL;
    int *buf_idx = NULL, *len_list = NULL;


#ifdef PROFILE
	MPE_Log_event(13, 0, "start computation");
#endif

    MPI_Comm_size(fd->comm, &nprocs);
    MPI_Comm_rank(fd->comm, &myrank);

/* the number of processes that actually perform I/O, nprocs_for_coll,
 * is stored in the hints off the ADIO_File structure
 */
    nprocs_for_coll = fd->hints->cb_nodes;
    orig_fp = fd->fp_ind;

    /* only check for interleaving if cb_write isn't disabled */
    if (fd->hints->cb_write != ADIOI_HINT_DISABLE) {
	/* For this process's request, calculate the list of offsets and
	   lengths in the file and determine the start and end offsets. */

	/* Note: end_offset points to the last byte-offset that will be accessed.
	   e.g., if start_offset=0 and 100 bytes to be read, end_offset=99*/

	ADIOI_Calc_my_off_len(fd, count, datatype, file_ptr_type, offset,
			      &offset_list, &len_list, &start_offset,
			      &end_offset, &contig_access_count); 

	/* each process communicates its start and end offsets to other 
	   processes. The result is an array each of start and end offsets stored
	   in order of process rank. */ 
    
	st_offsets = (ADIO_Offset *) ADIOI_Malloc(nprocs*sizeof(ADIO_Offset));
	end_offsets = (ADIO_Offset *) ADIOI_Malloc(nprocs*sizeof(ADIO_Offset));

	MPI_Allgather(&start_offset, 1, ADIO_OFFSET, st_offsets, 1,
		      ADIO_OFFSET, fd->comm);
	MPI_Allgather(&end_offset, 1, ADIO_OFFSET, end_offsets, 1,
		      ADIO_OFFSET, fd->comm);

	/* are the accesses of different processes interleaved? */
	for (i=1; i<nprocs; i++)
	    if ((st_offsets[i] < end_offsets[i-1]) && 
                (st_offsets[i] <= end_offsets[i]))
                interleave_count++;
	/* This is a rudimentary check for interleaving, but should suffice
	   for the moment. */
    }

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);

    if (fd->hints->cb_write == ADIOI_HINT_DISABLE ||
	(!interleave_count && (fd->hints->cb_write == ADIOI_HINT_AUTO)))
    {
	/* use independent accesses */
	if (fd->hints->cb_write != ADIOI_HINT_DISABLE) {
	    ADIOI_Free(offset_list);
	    ADIOI_Free(len_list);
	    ADIOI_Free(st_offsets);
	    ADIOI_Free(end_offsets);
	}

	fd->fp_ind = orig_fp;
        ADIOI_Datatype_iscontig(fd->filetype, &filetype_is_contig);

        if (buftype_is_contig && filetype_is_contig) {
            if (file_ptr_type == ADIO_EXPLICIT_OFFSET) {
                off = fd->disp + (fd->etype_size) * offset;
                ADIO_WriteContig(fd, buf, count, datatype,
				 ADIO_EXPLICIT_OFFSET,
				 off, status, error_code);
            }
            else ADIO_WriteContig(fd, buf, count, datatype, ADIO_INDIVIDUAL,
				  0, status, error_code);
        }
	else ADIO_WriteStrided(fd, buf, count, datatype, file_ptr_type,
			       offset, status, error_code);

	return;
    }

/* Divide the I/O workload among "nprocs_for_coll" processes. This is
   done by (logically) dividing the file into file domains (FDs); each
   process may directly access only its own file domain. */

    ADIOI_Calc_file_domains(st_offsets, end_offsets, nprocs,
			    nprocs_for_coll, &min_st_offset,
			    &fd_start, &fd_end, &fd_size);   


/* calculate what portions of the access requests of this process are
   located in what file domains */

    ADIOI_Calc_my_req(fd, offset_list, len_list, contig_access_count,
		      min_st_offset, fd_start, fd_end, fd_size,
		      nprocs, &count_my_req_procs, 
		      &count_my_req_per_proc, &my_req,
		      &buf_idx); 

/* based on everyone's my_req, calculate what requests of other
   processes lie in this process's file domain.
   count_others_req_procs = number of processes whose requests lie in
   this process's file domain (including this process itself) 
   count_others_req_per_proc[i] indicates how many separate contiguous
   requests of proc. i lie in this process's file domain. */

    ADIOI_Calc_others_req(fd, count_my_req_procs, 
			  count_my_req_per_proc, my_req, 
			  nprocs, myrank,
			  &count_others_req_procs, &others_req); 
    
    ADIOI_Free(count_my_req_per_proc);
    for (i=0; i < nprocs; i++) {
	if (my_req[i].count) {
	    ADIOI_Free(my_req[i].offsets);
	    ADIOI_Free(my_req[i].lens);
	}
    }
    ADIOI_Free(my_req);

/* exchange data and write in sizes of no more than coll_bufsize. */
    ADIOI_Exch_and_write(fd, buf, datatype, nprocs, myrank,
                        others_req, offset_list,
			len_list, contig_access_count, min_st_offset,
			fd_size, fd_start, fd_end, buf_idx, error_code);

    if (!buftype_is_contig) ADIOI_Delete_flattened(datatype);

/* free all memory allocated for collective I/O */

    for (i=0; i<nprocs; i++) {
	if (others_req[i].count) {
	    ADIOI_Free(others_req[i].offsets);
	    ADIOI_Free(others_req[i].lens);
	    ADIOI_Free(others_req[i].mem_ptrs);
	}
    }
    ADIOI_Free(others_req);

    ADIOI_Free(buf_idx);
    ADIOI_Free(offset_list);
    ADIOI_Free(len_list);
    ADIOI_Free(st_offsets);
    ADIOI_Free(end_offsets);
    ADIOI_Free(fd_start);
    ADIOI_Free(fd_end);

#ifdef HAVE_STATUS_SET_BYTES
    if (status) {
      int bufsize, size;
      /* Don't set status if it isn't needed */
      MPI_Type_size(datatype, &size);
      bufsize = size * count;
      MPIR_Status_set_bytes(status, datatype, bufsize);
    }
/* This is a temporary way of filling in status. The right way is to 
   keep track of how much data was actually written during collective I/O. */
#endif

    fd->fp_sys_posn = -1;   /* set it to null. */
}



/* If successful, error_code is set to MPI_SUCCESS.  Otherwise an error
 * code is created and returned in error_code.
 */
static void ADIOI_Exch_and_write(ADIO_File fd, void *buf, MPI_Datatype
				 datatype, int nprocs, int myrank,
				 ADIOI_Access
				 *others_req, ADIO_Offset *offset_list,
				 int *len_list, int contig_access_count,
				 ADIO_Offset
				 min_st_offset, ADIO_Offset fd_size,
				 ADIO_Offset *fd_start, ADIO_Offset *fd_end,
				 int *buf_idx, int *error_code)
{
/* Send data to appropriate processes and write in sizes of no more
   than coll_bufsize.    
   The idea is to reduce the amount of extra memory required for
   collective I/O. If all data were written all at once, which is much
   easier, it would require temp space more than the size of user_buf,
   which is often unacceptable. For example, to write a distributed
   array to a file, where each local array is 8Mbytes, requiring
   at least another 8Mbytes of temp space is unacceptable. */

    int hole, i, j, m, size=0, ntimes, max_ntimes, buftype_is_contig;
    ADIO_Offset st_loc=-1, end_loc=-1, off, done, req_off;
    char *write_buf=NULL;
    int *curr_offlen_ptr, *count, *send_size, req_len, *recv_size;
    int *partial_recv, *sent_to_proc, *start_pos, flag;
    int *send_buf_idx, *curr_to_proc, *done_to_proc;
    MPI_Status status;
    ADIOI_Flatlist_node *flat_buf=NULL;
    MPI_Aint buftype_extent;
    int info_flag, coll_bufsize;
    char *value;
    static char myname[] = "ADIOI_EXCH_AND_WRITE";

    *error_code = MPI_SUCCESS;  /* changed below if error */
    /* only I/O errors are currently reported */

/* calculate the number of writes of size coll_bufsize
   to be done by each process and the max among all processes.
   That gives the no. of communication phases as well. */

    value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));
    MPI_Info_get(fd->info, "cb_buffer_size", MPI_MAX_INFO_VAL, value, 
                 &info_flag);
    coll_bufsize = atoi(value);
    ADIOI_Free(value);


    for (i=0; i < nprocs; i++) {
	if (others_req[i].count) {
	    st_loc = others_req[i].offsets[0];
	    end_loc = others_req[i].offsets[0];
	    break;
	}
    }

    for (i=0; i < nprocs; i++)
	for (j=0; j < others_req[i].count; j++) {
	    st_loc = ADIOI_MIN(st_loc, others_req[i].offsets[j]);
	    end_loc = ADIOI_MAX(end_loc, (others_req[i].offsets[j]
				       + others_req[i].lens[j] - 1));
	}

/* ntimes=ceiling_div(end_loc - st_loc + 1, coll_bufsize)*/

    ntimes = (int) ((end_loc - st_loc + coll_bufsize)/coll_bufsize);

    if ((st_loc==-1) && (end_loc==-1)) {
	ntimes = 0; /* this process does no writing. */
    }

    MPI_Allreduce(&ntimes, &max_ntimes, 1, MPI_INT, MPI_MAX,
		  fd->comm); 

    if (ntimes) write_buf = (char *) ADIOI_Malloc(coll_bufsize);

    curr_offlen_ptr = (int *) ADIOI_Calloc(nprocs, sizeof(int)); 
    /* its use is explained below. calloc initializes to 0. */

    count = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    /* to store count of how many off-len pairs per proc are satisfied
       in an iteration. */

    partial_recv = (int *) ADIOI_Calloc(nprocs, sizeof(int));
    /* if only a portion of the last off-len pair is recd. from a process
       in a particular iteration, the length recd. is stored here.
       calloc initializes to 0. */

    send_size = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    /* total size of data to be sent to each proc. in an iteration.
       Of size nprocs so that I can use MPI_Alltoall later. */

    recv_size = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    /* total size of data to be recd. from each proc. in an iteration.*/

    sent_to_proc = (int *) ADIOI_Calloc(nprocs, sizeof(int));
    /* amount of data sent to each proc so far. Used in
       ADIOI_Fill_send_buffer. initialized to 0 here. */

    send_buf_idx = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    curr_to_proc = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    done_to_proc = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    /* Above three are used in ADIOI_Fill_send_buffer*/

    start_pos = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    /* used to store the starting value of curr_offlen_ptr[i] in 
       this iteration */

    ADIOI_Datatype_iscontig(datatype, &buftype_is_contig);
    if (!buftype_is_contig) {
	ADIOI_Flatten_datatype(datatype);
	flat_buf = ADIOI_Flatlist;
        while (flat_buf->type != datatype) flat_buf = flat_buf->next;
    }
    MPI_Type_extent(datatype, &buftype_extent);


/* I need to check if there are any outstanding nonblocking writes to
   the file, which could potentially interfere with the writes taking
   place in this collective write call. Since this is not likely to be
   common, let me do the simplest thing possible here: Each process
   completes all pending nonblocking operations before completing. */

    ADIOI_Complete_async(error_code);
    if (*error_code != MPI_SUCCESS) return;
    MPI_Barrier(fd->comm);

    done = 0;
    off = st_loc;

#ifdef PROFILE
	MPE_Log_event(14, 0, "end computation");
#endif

    for (m=0; m < ntimes; m++) {
       /* go through all others_req and check which will be satisfied
          by the current write */

       /* Note that MPI guarantees that displacements in filetypes are in 
          monotonically nondecreasing order and that, for writes, the
	  filetypes cannot specify overlapping regions in the file. This
	  simplifies implementation a bit compared to reads. */

          /* off = start offset in the file for the data to be written in 
                   this iteration 
             size = size of data written (bytes) corresponding to off
             req_off = off in file for a particular contiguous request 
                       minus what was satisfied in previous iteration
             req_size = size corresponding to req_off */

	/* first calculate what should be communicated */

#ifdef PROFILE
	MPE_Log_event(13, 0, "start computation");
#endif
	for (i=0; i < nprocs; i++) count[i] = recv_size[i] = 0;

	size = (int) (ADIOI_MIN(coll_bufsize, end_loc-st_loc+1-done)); 

	for (i=0; i < nprocs; i++) {
	    if (others_req[i].count) {
		start_pos[i] = curr_offlen_ptr[i];
		for (j=curr_offlen_ptr[i]; j<others_req[i].count; j++) {
		    if (partial_recv[i]) {
			/* this request may have been partially
			   satisfied in the previous iteration. */
			req_off = others_req[i].offsets[j] +
			    partial_recv[i]; 
                        req_len = others_req[i].lens[j] -
			    partial_recv[i];
			partial_recv[i] = 0;
			/* modify the off-len pair to reflect this change */
			others_req[i].offsets[j] = req_off;
			others_req[i].lens[j] = req_len;
		    }
		    else {
			req_off = others_req[i].offsets[j];
                        req_len = others_req[i].lens[j];
		    }
		    if (req_off < off + size) {
			count[i]++;
			MPI_Address(write_buf+req_off-off, 
                               &(others_req[i].mem_ptrs[j]));
			recv_size[i] += (int)(ADIOI_MIN(off + (ADIO_Offset)size - 
						  req_off, req_len));

			if (off+size-req_off < req_len)
			{
			    partial_recv[i] = (int) (off + size - req_off);

			    /* --BEGIN ERROR HANDLING-- */
			    if ((j+1 < others_req[i].count) && 
                                 (others_req[i].offsets[j+1] < off+size))
			    { 
				*error_code = MPIO_Err_create_code(MPI_SUCCESS,
								   MPIR_ERR_RECOVERABLE,
								   myname,
								   __LINE__,
								   MPI_ERR_ARG,
								   "Filetype specifies overlapping write regions (which is illegal according to the MPI-2 specification)", 0);
				/* allow to continue since additional
				 * communication might have to occur
				 */
			    }
			    /* --END ERROR HANDLING-- */
			    break;
			}
		    }
		    else break;
		}
		curr_offlen_ptr[i] = j;
	    }
	}
	
#ifdef PROFILE
	MPE_Log_event(14, 0, "end computation");
	MPE_Log_event(7, 0, "start communication");
#endif
	ADIOI_W_Exchange_data(fd, buf, write_buf, flat_buf, offset_list, 
                            len_list, send_size, recv_size, off, size, count, 
                            start_pos, partial_recv, 
                            sent_to_proc, nprocs, myrank, 
			    buftype_is_contig, contig_access_count,
			    min_st_offset, fd_size, fd_start, fd_end,
			    others_req, send_buf_idx, curr_to_proc,
                            done_to_proc, &hole, m, buftype_extent, buf_idx,
			    error_code); 
        if (*error_code != MPI_SUCCESS) return;
#ifdef PROFILE
	MPE_Log_event(8, 0, "end communication");
#endif

	flag = 0;
	for (i=0; i<nprocs; i++)
	    if (count[i]) flag = 1;

	if (flag) {
	    ADIO_WriteContig(fd, write_buf, size, MPI_BYTE, ADIO_EXPLICIT_OFFSET, 
                        off, &status, error_code);
	    if (*error_code != MPI_SUCCESS) return;
	}

	off += size;
	done += size;
    }

    for (i=0; i<nprocs; i++) count[i] = recv_size[i] = 0;
#ifdef PROFILE
	MPE_Log_event(7, 0, "start communication");
#endif
    for (m=ntimes; m<max_ntimes; m++) 
	/* nothing to recv, but check for send. */
	ADIOI_W_Exchange_data(fd, buf, write_buf, flat_buf, offset_list, 
                            len_list, send_size, recv_size, off, size, count, 
                            start_pos, partial_recv, 
                            sent_to_proc, nprocs, myrank, 
			    buftype_is_contig, contig_access_count,
			    min_st_offset, fd_size, fd_start, fd_end,
			    others_req, send_buf_idx, 
                            curr_to_proc, done_to_proc, &hole, m, 
                            buftype_extent, buf_idx, error_code); 
        if (*error_code != MPI_SUCCESS) return;
#ifdef PROFILE
	MPE_Log_event(8, 0, "end communication");
#endif

    if (ntimes) ADIOI_Free(write_buf);
    ADIOI_Free(curr_offlen_ptr);
    ADIOI_Free(count);
    ADIOI_Free(partial_recv);
    ADIOI_Free(send_size);
    ADIOI_Free(recv_size);
    ADIOI_Free(sent_to_proc);
    ADIOI_Free(start_pos);
    ADIOI_Free(send_buf_idx);
    ADIOI_Free(curr_to_proc);
    ADIOI_Free(done_to_proc);
}


/* Sets error_code to MPI_SUCCESS if successful, or creates an error code
 * in the case of error.
 */
static void ADIOI_W_Exchange_data(ADIO_File fd, void *buf, char *write_buf,
				  ADIOI_Flatlist_node *flat_buf, ADIO_Offset 
				  *offset_list, int *len_list, int *send_size, 
				  int *recv_size, ADIO_Offset off, int size,
				  int *count, int *start_pos,
				  int *partial_recv,
				  int *sent_to_proc, int nprocs, 
				  int myrank, int
				  buftype_is_contig, int contig_access_count,
				  ADIO_Offset min_st_offset,
				  ADIO_Offset fd_size,
				  ADIO_Offset *fd_start, ADIO_Offset *fd_end, 
				  ADIOI_Access *others_req, 
				  int *send_buf_idx, int *curr_to_proc,
				  int *done_to_proc, int *hole, int iter, 
				  MPI_Aint buftype_extent, int *buf_idx,
				  int *error_code)
{
    int i, j, k, *tmp_len, nprocs_recv, nprocs_send, err;
    char **send_buf = NULL; 
    MPI_Request *requests, *send_req;
    MPI_Datatype *recv_types;
    MPI_Status *statuses, status;
    int *srt_len, sum;
    ADIO_Offset *srt_off;
    static char myname[] = "ADIOI_W_EXCHANGE_DATA";

/* exchange recv_size info so that each process knows how much to
   send to whom. */

    MPI_Alltoall(recv_size, 1, MPI_INT, send_size, 1, MPI_INT, fd->comm);

    /* create derived datatypes for recv */

    nprocs_recv = 0;
    for (i=0; i<nprocs; i++) if (recv_size[i]) nprocs_recv++;

    recv_types = (MPI_Datatype *)
	ADIOI_Malloc((nprocs_recv+1)*sizeof(MPI_Datatype)); 
/* +1 to avoid a 0-size malloc */

    tmp_len = (int *) ADIOI_Malloc(nprocs*sizeof(int));
    j = 0;
    for (i=0; i<nprocs; i++) {
	if (recv_size[i]) {
/* take care if the last off-len pair is a partial recv */
	    if (partial_recv[i]) {
		k = start_pos[i] + count[i] - 1;
		tmp_len[i] = others_req[i].lens[k];
		others_req[i].lens[k] = partial_recv[i];
	    }
	    MPI_Type_hindexed(count[i], 
                 &(others_req[i].lens[start_pos[i]]),
	             &(others_req[i].mem_ptrs[start_pos[i]]), 
			 MPI_BYTE, recv_types+j);
	    /* absolute displacements; use MPI_BOTTOM in recv */
	    MPI_Type_commit(recv_types+j);
	    j++;
	}
    }

    /* To avoid a read-modify-write, check if there are holes in the 
       data to be written. For this, merge the (sorted) offset lists
       others_req using a heap-merge. */

    sum = 0;
    for (i=0; i<nprocs; i++) sum += count[i];
    srt_off = (ADIO_Offset *) ADIOI_Malloc((sum+1)*sizeof(ADIO_Offset));
    srt_len = (int *) ADIOI_Malloc((sum+1)*sizeof(int));
    /* +1 to avoid a 0-size malloc */

    ADIOI_Heap_merge(others_req, count, srt_off, srt_len, start_pos,
                     nprocs, nprocs_recv, sum);

/* for partial recvs, restore original lengths */
    for (i=0; i<nprocs; i++) 
        if (partial_recv[i]) {
            k = start_pos[i] + count[i] - 1;
            others_req[i].lens[k] = tmp_len[i];
        }
    ADIOI_Free(tmp_len);

/* check if there are any holes */
    *hole = 0;
    for (i=0; i<sum-1; i++)
	if (srt_off[i]+srt_len[i] < srt_off[i+1]) {
	    *hole = 1;
	    break;
	}

    ADIOI_Free(srt_off);
    ADIOI_Free(srt_len);

    if (nprocs_recv) {
	if (*hole) {
	    ADIO_ReadContig(fd, write_buf, size, MPI_BYTE, 
			    ADIO_EXPLICIT_OFFSET, off, &status, &err);
	    /* --BEGIN ERROR HANDLING-- */
	    if (err != MPI_SUCCESS) {
		*error_code = MPIO_Err_create_code(err,
						   MPIR_ERR_RECOVERABLE, myname,
						   __LINE__, MPI_ERR_IO,
						   "**ioRMWrdwr", 0);
		return;
	    } 
	    /* --END ERROR HANDLING-- */
	}
    }

    nprocs_send = 0;
    for (i=0; i < nprocs; i++) if (send_size[i]) nprocs_send++;

    if (fd->atomicity) {
        /* bug fix from Wei-keng Liao and Kenin Coloma */
        requests = (MPI_Request *)
	    ADIOI_Malloc((nprocs_send+1)*sizeof(MPI_Request)); 
        send_req = requests;
    }
    else {
        requests = (MPI_Request *) 	
            ADIOI_Malloc((nprocs_send+nprocs_recv+1)*sizeof(MPI_Request)); 
        /* +1 to avoid a 0-size malloc */

        /* post receives */
        j = 0;
        for (i=0; i<nprocs; i++) {
            if (recv_size[i]) {
                MPI_Irecv(MPI_BOTTOM, 1, recv_types[j], i, myrank+i+100*iter,
                          fd->comm, requests+j);
                j++;
            }
        }
	send_req = requests + nprocs_recv;
    }

/* post sends. if buftype_is_contig, data can be directly sent from
   user buf at location given by buf_idx. else use send_buf. */

    if (buftype_is_contig) {
	j = 0;
	for (i=0; i < nprocs; i++) 
	    if (send_size[i]) {
		MPI_Isend(((char *) buf) + buf_idx[i], send_size[i], 
  		            MPI_BYTE, i,  myrank+i+100*iter, fd->comm, 
                                  send_req+j);
		j++;
                buf_idx[i] += send_size[i];
	    }
    }
    else if (nprocs_send) {
	/* buftype is not contig */
	send_buf = (char **) ADIOI_Malloc(nprocs*sizeof(char*));
	for (i=0; i < nprocs; i++) 
	    if (send_size[i]) 
		send_buf[i] = (char *) ADIOI_Malloc(send_size[i]);

	ADIOI_Fill_send_buffer(fd, buf, flat_buf, send_buf,
                           offset_list, len_list, send_size, 
			   send_req,
                           sent_to_proc, nprocs, myrank, 
                           contig_access_count,
                           min_st_offset, fd_size, fd_start, fd_end, 
                           send_buf_idx, curr_to_proc, done_to_proc, iter,
                           buftype_extent);
        /* the send is done in ADIOI_Fill_send_buffer */
    }

    if (fd->atomicity) {
        /* bug fix from Wei-keng Liao and Kenin Coloma */
        j = 0;
        for (i=0; i<nprocs; i++) {
            MPI_Status wkl_status;
	    if (recv_size[i]) {
	        MPI_Recv(MPI_BOTTOM, 1, recv_types[j], i, myrank+i+100*iter,
		          fd->comm, &wkl_status);
	        j++;
	    }
        }
    }

    for (i=0; i<nprocs_recv; i++) MPI_Type_free(recv_types+i);
    ADIOI_Free(recv_types);
    
    if (fd->atomicity) {
        /* bug fix from Wei-keng Liao and Kenin Coloma */
        statuses = (MPI_Status *) ADIOI_Malloc((nprocs_send+1) * \
                                         sizeof(MPI_Status)); 
         /* +1 to avoid a 0-size malloc */
    }
    else {
        statuses = (MPI_Status *) ADIOI_Malloc((nprocs_send+nprocs_recv+1) * \
                                     sizeof(MPI_Status)); 
        /* +1 to avoid a 0-size malloc */
    }

#ifdef NEEDS_MPI_TEST
    i = 0;
    if (fd->atomicity) {
        /* bug fix from Wei-keng Liao and Kenin Coloma */
        while (!i) MPI_Testall(nprocs_send, send_req, &i, statuses);
    }
    else {
        while (!i) MPI_Testall(nprocs_send+nprocs_recv, requests, &i, statuses);
    }
#else
    if (fd->atomicity)
        /* bug fix from Wei-keng Liao and Kenin Coloma */
        MPI_Waitall(nprocs_send, send_req, statuses);
    else
        MPI_Waitall(nprocs_send+nprocs_recv, requests, statuses);
#endif

    ADIOI_Free(statuses);
    ADIOI_Free(requests);
    if (!buftype_is_contig && nprocs_send) {
	for (i=0; i < nprocs; i++) 
	    if (send_size[i]) ADIOI_Free(send_buf[i]);
	ADIOI_Free(send_buf);
    }
}


#define ADIOI_BUF_INCR \
{ \
    while (buf_incr) { \
        size_in_buf = ADIOI_MIN(buf_incr, flat_buf_sz); \
        user_buf_idx += size_in_buf; \
        flat_buf_sz -= size_in_buf; \
        if (!flat_buf_sz) { \
            if (flat_buf_idx < (flat_buf->count - 1)) flat_buf_idx++; \
            else { \
                flat_buf_idx = 0; \
                n_buftypes++; \
            } \
            user_buf_idx = flat_buf->indices[flat_buf_idx] + \
                              n_buftypes*buftype_extent; \
            flat_buf_sz = flat_buf->blocklens[flat_buf_idx]; \
        } \
        buf_incr -= size_in_buf; \
    } \
}


#define ADIOI_BUF_COPY \
{ \
    while (size) { \
        size_in_buf = ADIOI_MIN(size, flat_buf_sz); \
        memcpy(&(send_buf[p][send_buf_idx[p]]), \
               ((char *) buf) + user_buf_idx, size_in_buf); \
        send_buf_idx[p] += size_in_buf; \
        user_buf_idx += size_in_buf; \
        flat_buf_sz -= size_in_buf; \
        if (!flat_buf_sz) { \
            if (flat_buf_idx < (flat_buf->count - 1)) flat_buf_idx++; \
            else { \
                flat_buf_idx = 0; \
                n_buftypes++; \
            } \
            user_buf_idx = flat_buf->indices[flat_buf_idx] + \
                              n_buftypes*buftype_extent; \
            flat_buf_sz = flat_buf->blocklens[flat_buf_idx]; \
        } \
        size -= size_in_buf; \
        buf_incr -= size_in_buf; \
    } \
    ADIOI_BUF_INCR \
}



static void ADIOI_Fill_send_buffer(ADIO_File fd, void *buf, ADIOI_Flatlist_node
                           *flat_buf, char **send_buf, ADIO_Offset 
                           *offset_list, int *len_list, int *send_size, 
                           MPI_Request *requests, int *sent_to_proc, 
                           int nprocs, int myrank, 
                           int contig_access_count, 
                           ADIO_Offset min_st_offset, ADIO_Offset fd_size,
                           ADIO_Offset *fd_start, ADIO_Offset *fd_end, 
                           int *send_buf_idx, int *curr_to_proc, 
                           int *done_to_proc, int iter,
                           MPI_Aint buftype_extent)
{
/* this function is only called if buftype is not contig */

    int i, p, flat_buf_idx, size;
    int flat_buf_sz, buf_incr, size_in_buf, jj, n_buftypes;
    ADIO_Offset off, len, rem_len, user_buf_idx;

/*  curr_to_proc[p] = amount of data sent to proc. p that has already
    been accounted for so far
    done_to_proc[p] = amount of data already sent to proc. p in 
    previous iterations
    user_buf_idx = current location in user buffer 
    send_buf_idx[p] = current location in send_buf of proc. p  */

    for (i=0; i < nprocs; i++) {
	send_buf_idx[i] = curr_to_proc[i] = 0;
	done_to_proc[i] = sent_to_proc[i];
    }
    jj = 0;

    user_buf_idx = flat_buf->indices[0];
    flat_buf_idx = 0;
    n_buftypes = 0;
    flat_buf_sz = flat_buf->blocklens[0];

    /* flat_buf_idx = current index into flattened buftype
       flat_buf_sz = size of current contiguous component in 
	                 flattened buf */

    for (i=0; i<contig_access_count; i++) { 
	off     = offset_list[i];
	rem_len = (ADIO_Offset) len_list[i];

	/*this request may span the file domains of more than one process*/
	while (rem_len != 0) {
	    len = rem_len;
	    /* NOTE: len value is modified by ADIOI_Calc_aggregator() to be no
	     * longer than the single region that processor "p" is responsible
	     * for.
	     */
	    p = ADIOI_Calc_aggregator(fd,
				      off,
				      min_st_offset,
				      &len,
				      fd_size,
				      fd_start,
				      fd_end);

	    if (send_buf_idx[p] < send_size[p]) {
		if (curr_to_proc[p]+len > done_to_proc[p]) {
		    if (done_to_proc[p] > curr_to_proc[p]) {
			size = (int)ADIOI_MIN(curr_to_proc[p] + len - 
                                done_to_proc[p], send_size[p]-send_buf_idx[p]);
			buf_incr = done_to_proc[p] - curr_to_proc[p];
			ADIOI_BUF_INCR
		        buf_incr = (int)(curr_to_proc[p] + len - done_to_proc[p]);
			curr_to_proc[p] = done_to_proc[p] + size;
		        ADIOI_BUF_COPY
		    }
		    else {
			size = (int)ADIOI_MIN(len,send_size[p]-send_buf_idx[p]);
			buf_incr = (int)len;
			curr_to_proc[p] += size;
			ADIOI_BUF_COPY
		    }
		    if (send_buf_idx[p] == send_size[p]) {
			MPI_Isend(send_buf[p], send_size[p], MPI_BYTE, p, 
				myrank+p+100*iter, fd->comm, requests+jj);
			jj++;
		    }
		}
		else {
		    curr_to_proc[p] += (int)len;
		    buf_incr = (int)len;
		    ADIOI_BUF_INCR
		}
	    }
	    else {
		buf_incr = (int)len;
		ADIOI_BUF_INCR
            }
	    off     += len;
	    rem_len -= len;
	}
    }
    for (i=0; i < nprocs; i++) 
	if (send_size[i]) sent_to_proc[i] = curr_to_proc[i];
}



static void ADIOI_Heap_merge(ADIOI_Access *others_req, int *count, 
		      ADIO_Offset *srt_off, int *srt_len, int *start_pos,
		      int nprocs, int nprocs_recv, int total_elements)
{
    typedef struct {
	ADIO_Offset *off_list;
	int *len_list;
	int nelem;
    } heap_struct;

    heap_struct *a, tmp;
    int i, j, heapsize, l, r, k, smallest;

    a = (heap_struct *) ADIOI_Malloc((nprocs_recv+1)*sizeof(heap_struct));

    j = 0;
    for (i=0; i<nprocs; i++)
	if (count[i]) {
	    a[j].off_list = &(others_req[i].offsets[start_pos[i]]);
	    a[j].len_list = &(others_req[i].lens[start_pos[i]]);
	    a[j].nelem = count[i];
	    j++;
	}

    /* build a heap out of the first element from each list, with
       the smallest element of the heap at the root */

    heapsize = nprocs_recv;
    for (i=heapsize/2 - 1; i>=0; i--) {
	/* Heapify(a, i, heapsize); Algorithm from Cormen et al. pg. 143
           modified for a heap with smallest element at root. I have 
           removed the recursion so that there are no function calls.
           Function calls are too expensive. */
	k = i;
	for(;;) {
	    l = 2*(k+1) - 1;
	    r = 2*(k+1);

	    if ((l < heapsize) && 
		(*(a[l].off_list) < *(a[k].off_list)))
		smallest = l;
	    else smallest = k;

	    if ((r < heapsize) && 
		(*(a[r].off_list) < *(a[smallest].off_list)))
		smallest = r;

	    if (smallest != k) {
		tmp.off_list = a[k].off_list;
		tmp.len_list = a[k].len_list;
		tmp.nelem = a[k].nelem;

		a[k].off_list = a[smallest].off_list;
		a[k].len_list = a[smallest].len_list;
		a[k].nelem = a[smallest].nelem;
		
		a[smallest].off_list = tmp.off_list;
		a[smallest].len_list = tmp.len_list;
		a[smallest].nelem = tmp.nelem;
	    
		k = smallest;
	    }
	    else break;
	}
    }

    for (i=0; i<total_elements; i++) {
        /* extract smallest element from heap, i.e. the root */
	srt_off[i] = *(a[0].off_list);
	srt_len[i] = *(a[0].len_list);
	(a[0].nelem)--;

	if (!a[0].nelem) {
	    a[0].off_list = a[heapsize-1].off_list;
	    a[0].len_list = a[heapsize-1].len_list;
	    a[0].nelem = a[heapsize-1].nelem;
	    heapsize--;
	}
	else {
	    (a[0].off_list)++;
	    (a[0].len_list)++;
	}

	/* Heapify(a, 0, heapsize); */
	k = 0;
	for (;;) {
	    l = 2*(k+1) - 1;
	    r = 2*(k+1);

	    if ((l < heapsize) && 
		(*(a[l].off_list) < *(a[k].off_list)))
		smallest = l;
	    else smallest = k;

	    if ((r < heapsize) && 
		(*(a[r].off_list) < *(a[smallest].off_list)))
		smallest = r;

	    if (smallest != k) {
		tmp.off_list = a[k].off_list;
		tmp.len_list = a[k].len_list;
		tmp.nelem = a[k].nelem;

		a[k].off_list = a[smallest].off_list;
		a[k].len_list = a[smallest].len_list;
		a[k].nelem = a[smallest].nelem;
		
		a[smallest].off_list = tmp.off_list;
		a[smallest].len_list = tmp.len_list;
		a[smallest].nelem = tmp.nelem;
	    
		k = smallest;
	    }
	    else break;
	}
    }
    ADIOI_Free(a);
}
