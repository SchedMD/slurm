/*****************************************************************************\
 *  common_jag.c - slurm job accounting gather common plugin functions.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>, who borrowed heavily
 *  from the original code in jobacct_gather/linux
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>

#include "src/common/slurm_xlator.h"
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_acct_gather_filesystem.h"
#include "src/common/slurm_acct_gather_interconnect.h"
#include "src/common/xstring.h"
#include "src/slurmd/common/proctrack.h"

#include "common_jag.h"

/* These are defined here so when we link with something other than
 * the slurmstepd we will have these symbols defined.  They will get
 * overwritten when linking with the slurmstepd.
 */
#if defined (__APPLE__)
uint32_t g_tres_count __attribute__((weak_import));
char **assoc_mgr_tres_name_array __attribute__((weak_import));
#else
uint32_t g_tres_count;
char **assoc_mgr_tres_name_array;
#endif


static int cpunfo_frequency = 0;
static long hertz = 0;

static int my_pagesize = 0;
static DIR  *slash_proc = NULL;
static int energy_profile = ENERGY_DATA_NODE_ENERGY_UP;
static uint64_t debug_flags = 0;

static int _find_prec(void *x, void *key)
{
	jag_prec_t *prec = (jag_prec_t *) x;
	struct jobacctinfo *jobacct = (struct jobacctinfo *) key;

	if (prec->pid == jobacct->pid)
		return 1;

	return 0;
}

/* return weighted frequency in mhz */
static uint32_t _update_weighted_freq(struct jobacctinfo *jobacct,
				      char * sbuf)
{
	uint32_t tot_cpu;
	int thisfreq = 0;

	if (cpunfo_frequency)
		/* scaling not enabled */
		thisfreq = cpunfo_frequency;
	else
		sscanf(sbuf, "%d", &thisfreq);

	jobacct->current_weighted_freq =
		jobacct->current_weighted_freq +
		(uint32_t)jobacct->this_sampled_cputime * thisfreq;
	tot_cpu = (uint32_t) jobacct->tres_usage_in_tot[TRES_ARRAY_CPU];
	if (tot_cpu) {
		return (uint32_t) (jobacct->current_weighted_freq / tot_cpu);
	} else
		return thisfreq;
}

/* Parse /proc/cpuinfo file for CPU frequency.
 * Store the value in global variable cpunfo_frequency
 * RET: True if read valid CPU frequency */
inline static bool _get_freq(char *str)
{
	char *sep = NULL;
	double cpufreq_value;
	int cpu_mult;

	if (strstr(str, "MHz"))
		cpu_mult = 1;
	else if (strstr(str, "GHz"))
		cpu_mult = 1000;	/* Scale to MHz */
	else
		return false;

	sep = strchr(str, ':');
	if (!sep)
		return false;

	if (sscanf(sep + 2, "%lf", &cpufreq_value) < 1)
		return false;

	cpunfo_frequency = cpufreq_value * cpu_mult;
	debug2("cpunfo_frequency=%d", cpunfo_frequency);

	return true;
}

/*
 * collects the Pss value from /proc/<pid>/smaps
 */
static int _get_pss(char *proc_smaps_file, jag_prec_t *prec)
{
        uint64_t pss;
	uint64_t p;
        char line[128];
        FILE *fp;
	int i;

	fp = fopen(proc_smaps_file, "r");
        if (!fp) {
                return -1;
        }

	if (fcntl(fileno(fp), F_SETFD, FD_CLOEXEC) == -1)
		error("%s: fcntl(%s): %m", __func__, proc_smaps_file);
	pss = 0;

        while (fgets(line,sizeof(line),fp)) {

                if (xstrncmp(line, "Pss:", 4) != 0) {
                        continue;
                }

                for (i = 4; i < sizeof(line); i++) {

                        if (!isdigit(line[i])) {
                                continue;
                        }
                        if (sscanf(&line[i],"%"PRIu64"", &p) == 1) {
                                pss += p;
                        }
                        break;
                }
        }

	/* Check for error
	 */
	if (ferror(fp)) {
		debug("%s: ferror() indicates error on file %s, "
		      "process may have exited while reading",
		      __func__, proc_smaps_file);
		fclose(fp);
		return -1;
	}

        fclose(fp);
        /* Sanity checks */

        if (pss > 0 && prec->tres_data[TRES_ARRAY_MEM].size_read > pss) {
                prec->tres_data[TRES_ARRAY_MEM].size_read = pss;
        }

	debug3("%s: read pss %"PRIu64" for process %s",
	       __func__, pss, proc_smaps_file);

        return 0;
}

static int _get_sys_interface_freq_line(uint32_t cpu, char *filename,
					char * sbuf)
{
	int num_read, fd;
	FILE *sys_fp = NULL;
	char freq_file[80];
	char cpunfo_line [128];

	if (cpunfo_frequency)
		/* scaling not enabled, static freq obtained */
		return 1;

	snprintf(freq_file, 79,
		 "/sys/devices/system/cpu/cpu%d/cpufreq/%s",
		 cpu, filename);
	debug2("_get_sys_interface_freq_line: filename = %s ", freq_file);
	if ((sys_fp = fopen(freq_file, "r"))!= NULL) {
		/* frequency scaling enabled */
		fd = fileno(sys_fp);
		if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
			error("%s: fcntl(%s): %m", __func__, freq_file);
		num_read = read(fd, sbuf, (sizeof(sbuf) - 1));
		if (num_read > 0) {
			sbuf[num_read] = '\0';
			debug2(" cpu %d freq= %s", cpu, sbuf);
		}
		fclose(sys_fp);
	} else {
		/* frequency scaling not enabled */
		if (!cpunfo_frequency) {
			snprintf(freq_file, 14, "/proc/cpuinfo");
			debug2("_get_sys_interface_freq_line: filename = %s ",
			       freq_file);
			if ((sys_fp = fopen(freq_file, "r")) != NULL) {
				while (fgets(cpunfo_line, sizeof(cpunfo_line),
					     sys_fp) != NULL) {
					if (_get_freq(cpunfo_line))
						break;
				}
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

	if (snprintf(proc_status_file, 256, "/proc/%d/status",pid) > 256) {
		debug("jobacct_gather_linux: unable to build proc_status "
		      "fpath");
		return -1;
	}
	if (!(status_fp = fopen(proc_status_file, "r"))) {
		debug3("jobacct_gather_linux: unable to open %s",
		       proc_status_file);
		return -1;
	}


	do {
		rc = fscanf(status_fp,
			    "Name:\t%*s\n%*[ \ta-zA-Z0-9:()]\nTgid:\t%d\n",
			    &tgid);
	} while (rc < 0 && errno == EINTR);
	fclose(status_fp);

	/* unable to read /proc/[pid]/status content */
	if (rc != 1) {
		debug3("jobacct_gather_linux: unable to read requested "
		       "pattern in %s", proc_status_file);
		return -1;
	}

	/* if tgid differs from pid, this is a LWP (Thread POSIX) */
	if ((uint32_t) tgid != (uint32_t) pid) {
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
static int _get_process_data_line(int in, jag_prec_t *prec) {
	char sbuf[512], *tmp;
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

	/*
	 * split into "PID (cmd" and "<rest>" replace trailing ')' with NULL
	 */
	tmp = strrchr(sbuf, ')');
	if (!tmp)
		return 0;
	*tmp = '\0';

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

	prec->tres_data[TRES_ARRAY_PAGES].size_read = majflt;
	prec->tres_data[TRES_ARRAY_VMEM].size_read = vsize;
	prec->tres_data[TRES_ARRAY_MEM].size_read = rss * my_pagesize;

	prec->usec  = (double)utime/(double)hertz;
	prec->ssec  = (double)stime/(double)hertz;
	prec->last_cpu = last_cpu;
	return 1;
}

/* _get_process_memory_line() - get line of data from /proc/<pid>/statm
 *
 * IN:	in - input file descriptor
 * OUT:	prec - the destination for the data
 *
 * RETVAL:	==0 - no valid data
 * 		!=0 - data are valid
 *
 * The *prec will mostly be filled in. We need to simply subtract the
 * amount of shared memory used by the process (in KB) from *prec->rss
 * and return the updated struct.
 *
 */
static int _get_process_memory_line(int in, jag_prec_t *prec)
{
	char sbuf[256];
	int num_read, nvals;
	long int size, rss, share, text, lib, data, dt;

	num_read = read(in, sbuf, (sizeof(sbuf) - 1));
	if (num_read <= 0)
		return 0;
	sbuf[num_read] = '\0';

	nvals = sscanf(sbuf,
		       "%ld %ld %ld %ld %ld %ld %ld",
		       &size, &rss, &share, &text, &lib, &data, &dt);
	/* There are some additional fields, which we do not scan or use */
	if (nvals != 7)
		return 0;

	/* If shared > rss then there is a problem, give up... */
	if (share > rss) {
		debug("jobacct_gather_linux: share > rss - bail!");
		return 0;
	}

	/* Copy the values that slurm records into our data structure */
	prec->tres_data[TRES_ARRAY_MEM].size_read =
		(rss - share) * my_pagesize;;

	return 1;
}

static int _remove_share_data(char *proc_stat_file, jag_prec_t *prec)
{
	FILE *statm_fp = NULL;
	char proc_statm_file[256];	/* Allow ~20x extra length */
	int rc = 0, fd;

	snprintf(proc_statm_file, sizeof(proc_statm_file), "%sm",
		 proc_stat_file);
	if (!(statm_fp = fopen(proc_statm_file, "r")))
		return rc;  /* Assume the process went away */
	fd = fileno(statm_fp);
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		error("%s: fcntl(%s): %m", __func__, proc_statm_file);
	rc = _get_process_memory_line(fd, prec);
	fclose(statm_fp);
	return rc;
}

/* _get_process_io_data_line() - get line of data from /proc/<pid>/io
 *
 * IN:	in - input file descriptor
 * OUT:	prec - the destination for the data
 *
 * RETVAL:	==0 - no valid data
 * 		!=0 - data are valid
 *
 * /proc/<pid>/io content format is:
 * rchar: <# of characters read>
 * wrchar: <# of characters written>
 *   . . .
 */
static int _get_process_io_data_line(int in, jag_prec_t *prec) {
	char sbuf[256];
	char f1[7], f3[7];
	int num_read, nvals;
	uint64_t rchar, wchar;

	num_read = read(in, sbuf, (sizeof(sbuf) - 1));
	if (num_read <= 0)
		return 0;
	sbuf[num_read] = '\0';

	nvals = sscanf(sbuf, "%s %"PRIu64" %s %"PRIu64"",
		       f1, &rchar, f3, &wchar);
	if (nvals < 4)
		return 0;

	if (_is_a_lwp(prec->pid) > 0)
		return 0;

	/* keep real value here since we aren't doubles */
	prec->tres_data[TRES_ARRAY_FS_DISK].size_read = rchar;
	prec->tres_data[TRES_ARRAY_FS_DISK].size_write = wchar;

	return 1;
}

static void _handle_stats(List prec_list, char *proc_stat_file,
			  char *proc_io_file, char *proc_smaps_file,
			  jag_callbacks_t *callbacks,
			  int tres_count)
{
	static int no_share_data = -1;
	static int use_pss = -1;
	FILE *stat_fp = NULL;
	FILE *io_fp = NULL;
	int fd, fd2, i;
	jag_prec_t *prec = NULL;

	if (no_share_data == -1) {
		char *acct_params = slurm_get_jobacct_gather_params();
		if (acct_params && xstrcasestr(acct_params, "NoShare"))
			no_share_data = 1;
		else
			no_share_data = 0;

		if (acct_params && xstrcasestr(acct_params, "UsePss"))
			use_pss = 1;
		else
			use_pss = 0;
		xfree(acct_params);
	}

	if (!(stat_fp = fopen(proc_stat_file, "r")))
		return;  /* Assume the process went away */
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
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		error("%s: fcntl(%s): %m", __func__, proc_stat_file);

	prec = try_xmalloc(sizeof(jag_prec_t));
	if (prec == NULL) {	/* Avoid killing slurmstepd on malloc failure */
		fclose(stat_fp);
		return;
	}

	if (!tres_count) {
		assoc_mgr_lock_t locks = {
			NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
			READ_LOCK, NO_LOCK, NO_LOCK };
		assoc_mgr_lock(&locks);
		tres_count = g_tres_count;
		assoc_mgr_unlock(&locks);
	}

	prec->tres_count = tres_count;
	prec->tres_data = xmalloc(prec->tres_count *
				  sizeof(acct_gather_data_t));

	/* Initialize read/writes */
	for (i = 0; i < prec->tres_count; i++) {
		prec->tres_data[i].num_reads = INFINITE64;
		prec->tres_data[i].num_writes = INFINITE64;
		prec->tres_data[i].size_read = INFINITE64;
		prec->tres_data[i].size_write = INFINITE64;
	}

	if (!_get_process_data_line(fd, prec)) {
		xfree(prec->tres_data);
		xfree(prec);
		fclose(stat_fp);
		return;
	}
	fclose(stat_fp);

	if (acct_gather_filesystem_g_get_data(prec->tres_data) < 0) {
		debug2("problem retrieving filesystem data");
	}

	if (acct_gather_interconnect_g_get_data(prec->tres_data) < 0) {
		debug2("problem retrieving interconnect data");
	}

	/* Remove shared data from rss */
	if (no_share_data)
		_remove_share_data(proc_stat_file, prec);

	/* Use PSS instead if RSS */
	if (use_pss) {
		if (_get_pss(proc_smaps_file, prec) == -1) {
			xfree(prec->tres_data);
			xfree(prec);
			return;
		}
	}

	list_append(prec_list, prec);

	if ((io_fp = fopen(proc_io_file, "r"))) {
		fd2 = fileno(io_fp);
		if (fcntl(fd2, F_SETFD, FD_CLOEXEC) == -1)
			error("%s: fcntl: %m", __func__);
		_get_process_io_data_line(fd2, prec);
		fclose(io_fp);
	}
	if (callbacks->prec_extra)
		(*(callbacks->prec_extra))(prec);
}

static List _get_precs(List task_list, bool pgid_plugin, uint64_t cont_id,
		       jag_callbacks_t *callbacks)
{
	List prec_list = list_create(destroy_jag_prec);
	char	proc_stat_file[256];	/* Allow ~20x extra length */
	char	proc_io_file[256];	/* Allow ~20x extra length */
	char	proc_smaps_file[256];	/* Allow ~20x extra length */
	static	int	slash_proc_open = 0;
	int i;
	struct jobacctinfo *jobacct = NULL;

	xassert(task_list);

	jobacct = list_peek(task_list);

	if (!pgid_plugin) {
		pid_t *pids = NULL;
		int npids = 0;
		/* get only the processes in the proctrack container */
		proctrack_g_get_pids(cont_id, &pids, &npids);
		if (!npids) {
			/* update consumed energy even if pids do not exist */
			if (jobacct) {
				acct_gather_energy_g_get_data(
					energy_profile,
					&jobacct->energy);
				jobacct->tres_usage_in_tot[TRES_ARRAY_ENERGY] =
					jobacct->energy.consumed_energy;
				debug2("getjoules_task energy = %"PRIu64"",
				       jobacct->energy.consumed_energy);
			}

			debug4("no pids in this container %"PRIu64"", cont_id);
			goto finished;
		}
		for (i = 0; i < npids; i++) {
			snprintf(proc_stat_file, 256, "/proc/%d/stat", pids[i]);
			snprintf(proc_io_file, 256, "/proc/%d/io", pids[i]);
			snprintf(proc_smaps_file, 256, "/proc/%d/smaps", pids[i]);
			_handle_stats(prec_list, proc_stat_file, proc_io_file,
				      proc_smaps_file, callbacks,
				      jobacct ? jobacct->tres_count : 0);
		}
		xfree(pids);
	} else {
		struct dirent *slash_proc_entry;
		char  *iptr = NULL, *optr = NULL, *optr2 = NULL;

		if (slash_proc_open) {
			rewinddir(slash_proc);
		} else {
			slash_proc=opendir("/proc");
			if (slash_proc == NULL) {
				perror("opening /proc");
				goto finished;
			}
			slash_proc_open=1;
		}
		strcpy(proc_stat_file, "/proc/");
		strcpy(proc_io_file, "/proc/");
		strcpy(proc_smaps_file, "/proc/");

		while ((slash_proc_entry = readdir(slash_proc))) {

			/* Save a few cyles by simulating
			 * strcat(statFileName, slash_proc_entry->d_name);
			 * strcat(statFileName, "/stat");
			 * while checking for a numeric filename (which really
			 * should be a pid). Then do the same for the
			 * /proc/<pid>/io file name.
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

			optr2 = proc_io_file + sizeof("/proc");
			iptr = slash_proc_entry->d_name;
			i = 0;
			do {
				if ((*iptr < '0') ||
				    ((*optr2++ = *iptr++) > '9')) {
					i = -1;
					break;
				}
			} while (*iptr);
			if (i == -1)
				continue;
			iptr = (char*)"/io";

			do {
				*optr2++ = *iptr++;
			} while (*iptr);
			*optr2 = 0;

			optr2 = proc_smaps_file + sizeof("/proc");
			iptr = slash_proc_entry->d_name;
			i = 0;
			do {
				if ((*iptr < '0') ||
				    ((*optr2++ = *iptr++) > '9')) {
					i = -1;
					break;
				}
			} while (*iptr);
			if (i == -1)
				continue;
			iptr = (char*)"/smaps";

			do {
				*optr2++ = *iptr++;
			} while (*iptr);
			*optr2 = 0;

			_handle_stats(prec_list, proc_stat_file, proc_io_file,
				      proc_smaps_file,callbacks,
				      jobacct ? jobacct->tres_count : 0);
		}
	}

finished:

	return prec_list;
}

static void _record_profile(struct jobacctinfo *jobacct)
{
	enum {
		FIELD_CPUFREQ,
		FIELD_CPUTIME,
		FIELD_CPUUTIL,
		FIELD_RSS,
		FIELD_VMSIZE,
		FIELD_PAGES,
		FIELD_READ,
		FIELD_WRITE,
		FIELD_CNT
	};

	acct_gather_profile_dataset_t dataset[] = {
		{ "CPUFrequency", PROFILE_FIELD_UINT64 },
		{ "CPUTime", PROFILE_FIELD_DOUBLE },
		{ "CPUUtilization", PROFILE_FIELD_DOUBLE },
		{ "RSS", PROFILE_FIELD_UINT64 },
		{ "VMSize", PROFILE_FIELD_UINT64 },
		{ "Pages", PROFILE_FIELD_UINT64 },
		{ "ReadMB", PROFILE_FIELD_DOUBLE },
		{ "WriteMB", PROFILE_FIELD_DOUBLE },
		{ NULL, PROFILE_FIELD_NOT_SET }
	};

	static int64_t profile_gid = -1;
	double et;
	union {
		double d;
		uint64_t u64;
	} data[FIELD_CNT];

	if (profile_gid == -1)
		profile_gid = acct_gather_profile_g_create_group("Tasks");

	/* Create the dataset first */
	if (jobacct->dataset_id < 0) {
		char ds_name[32];
		snprintf(ds_name, sizeof(ds_name), "%u", jobacct->id.taskid);

		jobacct->dataset_id = acct_gather_profile_g_create_dataset(
			ds_name, profile_gid, dataset);
		if (jobacct->dataset_id == SLURM_ERROR) {
			error("JobAcct: Failed to create the dataset for "
			      "task %d",
			      jobacct->pid);
			return;
		}
	}

	if (jobacct->dataset_id < 0)
		return;

	data[FIELD_CPUFREQ].u64 = jobacct->act_cpufreq;
	/* Profile Mem and VMem as KB */
	data[FIELD_RSS].u64 =
		jobacct->tres_usage_in_tot[TRES_ARRAY_MEM] / 1024;
	data[FIELD_VMSIZE].u64 =
		jobacct->tres_usage_in_tot[TRES_ARRAY_VMEM] / 1024;
	data[FIELD_PAGES].u64 = jobacct->tres_usage_in_tot[TRES_ARRAY_PAGES];

	/* delta from last snapshot */
	if (!jobacct->last_time) {
		data[FIELD_CPUTIME].d = 0;
		data[FIELD_CPUUTIL].d = 0.0;
		data[FIELD_READ].d = 0.0;
		data[FIELD_WRITE].d = 0.0;
	} else {
		data[FIELD_CPUTIME].d =
			((double)jobacct->tres_usage_in_tot[TRES_ARRAY_CPU] -
			 jobacct->last_total_cputime) / CPU_TIME_ADJ;
		et = (jobacct->cur_time - jobacct->last_time);
		if (!et)
			data[FIELD_CPUUTIL].d = 0.0;
		else
			data[FIELD_CPUUTIL].d =
				(100.0 * (double)data[FIELD_CPUTIME].d) /
				((double) et);

		data[FIELD_READ].d = (double) jobacct->
			tres_usage_in_tot[TRES_ARRAY_FS_DISK] -
			jobacct->last_tres_usage_in_tot;
		data[FIELD_WRITE].d = (double) jobacct->
			tres_usage_out_tot[TRES_ARRAY_FS_DISK] -
			jobacct->last_tres_usage_out_tot;
		/* Profile disk as MB */
		data[FIELD_READ].d /= 1048576.0;
		data[FIELD_WRITE].d /= 1048576.0;
	}

	if (debug_flags & DEBUG_FLAG_PROFILE) {
		char str[256];
		info("PROFILE-Task: %s", acct_gather_profile_dataset_str(
			     dataset, data, str, sizeof(str)));
	}
	acct_gather_profile_g_add_sample_data(jobacct->dataset_id,
	                                      (void *)data, jobacct->cur_time);
}

extern void jag_common_init(long in_hertz)
{
	uint32_t profile_opt;

	debug_flags = slurm_get_debug_flags();

	acct_gather_profile_g_get(ACCT_GATHER_PROFILE_RUNNING,
				  &profile_opt);

	/* If we are profiling energy it will be checked at a
	   different rate, so just grab the last one.
	*/
	if (profile_opt & ACCT_GATHER_PROFILE_ENERGY)
		energy_profile = ENERGY_DATA_NODE_ENERGY;

	if (in_hertz) {
		hertz = in_hertz;
	} else {
		hertz = sysconf(_SC_CLK_TCK);

		if (hertz < 1) {
			error ("_get_process_data: unable to get clock rate");
			hertz = 100;	/* default on many systems */
		}
	}

	my_pagesize = getpagesize();
}

extern void jag_common_fini(void)
{
	if (slash_proc)
		(void) closedir(slash_proc);
}

extern void destroy_jag_prec(void *object)
{
	jag_prec_t *prec = (jag_prec_t *)object;
	xfree(prec->tres_data);
	xfree(prec);
	return;
}

extern void print_jag_prec(jag_prec_t *prec)
{
	int i;
	assoc_mgr_lock_t locks = {
		NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
		READ_LOCK, NO_LOCK, NO_LOCK };

	info("pid %d (ppid %d)", prec->pid, prec->ppid);
	info("act_cpufreq\t%d", prec->act_cpufreq);
	info("ssec \t%f", prec->ssec);
	assoc_mgr_lock(&locks);
	for (i = 0; i < prec->tres_count; i++) {
		if (prec->tres_data[i].size_read == INFINITE64)
			continue;
		info("%s in/read \t%"PRIu64"",
		     assoc_mgr_tres_name_array[i],
		     prec->tres_data[i].size_read);
		info("%s out/write \t%"PRIu64"",
		     assoc_mgr_tres_name_array[i],
		     prec->tres_data[i].size_write);
	}
	assoc_mgr_unlock(&locks);
	info("usec \t%f", prec->usec);
}

extern void jag_common_poll_data(
	List task_list, bool pgid_plugin, uint64_t cont_id,
	jag_callbacks_t *callbacks, bool profile)
{
	/* Update the data */
	List prec_list = NULL;
	uint64_t total_job_mem = 0, total_job_vsize = 0;
	ListIterator itr;
	jag_prec_t *prec = NULL;
	struct jobacctinfo *jobacct = NULL;
	static int processing = 0;
	char sbuf[72];
	int energy_counted = 0;
	time_t ct;
	static int no_over_memory_kill = -1;
	int i = 0;

	xassert(callbacks);

	if (!pgid_plugin && (cont_id == INFINITE64)) {
		debug("cont_id hasn't been set yet not running poll");
		return;
	}

	if (processing) {
		debug("already running, returning");
		return;
	}
	processing = 1;

	if (no_over_memory_kill == -1) {
		char *acct_params = slurm_get_jobacct_gather_params();
		if (acct_params && xstrcasestr(acct_params, "NoOverMemoryKill"))
			no_over_memory_kill = 1;
		else
			no_over_memory_kill = 0;
		xfree(acct_params);
	}

	if (!callbacks->get_precs)
		callbacks->get_precs = _get_precs;

	ct = time(NULL);
	prec_list = (*(callbacks->get_precs))(task_list, pgid_plugin, cont_id,
					      callbacks);

	if (!list_count(prec_list) || !task_list || !list_count(task_list))
		goto finished;	/* We have no business being here! */

	itr = list_iterator_create(task_list);
	while ((jobacct = list_next(itr))) {
		double cpu_calc;
		double last_total_cputime;
		if (!(prec = list_find_first(prec_list, _find_prec, jobacct)))
			continue;

#if _DEBUG
		info("pid:%u ppid:%u rss:%"PRIu64" B",
		     prec->pid, prec->ppid,
		     prec->tres_data[TRES_ARRAY_MEM].size_read);
#endif
		/* find all my descendents */
		if (callbacks->get_offspring_data)
			(*(callbacks->get_offspring_data))
				(prec_list, prec, prec->pid);

		last_total_cputime =
			(double)jobacct->tres_usage_in_tot[TRES_ARRAY_CPU];

		cpu_calc = prec->ssec + prec->usec;

		/*
		 * Since we are not storing things as a double anymore make it
		 * bigger so we don't loose precision.
		 */
		cpu_calc *= CPU_TIME_ADJ;

		prec->tres_data[TRES_ARRAY_CPU].size_read = (uint64_t)cpu_calc;

		/* get energy consumption
		 * only once is enough since we
		 * report per node energy consumption */
		debug2("energycounted = %d", energy_counted);
		if (energy_counted == 0) {
			acct_gather_energy_g_get_data(
				energy_profile,
				&jobacct->energy);
			prec->tres_data[TRES_ARRAY_ENERGY].size_read =
				jobacct->energy.consumed_energy;
			debug2("getjoules_task energy = %"PRIu64,
			       jobacct->energy.consumed_energy);
			energy_counted = 1;
		}

		/* tally their usage */
		for (i = 0; i < jobacct->tres_count; i++) {
			if (prec->tres_data[i].size_read == INFINITE64)
				continue;
			if (jobacct->tres_usage_in_max[i] == INFINITE64)
				jobacct->tres_usage_in_max[i] =
					prec->tres_data[i].size_read;
			else
				jobacct->tres_usage_in_max[i] =
					MAX(jobacct->tres_usage_in_max[i],
					    prec->tres_data[i].size_read);
			/*
			 * Even with min we want to get the max as we are
			 * looking at a specific task aso we are always looking
			 * at the max that task had, not the min (or lots of
			 * things will be zero).  The min is from compairing
			 * ranks later when combining.  So here it will be the
			 * same as the max value set above.
			 * (same thing goes for the out)
			 */
			jobacct->tres_usage_in_min[i] =
				jobacct->tres_usage_in_max[i];
			jobacct->tres_usage_in_tot[i] =
				prec->tres_data[i].size_read;

			if (jobacct->tres_usage_out_max[i] == INFINITE64)
				jobacct->tres_usage_out_max[i] =
					prec->tres_data[i].size_write;
			else
				jobacct->tres_usage_out_max[i] =
					MAX(jobacct->tres_usage_out_max[i],
					    prec->tres_data[i].size_write);
			jobacct->tres_usage_out_min[i] =
				jobacct->tres_usage_out_max[i];
			jobacct->tres_usage_out_tot[i] =
				prec->tres_data[i].size_write;
		}

		total_job_mem += jobacct->tres_usage_in_tot[TRES_ARRAY_MEM];
		total_job_vsize += jobacct->tres_usage_in_tot[TRES_ARRAY_VMEM];

		/* Update the cpu times */
		jobacct->user_cpu_sec = (uint32_t)prec->usec;
		jobacct->sys_cpu_sec = (uint32_t)prec->ssec;

		/* compute frequency */
		jobacct->this_sampled_cputime =
			cpu_calc - last_total_cputime;
		_get_sys_interface_freq_line(
			prec->last_cpu,
			"cpuinfo_cur_freq", sbuf);
		jobacct->act_cpufreq =
			_update_weighted_freq(jobacct, sbuf);

		debug("%s: Task %u pid %d ave_freq = %u mem size/max %"PRIu64"/%"PRIu64" vmem size/max %"PRIu64"/%"PRIu64", disk read size/max (%"PRIu64"/%"PRIu64"), disk write size/max (%"PRIu64"/%"PRIu64"), time %f(%u+%u)",
		      __func__,
		      jobacct->id.taskid,
		      jobacct->pid,
		      jobacct->act_cpufreq,
		      jobacct->tres_usage_in_tot[TRES_ARRAY_MEM],
		      jobacct->tres_usage_in_max[TRES_ARRAY_MEM],
		      jobacct->tres_usage_in_tot[TRES_ARRAY_VMEM],
		      jobacct->tres_usage_in_max[TRES_ARRAY_VMEM],
		      jobacct->tres_usage_in_tot[TRES_ARRAY_FS_DISK],
		      jobacct->tres_usage_in_max[TRES_ARRAY_FS_DISK],
		      jobacct->tres_usage_out_tot[TRES_ARRAY_FS_DISK],
		      jobacct->tres_usage_out_max[TRES_ARRAY_FS_DISK],
		      (double)(jobacct->tres_usage_in_tot[TRES_ARRAY_CPU] /
			       CPU_TIME_ADJ),
		      jobacct->user_cpu_sec,
		      jobacct->sys_cpu_sec);

		if (profile &&
		    acct_gather_profile_g_is_active(ACCT_GATHER_PROFILE_TASK)) {
			jobacct->cur_time = ct;

			_record_profile(jobacct);

			jobacct->last_tres_usage_in_tot =
				jobacct->tres_usage_in_tot[TRES_ARRAY_FS_DISK];
			jobacct->last_tres_usage_out_tot =
				jobacct->tres_usage_out_tot[TRES_ARRAY_FS_DISK];
			jobacct->last_total_cputime =
				jobacct->tres_usage_in_tot[TRES_ARRAY_CPU];

			jobacct->last_time = jobacct->cur_time;
		}
	}
	list_iterator_destroy(itr);

	if (!no_over_memory_kill)
		jobacct_gather_handle_mem_limit(total_job_mem, total_job_vsize);

finished:
	FREE_NULL_LIST(prec_list);
	processing = 0;
}
