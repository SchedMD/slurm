/*****************************************************************************\
 *  node_info.c - get/print the node state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/parse_time.h"
#include "src/interfaces/select.h"
#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Data structures for pthreads used to gather node information from multiple
 * clusters in parallel */
typedef struct load_node_req_struct {
	slurmdb_cluster_rec_t *cluster;
	int cluster_inx;
	slurm_msg_t *req_msg;
	list_t *resp_msg_list;
	uint16_t show_flags;
} load_node_req_struct_t;

typedef struct load_node_resp_struct {
	int cluster_inx;
	node_info_msg_t *new_msg;
} load_node_resp_struct_t;

/*
 * slurm_print_node_info_msg - output information about all Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_info_msg_ptr - node information message pointer
 * IN one_liner - print as a single line if true
 */
void
slurm_print_node_info_msg ( FILE * out, node_info_msg_t * node_info_msg_ptr,
			    int one_liner )
{
	int i;
	node_info_t * node_ptr = node_info_msg_ptr -> node_array ;
	char time_str[256];

	slurm_make_time_str ((time_t *)&node_info_msg_ptr->last_update,
			     time_str, sizeof(time_str));
	fprintf( out, "Node data as of %s, record count %d\n",
		 time_str, node_info_msg_ptr->record_count);

	for (i = 0; i < node_info_msg_ptr-> record_count; i++) {
		if (!node_ptr[i].name)
			continue;
		slurm_print_node_table ( out, & node_ptr[i],
					 one_liner ) ;
	}
}


/*
 * slurm_print_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN out - file to write to
 * IN node_ptr - an individual node information record pointer
 * IN one_liner - print as a single line if true
 */
void slurm_print_node_table(FILE *out, node_info_t *node_ptr, int one_liner)
{
	char *print_this = slurm_sprint_node_table(node_ptr, one_liner);
	fprintf(out, "%s", print_this);
	xfree(print_this);
}

/* Given data structures containing information about nodes and partitions,
 * populate the node's "partitions" field */
void
slurm_populate_node_partitions(node_info_msg_t *node_buffer_ptr,
			       partition_info_msg_t *part_buffer_ptr)
{
	int i, j, n, p;
	node_info_t *node_ptr;
	partition_info_t *part_ptr;

	if (!node_buffer_ptr || (node_buffer_ptr->record_count == 0) ||
	    !part_buffer_ptr || (part_buffer_ptr->record_count == 0))
		return;

	for (n = 0, node_ptr = node_buffer_ptr->node_array;
	     n < node_buffer_ptr->record_count; n++, node_ptr++) {
		xfree(node_ptr->partitions);
	}

	/*
	 * Iterate through the partitions in the slurm.conf using "p".  The
	 * partition has an array of node index pairs to specify the range.
	 * Using "i", iterate by two's through the node list to get the
	 * begin-end node range.  Using "j", iterate through the node range
	 * and add the partition name to the node's partition list.  If the
	 * node on the partition is a singleton (i.e. Nodes=node1), the
	 * begin-end range are both the same node index value.
	 */
	for (p = 0, part_ptr = part_buffer_ptr->partition_array;
	     p < part_buffer_ptr->record_count; p++, part_ptr++) {
		for (i = 0; ; i += 2) {
			if (part_ptr->node_inx[i] == -1)
				break;
			for (j = part_ptr->node_inx[i];
			     j <= part_ptr->node_inx[i+1]; j++) {
				char *sep = "";
				if ((j < 0) ||
				    (j >= node_buffer_ptr->record_count))
					continue;
				node_ptr = node_buffer_ptr->node_array + j;
				if (node_ptr->partitions)
					sep = ",";
				xstrfmtcat(node_ptr->partitions, "%s%s", sep,
					   part_ptr->name);
			}
		}
	}
}

/*
 * slurm_sprint_node_table - output information about a specific Slurm nodes
 *	based upon message as loaded using slurm_load_node
 * IN node_ptr - an individual node information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
char *slurm_sprint_node_table(node_info_t *node_ptr, int one_liner)
{
	char time_str[256];
	char *out = NULL, *reason_str = NULL, *complete_state = NULL;
	char *line_end = (one_liner) ? " " : "\n   ";

	/****** Line 1 ******/
	xstrfmtcat(out, "NodeName=%s ", node_ptr->name);

	if (node_ptr->arch)
		xstrfmtcat(out, "Arch=%s ", node_ptr->arch);

	if (node_ptr->cpu_bind) {
		char tmp_str[128];
		slurm_sprint_cpu_bind_type(tmp_str, node_ptr->cpu_bind);
		xstrfmtcat(out, "CpuBind=%s ", tmp_str);
	}

	xstrfmtcat(out, "CoresPerSocket=%u ", node_ptr->cores);

	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "CPUAlloc=%u CPUEfctv=%u CPUTot=%u ",
		   node_ptr->alloc_cpus, node_ptr->cpus_efctv, node_ptr->cpus);

	xstrfmtcat(out, "CPULoad=%.2f", (node_ptr->cpu_load / 100.0));

	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "AvailableFeatures=%s", node_ptr->features);
	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "ActiveFeatures=%s", node_ptr->features_act);
	xstrcat(out, line_end);

	/****** Line ******/
	xstrfmtcat(out, "Gres=%s", node_ptr->gres);
	xstrcat(out, line_end);

	/****** Line (optional) ******/
	if (node_ptr->gres_drain) {
		xstrfmtcat(out, "GresDrain=%s", node_ptr->gres_drain);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	if (node_ptr->gres_used) {
		xstrfmtcat(out, "GresUsed=%s", node_ptr->gres_used);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	{
		bool line_used = false;

		if (node_ptr->node_addr) {
			xstrfmtcat(out, "NodeAddr=%s ", node_ptr->node_addr);
			line_used = true;
		}

		if (node_ptr->node_hostname) {
			xstrfmtcat(out, "NodeHostName=%s ",
				   node_ptr->node_hostname);
			line_used = true;
		}

		if (node_ptr->bcast_address) {
			xstrfmtcat(out, "BcastAddr=%s ", node_ptr->bcast_address);
			line_used = true;
		}

		if (node_ptr->port != slurm_conf.slurmd_port) {
			xstrfmtcat(out, "Port=%u ", node_ptr->port);
			line_used = true;
		}

		if (node_ptr->version) {
			xstrfmtcat(out, "Version=%s", node_ptr->version);
			line_used = true;
		}

		if (line_used)
			xstrcat(out, line_end);
	}

	/****** Line ******/
	if (node_ptr->os) {
		xstrfmtcat(out, "OS=%s ", node_ptr->os);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	xstrfmtcat(out, "RealMemory=%"PRIu64" AllocMem=%"PRIu64" ",
		   node_ptr->real_memory, node_ptr->alloc_memory);

	if (node_ptr->free_mem == NO_VAL64)
		xstrcat(out, "FreeMem=N/A ");
	else
		xstrfmtcat(out, "FreeMem=%"PRIu64" ", node_ptr->free_mem);

	xstrfmtcat(out, "Sockets=%u Boards=%u",
		   node_ptr->sockets, node_ptr->boards);
	xstrcat(out, line_end);

	/****** core & memory specialization Line (optional) ******/
	if (node_ptr->core_spec_cnt || node_ptr->cpu_spec_list ||
	    node_ptr->mem_spec_limit) {
		if (node_ptr->core_spec_cnt) {
			xstrfmtcat(out, "CoreSpecCount=%u ",
				   node_ptr->core_spec_cnt);
		}
		if (node_ptr->cpu_spec_list) {
			xstrfmtcat(out, "CPUSpecList=%s ",
				   node_ptr->cpu_spec_list);
		}
		if (node_ptr->mem_spec_limit) {
			xstrfmtcat(out, "MemSpecLimit=%"PRIu64"",
				   node_ptr->mem_spec_limit);
		}
		xstrcat(out, line_end);
	}

	/* cores per gpu (optional) */
	if (node_ptr->res_cores_per_gpu) {
		xstrfmtcat(out, "RestrictedCoresPerGPU=%u(%s) ",
			   node_ptr->res_cores_per_gpu, node_ptr->gpu_spec);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	complete_state = node_state_string_complete(node_ptr->node_state);
	xstrfmtcat(out, "State=%s ThreadsPerCore=%u TmpDisk=%u Weight=%u ",
		   complete_state, node_ptr->threads, node_ptr->tmp_disk,
		   node_ptr->weight);
	xfree(complete_state);

	if (node_ptr->owner == NO_VAL) {
		xstrcat(out, "Owner=N/A ");
	} else {
		char *user_name = uid_to_string((uid_t) node_ptr->owner);
		xstrfmtcat(out, "Owner=%s(%u) ", user_name, node_ptr->owner);
		xfree(user_name);
	}

	xstrfmtcat(out, "MCS_label=%s",
		   (node_ptr->mcs_label == NULL) ? "N/A" : node_ptr->mcs_label);

	xstrcat(out, line_end);

	/****** Line ******/
	if ((node_ptr->next_state != NO_VAL) &&
	    (IS_NODE_REBOOT_REQUESTED(node_ptr) ||
	     IS_NODE_REBOOT_ISSUED(node_ptr))) {
		xstrfmtcat(out, "NextState=%s",
			   node_state_string(node_ptr->next_state));
		xstrcat(out, line_end);
	}

	/****** Line ******/
	if (node_ptr->partitions) {
		xstrfmtcat(out, "Partitions=%s ", node_ptr->partitions);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	if (node_ptr->boot_time) {
		slurm_make_time_str((time_t *)&node_ptr->boot_time,
				    time_str, sizeof(time_str));
		xstrfmtcat(out, "BootTime=%s ", time_str);
	} else {
		xstrcat(out, "BootTime=None ");
	}

	if (node_ptr->slurmd_start_time) {
		slurm_make_time_str ((time_t *)&node_ptr->slurmd_start_time,
				     time_str, sizeof(time_str));
		xstrfmtcat(out, "SlurmdStartTime=%s", time_str);
	} else {
		xstrcat(out, "SlurmdStartTime=None");
	}
	xstrcat(out, line_end);

	/****** Line ******/
	slurm_make_time_str((time_t *)&node_ptr->last_busy, time_str,
			    sizeof(time_str));
	xstrfmtcat(out, "LastBusyTime=%s ", time_str);
	if (node_ptr->resume_after) {
		slurm_make_time_str((time_t *)&node_ptr->resume_after, time_str,
				    sizeof(time_str));
		xstrfmtcat(out, "ResumeAfterTime=%s", time_str);
	} else {
		xstrcat(out, "ResumeAfterTime=None");
	}
	xstrcat(out, line_end);

	/****** TRES Line ******/
	xstrfmtcat(out, "CfgTRES=%s", node_ptr->tres_fmt_str);
	xstrcat(out, line_end);
	xstrfmtcat(out, "AllocTRES=%s",
		   (node_ptr->alloc_tres_fmt_str) ?
		    node_ptr->alloc_tres_fmt_str : "");
	xstrcat(out, line_end);

	/****** Power Consumption Line ******/
	if (!node_ptr->energy || node_ptr->energy->current_watts == NO_VAL)
		xstrcat(out, "CurrentWatts=n/a AveWatts=n/a");
	else
		xstrfmtcat(out, "CurrentWatts=%u AveWatts=%u",
				node_ptr->energy->current_watts,
				node_ptr->energy->ave_watts);

	/****** Line ******/
	if (node_ptr->reason && node_ptr->reason[0])
		xstrcat(reason_str, node_ptr->reason);
	if (reason_str) {
		int inx = 1;
		char *save_ptr = NULL, *tok, *user_name;
		xstrcat(out, line_end);
		tok = strtok_r(reason_str, "\n", &save_ptr);
		while (tok) {
			if (inx == 1) {
				xstrcat(out, "Reason=");
			} else {
				xstrcat(out, line_end);
				xstrcat(out, "       ");
			}
			xstrfmtcat(out, "%s", tok);
			if ((inx++ == 1) && node_ptr->reason_time) {
				user_name = uid_to_string(node_ptr->reason_uid);
				slurm_make_time_str((time_t *)&node_ptr->reason_time,
						    time_str, sizeof(time_str));
				xstrfmtcat(out, " [%s@%s]", user_name, time_str);
				xfree(user_name);
			}
			tok = strtok_r(NULL, "\n", &save_ptr);
		}
		xfree(reason_str);
	}

	/****** Line (optional) ******/
	if (node_ptr->comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Comment=%s", node_ptr->comment);
	}

	/****** Line (optional) ******/
	if (node_ptr->extra) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Extra=%s", node_ptr->extra);
	}

	/****** Line (optional) ******/
	if (node_ptr->instance_id || node_ptr->instance_type) {
		xstrcat(out, line_end);

		if (node_ptr->instance_id)
			xstrfmtcat(out, "InstanceId=%s ",
				   node_ptr->instance_id);
		if (node_ptr->instance_type)
			xstrfmtcat(out, "InstanceType=%s",
				   node_ptr->instance_type);
	}

	/****** Line (optional) ******/
	if (node_ptr->resv_name) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "ReservationName=%s", node_ptr->resv_name);
	}

	/****** TLS certificate info ******/
	if (node_ptr->cert_flags || node_ptr->cert_last_renewal) {
		bool token_set = node_ptr->cert_flags & NODE_CERT_TOKEN_SET;
		xstrcat(out, line_end);
		xstrfmtcat(out, "TLSCertTokenSet=%s ",
			   token_set ? "Yes" : "No");
		slurm_make_time_str((time_t *) &node_ptr->cert_last_renewal,
				    time_str, sizeof(time_str));
		xstrfmtcat(out, "TLSCertLastRenewal=%s", time_str);
	}

	/****** Line (optional) ******/
	if (node_ptr->topology_str) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Topology=%s", node_ptr->topology_str);
	}

	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}

static void _set_node_mixed_op(node_info_t *node_ptr)
{
	uint16_t idle_cpus = 0;
	bool make_mixed = false;

	xassert(node_ptr);

	/* Node is blank/hidden (e.g. blank dynamic node) */
	if (!node_ptr->name)
		return;

	idle_cpus = node_ptr->cpus_efctv - node_ptr->alloc_cpus;

	if (idle_cpus && (idle_cpus < node_ptr->cpus_efctv))
		make_mixed = true;
	if (node_ptr->alloc_tres_fmt_str && (idle_cpus == node_ptr->cpus_efctv))
		make_mixed = true;
	if (make_mixed) {
		node_ptr->node_state &= NODE_STATE_FLAGS;
		node_ptr->node_state |= NODE_STATE_MIXED;
	}
}

static void _set_node_mixed(node_info_msg_t *resp)
{
	node_info_t *node_ptr = NULL;
	int i;

	if (!resp)
		return;

	for (i = 0, node_ptr = resp->node_array;
	     i < resp->record_count; i++, node_ptr++) {
		_set_node_mixed_op(node_ptr);
	}
}

static int _load_cluster_nodes(slurm_msg_t *req_msg,
			       node_info_msg_t **node_info_msg_pptr,
			       slurmdb_cluster_rec_t *cluster,
			       uint16_t show_flags)
{
	slurm_msg_t resp_msg;
	int rc;

	slurm_msg_t_init(&resp_msg);

	if (slurm_send_recv_controller_msg(req_msg, &resp_msg, cluster) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_NODE_INFO:
		*node_info_msg_pptr = (node_info_msg_t *) resp_msg.data;
		if (show_flags & SHOW_MIXED)
			_set_node_mixed(*node_info_msg_pptr);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*node_info_msg_pptr = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/* Maintain a consistent ordering of records */
static int _sort_by_cluster_inx(void *x, void *y)
{
	load_node_resp_struct_t *resp_x = *(load_node_resp_struct_t **) x;
	load_node_resp_struct_t *resp_y = *(load_node_resp_struct_t **) y;

	if (resp_x->cluster_inx > resp_y->cluster_inx)
		return -1;
	if (resp_x->cluster_inx < resp_y->cluster_inx)
		return 1;
	return 0;
}

/* Thread to read node information from some cluster */
static void *_load_node_thread(void *args)
{
	load_node_req_struct_t *load_args = (load_node_req_struct_t *) args;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	node_info_msg_t *new_msg = NULL;
	int i, rc;

	if ((rc = _load_cluster_nodes(load_args->req_msg, &new_msg, cluster,
				      load_args->show_flags)) || !new_msg) {
		verbose("Error reading node information from cluster %s: %s",
			cluster->name, slurm_strerror(rc));
	} else {
		load_node_resp_struct_t *node_resp;
		for (i = 0; i < new_msg->record_count; i++) {
			if (!new_msg->node_array[i].cluster_name) {
				new_msg->node_array[i].cluster_name =
					xstrdup(cluster->name);
			}
		}
		node_resp = xmalloc(sizeof(load_node_resp_struct_t));
		node_resp->cluster_inx = load_args->cluster_inx;
		node_resp->new_msg = new_msg;
		list_append(load_args->resp_msg_list, node_resp);
	}
	xfree(args);

	return NULL;
}

static int _load_fed_nodes(slurm_msg_t *req_msg,
			   node_info_msg_t **node_info_msg_pptr,
			   uint16_t show_flags, char *cluster_name,
			   slurmdb_federation_rec_t *fed)
{
	int cluster_inx = 0, i;
	load_node_resp_struct_t *node_resp;
	node_info_msg_t *orig_msg = NULL, *new_msg = NULL;
	uint32_t new_rec_cnt;
	slurmdb_cluster_rec_t *cluster;
	list_itr_t *iter;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	load_node_req_struct_t *load_args;
	list_t *resp_msg_list;

	*node_info_msg_pptr = NULL;

	/* Spawn one pthread per cluster to collect node information */
	resp_msg_list = list_create(NULL);
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */

		load_args = xmalloc(sizeof(load_node_req_struct_t));
		load_args->cluster = cluster;
		load_args->cluster_inx = cluster_inx++;
		load_args->req_msg = req_msg;
		load_args->resp_msg_list = resp_msg_list;
		load_args->show_flags = show_flags;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_node_thread, load_args);
		pthread_count++;
	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		slurm_thread_join(load_thread[i]);
	xfree(load_thread);

	/* Maintain a consistent cluster/node ordering */
	list_sort(resp_msg_list, _sort_by_cluster_inx);

	/* Merge the responses into a single response message */
	iter = list_iterator_create(resp_msg_list);
	while ((node_resp = (load_node_resp_struct_t *) list_next(iter))) {
		new_msg = node_resp->new_msg;
		if (!orig_msg) {
			orig_msg = new_msg;
			*node_info_msg_pptr = orig_msg;
		} else {
			/* Merge the node records */
			orig_msg->last_update = MIN(orig_msg->last_update,
						    new_msg->last_update);
			new_rec_cnt = orig_msg->record_count +
				      new_msg->record_count;
			if (new_msg->record_count) {
				orig_msg->node_array =
					xrealloc(orig_msg->node_array,
						 sizeof(node_info_t) *
						 new_rec_cnt);
				(void) memcpy(orig_msg->node_array +
					      orig_msg->record_count,
					      new_msg->node_array,
					      sizeof(node_info_t) *
					      new_msg->record_count);
				orig_msg->record_count = new_rec_cnt;
			}
			xfree(new_msg->node_array);
			xfree(new_msg);
		}
		xfree(node_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	if (!orig_msg)
		slurm_seterrno_ret(SLURM_ERROR);

	return SLURM_SUCCESS;
}

/*
 * slurm_load_node - issue RPC to get slurm all node configuration information
 *	if changed since update_time
 * IN update_time - time of current configuration data
 * OUT resp - place to store a node configuration pointer
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node(time_t update_time, node_info_msg_t **resp,
			   uint16_t show_flags)
{
	slurm_msg_t req_msg;
	node_info_request_msg_t req;
	char *cluster_name = NULL;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if (working_cluster_rec)
		cluster_name = working_cluster_rec->name;
	else
		cluster_name = slurm_conf.cluster_name;

	if ((show_flags & SHOW_FEDERATION) && !(show_flags & SHOW_LOCAL) &&
	    (slurm_load_federation(&ptr) == SLURM_SUCCESS) &&
	    cluster_in_federation(ptr, cluster_name)) {
		/* In federation. Need full info from all clusters */
		update_time = (time_t) 0;
		show_flags &= (~SHOW_LOCAL);
	} else {
		/* Report local cluster info only */
		show_flags |= SHOW_LOCAL;
		show_flags &= (~SHOW_FEDERATION);
	}

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_NODE_INFO;
	req_msg.data     = &req;

	if ((show_flags & SHOW_FEDERATION) && ptr) { /* "ptr" check for CLANG */
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_nodes(&req_msg, resp, show_flags, cluster_name,
				     fed);
	} else {
		rc = _load_cluster_nodes(&req_msg, resp, working_cluster_rec,
					 show_flags);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}

/*
 * slurm_load_node2 - equivalent to slurm_load_node() with addition
 *	of cluster record for communications in a federation
 */
extern int slurm_load_node2(time_t update_time, node_info_msg_t **resp,
			    uint16_t show_flags, slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t req_msg;
	node_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_NODE_INFO;
	req_msg.data     = &req;

	return _load_cluster_nodes(&req_msg, resp, cluster, show_flags);
}

/*
 * slurm_load_node_single - issue RPC to get slurm configuration information
 *	for a specific node
 * OUT resp - place to store a node configuration pointer
 * IN node_name - name of the node for which information is requested
 * IN show_flags - node filtering options
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_node_info_msg
 */
extern int slurm_load_node_single(node_info_msg_t **resp, char *node_name,
				  uint16_t show_flags)
{
	slurm_msg_t req_msg;
	node_info_single_msg_t req;

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.node_name    = node_name;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_NODE_INFO_SINGLE;
	req_msg.data     = &req;

	return _load_cluster_nodes(&req_msg, resp, working_cluster_rec,
				   show_flags);
}

/*
 * slurm_load_node_single2 - equivalent to slurm_load_node_single() with
 *	addition of cluster record for communications in a federation
 */
extern int slurm_load_node_single2(node_info_msg_t **resp, char *node_name,
				   uint16_t show_flags,
				   slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t req_msg;
	node_info_single_msg_t req;

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.node_name    = node_name;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_NODE_INFO_SINGLE;
	req_msg.data     = &req;

	return _load_cluster_nodes(&req_msg, resp, cluster, show_flags);
}

/*
 * slurm_get_node_energy - issue RPC to get the energy data of all
 * configured sensors on the target machine
 * IN  host  - name of node to query, NULL if localhost
 * IN  context_id - specific plugin to query.
 * IN  delta - Use cache if data is newer than this in seconds
 * OUT sensors_cnt - number of sensors
 * OUT energy - array of acct_gather_energy_t structures on success or
 *                NULL other wise
 * RET 0 on success or a slurm error code
 * NOTE: free the response using xfree
 */
extern int slurm_get_node_energy(char *host, uint16_t context_id,
				 uint16_t delta,
				 uint16_t *sensor_cnt,
				 acct_gather_energy_t **energy)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	acct_gather_energy_req_msg_t req;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *this_addr;

	xassert(sensor_cnt);
	xassert(energy);

	*sensor_cnt = 0;
	*energy = NULL;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (host)
		slurm_conf_get_addr(host, &req_msg.address, req_msg.flags);
	else if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		if ((this_addr = getenv("SLURMD_NODENAME"))) {
			slurm_conf_get_addr(this_addr, &req_msg.address,
					    req_msg.flags);
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
				       this_addr);
		}
	} else {
		char this_host[256];
		/*
		 *  Set request message address to slurmd on localhost
		 */
		gethostname_short(this_host, sizeof(this_host));
		this_addr = slurm_conf_get_nodeaddr(this_host);
		if (this_addr == NULL)
			this_addr = xstrdup("localhost");
		slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
			       this_addr);
		xfree(this_addr);
	}

	memset(&req, 0, sizeof(req));
	req.context_id   = context_id;
	req.delta        = delta;
	req_msg.msg_type = REQUEST_ACCT_GATHER_ENERGY;
	req_msg.data     = &req;
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if (rc != SLURM_SUCCESS) {
		error("slurm_get_node_energy: %m");
		if (resp_msg.auth_cred)
			auth_g_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		auth_g_destroy(resp_msg.auth_cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_ACCT_GATHER_ENERGY:
		*sensor_cnt = ((acct_gather_node_resp_msg_t *)
			       resp_msg.data)->sensor_cnt;
		*energy = ((acct_gather_node_resp_msg_t *)
			   resp_msg.data)->energy;
		((acct_gather_node_resp_msg_t *) resp_msg.data)->energy = NULL;
		slurm_free_acct_gather_node_resp_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

extern int slurm_get_node_alias_addrs(char *node_list,
				      slurm_node_alias_addrs_t **alias_addrs)
{
	int rc;
	slurm_msg_t req_msg, resp_msg;
	slurm_node_alias_addrs_t data = {.node_list = node_list};

	xassert(node_list);
	if (!node_list)
		return SLURM_SUCCESS;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req_msg.data = &data;
	req_msg.msg_type = REQUEST_NODE_ALIAS_ADDRS;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_NODE_ALIAS_ADDRS:
		*alias_addrs = resp_msg.data;
		resp_msg.data = NULL;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

extern int slurm_controller_hostlist_expansion(const char *hostlist,
					       char **expanded)
{
	int rc;
	slurm_msg_t req_msg, resp_msg;

	if (!hostlist)
		return SLURM_SUCCESS;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req_msg.data = (void *) hostlist;
	req_msg.msg_type = REQUEST_HOSTLIST_EXPANSION;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_HOSTLIST_EXPANSION:
		*expanded = resp_msg.data;
		resp_msg.data = NULL;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}
