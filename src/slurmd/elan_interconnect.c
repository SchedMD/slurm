/*****************************************************************************\
 *  src/slurmd/elan_interconnect.c Elan interconnect implementation
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> 
 *         and Mark Grondona <mgrondona@llnl.gov>
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <elan3/elan3.h>

#include <slurm/slurm_errno.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/qsw.h"
#include "src/common/slurm_protocol_api.h"

#include "src/slurmd/interconnect.h"
#include "src/slurmd/setenvpf.h"
#include "src/slurmd/shm.h"

#define ELANID_CONFIG_FILE "/etc/elanhosts"

static char *errstr[] = 
{ "No error", 
  "Out of memory!",
  "Parse error", 
  "Number of ElanIds specified != number of hosts",
  NULL
};

struct elan_info {
	int   elanid;
	char *hostname;
};

/*
 * Static prototypes for network error resolver creation:
 */
static struct elan_info * elan_info_create(int elanid, char *hostname);
static void  elan_info_destroy(struct elan_info *ei);
static int   parse_elanid_config(List eil, const char *path);
static int   parse_elanid_line(List eil, char *buf);
static int   set_elan_ids(List eil);
static int   load_neterr_data(void);
static void *neterr_thr(void *arg);

static int             neterr_retval = 0;
static pthread_t       neterr_tid;
static pthread_mutex_t neterr_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  neterr_cond  = PTHREAD_COND_INITIALIZER;


/*  Initialize node for use of the Elan interconnect by loading 
 *   elanid/hostname pairs then spawning the Elan network error
 *   resover thread.
 *
 *  Main thread waits for neterr thread to successfully start before
 *   continuing.
 */
int interconnect_node_init(void)
{
	int err = 0;
	pthread_attr_t attr;

	/*
	 *  Load neterr elanid/hostname values into kernel 
	 */
	if (load_neterr_data() < 0)
		return SLURM_FAILURE;

	if ((err = pthread_attr_init(&attr)))
		error("pthread_attr_init: %s", slurm_strerror(err));

	err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (err)
		error("pthread_attr_setdetachstate: %s", slurm_strerror(err));

	if ((err = pthread_create(&neterr_tid, &attr, neterr_thr, NULL)))
		return SLURM_FAILURE;

	/*
	 *  Wait for successful startup of neterr thread before
	 *   returning control to slurmd.
	 */
	slurm_mutex_lock(&neterr_mutex);
	pthread_cond_wait(&neterr_cond, &neterr_mutex);
	pthread_mutex_unlock(&neterr_mutex);

	return neterr_retval;
}

static void *neterr_thr(void *arg)
{	
	debug3("Starting Elan network error resolver thread");

	if (!elan3_init_neterr_svc(0)) {
		error("elan3_init_neterr_svc: %m");
		goto fail;
	}

	/* 
	 *  Attempt to register the neterr svc thread. If the address 
	 *   cannot be bound, then there is already a thread running, and
	 *   we should just exit with success.
	 */
	if (!elan3_register_neterr_svc()) {
		if (errno != EADDRINUSE) {
			error("elan3_register_neterr_svc: %m");
			goto fail;
		}
		info("Warning: Elan error resolver thread already running");
		return NULL;
	}

	/* 
	 *  Signal main thread that we've successfully initialized
	 */
	slurm_mutex_lock(&neterr_mutex);
	neterr_retval = 0;
	pthread_cond_signal(&neterr_cond);
	slurm_mutex_unlock(&neterr_mutex);

	/*
	 *  Run the network error resolver thread. This should
	 *   never return. If it does, there's not much we can do
	 *   about it.
	 */
	elan3_run_neterr_svc();

	return NULL;

   fail:
	slurm_mutex_lock(&neterr_mutex);
	neterr_retval = SLURM_FAILURE;
	pthread_cond_signal(&neterr_cond);
	slurm_mutex_unlock(&neterr_mutex);

	return NULL;
}

/*
 *  Parse an ElanId config file and load elanid,hostname pairs
 *   into the kernel.
 */
static int 
load_neterr_data(void)
{
	List eil = list_create((ListDelF) elan_info_destroy);

	if (!eil) return SLURM_ERROR;

	if (parse_elanid_config(eil, ELANID_CONFIG_FILE) < 0)
		goto fail;

	if (set_elan_ids(eil) < 0)
		goto fail;

	list_destroy(eil);
	return SLURM_SUCCESS;

    fail:
	list_destroy(eil);
	return SLURM_FAILURE;
}


/*
 *  Called from slurmd just before termination.
 *   We don't really need to do anything special for Elan, but
 *   we'll call pthread_cancel() on the neterr resolver thread anyhow.
 */
int 
interconnect_node_fini(void)
{
	int err = pthread_cancel(neterr_tid);
	if (err == 0) 
		return SLURM_SUCCESS;

	error("Unable to cancel neterr thread: %s", slurm_strerror(err));
	return SLURM_FAILURE;
}


static int 
_wait_and_destroy_prg(qsw_jobinfo_t qsw_job)
{
	int i = 0;
	int sleeptime = 1;

	debug("going to destory program description...");

	while((qsw_prgdestroy(qsw_job) < 0) && (errno != EEXIST_PRGDESTROY)) {
		i++;
		if (errno != ECHILD_PRGDESTROY)
			error("qsw_prgdestroy: %m");
		if (i == 1) {
			debug("sending SIGTERM to remaining tasks");
			qsw_prgsignal(qsw_job, SIGTERM);
		} else {
			debug("sending SIGKILL to remaining tasks");
			qsw_prgsignal(qsw_job, SIGKILL);
		}

		debug("going to sleep for %d seconds and try again", sleeptime);
		sleep(sleeptime*=2);
	}

	debug("destroyed program description");
	return SLURM_SUCCESS;
}


int
interconnect_preinit(slurmd_job_t *job)
{
	return SLURM_SUCCESS;
}

/* 
 * prepare node for interconnect use
 */
int 
interconnect_init(slurmd_job_t *job)
{
	char buf[4096];

	debug2("calling interconnect_init from process %ld", (long) getpid());
	debug("Elan info: %s", qsw_capability_string(job->qsw_job, buf, 4096));

	if (qsw_prog_init(job->qsw_job, job->uid) < 0) {
		/*
		 * Check for EBADF, which probably means the rms
		 *  kernel module is not loaded.
		 */
		if (errno == EBADF)
			error("Initializing interconnect: "
			      "is the rms kernel module loaded?");
		else
			error ("elan_interconnect_init: %m");

		qsw_print_jobinfo(log_fp(), job->qsw_job);

		return SLURM_ERROR;
	}
	
	return SLURM_SUCCESS; 
}

int 
interconnect_fini(slurmd_job_t *job)
{
	qsw_prog_fini(job->qsw_job); 
	return SLURM_SUCCESS;
}

int
interconnect_postfini(slurmd_job_t *job)
{
	_wait_and_destroy_prg(job->qsw_job);
	return SLURM_SUCCESS;
}

int 
interconnect_attach(slurmd_job_t *job, int procid)
{
	int nodeid, nnodes, nprocs; 

	nodeid = job->nodeid;
	nnodes = job->nnodes;
	nprocs = job->nprocs;

	debug3("nodeid=%d nnodes=%d procid=%d nprocs=%d", 
	       nodeid, nnodes, procid, nprocs);
	debug3("setting capability in process %ld", (long) getpid());
	if (qsw_setcap(job->qsw_job, procid) < 0) {
		error("qsw_setcap: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/*
 * Set environment variables needed by QSW MPICH / libelan.
 */
int interconnect_env(slurmd_job_t *job, int taskid)
{
	int cnt  = job->envc;
	int rank = job->task[taskid]->gid; 

	if (setenvpf(&job->env, &cnt, "RMS_RANK=%d",   rank       ) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "RMS_NODEID=%d", job->nodeid) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "RMS_PROCID=%d", rank       ) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "RMS_NNODES=%d", job->nnodes) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "RMS_NPROCS=%d", job->nprocs) < 0)
		return -1;

	job->envc = (uint16_t) cnt;

	return 0;
}

static int 
parse_elanid_config(List eil, const char *path)
{
	char  buf[4096];
	int   line;
	FILE *fp;

	if (!(fp = fopen(path, "r"))) {
		error("failed to open %s",  path);
		return -1;
	}

	line = 1;
	while (fgets(buf, 4096, fp)) {
		int rc;
		if ((rc = parse_elanid_line(eil, buf)) < 0) {
			error("%s: line %d: %s", path, line, errstr[-rc]);
			return -1;
		}
		line++;
	}

	if (fclose(fp) < 0)
		error("close(%s): %m", path);

	return 0;
}


/*
 *  Parse one line of elanId list appending results to list "eil"
 *
 *  Returns -1 for parse error, -2 if the number of elanids specified
 *  doesn't equal the number of hosts.
 *
 *  Returns 0 on success
 */
static int 
parse_elanid_line(List eil, char *buf)
{
	hostlist_t  el, hl;
	const char *separators = " \t\n";
	char       *elanids;
	char       *hosts;
	char       *sp, *s;
	int         rc = 0;

	/* 
	 *  Nullify any comments
	 */
	if ((s = strchr(buf, '#')))
		*s = '\0';

	if (!(elanids = strtok_r(buf, separators, &sp)))
		return 0;

	if (!(hosts = strtok_r(NULL, separators, &sp)))
		return -2;

	el = hostlist_create(NULL);
	hl = hostlist_create(NULL);

	if (!el || !hl) {
		rc = -1;
		goto done;
	}

	if (hostlist_push(el, elanids) != hostlist_push(hl, hosts)) {
		rc = -3; 
		goto done;
	}

	while ((s = hostlist_shift(el))) {
		char *eptr;
		int   elanid = (int) strtoul(s, &eptr, 10);

		if (*eptr != '\0') {
			rc = -2;
			goto done;
		}

		free(s);
		if (!(s = hostlist_shift(hl))) {
			rc = -1;
			goto done;
		}

		list_append(eil, elan_info_create(elanid, s));
	}

    done:
	hostlist_destroy(el);
	hostlist_destroy(hl);

	return rc;
}

static struct elan_info *
elan_info_create(int elanid, char *hostname)
{
	struct elan_info *ei = (struct elan_info *) malloc(sizeof(*ei));
	ei->elanid   = elanid;
	ei->hostname = hostname;
	return ei;
}

static void
elan_info_destroy(struct elan_info *ei)
{
	if (ei->hostname)
		free(ei->hostname);
	free(ei);
}

static int
set_elan_ids(List eil)
{
	struct elan_info *ei;
	ListIterator      i  = list_iterator_create(eil);

	while ((ei = list_next(i))) {
		if (elan3_load_neterr_svc(ei->elanid, ei->hostname) < 0)
			error("elan3_load_neterr_svc(%d, %s): %m", 
			      ei->elanid, ei->hostname);
	}
	list_iterator_destroy(i);
}
