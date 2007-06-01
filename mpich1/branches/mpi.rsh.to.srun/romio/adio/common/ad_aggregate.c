/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997-2001 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"
#include "adio_extern.h"
#ifdef PROFILE
#include "mpe.h"
#endif

#undef AGG_DEBUG

/* This file contains four functions:
 *
 * ADIOI_Calc_aggregator()
 * ADIOI_Calc_file_domains()
 * ADIOI_Calc_my_req()
 * ADIOI_Calc_others_req()
 *
 * The last three of these were originally in ad_read_coll.c, but they are
 * also shared with ad_write_coll.c.  I felt that they were better kept with
 * the rest of the shared aggregation code.  
 */

/* Discussion of values available from above:
 *
 * ADIO_Offset st_offsets[0..nprocs-1]
 * ADIO_Offset end_offsets[0..nprocs-1]
 *    These contain a list of start and end offsets for each process in 
 *    the communicator.  For example, an access at loc 10, size 10 would
 *    have a start offset of 10 and end offset of 19.
 * int nprocs
 *    number of processors in the collective I/O communicator
 * ADIO_Offset min_st_offset
 * ADIO_Offset fd_start[0..nprocs_for_coll-1]
 *    starting location of "file domain"; region that a given process will
 *    perform aggregation for (i.e. actually do I/O)
 * ADIO_Offset fd_end[0..nprocs_for_coll-1]
 *    start + size - 1 roughly, but it can be less, or 0, in the case of 
 *    uneven distributions
 */

/* ADIOI_Calc_aggregator()
 *
 * The intention here is to implement a function which provides basically 
 * the same functionality as in Rajeev's original version of 
 * ADIOI_Calc_my_req().  He used a ceiling division approach to assign the 
 * file domains, and we use the same approach here when calculating the
 * location of an offset/len in a specific file domain.  Further we assume
 * this same distribution when calculating the rank_index, which is later
 *  used to map to a specific process rank in charge of the file domain.
 *
 * A better (i.e. more general) approach would be to use the list of file
 * domains only.  This would be slower in the case where the
 * original ceiling division was used, but it would allow for arbitrary
 * distributions of regions to aggregators.  We'd need to know the 
 * nprocs_for_coll in that case though, which we don't have now.
 *
 * Note a significant difference between this function and Rajeev's old code:
 * this code doesn't necessarily return a rank in the range
 * 0..nprocs_for_coll; instead you get something in 0..nprocs.  This is a
 * result of the rank mapping; any set of ranks in the communicator could be
 * used now.
 *
 * Returns an integer representing a rank in the collective I/O communicator.
 *
 * The "len" parameter is also modified to indicate the amount of data
 * actually available in this file domain.
 */
int ADIOI_Calc_aggregator(ADIO_File fd,
			 ADIO_Offset off, 
			 ADIO_Offset min_off, 
			 ADIO_Offset *len, 
			 ADIO_Offset fd_size,
			 ADIO_Offset *fd_start,
			 ADIO_Offset *fd_end)
{
    int rank_index, rank;
    ADIO_Offset avail_bytes;

    ADIOI_UNREFERENCED_ARG(fd_start);

#ifdef AGG_DEBUG
#if 0
    FPRINTF(stdout, "off = %lld, min_off = %lld, len = %lld, fd_size = %lld\n",
	    off, min_off, *len, fd_size);
#endif
#endif
    
    /* get an index into our array of aggregators */
    rank_index = (int) ((off - min_off + fd_size)/ fd_size - 1);

    /* we index into fd_end with rank_index, and fd_end was allocated to be no
     * bigger than fd->hins->cb_nodes.   If we ever violate that, we're
     * overrunning arrays.  Obviously, we should never ever hit this abort */
    if (rank_index >= fd->hints->cb_nodes)
	    MPI_Abort(MPI_COMM_WORLD, 1);

    /* remember here that even in Rajeev's original code it was the case that
     * different aggregators could end up with different amounts of data to
     * aggregate.  here we use fd_end[] to make sure that we know how much
     * data this aggregator is working with.  
     *
     * the +1 is to take into account the end vs. length issue.
     */
    avail_bytes = fd_end[rank_index] + 1 - off;
    if (avail_bytes < *len) {
	/* this file domain only has part of the requested contig. region */
	*len = avail_bytes;
    }

    /* map our index to a rank */
    /* NOTE: FOR NOW WE DON'T HAVE A MAPPING...JUST DO 0..NPROCS_FOR_COLL */
    rank = fd->hints->ranklist[rank_index];

    return rank;
}

void ADIOI_Calc_file_domains(ADIO_Offset *st_offsets, ADIO_Offset
			     *end_offsets, int nprocs, int nprocs_for_coll,
			     ADIO_Offset *min_st_offset_ptr,
			     ADIO_Offset **fd_start_ptr, ADIO_Offset 
			     **fd_end_ptr, ADIO_Offset *fd_size_ptr)
{
/* Divide the I/O workload among "nprocs_for_coll" processes. This is
   done by (logically) dividing the file into file domains (FDs); each
   process may directly access only its own file domain. */

    ADIO_Offset min_st_offset, max_end_offset, *fd_start, *fd_end, fd_size;
    int i;

#ifdef AGG_DEBUG
    FPRINTF(stderr, "ADIOI_Calc_file_domains: %d aggregator(s)\n", 
	    nprocs_for_coll);
#endif

/* find min of start offsets and max of end offsets of all processes */

    min_st_offset = st_offsets[0];
    max_end_offset = end_offsets[0];

    for (i=1; i<nprocs; i++) {
	min_st_offset = ADIOI_MIN(min_st_offset, st_offsets[i]);
	max_end_offset = ADIOI_MAX(max_end_offset, end_offsets[i]);
    }

/* determine the "file domain (FD)" of each process, i.e., the portion of
   the file that will be "owned" by each process */

/* partition the total file access range equally among nprocs_for_coll
   processes */ 
    fd_size = ((max_end_offset - min_st_offset + 1) + nprocs_for_coll -
	       1)/nprocs_for_coll; 
    /* ceiling division as in HPF block distribution */

    *fd_start_ptr = (ADIO_Offset *)
	ADIOI_Malloc(nprocs_for_coll*sizeof(ADIO_Offset)); 
    *fd_end_ptr = (ADIO_Offset *)
	ADIOI_Malloc(nprocs_for_coll*sizeof(ADIO_Offset)); 

    fd_start = *fd_start_ptr;
    fd_end = *fd_end_ptr;

    fd_start[0] = min_st_offset;
    fd_end[0] = min_st_offset + fd_size - 1;

    for (i=1; i<nprocs_for_coll; i++) {
	fd_start[i] = fd_end[i-1] + 1;
	fd_end[i] = fd_start[i] + fd_size - 1;
    }

/* take care of cases in which the total file access range is not
   divisible by the number of processes. In such cases, the last
   process, or the last few processes, may have unequal load (even 0).
   For example, a range of 97 divided among 16 processes.
   Note that the division is ceiling division. */

    for (i=0; i<nprocs_for_coll; i++) {
	if (fd_start[i] > max_end_offset)
	    fd_start[i] = fd_end[i] = -1;
	if (fd_end[i] > max_end_offset)
	    fd_end[i] = max_end_offset;
    }

    *fd_size_ptr = fd_size;
    *min_st_offset_ptr = min_st_offset;
}


/* ADIOI_Calc_my_req() - calculate what portions of the access requests
 * of this process are located in the file domains of various processes
 * (including this one)
 */
void ADIOI_Calc_my_req(ADIO_File fd, ADIO_Offset *offset_list, int *len_list, 
		       int contig_access_count, ADIO_Offset 
		       min_st_offset, ADIO_Offset *fd_start,
		       ADIO_Offset *fd_end, ADIO_Offset fd_size,
                       int nprocs,
                       int *count_my_req_procs_ptr,
		       int **count_my_req_per_proc_ptr,
		       ADIOI_Access **my_req_ptr,
		       int **buf_idx_ptr)
{
    int *count_my_req_per_proc, count_my_req_procs, *buf_idx;
    int i, l, proc;
    ADIO_Offset fd_len, rem_len, curr_idx, off;
    ADIOI_Access *my_req;

    *count_my_req_per_proc_ptr = (int *) ADIOI_Calloc(nprocs,sizeof(int)); 
    count_my_req_per_proc = *count_my_req_per_proc_ptr;
/* count_my_req_per_proc[i] gives the no. of contig. requests of this
   process in process i's file domain. calloc initializes to zero.
   I'm allocating memory of size nprocs, so that I can do an 
   MPI_Alltoall later on.*/

    buf_idx = (int *) ADIOI_Malloc(nprocs*sizeof(int));
/* buf_idx is relevant only if buftype_is_contig.
   buf_idx[i] gives the index into user_buf where data received
   from proc. i should be placed. This allows receives to be done
   without extra buffer. This can't be done if buftype is not contig. */
   
    /* initialize buf_idx to -1 */
    for (i=0; i < nprocs; i++) buf_idx[i] = -1;

    /* one pass just to calculate how much space to allocate for my_req;
     * contig_access_count was calculated way back in ADIOI_Calc_my_off_len()
     */
    for (i=0; i < contig_access_count; i++) {
	/* short circuit offset/len processing if len == 0 
	 * 	(zero-byte  read/write */
	if (len_list[i] == 0) 
		continue;
	off = offset_list[i];
	fd_len = len_list[i];
	/* note: we set fd_len to be the total size of the access.  then
	 * ADIOI_Calc_aggregator() will modify the value to return the 
	 * amount that was available from the file domain that holds the
	 * first part of the access.
	 */
	proc = ADIOI_Calc_aggregator(fd, off, min_st_offset, &fd_len, fd_size, 
				     fd_start, fd_end);
	count_my_req_per_proc[proc]++;

	/* figure out how much data is remaining in the access (i.e. wasn't 
	 * part of the file domain that had the starting byte); we'll take 
	 * care of this data (if there is any) in the while loop below.
	 */
	rem_len = len_list[i] - fd_len;

	while (rem_len != 0) {
	    off += fd_len; /* point to first remaining byte */
	    fd_len = rem_len; /* save remaining size, pass to calc */
	    proc = ADIOI_Calc_aggregator(fd, off, min_st_offset, &fd_len, 
					 fd_size, fd_start, fd_end);

	    count_my_req_per_proc[proc]++;
	    rem_len -= fd_len; /* reduce remaining length by amount from fd */
	}
    }

/* now allocate space for my_req, offset, and len */

    *my_req_ptr = (ADIOI_Access *)
	ADIOI_Malloc(nprocs*sizeof(ADIOI_Access)); 
    my_req = *my_req_ptr;

    count_my_req_procs = 0;
    for (i=0; i < nprocs; i++) {
	if (count_my_req_per_proc[i]) {
	    my_req[i].offsets = (ADIO_Offset *)
		ADIOI_Malloc(count_my_req_per_proc[i] * sizeof(ADIO_Offset));
	    my_req[i].lens = (int *)
		ADIOI_Malloc(count_my_req_per_proc[i] * sizeof(int));
	    count_my_req_procs++;
	}	    
	my_req[i].count = 0;  /* will be incremented where needed
				      later */
    }

/* now fill in my_req */
    curr_idx = 0;
    for (i=0; i<contig_access_count; i++) { 
	/* short circuit offset/len processing if len == 0 
	 * 	(zero-byte  read/write */
	if (len_list[i] == 0)
		continue;
	off = offset_list[i];
	fd_len = len_list[i];
	proc = ADIOI_Calc_aggregator(fd, off, min_st_offset, &fd_len, fd_size, 
				     fd_start, fd_end);

	/* for each separate contiguous access from this process */
	if (buf_idx[proc] == -1) buf_idx[proc] = (int) curr_idx;

	l = my_req[proc].count;
	curr_idx += (int) fd_len; /* NOTE: Why is curr_idx an int?  Fix? */

	rem_len = len_list[i] - fd_len;

	/* store the proc, offset, and len information in an array
         * of structures, my_req. Each structure contains the 
         * offsets and lengths located in that process's FD, 
	 * and the associated count. 
	 */
	my_req[proc].offsets[l] = off;
	my_req[proc].lens[l] = (int) fd_len;
	my_req[proc].count++;

	while (rem_len != 0) {
	    off += fd_len;
	    fd_len = rem_len;
	    proc = ADIOI_Calc_aggregator(fd, off, min_st_offset, &fd_len, 
					 fd_size, fd_start, fd_end);

	    if (buf_idx[proc] == -1) buf_idx[proc] = (int) curr_idx;

	    l = my_req[proc].count;
	    curr_idx += fd_len;
	    rem_len -= fd_len;

	    my_req[proc].offsets[l] = off;
	    my_req[proc].lens[l] = (int) fd_len;
	    my_req[proc].count++;
	}
    }

#ifdef AGG_DEBUG
    for (i=0; i<nprocs; i++) {
	if (count_my_req_per_proc[i] > 0) {
	    FPRINTF(stdout, "data needed from %d (count = %d):\n", i, 
		    my_req[i].count);
	    for (l=0; l < my_req[i].count; l++) {
		FPRINTF(stdout, "   off[%d] = %lld, len[%d] = %d\n", l,
			my_req[i].offsets[l], l, my_req[i].lens[l]);
	    }
	}
    }
#if 0
    for (i=0; i<nprocs; i++) {
	FPRINTF(stdout, "buf_idx[%d] = 0x%x\n", i, buf_idx[i]);
    }
#endif
#endif

    *count_my_req_procs_ptr = count_my_req_procs;
    *buf_idx_ptr = buf_idx;
}



void ADIOI_Calc_others_req(ADIO_File fd, int count_my_req_procs, 
				int *count_my_req_per_proc,
				ADIOI_Access *my_req, 
				int nprocs, int myrank,
				int *count_others_req_procs_ptr,
				ADIOI_Access **others_req_ptr)  
{
/* determine what requests of other processes lie in this process's
   file domain */

/* count_others_req_procs = number of processes whose requests lie in
   this process's file domain (including this process itself) 
   count_others_req_per_proc[i] indicates how many separate contiguous
   requests of proc. i lie in this process's file domain. */

    int *count_others_req_per_proc, count_others_req_procs;
    int i, j;
    MPI_Request *send_requests, *recv_requests;
    MPI_Status *statuses;
    ADIOI_Access *others_req;

/* first find out how much to send/recv and from/to whom */

    count_others_req_per_proc = (int *) ADIOI_Malloc(nprocs*sizeof(int));

    MPI_Alltoall(count_my_req_per_proc, 1, MPI_INT,
		 count_others_req_per_proc, 1, MPI_INT, fd->comm);

    *others_req_ptr = (ADIOI_Access *)
	ADIOI_Malloc(nprocs*sizeof(ADIOI_Access)); 
    others_req = *others_req_ptr;

    count_others_req_procs = 0;
    for (i=0; i<nprocs; i++) {
	if (count_others_req_per_proc[i]) {
	    others_req[i].count = count_others_req_per_proc[i];
	    others_req[i].offsets = (ADIO_Offset *)
		ADIOI_Malloc(count_others_req_per_proc[i]*sizeof(ADIO_Offset));
	    others_req[i].lens = (int *)
		ADIOI_Malloc(count_others_req_per_proc[i]*sizeof(int)); 
	    others_req[i].mem_ptrs = (MPI_Aint *)
		ADIOI_Malloc(count_others_req_per_proc[i]*sizeof(MPI_Aint)); 
	    count_others_req_procs++;
	}
	else others_req[i].count = 0;
    }
    
/* now send the calculated offsets and lengths to respective processes */

    send_requests = (MPI_Request *)
	ADIOI_Malloc(2*(count_my_req_procs+1)*sizeof(MPI_Request)); 
    recv_requests = (MPI_Request *)
	ADIOI_Malloc(2*(count_others_req_procs+1)*sizeof(MPI_Request)); 
/* +1 to avoid a 0-size malloc */

    j = 0;
    for (i=0; i<nprocs; i++) {
	if (others_req[i].count) {
	    MPI_Irecv(others_req[i].offsets, others_req[i].count, 
                      ADIO_OFFSET, i, i+myrank, fd->comm, &recv_requests[j]);
	    j++;
	    MPI_Irecv(others_req[i].lens, others_req[i].count, 
                      MPI_INT, i, i+myrank+1, fd->comm, &recv_requests[j]);
	    j++;
	}
    }

    j = 0;
    for (i=0; i < nprocs; i++) {
	if (my_req[i].count) {
	    MPI_Isend(my_req[i].offsets, my_req[i].count, 
                      ADIO_OFFSET, i, i+myrank, fd->comm, &send_requests[j]);
	    j++;
	    MPI_Isend(my_req[i].lens, my_req[i].count, 
                      MPI_INT, i, i+myrank+1, fd->comm, &send_requests[j]);
	    j++;
	}
    }

    statuses = (MPI_Status *) ADIOI_Malloc((1 + 2* \
                   ADIOI_MAX(count_my_req_procs,count_others_req_procs)) * \
                       sizeof(MPI_Status));
/* +1 to avoid a 0-size malloc */

    MPI_Waitall(2*count_my_req_procs, send_requests, statuses);
    MPI_Waitall(2*count_others_req_procs, recv_requests, statuses);

    ADIOI_Free(send_requests);
    ADIOI_Free(recv_requests);	    
    ADIOI_Free(statuses);
    ADIOI_Free(count_others_req_per_proc);

    *count_others_req_procs_ptr = count_others_req_procs;
}
