/*****************************************************************************\
 *  select_cray.c - node selection plugin for cray systems.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "other_select.h"

#define NOT_FROM_CONTROLLER -2
/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
struct node_record *node_record_table_ptr __attribute__((weak_import));
int bg_recover __attribute__((weak_import)) = NOT_FROM_CONTROLLER;
List part_list __attribute__((weak_import));
List job_list __attribute__((weak_import));
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
struct switch_record *switch_record_table __attribute__((weak_import));
int switch_record_cnt __attribute__((weak_import));
#elif !defined(__CYGWIN__)
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr;
int bg_recover = NOT_FROM_CONTROLLER;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
struct switch_record *switch_record_table;
int switch_record_cnt;
#endif

#define JOBINFO_MAGIC 0x8cb3
#define NODEINFO_MAGIC 0x82a3

struct select_jobinfo {
	uint16_t		magic;		/* magic number */
	select_jobinfo_t	*other_jobinfo;
	uint32_t		reservation_id;	/* BASIL reservation ID */
};

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	select_nodeinfo_t *other_nodeinfo;
};

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
const char plugin_type[]	= "select/cray";
uint32_t plugin_id	        = 104;
const uint32_t plugin_version	= 1;


/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	/*
	 * FIXME: At the moment the smallest Cray allocation unit are still
	 * full nodes. Node sharing (even across NUMA sockets of the same
	 * node) is, as of CLE 3.1 (summer 2010) still not supported, i.e.
	 * as per the LIMITATIONS section of the aprun(1) manpage of the
	 * 3.1.27A release).
	 * Hence for the moment we can only use select/linear.  If some
	 * time in the future this is allowable use code such as this
	 * to make things switch to the cons_res plugin.
	 * if (slurmctld_conf.select_type_param & CR_CONS_RES)
	 *	plugin_id = 105;
	 */
#ifndef HAVE_CRAY
	if (bg_recover != NOT_FROM_CONTROLLER)
		fatal("select/cray is incompatible with a non Cray system");
#endif
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
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

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
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
			     List *preemptee_job_list)
{

	return other_job_test(job_ptr, bitmap, min_nodes, max_nodes,
			      req_nodes, mode, preemptee_candidates,
			      preemptee_job_list);
}

extern int select_p_job_begin(struct job_record *job_ptr)
{

	return other_job_begin(job_ptr);
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	return other_job_ready(job_ptr);
}


extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	return other_job_resized(job_ptr, node_ptr);
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	return other_job_fini(job_ptr);
}

extern int select_p_job_suspend(struct job_record *job_ptr)
{
	return other_job_suspend(job_ptr);
}

extern int select_p_job_resume(struct job_record *job_ptr)
{
	return other_job_resume(job_ptr);
}

extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
	return other_pack_select_info(last_query_time, show_flags, buffer_ptr,
				      protocol_version);
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(uint32_t size)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;
	nodeinfo->other_nodeinfo = other_select_nodeinfo_alloc(size);

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
	if (protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
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
	if (protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
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

extern int select_p_select_nodeinfo_set_all(time_t last_query_time)
{
	return other_select_nodeinfo_set_all(last_query_time);
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
		error("other_get_nodeinfo: nodeinfo not set");
		return SLURM_ERROR;
	}
	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("set_nodeinfo: nodeinfo magic bad");
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

	if (jobinfo == NULL) {
		error("set_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("set_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_RESV_ID:
		jobinfo->reservation_id = *uint32;
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
	select_jobinfo_t **select_jobinfo = (select_jobinfo_t **) data;

	if (jobinfo == NULL) {
		error("get_jobinfo: jobinfo not set");
		return SLURM_ERROR;
	}
	if (jobinfo->magic != JOBINFO_MAGIC) {
		error("get_jobinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (data_type) {
	case SELECT_JOBDATA_PTR:
		*select_jobinfo = jobinfo->other_jobinfo;
		break;
	case SELECT_JOBDATA_RESV_ID:
		*uint32 = jobinfo->reservation_id;
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
		error("copy_jobinfo: jobinfo magic bad");
	else {
		rc = xmalloc(sizeof(struct select_jobinfo));
		rc->magic = JOBINFO_MAGIC;
		rc->reservation_id = jobinfo->reservation_id;
	}
	return rc;
}

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	int rc = SLURM_SUCCESS;

	if (jobinfo) {
		if (jobinfo->magic != JOBINFO_MAGIC) {
			error("free_jobinfo: jobinfo magic bad");
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

	if (protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
		if (!jobinfo) {
			pack32(0, buffer);
			return SLURM_SUCCESS;
		}
		pack32(jobinfo->reservation_id, buffer);
		rc = other_select_jobinfo_pack(jobinfo->other_jobinfo, buffer,
					       protocol_version);
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
	if (protocol_version >= SLURM_2_2_PROTOCOL_VERSION) {
		safe_unpack32(&jobinfo->reservation_id, buffer);
		rc = other_select_jobinfo_unpack(&jobinfo->other_jobinfo,
						 buffer, protocol_version);
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
		error("sprint_jobinfo: buf is null");
		return NULL;
	}

	if ((mode != SELECT_PRINT_DATA)
	    && jobinfo && (jobinfo->magic != JOBINFO_MAGIC)) {
		error("sprint_jobinfo: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("sprint_jobinfo: jobinfo bad");
			return NULL;
		}
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		snprintf(buf, size, "RESV_ID");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->reservation_id)
			snprintf(buf, size, "%7u", jobinfo->reservation_id);
		else
			snprintf(buf, size, "%7s", "none");
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->reservation_id)
			snprintf(buf, size, "Resv_ID=%u",
				 jobinfo->reservation_id);
		else
			snprintf(buf, size, "Resv_ID=none");
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
		error("xstrdup_jobinfo: jobinfo magic bad");
		return NULL;
	}

	if (jobinfo == NULL) {
		if (mode != SELECT_PRINT_HEAD) {
			error("xstrdup_jobinfo: jobinfo bad");
			return NULL;
		}
	}

	switch (mode) {
	case SELECT_PRINT_HEAD:
		xstrcat(buf, "RESV_ID");
		break;
	case SELECT_PRINT_DATA:
		if (jobinfo->reservation_id)
			xstrfmtcat(buf, "%7u", jobinfo->reservation_id);
		else
			xstrfmtcat(buf, "%7s", "none");
		break;
	case SELECT_PRINT_MIXED:
		if (jobinfo->reservation_id)
			xstrfmtcat(buf, "Resv_ID=%u", jobinfo->reservation_id);
		else
			xstrcat(buf, "Resv_ID=none");
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
	return other_update_block(block_desc_ptr);
}

extern int select_p_update_sub_node(update_block_msg_t *block_desc_ptr)
{
	return other_update_sub_node(block_desc_ptr);
}

extern int select_p_get_info_from_plugin(enum select_jobdata_type info,
					 struct job_record *job_ptr,
					 void *data)
{
	return other_get_info_from_plugin(info, job_ptr, data);
}

extern int select_p_update_node_config(int index)
{
	return other_update_node_config(index);
}

extern int select_p_update_node_state(int index, uint16_t state)
{
	return other_update_node_state(index, state);
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return other_alter_node_cnt(type, data);
}

extern int select_p_reconfigure(void)
{
	return other_reconfigure();
}
