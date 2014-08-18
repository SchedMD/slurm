/*****************************************************************************\
 *  cpu_frequency.c - support for srun option --cpu-freq=<frequency>
 *****************************************************************************
 *  Copyright (C) 2012 Bull
 *  Written by Don Albert, <don.albert@bull.com>
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
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/fd.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define PATH_TO_CPU	"/sys/devices/system/cpu/"
#define LINE_LEN	100
#define SYSFS_PATH_MAX	255
#define FREQ_LIST_MAX	32
#define GOV_NAME_LEN	24

#define GOV_CONSERVATIVE	0x01
#define GOV_ONDEMAND		0x02
#define GOV_PERFORMANCE		0x04
#define GOV_POWERSAVE		0x08
#define GOV_USERSPACE		0x10

static uint16_t cpu_freq_count = 0;
static struct cpu_freq_data {
	uint8_t  avail_governors;
	uint32_t orig_frequency;
	char     orig_governor[GOV_NAME_LEN];
	uint32_t new_frequency;
	char     new_governor[GOV_NAME_LEN];
} * cpufreq = NULL;
char *slurmd_spooldir = NULL;

static void	_cpu_freq_find_valid(uint32_t cpu_freq, int cpuidx);
static uint16_t	_cpu_freq_next_cpu(char **core_range, uint16_t *cpuidx,
				   uint16_t *start, uint16_t *end);
static int	_fd_lock_retry(int fd);

static int _fd_lock_retry(int fd)
{
	int i, rc;

	for (i = 0; i < 10; i++) {
		if (i)
			usleep(1000);	/* 1000 usec */
		rc = fd_get_write_lock(fd);
		if (rc == 0)
			break;
		if ((errno != EACCES) && (errno != EAGAIN))
			break;	/* Lock held by other job */
	}
	return rc;
}

/* This set of locks it designed to prevent race conditions when changing
 * CPU frequency or govorner. Specifically, when a job ends it should only
 * reset CPU frequency if it was the last job to set the CPU frequency.
 * with gang scheduling and cancellation of suspended or running jobs there
 * can be timing issues.
 * _set_cpu_owner_lock  - set specified job to own the CPU, this CPU file is
 *	locked on exit
 * _test_cpu_owner_lock - test if the specified job owns the CPU, this CPU is
 *	locked on return with true
 */
static int _set_cpu_owner_lock(int cpu_id, uint32_t job_id)
{
	char tmp[64];
	int fd, sz;

	if (!slurmd_spooldir)
		slurmd_spooldir = slurm_get_slurmd_spooldir();

	snprintf(tmp, sizeof(tmp), "%s/cpu", slurmd_spooldir);
	(void) mkdir(tmp, 0700);
	snprintf(tmp, sizeof(tmp), "%s/cpu/%d", slurmd_spooldir, cpu_id);
	fd = open(tmp, O_CREAT | O_RDWR, 0500);
	if (fd < 0) {
		error("%s: open: %m", __func__);
		return fd;
	}
	if (_fd_lock_retry(fd) < 0)
		error("%s: fd_get_write_lock: %m", __func__);
	sz = sizeof(uint32_t);
	if (fd_write_n(fd, (void *) &job_id, sz) != sz)
		error("%s: write: %m", __func__);

	return fd;
}

static int _test_cpu_owner_lock(int cpu_id, uint32_t job_id)
{
	char tmp[64];
	uint32_t in_job_id;
	int fd, sz;

	if (!slurmd_spooldir)
		slurmd_spooldir = slurm_get_slurmd_spooldir();

	snprintf(tmp, sizeof(tmp), "%s/cpu", slurmd_spooldir);
	(void) mkdir(tmp, 0700);
	snprintf(tmp, sizeof(tmp), "%s/cpu/%d", slurmd_spooldir, cpu_id);
	fd = open(tmp, O_RDWR);
	if (fd < 0) {
		error("%s: open: %m", __func__);
		return fd;
	}
	if (_fd_lock_retry(fd) < 0) {
		error("%s: fd_get_write_lock: %m", __func__);
		close(fd);
		return -1;
	}
	sz = sizeof(uint32_t);
	if (fd_read_n(fd, (void *) &in_job_id, sz) != sz) {
		error("%s: read: %m", __func__);
		close(fd);
		return -1;
	}
	if (job_id != in_job_id) {
		/* Result of various race conditions */
		debug("%s: CPU %d now owned by job %u rather than job %u",
		      __func__, cpu_id, in_job_id, job_id);
		close(fd);
		return -1;
	}
	debug("%s: CPU %d owned by job %u as expected",
	      __func__, cpu_id, job_id);

	return fd;
}

/*
 * called to check if the node supports setting CPU frequency
 * if so, initialize fields in cpu_freq_data structure
 */
extern void
cpu_freq_init(slurmd_conf_t *conf)
{
	char path[SYSFS_PATH_MAX];
	struct stat statbuf;
	FILE *fp;
	char value[LINE_LEN];
	unsigned int i, j;

	/* check for cpufreq support */
	if ( stat(PATH_TO_CPU "cpu0/cpufreq", &statbuf) != 0 ) {
		info("CPU frequency setting not configured for this node");
		return;
	}

	if (!S_ISDIR(statbuf.st_mode)) {
		error(PATH_TO_CPU "cpu0/cpufreq not a directory");
		return;
	}

	/* get the cpu frequency info into the cpu_freq_data structure */
	cpu_freq_count = conf->block_map_size;
	if (!cpufreq) {
		cpufreq = (struct cpu_freq_data *)
			  xmalloc(cpu_freq_count *
				  sizeof(struct cpu_freq_data));
	}

	debug2("Gathering cpu frequency information for %u cpus",
	       cpu_freq_count);
	for (i = 0; i < cpu_freq_count; i++) {
		snprintf(path, sizeof(path),
			 PATH_TO_CPU
			 "cpu%u/cpufreq/scaling_available_governors", i);
		if ((fp = fopen(path, "r")) == NULL)
			goto log_it;
		if (fgets(value, LINE_LEN, fp) == NULL) {
			fclose(fp);
			goto log_it;
		}
		if (strstr(value, "conservative"))
			cpufreq[i].avail_governors |= GOV_CONSERVATIVE;
		if (strstr(value, "ondemand"))
			cpufreq[i].avail_governors |= GOV_ONDEMAND;
		if (strstr(value, "performance"))
			cpufreq[i].avail_governors |= GOV_PERFORMANCE;
		if (strstr(value, "powersave"))
			cpufreq[i].avail_governors |= GOV_POWERSAVE;
		if (strstr(value, "userspace"))
			cpufreq[i].avail_governors |= GOV_USERSPACE;
		fclose(fp);

		if (!(cpufreq[i].avail_governors & GOV_USERSPACE))
			goto log_it;

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor", i);
		if ((fp = fopen(path, "r")) == NULL)
			goto log_it;
		if (fgets(value, LINE_LEN, fp) == NULL) {
			fclose(fp);
			goto log_it;
		}
		if (strlen(value) >= GOV_NAME_LEN) {
			fclose(fp);
			goto log_it;
		}
		strcpy(cpufreq[i].orig_governor, value);
		fclose(fp);
		j = strlen(cpufreq[i].orig_governor);
		if ((j > 0) && (cpufreq[i].orig_governor[j - 1] == '\n'))
			cpufreq[i].orig_governor[j - 1] = '\0';

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_min_freq", i);
		if ((fp = fopen(path, "r")) == NULL)
			continue;
		if (fscanf (fp, "%u", &cpufreq[i].orig_frequency) < 0) {
			error("cpu_freq_cgroup_valid: Could not read "
			      "scaling_min_freq");
		}
		fclose(fp);

log_it:		debug("cpu_freq_init: CPU:%u reset_freq:%u avail_gov:%x "
		      "orig_governor:%s",
		      i, cpufreq[i].orig_frequency, cpufreq[i].avail_governors,
		      cpufreq[i].orig_governor);
	}
	return;
}

extern void
cpu_freq_fini(void)
{
	xfree(cpufreq);
}

/*
 * Send the cpu_frequency table info to slurmstepd
 */
void
cpu_freq_send_info(int fd)
{
	if (cpu_freq_count) {
		safe_write(fd, &cpu_freq_count, sizeof(uint16_t));
		safe_write(fd, cpufreq,
			   (cpu_freq_count * sizeof(struct cpu_freq_data)));
	} else {
		safe_write(fd, &cpu_freq_count, sizeof(uint16_t));
	}
	return;
rwfail:
	error("Unable to send CPU frequency information for %u CPUs",
	      cpu_freq_count);
	return;
}


/*
 * Receive the cpu_frequency table info from slurmd
 */
void
cpu_freq_recv_info(int fd)
{
	safe_read(fd, &cpu_freq_count, sizeof(uint16_t));

	if (cpu_freq_count) {
		if (!cpufreq) {
			cpufreq = (struct cpu_freq_data *)
				  xmalloc(cpu_freq_count *
					  sizeof(struct cpu_freq_data));
		}
		safe_read(fd, cpufreq,
			  (cpu_freq_count * sizeof(struct cpu_freq_data)));
		debug2("Received CPU frequency information for %u CPUs",
		       cpu_freq_count);
	}
	return;
rwfail:
	error("Unable to receive CPU frequency information for %u CPUs",
	      cpu_freq_count);
	cpu_freq_count = 0;
	return;
}


/*
 * Validate the cpus and select the frequency to set
 * Called from task cpuset code with task launch request containing
 *  a pointer to a hex map string of the cpus to be used by this step
 */
void
cpu_freq_cpuset_validate(stepd_step_rec_t *job)
{
	int cpuidx, cpu_num;
	bitstr_t *cpus_to_set;
	bitstr_t *cpu_map;
	char *cpu_bind;
	char *cpu_str;
	char *savestr = NULL;

	debug2("cpu_freq_cpuset_validate: request = %12d  %8x",
	       job->cpu_freq, job->cpu_freq);
	debug2("  jobid=%u, stepid=%u, tasks=%u cpu/task=%u, cpus=%u",
	     job->jobid, job->stepid, job->node_tasks,
	       job->cpus_per_task, job->cpus);
	debug2("  cpu_bind_type=%4x, cpu_bind map=%s",
	       job->cpu_bind_type, job->cpu_bind);

	if (!cpu_freq_count)
		return;

	if (job->cpu_bind == NULL) {
		error("cpu_freq_cpuset_validate: cpu_bind string is null");
		return;
	}
	cpu_bind = xstrdup(job->cpu_bind);

	if ( (cpu_str = strtok_r(cpu_bind, ",", &savestr) ) == NULL) {
		error("cpu_freq_cpuset_validate: cpu_bind string invalid");
		xfree(cpu_bind);
		return;
	}

	cpu_map     = (bitstr_t *) bit_alloc(cpu_freq_count);
	cpus_to_set = (bitstr_t *) bit_alloc(cpu_freq_count);

	do {
		debug3("  cpu_str = %s", cpu_str);

		if ((job->cpu_bind_type & CPU_BIND_MAP) == CPU_BIND_MAP) {
			cpu_num = atoi(cpu_str);
			if (cpu_num >= cpu_freq_count) {
				error("cpu_freq_cpuset_validate: invalid cpu "
				      "number %d", cpu_num);
				bit_free(cpu_map);
				bit_free(cpus_to_set);
				xfree(cpu_bind);
				return;
			}
			bit_set(cpu_map, (bitoff_t)cpu_num);
		} else {
			if (bit_unfmt_hexmask(cpu_map, cpu_str) == -1) {
				error("cpu_freq_cpuset_validate: invalid cpu "
				      "mask %s", cpu_bind);
				bit_free(cpu_map);
				bit_free(cpus_to_set);
				xfree(cpu_bind);
				return;
			}
		}
		bit_or(cpus_to_set, cpu_map);
	} while ( (cpu_str = strtok_r(NULL, ",", &savestr) ) != NULL);

	for (cpuidx = 0; cpuidx < cpu_freq_count; cpuidx++) {
		if (bit_test(cpus_to_set, cpuidx)) {
			_cpu_freq_find_valid(job->cpu_freq, cpuidx);
		}
	}
	cpu_freq_set(job);

	bit_free(cpu_map);
	bit_free(cpus_to_set);
	xfree(cpu_bind);
	return;
}


/*
 * Validate the cpus and select the frequency to set
 * Called from task cgroup cpuset code with string containing
 *  the list of cpus to be used by this step
 */
void
cpu_freq_cgroup_validate(stepd_step_rec_t *job, char *step_alloc_cores)
{
	uint16_t start  = USHRT_MAX;
	uint16_t end    = USHRT_MAX;
	uint16_t cpuidx =  0;
	char *core_range;

	debug2("cpu_freq_cgroup_validate: request value = %12d  %8x",
	       job->cpu_freq, job->cpu_freq);
	debug2("  jobid=%u, stepid=%u, tasks=%u cpu/task=%u, cpus=%u",
	       job->jobid,job->stepid,job->node_tasks,
	       job->cpus_per_task,job->cpus);
	debug2("  cpu_bind_type=%4x, cpu_bind map=%s",
	       job->cpu_bind_type, job->cpu_bind);
	debug2("  step logical cores = %s, step physical cores = %s",
	       job->step_alloc_cores, step_alloc_cores);

	if (!cpu_freq_count)
		return;

	/* set entries in cpu frequency table for this step's cpus */
	core_range = step_alloc_cores;
	while ( (cpuidx = _cpu_freq_next_cpu(&core_range, &cpuidx,
					     &start, &end)) != USHRT_MAX) {
		if (cpuidx >= cpu_freq_count) {
		    error("cpu_freq_validate: index %u exceeds cpu count %u",
			  cpuidx, cpu_freq_count);
		    return;
		}
		_cpu_freq_find_valid(job->cpu_freq, cpuidx);
	}
	cpu_freq_set(job);
	return;
}


/*
 * get the next number in a range
 * assumes range is well-formed, i.e., monotonically increasing,
 *   no leading/trailing punctuation, either comma separated or dash
 *   separated: e.g., "4-6,8,10,13-15"
 */
uint16_t
_cpu_freq_next_cpu(char **core_range, uint16_t *cpuidx,
		   uint16_t *start, uint16_t *end)
{
	int i;
	char *p;

	p = *core_range;

	if (*start == USHRT_MAX) {
		if (*p == '\0')
			return USHRT_MAX;
		if (*p == ',')
			p++;

		i = 0;
		while ( isdigit(*p) ) {
			i = i*10 + (*p - '0');
			p++;
		}
		*core_range = p;
		*start = i;
		return i;
	}

	if (*end == USHRT_MAX) {
		switch (*p)
		{
		case '-' :
			p++;
			i = 0;
			while ( isdigit(*p) ) {
				i = i*10 + (*p - '0');
				p++;
			}
			*core_range = p;
			*end = i;
			break;

		case ',':
			p++;
			i = 0;
			while ( isdigit(*p) ) {
				i = i*10 + (*p - '0');
				p++;
			}
			*start = i;
			*end = USHRT_MAX;
			*core_range = p;
			return i;

		case '\0' :
			return USHRT_MAX;
		}
	}

	i = *cpuidx;
	if ( i < *end ) {
		i++;
		if ( i == *end) {
			*start = USHRT_MAX;
			*end = USHRT_MAX;
		}
	}
	return i;
}

/*
 * Compute the right frequency value to set, based on request
 *
 * input: job record containing cpu frequency parameter
 * input: index to current cpu entry in cpu_freq_data table
 *
 * sets "new_frequency" table entry if valid value found
 */
void
_cpu_freq_find_valid(uint32_t cpu_freq, int cpuidx)
{
	unsigned int j, freq_med = 0;
	uint32_t  freq_list[FREQ_LIST_MAX] =  { 0 };
	char path[SYSFS_PATH_MAX];
	FILE *fp = NULL;

	if ((cpu_freq == NO_VAL) || (cpu_freq == 0)) {	/* Default config */
		;
	} else if (cpu_freq & CPU_FREQ_RANGE_FLAG) {	/* Named values */
		switch(cpu_freq)
		{
		case CPU_FREQ_LOW :
			/* get the value from scale min freq */
			snprintf(path, sizeof(path),
				 PATH_TO_CPU
				 "cpu%u/cpufreq/scaling_min_freq", cpuidx);
			if ( ( fp = fopen(path, "r") ) == NULL ) {
				error("%s: Could not open scaling_min_freq",
				      __func__);
				return;
			}
			if (fscanf (fp, "%u",
				    &cpufreq[cpuidx].new_frequency) < 1) {
				error("%s: Could not read scaling_min_freq",
				      __func__);
				return;
			}
			if (cpufreq[cpuidx].avail_governors & GOV_USERSPACE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "userspace");
			break;

		case CPU_FREQ_MEDIUM :
		case CPU_FREQ_HIGHM1 :
			snprintf(path, sizeof(path),
				 PATH_TO_CPU
				 "cpu%u/cpufreq/scaling_available_frequencies",
				 cpuidx);
			if ( ( fp = fopen(path, "r") ) == NULL ) {
				error("%s: Could not open "
				      "scaling_available_frequencies",
				      __func__);
				return;
			}
			for (j = 0; j < FREQ_LIST_MAX; j++) {
				if ( fscanf(fp, "%u", &freq_list[j]) == EOF)
					break;
				freq_med = (j + 1) / 2;
			}
			if (cpu_freq == CPU_FREQ_MEDIUM) {
				cpufreq[cpuidx].new_frequency =
					freq_list[freq_med];
			} else if (j > 0) {	/* Find second highest freq */
				int high_loc = 0, m1_loc = -1;
				for (j = 1; j < FREQ_LIST_MAX; j++) {
					if (freq_list[j] == 0)
						break;
					if (freq_list[j] > freq_list[high_loc])
						high_loc = j;
				}
				for (j = 0; j < FREQ_LIST_MAX; j++) {
					if (freq_list[j] == 0)
						break;
					if (freq_list[j] == freq_list[high_loc])
						continue;
					if ((m1_loc == -1) ||
					    (freq_list[j] > freq_list[m1_loc]))
						m1_loc = j;
				}
				cpufreq[cpuidx].new_frequency =
					freq_list[m1_loc];
			}
			if (cpufreq[cpuidx].avail_governors & GOV_USERSPACE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "userspace");
			break;

		case CPU_FREQ_HIGH :
			/* get the value from scale max freq */
			snprintf(path, sizeof(path),
				 PATH_TO_CPU "cpu%u/cpufreq/scaling_max_freq",
				 cpuidx);
			if ( ( fp = fopen(path, "r") ) == NULL ) {
				error("%s: Could not open scaling_max_freq",
				      __func__);
				return;
			}
			if (fscanf (fp, "%u",
				    &cpufreq[cpuidx].new_frequency) < 1) {
				error("%s: Could not read scaling_max_freq",
				      __func__);
			}
			if (cpufreq[cpuidx].avail_governors & GOV_USERSPACE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "userspace");
			break;

		case CPU_FREQ_CONSERVATIVE:
			if (cpufreq[cpuidx].avail_governors & GOV_CONSERVATIVE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "conservative");
			break;

		case CPU_FREQ_ONDEMAND:
			if (cpufreq[cpuidx].avail_governors & GOV_ONDEMAND)
				strcpy(cpufreq[cpuidx].new_governor,"ondemand");
			break;

		case CPU_FREQ_PERFORMANCE:
			if (cpufreq[cpuidx].avail_governors & GOV_PERFORMANCE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "performance");
			break;

		case CPU_FREQ_POWERSAVE:
			if (cpufreq[cpuidx].avail_governors & GOV_POWERSAVE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "powersave");
			break;

		case CPU_FREQ_USERSPACE:
			if (cpufreq[cpuidx].avail_governors & GOV_USERSPACE)
				strcpy(cpufreq[cpuidx].new_governor,
				       "userspace");
			break;

		default :
			error("%s: invalid cpu_freq value %u",
			      __func__, cpu_freq);
			return;
		}

		if (fp)
			fclose(fp);

	} else {
		/* find legal value close to requested value */
		snprintf(path, sizeof(path),
			 PATH_TO_CPU
			 "cpu%u/cpufreq/scaling_available_frequencies", cpuidx);
		if ( ( fp = fopen(path, "r") ) == NULL )
			return;
		for (j = 0; j < FREQ_LIST_MAX; j++) {

			if ( fscanf(fp, "%u", &freq_list[j]) == EOF)
				break;
			if (cpu_freq == freq_list[j]) {
				cpufreq[cpuidx].new_frequency = freq_list[j];
				break;
			}
			if (j > 0) {
				if (freq_list[j] > freq_list[j-1] ) {
					/* ascending order */
					if ((cpu_freq > freq_list[j-1]) &&
					    (cpu_freq < freq_list[j])) {
						cpufreq[cpuidx].new_frequency =
							freq_list[j];
						break;
					}
				} else {
					/* descending order */
					if ((cpu_freq > freq_list[j]) &&
					    (cpu_freq < freq_list[j-1])) {
						cpufreq[cpuidx].new_frequency =
							freq_list[j];
						break;
					}
				}
			}
		}
		fclose(fp);
		if (cpufreq[cpuidx].avail_governors & GOV_USERSPACE)
			strcpy(cpufreq[cpuidx].new_governor, "userspace");
	}

	debug3("%s: CPU:%u, frequency:%u governor:%s",
	       __func__, cpuidx, cpufreq[cpuidx].new_frequency,
	       cpufreq[cpuidx].new_governor);

	return;
}


/*
 * Verify cpu_freq parameter
 *
 * In addition to a numeric frequency value, we allow the user to specify
 * "low", "medium", "highm1", or "high" frequency plus "performance",
 * "powersave", "userspace" and "ondemand" governor
 *
 * returns -1 on error, 0 otherwise
 */
int
cpu_freq_verify_param(const char *arg, uint32_t *cpu_freq)
{
	char *end;
	uint32_t frequency;

	if (arg == NULL) {
		return 0;
	}

	if ( (frequency = strtoul(arg, &end, 10) )) {
		*cpu_freq = frequency;
		return 0;
	}

	if (strncasecmp(arg, "lo", 2) == 0) {
		*cpu_freq = CPU_FREQ_LOW;
		return 0;
	} else if (strncasecmp(arg, "co", 2) == 0) {
		*cpu_freq = CPU_FREQ_CONSERVATIVE;
		return 0;
	} else if (strncasecmp(arg, "him1", 4) == 0 ||
		   strncasecmp(arg, "highm1", 6) == 0) {
		*cpu_freq = CPU_FREQ_HIGHM1;
		return 0;
	} else if (strncasecmp(arg, "hi", 2) == 0) {
		*cpu_freq = CPU_FREQ_HIGH;
		return 0;
	} else if (strncasecmp(arg, "med", 3) == 0) {
		*cpu_freq = CPU_FREQ_MEDIUM;
		return 0;
	} else if (strncasecmp(arg, "perf", 4) == 0) {
		*cpu_freq = CPU_FREQ_PERFORMANCE;
		return 0;
	} else if (strncasecmp(arg, "pow", 3) == 0) {
		*cpu_freq = CPU_FREQ_POWERSAVE;
		return 0;
	} else if (strncasecmp(arg, "user", 4) == 0) {
		*cpu_freq = CPU_FREQ_USERSPACE;
		return 0;
	} else if (strncasecmp(arg, "onde", 4) == 0) {
		*cpu_freq = CPU_FREQ_ONDEMAND;
		return 0;
	}

	error("unrecognized --cpu-freq argument \"%s\"", arg);
	return -1;
}

/* Convert a cpu_freq number to its equivalent string */
void
cpu_freq_to_string(char *buf, int buf_size, uint32_t cpu_freq)
{
	if (cpu_freq == CPU_FREQ_LOW)
		snprintf(buf, buf_size, "Low");
	else if (cpu_freq == CPU_FREQ_MEDIUM)
		snprintf(buf, buf_size, "Medium");
	else if (cpu_freq == CPU_FREQ_HIGHM1)
		snprintf(buf, buf_size, "Highm1");
	else if (cpu_freq == CPU_FREQ_HIGH)
		snprintf(buf, buf_size, "High");
	else if (cpu_freq == CPU_FREQ_CONSERVATIVE)
		snprintf(buf, buf_size, "Conservative");
	else if (cpu_freq == CPU_FREQ_PERFORMANCE)
		snprintf(buf, buf_size, "Performance");
	else if (cpu_freq == CPU_FREQ_POWERSAVE)
		snprintf(buf, buf_size, "PowerSave");
	else if (cpu_freq == CPU_FREQ_USERSPACE)
		snprintf(buf, buf_size, "UserSpace");
	else if (cpu_freq == CPU_FREQ_ONDEMAND)
		snprintf(buf, buf_size, "OnDemand");
	else if (cpu_freq & CPU_FREQ_RANGE_FLAG)
		snprintf(buf, buf_size, "Unknown");
	else if (fuzzy_equal(cpu_freq, NO_VAL)) {
		if (buf_size > 0)
			buf[0] = '\0';
	} else
		convert_num_unit2((double)cpu_freq, buf, buf_size,
				  UNIT_KILO, 1000, false);
}

/*
 * set cpu frequency if possible for each cpu of the job step
 */
void
cpu_freq_set(stepd_step_rec_t *job)
{
	char path[SYSFS_PATH_MAX];
	FILE *fp;
	char freq_value[LINE_LEN], gov_value[LINE_LEN];
	unsigned int i, j;
	int fd;

	if ((!cpu_freq_count) || (!cpufreq))
		return;

	j = 0;
	for (i = 0; i < cpu_freq_count; i++) {
		bool reset_freq = false;
		bool reset_gov = false;

		if (cpufreq[i].new_frequency != 0)
			reset_freq = true;
		if (cpufreq[i].new_governor[0] != '\0')
			reset_gov = true;
		if (!reset_freq && !reset_gov)
			continue;

		fd = _set_cpu_owner_lock(i, job->jobid);
		if (reset_gov) {
			snprintf(gov_value, LINE_LEN, "%s",
				 cpufreq[i].new_governor);
			snprintf(path, sizeof(path),
				 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor",
				 i);
			if ((fp = fopen(path, "w"))) {
				fputs(gov_value, fp);
				fputc('\n', fp);
				fclose(fp);
			} else {
				error("%s: Can not set CPU governor: %m",
				      __func__);
			}
		}

		if (reset_freq) {
			snprintf(path, sizeof(path),
				 PATH_TO_CPU "cpu%u/cpufreq/scaling_setspeed",
				 i);
			snprintf(freq_value, LINE_LEN, "%u",
				 cpufreq[i].new_frequency);
			if ((fp = fopen(path, "w"))) {
				fputs(freq_value, fp);
				fclose(fp);
			} else {
				error("%s: Can not set CPU frequency: %m",
				      __func__);
			}
		} else {
			strcpy(freq_value, "N/A");
		}
		(void) close(fd);

		j++;
		debug3("%s: CPU:%u frequency:%s governor:%s",
		       __func__, i, freq_value, gov_value);
	}
	debug("%s: #cpus set = %u", __func__, j);
}

/*
 * reset the cpus used by the process to their
 * default frequency and governor type
 */
void
cpu_freq_reset(stepd_step_rec_t *job)
{
	char path[SYSFS_PATH_MAX];
	FILE *fp;
	char value[LINE_LEN];
	unsigned int i, j;
	uint32_t def_cpu_freq;
	int fd;

	if ((!cpu_freq_count) || (!cpufreq))
		return;

	def_cpu_freq = slurm_get_cpu_freq_def();
	j = 0;
	for (i = 0; i < cpu_freq_count; i++) {
		bool reset_freq = false;
		bool reset_gov = false;

		if (cpufreq[i].new_frequency != 0)
			reset_freq = true;
		if (cpufreq[i].new_governor[0] != '\0')
			reset_gov = true;
		if (!reset_freq && !reset_gov)
			continue;
		fd = _test_cpu_owner_lock(i, job->jobid);
		if (fd < 0)
			continue;

		cpufreq[i].new_frequency = 0;
		cpufreq[i].new_governor[0] = '\0';
		_cpu_freq_find_valid(def_cpu_freq, i);
		if (cpufreq[i].new_frequency == 0)
			cpufreq[i].new_frequency = cpufreq[i].orig_frequency;
		if (cpufreq[i].new_governor[0] == '\0') {
			strcpy(cpufreq[i].new_governor,
			       cpufreq[i].orig_governor);
		}

		if (reset_freq) {
			snprintf(path, sizeof(path),
				 PATH_TO_CPU "cpu%u/cpufreq/scaling_setspeed",
				 i);
			snprintf(value, LINE_LEN, "%u",
				 cpufreq[i].new_frequency);
			if ((fp = fopen(path, "w"))) {
				fputs(value, fp);
				fclose(fp);
			} else {
				error("%s: Can not set CPU frequency: %m",
				      __func__);
			}
		}

		if (reset_gov) {
			snprintf(path, sizeof(path),
				 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor",
				 i);
			if ((fp = fopen(path, "w"))) {
				fputs(cpufreq[i].new_governor, fp);
				fputc('\n', fp);
				fclose(fp);
			} else {
				error("%s: Can not set CPU governor: %m",
				      __func__);
			}
		}
		(void) close(fd);

		j++;
		debug3("%s: CPU:%u frequency:%u governor:%s",
		       __func__, i, cpufreq[i].new_frequency,
		       cpufreq[i].new_governor);
	}
	debug("%s: #cpus reset = %u", __func__, j);
	xfree(slurmd_spooldir);
}
