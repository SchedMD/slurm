 /*****************************************************************************\
 *  jobacct_gather_aix.c - slurm job accounting gather plugin for AIX.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>, who borrowed heavily
 *  from other parts of SLURM, and Danny Auble, <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_acct_gather_infiniband.h"
#include "../common/common_jag.h"

#ifdef HAVE_AIX
#include <procinfo.h>
#include <sys/types.h>
#define NPROCS 5000
#endif

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job accounting API
 * matures.
 */
const char plugin_name[] = "Job accounting gather AIX plugin";
const char plugin_type[] = "jobacct_gather/aix";
const uint32_t plugin_version = 200;

/* Other useful declarations */
static int pagesize = 0;

static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmstepd");
	}

	return run;
}

#ifdef HAVE_AIX

/* Finally, pre-define all the routines. */

static void _get_offspring_data(List prec_list, prec_t *ancestor, pid_t pid);

/* system call to get process table */
extern int getprocs(struct procsinfo *procinfo, int, struct fdsinfo *,
		    int, pid_t *, int);
    /* procinfo:   pointer to array of procinfo struct */
    /* nproc:      number of user procinfo struct */
    /* sizproc:    size of expected procinfo structure */


/*
 * _get_offspring_data() -- collect memory usage data for the offspring
 *
 * For each process that lists <pid> as its parent, add its memory
 * usage data to the ancestor's <prec> record. Recurse to gather data
 * for *all* subsequent generations.
 *
 * IN:	prec_list       list of prec's
 * 	ancestor	The entry in prec_list to which the data
 * 			should be added. Even as we recurse, this will
 * 			always be the prec for the base of the family
 * 			tree.
 * 	pid		The process for which we are currently looking
 * 			for offspring.
 *
 * OUT:	none.
 *
 * RETVAL:	none.
 *
 * THREADSAFE! Only one thread ever gets here.
 */
static void _get_offspring_data(List prec_list, jag_prec_t *ancestor, pid_t pid)
{
	ListIterator itr;
	jag_prec_t *prec = NULL;

	itr = list_iterator_create(prec_list);
	while((prec = list_next(itr))) {
		if (prec->ppid == pid) {
			_get_offspring_data(prec_list, ancestor, prec->pid);
			debug2("adding %d to %d rss = %f vsize = %f",
			      prec->pid, ancestor->pid,
			      prec->rss, prec->vsize);
			ancestor->usec += prec->usec;
			ancestor->ssec += prec->ssec;
			ancestor->pages += prec->pages;
			ancestor->rss += prec->rss;
			ancestor->vsize += prec->vsize;
		}
	}
	list_iterator_destroy(itr);

	return;
}

static List _get_precs(List task_list, bool pgid_plugin, uint64_t cont_id,
		       jag_callbacks_t *callbacks)
{
	jag_prec_t *prec = NULL;
	int pid = 0;

	if (!pgid_plugin) {
		pid_t *pids = NULL;
		int npids = 0;
		/* get only the processes in the proctrack container */
		proctrack_g_get_pids(cont_id, &pids, &npids);
		if (!npids) {
			debug4("no pids in this container %"PRIu64"", cont_id);
			goto finished;
		}
		for (i = 0; i < npids; i++) {
			pid = pids[i];
			if (!getprocs(&proc, sizeof(proc), 0, 0, &pid, 1))
				continue; /* Assume the process went away */
			prec = xmalloc(sizeof(prec_t));
			list_append(prec_list, prec);
			prec->pid = proc.pi_pid;
			prec->ppid = proc.pi_ppid;
			prec->usec = proc.pi_ru.ru_utime.tv_sec +
				proc.pi_ru.ru_utime.tv_usec * 1e-6;
			prec->ssec = proc.pi_ru.ru_stime.tv_sec +
				proc.pi_ru.ru_stime.tv_usec * 1e-6;
			prec->pages = proc.pi_majflt;
			prec->rss = (proc.pi_trss + proc.pi_drss) * pagesize;
			//prec->rss *= 1024;
			prec->vsize = (proc.pi_tsize / 1024);
			prec->vsize += (proc.pi_dvm * pagesize);
			//prec->vsize *= 1024;
			/*  debug("vsize = %f = (%d/1024)+(%d*%d)",   */
/*    		      prec->vsize, proc.pi_tsize, proc.pi_dvm, pagesize);  */
		}
	} else {
		while (getprocs(&proc, sizeof(proc), 0, 0, &pid, 1) == 1) {
			prec = xmalloc(sizeof(prec_t));
			list_append(prec_list, prec);
			prec->pid = proc.pi_pid;
			prec->ppid = proc.pi_ppid;
			prec->usec = proc.pi_ru.ru_utime.tv_sec +
				proc.pi_ru.ru_utime.tv_usec * 1e-6;
			prec->ssec = proc.pi_ru.ru_stime.tv_sec +
				proc.pi_ru.ru_stime.tv_usec * 1e-6;
			prec->pages = proc.pi_majflt;
			prec->rss = (proc.pi_trss + proc.pi_drss) * pagesize;
			//prec->rss *= 1024;
			prec->vsize = (proc.pi_tsize / 1024);
			prec->vsize += (proc.pi_dvm * pagesize);
			//prec->vsize *= 1024;
			/*  debug("vsize = %f = (%d/1024)+(%d*%d)",   */
/*    		      prec->vsize, proc.pi_tsize, proc.pi_dvm, pagesize);  */
		}
	}
}

/*
 * jobacct_gather_p_poll_data() - Build a table of all current processes
 *
 * IN/OUT: task_list - list containing current processes.
 * IN: pgid_plugin - if we are running with the pgid plugin.
 * IN: cont_id - container id of processes if not running with pgid.
 *
 * OUT:	none
 *
 * THREADSAFE! Only one thread ever gets here.  It is locked in
 * slurm_jobacct_gather.
 *
 * Assumption:
 *    Any file with a name of the form "/proc/[0-9]+/stat"
 *    is a Linux-style stat entry. We disregard the data if they look
 *    wrong.
 */
extern void jobacct_gather_p_poll_data(
	List task_list, bool pgid_plugin, uint64_t cont_id)
{
	static jag_callbacks_t callbacks;
	static bool first = 1;

	if (first) {
		memset(&callbacks, 0, sizeof(jag_callbacks_t));
		first = 0;
		callbacks.get_precs = _get_precs;
		callbacks.get_offspring_data = _get_offspring_data;
	}

	jag_common_poll_data(task_list, pgid_plugin, cont_id, &callbacks);
	return;
}

#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	if (_run_in_daemon()) {
		jag_common_init(1);
		pagesize = getpagesize()/1024;
	}

	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	if (_run_in_daemon()) {
		/* just to make sure it closes things up since we call it
		 * from here */
		acct_gather_energy_fini();
	}

	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_endpoll ( void )
{
	return SLURM_SUCCESS;
}


extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}
