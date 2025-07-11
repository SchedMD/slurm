/*****************************************************************************\
 *  cpu_frequency.c - support for srun option --cpu-freq=<frequency>
 *****************************************************************************
 *  Copyright (C) 2012 Bull
 *  Written by Don Albert, <don.albert@bull.com>
 *  Modified by Rod Schultz, <rod.schultz@bull.com> for min-max:gov
 *  Modified by Janne Blomqvist, <janne.blomqvist@aalto.fi> for
 *  intel_pstate support
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
\*****************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "slurm/slurm.h"

#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/read_config.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmd/slurmd/slurmd.h"

#define PATH_TO_CPU	"/sys/devices/system/cpu/"
#define LINE_LEN	100
#define FREQ_LIST_MAX	64
#define GOV_NAME_LEN	24

#define GOV_CONSERVATIVE	0x01
#define GOV_ONDEMAND		0x02
#define GOV_PERFORMANCE		0x04
#define GOV_POWERSAVE		0x08
#define GOV_USERSPACE		0x10
#define GOV_SCHEDUTIL		0x20

static uint16_t cpu_freq_count = 0;
static int set_batch_freq = -1;

static struct cpu_freq_data {
	uint8_t  avail_governors;
	uint8_t  nfreq;
	bool     org_set;
	uint32_t avail_freq[FREQ_LIST_MAX];
	char     org_governor[GOV_NAME_LEN];
	char     new_governor[GOV_NAME_LEN];
	uint32_t org_frequency;
	uint32_t new_frequency;
	uint32_t org_min_freq;
	uint32_t new_min_freq;
	uint32_t org_max_freq;
	uint32_t new_max_freq;
} * cpufreq = NULL;
static char *slurmd_spooldir = NULL;

static int      _cpu_freq_cpu_avail(int cpx);
static int      _cpu_freq_current_state(int cpx);
static uint16_t	_cpu_freq_next_cpu(char **core_range, uint16_t *cpx,
				   uint16_t *start, uint16_t *end);
static uint32_t	_cpu_freq_get_scaling_freq(int cpuidx, char* option);
static void     _cpu_freq_init_data(int cpx);
static void     _cpu_freq_setup_data(stepd_step_rec_t *step, int cpx);
static bool	_cpu_freq_test_scaling_freq(int cpuidx, char *option);
static int	_derive_avail_freq(int cpuidx);
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
 * _set_cpu_owner_lock  - set specified job to own the CPU, file locked at exit
 * _test_cpu_owner_lock - test if the specified job owns the CPU
 */
static int _set_cpu_owner_lock(int cpu_id, uint32_t job_id)
{
	char tmp[PATH_MAX];
	int fd;

	snprintf(tmp, sizeof(tmp), "%s/cpu", slurmd_spooldir);
	if ((mkdir(tmp, 0700) != 0) && (errno != EEXIST)) {
		error("mkdir failed: %m %s",tmp);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "%s/cpu/%d", slurmd_spooldir, cpu_id);
	fd = open(tmp, O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		error("%s: open: %m %s", __func__, tmp);
		return fd;
	}
	if (_fd_lock_retry(fd) < 0)
		error("%s: fd_get_write_lock: %m %s", __func__, tmp);
	safe_write(fd, &job_id, sizeof(job_id));

	return fd;

rwfail:
	error("%s: write: %m %s", __func__, tmp);
	return fd;
}

/* Test if specified job ID owns this CPU for frequency/governor control
 * RET 0 if owner, -1 otherwise */
static int _test_cpu_owner_lock(int cpu_id, uint32_t job_id)
{
	char tmp[PATH_MAX];
	uint32_t in_job_id;
	int fd;

	snprintf(tmp, sizeof(tmp), "%s/cpu", slurmd_spooldir);
	if ((mkdir(tmp, 0700) != 0) && (errno != EEXIST)) {
		error("%s: mkdir failed: %m %s", __func__, tmp);
		return -1;
	}
	snprintf(tmp, sizeof(tmp), "%s/cpu/%d", slurmd_spooldir, cpu_id);
	fd = open(tmp, O_RDWR, 0600);
	if (fd < 0) {
		if (errno != ENOENT)	/* Race condition */
			error("%s: open: %m %s", __func__, tmp);
		return -1;
	}
	if (_fd_lock_retry(fd) < 0) {
		error("%s: fd_get_write_lock: %m %s", __func__, tmp);
		close(fd);
		return -1;
	}
	safe_read(fd, &in_job_id, sizeof(in_job_id));
	(void) fd_release_lock(fd);

	if (job_id != in_job_id) {
		/* Result of various race conditions */
		debug("%s: CPU %d now owned by job %u rather than job %u",
		      __func__, cpu_id, in_job_id, job_id);
		close(fd);
		return -1;
	}
	close(fd);
	debug2("%s: CPU %d owned by job %u as expected",
	       __func__, cpu_id, job_id);

	return 0;

rwfail:
	error("%s: read: %m %s", __func__, tmp);
	(void) fd_release_lock(fd);
	close(fd);
	return -1;
}

/*
 * Try do build a table of available frequencies based upon the min/max values
 */
static int _derive_avail_freq(int cpuidx)
{
	uint32_t min_freq, max_freq, delta_freq;
	int i;

	min_freq = _cpu_freq_get_scaling_freq(cpuidx, "scaling_min_freq");
	if (min_freq == 0)
		return SLURM_ERROR;
	max_freq = _cpu_freq_get_scaling_freq(cpuidx, "scaling_max_freq");
	if (max_freq == 0)
		return SLURM_ERROR;
	delta_freq = (max_freq - min_freq) / (FREQ_LIST_MAX - 1);
	for (i = 0; i < (FREQ_LIST_MAX - 1); i++)
		cpufreq[cpuidx].avail_freq[i] = min_freq + (delta_freq * i);
	cpufreq[cpuidx].avail_freq[FREQ_LIST_MAX - 1] = max_freq;
	cpufreq[cpuidx].nfreq = FREQ_LIST_MAX;

	return SLURM_SUCCESS;
}

/*
 * Find available frequencies on this cpu
 * IN      cpuidx     - cpu to query
 * Return: SLURM_SUCCESS or SLURM_ERROR
 *         avail_freq array will be in strictly ascending order
 */
static int
_cpu_freq_cpu_avail(int cpuidx)
{
	FILE *fp = NULL;
	char path[PATH_MAX];
	int i, j, k;
	uint32_t freq;
	bool all_avail = false;

	snprintf(path, sizeof(path),  PATH_TO_CPU
		 "cpu%u/cpufreq/scaling_available_frequencies", cpuidx);
	if ( ( fp = fopen(path, "r") ) == NULL ) {
		/*
		 * Don't log an error here, scaling_available_frequencies
		 * does not exist when using the intel_pstate driver.
		 * Derive values from min/max values
		 */
		return _derive_avail_freq(cpuidx);
	}
	for (i = 0; i < (FREQ_LIST_MAX-1); i++) {
		if ( fscanf(fp, "%u", &freq) == EOF) {
			all_avail = true;
			break;
		}
		/* make sure list is sorted */
		for (j = 0; j < i; j++) {
			if (freq < cpufreq[cpuidx].avail_freq[j]) {
				for (k = i; k >= j; k--) {
					cpufreq[cpuidx].avail_freq[k+1] =
						cpufreq[cpuidx].avail_freq[k];
				}
				break;
			}
		}
		cpufreq[cpuidx].avail_freq[j] = freq;
	}
	cpufreq[cpuidx].nfreq = i;
	fclose(fp);
	if (!all_avail)
		error("all available frequencies not scanned");
	return SLURM_SUCCESS;
}

/*
 * called to check if the node supports setting CPU frequency
 * if so, initialize fields in cpu_freq_data structure
 */
extern void
cpu_freq_init(slurmd_conf_t *conf)
{
	char path[PATH_MAX];
	struct stat statbuf;
	FILE *fp;
	char value[LINE_LEN];
	unsigned int i, j;

	xfree(slurmd_spooldir);
	slurmd_spooldir = xstrdup(conf->spooldir);

	if (running_in_slurmstepd())
		return;

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
		int cpuidx;
		cpufreq = (struct cpu_freq_data *)
			  xmalloc(cpu_freq_count *
				  sizeof(struct cpu_freq_data));

		for (cpuidx = 0; cpuidx < cpu_freq_count; cpuidx++)
			_cpu_freq_init_data(cpuidx);
	}

	debug2("Gathering cpu frequency information for %u cpus",
	       cpu_freq_count);
	for (i = 0; i < cpu_freq_count; i++) {
		snprintf(path, sizeof(path),
			 PATH_TO_CPU
			 "cpu%u/cpufreq/scaling_available_governors", i);
		if ((fp = fopen(path, "r")) == NULL)
			continue;
		if (fgets(value, LINE_LEN, fp) == NULL) {
			fclose(fp);
			continue;
		}
		if (strstr(value, "conservative")) {
			cpufreq[i].avail_governors |= GOV_CONSERVATIVE;
			if (i == 0)
				log_flag(CPU_FREQ, "cpu_freq: Conservative governor defined on cpu 0");
		}
		if (strstr(value, "ondemand")) {
			cpufreq[i].avail_governors |= GOV_ONDEMAND;
			if (i == 0)
				log_flag(CPU_FREQ, "cpu_freq: OnDemand governor defined on cpu 0");
		}
		if (strstr(value, "performance")) {
			cpufreq[i].avail_governors |= GOV_PERFORMANCE;
			if (i == 0)
				log_flag(CPU_FREQ, "cpu_freq: Performance governor defined on cpu 0");
		}
		if (strstr(value, "powersave")) {
			cpufreq[i].avail_governors |= GOV_POWERSAVE;
			if (i == 0)
				log_flag(CPU_FREQ, "cpu_freq: PowerSave governor defined on cpu 0");
		}
		if (strstr(value, "userspace")) {
			cpufreq[i].avail_governors |= GOV_USERSPACE;
			if (i == 0)
				log_flag(CPU_FREQ, "cpu_freq: UserSpace governor defined on cpu 0");
		}
		if (strstr(value, "schedutil")) {
			cpufreq[i].avail_governors |= GOV_SCHEDUTIL;
			if (i == 0)
				log_flag(CPU_FREQ, "cpu_freq: SchedUtil governor defined on cpu 0");
		}
		fclose(fp);
		if (_cpu_freq_cpu_avail(i) == SLURM_ERROR)
			continue;
		if ((i == 0) &&
		    (slurm_conf.debug_flags & DEBUG_FLAG_CPU_FREQ)) {
			for (j = 0; j < cpufreq[i].nfreq; j++) {
				info("cpu_freq: frequency %u defined on cpu 0",
				     cpufreq[i].avail_freq[j]);
			}
		}
	}
	return;
}

extern void
cpu_freq_fini(void)
{
	xfree(cpufreq);
	xfree(slurmd_spooldir);
}

/*
 * Send the cpu_frequency table info to slurmstepd
 */
extern void
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
extern void
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
 * Called from task/affinity code with task launch request containing
 *  a pointer to a hex map string of the cpus to be used by this step
 */
extern void
cpu_freq_cpuset_validate(stepd_step_rec_t *step)
{
	int cpuidx, cpu_num;
	bitstr_t *cpus_to_set;
	bitstr_t *cpu_map;
	char *cpu_bind;
	char *cpu_str;
	char *savestr = NULL;
	char cpu_bind_type_string[128];

	if (set_batch_freq == -1) {
		if (xstrcasestr(slurm_conf.launch_params,
				"batch_step_set_cpu_freq"))
			set_batch_freq = 1;
		else
			set_batch_freq = 0;
	}

	if (((step->step_id.step_id == SLURM_BATCH_SCRIPT) &&
	     !set_batch_freq) ||
	    (step->step_id.step_id == SLURM_INTERACTIVE_STEP) ||
	    (step->step_id.step_id == SLURM_EXTERN_CONT))
		return;

	slurm_sprint_cpu_bind_type(cpu_bind_type_string, step->cpu_bind_type);

	log_flag(CPU_FREQ, "%s: request: min=(%12d  %8x) max=(%12d %8x) governor=%8x",
		 __func__, step->cpu_freq_min, step->cpu_freq_min,
		 step->cpu_freq_max, step->cpu_freq_max, step->cpu_freq_gov);
	log_flag(CPU_FREQ, "  jobid=%u, stepid=%u, tasks=%u cpu/task=%u, cpus=%u",
		 step->step_id.job_id, step->step_id.step_id,
		 step->node_tasks, step->cpus_per_task,
		 step->cpus);
	log_flag(CPU_FREQ, "  cpu_bind_type=%4x, cpu_bind map=%s",
		 step->cpu_bind_type, step->cpu_bind);

	if (!cpu_freq_count)
		return;

	if (step->cpu_bind == NULL) {
		/*
		 * slurm_verify_cpu_bind will set cpu_bind to NULL for manual
		 * binding that doesn't require an argument
		 */
		if (!((step->cpu_bind_type & CPU_BIND_NONE) ||
		      (step->cpu_bind_type & CPU_BIND_LDRANK)))
			error("cpu_freq_cpuset_validate: cpu_bind string is null");
		return;
	}
	cpu_bind = xstrdup(step->cpu_bind);

	if ( (cpu_str = strtok_r(cpu_bind, ",", &savestr) ) == NULL) {
		error("cpu_freq_cpuset_validate: cpu_bind string invalid");
		xfree(cpu_bind);
		return;
	}

	cpu_map = bit_alloc(cpu_freq_count);
	cpus_to_set = bit_alloc(cpu_freq_count);

	do {
		debug3("  cpu_str = %s", cpu_str);

		if ((step->cpu_bind_type & CPU_BIND_MAP) == CPU_BIND_MAP) {
			cpu_num = atoi(cpu_str);
			if (cpu_num >= cpu_freq_count) {
				error("cpu_freq_cpuset_validate: invalid cpu "
				      "number %d", cpu_num);
				FREE_NULL_BITMAP(cpu_map);
				FREE_NULL_BITMAP(cpus_to_set);
				xfree(cpu_bind);
				return;
			}
			bit_set(cpu_map, (bitoff_t)cpu_num);
		} else {
			if (bit_unfmt_hexmask(cpu_map, cpu_str) == -1) {
				error("cpu_freq_cpuset_validate: invalid cpu "
				      "mask %s", cpu_bind);
				FREE_NULL_BITMAP(cpu_map);
				FREE_NULL_BITMAP(cpus_to_set);
				xfree(cpu_bind);
				return;
			}
		}
		bit_or(cpus_to_set, cpu_map);
	} while ( (cpu_str = strtok_r(NULL, ",", &savestr) ) != NULL);

	for (cpuidx = 0; cpuidx < cpu_freq_count; cpuidx++) {
		if (bit_test(cpus_to_set, cpuidx)) {
			_cpu_freq_setup_data(step, cpuidx);
		}
	}
	cpu_freq_set(step);

	FREE_NULL_BITMAP(cpu_map);
	FREE_NULL_BITMAP(cpus_to_set);
	xfree(cpu_bind);
	return;
}

/*
 * Validate the cpus and select the frequency to set
 * Called from task cgroup cpuset code with string containing
 *  the list of cpus to be used by this step
 */
extern void
cpu_freq_cgroup_validate(stepd_step_rec_t *step, char *step_alloc_cores)
{
	uint16_t start  = USHRT_MAX;
	uint16_t end    = USHRT_MAX;
	uint16_t cpuidx =  0;
	char *core_range;

	if (set_batch_freq == -1) {
		if (xstrcasestr(slurm_conf.launch_params,
				"batch_step_set_cpu_freq"))
			set_batch_freq = 1;
		else
			set_batch_freq = 0;
	}

	if (((step->step_id.step_id == SLURM_BATCH_SCRIPT) &&
	     !set_batch_freq) ||
	    (step->step_id.step_id == SLURM_INTERACTIVE_STEP) ||
	    (step->step_id.step_id == SLURM_EXTERN_CONT))
		return;

	log_flag(CPU_FREQ, "%s: request: min=(%12d  %8x) max=(%12d %8x) governor=%8x",
		 __func__, step->cpu_freq_min, step->cpu_freq_min,
		 step->cpu_freq_max, step->cpu_freq_max, step->cpu_freq_gov);
	log_flag(CPU_FREQ, "  jobid=%u, stepid=%u, tasks=%u cpu/task=%u, cpus=%u",
		 step->step_id.job_id, step->step_id.step_id,
		 step->node_tasks, step->cpus_per_task,
		 step->cpus);
	log_flag(CPU_FREQ, "  cpu_bind_type=%4x, cpu_bind map=%s",
		 step->cpu_bind_type, step->cpu_bind);
	log_flag(CPU_FREQ, "  step logical cores = %s, step physical cores = %s",
		 step->step_alloc_cores, step_alloc_cores);

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
		_cpu_freq_setup_data(step, cpuidx);
	}
	cpu_freq_set(step);
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
 * Find current governor on this cpu
 *
 * Return: SLURM_SUCCESS or SLURM_ERROR
 */
static int
_cpu_freq_get_cur_gov(int cpuidx)
{
	FILE *fp = NULL;
	char path[PATH_MAX], gov_value[LINE_LEN];
	int j;

	snprintf(path, sizeof(path),
		 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor", cpuidx);
	if ((fp = fopen(path, "r")) == NULL) {
		error("%s: Could not open scaling_governor", __func__);
		return SLURM_ERROR;
	}
	if (fgets(gov_value, LINE_LEN, fp) == NULL) {
		error("%s: Could not read scaling_governor", __func__);
		fclose(fp);
		return SLURM_ERROR;
	}
	if (strlen(gov_value) >= GOV_NAME_LEN) {
		error("%s: scaling_governor is to long", __func__);
		fclose(fp);
		return SLURM_ERROR;
	}
	strcpy(cpufreq[cpuidx].org_governor, gov_value);
	fclose(fp);
	j = strlen(cpufreq[cpuidx].org_governor);
	if ((j > 0) && (cpufreq[cpuidx].org_governor[j - 1] == '\n'))
		cpufreq[cpuidx].org_governor[j - 1] = '\0';
	return SLURM_SUCCESS;
}

/*
 * set cpu governor
 */
static int
_cpu_freq_set_gov(stepd_step_rec_t *step, int cpuidx, char *gov)
{
	char path[PATH_MAX];
	FILE *fp;
	int fd, rc;

	rc = SLURM_SUCCESS;
	snprintf(path, sizeof(path), PATH_TO_CPU
		 "cpu%u/cpufreq/scaling_governor", cpuidx);
	fd = _set_cpu_owner_lock(cpuidx, step->step_id.job_id);
	if ((fp = fopen(path, "w"))) {
		fputs(gov, fp);
		fputc('\n', fp);
		fclose(fp);
	} else {
		error("%s: Can not set CPU governor: %m", __func__);
		rc = SLURM_ERROR;
	}
	if (fd >= 0) {
		(void) fd_release_lock(fd);
		(void) close(fd);
	}
	return rc;
}

/*
 * get one of scalling_min_freq, scaling_max_freq, cpuinfo_cur_freq
 *
 * Return: value of scaling_min_freq, or 0 on error
 */
static uint32_t
_cpu_freq_get_scaling_freq(int cpuidx, char* option)
{
	FILE *fp = NULL;
	char path[PATH_MAX];
	uint32_t freq;
	/* get the value from 'option' */
	snprintf(path, sizeof(path), PATH_TO_CPU
		"cpu%u/cpufreq/%s", cpuidx, option);
	if ( ( fp = fopen(path, "r") ) == NULL ) {
		error("%s: Could not open %s", __func__, option);
		return 0;
	}
	if (fscanf (fp, "%u", &freq) < 1) {
		error("%s: Could not read %s", __func__, option);
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return freq;
}

/*
 * test for existence of cpufreq file
 *
 * Return: true if file found
 */
static bool
_cpu_freq_test_scaling_freq(int cpuidx, char *option)
{
	char path[PATH_MAX];
	struct stat stat_buf;

	/* get the value from 'option' */
	snprintf(path, sizeof(path), PATH_TO_CPU
		"cpu%u/cpufreq/%s", cpuidx, option);
	if (stat(path, &stat_buf) == 0)
		return true;
	return false;
}

/*
 * set one of scalling_min_freq, scaling_max_freq, scaling_setspeed
 * -- assume governor already set to userspace ---
 *
 */
static int
_cpu_freq_set_scaling_freq(stepd_step_rec_t *step, int cpx, uint32_t freq,
		char* option)
{
	char path[PATH_MAX];
	FILE *fp;
	int fd, rc;
	uint32_t newfreq;

	rc = SLURM_SUCCESS;
	snprintf(path, sizeof(path), PATH_TO_CPU
		 "cpu%u/cpufreq/%s", cpx, option);
	fd = _set_cpu_owner_lock(cpx, step->step_id.job_id);
	if ((fp = fopen(path, "w"))) {
		fprintf(fp, "%u\n", freq);
		fclose(fp);
	} else {
		error("%s: Can not set %s: %m", __func__, option);
		rc = SLURM_ERROR;
	}
	if (fd >= 0) {
		(void) fd_release_lock(fd);
		(void) close(fd);
	}
	if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_FREQ) {
		newfreq = _cpu_freq_get_scaling_freq(cpx, option);
		if (newfreq != freq) {
			error("Failed to set freq_scaling %s to %u (org=%u)",
			      option, freq, newfreq);
		}
	}
	return rc;

}

/*
 * Get current state
 *
 * IN:     cpuidx        - cpu to query
 * Return: SLURM_SUCCESS or SLURM_ERROR
 */
static int
_cpu_freq_current_state(int cpuidx)
{
	static int freq_file = -1;
	uint32_t freq;

	if (cpufreq[cpuidx].org_set) {
		/*
		 * The current state was already loaded for this cpu.
		 * Likely caused by stacked task plugins. Prevent
		 * overwriting the original values so they can be
		 * restored correctly after job completion.
		 */
		return SLURM_SUCCESS;
	}

	/*
	 * Getting 'previous' values using the 'scaling' values rather
	 * than the 'cpuinfo' values.
	 * The 'cpuinfo' values are read only. min/max seem to be raw
	 * hardware capability.
	 * The 'scaling' values are set by the governor.
	 * For the current frequency, use the cpuinfo_cur_freq file
	 * since the intel_pstate driver doesn't necessarily create
	 * the scaling_cur_freq file.
	 */
	if (freq_file == -1) {
		if (_cpu_freq_test_scaling_freq(cpuidx, "cpuinfo_cur_freq"))
			freq_file = 0;
		else				/* Use "scaling_cur_freq" */
			freq_file = 1;
	}
	if (freq_file == 0)
		freq = _cpu_freq_get_scaling_freq(cpuidx, "cpuinfo_cur_freq");
	else
		freq = _cpu_freq_get_scaling_freq(cpuidx, "scaling_cur_freq");
	if (freq == 0)
		return SLURM_ERROR;
	cpufreq[cpuidx].org_frequency = freq;
	freq = _cpu_freq_get_scaling_freq(cpuidx, "scaling_min_freq");
	if (freq == 0)
		return SLURM_ERROR;
	cpufreq[cpuidx].org_min_freq = freq;
	freq = _cpu_freq_get_scaling_freq(cpuidx, "scaling_max_freq");
	if (freq == 0)
		return SLURM_ERROR;
	cpufreq[cpuidx].org_max_freq = freq;

	if (_cpu_freq_get_cur_gov(cpuidx) == SLURM_SUCCESS) {
		cpufreq[cpuidx].org_set = true;
		return SLURM_SUCCESS;
	} else {
		return SLURM_ERROR;
	}
}


/*
 * Copy string representation of a governor into cpufreq structure for a cpu.
 */
static int
_cpu_freq_govspec_string(uint32_t cpu_freq, int cpuidx)
{

	if ((cpu_freq & CPU_FREQ_RANGE_FLAG) == 0)
		return SLURM_ERROR;

	switch(cpu_freq)
	{
	case CPU_FREQ_CONSERVATIVE:
		if (cpufreq[cpuidx].avail_governors & GOV_CONSERVATIVE)
			strcpy(cpufreq[cpuidx].new_governor, "conservative");
		return SLURM_SUCCESS;
	case CPU_FREQ_ONDEMAND:
		if (cpufreq[cpuidx].avail_governors & GOV_ONDEMAND)
			strcpy(cpufreq[cpuidx].new_governor,"ondemand");
		return SLURM_SUCCESS;
	case CPU_FREQ_PERFORMANCE:
		if (cpufreq[cpuidx].avail_governors & GOV_PERFORMANCE)
			strcpy(cpufreq[cpuidx].new_governor, "performance");
		return SLURM_SUCCESS;
	case CPU_FREQ_POWERSAVE:
		if (cpufreq[cpuidx].avail_governors & GOV_POWERSAVE)
			strcpy(cpufreq[cpuidx].new_governor, "powersave");
		return SLURM_SUCCESS;
	case CPU_FREQ_USERSPACE:
		if (cpufreq[cpuidx].avail_governors & GOV_USERSPACE)
			strcpy(cpufreq[cpuidx].new_governor, "userspace");
		return SLURM_SUCCESS;
	case CPU_FREQ_SCHEDUTIL:
		if (cpufreq[cpuidx].avail_governors & GOV_SCHEDUTIL)
			strcpy(cpufreq[cpuidx].new_governor, "schedutil");
		return SLURM_SUCCESS;
	default:
		return SLURM_ERROR;
	}
}

/*
 * Convert frequency_spec into an actual frequency
 * Returns -- frequency from avail frequency list, or NO_VAL
 */
uint32_t
_cpu_freq_freqspec_num(uint32_t cpu_freq, int cpuidx)
{
	int fx, j;
	if (!cpufreq || !cpufreq[cpuidx].nfreq)
		return NO_VAL;
	/* assume the frequency list is in ascending order */
	if (cpu_freq & CPU_FREQ_RANGE_FLAG) {	/* Named values */
		switch(cpu_freq)
		{
		case CPU_FREQ_LOW :
			return cpufreq[cpuidx].avail_freq[0];

		case CPU_FREQ_MEDIUM :
			if (cpufreq[cpuidx].nfreq == 1)
				return cpufreq[cpuidx].avail_freq[0];
			fx = (cpufreq[cpuidx].nfreq - 1) / 2;
			return cpufreq[cpuidx].avail_freq[fx];

		case CPU_FREQ_HIGHM1 :
			if (cpufreq[cpuidx].nfreq == 1)
				return cpufreq[cpuidx].avail_freq[0];
			fx = cpufreq[cpuidx].nfreq - 2;
			return cpufreq[cpuidx].avail_freq[fx];

		case CPU_FREQ_HIGH :
			fx = cpufreq[cpuidx].nfreq - 1;
			return cpufreq[cpuidx].avail_freq[fx];

		default:
			return NO_VAL;
		}
	}

	/* check for request above or below available values */
	if (cpu_freq < cpufreq[cpuidx].avail_freq[0]) {
		error("Rounding requested frequency %d "
		      "up to lowest available %d", cpu_freq,
		      cpufreq[cpuidx].avail_freq[0]);
		return cpufreq[cpuidx].avail_freq[0];
	} else if (cpufreq[cpuidx].avail_freq[cpufreq[cpuidx].nfreq - 1]
		   < cpu_freq) {
		error("Rounding requested frequency %d "
		      "down to highest available %d", cpu_freq,
		      cpufreq[cpuidx].avail_freq[cpufreq[cpuidx].nfreq - 1]);
		return cpufreq[cpuidx].avail_freq[cpufreq[cpuidx].nfreq - 1];
	}

	/* check for frequency, round up if no exact match */
	for (j = 0; j < cpufreq[cpuidx].nfreq; ) {
		if (cpu_freq == cpufreq[cpuidx].avail_freq[j]) {
			return cpufreq[cpuidx].avail_freq[j];
		}
		j++; 	/* step up to next element to round up *
			 * safe to advance due to bounds checks above here */
		if (cpu_freq < cpufreq[cpuidx].avail_freq[j]) {
			info("Rounding requested frequency %d "
			     "up to next available %d", cpu_freq,
			     cpufreq[cpuidx].avail_freq[j]);
			return cpufreq[cpuidx].avail_freq[j];
		}
	}
	/* loop above must return due to previous bounds checks
	 * but return NO_VAL here anyways to silence compiler warnings */
	return NO_VAL;
}

/*
 * Initialize data structure
 */
static void
_cpu_freq_init_data(int cpx)
{
	/* avail_governors -- set at initialization */
	cpufreq[cpx].org_governor[0] = '\0';
	cpufreq[cpx].new_governor[0] = '\0';
	cpufreq[cpx].org_frequency = NO_VAL;
	cpufreq[cpx].new_frequency = NO_VAL;
	cpufreq[cpx].org_min_freq = NO_VAL;
	cpufreq[cpx].new_min_freq = NO_VAL;
	cpufreq[cpx].org_max_freq = NO_VAL;
	cpufreq[cpx].new_max_freq = NO_VAL;
	cpufreq[cpx].org_set = false;
}
/*
 * Set either current frequency (speed)
 * Or min/max governor base on --cpu-freq parameter
 */
static void
_cpu_freq_setup_data(stepd_step_rec_t *step, int cpx)
{
	uint32_t freq;

	/* If no --cpu-freq, use default governor from conf file. */
	if (step->cpu_freq_gov == NO_VAL)
		step->cpu_freq_gov = slurm_conf.cpu_freq_def;
	if (step->cpu_freq_gov == NO_VAL)
		return;

	/* Get current state */
	if (_cpu_freq_current_state(cpx) == SLURM_ERROR)
		return;

	if (step->cpu_freq_min == NO_VAL &&
	    step->cpu_freq_max != NO_VAL &&
	    step->cpu_freq_gov == NO_VAL) {
		/* Pre version 15.08 behavior */
		freq = _cpu_freq_freqspec_num(step->cpu_freq_max, cpx);
		cpufreq[cpx].new_frequency = freq;
		goto newfreq;
	}
	if (step->cpu_freq_gov == CPU_FREQ_USERSPACE) {
		_cpu_freq_govspec_string(step->cpu_freq_gov, cpx);
		if (step->cpu_freq_max == NO_VAL) {
			return; /* pre version 15.08 behavior. */
		}
		/* Power capping */
		freq = _cpu_freq_freqspec_num(step->cpu_freq_max, cpx);
		cpufreq[cpx].new_frequency = freq;
		cpufreq[cpx].new_min_freq = freq;
		cpufreq[cpx].new_max_freq = freq;
		return;
	}
	if (step->cpu_freq_min != NO_VAL && step->cpu_freq_max != NO_VAL) {
		freq = _cpu_freq_freqspec_num(step->cpu_freq_min, cpx);
		cpufreq[cpx].new_min_freq = freq;
		freq = _cpu_freq_freqspec_num(step->cpu_freq_max, cpx);
		cpufreq[cpx].new_max_freq = freq;
	}

	if (step->cpu_freq_gov != NO_VAL) {
		_cpu_freq_govspec_string(step->cpu_freq_gov, cpx);
	}
newfreq:
	/* Make sure a 'new' frequency is within scaling min/max */
	if (cpufreq[cpx].new_frequency != NO_VAL) {
		if (cpufreq[cpx].new_frequency < cpufreq[cpx].org_min_freq) {
			cpufreq[cpx].new_min_freq = cpufreq[cpx].new_frequency;
		}
		if (cpufreq[cpx].new_frequency > cpufreq[cpx].org_max_freq) {
			cpufreq[cpx].new_max_freq = cpufreq[cpx].new_frequency;
		}
	}
}

/*
 * check an argument against valid governors.
 *
 * Input:  - arg     - string value of governor
 *         - illegal - combination of enums for governors not allowed.
 * Returns - enum of governor found
 * 	   - or 0 if not found
 */
static uint32_t
_cpu_freq_check_gov(const char* arg, uint32_t illegal)
{
	uint32_t rc = 0;
	if (xstrncasecmp(arg, "co", 2) == 0) {
		rc = CPU_FREQ_CONSERVATIVE;
	} else if (xstrncasecmp(arg, "perf", 4) == 0) {
		rc = CPU_FREQ_PERFORMANCE;
	} else if (xstrncasecmp(arg, "pow", 3) == 0) {
		rc = CPU_FREQ_POWERSAVE;
	} else if (xstrncasecmp(arg, "user", 4) == 0) {
		rc = CPU_FREQ_USERSPACE;
	} else if (xstrncasecmp(arg, "onde", 4) == 0) {
		rc = CPU_FREQ_ONDEMAND;
	} else if (xstrncasecmp(arg, "sche", 4) == 0) {
		rc = CPU_FREQ_SCHEDUTIL;
	}
	rc &= (~illegal);
	if (rc == 0)
		return 0;
	return (rc | CPU_FREQ_RANGE_FLAG);
}

/*
 * check an argument for a frequency or frequency synonym.
 *
 * Input:  - arg - string value of frequency
 *
 * Returns - frequency
 *         - enum for synonym
 *         0 on error.
 */
static uint32_t
_cpu_freq_check_freq(const char* arg)
{
	char *end;
	uint32_t frequency;

	if (xstrncasecmp(arg, "lo", 2) == 0) {
		return CPU_FREQ_LOW;
	} else if (xstrncasecmp(arg, "him1", 4) == 0 ||
		   xstrncasecmp(arg, "highm1", 6) == 0) {
		return CPU_FREQ_HIGHM1;
	} else if (xstrncasecmp(arg, "hi", 2) == 0) {
		return CPU_FREQ_HIGH;
	} else if (xstrncasecmp(arg, "med", 3) == 0) {
		return CPU_FREQ_MEDIUM;
	}
	frequency = strtoul(arg, &end, 10);
	if ((*end != '\0') ||
	    ((frequency == 0) && (errno == EINVAL))) {
		error("unrecognized --cpu-freq argument \"%s\"", arg);
		return 0;
	}
	return frequency;
}

/*
 * set cpu frequency if possible for each cpu of the job step
 */
extern void
cpu_freq_set(stepd_step_rec_t *step)
{
	char freq_detail[100];
	uint32_t freq;
	int i, rc;

	if ((!cpu_freq_count) || (!cpufreq))
		return;

	for (i = 0; i < cpu_freq_count; i++) {
		if (cpufreq[i].new_frequency == NO_VAL
		    && cpufreq[i].new_min_freq == NO_VAL
	            && cpufreq[i].new_max_freq == NO_VAL
		    && cpufreq[i].new_governor[0] == '\0')
			continue; /* Nothing to set on this CPU */

		log_flag(CPU_FREQ, "cpu_freq: current_state cpu=%d org_min=%u org_freq=%u org_max=%u org_gpv=%s",
			 i, cpufreq[i].org_min_freq, cpufreq[i].org_frequency,
			 cpufreq[i].org_max_freq, cpufreq[i].org_governor);

		/* Max must be set before min, per
		 * www.kernel.org/doc/Documentation/cpu-freq/user-guide.txt
		 */
		if (cpufreq[i].new_max_freq != NO_VAL ) {
			freq = cpufreq[i].new_max_freq;
			if (cpufreq[i].org_frequency > freq) {
				/* The current frequency is > requested max,
				 * Set it so it is in range
				 * have to go to UserSpace to do it. */
				rc = _cpu_freq_set_gov(step, i, "userspace");
				if (rc == SLURM_ERROR)
					return;
				rc = _cpu_freq_set_scaling_freq(step, i, freq,
						         "scaling_setspeed");
				if (rc == SLURM_ERROR)
					continue;
				if (cpufreq[i].new_governor[0] == '\0') {
					/* Not requesting new gov, so restore */
					rc = _cpu_freq_set_gov(step, i,
						cpufreq[i].org_governor);
					if (rc == SLURM_ERROR)
						continue;
				}
			}
			rc = _cpu_freq_set_scaling_freq(step, i, freq,
							"scaling_max_freq");
			if (rc == SLURM_ERROR)
				continue;
		}
		if (cpufreq[i].new_min_freq != NO_VAL) {
			freq = cpufreq[i].new_min_freq;
			if (cpufreq[i].org_frequency < freq) {
				/* The current frequency is < requested min,
				 * Set it so it is in range
				 * have to go to UserSpace to do it. */
				rc = _cpu_freq_set_gov(step, i, "userspace");
				if (rc == SLURM_ERROR)
					continue;
				rc = _cpu_freq_set_scaling_freq(step, i, freq,
						         "scaling_setspeed");
				if (rc == SLURM_ERROR)
					continue;
				if (cpufreq[i].new_governor[0] == '\0') {
					/* Not requesting new gov, so restore */
					rc= _cpu_freq_set_gov(step, i,
						cpufreq[i].org_governor);
					if (rc == SLURM_ERROR)
						continue;
				}
			}
			rc= _cpu_freq_set_scaling_freq(step, i, freq,
						       "scaling_min_freq");
			if (rc == SLURM_ERROR)
				continue;
		}
		if (cpufreq[i].new_frequency != NO_VAL) {
			if (xstrcmp(cpufreq[i].org_governor,"userspace")) {
				rc = _cpu_freq_set_gov(step, i, "userspace");
				if (rc == SLURM_ERROR)
					continue;
			}
			rc = _cpu_freq_set_scaling_freq(step, i,
					cpufreq[i].new_frequency,
					"scaling_setspeed");
			if (rc == SLURM_ERROR)
				continue;
		}
		if (cpufreq[i].new_governor[0] != '\0') {
			rc = _cpu_freq_set_gov(step, i, cpufreq[i].new_governor);
			if (rc == SLURM_ERROR)
				continue;
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_FREQ) {
			cpu_freq_debug(NULL, NULL,
					freq_detail, sizeof(freq_detail),
					NO_VAL, cpufreq[i].new_min_freq,
					cpufreq[i].new_max_freq,
					cpufreq[i].new_frequency);
			if (cpufreq[i].new_governor[0] != '\0') {
				info("cpu_freq: set cpu=%d %s Governor=%s",
				     i, freq_detail, cpufreq[i].new_governor);
			} else {
				info("cpu_freq: reset cpu=%d %s", i,
				     freq_detail);
			}
		}
	}
}

/*
 * reset the cpus used by the process to their
 * default frequency and governor type
 */
extern void
cpu_freq_reset(stepd_step_rec_t *step)
{
	int i, rc, fd;
	char freq_detail[100];

	if ((!cpu_freq_count) || (!cpufreq))
		return;

	for (i = 0; i < cpu_freq_count; i++) {
		if (cpufreq[i].new_frequency == NO_VAL
		    && cpufreq[i].new_min_freq == NO_VAL
		    && cpufreq[i].new_max_freq == NO_VAL
		    && cpufreq[i].new_governor[0] == '\0')
			continue; /* Nothing to reset on this CPU */

		fd = _test_cpu_owner_lock(i, step->step_id.job_id);
		if (fd < 0)
			continue;

		if (cpufreq[i].new_frequency != NO_VAL) {
			rc = _cpu_freq_set_gov(step, i, "userspace");
			if (rc == SLURM_ERROR)
				continue;
			rc = _cpu_freq_set_scaling_freq(step, i,
					cpufreq[i].org_frequency,
					"scaling_setspeed");
			if (rc == SLURM_ERROR)
				continue;
			cpufreq[i].new_governor[0] = 'u'; /* force gov reset */
		}
		/* Max must be set before min, per
		 * www.kernel.org/doc/Documentation/cpu-freq/user-guide.txt
		 */
		if (cpufreq[i].new_max_freq != NO_VAL) {
			rc = _cpu_freq_set_scaling_freq(step, i,
					cpufreq[i].org_max_freq,
					"scaling_max_freq");
			if (rc == SLURM_ERROR)
				continue;
		}
		if (cpufreq[i].new_min_freq != NO_VAL) {
			rc = _cpu_freq_set_scaling_freq(step, i,
					cpufreq[i].org_min_freq,
					"scaling_min_freq");
			if (rc == SLURM_ERROR)
				continue;
		}
		if (cpufreq[i].new_governor[0] != '\0') {
			rc = _cpu_freq_set_gov(
				step, i, cpufreq[i].org_governor);
			if (rc == SLURM_ERROR)
				continue;
		}

		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_FREQ) {
			cpu_freq_debug(NULL, NULL,
					freq_detail, sizeof(freq_detail),
					NO_VAL, cpufreq[i].org_min_freq,
					cpufreq[i].org_max_freq,
					cpufreq[i].org_frequency);
			if (cpufreq[i].new_governor[0] != '\0') {
				info("cpu_freq: reset cpu=%d %s Governor=%s",
				     i, freq_detail, cpufreq[i].org_governor);
			} else {
				info("cpu_freq: reset cpu=%d %s", i,
				     freq_detail);
			}
		}
	}
}

/* Convert a cpu_freq number to its equivalent string */
extern void
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
	else if (cpu_freq == CPU_FREQ_SCHEDUTIL)
		snprintf(buf, buf_size, "SchedUtil");
	else if (cpu_freq & CPU_FREQ_RANGE_FLAG)
		snprintf(buf, buf_size, "Unknown");
	else if (fuzzy_equal(cpu_freq, NO_VAL)) {
		if (buf_size > 0)
			buf[0] = '\0';
	} else
		convert_num_unit2((double)cpu_freq, buf, buf_size,
				  UNIT_KILO, NO_VAL, 1000, 0);
}

extern char *cpu_freq_to_cmdline(uint32_t min, uint32_t max, uint32_t gov)
{
	char bfgov[32], bfmin[32], bfmax[32];
	char *bfall = NULL;
	bfgov[0] = '\0';
	bfmin[0] = '\0';
	bfmax[0] = '\0';

	/*
	 * Default value from command line is NO_VAL,
	 * Default value from slurmstepd for batch jobs is 0
	 * Convert slurmstepd values to command line ones.
	 */
	if (min == 0)
		min = NO_VAL;
	if (max == 0)
		max = NO_VAL;
	if (gov == 0)
		gov = NO_VAL;

	if ((min == NO_VAL) && (max == NO_VAL) && (gov == NO_VAL))
		return NULL;

	if (min != NO_VAL) {
		if (min & CPU_FREQ_RANGE_FLAG) {
			cpu_freq_to_string(bfmin, sizeof(bfmin), min);
		} else {
			snprintf(bfmin, 32, "%u", min);
		}
	}
	if (max != NO_VAL) {
		if (max & CPU_FREQ_RANGE_FLAG) {
			cpu_freq_to_string(bfmax, sizeof(bfmax), max);
		} else {
			snprintf(bfmax, 32, "%u", max);
		}
	}
	if (gov != NO_VAL) {
		cpu_freq_to_string(bfgov, sizeof(bfgov), gov);
	}
	if ((min != NO_VAL) && (max != NO_VAL) && (gov != NO_VAL)) {
		xstrfmtcat(bfall, "%s-%s:%s", bfmin, bfmax, bfgov);
	} else if ((min != NO_VAL) && (max != NO_VAL)) {
		xstrfmtcat(bfall, "%s-%s", bfmin, bfmax);
	} else if (max != NO_VAL) {
		xstrcat(bfall, bfmax);
	} else if (gov != NO_VAL) {
		xstrcat(bfall, bfgov);
	}

	return bfall;
}

/*
 * Set environment variables associated with the frequency variables.
 */
extern int cpu_freq_set_env(char *var, uint32_t min, uint32_t max,
			    uint32_t gov)
{
	char *bfall = cpu_freq_to_cmdline(min, max, gov);
	if (bfall && setenvf(NULL, var, "%s", bfall)) {
		xfree(bfall);
		error("Unable to set %s", var);
		return SLURM_ERROR;
	}
	xfree(bfall);
	return SLURM_SUCCESS;
}

/* Convert a composite cpu governor enum to its equivalent string
 *
 * Input:  - buf   - buffer to contain string
 *         - bufsz - size of buffer
 *         - gpvs  - composite enum of governors
 */
extern void
cpu_freq_govlist_to_string(char* buf, uint16_t bufsz, uint32_t govs)
{
	char *list = NULL;
	char *sep = "", *pos = NULL;

	if ((govs & CPU_FREQ_CONSERVATIVE) == CPU_FREQ_CONSERVATIVE) {
		xstrfmtcatat(list, &pos, "%s%s", sep, "Conservative");
		sep = ",";
	}
	if ((govs & CPU_FREQ_ONDEMAND) == CPU_FREQ_ONDEMAND) {
		xstrfmtcatat(list, &pos, "%s%s", sep, "OnDemand");
		sep = ",";
	}
	if ((govs & CPU_FREQ_PERFORMANCE) == CPU_FREQ_PERFORMANCE) {
		xstrfmtcatat(list, &pos, "%s%s", sep, "Performance");
		sep = ",";
	}
	if ((govs & CPU_FREQ_POWERSAVE) == CPU_FREQ_POWERSAVE) {
		xstrfmtcatat(list, &pos, "%s%s", sep, "PowerSave");
		sep = ",";
	}
	if ((govs & CPU_FREQ_SCHEDUTIL) == CPU_FREQ_SCHEDUTIL) {
		xstrfmtcatat(list, &pos, "%s%s", sep, "SchedUtil");
		sep = ",";
	}
	if ((govs & CPU_FREQ_USERSPACE) == CPU_FREQ_USERSPACE) {
		xstrfmtcatat(list, &pos, "%s%s", sep, "UserSpace");
		sep = ",";
	}

	if (list) {
		strlcpy(buf, list, bufsz);
		xfree(list);
	} else {
		strlcpy(buf, "No Governors defined", bufsz);
	}
}

/*
 * Verify slurm.conf CpuFreqDef option
 *
 * Input:  - arg  - governor/frequency value to check:
 * 		    valid governor, low, medium, highm1, high,
 * 		    or numeric frequency
 *	   - freq - pointer to corresponding enum or numeric value
 * Returns - -1 on error, else 0
 */
extern int
cpu_freq_verify_def(const char *arg, uint32_t *freq)
{
	uint32_t cpufreq = 0;

	cpufreq = _cpu_freq_check_gov(arg, CPU_FREQ_USERSPACE);
	if (cpufreq) {
		debug3("cpu_freq_verify_def: %s set", arg);
		*freq = cpufreq;
		return 0;
	}
	error("%s: CpuFreqDef=%s invalid", __func__, arg);
	return -1;
}

/*
 * Verify slurm.conf CpuFreqGovernors list
 *
 * Input:  - arg  - string list of governors
 *	   - govs - pointer to composite of enum for each governor in list
 * Returns - -1 on error, else 0
 */
extern int
cpu_freq_verify_govlist(const char *arg, uint32_t *govs)
{
	char *list, *gov, *savestr = NULL;
	uint32_t agov;

	*govs = 0;
	if (arg == NULL) {
		error("cpu_freq_verify_govlist: governor list is empty");
		return -1;
	}

	list = xstrdup(arg);
	if ( (gov = strtok_r(list, ",", &savestr) ) == NULL) {
		error("cpu_freq_verify_govlist: governor list '%s' invalid",
				arg);
		return -1;
	}
	do {
		debug3("cpu_freq_verify_govlist: gov = %s", gov);
		agov = _cpu_freq_check_gov(gov, 0);
		if (agov == 0) {
			error("cpu_freq_verify_govlist: governor '%s' invalid",
				gov);
			return -1;
		}
		*govs |= agov;
	} while ( (gov = strtok_r(NULL, ",", &savestr) ) != NULL);
	xfree(list);
	return 0;
}

/*
 * Verify cpu_freq command line option
 *
 * --cpu-freq=arg
 *   where arg is p1[-p2][:p3]
 *
 * - p1 can be  [#### | low | medium | high | highm1]
 * 	which will set the current frequency, and set the governor to
 * 	UserSpace.
 * - p1 can be [Conservative | OnDemand | Performance | PowerSave | UserSpace]
 *      which will set the governor to the corresponding value.
 * - When p2 is present, p1 will be the minimum frequency and p2 will be
 *   the maximum. The governor cannot be UserSpace, so CpuFreqDef must be set in
 *   slurm.conf if there's no p3.
 * - p2 can be  [#### | medium | high | highm1] p2 must be greater than p1.
 * - If the current frequency is < min, it will be set to min.
 *   Likewise, if the current frequency is > max, it will be set to max.
 * - p3 can be [Conservative | OnDemand | Performance | PowerSave | UserSpace]
 *   which will set the governor to the corresponding value.
 *   When p3 is UserSpace, p2 must be empty.
 *   p2 will have been set by PowerCapping.
 *
 * returns -1 on error, 0 otherwise
 */
extern int
cpu_freq_verify_cmdline(const char *arg,
			uint32_t *cpu_freq_min,
			uint32_t *cpu_freq_max,
			uint32_t *cpu_freq_gov)
{
	char *poscolon, *posdash;
	char *p1=NULL, *p2=NULL, *p3=NULL;
	uint32_t frequency;
	int rc = 0;

	if (arg == NULL || cpu_freq_min == NULL || cpu_freq_max == NULL
			|| cpu_freq_gov == NULL) {
		return -1;
	}
	*cpu_freq_min = NO_VAL;
	*cpu_freq_max = NO_VAL;
	*cpu_freq_gov = NO_VAL;
	poscolon = strchr(arg,':');
	if (poscolon) {
		p3 = xstrdup((poscolon+1));
	}
	posdash = strchr(arg,'-');
	if (posdash) {
		p1 = xstrndup(arg, (posdash-arg));
		if (poscolon) {
			p2 = xstrndup((posdash+1), ((poscolon-posdash)-1));
		} else {
			p2 = xstrdup((posdash+1));
		}
	} else {
		if (poscolon) {
			p1 = xstrndup(arg, (poscolon-arg));
		} else {
			p1 = xstrdup(arg);
		}
	}

	frequency = _cpu_freq_check_gov(p1, 0);
	if (frequency != 0) {
		if (p3) {
			error("governor cannot be specified twice "
			      "%s{-}:%s in --cpu-freq", p1, p3);
			rc = -1;
			goto clean;
		}
		*cpu_freq_gov = frequency;
	} else {
		frequency = _cpu_freq_check_freq(p1);
		if (frequency == 0) {
			rc = -1;
			goto clean;
		}
		*cpu_freq_max = frequency;
	}
	if (p2) {
		if (!p3 && (slurm_conf.cpu_freq_def == NO_VAL)) {
			/*
			 * If the user specified a range without a governor,
			 * (even if userspace is not set), we won't accept the
			 * request. We don't know how the cpus are set and we
			 * won't decide which one to set for the user. Note that
			 * a range is valid for multiple governors.
			 */
			error("You must explicitly choose a governor when defining a range. Please specify only one value for the desired frequency (p1) or choose a specific governor (p3).");
			rc = -1;
			goto clean;
		}
		frequency = _cpu_freq_check_freq(p2);
		if (frequency == 0) {
			rc = -1;
			goto clean;
		}
		*cpu_freq_min = *cpu_freq_max;
		*cpu_freq_max = frequency;
		if (*cpu_freq_max < *cpu_freq_min) {
			error("min cpu-frec (%s) must be < max cpu-freq (%s)",
			      p1, p2);
			rc = -1;
			goto clean;
		}
	}
	if (p3) {
		frequency = _cpu_freq_check_gov(p3, 0);
		if (frequency == 0) {
			error("illegal governor: %s on --cpu-freq", p3);
			rc = -1;
			goto clean;
		}
		if (!p2) {
			if (frequency != CPU_FREQ_USERSPACE) {
				error("gov on cpu-frec (%s) illegal without max",
				      p3);
				rc = -1;
				goto clean;
			}
		} else {
			if (frequency == CPU_FREQ_USERSPACE) {
				error("%s governor does not support a range. Please specify only one value for the desired frequency (p1) or choose a different governor.",
				      p3);
				rc = -1;
				goto clean;
			}
		}
		*cpu_freq_gov = frequency;
	} else if (p2 && (*cpu_freq_gov == NO_VAL) &&
		   (slurm_conf.cpu_freq_def != NO_VAL)) {
		/*
		 * No governor specified and a range is specified.
		 * Use slurm.conf CpuFreqDef if defined. Note that this cannot
		 * be UserSpace.
		 */
		*cpu_freq_gov = slurm_conf.cpu_freq_def;
	}

	/* Also force this in case we specify just one frequency. */
	if ((*cpu_freq_gov == NO_VAL) && !p2 && !p3)
		*cpu_freq_gov = CPU_FREQ_USERSPACE;

clean:
	if (*cpu_freq_gov != NO_VAL) {
		if (((*cpu_freq_gov & slurm_conf.cpu_freq_govs)
		    & ~CPU_FREQ_RANGE_FLAG) == 0) {
			error("governor of %s is not allowed in slurm.conf",
			      arg);
			*cpu_freq_gov = NO_VAL;
			rc = -1;
		}
	}
	if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_FREQ) {
		cpu_freq_debug("command", "NO_VAL", NULL, 0,
			       *cpu_freq_gov, *cpu_freq_min,
			       *cpu_freq_max, NO_VAL);
	}
	xfree(p1);
	xfree(p2);
	xfree(p3);
	return rc;

}

/*
 * Convert frequency parameters to strings
 * Typically called to produce string for a log or reporting utility.
 *
 * When label!=NULL, info message is put to log. This is convenient for
 *      inserting debug calls to verify values in structures or messages.
 * noval_str==NULL allows missing parameters not to be reported.
 * freq_str is a buffer to hold the composite string for all input values.
 * freq_len is length of freq_str
 * gov is a governor value
 * min is a minimum value
 * max is a maximum value
 * freq is a (current) frequency value.
 *
 * Returns 0 if all parameters are NO_VAL (or 0)
 */
extern int
cpu_freq_debug(char* label, char* noval_str, char* freq_str, int freq_len,
		  uint32_t gov, uint32_t min, uint32_t max, uint32_t freq)
{
	int rc = 0;
	char bfgov[64], bfmin[32], bfmax[32], bffreq[32];
	char *sep1 = " ", *sep2 = " ", *sep3 = " ";

	bfgov[0] = '\0';
	bfmin[0] = '\0';
	bfmax[0] = '\0';
	bffreq[0] = '\0';

	if (freq != NO_VAL && freq != 0) {
		rc = 1;
		sprintf(bffreq, "cur_freq=%u", freq);
	} else {
		sep1 = "";
	}
	if ((min != NO_VAL) && (min != 0)) {
		rc = 1;
		if (min & CPU_FREQ_RANGE_FLAG) {
			strcpy(bfmin, "CPU_min_freq=");
			cpu_freq_to_string(&bfmin[13], (sizeof(bfmin)-13), min);
		} else {
			sprintf(bfmin, "CPU_min_freq=%u", min);
		}
	} else if (noval_str) {
		if (strlen(noval_str) >= sizeof(bfmin)) {
			error("%s: minimum CPU frequency string too large",
			      __func__);
		} else {
			strlcpy(bfmin, noval_str, sizeof(bfmin));
		}
	} else {
		sep2 = "";
	}
	if ((max != NO_VAL) && (max != 0)) {
		rc = 1;
		if (max & CPU_FREQ_RANGE_FLAG) {
			strcpy(bfmax, "CPU_max_freq=");
			cpu_freq_to_string(&bfmax[13], (sizeof(bfmax)-13), max);
		} else {
			sprintf(bfmax, "CPU_max_freq=%u", max);
		}
	} else if (noval_str) {
		if (strlen(noval_str) >= sizeof(bfmax)) {
			error("%s: maximum CPU frequency string too large",
			      __func__);
		} else {
			strlcpy(bfmax, noval_str, sizeof(bfmax));
		}
	} else {
		sep3 = "";
	}
	if ((gov != NO_VAL) && (gov != 0)) {
		rc = 1;
		strcpy(bfgov, "Governor=");
		cpu_freq_to_string(&bfgov[9], (sizeof(bfgov)-9), gov);
	} else if (noval_str) {
		if (strlen(noval_str) >= sizeof(bfgov)) {
			error("%s: max CPU governor string too large",
			      __func__);
		} else {
			strlcpy(bfgov, noval_str, sizeof(bfgov));
		}
	}
	if (rc) {
		if (freq_str) {
			snprintf(freq_str, freq_len, "%s%s%s%s%s%s%s",
				 bffreq, sep1, bfmin, sep2, bfmax, sep3, bfgov);
		}
	} else {
		if (freq_str)
			freq_str[0] = '\0';
	}
	if (label) {
		info("cpu-freq: %s :: %s%s%s%s%s%s%s", label,
		     bffreq, sep1, bfmin, sep2, bfmax, sep3, bfgov);
	}
	return rc;
}
