/*****************************************************************************\
 *  jobacct_gather_linux.c - slurm job accounting gather plugin for linux.
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>, who borrowed heavily
 *  from other parts of SLURM, and Danny Auble, <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include "src/common/slurm_xlator.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/slurmd/common/proctrack.h"

#define _DEBUG 0

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
const char plugin_name[] = "Job accounting gather LINUX plugin";
const char plugin_type[] = "jobacct_gather/linux";
const uint32_t plugin_version = 200;

/* Other useful declarations */

typedef struct prec {	/* process record */
	pid_t	pid;
	pid_t	ppid;
	int     usec;   /* user cpu time */
	int     ssec;   /* system cpu time */
	int     pages;  /* pages */
	int	rss;	/* rss */
	int	vsize;	/* virtual size */
	int	act_cpufreq;	/* actual average cpu frequency */
	int	last_cpu;	/* last cpu */
} prec_t;

static int pagesize = 0;
static DIR  *slash_proc = NULL;
static pthread_mutex_t reading_mutex = PTHREAD_MUTEX_INITIALIZER;
static int cpunfo_frequency = 0;

/* Finally, pre-define all local routines. */

static void _destroy_prec(void *object);
static int  _is_a_lwp(uint32_t pid);
static void _get_offspring_data(List prec_list, prec_t *ancestor, pid_t pid);
static int  _get_process_data_line(int in, prec_t *prec);
static int _get_sys_interface_freq_line(uint32_t cpu, char *filename,
					char *sbuf );
static uint32_t _update_weighted_freq(struct jobacctinfo *jobacct,
				      char * sbuf);

/*
 * _get_offspring_data() -- collect memory usage data for the offspring
 *
 * For each process that lists <pid> as its parent, add its memory
 * usage data to the ancestor's <prec> record. Recurse to gather data
 * for *all* subsequent generations.
 *
 * IN:	prec_list       list of prec's
 *      ancestor	The entry in precTable[] to which the data
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
_get_offspring_data(List prec_list, prec_t *ancestor, pid_t pid)
{
	ListIterator itr;
	prec_t *prec = NULL;

	itr = list_iterator_create(prec_list);
	while((prec = list_next(itr))) {
		if (prec->ppid == pid) {
#if _DEBUG
			info("pid:%u ppid:%u rss:%d KB",
			     prec->pid, prec->ppid, prec->rss);
#endif
			_get_offspring_data(prec_list, ancestor, prec->pid);
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

/* return weighted frequency in mhz */
static uint32_t _update_weighted_freq(struct jobacctinfo *jobacct,
				      char * sbuf)
{
	int thisfreq = 0;

	if (cpunfo_frequency)
		/* scaling not enabled */
		thisfreq = cpunfo_frequency;
	else
		sscanf(sbuf, "%d", &thisfreq);

	jobacct->current_weighted_freq =
			jobacct->current_weighted_freq +
			jobacct->this_sampled_cputime * thisfreq;
	if (jobacct->last_total_cputime) {
		return (jobacct->current_weighted_freq /
			jobacct->last_total_cputime);
	} else 
		return thisfreq;
}

static char * skipdot (char *str)
{
	int pntr = 0;
	while (str[pntr]) {
		if (str[pntr] == '.') {
			str[pntr] = '0';
			break;
		}
		pntr++;
	}
	str[pntr+3] = '\0';
	return str;
}

static int _get_sys_interface_freq_line(uint32_t cpu, char *filename,
					char * sbuf )
{
	int num_read, fd;
	FILE *sys_fp = NULL;
	char freq_file[80];
	char cpunfo_line [128];
	char cpufreq_line [10];

	if (cpunfo_frequency)
		/* scaling not enabled, static freq obtained */
		return 1;

	snprintf(freq_file, 79,
		 "/sys/devices/system/cpu/cpu%d/cpufreq/%s",
		 cpu, filename);
	debug2("_get_sys_interface_freq_line: "
			"filename = %s ",
			freq_file);
	if ((sys_fp = fopen(freq_file, "r"))!= NULL) {
		/* frequency scaling enabled */
		fd = fileno(sys_fp);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		num_read = read(fd, sbuf, (sizeof(sbuf) - 1));
		if (num_read > 0) {
			sbuf[num_read] = '\0';
			debug2(" cpu %d freq= %s", cpu, sbuf);
		}
		fclose(sys_fp);
	} else {
		/* frequency scaling not enabled */
		if (!cpunfo_frequency){
			snprintf(freq_file, 14,
					"/proc/cpuinfo");
			debug2("_get_sys_interface_freq_line: "
				"filename = %s ",
				freq_file);
			if ((sys_fp = fopen(freq_file, "r")) != NULL) {
				while (fgets(cpunfo_line, sizeof cpunfo_line,
					sys_fp ) != NULL) {
					if (strstr(cpunfo_line, "cpu MHz") ||
					    strstr(cpunfo_line, "cpu GHz")) {
						break;
					}
				}
				strncpy(cpufreq_line, cpunfo_line+11, 8);
				skipdot(cpufreq_line);
				sscanf(cpufreq_line, "%d", &cpunfo_frequency);
				debug2("cpunfo_frequency= %d",cpunfo_frequency);
				fclose(sys_fp);
			}
		}
		return 1;
	}
	return 0;

}

static int _is_a_lwp(uint32_t pid) {

	FILE		*status_fp = NULL;
	char		proc_status_file[256];
	uint32_t        tgid;
	int             rc;

	if ( snprintf(proc_status_file, 256,
		      "/proc/%d/status",pid) > 256 ) {
		debug("jobacct_gather_linux: unable to build proc_status "
		      "fpath");
		return -1;
	}
	if ((status_fp = fopen(proc_status_file, "r"))==NULL) {
		debug3("jobacct_gather_linux: unable to open %s",
		       proc_status_file);
		return -1;
	}


	do {
		rc = fscanf(status_fp,
			    "Name:\t%*s\n%*[ \ta-zA-Z0-9:()]\nTgid:\t%d\n",
			    &tgid);
	} while ( rc < 0 && errno == EINTR );
	fclose(status_fp);

	/* unable to read /proc/[pid]/status content */
	if ( rc != 1 ) {
		debug3("jobacct_gather_linux: unable to read requested "
		       "pattern in %s",proc_status_file);
		return -1;
	}

	/* if tgid differs from pid, this is a LWP (Thread POSIX) */
	if ( (uint32_t) tgid != (uint32_t) pid ) {
		debug3("jobacct_gather_linux: pid=%d is a lightweight process",
		       tgid);
		return 1;
	} else
		return 0;

}

/* _get_process_data_line() - get line of data from /proc/<pid>/stat
 *
 * IN:	in - input file descriptor
 * OUT:	prec - the destination for the data
 *
 * RETVAL:	==0 - no valid data
 * 		!=0 - data are valid
 *
 * Based upon stat2proc() from the ps command. It can handle arbitrary
 * executable file basenames for `cmd', i.e. those with embedded whitespace or 
 * embedded ')'s. Such names confuse %s (see scanf(3)), so the string is split
 * and %39c is used instead. (except for embedded ')' "(%[^)]c)" would work.
 */
static int _get_process_data_line(int in, prec_t *prec) {
	char sbuf[256], *tmp;
	int num_read, nvals;
	char cmd[40], state[1];
	int ppid, pgrp, session, tty_nr, tpgid;
	long unsigned flags, minflt, cminflt, majflt, cmajflt;
	long unsigned utime, stime, starttime, vsize;
	long int cutime, cstime, priority, nice, timeout, itrealvalue, rss;
	long unsigned f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13;
	int exit_signal, last_cpu;

	num_read = read(in, sbuf, (sizeof(sbuf) - 1));
	if (num_read <= 0)
		return 0;
	sbuf[num_read] = '\0';

	tmp = strrchr(sbuf, ')');	/* split into "PID (cmd" and "<rest>" */
	*tmp = '\0';			/* replace trailing ')' with NUL */
	/* parse these two strings separately, skipping the leading "(". */
	nvals = sscanf(sbuf, "%d (%39c", &prec->pid, cmd);
	if (nvals < 2)
		return 0;

	nvals = sscanf(tmp + 2,	 /* skip space after ')' too */
		       "%c %d %d %d %d %d "
		       "%lu %lu %lu %lu %lu "
		       "%lu %lu %ld %ld %ld %ld "
		       "%ld %ld %lu %lu %ld "
		       "%lu %lu %lu %lu %lu "
		       "%lu %lu %lu %lu %lu "
		       "%lu %lu %lu %d %d ",
		       state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
		       &flags, &minflt, &cminflt, &majflt, &cmajflt,
		       &utime, &stime, &cutime, &cstime, &priority, &nice,
		       &timeout, &itrealvalue, &starttime, &vsize, &rss,
		       &f1, &f2, &f3, &f4, &f5 ,&f6, &f7, &f8, &f9, &f10, &f11,
		       &f12, &f13, &exit_signal, &last_cpu);
	/* There are some additional fields, which we do not scan or use */
	if ((nvals < 37) || (rss < 0))
		return 0;

	/* If current pid corresponds to a Light Weight Process (Thread POSIX) */
	/* skip it, we will only account the original process (pid==tgid) */
	if (_is_a_lwp(prec->pid) > 0)
		return 0;

	/* Copy the values that slurm records into our data structure */
	prec->ppid  = ppid;
	prec->pages = majflt;
	prec->usec  = utime;
	prec->ssec  = stime;
	prec->vsize = vsize / 1024;	      /* convert from bytes to KB */
	prec->rss   = rss * getpagesize() / 1024;/* convert from pages to KB */
	prec->last_cpu = last_cpu;
	return 1;
}

static void _destroy_prec(void *object)
{
	prec_t *prec = (prec_t *)object;
	xfree(prec);
	return;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	pagesize = getpagesize()/1024;

	verbose("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	/* just to make sure it closes things up since we call it
	 * from here */
	acct_gather_energy_fini();
	return SLURM_SUCCESS;
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
	static	int	slash_proc_open = 0;

	struct	dirent *slash_proc_entry;
	char		*iptr = NULL, *optr = NULL;
	FILE		*stat_fp = NULL;
	char		proc_stat_file[256];	/* Allow ~20x extra length */
	List prec_list = NULL;
	pid_t *pids = NULL;
	int npids = 0;
	uint32_t total_job_mem = 0, total_job_vsize = 0;
	int		i, fd;
	ListIterator itr;
	ListIterator itr2;
	prec_t *prec = NULL;
	struct jobacctinfo *jobacct = NULL;
	static int processing = 0;
	long		hertz;
	char		sbuf[72];
	int energy_counted = 0;

	if (!pgid_plugin && (cont_id == (uint64_t)NO_VAL)) {
		debug("cont_id hasn't been set yet not running poll");
		return;
	}

	if(processing) {
		debug("already running, returning");
		return;
	}
	processing = 1;
	prec_list = list_create(_destroy_prec);

	hertz = sysconf(_SC_CLK_TCK);
	if (hertz < 1) {
		error ("_get_process_data: unable to get clock rate");
		hertz = 100;	/* default on many systems */
	}

	if (!pgid_plugin) {
		/* get only the processes in the proctrack container */
		slurm_container_get_pids(cont_id, &pids, &npids);
		if (!npids) {
			/* update consumed energy even if pids do not exist */
			itr = list_iterator_create(task_list);
			if ((jobacct = list_next(itr))) {
				acct_gather_energy_g_get_data(
					ENERGY_DATA_JOULES_TASK,
					&jobacct->energy);
				debug2("getjoules_task energy = %u",
				       jobacct->energy.consumed_energy);
			}
			list_iterator_destroy(itr);

			debug4("no pids in this container %"PRIu64"", cont_id);
			goto finished;
		}
		for (i = 0; i < npids; i++) {
			snprintf(proc_stat_file, 256,
				 "/proc/%d/stat", pids[i]);
			if ((stat_fp = fopen(proc_stat_file, "r"))==NULL)
				continue;  /* Assume the process went away */
			/*
			 * Close the file on exec() of user tasks.
			 *
			 * NOTE: If we fork() slurmstepd after the
			 * fopen() above and before the fcntl() below,
			 * then the user task may have this extra file
			 * open, which can cause problems for
			 * checkpoint/restart, but this should be a very rare
			 * problem in practice.
			 */
			fd = fileno(stat_fp);
			fcntl(fd, F_SETFD, FD_CLOEXEC);

			prec = xmalloc(sizeof(prec_t));
			if (_get_process_data_line(fd, prec))
				list_append(prec_list, prec);
			else
				xfree(prec);
			fclose(stat_fp);
		}
	} else {
		slurm_mutex_lock(&reading_mutex);

		if (slash_proc_open) {
			rewinddir(slash_proc);
		} else {
			slash_proc=opendir("/proc");
			if (slash_proc == NULL) {
				perror("opening /proc");
				slurm_mutex_unlock(&reading_mutex);
				goto finished;
			}
			slash_proc_open=1;
		}
		strcpy(proc_stat_file, "/proc/");

		while ((slash_proc_entry = readdir(slash_proc))) {

			/* Save a few cyles by simulating
			 * strcat(statFileName, slash_proc_entry->d_name);
			 * strcat(statFileName, "/stat");
			 * while checking for a numeric filename (which really
			 * should be a pid).
			 */
			optr = proc_stat_file + sizeof("/proc");
			iptr = slash_proc_entry->d_name;
			i = 0;
			do {
				if ((*iptr < '0') ||
				    ((*optr++ = *iptr++) > '9')) {
					i = -1;
					break;
				}
			} while (*iptr);

			if (i == -1)
				continue;
			iptr = (char*)"/stat";

			do {
				*optr++ = *iptr++;
			} while (*iptr);
			*optr = 0;

			if ((stat_fp = fopen(proc_stat_file,"r"))==NULL)
				continue;  /* Assume the process went away */
			/*
			 * Close the file on exec() of user tasks.
			 *
			 * NOTE: If we fork() slurmstepd after the
			 * fopen() above and before the fcntl() below,
			 * then the user task may have this extra file
			 * open, which can cause problems for
			 * checkpoint/restart, but this should be a very rare
			 * problem in practice.
			 */
			fd = fileno(stat_fp);
			fcntl(fd, F_SETFD, FD_CLOEXEC);

			prec = xmalloc(sizeof(prec_t));
			if (_get_process_data_line(fd, prec))
				list_append(prec_list, prec);
			else
				xfree(prec);
			fclose(stat_fp);
		}
		slurm_mutex_unlock(&reading_mutex);

	}

	if (!list_count(prec_list) || !task_list || !list_count(task_list))
		goto finished;	/* We have no business being here! */

	itr = list_iterator_create(task_list);
	while ((jobacct = list_next(itr))) {
		itr2 = list_iterator_create(prec_list);
		while ((prec = list_next(itr2))) {
			if (prec->pid == jobacct->pid) {
#if _DEBUG
				info("pid:%u ppid:%u rss:%d KB",
				     prec->pid, prec->ppid, prec->rss);
#endif
				/* find all my descendents */
				_get_offspring_data(prec_list,
						    prec, prec->pid);
				/* tally their usage */
				jobacct->max_rss = jobacct->tot_rss =
					MAX(jobacct->max_rss, prec->rss);
				total_job_mem += prec->rss;
				jobacct->max_vsize = jobacct->tot_vsize =
					MAX(jobacct->max_vsize, prec->vsize);
				total_job_vsize += prec->vsize;
				jobacct->max_pages = jobacct->tot_pages =
					MAX(jobacct->max_pages, prec->pages);
				jobacct->min_cpu = jobacct->tot_cpu =
					MAX(jobacct->min_cpu,
					    ((prec->ssec + prec->usec)/hertz));
				debug2("%d mem size %u %u time %u(%u+%u)",
				       jobacct->pid, jobacct->max_rss,
				       jobacct->max_vsize, jobacct->tot_cpu,
				       prec->usec, prec->ssec);
				/* compute frequency */
				_get_sys_interface_freq_line(prec->last_cpu,
						"cpuinfo_cur_freq", sbuf);
				jobacct->this_sampled_cputime =
					((prec->ssec + prec->usec) / hertz)
					-  jobacct->last_total_cputime;
				jobacct->last_total_cputime =
					((prec->ssec + prec->usec) / hertz);
				jobacct->act_cpufreq = (uint32_t)
					_update_weighted_freq(jobacct, sbuf);
				debug2("Task average frequency = %u",
				       jobacct->act_cpufreq);
				debug2(" pid %d mem size %u %u time %u(%u+%u)",
				       jobacct->pid, jobacct->max_rss,
				       jobacct->max_vsize, jobacct->tot_cpu,
				       prec->usec, prec->ssec);
				/* get energy consumption
  				 * only once is enough since we
 				 * report per node energy consumption */
				debug2("energycounted= %d", energy_counted);
				if (energy_counted == 0) {
					acct_gather_energy_g_get_data(
						ENERGY_DATA_JOULES_TASK,
						&jobacct->energy);
					debug2("getjoules_task energy = %u",
					       jobacct->energy.consumed_energy);
					energy_counted = 1;
				}
				break;
			}
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);

	jobacct_gather_handle_mem_limit(total_job_mem, total_job_vsize);

finished:
	list_destroy(prec_list);
	processing = 0;
	return;
}

extern int jobacct_gather_p_endpoll(void)
{
	if (slash_proc) {
		slurm_mutex_lock(&reading_mutex);
		(void) closedir(slash_proc);
		slurm_mutex_unlock(&reading_mutex);
	}

	return SLURM_SUCCESS;
}

extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}
