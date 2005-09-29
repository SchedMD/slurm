/*****************************************************************************\
 *  mvapich.c - srun support for MPICH-IB (MVAPICH 0.9.4 and 0.9.5)
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).  
 *
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/net.h"

#include "src/plugins/mpi/mvapich/mvapich.h"

/*
 *  Arguments passed to mvapich support thread.
 */
struct mvapich_args {
	srun_job_t *job;         /* SRUN job information                  */
	int fd;             /* fd on which to accept new connections */
};

/*
 *  Information read from each MVAPICH process
 */
struct mvapich_info
{
	int fd;             /* fd for socket connection to MPI task  */
	int version;        /* Version of mvapich startup protocol   */
	int rank;           /* This process' MPI rank                */
	int pidlen;         /* length of pid buffer                  */
	char *pid;          /* This rank's local pid (V3 only)       */
	int addrlen;        /* Length of addr array in bytes         */

	int *addr;          /* This process' address array, which for
	                     *  process rank N in an M process job 
	                     *  looks like:
	                     *
	                     *   qp0,qp1,..,lid,qpN+1,..,qpM-1, hostid
	                     *
	                     *  Where position N is this rank's lid,
	                     *  and the hostid is tacked onto the end
	                     *  of the array
	                     */
};

/*  Globals for the mvapich thread.
 */
static struct mvapich_info **mvarray = NULL;
static int  mvapich_fd       = -1;
static int  nprocs           = -1;
static int  protocol_version = -1;

static void mvapich_info_destroy (struct mvapich_info *mvi);

#define E_RET(msg, args...) \
	do { \
	  error (msg, ## args); \
	  mvapich_info_destroy (mvi); \
      return (NULL); \
	} while (0); 

/*
 *  Create an mvapich_info object by reading information from
 *   file descriptor `fd'
 */
static struct mvapich_info * mvapich_info_create (int fd)
{
	int n;
	unsigned char host[4];
	struct mvapich_info *mvi = xmalloc (sizeof (*mvi));

	mvi->fd = fd;
	mvi->addr = NULL;

	if (fd_read_n (fd, &mvi->version, sizeof (int)) < 0)
		E_RET ("mvapich: Unable to read version from task: %m");

	if (protocol_version == -1) 
		protocol_version = mvi->version;
	else if (protocol_version != mvi->version) 
		E_RET ("mvapich: version %d != %d", mvi->version, protocol_version);

	if (fd_read_n (fd, &mvi->rank, sizeof (int)) < 0)
		E_RET ("mvapich: Unable to read rank id: %m", mvi->rank);

	if (mvi->version <= 1 || mvi->version > 3)
		E_RET ("Unsupported version %d from rank %d", mvi->version, mvi->rank);

	if (fd_read_n (fd, &mvi->addrlen, sizeof (int)) < 0)
		E_RET ("mvapich: Unable to read addrlen for rank %d: %m", mvi->rank);

	mvi->addr = xmalloc (mvi->addrlen);

	if (fd_read_n (fd, mvi->addr, mvi->addrlen) < 0)
		E_RET ("mvapich: Unable to read addr info for rank %d: %m", mvi->rank);

	if (mvi->version == 3) {
		if (fd_read_n (fd, &mvi->pidlen, sizeof (int)) < 0)
			E_RET ("mvapich: Unable to read pidlen for rank %d: %m", mvi->rank);

		mvi->pid = xmalloc (mvi->pidlen);

		if (fd_read_n (fd, &mvi->pid, mvi->pidlen) < 0)
			E_RET ("mvapich: Unable to read pid for rank %d: %m", mvi->rank);
	}

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
static void mvapich_bcast (void)
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
		out_addrs[2 * nprocs + i] = m->addr[(m->addrlen/sizeof (int)) - 1];
	}

	for (i = 0; i < nprocs; i++) {
		m = mvarray[i];

		/*
		 * qp array is tailored to each process.
		 */
		for (j = 0; j < nprocs; j++)  
			out_addrs[nprocs + j] = (i == j) ? -1 : mvarray[j]->addr[i];

		fd_write_n (m->fd, out_addrs, out_addrs_len);

		/*
		 * Protocol version 3 requires pid list to be sent next
		 */
		if (protocol_version == 3) {
			for (j = 0; j < nprocs; j++)
				fd_write_n (m->fd, &mvarray[j]->pid, mvarray[j]->pidlen);
		}

	}

	xfree (out_addrs);
	return;
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
		fd_read_n (m->fd, &j, sizeof (j));
	}

	debug ("mvapich: completed barrier for all tasks");

	for (i = 0; i < nprocs; i++) {
		m = mvarray[i];
		fd_write_n (m->fd, &i, sizeof (i));
		close (m->fd);
		m->fd = -1;
	}

	return;
}

static void mvapich_wait_for_abort(srun_job_t *job)
{
	int rlen;
	char rbuf[1024];

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

		if ((rlen = fd_read_n (newfd, rbuf, sizeof (rbuf))) < 0) {
			error("MPI recv (abort-wait) returned %d", rlen);
			close(newfd);
			continue;
		}
		close(newfd);
		if (protocol_version == 3) {
			int rank = (int) (*rbuf);
			info ("mvapich: Received ABORT message from MPI Rank %d", rank);
		} else
			info ("mvapich: Received ABORT message from an MPI process.");
		fwd_signal(job, SIGKILL);
	}

	return; /* but not reached */
}



static void *mvapich_thr(void *arg)
{
	srun_job_t *job = arg;
	int i = 0;

	mvarray = xmalloc (nprocs * sizeof (*mvarray));

	debug ("mvapich-0.9.[45]/gen2: thread started: %ld", pthread_self ());

	while (i < nprocs) {
		struct mvapich_info *mvi = NULL;
		slurm_addr addr;
		int newfd = slurm_accept_msg_conn (mvapich_fd, &addr);

		if (newfd < 0) {
			fatal ("Failed to accept connection from mvapich task: %m");
			continue;
		}

		if ((mvi = mvapich_info_create (newfd)) == NULL) {
			error ("mvapich: MPI task failed to check in");
			return NULL;
		}

		if (mvarray[mvi->rank] != NULL) {
			fatal ("mvapich: MPI task checked in more than once");
			return NULL;
		}

		debug ("mvapich: rank %d checked in", mvi->rank);
		mvarray[mvi->rank] = mvi;
		i++;
	}

	mvapich_bcast ();

	mvapich_barrier ();

	mvapich_wait_for_abort (job);

	return (void *)0;
}

extern int mvapich_thr_create(srun_job_t *job)
{
	int port;
	char name[128];
	pthread_attr_t attr;
	pthread_t tid;

	nprocs = opt.nprocs;

	if (net_stream_listen(&mvapich_fd, &port) < 0)
		error ("Unable to create ib listen port: %m");

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
	setenvf (NULL, "MPIRUN_PORT",   "%d", ntohs (port));
	setenvf (NULL, "MPIRUN_NPROCS", "%d", nprocs);
	setenvf (NULL, "MPIRUN_ID",     "%d", job->jobid);

	verbose ("mvapich-0.9.[45] master listening on port %d", ntohs (port));

	return 0;
}
