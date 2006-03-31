/*****************************************************************************\
 *  jobacct_linux.c - slurm job accounting plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>, who borrowed heavily
 *  from other parts of SLURM, and Danny Auble, <da@llnl.gov>
 *  UCRL-CODE-217948.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <dirent.h>

#include <slurm/slurm_errno.h>

#include "src/common/slurm_jobacct.h"
#include "src/common/xmalloc.h"


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
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] =
"Job accounting LINUX plugin for slurmctld and slurmd";
const char plugin_type[] = "jobacct/linux";
const uint32_t plugin_version = 100;

/* Other useful declarations */

typedef struct prec {	/* process record */
	pid_t	pid;
	pid_t	ppid;
	int	psize;	/* maxrss */
	int	vsize;	/* max virtual size */
} prec_t;

static bool fini = false;
static bool suspended = false;
static char *rev_stg = "$Revision$";
static int max_psize = 0, max_vsize = 0;
static List prec_list = NULL;
/* Finally, pre-define all the routines. */

static void _get_offspring_data(prec_t *ancestor, pid_t pid);
static void _get_process_data(pid_t pid);
static int _get_process_data_line(FILE *in, prec_t *prec);
static void *_watch_tasks(void *arg);
static void _destroy_prec(void *object);

/*
 * The following routine is called by the slurmd mainline
 */

/*
 * jobacct_init() is called when the plugin is loaded by
 * slurmd, before any other functions are called.  Put global
 * initialization here.
 */

int jobacct_p_init(int frequency)
{
	int rc = SLURM_SUCCESS;
	
	pthread_attr_t attr;
	pthread_t _watch_tasks_thread_id;
	
	info("jobacct LINUX plugin (%s)", rev_stg);

	/* Parse the JobAcctParameters */

	
	debug("jobacct: frequency = %d", frequency);
		
	fini = false;
	
	if (frequency == 0) {	/* don't want dynamic monitoring? */
		debug2("jobacct LINUX dynamic logging disabled");
		return rc;
	}

	/* create polling thread */
	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	
	if  (pthread_create(&_watch_tasks_thread_id, &attr,
			    &_watch_tasks, (void*)frequency)) {
		debug("jobacct failed to create _watch_tasks "
		      "thread: %m");
		frequency = 0;
	}
	else 
		debug3("jobacct LINUX dynamic logging enabled");
	pthread_attr_destroy(&attr);
	
	return rc;
}

int jobacct_p_fini(slurmd_job_t *job)
{
	fini = true;
	job->max_psize = max_psize;
	job->max_vsize = max_vsize;
	return SLURM_SUCCESS;
}

int jobacct_p_suspend()
{
	if(suspended)
		suspended = false;
	else
		suspended = true;
	return SLURM_SUCCESS;
}

/* 
 * _get_offspring_data() -- collect memory usage data for the offspring
 *
 * For each process that lists <pid> as its parent, add its memory
 * usage data to the ancestor's <prec> record. Recurse to gather data
 * for *all* subsequent generations.
 *
 * IN:	ancestor	The entry in precTable[] to which the data
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
static void
_get_offspring_data(prec_t *ancestor, pid_t pid) {
	
	ListIterator itr;
	prec_t *prec = NULL;

	itr = list_iterator_create(prec_list);
	while(prec = list_next(itr)) {
		if (prec->ppid == pid) {
			_get_offspring_data(ancestor, prec->pid);
			ancestor->psize += prec->psize;
			ancestor->vsize += prec->vsize;
		}
	}
	list_iterator_destroy(itr);
	return;
}

/*
 * _get_process_data() - Build a table of all current processes
 *
 * IN:	pid.
 *
 * OUT:	none
 *
  * THREADSAFE! Only one thread ever gets here.
 *
 * Assumption:
 *    Any file with a name of the form "/proc/[0-9]+/stat"
 *    is a Linux-style stat entry. We disregard the data if they look
 *    wrong.
 */
static void _get_process_data(pid_t pid) {
	static	DIR	*SlashProc;		/* For /proc */ 
	static	int	SlashProcOpen = 0;

	struct		dirent *SlashProcEntry;
	FILE		*statFile;
	char		*iptr, *optr;
	char		statFileName[256];	/* Allow ~20x extra length */
	
	int		i;
	long		psize, vsize;
	ListIterator itr;
	prec_t *prec = NULL;

	prec_list = list_create(_destroy_prec);

	if (SlashProcOpen) {
		rewinddir(SlashProc);
	} else {
		SlashProc=opendir("/proc");
		if (SlashProc == NULL) {
			perror("opening /proc");
			goto finished;
		}
		SlashProcOpen=1;
	}
	strcpy(statFileName, "/proc/");

	while ((SlashProcEntry=readdir(SlashProc))) {

		/* Save a few cyles by simulating
		   strcat(statFileName, SlashProcEntry->d_name);
		   strcat(statFileName, "/stat");
		   while checking for a numeric filename (which really
		   should be a pid).
		*/
		optr = statFileName+sizeof("/proc");
		iptr = SlashProcEntry->d_name;
		i = 0;
		do {
			if((*iptr < '0') 
			   || ((*optr++ = *iptr++) > '9')) {
				i = -1;
				break;
			}
		} while (*iptr);

		if(i == -1)
			continue;
		iptr = (char*)"/stat";

		do { *optr++ = *iptr++; } while (*iptr);
		*optr = 0;

		if ((statFile=fopen(statFileName,"r"))==NULL)
			continue;	/* Assume the process went away */

		prec = xmalloc(sizeof(prec_t));
		if (_get_process_data_line(statFile, prec)) 
			list_append(prec_list, prec);
		else 
			xfree(prec);
		fclose(statFile);
	}
	
	if (!list_count(prec_list))
		goto finished;	/* We have no business being here! */

	psize = 0;
	vsize = 0;

	itr = list_iterator_create(prec_list);
	while(prec = list_next(itr)) {
		if (prec->ppid == pid) {
			/* find all my descendents */
			_get_offspring_data(prec, prec->pid);
			/* tally their memory usage */
			psize += prec->psize;
			vsize += prec->vsize;
			/* Flag to let us know we found it,
			   though it is already finished */
			if (vsize==0)
				vsize=1; 
			break;
		}
	}
	list_iterator_destroy(itr);
	
	max_psize = MAX(max_psize, psize);
	max_vsize = MAX(max_vsize, vsize);
	debug2("got info for %d size now %d %d", 
	      pid, max_psize, max_vsize);	
finished:
	list_destroy(prec_list);
	return;
}

/* _get_process_data_line() - get line of data from /proc/<pid>/stat
 *
 * IN:	in - input file channel
 * OUT:	prec - the destination for the data
 *
 * RETVAL:	==0 - no valid data
 * 		!=0 - data are valid
 *
 * Note: It seems a bit wasteful to do all those atoi() and
 *       atol() conversions that are implicit in the scanf(),
 *       but they help to ensure that we really are looking at the
 *       expected type of record.
 */
static int _get_process_data_line(FILE *in, prec_t *prec) {
	/* discardable data */
	int		d;
	char		c;
	char		*s;
	long int	ld;
	unsigned long	lu;
	int max_path_len = pathconf("/", _PC_NAME_MAX);

	/* useful datum */
	int		nvals;

	s = xmalloc(max_path_len + 1);
	nvals=fscanf(in,
		     "%d %s %c %d %d "
		     "%d %d %d %lu %lu "
		     "%lu %lu %lu %lu %lu "
		     "%ld %ld %ld %ld %ld "
		     "%ld %lu %lu %ld %lu", 
		     &prec->pid, s, &c, &prec->ppid, &d,
		     &d, &d, &d, &lu, &lu,
		     &lu, &lu, &lu, &lu, &lu,
		     &ld, &ld, &ld, &ld, &ld,
		     &ld, &lu, &prec->vsize, &prec->psize, &lu );
	/* The fields in the record are
	 *	pid, command, state, ppid, pgrp,
	 *	session, tty_nr, tpgid, flags, minflt,
	 *	cminflt, majflt, cmajflt, utime, stime,
	 *	cutime, cstime, priority, nice, lit_0,
	 *	itrealvalue, starttime, vsize, rss, rlim
	 */
	xfree(s);
	if (nvals != 25)	/* Is it what we expected? */
		return 0;	/* No! */
	prec->psize *= getpagesize();	/* convert pages to bytes */
	prec->psize /= 1024;		/* now convert psize to kibibytes */
	prec->vsize /= 1024;		/* and convert vsize to kibibytes */
	return 1;
}

/* _watch_tasks() -- monitor slurm jobs and track their memory usage
 *
 * IN, OUT:	Irrelevant; this is invoked by pthread_create()
 */

static void *_watch_tasks(void *arg) {

	int frequency = (int)arg;
	pid_t pid = getpid();

	while(!fini) {	/* Do this until slurm_jobacct_task_exit() stops us */
		if(!suspended) {
			_get_process_data(pid);	/* Update the data */ 
		}
		sleep(frequency);
	} 
	return;
}

static void _destroy_prec(void *object)
{
	prec_t *prec = (prec_t *)object;
	xfree(prec);
	return;
}
