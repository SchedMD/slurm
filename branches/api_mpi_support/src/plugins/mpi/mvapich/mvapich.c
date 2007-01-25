/*****************************************************************************\
 *  mvapich.c - srun support for MPICH-IB (MVAPICH 0.9.4 and 0.9.5,7,8)
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).  
 *
 *  UCRL-CODE-226842.
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

struct mvapich_args {
	mpi_plugin_client_info_t *job;    /* job step information          */
	int fd;             /* fd on which to accept new connections */
};


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
static struct mvapich_info **mvarray = NULL;
static int  mvapich_fd       = -1;
static int  nprocs           = -1;
static int  protocol_version = -1;
static int  protocol_phase   = 0;
static int  connect_once     = 1;
static int  mvapich_verbose  = 0;
static int  do_timing        = 0;


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

static int mvapich_requires_pids (void)
{
	if ( protocol_version == MVAPICH_VERSION_REQUIRES_PIDS 
	  || protocol_version == 5
	  || protocol_version == 6 )
		return (1);
	return (0);
}

/*
 *  Return non-zero if protocol version has two phases.
 */
static int mvapich_dual_phase (void)
{
	return (protocol_version == 5 || protocol_version == 6);
}

static int mvapich_abort_sends_rank (void)
{
	if (protocol_version >= 3)
		return (1);
	return (0);
}

/*
 *  Create an mvapich_info object by reading information from
 *   file descriptor `fd'
 */
static int mvapich_get_task_info (struct mvapich_info *mvi)
{
	int fd = mvi->fd;

	if (fd_read_n (fd, &mvi->addrlen, sizeof (int)) <= 0)
		return error ("mvapich: Unable to read addrlen for rank %d: %m", 
				mvi->rank);

	mvi->addr = xmalloc (mvi->addrlen);

	if (fd_read_n (fd, mvi->addr, mvi->addrlen) <= 0)
		return error ("mvapich: Unable to read addr info for rank %d: %m", 
				mvi->rank);

	if (!mvapich_requires_pids ())
		return (0);

	if (fd_read_n (fd, &mvi->pidlen, sizeof (int)) <= 0)
		return error ("mvapich: Unable to read pidlen for rank %d: %m", 
				mvi->rank);


	mvi->pid = xmalloc (mvi->pidlen);

	if (fd_read_n (fd, mvi->pid, mvi->pidlen) <= 0)
		return error ("mvapich: Unable to read pid for rank %d: %m", mvi->rank);


	mvi->do_poll = 0;

	return (0);
}

static int mvapich_get_hostid (struct mvapich_info *mvi)
{
	if (fd_read_n (mvi->fd, &mvi->hostidlen, sizeof (int)) < 0)
		return error ("mvapich: Unable to read hostidlen for rank %d: %m",
				mvi->rank);
	if (mvi->hostidlen != sizeof (int))
		return error ("mvapich: Unexpected size for hostidlen (%d)", mvi->hostidlen);
	if (fd_read_n (mvi->fd, &mvi->hostid, sizeof (int)) < 0)
		return error ("mvapich: unable to read hostid from rank %d", 
				mvi->rank);

	return (0);
}

static int mvapich_get_task_header (int fd, int *version, int *rank)
{
	/*
	 *  dual phase only sends version on first pass
	 */
	if (!mvapich_dual_phase () || protocol_phase == 0) {
		if (fd_read_n (fd, version, sizeof (int)) < 0) 
			return error ("mvapich: Unable to read version from task: %m");
	} 

	if (fd_read_n (fd, rank, sizeof (int)) < 0) 
		return error ("mvapich: Unable to read task rank: %m");

	if (mvapich_dual_phase () && protocol_phase > 0)
		return (0);

	if (protocol_version == -1)
		protocol_version = *version;
	else if (protocol_version != *version) {
		return error ("mvapich: rank %d version %d != %d", *rank, *version, 
				protocol_version);
	}

	return (0);

}

static int mvapich_handle_task (int fd, struct mvapich_info *mvi)
{
	mvi->fd = fd;

	switch (protocol_version) {
		case 1:
		case 2:
		case 3:
			return mvapich_get_task_info (mvi);
		case 5:
		case 6:
			if (protocol_phase == 0)
				return mvapich_get_hostid (mvi);
			else
				return mvapich_get_task_info (mvi);
		default:
			return (error ("mvapich: Unsupported protocol version %d", 
					protocol_version));
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
static void mvapich_bcast_addrs (void)
{
	struct mvapich_info *m;
	int out_addrs_len = 3 * nprocs * sizeof (int);
	int *out_addrs = xmalloc (out_addrs_len);
	int i = 0;
	int j = 0;

	for (i = 0; i < nprocs; i++) {
		m = mvarray[i];
		/*
		 * lids are found in addrs[rank] for each process
		 */
		out_addrs[i] = m->addr[m->rank];

		/*
		 * hostids are the last entry in addrs
		 */
		out_addrs[2 * nprocs + i] =
			m->addr[(m->addrlen/sizeof (int)) - 1];
	}

	for (i = 0; i < nprocs; i++) {
		m = mvarray[i];

		/*
		 * qp array is tailored to each process.
		 */
		for (j = 0; j < nprocs; j++)  
			out_addrs[nprocs + j] = 
				(i == j) ? -1 : mvarray[j]->addr[i];

		fd_write_n (m->fd, out_addrs, out_addrs_len);

		/*
		 * Protocol version 3 requires pid list to be sent next
		 */
		if (mvapich_requires_pids ()) {
			for (j = 0; j < nprocs; j++)
				fd_write_n (m->fd, &mvarray[j]->pid,
					    mvarray[j]->pidlen);
		}

	}

	xfree (out_addrs);
	return;
}

static void mvapich_bcast_hostids (void)
{
	int *  hostids;
	int    i   = 0;
	size_t len = nprocs * sizeof (int);

	hostids = xmalloc (len);

	for (i = 0; i < nprocs; i++)
		hostids [i] = mvarray[i]->hostid;

	for (i = 0; i < nprocs; i++) {
		struct mvapich_info *mvi = mvarray [i];
		int co, rc;
		if (fd_write_n (mvi->fd, hostids, len) < 0)
			error ("mvapich: write hostid rank %d: %m", mvi->rank);

		if ((rc = fd_read_n (mvi->fd, &co, sizeof (int))) <= 0) {
			close (mvi->fd);
			connect_once = 0;
		} else
			mvi->do_poll = 1;
	}

	xfree (hostids);
}

static void mvapich_bcast (void)
{
	if (!mvapich_dual_phase () || protocol_phase > 0)
		return mvapich_bcast_addrs ();
	else
		return mvapich_bcast_hostids ();
}

static void mvapich_barrier (void)
{
	int i;
	struct mvapich_info *m;
	/*
	 *  Simple barrier to wait for qp's to come up. 
	 *   Once all processes have written their rank over the socket,
	 *   simply write their rank right back to them.
	 */

	debug ("mvapich: starting barrier");

	for (i = 0; i < nprocs; i++) {
		int j;
		m = mvarray[i];
		if (fd_read_n (m->fd, &j, sizeof (j)) == -1)
			error("mvapich read on barrier");
	}

	debug ("mvapich: completed barrier for all tasks");

	for (i = 0; i < nprocs; i++) {
		m = mvarray[i];
		if (fd_write_n (m->fd, &i, sizeof (i)) == -1)
			error("mvapich: write on barrier: %m");
		close (m->fd);
		m->fd = -1;
	}

	return;
}

static void 
mvapich_print_abort_message (srun_job_t *job, int rank, int dest, 
		char *msg, int msglen)
{
	slurm_step_layout_t *sl = job->step_layout;
	char *host;

	if (!mvapich_abort_sends_rank ()) {
		info ("mvapich: Received ABORT message from an MPI process.");
		return;
	}

	host = slurm_step_layout_host_name(
		sl, slurm_step_layout_host_id(sl, rank));

	if (dest >= 0) {
		const char *dsthost = slurm_step_layout_host_name (sl, dest);

		if (msg [msglen - 1] == '\n')
			msg [msglen - 1] = '\0';

		info ("mvapich: %M: ABORT from MPI rank %d [on %s] dest rank %d [on %s]",
		      rank, host, dest, dsthost);

		/*
		 *  If we got a message from MVAPICH, log it to syslog
		 *   so that system administrators know about possible HW events.
		 */
		if (msglen > 0) {
			openlog ("srun", 0, LOG_USER);
			syslog (LOG_WARNING, 
					"MVAPICH ABORT [jobid=%u.%u src=%d(%s) dst=%d(%s)]: %s",
					job->jobid, job->stepid, rank, host, dest, dsthost, msg);
			closelog();
		}
	}
	else {
		info ("mvapich: %M: ABORT from MPI rank %d [on %s]", 
				rank, host);
	}
	return;
}


static void mvapich_wait_for_abort(mpi_plugin_client_info_t *job)
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
		slurm_addr addr;
		int newfd = slurm_accept_msg_conn (mvapich_fd, &addr);

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
		if (ranks[1] >= 0) {
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

		mvapich_print_abort_message (job, src, dst, msg, msglen);
		slurm_signal_job_step(job->jobid, job->stepid, SIGKILL);
	}

	return; /* but not reached */
}

static void mvapich_mvarray_create (void)
{
	int i;
	mvarray = xmalloc (nprocs * sizeof (*mvarray));
	for (i = 0; i < nprocs; i++) {
		mvarray [i] = mvapich_info_create ();
		mvarray [i]->rank = i;
	}
}

static void mvapich_mvarray_destroy (void)
{
	int i;
	for (i = 0; i < nprocs; i++)
		mvapich_info_destroy (mvarray [i]);
	xfree (mvarray);
}

static int mvapich_rank_from_fd (int fd)
{
	int rank = 0;
	while (mvarray[rank]->fd != fd)
		rank++;
	return (rank);
}

static int mvapich_handle_connection (int fd)
{
	int version, rank;

	if (protocol_phase == 0 || !connect_once) {
		if (mvapich_get_task_header (fd, &version, &rank) < 0)
			return (-1);

		mvarray [rank]->rank = rank;

		if (rank > nprocs - 1) 
			return (error ("mvapich: task reported invalid rank (%d)", rank));
	} else {
		rank = mvapich_rank_from_fd (fd);
	}

	if (mvapich_handle_task (fd, mvarray [rank]) < 0) 
		return (-1);

	return (0);
}

static int poll_mvapich_fds (void)
{
	int i = 0;
	int j = 0;
	int rc;
	int fd;
	int nfds = 0;
	struct pollfd *fds = xmalloc (nprocs * sizeof (struct pollfd));

	for (i = 0; i < nprocs; i++) {
		if (mvarray[i]->do_poll) {
			fds[j].fd = mvarray[i]->fd;
			fds[j].events = POLLIN;
			j++;
			nfds++;
		}
	}

	mvapich_debug2 ("Going to poll %d fds", nfds);
	if ((rc = poll (fds, nfds, -1)) < 0) 
		return (error ("mvapich: poll: %m"));

	i = 0;
	while (fds[i].revents != POLLIN)
		i++;

	fd = fds[i].fd;
	xfree (fds);

	return (fd);
}

static int mvapich_get_next_connection (int listenfd)
{
	slurm_addr addr;
	int fd;

	if (connect_once && protocol_phase > 0) {
		return (poll_mvapich_fds ());
	} 
		
	if ((fd = slurm_accept_msg_conn (mvapich_fd, &addr)) < 0) {
		error ("mvapich: accept: %m");
		return (-1);
	}
	mvapich_debug2 ("accept() = %d", fd);

	return (fd);
}

static void do_timings (void)
{
	static int initialized = 0;
	static struct timeval initv = { 0, 0 };
	struct timeval tv;
	struct timeval result;

	if (!do_timing)
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
	mpi_plugin_client_info_t *job = arg;
	int i = 0;
	int first = 1;

	debug ("mvapich-0.9.x/gen2: thread started: %ld", pthread_self ());

	mvapich_mvarray_create ();

again:
	i = 0;
	while (i < nprocs) {
		int fd;
		
		mvapich_debug ("Waiting to accept remote connection %d of %d\n", 
				i, nprocs);

		if ((fd = mvapich_get_next_connection (mvapich_fd)) < 0) {
			error ("mvapich: accept: %m");
			goto fail;
		}

		if (first) {
			mvapich_debug ("first task checked in");
			do_timings ();
			first = 0;
		}

		if (mvapich_handle_connection (fd) < 0) 
			goto fail;

		i++;
	}

	mvapich_debug ("bcasting mvapich info to %d tasks", nprocs);
	mvapich_bcast ();

	if (mvapich_dual_phase () && protocol_phase == 0) {
		protocol_phase = 1;
		goto again;
	}

	mvapich_debug ("calling mvapich_barrier");
	mvapich_barrier ();
	mvapich_debug ("all tasks have checked in");

	do_timings ();

	mvapich_wait_for_abort (job);

	mvapich_mvarray_destroy ();

	return (NULL);

fail:
	error ("mvapich: fatal error, killing job");
	slurm_signal_job_step(job->jobid, job->stepid, SIGKILL);
	return (void *)0;
}

static int process_environment (void)
{
	char *val;

	if (getenv ("MVAPICH_CONNECT_TWICE"))
		connect_once = 0;

	if ((val = getenv ("SLURM_MVAPICH_DEBUG"))) {
		int level = atoi (val);
		if (level > 0)
			mvapich_verbose = level;
	}

	if (getenv ("SLURM_MVAPICH_TIMING"))
		do_timing = 1;

	return (0);
}

extern int mvapich_thr_create(const mpi_plugin_client_info_t *job,
			      char ***env)
{
	short port;
	pthread_attr_t attr;
	pthread_t tid;

	if (process_environment () < 0)
		return error ("mvapich: Failed to read environment settings\n");

	nprocs = job->step_layout->task_cnt;

	if (net_stream_listen(&mvapich_fd, &port) < 0)
		return error ("Unable to create ib listen port: %m");

	/*
	 * Accept in a separate thread.
	 */
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&tid, &attr, &mvapich_thr, (void *)job))
		return -1;

	/*
	 *  Set some environment variables in current env so they'll get
	 *   passed to all remote tasks
	 */
	env_array_overwrite_fmt(env, "MPIRUN_PORT",   "%d", port);
	env_array_overwrite_fmt(env, "MPIRUN_NPROCS", "%d", nprocs);
	env_array_overwrite_fmt(env, "MPIRUN_ID",     "%d", job->jobid);
	if (connect_once) {
		env_array_overwrite_fmt(env, "MPIRUN_CONNECT_ONCE", "1");
	}

	verbose ("mvapich-0.9.[45] master listening on port %d", port);

	return 0;
}
