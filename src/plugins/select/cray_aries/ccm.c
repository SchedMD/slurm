/*****************************************************************************\
 *  ccm.c - Cray CCM app ssh launch over the Aries interconnect; node
 *  selection plugin for cray systems.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
 *  Copyright 2016 Hewlett Packard Enterprise Development LP
 *  Written by Marlys Kohnke
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

#define _GNU_SOURCE		/* needed for getline() */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"

#include "src/common/env.h"
#include "src/common/pack.h"
#include "src/common/select.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"

#include "ccm.h"

/* CCM use */
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/srun_comm.h"

const char *ccm_prolog_path;
const char *ccm_epilog_path;
ccm_config_t ccm_config;

static char *_get_ccm_partition(ccm_config_t *ccm_config);
static int _parse_ccm_config(char *entry, char **ccm_partition);
static void _free_ccm_info(ccm_info_t *ccm_info);
static int *_ccm_convert_nodelist(char *nodelist, int *node_cnt);
static char * _ccm_create_unique_file(char *uniqnm, int *fd,
				      ccm_info_t *ccm_info);
static char *_ccm_create_nidlist_file(ccm_info_t *ccm_info);
static int _run_ccm_prolog_epilog(ccm_info_t *ccm_info, char *ccm_type,
				  const char *ccm_script);

/*
 * Open the CCM config file and read the CCM_QUEUES list of partition
 * name(s).  Store the values in the global ccm_partition array.
 * This is done once per slurmctld startup.
 *
 * Upon success, returns NULL; otherwise, an error string is returned.
 */
static char *_get_ccm_partition(ccm_config_t *ccm_config)
{
	FILE *fp;
	size_t num_read, len;
	char *entry = NULL, *err_mesg = NULL, extra[2];
	static char err_buf[256];
	int i, num_ents = 0;

	ccm_config->num_ccm_partitions = 0;
	fp = fopen(CCM_CONF_PATH, "r");
	if (fp == NULL) {
		snprintf(err_buf, sizeof(err_buf),
			 "CCM unable to open %s, %m\n", CCM_CONF_PATH);
		err_mesg = err_buf;
		return err_mesg;
	}
	while ((num_read = getline(&entry, &len, fp)) != -1) {
		if (entry) {
			if (entry[num_read - 1] == '\n') {
				entry[num_read - 1] = '\0';
			}
			if (xstrcasestr(entry, "CCM_QUEUES") == 0) {
				continue;
			}
			/* Ignore a comment line */
			if (sscanf(entry, " %1[""#""]", extra) == 1) {
				continue;
			}
			num_ents = _parse_ccm_config(entry,
						     ccm_config->ccm_partition);
			if (num_ents <= 0) {
				snprintf(err_buf, sizeof(err_buf),
					 "CCM bad CCM_QUEUES %s in %s\n",
					 entry, CCM_CONF_PATH);
				err_mesg = err_buf;
				free(entry);
				return err_mesg;
			}
			ccm_config->num_ccm_partitions = num_ents;
			break;
		}
	}
	debug2("CCM _get_ccm_partition num_ents %d",
	       ccm_config->num_ccm_partitions);
	if (ccm_config->num_ccm_partitions > 0) {
		for (i = 0; i < ccm_config->num_ccm_partitions; i++) {
			debug2("CCM ccm_config->ccm_partition[%d] %s", i,
			       ccm_config->ccm_partition[i]);
		}
	}
	free(entry);
	return err_mesg;
}

/*
 * Parse the CCM_QUEUES entry within the CCM config file.
 * CCM_QUEUES value is a string containing one or more partition names,
 * such as CCM_QUEUES="ccm_queue, ccm_queue_2"
 *
 * Upon success, the ccm_partitions array is filled in and the
 * number of partition names is returned.  For any error, -1 is returned.
 */
static int _parse_ccm_config(char *entry, char **ccm_partition)
{
	int num_ents = -1, i = 0, token_sz = 0;
	char *part_list, *token, *saveptr, *tmp;
	char const *delims = " \t\n\v\f\r,"; /* whitespace + comma */

	part_list = NULL;
	/* Find start of tokens indicated by first " */
	part_list = (strchr(entry, '"'));
	if (part_list == NULL) {
		debug("CCM part_list invalid config entry %s", entry);
		return num_ents;
	}
	/* Move past initial " and ignore anything after the end matching " */
	tmp = strchr(part_list + 1, '"');
	if (tmp == NULL) {
		debug("CCM tmp invalid config entry %s", part_list + 1);
		return num_ents;
	}
	*tmp = '\0';
	token = strtok_r(part_list + 1, delims, &saveptr);
	while ((token != NULL) && (i < CCM_PARTITION_MAX)) {
		token_sz = strlen(token);
		/* Strip off an ending " */
		if (token[token_sz - 1] == '"') {
			token[token_sz - 1] = '\0';
		}
		if (strlen(token) > 0) {
			ccm_partition[i] = xmalloc(token_sz + 1);
			strcpy(ccm_partition[i], token);
			i++;
		}
		token = strtok_r(NULL, delims, &saveptr);
	}
	num_ents = i;
	return num_ents;
}

/*
 * Free the malloc'd fields within a ccm_info structure.
 */
static void _free_ccm_info(ccm_info_t *ccm_info)
{
	xfree(ccm_info->cpu_count_reps);
	xfree(ccm_info->cpus_per_node);
	xfree(ccm_info->nodelist);
	return;
}

/*
 * Take an input string of hostnames (i.e. nid00050) and convert that string
 * into an array of integers (i.e. 50).  For success, returns an integer array;
 * for an error, returns NULL.  For success, nid_cnt is set to the number of
 * array entries; otherwise, nid_cnt is set to -1.
 */
static int * _ccm_convert_nodelist(char *nodelist, int *nid_cnt)
{
	hostlist_t hl;
	int i, cnt = -1, *nid_array = NULL;
	char *nidname, *nidstr = NULL;

	if (!(hl = hostlist_create(nodelist))) {
		CRAY_ERR("CCM hostlist_create error on %s", nodelist);
		return NULL;
	}
	if (!(cnt = hostlist_count(hl))) {
		CRAY_ERR("CCM nodelist %s hostlist_count cnt %d",
			 nodelist, cnt);
		hostlist_destroy(hl);
		return NULL;
	}
	i = 0;
	nid_array = xmalloc(cnt * sizeof(int32_t));
	while ((nidname = hostlist_shift(hl))) {
		if (!(nidstr = strpbrk(nidname, "0123456789"))) {
			CRAY_ERR("CCM unexpected format nidname %s", nidname);
			free(nidname);
			xfree(nid_array);
			hostlist_destroy(hl);
			return NULL;
		}
		nid_array[i++] = atoi(nidstr);
		free(nidname);
	}
	hostlist_destroy(hl);

	*nid_cnt = cnt;
	return nid_array;
}

/*
 * Create a unique nidlist file.  The file name is returned or
 * NULL for any error.  For success, the assigned fd is returned; otherwise,
 * uniq_fd is set to -1.  The caller is responsible to free the returned
 * file name.
 */
static char * _ccm_create_unique_file(char *uniqnm, int *uniq_fd,
				      ccm_info_t *ccm_info)
{
	int fd = -1;
	char *tmpfilenm = NULL;

	/*
	 * Create a unique temp file; name of the file will be passed
	 * in an env variable to the CCM prolog/epilog.
	 */
	*uniq_fd = -1;
	tmpfilenm = xstrdup(uniqnm);
	fd = mkstemp(tmpfilenm);
	if (fd < 0) {
		CRAY_ERR("CCM job %u unable to mkstemp %s, %m",
			 ccm_info->job_id, uniqnm);
	} else if ((fchmod(fd, 0644)) < 0) {
		CRAY_ERR("CCM job %u file %s, fd %d, fchmod error, %m",
			 ccm_info->job_id, uniqnm, fd);
		close(fd);
		fd = -1;
	}
	if (fd < 0) {
		xfree(tmpfilenm);
	} else {
		*uniq_fd = fd;
	}
	return tmpfilenm;
}

/*
 * Fill in a nodelist file with one nid entry per PE (exec'd app process).
 * For 2 PEs running on nid 36 and 1 on nid 100, the file contents are:
 * 36
 * 36
 * 100
 *
 * The name of the unique nodelist file is returned; NULL is returned
 * for any error.  The caller is responsible to free the returned file name.
 */
static char *_ccm_create_nidlist_file(ccm_info_t *ccm_info)
{
	int i, j, fd = -1, nodecnt = 0, *nodes = NULL;
	char *unique_filenm = NULL;
	FILE *tmp_fp = NULL;
	slurm_step_layout_t *step_layout = NULL;
	slurm_step_layout_req_t step_layout_req;
	uint16_t cpus_per_task_array[1];
	uint32_t cpus_task_reps[1];

	/*
	 * Create a unique temp file; name of the file will be passed
	 * in an env variable to the CCM prolog/epilog.
	 */
	unique_filenm = _ccm_create_unique_file(CCM_CRAY_UNIQUE_FILENAME, &fd,
						ccm_info);
	if (!unique_filenm) {
		return NULL;
	}
	if ((tmp_fp = fdopen(fd, "w")) == NULL) {
		CRAY_ERR("CCM job %u file %s, fd %d, fdopen error %m",
			 ccm_info->job_id, unique_filenm, fd);
		close(fd);
		xfree(unique_filenm);
		return NULL;
	}
	/* Convert the nodelist into an array of nids */
	nodes = _ccm_convert_nodelist(ccm_info->nodelist, &nodecnt);
	debug("CCM job %u nodelist %s, nodecnt %d", ccm_info->job_id,
	      ccm_info->nodelist, nodecnt);
	if (!nodes) {
		fclose(tmp_fp);
		xfree(unique_filenm);
		return NULL;
	}
	for (i = 0; i < nodecnt; i++) {
		debug3("CCM job %u nodes[%d] is %d",
		       ccm_info->job_id, i, nodes[i]);
	}

	memset(&step_layout_req, 0, sizeof(slurm_step_layout_req_t));

	step_layout_req.node_list = ccm_info->nodelist;
	step_layout_req.cpus_per_node = ccm_info->cpus_per_node;
	step_layout_req.cpu_count_reps = ccm_info->cpu_count_reps;
	step_layout_req.num_hosts = ccm_info->node_cnt;
	step_layout_req.num_tasks = ccm_info->num_tasks;

	cpus_per_task_array[0] = ccm_info->cpus_per_task;
	cpus_task_reps[0] = step_layout_req.num_hosts;

	step_layout_req.cpus_per_task = cpus_per_task_array;
	step_layout_req.cpus_task_reps = cpus_task_reps;
	step_layout_req.task_dist = ccm_info->task_dist;
	step_layout_req.plane_size = ccm_info->plane_size;
	/* Determine how many PEs(tasks) will be run on each node */
	step_layout = slurm_step_layout_create(&step_layout_req);
	if (!step_layout) {
		CRAY_ERR("CCM job %u slurm_step_layout_create failure",
			 ccm_info->job_id);
		fclose(tmp_fp);
		xfree(unique_filenm);
		xfree(nodes);
		return NULL;
	}
	debug2("CCM job %u step_layout node_cnt %d", ccm_info->job_id,
	       step_layout->node_cnt);
	/* Fill in the nodelist file with an entry per PE */
	for (i = 0; i < step_layout->node_cnt; i++) {
		debug2("CCM job %u step_layout nodes[%d] %d, tasks[%d] %d",
		       ccm_info->job_id, i, nodes[i], i,
		       step_layout->tasks[i]);
		for (j = 0; j < step_layout->tasks[i]; j++) {
			fprintf(tmp_fp, "%d\n", nodes[i]);
			debug3("CCM job %u nodelist file step tasks[%d] %d, "
			       "j %d nodes[%d] %d", ccm_info->job_id, i,
			       step_layout->tasks[i], j, i, nodes[i]);
		}
	}
	slurm_step_layout_destroy(step_layout);
	fclose(tmp_fp);
	xfree(nodes);
	debug2("CCM job %u unique_filemn %s", ccm_info->job_id, unique_filenm);
	return unique_filenm;
}

/*
 * Set up the appropriate environment to run the CCM prolog and epilog
 * scripts.  Fork a child to exec the script; wait for the child to complete.
 * Return value of kill so caller can decide what further action to take.
 */
static int _run_ccm_prolog_epilog(ccm_info_t *ccm_info, char *ccm_type,
				  const char *ccm_script)
{
	int cpid, ret, status, wait_rc, kill = 1;
	char *nid_list_file = NULL;
	const char *argv[4];
	DEF_TIMERS;

	START_TIMER;
	if (!xstrcasecmp(ccm_type, "prolog")) {
		nid_list_file = _ccm_create_nidlist_file(ccm_info);
		if (nid_list_file == NULL) {
			CRAY_ERR("CCM job %u unable to create nidlist file",
				 ccm_info->job_id);
			return kill;
		}
	}
	/* Fork a child to exec the CCM script */
	cpid = fork();
	if (cpid < 0) {
		CRAY_ERR("CCM job %u %s fork failed, %m", ccm_info->job_id,
			 ccm_type);
		if (nid_list_file) {
			ret = unlink(nid_list_file);
			if (ret == -1) {
				CRAY_ERR("CCM job %u unable to unlink %s",
					 ccm_info->job_id, nid_list_file);
			}
			xfree(nid_list_file);
		}
		return kill;
	} else if (cpid == 0) {
		/* child */
		char **env = env_array_create();
		setsid();
		setpgid(0, 0);
		env_array_append_fmt(&env, "ALPS_PREP_BATCHID", "%u",
		                     ccm_info->job_id);
		env_array_append_fmt(&env, "ALPS_PREP_UID", "%u",
		                     ccm_info->user_id);
		if (nid_list_file) {
			env_array_append_fmt(&env, "ALPS_PREP_NIDFILE", "%s",
			                     nid_list_file);
		}
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = ccm_script;
		argv[3] = NULL;
		debug("CCM job %u invoking %s %s", ccm_info->job_id, ccm_type,
		      ccm_script);
		execve("/bin/sh", (char *const *)argv, env);
		CRAY_ERR("CCM job %u %s %s execv failed, %m",
			 ccm_info->job_id, ccm_type, ccm_script);
		_exit(127);
	} else {
		/* parent */
		while (1) {
			wait_rc = waitpid(cpid, &status, 0);
			if (wait_rc > 0) {
				if (WIFEXITED(status)) {
					ret = WEXITSTATUS(status);
					if (ret != 0) {
						info("CCM job %u %s waitpid "
						     "ret %d",
						     ccm_info->job_id,
						     ccm_type, ret);
					} else {
						kill = 0;
					}
				} else if (WIFSIGNALED(status)) {
					info("CCM job %u %s received "
					     "signal %d", ccm_info->job_id,
					     ccm_type, WTERMSIG(status));
				} else {
					/* Success */
					kill = 0;
				}
				break;
			} else if (wait_rc < 0) {
				if (errno == EINTR) {
					continue;
				}
				CRAY_ERR("CCM job %u %s waitpid error %m",
					 ccm_info->job_id, ccm_type);
				break;
			}
		}
		if (nid_list_file) {
			ret = unlink(nid_list_file);
			if (ret == -1) {
				info("CCM job %u unable to unlink %s, %m",
				     ccm_info->job_id, nid_list_file);
			}
			xfree(nid_list_file);
		}
	}
	END_TIMER;
	debug("CCM job %u %s completed in %s", ccm_info->job_id, ccm_type,
	      TIME_STR);
	return kill;
}

/*
 * Get the CCM configuration information.
 *
 */
extern void ccm_get_config(void)
{
	char *err_msg = NULL, *ccm_env;

	/* Alternate paths for testing purposes */
	ccm_env = getenv("CCM_PROLOG");
	if (ccm_env) {
		ccm_prolog_path = xstrdup(ccm_env);
	} else {
		ccm_prolog_path = xstrdup(CCM_PROLOG_PATH);
	}
	ccm_env = getenv("CCM_EPILOG");
	if (ccm_env) {
		ccm_epilog_path = xstrdup(ccm_env);
	} else {
		ccm_epilog_path = xstrdup(CCM_EPILOG_PATH);
	}
	ccm_config.ccm_enabled = 0;
	err_msg = _get_ccm_partition(&ccm_config);
	if (err_msg) {
		info("CCM ssh launch disabled: %s", err_msg);
	} else {
		if (ccm_config.num_ccm_partitions > 0) {
			ccm_config.ccm_enabled = 1;
			info("CCM prolog %s, epilog %s",
			     ccm_prolog_path, ccm_epilog_path);
		}
	}
	return;
}

/*
 * Check if this batch job is being started from a CCM partition.
 * Returns 1 if so, otherwise 0.
 */
extern int ccm_check_partitions(job_record_t *job_ptr)
{
	int i, ccm_partition;
	char *partition = NULL;

	ccm_partition = 0;
	partition = job_ptr->partition;
	debug2("CCM job %u ccm_check_partitions partition %s",
	       job_ptr->job_id, partition);
	for (i = 0; i < ccm_config.num_ccm_partitions; i++) {
		if (!xstrcasecmp(partition, ccm_config.ccm_partition[i])) {
			ccm_partition = 1;
			break;
		}
	}
	return ccm_partition;
}

/*
 * As applicable, this is run at batch job start to gather info for the
 * CCM prolog activities.  If the CCM prolog fails, the job will be
 * killed.
 */
extern void *ccm_begin(void *args)
{
	int i, j, num_ents, kill = 1;
	uint32_t job_id;
	size_t copysz;
	ccm_info_t ccm_info;
	char err_str_buf[128], srun_msg_buf[256];
	job_record_t *job_ptr = (job_record_t *) args;
	slurmctld_lock_t job_read_lock =
		{ NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, READ_LOCK };

	lock_slurmctld(job_read_lock);
	if (job_ptr->magic != JOB_MAGIC) {
		unlock_slurmctld(job_read_lock);
		error("ccm job has disappeared");
		return NULL;
	} else if (IS_JOB_COMPLETING(job_ptr)) {
		unlock_slurmctld(job_read_lock);
		debug("ccm %u job has already completed", job_ptr->job_id);
		return NULL;
	}

	job_id = job_ptr->job_id;

	debug2("CCM job %u_ccm_begin partition %s", job_ptr->job_id,
	       job_ptr->partition);
	memset(&ccm_info, 0, sizeof(ccm_info_t));

	ccm_info.job_id = job_ptr->job_id;
	ccm_info.user_id = job_ptr->user_id;
	ccm_info.nodelist = xstrdup(job_ptr->nodes);
	ccm_info.node_cnt = job_ptr->node_cnt;
	ccm_info.cpus_per_task = job_ptr->details->cpus_per_task;
	if (ccm_info.cpus_per_task == 0) {
		ccm_info.cpus_per_task = 1;
	}
	ccm_info.num_cpu_groups = job_ptr->job_resrcs->cpu_array_cnt;
	copysz = ccm_info.num_cpu_groups * sizeof(uint16_t);
	ccm_info.cpus_per_node = xmalloc(copysz);
	memcpy(ccm_info.cpus_per_node, job_ptr->job_resrcs->cpu_array_value,
	       copysz);
	copysz = ccm_info.num_cpu_groups * sizeof(uint32_t);
	ccm_info.cpu_count_reps = xmalloc(copysz);
	memcpy(ccm_info.cpu_count_reps, job_ptr->job_resrcs->cpu_array_reps,
	       copysz);
	ccm_info.num_tasks = job_ptr->details->num_tasks;
	if (ccm_info.num_tasks == 0) {
		ccm_info.num_tasks =
			job_ptr->cpu_cnt / ccm_info.cpus_per_task;
		debug("CCM job %u ccm_info.num_tasks was 0; now %d",
		      job_ptr->job_id, ccm_info.num_tasks);
	}
	/*
	 * When task_dist is set to PLANE, the plane_size is still 0.
	 * This causes a failure later with the slurm_step_layout_create()
	 * call.  Both task_dist and plane_size are arguments to that
	 * procedure call used to get the number of tasks for each node.
	 * I don't think either value impacts that, but leave this code
	 * here.
	 */
	if ((job_ptr->details->task_dist == 0) ||
	    (job_ptr->details->task_dist > SLURM_DIST_UNKNOWN) ||
	    (job_ptr->details->task_dist == SLURM_DIST_PLANE)) {
		ccm_info.task_dist = SLURM_DIST_BLOCK;
		debug("CCM job %u job task_dist %d, CCM using SLURM_DIST_BLOCK",
		      job_ptr->job_id, job_ptr->details->task_dist);
	} else {
		ccm_info.task_dist = job_ptr->details->task_dist;
	}
	ccm_info.plane_size = job_ptr->details->plane_size;

	debug("CCM job %u, user_id %u, nodelist %s, node_cnt %d, "
	      "num_tasks %d", ccm_info.job_id, ccm_info.user_id,
	      ccm_info.nodelist, ccm_info.node_cnt, ccm_info.num_tasks);
	debug("CCM job %u cpus_per_task %d, task_dist %u, plane_size %d",
	      ccm_info.job_id, ccm_info.cpus_per_task, ccm_info.task_dist,
	      ccm_info.plane_size);
	for (i = num_ents = 0; i < ccm_info.num_cpu_groups; i++) {
		for (j = 0; j < ccm_info.cpu_count_reps[i]; j++) {
			debug3("CCM job %u cpus_per_node[%d] %d, i %d, j %d",
			       ccm_info.job_id, num_ents,
			       ccm_info.cpus_per_node[i], i, j);
			num_ents++;
		}
	}
	unlock_slurmctld(job_read_lock);

	if (ccm_info.node_cnt != num_ents) {
		CRAY_ERR("CCM job %u ccm_info.node_cnt %d doesn't match the "
			 "number of cpu_count_reps entries %d",
			 job_id, ccm_info.node_cnt, num_ents);
		snprintf(err_str_buf, sizeof(err_str_buf),
			 "node_cnt %d != cpu_count_reps %d, prolog not run",
			 ccm_info.node_cnt, num_ents);
	} else {
		kill = _run_ccm_prolog_epilog(&ccm_info, "prolog",
					      ccm_prolog_path);
		snprintf(err_str_buf, sizeof(err_str_buf),
			 "prolog failed");
	}

	lock_slurmctld(job_write_lock);
	if ((job_ptr->magic  != JOB_MAGIC) ||
	    (job_ptr->job_id != job_id)) {
		unlock_slurmctld(job_write_lock);
		error("ccm job %u has disappeared after running ccm", job_id);
		return NULL;
	}
	debug("CCM ccm_begin job %u prolog_running_decr, cur %d",
	      ccm_info.job_id, job_ptr->details->prolog_running);
	prolog_running_decr(job_ptr);
	if (kill) {
		/* Stop the launch */
		CRAY_ERR("CCM %s, job %u killed", err_str_buf, job_ptr->job_id);
		snprintf(srun_msg_buf, sizeof(srun_msg_buf),
			 "CCM %s, job %u killed", err_str_buf, ccm_info.job_id);
		srun_user_message(job_ptr, srun_msg_buf);
		(void) job_signal(job_ptr, SIGKILL, 0, 0, false);
	}
	unlock_slurmctld(job_write_lock);
	/* Free the malloc'd fields within this structure */
	_free_ccm_info(&ccm_info);
	return NULL;
}

/*
 * As applicable, this is run at batch job exit to provide info for the
 * CCM epilog activities.  The epilog only needs the job id and user id.
 * If the CCM prolog is still executing, delay starting the CCM epilog
 * to prevent bad interactions between the two.  Delay up to
 * CCM_MAX_EPILOG_DELAY seconds.
 */
extern void *ccm_fini(void *args)
{
	int rc;
	ccm_info_t ccm_info;
	time_t delay;
	job_record_t *job_ptr = (job_record_t *) args;
	slurmctld_lock_t job_read_lock =
		{NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	memset(&ccm_info, 0, sizeof(ccm_info_t));
	lock_slurmctld(job_read_lock);

	ccm_info.job_id = job_ptr->job_id;
	ccm_info.user_id = job_ptr->user_id;

	unlock_slurmctld(job_read_lock);
	/*
	 * Delay starting the CCM epilog if the CCM prolog may still be
	 * running.
	 */
	if (job_ptr->details && (job_ptr->details->prolog_running > 0)) {
		delay = time(0) + CCM_MAX_EPILOG_DELAY;
		info("CCM job %u epilog delayed; prolog_running %d",
		     ccm_info.job_id, job_ptr->details->prolog_running);
		while (job_ptr->details->prolog_running > 0) {
			usleep(100000);
			if (time(0) >= delay) {
				info("CCM job %u epilog max delay; "
				     "running epilog", ccm_info.job_id);
				break;
			}
		}
	}
	debug2("CCM epilog job %u, user_id %u", ccm_info.job_id,
	       ccm_info.user_id);
	rc = _run_ccm_prolog_epilog(&ccm_info, "epilog", ccm_epilog_path);
	if (rc) {
		/* Log a failure, no further action to take */
		CRAY_ERR("CCM job %u epilog failed", ccm_info.job_id);
	}
	return NULL;
}
