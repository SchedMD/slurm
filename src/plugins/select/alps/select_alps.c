/*****************************************************************************\
 *  select_alps.c - node selection plugin for alps/cray systems.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Supported by the Oak Ridge National Laboratory Extreme Scale Systems Center
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/slurm_strcasestr.h"
#include "other_select.h"
#include "basil_interface.h"
#include "cray_config.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
struct node_record *node_record_table_ptr __attribute__((weak_import));
List part_list __attribute__((weak_import));
List job_list __attribute__((weak_import));
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
int slurmctld_primary __attribute__((weak_import));
struct switch_record *switch_record_table __attribute__((weak_import));
int switch_record_cnt __attribute__((weak_import));
slurmdb_cluster_rec_t *working_cluster_rec  __attribute__((weak_import)) = NULL;
void *acct_db_conn __attribute__((weak_import)) = NULL;
bitstr_t *avail_node_bitmap __attribute__((weak_import)) = NULL;
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
#else
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
int slurmctld_primary;
struct switch_record *switch_record_table;
int switch_record_cnt;
slurmdb_cluster_rec_t *working_cluster_rec = NULL;
void *acct_db_conn = NULL;
bitstr_t *avail_node_bitmap = NULL;
int bg_recover = NOT_FROM_CONTROLLER;

int clusteracct_storage_g_node_down(void *db_conn, struct node_record *node_ptr,
			time_t event_time, char *reason,
			uint32_t reason_uid) { return 0; }
int clusteracct_storage_g_node_up(void *db_conn, struct node_record *node_ptr,
			time_t event_time) { return 0; }
struct node_record *find_node_record (char *name) { return NULL; }
uint32_t gres_plugin_get_job_value_by_type(List job_gres_list,
			char *gres_name_type) { return 0; }
void make_node_idle(struct node_record *node_ptr,
			struct job_record *job_ptr) { ; }
int select_char2coord(char coord) { return 0; }
void set_node_down_ptr (struct node_record *node_ptr, char *reason) { ; }
char *uid_to_string (uid_t uid) { return NULL; }
#endif

#if !defined (SIGRTMIN) && defined(__NetBSD__)
/* protected definition in <sys/signal.h> */
#  define SIGRTMIN (SIGPWR+1)
#endif
/*
 * SIGRTMIN isn't defined on osx, so lets keep it above the signals in use.
 */
#if !defined (SIGRTMIN) && defined (__APPLE__)
#  define SIGRTMIN SIGUSR2+1
#endif

int inv_interval = 0;

/* All current (2011) XT/XE installations have a maximum dimension of 3,
 * smaller systems deploy a 2D Torus which has no connectivity in
 * X-dimension.  We know the highest system dimensions possible here
 * are 3 so we set it to that.  Do not use SYSTEM_DIMENSIONS since
 * that could easily be wrong if built on a non Cray system. */
static int select_cray_dim_size[3] = {-1};

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
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the node selection API matures.
 */
const char plugin_name[]	= "Cray node selection plugin";
const char plugin_type[]	= "select/alps";
uint32_t plugin_id		= 104;
const uint32_t plugin_version	= 120;

static bool _zero_size_job ( struct job_record *job_ptr )
{
	xassert (job_ptr);
	if (job_ptr->details &&
	    (job_ptr->details->min_nodes == 0) &&
	    (job_ptr->details->max_nodes == 0))
		return true;
	return false;
}

static void _set_inv_interval(void)
{
	char *tmp_ptr, *sched_params = slurm_get_sched_params();
	int i;

	if (sched_params) {
		if (sched_params &&
		    (tmp_ptr = slurm_strcasestr(sched_params,
						"inventory_interval="))) {
		/*                                   0123456789012345 */
			i = atoi(tmp_ptr + 19);
			if (i < 0)
				error("ignoring SchedulerParameters: "
				      "inventory_interval of %d", i);
			else
				inv_interval = i;
		}
		xfree(sched_params);
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	/*
	 * FIXME: At the moment the smallest Cray allocation unit are still
	 * full nodes. Node sharing (even across NUMA sockets of the same
	 * node) is, as of CLE 3.x (Summer 2011) still not supported, i.e.
	 * as per the LIMITATIONS section of the aprun(1) manpage of the
	 * 3.1.27A release).
	 * Hence for the moment we can only use select/linear.  If some
	 * time in the future this is allowable use code such as this
	 * to make things switch to the cons_res plugin.
	 * if (slurmctld_conf.select_type_param & CR_OTHER_CONS_RES)
	 *	plugin_id = 105;
	 */
	if (bg_recover != NOT_FROM_CONTROLLER) {
		if (slurmctld_conf.select_type_param & CR_OTHER_CONS_RES) {
			fatal("SelectTypeParams=other_cons_res is not valid "
			      "for select/alps");
		}
		_set_inv_interval();
	}

	create_config();
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	destroy_config();
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM
 * node selection API.
 */

extern int select_p_state_save(char *dir_name)
{
	return other_state_save(dir_name);
}

extern int select_p_state_restore(char *dir_name)
{
	return other_state_restore(dir_name);
}

extern int select_p_job_init(List job_list)
{
	return other_job_init(job_list);
}

/*
 * select_p_node_ranking - generate node ranking for Cray nodes
 */
extern bool select_p_node_ranking(struct node_record *node_ptr, int node_cnt)
{
	if (slurmctld_primary == 0)
		return false;
	if (basil_node_ranking(node_ptr, node_cnt) < 0)
		fatal("can not resolve node coordinates: ALPS problem?");
	return true;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	if (slurmctld_primary && basil_geometry(node_ptr, node_cnt)) {
		error("can not get initial ALPS node state");
		return SLURM_ERROR;
	}
	return other_node_init(node_ptr, node_cnt);
}

extern int select_p_block_init(List part_list)
{
	return other_block_init(part_list);
}

/*
 * select_p_job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 *	"best" is defined as either single set of consecutive nodes satisfying
 *	the request and leaving the minimum number of unused nodes OR
 *	the fewest number of consecutive node sets
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN exc_core_bitmap - bitmap of cores being reserved.
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init):
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_cpus: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of the job's required at the time that
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{

	if (!job_ptr->details)
		return EINVAL;

	if (min_nodes == 0) {
		/* Allocate resources only on a front-end node */
		job_ptr->details->min_cpus = 0;
	}

	if (job_ptr->details->core_spec != (uint16_t) NO_VAL) {
		verbose("select/alps: job %u core_spec(%u) not supported",
			job_ptr->job_id, job_ptr->details->core_spec);
		job_ptr->details->core_spec = (uint16_t) NO_VAL;
	}

	return other_job_test(job_ptr, bitmap, min_nodes, max_nodes,
			      req_nodes, mode, preemptee_candidates,
			      preemptee_job_list, exc_core_bitmap);
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	xassert(job_ptr);

	if (slurmctld_primary && !_zero_size_job(job_ptr) &&
	    (do_basil_reserve(job_ptr) != SLURM_SUCCESS)) {
		job_ptr->state_reason = WAIT_RESOURCES;
		xfree(job_ptr->state_desc);
		return SLURM_ERROR;
	}
	return other_job_begin(job_ptr);
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;

	xassert(job_ptr);
	/*
	 * Convention:	this function may be called also from stepdmgr, to
	 *		confirm the ALPS reservation of a batch job. In this
	 *		case, job_ptr only has minimal information and sets
	 *		job_state == NO_VAL to distinguish this call from one
	 *		done by slurmctld. It also sets batch_flag == 0, which
	 *		means that we need to confirm only if batch_flag is 0,
	 *		and execute the other_job_ready() only in slurmctld.
	 */
	if ((slurmctld_primary || (job_ptr->job_state == (uint16_t)NO_VAL))
	    && !job_ptr->batch_flag && !_zero_size_job(job_ptr))
		rc = do_basil_confirm(job_ptr);
	if ((rc != SLURM_SUCCESS) || (job_ptr->job_state == (uint16_t) NO_VAL))
		return rc;
	return other_job_ready(job_ptr);
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	/* return other_job_resized(job_ptr, node_ptr); */
	return ESLURM_NOT_SUPPORTED;
}

extern bool select_p_job_expand_allow(void)
{
	return false;
}

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	xassert(job_ptr);
	/*
	 * Release the ALPS reservation already here for those signals that are
	 * likely to terminate the job. Otherwise there is a race condition if a
	 * script has more than one aprun line: while the apkill of the current
	 * aprun line is underway, the job script proceeds to run and executes
	 * the next following aprun line, until reaching the end of the script.
	 * This not only creates large delays, it can also mess up cleaning up
	 * after the job. Releasing the reservation will stop any new aprun
	 * lines from being executed.
	 */
	if (slurmctld_primary) {
		switch (signal) {
			case SIGCHLD:
			case SIGCONT:
			case SIGSTOP:
			case SIGTSTP:
			case SIGTTIN:
			case SIGTTOU:
			case SIGURG:
			case SIGWINCH:
				break;
			default:
				if (signal < SIGRTMIN)
					do_basil_release(job_ptr);
		}
	}

	if (slurmctld_primary && !_zero_size_job(job_ptr)) {
		if (signal != SIGKILL) {
			if (do_basil_signal(job_ptr, signal) != SLURM_SUCCESS)
				return SLURM_ERROR;
		} else {
			uint16_t kill_wait = slurm_get_kill_wait();
			if (do_basil_signal(job_ptr, SIGCONT) != SLURM_SUCCESS)
				return SLURM_ERROR;
			if (do_basil_signal(job_ptr, SIGTERM) != SLURM_SUCCESS)
				return SLURM_ERROR;
			queue_basil_signal(job_ptr, SIGKILL, kill_wait);
		}
	}
	return other_job_signal(job_ptr, signal);
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	if (job_ptr == NULL)
		return SLURM_SUCCESS;

	/* Don't run the release in the controller for batch jobs.  It is
	 * handled on the stepd end.
	 */
	if (((slurmctld_primary && !job_ptr->batch_flag) ||
	     (job_ptr->job_state == (uint16_t)NO_VAL))
	    && !_zero_size_job(job_ptr) &&
	    (do_basil_release(job_ptr) != SLURM_SUCCESS))
		return SLURM_ERROR;
	/*
	 * Convention: like select_p_job_ready, may be called also from
	 *             stepdmgr, where job_state == NO_VAL is used to
	 *             distinguish the context from that of slurmctld.
	 */
	if (job_ptr->job_state == (uint16_t)NO_VAL)
		return SLURM_SUCCESS;
	return other_job_fini(job_ptr);
}

extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
	if (job_ptr == NULL)
		return SLURM_SUCCESS;

	if (slurmctld_primary && !_zero_size_job(job_ptr) &&
	    (do_basil_switch(job_ptr, 1) != SLURM_SUCCESS))
		return SLURM_ERROR;

	return other_job_suspend(job_ptr, indf_susp);
}

extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
	if (job_ptr == NULL)
		return SLURM_SUCCESS;

	if (slurmctld_primary && !_zero_size_job(job_ptr) &&
	    (do_basil_switch(job_ptr, 0) != SLURM_SUCCESS))
		return SLURM_ERROR;

	return other_job_resume(job_ptr, indf_susp);
}

extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	return other_step_pick_nodes(job_ptr, jobinfo, node_count, avail_nodes);
}

extern int select_p_step_start(struct step_record *step_ptr)
{
	return other_step_start(step_ptr);
}

extern int select_p_step_finish(struct step_record *step_ptr)
{
	return other_step_finish(step_ptr);
}

extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
	return other_pack_select_info(last_query_time, show_flags, buffer_ptr,
				      protocol_version);
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;
	nodeinfo->other_nodeinfo = other_select_nodeinfo_alloc();

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		other_select_nodeinfo_free(nodeinfo->other_nodeinfo);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer, uint16_t protocol_version)
{
	int rc = SLURM_ERROR;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		rc = other_select_nodeinfo_pack(nodeinfo->other_nodeinfo,
						buffer, protocol_version);
	}
	return rc;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo_pptr,
					   Buf buffer,
					   uint16_t protocol_version)
{
	int rc = SLURM_ERROR;
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	*nodeinfo_pptr = nodeinfo;

	nodeinfo->magic = NODEINFO_MAGIC;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		rc = other_select_nodeinfo_unpack(&nodeinfo->other_nodeinfo,
						  buffer, protocol_version);
	}

	if (rc != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	select_p_select_nodeinfo_free(nodeinfo);
	*nodeinfo_pptr = NULL;

	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	return other_select_nodeinfo_set_all();
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	return other_select_nodeinfo_set(job_ptr);
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	int rc = SLURM_SUCCESS;
	select_nodeinfo_t **select_nodeinfo = (select_nodeinfo_t **) data;

	if (nodeinfo == NULL) {
		error("select/cray nodeinfo_get: nodeinfo not set");
		return SLURM_ERROR;
	}
	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("select/cray nodeinfo_get: nodeinfo magic bad");
		return SLURM_ERROR;
	}


	switch (dinfo) {
	case SELECT_NODEDATA_PTR:
		*select_nodeinfo = nodeinfo->other_nodeinfo;
		break;
	default:
		rc = other_select_nodeinfo_get(nodeinfo->other_nodeinfo,
					       dinfo, state, data);
		break;
	}
	return rc;
}

extern select_jobinfo_t *select_p_select_jobinfo_alloc(void)
{
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));
	jobinfo->magic = JOBINFO_MAGIC;
	jobinfo->other_jobinfo = other_select_jobinfo_alloc();

	return jobinfo;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	int rc = SLURM_SUCCESS;
	uint32_t *uint32 = (uint32_t *) data;
	uint64_t *uint64 = (uint64_t *) data;

	if (jobinfo == NULL) {
		error("select/cray jobinfo_set: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select/cray jobinfo_set: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_RESV_ID:
		jobinfo->reservation_id = *uint32;
		break;
	case SELECT_JOBDATA_PAGG_ID:
		jobinfo->confirm_cookie = *uint64;
		break;
	default:
		rc = other_select_jobinfo_set(jobinfo, data_type, data);
		break;
	}

	return rc;
}

extern int select_p_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	int rc = SLURM_SUCCESS;
	uint32_t *uint32 = (uint32_t *) data;
	uint64_t *uint64 = (uint64_t *) data;
	select_jobinfo_t **select_jobinfo = (select_jobinfo_t **) data;

	if (jobinfo == NULL) {
		error("select/cray jobinfo_get: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("select/cray jobinfo_get: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_PTR:
		*select_jobinfo = jobinfo->other_jobinfo;
		break;
	case SELECT_JOBDATA_RESV_ID:
		*uint32 = jobinfo->reservation_id;
		break;
	case SELECT_JOBDATA_PAGG_ID:
		*uint64 = jobinfo->confirm_cookie;
		break;
	default:
		rc = other_select_jobinfo_get(jobinfo, data_type, data);
		break;
	}

	return rc;
}

extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	struct select_jobinfo *rc = NULL;

	if (jobinfo == NULL)
		;
	else if (jobinfo->magic != JOBINFO_MAGIC)
		error("select/cray jobinfo_copy: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(struct select_jobinfo));
		rc->magic = JOBINFO_MAGIC;
		rc->reservation_id = jobinfo->reservation_id;
		rc->confirm_cookie = jobinfo->confirm_cookie;
	}
	return rc;
}

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (jobinfo) {
		if (jobinfo->magic != JOBINFO_MAGIC) {
			error("select/cray jobinfo_free: jobinfo magic bad");
			return EINVAL;
		}

		jobinfo->magic = 0;
		xfree(jobinfo);
	}

	return rc;
}

extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					uint16_t protocol_version)
{
	int rc = SLURM_ERROR;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!jobinfo) {
			pack8(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			return SLURM_SUCCESS;
		}
		pack8(jobinfo->confirmed, buffer);
		pack32(jobinfo->reservation_id, buffer);
		pack64(jobinfo->confirm_cookie, buffer);
		rc = other_select_jobinfo_pack(jobinfo->other_jobinfo, buffer,
					       protocol_version);
	} else {
 		error("select_p_select_jobinfo_pack: protocol_version "
 		      "%hu not supported", protocol_version);
	}
	return rc;
}

extern int select_p_select_jobinfo_unpack(select_jobinfo_t **jobinfo_pptr,
					  Buf buffer, uint16_t protocol_version)
{
	int rc = SLURM_ERROR;
	select_jobinfo_t *jobinfo = xmalloc(sizeof(struct select_jobinfo));

	*jobinfo_pptr = jobinfo;

	jobinfo->magic = JOBINFO_MAGIC;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack8(&jobinfo->confirmed, buffer);
		safe_unpack32(&jobinfo->reservation_id, buffer);
		safe_unpack64(&jobinfo->confirm_cookie, buffer);
		rc = other_select_jobinfo_unpack(&jobinfo->other_jobinfo,
						 buffer, protocol_version);
	} else {
 		error("select_p_select_jobinfo_unpack: protocol_version "
 		      "%hu not supported", protocol_version);
	}

	if (rc != SLURM_SUCCESS)
		goto unpack_error;

	return SLURM_SUCCESS;

unpack_error:
	select_p_select_jobinfo_free(jobinfo);
	*jobinfo_pptr = NULL;

	return SLURM_ERROR;
}

extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{

	if (buf == NULL) {
		error("select/cray jobinfo_sprint: buf is null");
		return NULL;
	}

	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("select/cray jobinfo_sprint: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select/cray jobinfo_sprint: jobinfo bad");
			return NULL;
		}
	}

	switch (mode) {
	/*
	 * SLURM only knows the ALPS reservation ID. The application IDs (APIDs)
	 * of the reservation need to be queried from the Inventory response.
	 * The maximum known reservation ID is 4096, it wraps around after that.
	 */
	case SELECT_PRINT_HEAD:
		snprintf(buf, size, "ALPS");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->reservation_id)
			snprintf(buf, size, "%4u", jobinfo->reservation_id);
		else
			snprintf(buf, size, "%4s", "none");
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->reservation_id)
			snprintf(buf, size, "resId=%u",
				 jobinfo->reservation_id);
		else
			snprintf(buf, size, "resId=none");
		break;
	case SELECT_PRINT_RESV_ID:
		snprintf(buf, size, "%u", jobinfo->reservation_id);
		break;
	default:
		other_select_jobinfo_sprint(jobinfo->other_jobinfo, buf,
					    size, mode);
		break;
	}

	return buf;
}

extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	char *buf = NULL;

	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("select/cray jobinfo_xstrdup: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("select/cray jobinfo_xstrdup: jobinfo bad");
			return NULL;
		}
	}

	switch (mode) {
	/* See comment in select_p_select_jobinfo_sprint() regarding format. */
	case SELECT_PRINT_HEAD:
		xstrcat(buf, "ALPS");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->reservation_id)
			xstrfmtcat(buf, "%4u", jobinfo->reservation_id);
		else
			xstrfmtcat(buf, "%4s", "none");
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->reservation_id)
			xstrfmtcat(buf, "resId=%u", jobinfo->reservation_id);
		else
			xstrcat(buf, "resId=none");
		break;
	case SELECT_PRINT_RESV_ID:
		xstrfmtcat(buf, "%u", jobinfo->reservation_id);
		break;
	default:
		xstrcat(buf, other_select_jobinfo_xstrdup(
				jobinfo->other_jobinfo, mode));
		break;
	}

	return buf;
}

extern int select_p_update_block(update_block_msg_t *block_desc_ptr)
{
	if (slurmctld_primary && basil_inventory())
		return SLURM_ERROR;

	return other_update_block(block_desc_ptr);
}

extern int select_p_update_sub_node(update_block_msg_t *block_desc_ptr)
{
	return other_update_sub_node(block_desc_ptr);
}

extern int select_p_fail_cnode(struct step_record *step_ptr)
{
	return other_fail_cnode(step_ptr);
}

extern int select_p_get_info_from_plugin(enum select_plugindata_info dinfo,
					 struct job_record *job_ptr,
					 void *data)
{
	return other_get_info_from_plugin(dinfo, job_ptr, data);
}

extern int select_p_update_node_config(int index)
{
	return other_update_node_config(index);
}

extern int select_p_update_node_state(struct node_record *node_ptr)
{
	return other_update_node_state(node_ptr);
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return other_alter_node_cnt(type, data);
}

extern int select_p_reconfigure(void)
{
	_set_inv_interval();

	return other_reconfigure();
}

extern bitstr_t * select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				     uint32_t node_cnt,
				     bitstr_t *avail_bitmap,
				     bitstr_t **core_bitmap)
{
	return other_resv_test(resv_desc_ptr, node_cnt,
			       avail_bitmap, core_bitmap);
}

extern void select_p_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	int i, j, offset;
	int dims = slurmdb_setup_cluster_dims();

	if (select_cray_dim_size[0] == -1) {
		node_info_t *node_ptr;

		/* init the rest of the dim sizes. All current (2011)
		 * XT/XE installations have a maximum dimension of 3,
		 * smaller systems deploy a 2D Torus which has no
		 * connectivity in X-dimension.  Just incase they
		 * decide to change it where we only get 2 instead of
		 * 3 we will initialize it later. */
		for (i = 1; i < dims; i++)
			select_cray_dim_size[i] = -1;
		for (i = 0; i < node_info_ptr->record_count; i++) {
			node_ptr = &(node_info_ptr->node_array[i]);
			if (!node_ptr->node_addr ||
			    (strlen(node_ptr->node_addr) != dims))
				continue;
			for (j = 0; j < dims; j++) {
				offset = select_char2coord(
					node_ptr->node_addr[j]);
				select_cray_dim_size[j] =
					MAX((offset+1),
					    select_cray_dim_size[j]);
			}
		}
	}

	/*
	 * Override the generic setup of dim_size made in _setup_cluster_rec()
	 * FIXME: use a better way, e.g. encoding the 3-dim triplet as a
	 *        string which gets stored in a database (event_table?) entry.
	 */
	if (working_cluster_rec) {
		xfree(working_cluster_rec->dim_size);
		working_cluster_rec->dim_size = xmalloc(sizeof(int) * dims);
		for (j = 0; j < dims; j++)
			working_cluster_rec->dim_size[j] =
				select_cray_dim_size[j];
	}

	other_ba_init(node_info_ptr, sanity_check);
}

extern int *select_p_ba_get_dims(void)
{
	/* Size of system in each dimension as set by basil_geometry(),
	 * which might not be called yet */
	if (select_cray_dim_size[0] != -1)
		return select_cray_dim_size;
	return NULL;
}

extern void select_p_ba_fini(void)
{
	other_ba_fini();
}

extern bitstr_t *select_p_ba_cnodelist2bitmap(char *cnodelist)
{
	return other_ba_cnodelist2bitmap(cnodelist);
}
