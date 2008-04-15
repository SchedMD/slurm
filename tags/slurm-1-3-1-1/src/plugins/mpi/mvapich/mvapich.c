/*****************************************************************************\
 *  mvapich.c - srun support for MPICH-IB (MVAPICH 0.9.4 and 0.9.5,7,8)
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).  
 *
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/poll.h>
#include <sys/time.h>

#include "src/common/slurm_xlator.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/net.h"
#include "src/common/fd.h"

/* NOTE: MVAPICH has changed protocols without changing version numbers.
 * This makes support of MVAPICH very difficult. 
 * Support for the following versions have been validated:
 *
 * For MVAPICH-GEN2-1.0-103,    set MVAPICH_VERSION_REQUIRES_PIDS to 2
 * For MVAPICH 0.9.4 and 0.9.5, set MVAPICH_VERSION_REQUIRES_PIDS to 3
 *
 * See functions mvapich_requires_pids() below for other mvapich versions.
 */
#define MVAPICH_VERSION_REQUIRES_PIDS 3

#include "src/plugins/mpi/mvapich/mvapich.h"

/* NOTE: AIX lacks timersub */
/* Why are we using mvapich on AIX? */
#ifndef timersub
#  define timersub(a, b, result)					\
	do {								\
		(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
		(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
		if ((result)->tv_usec < 0) {				\
			--(result)->tv_sec;				\
			(result)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif

/*
 *  Information read from each MVAPICH process
 */
struct mvapich_info
{
	int do_poll;          
	int fd;             /* fd for socket connection to MPI task  */
	int rank;           /* This process' MPI rank                */
	int pidlen;         /* length of pid buffer                  */
	char *pid;          /* This rank's local pid (V3 only)       */
	int hostidlen;      /* Host id length                        */
	int hostid;         /* Separate hostid (for protocol v5)     */
	int addrlen;        /* Length of addr array in bytes         */

	int *addr;          /* This process' address array, which for
	                     *  process rank N in an M process job 
	                     *  looks like:
	                     *
	                     *   qp0,qp1,..,lid,qpN+1,..,qpM-1, hostid
	                     *
	                     *  Where position N is this rank's lid,
	                     *  and the hostid is tacked onto the end
	                     *  of the array (for protocol version 3)
	                     */
};

/*  Globals for the mvapich thread.
 */
int mvapich_verbose = 0;
static time_t first_abort_time = 0;

/*  Per-job step state information.  The MPI plugin may be called
 *  multiple times from the SLURM API's slurm_step_launch() in the
 *  same process.
 */
struct mvapich_state {
	pthread_t tid;
	struct mvapich_info **mvarray;
	int fd;
	int nprocs;
	int protocol_version;
	int protocol_phase;
	int connect_once;
	int do_timing;

	int timeout;         /* Initialization timeout in seconds  */
	int start_time;      /* Time from which to measure timeout */

	mpi_plugin_client_info_t job[1];
};

#define mvapich_debug(args...) \
	do { \
		if (mvapich_verbose) \
			info ("mvapich: " args); \
	} while (0);

#define mvapich_debug2(args...) \
	do { \
		if (mvapich_verbose > 1) \
			info ("mvapich: " args); \
	} while (0);

static struct mvapich_info * mvapich_info_create (void)
{
	struct mvapich_info *mvi = xmalloc (sizeof (*mvi));
	memset (mvi, 0, sizeof (*mvi));
	mvi->fd = -1;
	mvi->rank = -1;
	return (mvi);
}

static void mvapich_info_destroy (struct mvapich_info *mvi)
{
	xfree (mvi->addr);
	xfree (mvi->pid);
	xfree (mvi);
	return;
}

static int mvapich_requires_pids (mvapich_state_t *st)
{
	if ( st->protocol_version == MVAPICH_VERSION_REQUIRES_PIDS 
	  || st->protocol_version == 5
	  || st->protocol_version == 6 )
		return (1);
	return (0);
}

static int mvapich_terminate_job (mvapich_state_t *st)
{
	slurm_kill_job_step (st->job->jobid, st->job->stepid, SIGKILL);
	/* Give srun a chance to terminate job */
	sleep (5);
	/* exit forcefully */
	exit (1);
	/* NORETURN */
}

static void report_absent_tasks (mvapich_state_t *st)
{
	int i;
	char buf[16];
	hostlist_t tasks = hostlist_create (NULL);
	hostlist_t hosts = hostlist_create (NULL);
	slurm_step_layout_t *sl = st->job->step_layout;

	for (i = 0; i < st->nprocs; i++) {
		if (st->mvarray[i]->fd < 0) {
			const char *host = slurm_step_layout_host_name (sl, i);
			sprintf (buf, "%d", i);
			hostlist_push (tasks, buf);
			hostlist_push (hosts, host);
		}
	}

	if (hostlist_count (tasks)) {
		char r [4096];
		char h [4096];
		hostlist_uniq (hosts);
		int nranks = hostlist_count (tasks);
		int nhosts = hostlist_count (hosts);
		hostlist_ranged_string (tasks, 4096, r);
		hostlist_ranged_string (hosts, 4096, h);
		error ("mvapich: timeout: never heard from rank%s %s on host%s %s.\n", 
				nranks > 1 ? "s" : "", r, 
				nhosts > 1 ? "s" : "", h);
	}

	hostlist_destroy (hosts);
	hostlist_destroy (tasks);
}


static int startup_timeout (mvapich_state_t *st)
{
	time_t now;
	time_t remaining;

	if (st->timeout <= 0)
		return (-1);

	now = time (NULL);

	if (!st->start_time)
		return (-1);

	remaining = st->timeout - (now - st->start_time);

	if (remaining >= 0)
		return ((int) remaining * 1000);
	else
		return (0);
}

static int mvapich_poll (mvapich_state_t *st, struct mvapich_info *mvi, 
		                 int write) {
	int rc = 0;
	struct pollfd pfds[1];
	int timeout;

	pfds->fd = mvi->fd;
	pfds->events = write ? POLLOUT : POLLIN;

	timeout = startup_timeout (st);
	while (timeout && (rc = poll (pfds, 1, startup_timeout (st))) < 0) {
		if (errno != EINTR)
			return (-1);
	}

	/* 
	 *  If poll() timed out, forcibly kill job and exit instead of
	 *   waiting longer for remote IO, process exit, etc.
	 */
	if (rc == 0) {
		if (mvi->rank >= 0) {
			slurm_step_layout_t *sl = st->job->step_layout;
			const char *host = slurm_step_layout_host_name (sl, mvi->rank);
			error("Timeout waiting to read from MPI rank %d [on %s]. Exiting.", 
					mvi->rank, host);
		} 
		else {
			report_absent_tasks (st);
		}

		mvapich_terminate_job (st);
		/* NORETURN */
	}

	return (rc);
}

static int mvapich_write_n (mvapich_state_t *st, struct mvapich_info *mvi,
		                    void *buf, size_t len)
{
	int nleft = len;
	int n = 0;
	unsigned char * p = buf;

	while (nleft > 0) {
		/* Poll for write-activity */
		if (mvapich_poll (st, mvi, 1) < 0)
			return (-1);

		if ((n = write (mvi->fd, p, nleft)) < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return (-1);
		}

		nleft -= n;
		p += n;
	}

	return (len - nleft);
}

static int mvapich_read_n (mvapich_state_t *st,  struct mvapich_info *mvi,
						   void *buf, size_t len)
{
	int nleft = len;
	int n = 0;
	unsigned char * p = buf;

	while (nleft > 0) {
		/* Poll for write-activity */
		if (mvapich_poll (st, mvi, 0) < 0)
			return (-1);

		if ((n = read (mvi->fd, p, nleft)) < 0) { 
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return (-1);
		}

		if (n == 0) { /* unexpected EOF */
			error ("mvapich: rank %d: "
			       "Unexpected EOF (%dB left to read)", 
			       mvi->rank, nleft);
			return (-1);
		}

		nleft -= n;
		p += n;
	}

	return (len - nleft);
}


/*
 *  Return non-zero if protocol version has two phases.
 */
static int mvapich_dual_phase (mvapich_state_t *st)
{
	return (st->protocol_version == 5 || st->protocol_version == 6);
}

static int mvapich_abort_sends_rank (mvapich_state_t *st)
{
	if (st->protocol_version >= 3)
		return (1);
	return (0);
}

/*
 *  Create an mvapich_info object by reading information from
 *   file descriptor `fd'
 */
static int mvapich_get_task_info (mvapich_state_t *st,
				  struct mvapich_info *mvi)
{
	mvi->do_poll = 0;

	if (mvapich_read_n (st, mvi, &mvi->addrlen, sizeof (int)) <= 0)
		return error ("mvapich: Unable to read addrlen for rank %d: %m", 
				mvi->rank);

	mvi->addr = xmalloc (mvi->addrlen);

	if (mvapich_read_n (st, mvi, mvi->addr, mvi->addrlen) <= 0)
		return error ("mvapich: Unable to read addr info for rank %d: %m", 
				mvi->rank);

	if (!mvapich_requires_pids (st))
		return (0);

	if (mvapich_read_n (st, mvi, &mvi->pidlen, sizeof (int)) <= 0) {
		return error ("mvapich: Unable to read pidlen for rank %d: %m", 
				mvi->rank);
	}

	mvi->pid = xmalloc (mvi->pidlen);

	if (mvapich_read_n (st, mvi, mvi->pid, mvi->pidlen) <= 0) {
		return error ("mvapich: Unable to read pid for rank %d: %m", 
				mvi->rank);
	}

	return (0);
}

static int mvapich_get_hostid (mvapich_state_t *st, struct mvapich_info *mvi)
{
	if (mvapich_read_n (st, mvi, &mvi->hostidlen, sizeof (int)) < 0) {
		return error ("mvapich: Unable to read hostidlen for rank %d: %m",
				mvi->rank);
	}
	if (mvi->hostidlen != sizeof (int)) {
		return error ("mvapich: Unexpected size for hostidlen (%d)", 
				mvi->hostidlen);
	}
	if (mvapich_read_n (st, mvi, &mvi->hostid, sizeof (int)) < 0) {
		return error ("mvapich: unable to read hostid from rank %d", 
				mvi->rank);
	}

	return (0);
}

static int mvapich_get_task_header (mvapich_state_t *st,
				    int fd, int *version, int *rank)
{
	struct mvapich_info tmp[1] ;

	fd_set_nonblocking (fd);

	tmp->fd = fd;
	tmp->rank = -1;

	/*
	 *  dual phase only sends version on first pass
	 */
	if (!mvapich_dual_phase (st) || st->protocol_phase == 0) {
		if (mvapich_read_n (st, tmp, version, sizeof (int)) < 0) 
			return error ("mvapich: Unable to read version from task: %m");
	} 

	if (mvapich_read_n (st, tmp, rank, sizeof (int)) < 0) 
		return error ("mvapich: Unable to read task rank: %m");


	if (mvapich_dual_phase (st) && st->protocol_phase > 0)
		return (0);

	if (st->protocol_version == -1)
		st->protocol_version = *version;
	else if (st->protocol_version != *version) {
		return error ("mvapich: rank %d version %d != %d",
			      *rank, *version, st->protocol_version);
	}

	return (0);

}

static int mvapich_handle_task (mvapich_state_t *st,
				int fd, struct mvapich_info *mvi)
{
	mvi->fd = fd;

	switch (st->protocol_version) {
		case 1:
		case 2:
		case 3:
			return mvapich_get_task_info (st, mvi);
		case 5:
		case 6:
			if (st->protocol_phase == 0)
				return mvapich_get_hostid (st, mvi);
			else
				return mvapich_get_task_info (st, mvi);
		case 8:
			return (0);
		default:
			return (error ("mvapich: Unsupported protocol version %d", 
				       st->protocol_version));
	}

	return (0);
}
/*
 *  Broadcast addr information to all connected mvapich processes.
 *   The format of the information sent back to each process is:
 *
 *   for rank N in M process job:
 *   
 *    lid info :  lid0,lid1,...lidM-1
 *    qp info  :  qp0, qp1, ..., -1, qpN+1, ...,qpM-1
 *    hostids  :  hostid0,hostid1,...,hostidM-1
 *
 *   total of 3*nprocs ints.
 *
 */   
static void mvapich_bcast_addrs (mvapich_state_t *st)
{
	struct mvapich_info *m;
	int out_addrs_len = 3 * st->nprocs * sizeof (int);
	int *out_addrs = xmalloc (out_addrs_len);
	int i = 0;
	int j = 0;

	for (i = 0; i < st->nprocs; i++) {
		m = st->mvarray[i];
		/*
		 * lids are found in addrs[rank] for each process
		 */
		out_addrs[i] = m->addr[m->rank];

		/*
		 * hostids are the last entry in addrs
		 */
		out_addrs[2 * st->nprocs + i] =
			m->addr[(m->addrlen/sizeof (int)) - 1];
	}

	for (i = 0; i < st->nprocs; i++) {
		m = st->mvarray[i];

		/*
		 * qp array is tailored to each process.
		 */
		for (j = 0; j < st->nprocs; j++)  
			out_addrs[st->nprocs + j] = 
				(i == j) ? -1 : st->mvarray[j]->addr[i];

		mvapich_write_n (st, m, out_addrs, out_addrs_len);

		/*
		 * Protocol version 3 requires pid list to be sent next
		 */
		if (mvapich_requires_pids (st)) {
			for (j = 0; j < st->nprocs; j++)
				mvapich_write_n (st, m, 
						&st->mvarray[j]->pid, st->mvarray[j]->pidlen);
		}

	}

	xfree (out_addrs);
	return;
}

static void mvapich_bcast_hostids (mvapich_state_t *st)
{
	int *  hostids;
	int    i   = 0;
	size_t len = st->nprocs * sizeof (int);

	hostids = xmalloc (len);

	for (i = 0; i < st->nprocs; i++)
		hostids [i] = st->mvarray[i]->hostid;

	for (i = 0; i < st->nprocs; i++) {
		struct mvapich_info *mvi = st->mvarray[i];
		int co, rc;

		if (mvapich_write_n (st, mvi, hostids, len) < 0)
			error ("mvapich: write hostid rank %d: %m", mvi->rank);

		if ((rc = mvapich_read_n (st, mvi, &co, sizeof (int))) <= 0) {
			mvapich_debug2 ("reading connect once value rc=%d: %m\n", rc);
			close (mvi->fd);
			st->connect_once = 0;
		} else
			mvi->do_poll = 1;
	}

	xfree (hostids);
}

/* Write size bytes from buf into socket for rank */
static int mvapich_send (mvapich_state_t *st, void* buf, int size, int rank)
{
	struct mvapich_info *mvi = st->mvarray [rank];
	return (mvapich_write_n (st, mvi, buf, size));
}

/* Read size bytes from socket for rank into buf */
static int mvapich_recv (mvapich_state_t *st, void* buf, int size, int rank)
{
	struct mvapich_info *mvi = st->mvarray [rank];
	return (mvapich_read_n (st, mvi, buf, size)); 
}

/* Scatter data in buf to ranks using chunks of size bytes */
static int mvapich_scatterbcast (mvapich_state_t *st, void* buf, int size)
{
	int i, rc;
	int n = 0;

	for (i = 0; i < st->nprocs; i++) {
		if ((rc = mvapich_send (st, buf + i*size, size, i)) <= 0) 
			return (-1);
		n += rc;
	}
	return (n);
}

/* Broadcast buf to each rank, which is size bytes big */
static int mvapich_allgatherbcast (mvapich_state_t *st, void* buf, int size)
{
	int i, rc;
	int n = 0;

	for (i = 0; i < st->nprocs; i++) {
		if ((rc = mvapich_send (st, buf, size, i)) <= 0)
			return (-1);
		n += rc;
	}
	return (n);
}

/* Perform alltoall using data in buf with elements of size bytes */
static int mvapich_alltoallbcast (mvapich_state_t *st, void* buf, int size)
{
	int pbufsize = size * st->nprocs;
	void* pbuf = xmalloc(pbufsize);	
	int i, src, rc = 0;
	int n = 0;

	for (i = 0; i < st->nprocs; i++) {
		for (src = 0; src < st->nprocs; src++) {
			memcpy( pbuf + size*src,
				buf + size*(src*st->nprocs + i),
				size
				);
		}
		if ((rc = mvapich_send (st, pbuf, pbufsize, i)) <= 0)
			goto out;
		n += rc;
	}
	
    out:
	xfree(pbuf);
	return (rc < 0 ? rc : n);
}

static int recv_common_value (mvapich_state_t *st, int *valp, int rank)
{
	int val;
	if (mvapich_recv (st, &val, sizeof (int), rank) <= 0) {
		error ("mvapich: recv: rank %d: %m\n", rank);
		return (-1);
	}

	/*
	 *  If value is uninitialized, set it to current value,
	 *   otherwise ensure that current value matches previous
	 */
	if (*valp == -1)
		*valp = val;
	else if (val != *valp) {
		error ("mvapich: PMGR: unexpected value from rank %d: "
		       "expected %d, recvd %d", rank, *valp, val);
		return (-1);
	}
	return (0);
}

/* 
 * PMGR_BCAST (root, size of message, then message data (from root only))
 */
static int process_pmgr_bcast (mvapich_state_t *st, int *rootp, int *sizep, 
		void ** bufp, int rank)
{
	if (recv_common_value (st, rootp, rank) < 0)
		return (-1);
	if (recv_common_value (st, sizep, rank) < 0)
		return (-1);
	if (rank != *rootp)
		return (0);

	/* 
	 *  Recv data from root 
	 */
	*bufp = xmalloc (*sizep);
	if (mvapich_recv (st, *bufp, *sizep, rank) < 0) {
		error ("mvapich: PMGR_BCAST: Failed to recv from root: %m");
		return (-1);
	}
	return (0);
}

/*
 * PMGR_GATHER (root, size of message, then message data)
 */
static int process_pmgr_gather (mvapich_state_t *st, int *rootp, 
		int *sizep, void **bufp, int rank)
{
	if (recv_common_value (st, rootp, rank) < 0)
		return (-1);
	if (recv_common_value (st, sizep, rank) < 0)
		return (-1);
	if (*bufp == NULL)
		*bufp = xmalloc (*sizep * st->nprocs);
		
	if (mvapich_recv(st, (*bufp) + (*sizep)*rank, *sizep, rank) < 0) {
		error ("mvapich: PMGR_/GATHER: rank %d: recv: %m", rank);
		return (-1);
	}
	return (0);
}

/*
 * PMGR_SCATTER (root, size of message, then message data)
 */
static int process_pmgr_scatter (mvapich_state_t *st, int *rootp, 
		int *sizep, void **bufp, int rank)
{
	if (recv_common_value (st, rootp, rank) < 0)
		return (-1);
	if (recv_common_value (st, sizep, rank) < 0)
		return (-1);
	if (rank != *rootp)
		return (0);

	if (*bufp == NULL)
		*bufp = xmalloc (*sizep * st->nprocs);
		
	if (mvapich_recv(st, *bufp, (*sizep) * st->nprocs, rank) < 0) {
		error ("mvapich: PMGR_SCATTER: rank %d: recv: %m", rank);
		return (-1);
	}
	return (0);
}

/*
 * PMGR_ALLGATHER (size of message, then message data)
 */
static int process_pmgr_allgather (mvapich_state_t *st, int *sizep, 
		void **bufp, int rank)
{
	if (recv_common_value (st, sizep, rank) < 0)
		return (-1);
	if (*bufp == NULL)
		*bufp = xmalloc (*sizep * st->nprocs);
	if (mvapich_recv (st, (*bufp) + *sizep*rank, *sizep, rank) < 0) {
		error ("mvapich: PMGR_ALLGATHER: rank %d: %m", rank);
		return (-1);
	}
	return (0);
}

/*
 * PMGR_ALLTOALL (size of message, then message data)
 */
static int process_pmgr_alltoall (mvapich_state_t *st, int *sizep, 
		void **bufp, int rank)
{
	if (recv_common_value (st, sizep, rank) < 0)
		return (-1);

	if (*bufp == NULL)
		*bufp = xmalloc (*sizep * st->nprocs * st->nprocs);
	if (mvapich_recv ( st, 
	                   *bufp + (*sizep * st->nprocs)*rank,
	                   *sizep * st->nprocs, rank ) < 0) {
		error ("mvapich: PMGR_ALLTOALL: recv: rank %d: %m", rank);
		return (-1);
	}

	return (0);
}

/* 
 * This function carries out pmgr_collective operations to
 * bootstrap MPI.  These collective operations are modeled after
 * MPI collectives -- all tasks must call them in the same order
 * and with consistent parameters.
 *
 * Until a 'CLOSE' or 'ABORT' message is seen, we continuously loop
 * processing ops
 *   For each op, we read one packet from each rank (socket)
 *     A packet consists of an integer OP CODE, followed by variable
 *     length data depending on the operation
 *   After reading a packet from each rank, srun completes the
 *   operation by broadcasting data back to any destinations,
 *   depending on the operation being performed
 *
 * Note: Although there are op codes available for PMGR_OPEN and
 * PMGR_ABORT, neither is fully implemented and should not be used.
 */
static int mvapich_processops (mvapich_state_t *st)
{
	/* Until a 'CLOSE' or 'ABORT' message is seen, we continuously 
	 *  loop processing ops
	 */
	int exit = 0;
	while (!exit) {
	int opcode = -1;
	int root   = -1;
	int size   = -1;
	void* buf = NULL;

	mvapich_debug ("Processing PMGR opcodes");

	// for each process, read in one opcode and its associated data
	int i;
	for (i = 0; i < st->nprocs; i++) {
		struct mvapich_info *mvi = st->mvarray [i];

		// read in opcode
		if (recv_common_value (st, &opcode, i) < 0) {
			error ("mvapich: rank %d: Failed to read opcode: %m", 
				mvi->rank);
			return (-1);
		}

		// read in additional data depending on current opcode
		int rank, code;
		switch(opcode) {
		case 0: // PMGR_OPEN (followed by rank)
			if (mvapich_recv (st, &rank, sizeof (int), i) <= 0) {
				error ("mvapich: PMGR_OPEN: recv: %m");
				exit = 1;
			}
			break;
		case 1: // PMGR_CLOSE (no data, close the socket)
			close(mvi->fd);
			break;
		case 2: // PMGR_ABORT (followed by exit code)
			if (mvapich_recv (st, &code, sizeof (int), i) <= 0) {
				error ("mvapich: PMGR_ABORT: recv: %m");
			}
			error("mvapich abort with code %d from rank %d", 
				code, i);
			break;
		case 3: // PMGR_BARRIER (no data)
			break;
		case 4: // PMGR_BCAST
			if (process_pmgr_bcast (st, &root, &size, &buf, i) < 0)
				return (-1);
			break;
		case 5: // PMGR_GATHER 
			if (process_pmgr_gather (st, &root, &size, &buf, i) < 0)
				return (-1);
			break;
		case 6: // PMGR_SCATTER 
			if (process_pmgr_scatter (st, &root, 
			                          &size, &buf, i) < 0)
				return (-1);
			break;
		case 7: // PMGR_ALLGATHER 
			if (process_pmgr_allgather (st, &size, &buf, i) < 0)
				return (-1);
			break;
		case 8: // PMGR_ALLTOALL 
			if (process_pmgr_alltoall (st, &size, &buf, i) < 0)
				return (-1);
			break;
		default:
			error("Unrecognized PMGR opcode: %d", opcode);
			return (-1);
		}
	}

	// Complete any operations
	switch(opcode) {
		case 0: // PMGR_OPEN
			mvapich_debug ("Completed PMGR_OPEN");
			break;
		case 1: // PMGR_CLOSE
			mvapich_debug ("Completed PMGR_CLOSE");
			exit = 1;
			break;
		case 2: // PMGR_ABORT
			mvapich_debug ("Completed PMGR_ABORT");
			exit = 1;
			break;
		case 3: // PMGR_BARRIER (just echo the opcode back)
			mvapich_debug ("Completing PMGR_BARRIER");
			mvapich_allgatherbcast (st, &opcode, sizeof(opcode));
			mvapich_debug ("Completed PMGR_BARRIER");
			break;
		case 4: // PMGR_BCAST
			mvapich_debug ("Completing PMGR_BCAST");
			mvapich_allgatherbcast (st, buf, size);
			mvapich_debug ("Completed PMGR_BCAST");
			break;
		case 5: // PMGR_GATHER
			mvapich_debug ("Completing PMGR_GATHER");
			mvapich_send (st, buf, size * st->nprocs, root);
			mvapich_debug ("Completed PMGR_GATHER");
			break;
		case 6: // PMGR_SCATTER
			mvapich_debug ("Completing PMGR_SCATTER");
			mvapich_scatterbcast (st, buf, size);
			mvapich_debug ("Completed PMGR_SCATTER");
			break;
		case 7: // PMGR_ALLGATHER
			mvapich_debug ("Completing PMGR_ALLGATHER");
			mvapich_allgatherbcast (st, buf, size * st->nprocs);
			mvapich_debug ("Completed PMGR_ALLGATHER");
			break;
		case 8: // PMGR_ALLTOALL
			mvapich_debug ("Completing PMGR_ALLTOALL");
			mvapich_alltoallbcast (st, buf, size);
			mvapich_debug ("Completed PMGR_ALLTOALL");
			break;
		default:
			error("Unrecognized PMGR opcode: %d", opcode);
	}

	xfree(buf);
  } // while(!exit)
  mvapich_debug ("Completed processing PMGR opcodes");
  return (0);
}

static void mvapich_bcast (mvapich_state_t *st)
{
	if (!mvapich_dual_phase (st) || st->protocol_phase > 0)
		return mvapich_bcast_addrs (st);
	else
		return mvapich_bcast_hostids (st);
}

static void mvapich_barrier (mvapich_state_t *st)
{
	int i;
	struct mvapich_info *m;
	/*
	 *  Simple barrier to wait for qp's to come up. 
	 *   Once all processes have written their rank over the socket,
	 *   simply write their rank right back to them.
	 */

	debug ("mvapich: starting barrier");

	for (i = 0; i < st->nprocs; i++) {
		int j;
		m = st->mvarray[i];
		if (mvapich_read_n (st, m, &j, sizeof (j)) == -1)
			error("mvapich read on barrier");
	}

	debug ("mvapich: completed barrier for all tasks");

	for (i = 0; i < st->nprocs; i++) {
		m = st->mvarray[i];
		if (mvapich_write_n (st, m, &i, sizeof (i)) == -1)
			error("mvapich: write on barrier: %m");
		close (m->fd);
		m->fd = -1;
	}

	return;
}

static void 
mvapich_print_abort_message (mvapich_state_t *st, int rank,
			     int dest, char *msg, int msglen)
{
	slurm_step_layout_t *sl = st->job->step_layout;
	char *host;
	char *msgstr;

	if (!mvapich_abort_sends_rank (st)) {
		info ("mvapich: Received ABORT message from an MPI process.");
		return;
	}

	if (msg && (msglen > 0)) {
		/* 
		 *  Remove trailing newline if it exists (syslog will add newline)
		 */
		if (msg [msglen - 1] == '\n')
			msg [msglen - 1] = '\0';

		msgstr = msg;
	} 
	else {
		msgstr = "";
		msglen = 0;
	}

	host = slurm_step_layout_host_name (sl, rank);

	if (dest >= 0) {
		const char *dsthost = slurm_step_layout_host_name (sl, dest);

		info ("mvapich: %M: ABORT from MPI rank %d [on %s] dest rank %d [on %s]",
		      rank, host, dest, dsthost);

		/*
		 *  Log the abort event to syslog
		 *   so that system administrators know about possible HW events.
		 */
		openlog ("srun", 0, LOG_USER);
		syslog (LOG_WARNING, 
				"MVAPICH ABORT [jobid=%u.%u src=%d(%s) dst=%d(%s)]: %s",
				st->job->jobid, st->job->stepid, 
				rank, host, dest, dsthost, msgstr);
		closelog();
	} else {
		info ("mvapich: %M: ABORT from MPI rank %d [on %s]", 
		      rank, host);
		/*
		 *  Log the abort event to syslog
		 *   so that system administrators know about possible HW events.
		 */
		openlog ("srun", 0, LOG_USER);
		syslog (LOG_WARNING, 
				"MVAPICH ABORT [jobid=%u.%u src=%d(%s) dst=-1()]: %s",
				st->job->jobid, st->job->stepid, 
				rank, host, msgstr);
		closelog();

	}
	return;
}


static int mvapich_abort_timeout (void)
{
	int timeout;

	if (first_abort_time == 0)
		return (-1);

	timeout = 60 - (time (NULL) - first_abort_time);

	if (timeout < 0)
		return (0);

	return (timeout * 1000);
}

static int mvapich_abort_accept (mvapich_state_t *st)
{
	slurm_addr addr;
	int rc;
	struct pollfd pfds[1];

	pfds->fd = st->fd;
	pfds->events = POLLIN;

	while ((rc = poll (pfds, 1, mvapich_abort_timeout ())) < 0) {
		if (errno != EINTR)
			return (-1);
	}

	/* 
	 *  If poll() timed out, forcibly kill job and exit instead of
	 *   waiting longer for remote IO, process exit, etc.
	 */
	if (rc == 0) {
		error("Timeout waiting for all tasks after MVAPICH ABORT. Exiting.");
		mvapich_terminate_job (st);
		/* NORETURN */
	}

	return (slurm_accept_msg_conn (st->fd, &addr));
}


static void mvapich_wait_for_abort(mvapich_state_t *st)
{
	int src, dst;
	int ranks[2];
	int n;
	char msg [1024] = "";
	int msglen = 0;

	/*
	 *  Wait for abort notification from any process.
	 *  For mvapich 0.9.4, it appears that an MPI_Abort is registered
	 *   simply by connecting to this socket and immediately closing
	 *   the connection. In other versions, the process may write
	 *   its rank.
	 */
	while (1) {
		int newfd = mvapich_abort_accept (st);

		if (newfd == -1) {
			fatal("MPI master failed to accept (abort-wait)");
		}

		fd_set_blocking (newfd);

		ranks[1] = -1;
		if ((n = fd_read_n (newfd, &ranks, sizeof (ranks))) < 0) {
			error("mvapich: MPI recv (abort-wait) failed");
			close (newfd);
			continue;
		}

		/*
		 *  If we read both src/dest rank, then also try to 
		 *   read an error message. If this fails, msglen will
		 *   stay zero and no message will be printed.
		 */
		if (n == sizeof (ranks)) {
			dst = ranks[0];
			src = ranks[1];
			fd_read_n (newfd, &msglen, sizeof (int));
			if (msglen)
				fd_read_n (newfd, msg, msglen);
		} else {
			src = ranks[0];
			dst = -1;
		}

		close(newfd);

		mvapich_print_abort_message (st, src, dst, msg, msglen);
		slurm_signal_job_step (st->job->jobid, st->job->stepid, SIGKILL);
		if (!first_abort_time)
			first_abort_time = time (NULL);
	}

	return; /* but not reached */
}

static void mvapich_mvarray_create (mvapich_state_t *st)
{
	int i;
	st->mvarray = xmalloc (st->nprocs * sizeof (*(st->mvarray)));
	for (i = 0; i < st->nprocs; i++) {
		st->mvarray [i] = mvapich_info_create ();
		st->mvarray [i]->rank = i;
	}
}

static void mvapich_mvarray_destroy (mvapich_state_t *st)
{
	int i;
	for (i = 0; i < st->nprocs; i++)
		mvapich_info_destroy (st->mvarray[i]);
	xfree (st->mvarray);
}

static int mvapich_rank_from_fd (mvapich_state_t *st, int fd)
{
	int rank = 0;
	while (st->mvarray[rank]->fd != fd)
		rank++;
	return (rank);
}

static int mvapich_handle_connection (mvapich_state_t *st, int fd)
{
	int version, rank;

	if (st->protocol_phase == 0 || !st->connect_once) {
		if (mvapich_get_task_header (st, fd, &version, &rank) < 0)
			return (-1);

		if (rank > st->nprocs - 1) { 
			return (error ("mvapich: task reported invalid rank (%d)", 
					rank));

		st->mvarray[rank]->rank = rank;

		}
	} else {
		rank = mvapich_rank_from_fd (st, fd);
	}

	if (mvapich_handle_task (st, fd, st->mvarray[rank]) < 0) 
		return (-1);

	return (0);
}

static int poll_mvapich_fds (mvapich_state_t *st)
{
	int i = 0;
	int j = 0;
	int rc;
	int fd;
	int nfds = 0;
	struct pollfd *fds = xmalloc (st->nprocs * sizeof (struct pollfd));

	for (i = 0; i < st->nprocs; i++) {
		if (st->mvarray[i]->do_poll) {
			fds[j].fd = st->mvarray[i]->fd;
			fds[j].events = POLLIN;
			j++;
			nfds++;
		}
	}

	if ((rc = poll (fds, nfds, startup_timeout (st))) < 0) {
		error ("mvapich: poll: %m");
		xfree (fds);
		return SLURM_ERROR;
	}

	i = 0;
	while (fds[i].revents != POLLIN)
		i++;

	fd = fds[i].fd;
	xfree (fds);

	return (fd);
}
static int mvapich_get_next_connection (mvapich_state_t *st)
{
	slurm_addr addr;
	int fd;
	int rc;
	struct mvapich_info tmp[1];

	if (st->connect_once && st->protocol_phase > 0) {
		return (poll_mvapich_fds (st));
	} 
		
	tmp->fd = st->fd;
	tmp->rank = -1;
	if ((rc = mvapich_poll (st, tmp, 0)) == 0)
		report_absent_tasks (st);
	else if (rc < 0) {
		error ("mvapich: poll for accept: %m");
		return (-1);
	}
		

	if ((fd = slurm_accept_msg_conn (st->fd, &addr)) < 0) {
		error ("mvapich: accept: %m");
		return (-1);
	}
	mvapich_debug2 ("accept() = %d", fd);

	return (fd);
}

static void do_timings (mvapich_state_t *st)
{
	static int initialized = 0;
	static struct timeval initv = { 0, 0 };
	struct timeval tv;
	struct timeval result;

	if (!st->do_timing)
		return;

	if (!initialized) {
		if (gettimeofday (&initv, NULL) < 0)
			error ("mvapich: do_timings(): gettimeofday(): %m\n");
		initialized = 1;
		return;
	}

	if (gettimeofday (&tv, NULL) < 0) {
		error ("mvapich: do_timings(): gettimeofday(): %m\n");
		return;
	}

	timersub (&tv, &initv, &result);

	info ("mvapich: Intialization took %d.%03d seconds", result.tv_sec,
			result.tv_usec/1000);

	return;
}

static void *mvapich_thr(void *arg)
{
	mvapich_state_t *st = arg;
	int i = 0;
	int first = 1;

	debug ("mvapich-0.9.x/gen2: thread started: %ld", pthread_self ());

	mvapich_mvarray_create (st);

again:
	i = 0;
	while (i < st->nprocs) {
		int fd;
		
		mvapich_debug ("Waiting to accept remote connection %d of %d\n", 
				i, st->nprocs);

		if ((fd = mvapich_get_next_connection (st)) < 0) {
			error ("mvapich: accept: %m");
			goto fail;
		}

		if (first) {
			mvapich_debug ("first task checked in");
			do_timings (st);
			/*
			 *  Officially start timeout timer now.
			 */
			st->start_time = time(NULL);
			first = 0;
		}

		if (mvapich_handle_connection (st, fd) < 0) 
			goto fail;

		i++;
	}

	if (st->protocol_version == 8) {
		if (mvapich_processops(st) < 0)
			goto fail;
	} else {
		mvapich_debug ("bcasting mvapich info to %d tasks", st->nprocs);
		mvapich_bcast (st);

		if (mvapich_dual_phase (st) && st->protocol_phase == 0) {
			mvapich_debug2 ("protocol phase 0 complete\n");
			st->protocol_phase = 1;
			goto again;
		}

		mvapich_debug ("calling mvapich_barrier");
		mvapich_barrier (st);
		mvapich_debug ("all tasks have checked in");
	}

	do_timings (st);

	mvapich_wait_for_abort (st);

	mvapich_mvarray_destroy (st);

	return (NULL);

fail:
	error ("mvapich: fatal error, killing job");
	mvapich_terminate_job (st);
	return (void *)0;
}

static int process_environment (mvapich_state_t *st)
{
	char *val;

	if (getenv ("MVAPICH_CONNECT_TWICE"))
		st->connect_once = 0;

	if ((val = getenv ("SLURM_MVAPICH_DEBUG"))) {
		int level = atoi (val);
		if (level > 0)
			mvapich_verbose = level;
	}

	if (getenv ("SLURM_MVAPICH_TIMING"))
		st->do_timing = 1;

	if ((val = getenv ("SLURM_MVAPICH_TIMEOUT"))) {
		st->timeout = atoi (val);
	}

	return (0);
}

static mvapich_state_t *
mvapich_state_create(const mpi_plugin_client_info_t *job)
{
	mvapich_state_t *state;

	state = (mvapich_state_t *)xmalloc(sizeof(mvapich_state_t));

	state->tid		= (pthread_t)-1;
	state->mvarray          = NULL;
	state->fd               = -1;
	state->nprocs           = job->step_layout->task_cnt;
	state->protocol_version = -1;
	state->protocol_phase   = 0;
	state->connect_once     = 1;
	state->do_timing        = 0;
	state->timeout          = 600;

	*(state->job) = *job;

	return state;
}

static void mvapich_state_destroy(mvapich_state_t *st)
{
	xfree(st);
}

/*
 *  Create a unique MPIRUN_ID for jobid/stepid pairs.
 *  Combine the least significant bits of the jobid and stepid
 */
int mpirun_id_create(const mpi_plugin_client_info_t *job)
{
	return (int) ((job->jobid << 16) | (job->stepid & 0xffff));
}

extern mvapich_state_t *mvapich_thr_create(const mpi_plugin_client_info_t *job,
					   char ***env)
{
	short port;
	pthread_attr_t attr;
	mvapich_state_t *st = NULL;

	st = mvapich_state_create(job);
	if (process_environment (st) < 0) {
		error ("mvapich: Failed to read environment settings\n");
		mvapich_state_destroy(st);
		return NULL;
	}
	if (net_stream_listen(&st->fd, &port) < 0) {
		error ("Unable to create ib listen port: %m");
		mvapich_state_destroy(st);
		return NULL;
	}

	fd_set_nonblocking (st->fd);

	/*
	 * Accept in a separate thread.
	 */
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&st->tid, &attr, &mvapich_thr, (void *)st)) {
		slurm_attr_destroy(&attr);
		mvapich_state_destroy(st);
		return NULL;
	}
	slurm_attr_destroy(&attr);

	/*
	 *  Set some environment variables in current env so they'll get
	 *   passed to all remote tasks
	 */
	env_array_overwrite_fmt(env, "MPIRUN_PORT",   "%hu", port);
	env_array_overwrite_fmt(env, "MPIRUN_NPROCS", "%d", st->nprocs);
	env_array_overwrite_fmt(env, "MPIRUN_ID",     "%d", mpirun_id_create(job));
	if (st->connect_once) {
		env_array_overwrite_fmt(env, "MPIRUN_CONNECT_ONCE", "1");
	}

	verbose ("mvapich-0.9.[45] master listening on port %hu", port);

	return st;
}

extern int mvapich_thr_destroy(mvapich_state_t *st)
{
	if (st != NULL) {
		if (st->tid != (pthread_t)-1) {
			pthread_cancel(st->tid);
			pthread_join(st->tid, NULL);
		}
		mvapich_state_destroy(st);
	}
	return SLURM_SUCCESS;
}
