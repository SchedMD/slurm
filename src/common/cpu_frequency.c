/*****************************************************************************\
 *  cpu_frequency.c - support for srun option --cpu-freq=<frequency>
 *****************************************************************************
 *  Copyright (C) 2012 Bull
 *  Written by Don Albert, <don.albert@bull.com>
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
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>

#include "slurm/slurm.h"
#include "src/common/xcpuinfo.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/cpu_frequency.h"



#define PATH_TO_CPU "/sys/devices/system/cpu/"
#define LINE_LEN 100
#define SYSFS_PATH_MAX 255
#define FREQ_LIST_MAX 16
#define GOV_NAME_LEN 24

static uint16_t cpu_freq_count = 0;
static struct cpu_freq_data {
	uint32_t frequency_to_set;
	uint32_t reset_frequency;
	char     reset_governor[GOV_NAME_LEN];
} * cpufreq = NULL;

static void _cpu_freq_find_valid(uint32_t cpu_freq, int cpuidx);
static uint16_t _cpu_freq_next_cpu(char **core_range, uint16_t *cpuidx,
				   uint16_t *start, uint16_t *end);



/*
 * called to check if the node supports setting cpu frequency
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

	info("Gathering cpu frequency information for %u cpus", cpu_freq_count);
	for (i = 0; i < cpu_freq_count; i++) {

		cpufreq[i].frequency_to_set  = 0;
		cpufreq[i].reset_frequency = 0;

		snprintf(path, sizeof(path),
			 PATH_TO_CPU
			 "cpu%u/cpufreq/scaling_available_governors", i);
		if ( ( fp = fopen(path, "r") ) == NULL )
			continue;
		if (fgets(value, LINE_LEN, fp) == NULL) {
			fclose(fp);
			continue;
		}
		if (strstr(value, "userspace") == NULL) {
			fclose(fp);
			continue;
		}
		fclose(fp);

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor", i);
		if ( ( fp = fopen(path, "r") ) == NULL )
			continue;
		if (fgets(value, LINE_LEN, fp) == NULL) {
			fclose(fp);
			continue;
		}
		if (strlen(value) >= GOV_NAME_LEN) {
			fclose(fp);
			continue;
		}
		strcpy(cpufreq[i].reset_governor, value);
		fclose(fp);
		j = strlen(cpufreq[i].reset_governor);
		if ((j > 0) && (cpufreq[i].reset_governor[j - 1] == '\n'))
			cpufreq[i].reset_governor[j - 1] = '\0';

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_min_freq", i);
		if ( ( fp = fopen(path, "r") ) == NULL )
			continue;
		if (fscanf (fp, "%u", &cpufreq[i].reset_frequency) < 0) {
			error("cpu_freq_cgroup_valid: Could not read "
			      "scaling_min_freq");
		}
		fclose(fp);

		debug("cpu_freq_init: cpu %u, reset freq: %u, "
		      "reset governor: %s",
		      i,cpufreq[i].reset_frequency,cpufreq[i].reset_governor);
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
	error("Unable to send cpu frequency information for %u cpus",
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
		info("Received cpu frequency information for %u cpus",
		     cpu_freq_count);
	}
	return;
rwfail:
	error("Unable to recv cpu frequency information for %u cpus",
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
cpu_freq_cpuset_validate(slurmd_job_t *job)
{
	int cpuidx;
	bitstr_t *cpus_to_set;
	bitstr_t *cpu_map;
	char *cpu_bind;
	char *cpu_str;
	char *savestr = NULL;

	debug2("cpu_freq_cpuset_validate: request = %12d  %8x",
	       job->cpu_freq, job->cpu_freq);
	debug2("  jobid=%u, stepid=%u, tasks=%u cpu/task=%u, cpus=%u",
	     job->jobid, job->stepid, job->node_tasks,
	       job->cpus_per_task,job->cpus);
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

		if (bit_unfmt_hexmask(cpu_map, cpu_str) == -1) {
			error("cpu_freq_cpuset_validate: invalid cpu mask %s",
			      cpu_bind);
			bit_free(cpu_map);
			bit_free(cpus_to_set);
			xfree(cpu_bind);
			return;
		}
		bit_or(cpus_to_set, cpu_map);
	} while ( (cpu_str = strtok_r(NULL, ",", &savestr) ) != NULL);

	for (cpuidx=0; cpuidx < cpu_freq_count; cpuidx++) {
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
cpu_freq_cgroup_validate(slurmd_job_t *job, char *step_alloc_cores)
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
 * sets "frequency_to_set" table entry if valid value found
 */
void
_cpu_freq_find_valid(uint32_t cpu_freq, int cpuidx)
{
	unsigned int j, freq_med = 0;
	uint32_t  freq_list[FREQ_LIST_MAX] =  { 0 };
	char path[SYSFS_PATH_MAX];
	FILE *fp;

	/* see if user requested "high" "medium" or "low"  */
	if (cpu_freq & CPU_FREQ_RANGE_FLAG) {

		switch(cpu_freq)
		{
		case CPU_FREQ_LOW :
			/* get the value from scale min freq */
			snprintf(path, sizeof(path),
				 PATH_TO_CPU 
				 "cpu%u/cpufreq/scaling_min_freq", cpuidx);
			if ( ( fp = fopen(path, "r") ) == NULL ) {
				error("cpu_freq_cgroup_valid: Could not open "
				      "scaling_min_freq");
				return;
			}
			if (fscanf (fp, "%u",
				    &cpufreq[cpuidx].frequency_to_set) < 1) {
				error("cpu_freq_cgroup_valid: Could not read "
				      "scaling_min_freq");
			}
			break;


		case CPU_FREQ_MEDIUM :
			snprintf(path, sizeof(path),
				 PATH_TO_CPU
				 "cpu%u/cpufreq/scaling_available_frequencies",
				 cpuidx);
			if ( ( fp = fopen(path, "r") ) == NULL ) {
				error("cpu_freq_cgroup_valid: Could not open "
				      "scaling_available_frequencies");
				return;
			}
			for (j = 0; j < FREQ_LIST_MAX; j++) {
				if ( fscanf(fp, "%u", &freq_list[j]) == EOF)
					break;
				freq_med = (j + 1) / 2;
			}
			cpufreq[cpuidx].frequency_to_set = freq_list[freq_med];
			break;


		case CPU_FREQ_HIGH :
			/* get the value from scale max freq */
			snprintf(path, sizeof(path),
				 PATH_TO_CPU "cpu%u/cpufreq/scaling_max_freq",
				 cpuidx);
			if ( ( fp = fopen(path, "r") ) == NULL ) {
				error("cpu_freq_cgroup_valid: Could not open "
				      "scaling_max_freq");
				return;
			}
			if (fscanf (fp, "%u",
				    &cpufreq[cpuidx].frequency_to_set) < 1) {
				error("cpu_freq_cgroup_valid: Could not read "
				      "scaling_max_freq");
			}
			break;

		default :
			error("cpu_freq_cgroup_valid: "
			      "invalid cpu_freq value %u", cpu_freq);
			return;
		}
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
				cpufreq[cpuidx].frequency_to_set = freq_list[j];
				break;
			}
			if (j > 0) {
				if (freq_list[j] > freq_list[j-1] ) {
					/* ascending order */
					if ((cpu_freq > freq_list[j-1]) &&
					    (cpu_freq < freq_list[j])) {
						cpufreq[cpuidx].frequency_to_set = 
							freq_list[j];
						break;
					}
				} else {
					/* descending order */
					if ((cpu_freq > freq_list[j]) &&
					    (cpu_freq < freq_list[j-1])) {
						cpufreq[cpuidx].frequency_to_set = 
							freq_list[j];
						break;
					}
				}
			}
		}
		fclose(fp);
	}

	debug3("cpu_freq_cgroup_validate: cpu %u, frequency to set: %u",
	       cpuidx, cpufreq[cpuidx].frequency_to_set);

	return;
}


/*
 * verify cpu_freq parameter
 *
 * in addition to a numeric frequency value, we allow the user
 * to specify "low", "medium", or "high" frequency
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
	} else if (strncasecmp(arg, "hi", 2) == 0) {
		*cpu_freq = CPU_FREQ_HIGH;
		return 0;
	} else if (strncasecmp(arg, "med", 3) == 0) {
		*cpu_freq = CPU_FREQ_MEDIUM;
		return 0;
	}

	error("unrecognized --cpu-freq argument \"%s\"", arg);
	return -1;
}


/*
 * set cpu frequency if possible for each cpu of the job step
 */
void
cpu_freq_set(slurmd_job_t *job)
{
	char path[SYSFS_PATH_MAX];
	FILE *fp;
	char value[LINE_LEN];
	unsigned int i,j;

	if ((!cpu_freq_count) || (!cpufreq))
		return;

	j = 0;
	for (i = 0; i < cpu_freq_count; i++) {

		if (cpufreq[i].frequency_to_set == 0)
			continue;

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor", i);
		if ( ( fp = fopen(path, "w") ) == NULL )
			continue;
		fputs("userspace\n", fp);
		fclose(fp);

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_setspeed", i);
		snprintf(value, LINE_LEN, "%u", cpufreq[i].frequency_to_set);

		if ( ( fp = fopen(path, "w") ) == NULL )
			continue;
		fputs(value, fp);
		fclose(fp);

		j++;
		debug2("cpu_freq_set: cpu %u, frequency: %u",
		       i,cpufreq[i].frequency_to_set);
	}
	debug("cpu_freq_set: #cpus set = %u", j);
}

/*
 * reset the cpus used by the process to their
 * default frequency and governor type
 */
void
cpu_freq_reset(slurmd_job_t *job)
{
	char path[SYSFS_PATH_MAX];
	FILE *fp;
	char value[LINE_LEN];
	unsigned int i, j;

	if ((!cpu_freq_count) || (!cpufreq))
		return;

	j = 0;
	for (i = 0; i < cpu_freq_count; i++) {

		if (cpufreq[i].frequency_to_set == 0)
			continue;

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_setspeed", i);
		snprintf(value, LINE_LEN, "%u", cpufreq[i].reset_frequency);

		if ( ( fp = fopen(path, "w") ) == NULL )
			continue;
		fputs(value, fp);
		fclose(fp);

		snprintf(path, sizeof(path),
			 PATH_TO_CPU "cpu%u/cpufreq/scaling_governor", i);
		if ( ( fp = fopen(path, "w") ) == NULL )
			continue;
		fputs(cpufreq[i].reset_governor, fp);
		fputc('\n', fp);
		fclose(fp);

		j++;
		debug3("cpu_freq_reset: "
		       "cpu %u, frequency reset: %u, governor reset: %s",
		       i,cpufreq[i].reset_frequency,cpufreq[i].reset_governor);
	}
	debug("cpu_freq_reset: #cpus reset = %u", j);
}
