/*****************************************************************************\
 *  mvapich.c - srun support for MPICH-IB (MVAPICH 0.9.4 and 0.9.5,7,8)
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

/*
 *  2008-07-03:
 *
 *  This version of mvapich.c has been tested against the following
 *   protocol versions:
 *
 *   Version 8: (pmgr_collective): mvapich-1.0.1, mvapich-1.0
 *   Version 5: mvapich-0.9.9 r1760, mvapich-0.9.7-mlx2.2.0
 *   Version 3: mvapich-0.9.8
 */

/* NOTE: MVAPICH has changed protocols without changing version numbers.
 * This makes support of MVAPICH very difficult.
 * Support for the following versions have been validated:
 *
 * For MVAPICH-GEN2-1.0-103,    set MVAPICH_VERSION_REQUIRES_PIDS to 2
 * For MVAPICH 0.9.4 and 0.9.5, set MVAPICH_VERSION_REQUIRES_PIDS to 3
 *
 * See functions mvapich_requires_pids() below for other mvapich versions.
 *
 */
#define MVAPICH_VERSION_REQUIRES_PIDS 3

#include "mvapich.h"

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
 *  MVAPICH initialization data state codes
 */
enum mv_init_state
{
	MV_READ_VERSION,
	MV_READ_RANK,
	MV_READ_HOSTIDLEN,
	MV_READ_HOSTID,
	MV_READ_ADDRLEN,
	MV_READ_ADDRS,
	MV_READ_PIDLEN,
	MV_READ_PID,
	MV_INIT_DONE,
};

/*
 *  Information cache for each MVAPICH process
 */
struct mvapich_info
{
	int do_poll;
	enum mv_init_state state; /* Initialization state            */
	int nread;                /* Amount of data read so far      */
	int nwritten;             /* Amount of data written          */

	int fd;             /* fd for socket connection to MPI task  */
	int version;        /* Protocol version for this rank        */
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
	int nconnected;
	int protocol_version;
	int protocol_phase;
	int connect_once;
	int do_timing;

	int timeout;          /* Initialization timeout in seconds  */
	int start_time;       /* Time from which to measure timeout */

	int shutdown_pipe[2]; /* Write to this pipe to interrupt poll calls */
	bool shutdown_complete;  /* Set true when mpi thr about to exit */
	int  shutdown_timeout;   /* Num secs for main thread to wait for
				    mpi thread to finish */

	pthread_mutex_t  shutdown_lock;
	pthread_cond_t   shutdown_cond;

	mpi_plugin_client_info_t job[1];
};

/*
 *  MVAPICH poll structure used by mvapich_poll_next, etc.
 */
struct mvapich_poll
{
	mvapich_state_t      *st;
	struct mvapich_info **mvmap;
	struct pollfd        *fds;
	int                   counter;
	int                   nfds;
};


/*
 *  mvapich debugging defines
 */
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

#define mvapich_debug3(args...) \
	do { \
		if (mvapich_verbose > 2) \
			info ("mvapich: " args); \
	} while (0);


static void do_timings (mvapich_state_t *st, const char *fmt, ...);
void mvapich_thr_exit(mvapich_state_t *st);

static int mvapich_requires_pids (mvapich_state_t *st)
{
	if ( st->protocol_version == MVAPICH_VERSION_REQUIRES_PIDS
	  || st->protocol_version == 5
	  || st->protocol_version == 6 )
		return (1);
	return (0);
}

/*
 *  Return the number of ms left until the MVAPICH startup
 *   timeout expires.
 */
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

char * vmsg (const char *msg, va_list ap)
{
	int n = -1;
	int size = BUFSIZ;
	va_list vp;
	char *p = xmalloc (size);

	while (1) {
		va_copy (vp, ap);
		n = vsnprintf (p, size, msg, vp);
		va_end (vp);

		if (n > -1 && n < size)
			return (p);

		if (n > -1)
			size = n + 1;
		else if (n == -1)
			size *= 2;

		p = xrealloc (p, size);
	}

	return (p);
}


/*
 *  Forcibly kill job (with optional error message).
 */
static int mvapich_terminate_job (mvapich_state_t *st, const char *msg, ...)
{
	if (msg) {
		va_list ap;
		va_start (ap, msg);
		char *p = vmsg (msg, ap);
		error ("mvapich: %s", p);
		xfree (p);
	}

	slurm_kill_job_step (st->job->jobid, st->job->stepid, SIGKILL);
	/* Give srun a chance to terminate job */
	sleep (5);
	/* exit forcefully */
	exit (1);
	/* NORETURN */
}

static struct mvapich_info *mvapich_info_find (mvapich_state_t *st, int rank)
{
	int i;

	for (i = 0; i < st->nprocs; i++) {
		if (st->mvarray[i] && st->mvarray[i]->rank == rank)
			return (st->mvarray[i]);
	}
	return (NULL);
}

/*
 *  Issue a report of tasks/hosts that we may be waiting for.
 *   by checking either mvi->fd < 0 || mvi->do_poll == 1.
 */
static void report_absent_tasks (mvapich_state_t *st, int check_do_poll)
{
	int i;
	char buf[16];
	hostlist_t tasks = hostlist_create (NULL);
	hostlist_t hosts = hostlist_create (NULL);
	slurm_step_layout_t *sl = st->job->step_layout;

	for (i = 0; i < st->nprocs; i++) {
		struct mvapich_info *m = mvapich_info_find (st ,i);

		if ((m == NULL) || (m->fd < 0) || (check_do_poll && m->do_poll)) {
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
		error ("mvapich: timeout: waiting on rank%s %s on host%s %s.\n",
				nranks > 1 ? "s" : "", r,
				nhosts > 1 ? "s" : "", h);
	}

	hostlist_destroy (hosts);
	hostlist_destroy (tasks);
}


static struct mvapich_info * mvapich_info_create (void)
{
	struct mvapich_info *mvi = xmalloc (sizeof (*mvi));
	memset (mvi, 0, sizeof (*mvi));
	mvi->fd = -1;
	mvi->rank = -1;
	mvi->state = MV_READ_VERSION;
	mvi->nread = 0;

	return (mvi);
}

static void mvapich_info_destroy (struct mvapich_info *mvi)
{
	xfree (mvi->addr);
	xfree (mvi->pid);
	xfree (mvi);
	return;
}

/*
 *  Reset an mvapich_poll object so it may be used again.
 */
static void mvapich_poll_reset (struct mvapich_poll *mp)
{
	int i;
	mp->counter = 0;
	mp->nfds = 0;

	/*
	 *  Reset mvapich_info do_poll attribute.
	 */
	for (i = 0; i < mp->st->nprocs; i++)
		mp->st->mvarray[i]->do_poll = 1;
	return;
}


/*
 *  Create an mvapich_poll object, used to poll all mvapich
 *   file descriptors for read/write activity
 *
 *  Resets do_poll for all mvapich_info objects in mvarray to 1.
 *   (Thus, only one mvapich_poll should be in use at a time)
 */
static struct mvapich_poll * mvapich_poll_create (mvapich_state_t *st)
{
	struct mvapich_poll *mp = xmalloc (sizeof (*mp));

	mp->mvmap   = xmalloc (st->nprocs * sizeof (struct mvapich_info *));
	mp->fds     = xmalloc (st->nprocs * sizeof (struct pollfd));
	mp->st = st;

	mvapich_poll_reset (mp);

	return (mp);
}

static void mvapich_poll_destroy (struct mvapich_poll *mp)
{
	xfree (mp->mvmap);
	xfree (mp->fds);
	xfree (mp);
}


/*
 *  Call poll(2) on mvapich_poll object, handling EAGAIN and EINTR errors.
 */
static int mvapich_poll_internal (struct mvapich_poll *mp)
{
	int n;
	while ((n = poll (mp->fds, mp->nfds, startup_timeout (mp->st))) < 0) {
		if (errno != EINTR && errno != EAGAIN)
			return (-1);
	}
	return (n);
}

/*
 *  Poll for next available mvapich_info object with read/write activity
 *
 *  Returns NULL when no more mvapich fds need to be polled.
 *
 *  The caller is responsible for updating mvi->do_poll to indicate
 *    when a mvapich_info object's file descriptor no longer needs
 *    to be polled for activity.
 *
 */
static struct mvapich_info *
mvapich_poll_next (struct mvapich_poll *mp, int do_read)
{
	int i, rc;
	int event = do_read ? POLLIN : POLLOUT;
	mvapich_state_t *st = mp->st;

again:
	/*
	 *  If the loop counter is 0, then we need to reset data structures
	 *    and poll again.
	 */
	if (mp->counter == 0) {
		int j = 0;

		memset (mp->fds, 0, sizeof (st->nprocs * sizeof (struct pollfd)));
		memset (mp->mvmap, 0, sizeof (st->nprocs * sizeof (*mp->mvmap)));
		mp->nfds = 0;

		for (i = 0; i < st->nprocs; i++) {
			struct mvapich_info *mvi = mp->st->mvarray [i];
			if (mvi->do_poll) {
				mp->mvmap[j] = mvi;
				mp->fds[j].fd = mvi->fd;
				mp->fds[j].events = event;
				j++;
				mp->nfds++;
			}
		}

		/*
		 *  If there are no more file descriptors to poll, then
		 *   return NULL to indicate we're done.
		 */
		if (mp->nfds == 0)
			return (NULL);

		mvapich_debug3 ("mvapich_poll_next (nfds=%d, timeout=%d)\n",
				mp->nfds, startup_timeout (st));
		if ((rc = mvapich_poll_internal (mp)) < 0)
			mvapich_terminate_job (st, "mvapich_poll_next: %m");
		else if (rc == 0) {
			/*
			 *  If we timed out, then report all tasks that we were
			 *   still waiting for.
			 */
			report_absent_tasks (st, 1);
			mvapich_terminate_job (st, NULL);
		}
	}

	/*
	 *  Loop through poll fds and return first mvapich_info object
	 *   we find that has the requested read/write activity.
	 *   When found, we update the loop counter, and return
	 *   the corresponding mvapich_info object.
	 *
	 */
	for (i = mp->counter; i < mp->nfds; i++) {
		if (mp->fds[i].revents == event) {
			mp->counter = i+1;
			return (mp->mvmap[i]);
		}
	}

	mp->counter = 0;
	goto again;

	return (NULL);
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
			report_absent_tasks (st, 0);
		}

		mvapich_terminate_job (st, NULL);
		/* NORETURN */
	}

	return (rc);
}

static int mvapich_write (struct mvapich_info *mvi, void * buf, size_t len)
{
	size_t nleft;
	ssize_t n;
	unsigned char *p;

	p = buf + mvi->nwritten;
	nleft = len - mvi->nwritten;

	n = write (mvi->fd, p, nleft);

	if ((n < 0) && (errno != EAGAIN)) {
		error ("mvapich: rank %d: write (%d/%ld): %m\n", mvi->rank, nleft, len);
		return (-1);
	}

	if (n > 0)
		mvi->nwritten += n;

	if (mvi->nwritten == len) {
		mvi->nwritten = 0;
		mvi->do_poll = 0;
	}

	return (0);
}

static int mvapich_read (struct mvapich_info *mvi, void * buf, size_t len)
{
	size_t nleft;
	ssize_t n;
	unsigned char *p;

	p = buf + mvi->nread;
	nleft = len - mvi->nread;

	n = read (mvi->fd, p, nleft);

	if ((n < 0) && (errno != EAGAIN)) {
		error ("mvapich: rank %d: read (%d/%ld): %m\n", mvi->rank, nleft, len);
		return (-1);
	}

	if (n > 0)
		mvi->nread += n;

	if (mvi->nread == len) {
		mvi->nread = 0;
		mvi->do_poll = 0;
	}

	return (0);
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
		/* Poll for read-activity */
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

	mvapich_debug2 ("Bcasting addrs to %d tasks", st->nprocs);

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

		mvapich_debug3 ("writing addrs to task %d", i);
		mvapich_write_n (st, m, out_addrs, out_addrs_len);
		if (mvapich_verbose > 2)
			do_timings (st, "Write addrs to task %d", i);

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
	struct mvapich_poll *mp;
	struct mvapich_info *mvi;
	int *  hostids;
	int    i   = 0;
	size_t len = st->nprocs * sizeof (int);

	hostids = xmalloc (len);

	for (i = 0; i < st->nprocs; i++)
		hostids [i] = st->mvarray[i]->hostid;

	/*
	 *  Broadcast hostids
	 */
	mvapich_debug ("bcasting hostids\n");
	mp = mvapich_poll_create (st);
	while ((mvi = mvapich_poll_next (mp, 0))) {
		if (mvapich_write (mvi, hostids, len) < 0)
			mvapich_terminate_job (st, "write hostid rank %d: %m", mvi->rank);
	}
	xfree (hostids);

	/*
	 *  Read connect_once value from every rank
	 *   Each rank will either close the connection (connect_once = 0)
	 *    or send the connect_once value (presumed 1).
	 */
	mvapich_debug ("reading connect once value");
	mvapich_poll_reset (mp);
	while ((mvi = mvapich_poll_next (mp, 1))) {
		int co = 1, rc;
		mvapich_debug3 ("reading connect once value from rank %d fd=%d\n",
				mvi->rank, mvi->fd);
		if ((rc = read (mvi->fd, &co, sizeof (int))) <= 0) {
			mvapich_debug2 ("reading connect once value rc=%d: %m\n", rc);
			close (mvi->fd);
			mvi->fd = -1;
			st->connect_once = 0;
		}
		mvi->do_poll = 0;
	}

	mvapich_poll_destroy (mp);
	return;
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
	int rc = 0;
	int n = 0;
	struct mvapich_poll *mp;
	struct mvapich_info *mvi;

	mp = mvapich_poll_create (st);
	while ((mvi = mvapich_poll_next (mp, 0))) {
		if ((rc = mvapich_write (mvi, buf + (mvi->rank * size), size)) < 0)
			break;
		n += rc;
	}
	mvapich_poll_destroy (mp);

	return (rc < 0 ? rc : n);
}

/* Broadcast buf to each rank, which is size bytes big */
static int mvapich_allgatherbcast (mvapich_state_t *st, void* buf, int size)
{
	int rc = 0;
	int n = 0;
	struct mvapich_poll *mp;
	struct mvapich_info *mvi;

	mp = mvapich_poll_create (st);
	while ((mvi = mvapich_poll_next (mp, 0))) {
		if ((rc = mvapich_write (mvi, buf, size)) < 0)
			break;
		n += rc;
	}
	mvapich_poll_destroy (mp);

	return (rc < 0 ? rc : n);
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
		error ("mvapich: recv_common_value: rank %d: %m\n", rank);
		return (-1);
	}
	mvapich_debug3 ("recv_common_value (rank=%d, val=%d)\n", rank, *valp);

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
	mvapich_debug3 ("PMGR_BCAST: recv from root\n");
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

	mvapich_debug3 ("PMGR_GATHER: recv from rank %d\n", rank);
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
	mvapich_debug3 ("PMGR_SCATTER: recv from rank %d", rank);
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

	mvapich_debug3 ("PMGR_ALLGATHER: recv from rank %d\n", rank);
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
	mvapich_debug3 ("PMGR_ALLTOALL: recv from rank %d\n", rank);
	if (mvapich_recv ( st,
				*bufp + (*sizep * st->nprocs)*rank,
				*sizep * st->nprocs, rank ) < 0) {
		error ("mvapich: PMGR_ALLTOALL: recv: rank %d: %m", rank);
		return (-1);
	}

	return (0);
}


static int mvapich_process_op (mvapich_state_t *st,
		struct mvapich_info *mvi, int *rootp, int *opcodep,
		void **bufp, int *sizep)
{
	int rank, code, opcode = -1;
	int exit = 0;

	// read in opcode
	if (recv_common_value (st, opcodep, mvi->rank) < 0) {
		error ("mvapich: rank %d: Failed to read opcode: %m",
				mvi->rank);
		return (-1);
	}

	opcode = *opcodep;
	mvapich_debug3 ("rank %d: opcode=%d\n", mvi->rank, opcode);

	// read in additional data depending on current opcode

	switch (*opcodep) {
		case 0: // PMGR_OPEN (followed by rank)
			if (mvapich_recv (st, &rank, sizeof (int), mvi->rank) <= 0) {
				error ("mvapich: PMGR_OPEN: recv: %m");
				exit = 1;
			}
			break;
		case 1: // PMGR_CLOSE (no data, close the socket)
			close(mvi->fd);
			break;
		case 2: // PMGR_ABORT (followed by exit code)
			if (mvapich_recv (st, &code, sizeof (int), mvi->rank) <= 0) {
				error ("mvapich: PMGR_ABORT: recv: %m");
			}
			error("mvapich abort with code %d from rank %d", code, mvi->rank);
			break;
		case 3: // PMGR_BARRIER (no data)
			break;
		case 4: // PMGR_BCAST
			if (process_pmgr_bcast (st, rootp, sizep, bufp, mvi->rank) < 0)
				return (-1);
			break;
		case 5: // PMGR_GATHER
			if (process_pmgr_gather (st, rootp, sizep, bufp, mvi->rank) < 0)
				return (-1);
			break;
		case 6: // PMGR_SCATTER
			if (process_pmgr_scatter (st, rootp, sizep, bufp, mvi->rank) < 0)
				return (-1);
			break;
		case 7: // PMGR_ALLGATHER
			if (process_pmgr_allgather (st, sizep, bufp, mvi->rank) < 0)
				return (-1);
			break;
		case 8: // PMGR_ALLTOALL
			if (process_pmgr_alltoall (st, sizep, bufp, mvi->rank) < 0)
				return (-1);
			break;
		default:
			error("Unrecognized PMGR opcode: %d", opcode);
			return (-1);
	}

	return (exit);
}

static int mvapich_complete_op (mvapich_state_t *st, int opcode, int root,
		void *buf, int size)
{
	int rc = 0;

	switch(opcode) {
		case 0: // PMGR_OPEN
			mvapich_debug ("Completed PMGR_OPEN");
			break;
		case 1: // PMGR_CLOSE
			mvapich_debug ("Completed PMGR_CLOSE");
			rc = 1;
			break;
		case 2: // PMGR_ABORT
			mvapich_debug ("Completed PMGR_ABORT");
			rc = 1;
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

	return (rc);
}

static int mvapich_pmgr_loop (mvapich_state_t *st)
{
	int opcode = -1;
	int root   = -1;
	int size   = -1;
	int done   = 0;
	void * buf = NULL;

	int completed = 0;
	struct mvapich_info *mvi;
	struct mvapich_poll *mp;

	mvapich_debug ("Processing PMGR opcodes");

	// for each process, read in one opcode and its associated data
	mp = mvapich_poll_create (st);
	while ((mvi = mvapich_poll_next (mp, 1))) {
		done = mvapich_process_op (st, mvi, &root, &opcode, &buf, &size);
		completed++;
		mvi->do_poll = 0;
	}
	mvapich_poll_destroy (mp);

	// Complete any operations
	done = mvapich_complete_op (st, opcode, root, buf, size);

	return (done);
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
	mvapich_debug ("Initiated PMGR processing\n");
	while (mvapich_pmgr_loop (st) != 1) {};
	mvapich_debug ("Completed processing PMGR opcodes\n");

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
	struct mvapich_poll *mp;
	/*
	 *  Simple barrier to wait for qp's to come up.
	 *   Once all processes have written their rank over the socket,
	 *   simply write their rank right back to them.
	 */

	debug ("mvapich: starting barrier");
	mp = mvapich_poll_create (st);
	while ((m = mvapich_poll_next (mp, 1)))
		mvapich_read (m, &i, sizeof (i));

	debug ("mvapich: completed barrier for all tasks");

	mvapich_poll_reset (mp);
	while ((m = mvapich_poll_next (mp, 0)))
		mvapich_write (m, &m->rank, sizeof (m->rank));

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


/*
 * Returns file descriptor from which to read abort message,
 * -1 on error, or exits if shutdown message is received
 */

static int mvapich_abort_accept (mvapich_state_t *st)
{
	slurm_addr addr;
	int rc;
	struct pollfd pfds[2];

	/*
	 * st->fd accepts connections from MPI procs to indicate an MPI error
	 * st->shutdown_pipe is written to by the main thread, to break out
	 * of the poll call when it is time to shut down
	 */

	pfds[0].fd = st->fd;
	pfds[0].events = POLLIN;

	pfds[1].fd = st->shutdown_pipe[0];
	pfds[1].events = POLLIN;

	mvapich_debug3 ("Polling to accept MPI_ABORT timeout=%d",
			mvapich_abort_timeout ());

	/*
	 * limit cancellation to the long periods waiting on this poll
	 */
	while ((rc = poll (pfds, 2, mvapich_abort_timeout ())) < 0) {
		if (errno == EINTR || errno == EAGAIN)
			continue;

		return (-1);
	}

	/*
	 *  If poll() timed out, forcibly kill job and exit instead of
	 *   waiting longer for remote IO, process exit, etc.
	 */
	if (rc == 0) {
		mvapich_terminate_job (st, "Timeout waiting for all tasks after ABORT.");
		/* NORETURN */
	}

	if (pfds[1].revents & POLLIN) {
		mvapich_thr_exit(st);
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


static void mvapich_mvarray_destroy (mvapich_state_t *st)
{
	int i;

	if (st->mvarray) {
		for (i = 0; i < st->nprocs; i++) {
			if (st->mvarray[i])
				mvapich_info_destroy(st->mvarray[i]);
		}
		xfree(st->mvarray);
	}
}

static void do_timings (mvapich_state_t *st, const char *fmt, ...)
{
	static int initialized = 0;
	static struct timeval initv = { 0, 0 };
	struct timeval tv;
	struct timeval result;
	char *msg;
	va_list ap;

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

	va_start (ap, fmt);
	msg = vmsg (fmt, ap);
	va_end (ap);

	info ("mvapich: %s took %d.%03d seconds", msg, result.tv_sec,
			result.tv_usec/1000);

	xfree (msg);

	return;
}

static int mvapich_read_item (struct mvapich_info *mvi, void *buf, size_t size)
{
	size_t nleft;
	ssize_t n;
	unsigned char *p;

	p = buf + mvi->nread;
	nleft = size - mvi->nread;

	if ((n = read (mvi->fd, p, nleft)) < 0) {
		if (errno == EAGAIN)
			return (EAGAIN);
		else {
			error ("mvapich: %d: nread=%d, read (%d, %lx, size=%d, nleft=%d): %m",
					mvi->rank, mvi->nread, mvi->fd, buf, size, nleft);
			return (-1);
		}
	}

	mvi->nread += n;
	if (mvi->nread == size) {
		mvi->nread = 0;
		mvi->state++;
	}

	return (0);
}

/*
 *  Process initial mvapich states to read items such as
 *   version, rank, hostidlen, hostids... and so on.
 *
 *  Current state is tracked int he mvapich_info object itself
 *   and state transitions happen automatically in mvapich_read_item()
 *   when the current item is completely read. Early exit from
 *   the state processing may occur based on protocol version.
 *   Similarly, some protocol version may enter state processing
 *   at a different point.
 *
 *  State processing is considered complete when state == MV_INIT_DONE.
 *
 */
static int mvapich_info_process_init (mvapich_state_t *st,
		                              struct mvapich_info *mvi)
{
	int rc = 0;

again:
	switch (mvi->state) {
	case MV_READ_VERSION:
		mvapich_debug2 ("fd %d: reading mvapich version.", mvi->fd);
		rc = mvapich_read_item (mvi, &mvi->version, sizeof (mvi->version));

		if (mvi->state != MV_READ_RANK)
			break;

	case MV_READ_RANK:
		if (st->protocol_version < 0)
			st->protocol_version = mvi->version;

		mvapich_debug2 ("fd %d: reading mvapich rank. version = %d",
				mvi->fd, mvi->version);

		rc = mvapich_read_item (mvi, &mvi->rank, sizeof (int));

		/*
		 *  No hostids in protocol version 3.
		 */
		if (mvi->version == 3 && mvi->state == MV_READ_HOSTIDLEN) {
			mvi->state = MV_READ_ADDRLEN;
			goto again;
		}

		if (mvi->version >= 8 || mvi->state != MV_READ_HOSTIDLEN)
			break;

	case MV_READ_HOSTIDLEN:
		mvapich_debug2 ("rank %d: reading hostidlen.", mvi->rank);

		mvi->hostidlen = 0;
		rc = mvapich_read_item (mvi, &mvi->hostidlen, sizeof (mvi->hostidlen));

		if (mvi->state != MV_READ_HOSTID)
			break;

	case MV_READ_HOSTID:
		if (mvi->hostidlen != sizeof (int)) {
			error ("mvapich: rank %d: unexpected hostidlen = %d\n",
					mvi->rank, mvi->hostidlen);
			return (-1);
		}
		mvapich_debug2 ("rank %d: reading hostid. hostidlen = %d",
				mvi->rank, mvi->hostidlen);

		rc = mvapich_read_item (mvi, &mvi->hostid, mvi->hostidlen);

		if (mvi->state != MV_READ_ADDRLEN || mvi->version > 3)
			break;

	case MV_READ_ADDRLEN:
		mvapich_debug2 ("rank %d: read addrlen.", mvi->rank);

		rc = mvapich_read_item (mvi, &mvi->addrlen, sizeof (mvi->addrlen));

		if (mvi->state != MV_READ_ADDRS)
			break;

	case MV_READ_ADDRS:
		mvapich_debug2 ("rank %d: read addr. addrlen = %d",
				mvi->rank, mvi->addrlen);

		mvi->addr = xmalloc (mvi->addrlen);
		rc = mvapich_read_item (mvi, mvi->addr, mvi->addrlen);

		if (mvi->state != MV_READ_PIDLEN || !mvapich_requires_pids (st))
			break;

	case MV_READ_PIDLEN:
		mvapich_debug2 ("rank %d: read pidlen", mvi->rank);

		rc = mvapich_read_item (mvi, &mvi->pidlen, sizeof (int));

		if (mvi->state != MV_READ_PID)
			break;

	case MV_READ_PID:
		mvapich_debug2 ("rank %d: read pid: pidlen = %d",
				mvi->rank, mvi->pidlen);

		mvi->pid = xmalloc (mvi->pidlen);

		rc = mvapich_read_item (mvi, mvi->pid, mvi->pidlen);

		break;

	case MV_INIT_DONE:
		break;
	}

	/*
	 *  If protocol doesn't read PIDs, we're done after ADDRs
	 */
	if (mvi->state == MV_READ_PIDLEN && !mvapich_requires_pids (st))
		mvi->state = MV_INIT_DONE;

	/*
	 *  Protocol version 4,5,6: Done after reading HOSTID
	 */
	if (mvi->state == MV_READ_ADDRLEN && mvi->version >= 5)
		mvi->state = MV_INIT_DONE;

	/*
	 *  Protocol version 8: Done after reading RANK
	 */
	if (mvi->state == MV_READ_HOSTIDLEN && mvi->version == 8)
		mvi->state = MV_INIT_DONE;

	return (rc);
}


/*
 *  Accept as many new connections as possible and place them on
 *   the next available slot in the mvarray.
 */
static int mvapich_accept_new (mvapich_state_t *st)
{
	slurm_addr addr;
	int fd;

	/*
	 *  Accept as many new connections as possible
	 */
	while (1) {
		if ( ((fd = slurm_accept_msg_conn (st->fd, &addr)) < 0)
		   && errno == EAGAIN) {
			mvapich_debug2 ("mvapich: accept new: %m");
			return (0);
		}
		else if (fd < 0) {
			error ("mvapich: accept: %m");
			return (-1);
		}

		if (st->nconnected == 0 && st->protocol_phase == 0) {
			mvapich_debug ("first task connected");
			do_timings (st, NULL);
			/*
			 *  Officially start timeout timer now.
			 */
			st->start_time = time (NULL);
		}

		fd_set_nonblocking (fd);

		st->mvarray[st->nconnected] = mvapich_info_create ();
		st->mvarray[st->nconnected]->fd = fd;
		st->nconnected++;

		mvapich_debug3 ("Got connection %d: fd=%d\n", st->nconnected, fd);
	}

	return (0);
}

/*
 *  Accept new connections on st->fd and process them with the
 *   function [fn].  The poll loop preferentially handles incoming
 *   connections to avoid exceeding the socket listen queue, which can
 *   be quite likely when launching very large jobs.
 *
 *  When there are no connections waiting, and existing connections register
 *   read activity, these connections are processed using [fn], until
 *   such time as the mvapich_info state == MV_INIT_DONE.
 *
 *  Returns 0  after all successful connections made
 *         -1  on an error
 *  Exits if st->shutdown_pipe is written to
 */
static int
mvapich_initialize_connections (mvapich_state_t *st,
		int (fn) (mvapich_state_t *, struct mvapich_info *) )
{
	int i, j;
	int nfds;
	int ncompleted;
	int rc = 0;
	int printonce = 0;
	struct mvapich_info **mvmap;
	struct pollfd *fds;

	fds = xmalloc ((st->nprocs+2) * sizeof (struct pollfd));
	mvmap = xmalloc (st->nprocs * sizeof (struct mvapich_info *));
	st->nconnected = 0;

	while (1) {

		memset (fds, 0, sizeof (struct pollfd) * (st->nprocs + 2));
		memset (mvmap, 0, sizeof (struct mvapich_info *) * st->nprocs);

		/*
		 *  Listen socket
		 */
		fds[0].fd = st->fd;
		fds[0].events = POLLIN;

		/*
		 *  Shutdown pipe
		 */
		fds[1].fd = st->shutdown_pipe[0];
		fds[1].events = POLLIN;

		j = 2;
		nfds = 2;
		ncompleted = 0;

		if (st->nconnected < st->nprocs)
			mvapich_debug2 ("Waiting for connection %d/%d\n",
					st->nconnected + 1, st->nprocs);

		for (i = 0; i < st->nconnected; i++) {
			struct mvapich_info *m = st->mvarray[i];

			if (m->fd >= 0 && m->state < MV_INIT_DONE) {
				mvmap[j-2] = m;
				fds[j].fd = m->fd;
				fds[j].events = POLLIN;
				j++;
				nfds++;
			}
			else if (m->fd > 0 && m->state == MV_INIT_DONE)
				ncompleted++;
		}

		if (st->nconnected == st->nprocs && !printonce) {
			mvapich_debug ("Got %d connections.\n", st->nprocs);
			do_timings (st, "Accept %d connection%s%s",
					st->nprocs, st->nprocs == 1 ? "" : "s",
					st->protocol_phase ? " (phase 2)" : "");
			printonce = 1;
		}

		if (ncompleted == st->nprocs) {
			do_timings (st, "Read info for %d task%s%s",
					st->nprocs, st->nprocs == 1 ? "" : "s",
					st->protocol_phase ? " (phase 2)" : "");
			break; /* All done. */
		}

		mvapich_debug3 ("do_poll (nfds=%d)\n", nfds);

		while ((rc = poll (fds, nfds, startup_timeout (st))) < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			error ("mvapich: poll: %m");
			break;
		}
		if (rc == 0) {
			report_absent_tasks (st, 1);
			mvapich_terminate_job (st, NULL);
		}

		mvapich_debug3 ("poll (nfds=%d) = %d\n", nfds, rc);

		/*
		 *  Stop other work if told to shut down
		 */
		if (fds[1].revents == POLLIN) {
			xfree (fds);
			xfree (mvmap);
			mvapich_thr_exit(st);
		}

		/*
		 *  Preferentially accept new connections.
		 */
		if (fds[0].revents == POLLIN) {
			if ((rc = mvapich_accept_new (st)) < 0)
				break;
			continue;
		}

		/*
		 *  If there are no pending connections, handle read
		 *   activity with passed in function [fn].
		 */
		for (i = 0; i < st->nconnected; i++) {
			if (fds[i+2].revents == POLLIN) {
				if ((rc = (*fn) (st, mvmap[i])) < 0)
					goto out;
			}
		}
	}

  out:
	xfree (fds);
	xfree (mvmap);
	return (rc);
}


static int mvapich_phase_two (mvapich_state_t *st)
{
	struct mvapich_poll *mp;
	struct mvapich_info *mvi;
	int i;

	/*
	 *  For phase 2, start reading addrlen for all tasks:
	 */
	for (i = 0; i < st->nprocs; i++)
		st->mvarray[i]->state = MV_READ_ADDRLEN;

	mvapich_debug ("Reading addrs from all tasks");
	mp = mvapich_poll_create (st);
	while ((mvi = mvapich_poll_next (mp, 1))) {

		mvapich_info_process_init (st, mvi);

		if (mvi->state == MV_INIT_DONE)
			mvi->do_poll = 0;
	}
	mvapich_poll_destroy (mp);

	do_timings (st, "Reading addrs from %d tasks", st->nprocs);

	mvapich_bcast_addrs (st);

	do_timings (st, "Bcast addrs to %d tasks", st->nprocs);

	return (0);
}

static int read_phase2_header (mvapich_state_t *st, struct mvapich_info *mvi)
{
	int rc;

	/*
	 *  Phase 2 header is just our rank, so we know who the
	 *   new connection is coming from.
	 */
	if ((rc = mvapich_read (mvi, &mvi->rank, sizeof (mvi->rank))) < 0)
		error ("mvapich_read: %m");
	/*
	 *  mvapich_read resets do_poll if we're done reading.
	 *   Use this to set our state to MV_INIT_DONE so we don't continue
	 *   to poll on this fd.
	 */
	if (mvi->do_poll == 0)
		mvi->state = MV_INIT_DONE;

	return (rc);
}

static int mvapich_handle_phase_two (mvapich_state_t *st)
{
	mvapich_debug ("protocol phase 0 complete. beginning phase 2.\n");

	st->protocol_phase = 1;

	/*
	 *  Phase 2 is either in "connect_once" mode, where we reuse
	 *   the existing connection (easy), or we have to handle the
	 *   remote tasks reconnecting and re-sending their ranks
	 *   before restarting the protocol. Since we don't know which
	 *   connection is from which rank, we have to use a temporary
	 *   mvapich_info array until all ranks have been read.
	 */
	if (!st->connect_once)  {
		struct mvapich_info **mvarray = st->mvarray;
		int i;

		mvapich_debug ("Waiting for %d ranks to reconnect", st->nprocs);

		/*
		 *  Create temporary mvarray to handle incoming connections
		 */
		st->mvarray = xmalloc (st->nprocs * sizeof (struct mvapich_info *));

		/*
		 *  Accept all incoming connections and read the header (rank).
		 */
		if (mvapich_initialize_connections (st, read_phase2_header) < 0)
			mvapich_terminate_job (st, "Failed to initialize phase 2");

		do_timings (st, "Phase 2 reconnect");

		/*
		 *  Now reassign mvi->fds in the real mvarray, and copy
		 *   this back to st->mvarray.
		 */
		for (i = 0; i < st->nprocs; i++) {
			struct mvapich_info *mvi = st->mvarray[i];
			mvarray[mvi->rank]->fd = mvi->fd;
		}

		xfree (st->mvarray);
		st->mvarray = mvarray;
	}

	/*
	 *  Finish processing phase two.
	 */
	mvapich_phase_two (st);

	return (0);
}

/*
 *  Intialize all NPROCS connections
 */
static void mvapich_connection_init (mvapich_state_t *st)
{
	struct mvapich_info **mva;
	int i;

	st->mvarray = xmalloc (st->nprocs * sizeof (*(st->mvarray)));

	/*
	 *  Get initial connections and read task header information:
	 */
	if (mvapich_initialize_connections (st, mvapich_info_process_init) < 0)
		goto fail;

	/*
	 *  Sort mvarray in rank order. The rest of the startup code
	 *   expects this.
	 */
	mva = xmalloc (st->nprocs * sizeof (*mva));
	for (i = 0; i < st->nprocs; i++) {
		if ((mva[i] = mvapich_info_find (st, i)) == NULL) {
			error ("mvapich: failed to find rank %d!", i);
			goto fail;
		}
	}
	xfree (st->mvarray);
	st->mvarray = mva;

	return;

fail:
	mvapich_terminate_job (st, "Fatal error. Killing job");
	return;
}

/*
 *  Close all fds in mvarray
 */
static void mvapich_close_fds (mvapich_state_t *st)
{
	int i;
	for (i = 0; i < st->nprocs; i++) {
		struct mvapich_info *mvi = st->mvarray[i];
		close (mvi->fd);
		mvi->fd = -1;
	}
}

/*
 *  This separate mvapich thread handles the MVAPICH startup
 *   protocol (tries to handle the many versions of it...).
 */
static void *mvapich_thr(void *arg)
{
	mvapich_state_t *st = arg;

	/*
	 *  Accept and initialize all remote task connections:
	 */
	mvapich_connection_init (st);

	/*
	 *  Process subsequent phases of various protocol versions.
	 */
	if (st->protocol_version == 8) {
		if (mvapich_processops (st) < 0)
			mvapich_terminate_job (st, "mvapich_processops failed.");
	}
	else {
		mvapich_debug ("bcasting mvapich info to %d tasks", st->nprocs);
		mvapich_bcast (st);
		do_timings (st,"Bcasting mvapich info to %d tasks", st->nprocs);

		if (mvapich_dual_phase (st) && st->protocol_phase == 0) {
			if (mvapich_handle_phase_two (st) < 0)
				mvapich_terminate_job (st, "Phase 2 failed.");
		}

		do_timings (st, "Phase 2");

		mvapich_debug ("calling mvapich_barrier");
		mvapich_barrier (st);
		mvapich_debug ("all tasks have checked in");
		mvapich_close_fds (st);
	}

	do_timings (st, "MVAPICH initialization");
	mvapich_wait_for_abort (st);
	return (NULL);
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
	state->shutdown_timeout = 5;

	if (pipe(state->shutdown_pipe) < 0) {
		error ("mvapich_state_create: pipe: %m");
		xfree(state);
		return (NULL);
	}
	fd_set_nonblocking(state->shutdown_pipe[0]);
	fd_set_nonblocking(state->shutdown_pipe[1]);
	state->shutdown_complete = false;

	slurm_mutex_init(&state->shutdown_lock);
	pthread_cond_init(&state->shutdown_cond, NULL);

	*(state->job) = *job;

	return state;
}

static void mvapich_state_destroy(mvapich_state_t *st)
{
	mvapich_mvarray_destroy(st);

	close(st->shutdown_pipe[0]);
	close(st->shutdown_pipe[1]);

	slurm_mutex_destroy(&st->shutdown_lock);
	pthread_cond_destroy(&st->shutdown_cond);

	xfree(st);
}

/*
 *  Create a unique MPIRUN_ID for jobid/stepid pairs.
 *   Combine the least significant bits of the jobid and stepid
 *
 *  The MPIRUN_ID is used by MVAPICH to create shmem files in /tmp,
 *   so we have to make sure multiple jobs and job steps on the
 *   same node have different MPIRUN_IDs.
 */
int mpirun_id_create(const mpi_plugin_client_info_t *job)
{
	return (int) ((job->jobid << 16) | (job->stepid & 0xffff));
}

/*
 * Returns the port number in host byte order.
 */
static short _sock_bind_wild(int sockfd)
{
	socklen_t len;
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);    /* bind ephemeral port */

	if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
		return (-1);
	len = sizeof(sin);
	if (getsockname(sockfd, (struct sockaddr *) &sin, &len) < 0)
		return (-1);
	return ntohs(sin.sin_port);
}


int do_listen (int *fd, short *port)
{
	int rc, val;

	if ((*fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return -1;

	val = 1;
	rc = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (rc > 0)
		goto cleanup;

	*port = _sock_bind_wild(*fd);
	rc = listen(*fd, 2048);

	if (rc < 0)
		goto cleanup;

	return 1;

cleanup:
	close(*fd);
	return -1;

}

extern mvapich_state_t *mvapich_thr_create(const mpi_plugin_client_info_t *job,
					   char ***env)
{
	short port;
	pthread_attr_t attr;
	mvapich_state_t *st = NULL;

	st = mvapich_state_create(job);
	if (!st) {
		error ("mvapich: Failed initialization\n");
		return NULL;
	}
	if (process_environment (st) < 0) {
		error ("mvapich: Failed to read environment settings\n");
		mvapich_state_destroy(st);
		return NULL;
	}
	if (do_listen (&st->fd, &port) < 0) {
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

	verbose ("mvapich-0.9.x,1.0.x master listening on port %hu", port);

	return st;
}

/*
 * The main thread calls this function to terminate the mpi thread and clean
 * up.  A write to this pipe will break the mpi thread out of one of two poll
 * calls--the wait for mpi abort messages and the wait for initial connections.
 * The mpi thread will spend most of its time in the first location if this
 * is an mpi job, and the second location if this is not an mpi job.  When the
 * mpi thread sees activity on this pipe, it will set st->shutdown_complete =
 * true and then pthread_exit().  If the mpi thread is not blocked on either of
 * those polls, and does not reach either poll within st->shutdown_timeout
 * secs, the main thread returns.  The main thread could call pthread_cancel
 * if it can't shutdown nicely, but there's a danger the thread could be
 * cancelled while it has a mutex locked, especially while logging.
 */
extern int mvapich_thr_destroy(mvapich_state_t *st)
{
	if (st != NULL) {
		if (st->tid != (pthread_t)-1) {
			char tmp = 1;
			int n;

			n = write(st->shutdown_pipe[1], &tmp, 1);
			if (n == 1) {
				struct timespec ts = {0, 0};

				slurm_mutex_lock(&st->shutdown_lock);
				ts.tv_sec = time(NULL) + st->shutdown_timeout;

				while (!st->shutdown_complete) {
					if (time(NULL) >= ts.tv_sec) {
						break;
					}
					pthread_cond_timedwait(
						&st->shutdown_cond,
						&st->shutdown_lock, &ts);
				}
				slurm_mutex_unlock(&st->shutdown_lock);
			}
		}
		if (st->shutdown_complete) {
			mvapich_state_destroy(st);
		}
	}
	return SLURM_SUCCESS;
}


void mvapich_thr_exit(mvapich_state_t *st)
{
	pthread_mutex_lock(&st->shutdown_lock);

	st->shutdown_complete = true;

	pthread_cond_signal(&st->shutdown_cond);
	pthread_mutex_unlock(&st->shutdown_lock);

	pthread_exit(NULL);
}



