/*****************************************************************************\
 *  node_mgr.c - manage the node records of slurm
 *	Note: there is a global node table (node_record_table_ptr), its
 *	hash table (node_hash_table), time stamp (last_node_update) and
 *	configuration list (config_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/drained_tres.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/hostlist.h"
#include "src/common/kv.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/pack.h"
#include "src/common/pack_data.h"
#include "src/common/parse_time.h"
#include "src/common/parse_value.h"
#include "src/common/power_action.h"
#include "src/common/read_config.h"
#include "src/common/sercli.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/state_save.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/conn.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/select.h"
#include "src/interfaces/serializer.h"
#include "src/interfaces/topology.h"

#include "src/stepmgr/gres_stepmgr.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sackd_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/trigger_mgr.h"

/* No need to change we always pack SLURM_PROTOCOL_VERSION */
#define NODE_STATE_VERSION        "PROTOCOL_VERSION"

#define DEFAULT_NODE_REG_MEM_PERCENT 100.0
#define DEFAULT_CLOUD_REG_MEM_PERCENT 90.0

/*
 * Starting size for the all-nodes reply. This is the largest response the
 * controller assembles, so it is sized for a real cluster up front rather
 * than left to grow into one xrealloc() per increment.
 */
#define BUF_MIN_NODES_BYTES (BUF_SIZE * 16)

static bool config_list_update = false;
static pthread_mutex_t config_list_update_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	slurm_msg_t *msg;
	part_record_t **visible_parts;
} pack_node_info_t;

typedef struct {
	list_t *failed_jobs;
	node_record_t *node_ptr;
} foreach_bind_gres_t;

/* A node's drained TRES while the event describing them is being built. */
typedef struct {
	uint64_t *drained_cnt; /* per TRES position, g_tres_count long */
	char *fallback_reason; /* reason for TRES that carry none */
	node_record_t *node_ptr;
	list_t *reasons; /* every distinct reason, as char * */
	bitstr_t *ref_idx_bits; /* indexes accumulated into the open group */
	char *ref_idx_name; /* TRES name of the open (IDX:) group, or NULL */
	char *ref_str; /* compact rendering of every drained TRES */
} drained_tres_args_t;

/* Global variables */
bitstr_t *asap_node_bitmap = NULL; /* bitmap of rebooting asap nodes */
bitstr_t *avail_node_bitmap = NULL;	/* bitmap of available nodes */
bitstr_t *bf_ignore_node_bitmap = NULL; /* bitmap of nodes to ignore during a
					 * backfill cycle */
bitstr_t *booting_node_bitmap = NULL;	/* bitmap of booting nodes */
bitstr_t *cg_node_bitmap    = NULL;	/* bitmap of completing nodes */
bitstr_t *cloud_node_bitmap = NULL;	/* bitmap of cloud nodes */
bitstr_t *external_node_bitmap = NULL;	/* bitmap of external nodes */
bitstr_t *future_node_bitmap = NULL;	/* bitmap of FUTURE nodes */
bitstr_t *idle_node_bitmap  = NULL;	/* bitmap of idle nodes */
bitstr_t *power_down_node_bitmap = NULL; /* bitmap of powered down nodes */
bitstr_t *rs_node_bitmap    = NULL; 	/* bitmap of resuming nodes */
bitstr_t *share_node_bitmap = NULL;  	/* bitmap of sharable nodes */
bitstr_t *up_node_bitmap    = NULL;  	/* bitmap of non-down nodes */
bitstr_t *power_up_node_bitmap = NULL;	/* bitmap of power_up requested nodes */

static int _delete_node_ptr(node_record_t *node_ptr);
static void	_drain_node(node_record_t *node_ptr, char *reason,
			    uint32_t reason_uid, time_t now);
static void    _make_node_unavail(node_record_t *node_ptr);
static void 	_make_node_down(node_record_t *node_ptr,
				time_t event_time);
static bool	_node_is_hidden(node_record_t *node_ptr,
				pack_node_info_t *pack_info);
static void 	_pack_node(node_record_t *dump_node_ptr, buf_t *buffer,
			   uint16_t protocol_version, uint16_t show_flags);
static void _reconcile_tres_drain(node_record_t *node_ptr, char *reason,
				  uid_t auth_uid, time_t now);
static void	_sync_bitmaps(node_record_t *node_ptr, int job_count);
static void	_update_config_ptr(bitstr_t *bitmap,
				   config_record_t *config_ptr);
static int _update_node_gres(char *node_names, char *gres, uid_t auth_uid);
static int _update_node_suspend_time(char *node_names, uint32_t suspend_time);
static char *_suspend_time_str(uint32_t suspend_time, char *buf, size_t len);
static int	_update_node_weight(char *node_names, uint32_t weight);
static bool 	_valid_node_state_change(uint32_t old, uint32_t new);

static char *_get_msg_hostname(slurm_msg_t *msg)
{
	slurm_addr_t *addr = &msg->address;
	char *name = NULL;

	if (addr->ss_family == AF_UNSPEC) {
		int fd = conn_g_get_fd(msg->conn);
		(void) slurm_get_peer_addr(fd, addr);
	}
	if (addr->ss_family != AF_UNSPEC) {
		name = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(addr, name, INET6_ADDRSTRLEN);
	}

	return name;
}

static void _dump_cluster_settings(buf_t *buffer)
{
	packstr(slurm_conf.suspend_exc_nodes, buffer);
	packstr(slurm_conf.suspend_exc_parts, buffer);
	packstr(slurm_conf.suspend_exc_states, buffer);
}

static int _load_cluster_settings(bool state_only,
				  buf_t *buffer,
				  uint16_t protocol_version)
{
	char *suspend_exc_nodes = NULL;
	char *suspend_exc_parts = NULL;
	char *suspend_exc_states = NULL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&suspend_exc_nodes, buffer);
		safe_unpackstr(&suspend_exc_parts, buffer);
		safe_unpackstr(&suspend_exc_states, buffer);

		if (!state_only) {
			xfree(slurm_conf.suspend_exc_nodes);
			slurm_conf.suspend_exc_nodes = suspend_exc_nodes;
			suspend_exc_nodes = NULL;

			xfree(slurm_conf.suspend_exc_parts);
			slurm_conf.suspend_exc_parts = suspend_exc_parts;
			suspend_exc_parts = NULL;

			xfree(slurm_conf.suspend_exc_states);
			slurm_conf.suspend_exc_states = suspend_exc_states;
			suspend_exc_states = NULL;
		} else {
			xfree(suspend_exc_nodes);
			xfree(suspend_exc_parts);
			xfree(suspend_exc_states);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	xfree(suspend_exc_nodes);
	xfree(suspend_exc_parts);
	xfree(suspend_exc_states);

	return SLURM_ERROR;
}

/* dump_all_node_state - save the state of all nodes to file */
int dump_all_node_state ( void )
{
	/* Save high-water mark to avoid buffer growth with copies */
	static uint32_t high_buffer_size = (1024 * 1024);
	int error_code = 0, inx;
	node_record_t *node_ptr;
	/* Locks: Read config and node */
	slurmctld_lock_t node_read_lock = { READ_LOCK, NO_LOCK, READ_LOCK,
					    NO_LOCK, NO_LOCK };
	buf_t *buffer = init_buf(high_buffer_size);
	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	packstr(NODE_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time (NULL), buffer);

	/* write node records to buffer */
	lock_slurmctld (node_read_lock);
	_dump_cluster_settings(buffer);
	sackd_mgr_dump_state(buffer, SLURM_PROTOCOL_VERSION);
	pack32(node_record_count, buffer);
	for (inx = 0; (node_ptr = next_node(&inx)); inx++) {
		xassert (node_ptr->magic == NODE_MAGIC);
		xassert (node_ptr->config_ptr->magic == CONFIG_MAGIC);
		pack32(inx, buffer);
		node_record_pack_state(node_ptr, SLURM_PROTOCOL_VERSION,
				       buffer);
	}
	unlock_slurmctld (node_read_lock);

	error_code = save_buf_to_state("node_state", buffer, &high_buffer_size);

	FREE_NULL_BUFFER(buffer);
	END_TIMER2(__func__);
	return error_code;
}

static void _queue_consolidate_config_list(void)
{
	slurm_mutex_lock(&config_list_update_mutex);
	config_list_update = true;
	slurm_mutex_unlock(&config_list_update_mutex);
}

static bool _get_config_list_update(void)
{
       bool rc;

       slurm_mutex_lock(&config_list_update_mutex);
       rc = config_list_update;
       slurm_mutex_unlock(&config_list_update_mutex);

       return rc;
}

static int _validate_nodes_vs_nodeset(char *nodes_str)
{
	hostlist_t *nodes = NULL;
	slurm_conf_nodeset_t **ptr;
	int count_nodeset = slurm_conf_nodeset_array(&ptr);

	if (!nodes_str)
		return SLURM_SUCCESS;

	nodes = hostlist_create(nodes_str);
	for (int i = 0; i < count_nodeset; i++) {
		if (hostlist_find(nodes, ptr[i]->name) != -1) {
			error("NodeSet with name %s overlaps with an existing NodeName",
			      ptr[i]->name);
			hostlist_destroy(nodes);
			return ESLURM_INVALID_NODE_NAME;
		}
	}

	hostlist_destroy(nodes);
	return SLURM_SUCCESS;
}

/*
 * Collapse a CPU-indexed bitmap into schedulable cores. A core is selected
 * when any of its threads is set.
 */
static bitstr_t *_fold_cpus_to_cores(bitstr_t *cpu_map, uint16_t tpc, int cores)
{
	bitstr_t *core_map = bit_alloc(cores);
	bitoff_t ncpus = bit_size(cpu_map);

	for (int core = 0; core < cores; core++) {
		for (uint16_t thread = 0; thread < tpc; thread++) {
			bitoff_t cpu = ((bitoff_t) core * tpc) + thread;

			if ((cpu < ncpus) && bit_test(cpu_map, cpu)) {
				bit_set(core_map, core);
				break;
			}
		}
	}

	return core_map;
}

/*
 * Adopt the restored CPU drain state as schedulable cores.
 *
 * The map is indexed by core (abstract CPU id / tpc). An edit to CPUs= or
 * ThreadsPerCore resizes it in core space, so a core that still exists stays
 * drained instead of having its thread bits reinterpreted as different cores.
 */
static void _adopt_cpu_drain_state(node_record_t *node_ptr,
				   node_record_t *node_state_rec)
{
	bitstr_t *map = node_state_rec->core_drain_bitmap;
	int saved_bits = map ? bit_size(map) : 0;
	int cores = node_ptr->tot_cores;
	int cut = 0;

	node_state_rec->core_drain_bitmap = NULL;

	FREE_NULL_BITMAP(node_ptr->core_drain_bitmap);
	FREE_NULL_LIST(node_ptr->cpu_drain_groups);

	node_ptr->cpu_drain_groups = node_state_rec->cpu_drain_groups;
	node_state_rec->cpu_drain_groups = NULL;

	if (map && (saved_bits != cores)) {
		if (cores < saved_bits)
			cut = bit_set_count_range(map, cores, saved_bits);
		if (cut)
			info("node %s now has %d cores, so %d drained cores past them are no longer drained",
			     node_ptr->name, cores, cut);
		else
			debug("node %s has %d schedulable cores but its saved drain state covers %d; resizing",
			      node_ptr->name, cores, saved_bits);
		bit_realloc(map, cores);
	}

	/*
	 * The groups are sized to the same core count as the map, but a
	 * truncated or hand-edited state file can disagree. Resize
	 * unconditionally so the two always describe the same cores.
	 */
	drained_tres_groups_resize(&node_ptr->cpu_drain_groups, cores);

	/*
	 * Hold the invariant the rest of the code relies on: the map exists
	 * only while at least one core is drained, and it never outlives its
	 * reasons. Shrinking the node can drop the last drained core.
	 */
	if (!map || !bit_set_count(map)) {
		FREE_NULL_BITMAP(map);
		FREE_NULL_LIST(node_ptr->cpu_drain_groups);
	}

	node_ptr->core_drain_bitmap = map;
}

/*
 * RealMemory= may have shrunk below what was drained while the controller
 * was down. Clamp, so the node never reports more drained memory than it has.
 */
static void _adopt_mem_drain_state(node_record_t *node_ptr,
				   node_record_t *node_state_rec)
{
	uint64_t saved = node_state_rec->mem_drain.count;
	uint64_t clamped = MIN(saved, node_ptr->real_memory);

	drained_tres_count_clear(&node_ptr->mem_drain);

	if (clamped != saved)
		info("Node %s had %"PRIu64"MB of memory drained but only has %"PRIu64"MB; clamping",
		     node_ptr->name, saved, node_ptr->real_memory);

	if (!clamped) {
		drained_tres_count_clear(&node_state_rec->mem_drain);
		return;
	}

	node_ptr->mem_drain.count = clamped;
	node_ptr->mem_drain.reason = node_state_rec->mem_drain.reason;
	node_state_rec->mem_drain.reason = NULL;
	node_state_rec->mem_drain.count = 0;
}

/*
 * load_all_node_state - Load the node state from file, recover on slurmctld
 *	restart. Execute this after loading the configuration file data.
 *	Data goes into common storage.
 * IN state_only - if true, overwrite only node state and reason
 *	Use this to overwrite the "UNKNOWN state typically used in slurm.conf
 * RET SUCCESS or error code
 */
extern int load_all_node_state ( bool state_only )
{
	char *state_file;
	int error_code = SLURM_SUCCESS, node_cnt = 0;

	node_record_t *node_ptr;
	time_t time_stamp;
	buf_t *buffer;
	char *ver_str = NULL;
	hostlist_t *down_nodes = NULL;
	uint16_t protocol_version = NO_VAL16;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	/* read the file */
	buffer = state_save_open("node_state", &state_file);
	if (!buffer) {
		if ((clustername_existed == 1) && (!ignore_state_errors))
			fatal("No node state file (%s) to recover", state_file);
		info("No node state file (%s) to recover", state_file);
		xfree(state_file);
		return ENOENT;
	}
	xfree(state_file);

	safe_unpackstr(&ver_str, buffer);
	debug3("Version string in node_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, NODE_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);

	if (!protocol_version || (protocol_version == NO_VAL16)) {
		if (!ignore_state_errors)
			fatal("Can not recover node state, data version incompatible, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		error("*****************************************************");
		error("Can not recover node state, data version incompatible");
		error("*****************************************************");
		xfree(ver_str);
		FREE_NULL_BUFFER(buffer);
		return EFAULT;
	}
	xfree(ver_str);

	safe_unpack_time (&time_stamp, buffer);

	if (_load_cluster_settings(state_only, buffer, protocol_version))
		goto unpack_error;

	if (sackd_mgr_load_state(buffer, protocol_version))
		goto unpack_error;

	xfree(node_old_to_new_map);
	old_node_record_count = 0;
	if (protocol_version >= SLURM_26_05_PROTOCOL_VERSION) {
		safe_unpack32(&old_node_record_count, buffer);
		if (old_node_record_count) {
			node_old_to_new_map =
				xcalloc(old_node_record_count, sizeof(int));
			memset(node_old_to_new_map, -1,
			       old_node_record_count * sizeof(int));
		}
	}

	while (remaining_buf (buffer) > 0) {
		node_record_t *node_state_rec = NULL;
		uint32_t node_state, base_state;
		uint32_t old_inx = NO_VAL;

		if (protocol_version >= SLURM_26_05_PROTOCOL_VERSION)
			safe_unpack32(&old_inx, buffer);

		if (node_record_unpack((void *) &node_state_rec,
				       protocol_version, buffer)) {
			error("failed to unpack node state");
			goto unpack_error;
		}

		node_state = node_state_rec->node_state;
		base_state = node_state & NODE_STATE_BASE;

		/* validity test as possible */
		if ((node_state_rec->cpus == 0) ||
		    (node_state_rec->boards == 0) ||
		    (node_state_rec->tot_sockets == 0) ||
		    (node_state_rec->cores == 0) ||
		    (node_state_rec->threads == 0) ||
		    (base_state  >= NODE_STATE_END)) {
			error("Invalid data for node %s: procs=%u, boards=%u, "
			       "sockets=%u, cores=%u, threads=%u, state=%u",
				node_state_rec->name, node_state_rec->cpus,
				node_state_rec->boards,
				node_state_rec->tot_sockets,
				node_state_rec->cores, node_state_rec->threads,
				node_state);
			error("No more node data will be processed from the checkpoint file");
			purge_node_rec(node_state_rec);
			goto unpack_error;

		}

		/*
		 * When a NodeSet is defined with the same name than
		 * an existing node name found in the state file, and which
		 * might not be in the configuration, then fatal. This can
		 * happen for example when adding a dynamic node, then changing
		 * slurm.conf by adding a NodeSet with the same name than the
		 * dynamic node, and then restarting.
		 */
		if (_validate_nodes_vs_nodeset(node_state_rec->name) !=
		    SLURM_SUCCESS) {
			fatal("This error might happen when names overlap with dynamic nodes. Please rename the NodeSet in slurm.conf.");
		}

		if (node_state & NODE_STATE_DYNAMIC_NORM) {
			/*
			 * Create node record to restore node into.
			 *
			 * cpu_spec_list, core_spec_cnt, port are only restored
			 * for dynamic nodes, otherwise always trust slurm.conf
			 */
			config_record_t *config_ptr =
				config_record_from_node_record(node_state_rec);

			if ((error_code = add_node_record(node_state_rec->name,
							  config_ptr,
							  &node_ptr))) {
				error("%s (%s)",
				      slurm_strerror(error_code),
				      node_state_rec->name);
				error_code = SLURM_SUCCESS;
				list_delete_ptr(config_list, config_ptr);
			} else {
				if (node_state_rec->port) {
					node_ptr->port = node_state_rec->port;
					/*
					 * Get node in conf hash tables with
					 * port. set_node_comm_name() doesn't
					 * add the node with the port.
					 */
					slurm_conf_add_node(node_ptr);
				}
				/*
				 * add_node_record() populates gres_list but we
				 * want to use the gres_list from state.
				 */
				FREE_NULL_LIST(node_ptr->gres_list);
				/*
				 * The HRES a dynamic node was created with is
				 * its baseline HRES layer membership. It is
				 * only in the state, so always restore it.
				 */
				node_ptr->hres_list_config =
					node_state_rec->hres_list_config;
				node_state_rec->hres_list_config = NULL;
				_queue_consolidate_config_list();
			}
		}

		/* find record and perform update */
		node_ptr = find_node_record (node_state_rec->name);

		/* Record mapping of the old node index to the new node index */
		if (node_ptr && (old_inx != NO_VAL) && node_old_to_new_map &&
		    (old_inx < old_node_record_count))
			node_old_to_new_map[old_inx] = node_ptr->index;

		if (node_ptr == NULL) {
			error ("Node %s has vanished from configuration",
			       node_state_rec->name);
		} else if (state_only &&
			   !(node_state & NODE_STATE_DYNAMIC_NORM)) {
			uint32_t orig_flags;
			if ((IS_NODE_CLOUD(node_ptr) ||
			    (node_state & NODE_STATE_DYNAMIC_FUTURE)) &&
			    node_state_rec->comm_name &&
			    node_state_rec->node_hostname) {
				/* Recover NodeAddr and NodeHostName */
				set_node_comm_name(
					node_ptr,
					node_state_rec->comm_name,
					node_state_rec->node_hostname);
			}
			if (IS_NODE_FUTURE(node_ptr)) {
				/* preserve state for conf FUTURE nodes */
				node_ptr->node_state = node_state;
			} else if (IS_NODE_CLOUD(node_ptr)) {
				/*
				 * Replace FUTURE state with new state (idle),
				 * but preserve recovered state flags
				 * (e.g. POWER*).
				 */
				if ((node_state & NODE_STATE_BASE) ==
				    NODE_STATE_FUTURE) {
					node_state =
						((node_ptr->node_state &
						  NODE_STATE_BASE) |
						 (node_state &
						  NODE_STATE_FLAGS));

					/*
					 * If node was FUTURE, then it wasn't up
					 * so mark it as powered down.
					 */
					node_state |= NODE_STATE_POWERED_DOWN;
				}

				node_ptr->node_state =
					node_state | NODE_STATE_CLOUD;

				/*
				 * Preserve dynamic topology for cloud nodes.
				 * If the node is powered down then keep topo
				 * as configured by current config files to stop
				 * old slurm.conf node topology from persisting.
				 * Ignore topology_orig_str for similar reason
				 * for topology.conf config.
				 */
				if (node_state_rec->topology_str &&
				    !IS_NODE_POWERING_DOWN(node_ptr) &&
				    !IS_NODE_POWERED_DOWN(node_ptr)) {
					xfree(node_ptr->topology_str);
					node_ptr->topology_str =
						node_state_rec->topology_str;
					node_state_rec->topology_str = NULL;
				}
				/* Preserve cloud node HRES like topology. */
				if (node_state_rec->hres_list &&
				    !IS_NODE_POWERING_DOWN(node_ptr) &&
				    !IS_NODE_POWERED_DOWN(node_ptr)) {
					FREE_NULL_LIST(node_ptr->hres_list);
					node_ptr->hres_list =
						node_state_rec->hres_list;
					node_state_rec->hres_list = NULL;
				}
			} else if (IS_NODE_UNKNOWN(node_ptr)) {
				if (base_state == NODE_STATE_DOWN) {
					orig_flags = node_ptr->node_state &
						     NODE_STATE_FLAGS;
					node_ptr->node_state = NODE_STATE_DOWN
						| orig_flags;
				}
				if (node_state & NODE_STATE_DRAIN)
					 node_ptr->node_state |=
						 NODE_STATE_DRAIN;
				if (node_state & NODE_STATE_FAIL)
					node_ptr->node_state |=
						NODE_STATE_FAIL;
				if ((node_state & NODE_STATE_POWERED_DOWN) ||
				    (node_state & NODE_STATE_POWERING_DOWN)) {
					uint32_t power_flag =
						node_state &
						(NODE_STATE_POWERED_DOWN |
						 NODE_STATE_POWERING_DOWN);
					if (IS_NODE_UNKNOWN(node_ptr)) {
						orig_flags =
							node_ptr->node_state &
							NODE_STATE_FLAGS;
						node_ptr->node_state =
							NODE_STATE_IDLE |
							orig_flags | power_flag;
					} else {
						node_ptr->node_state |=
							power_flag;
					}
					/* Recover hardware state for powered
					 * down nodes */
					node_ptr->cpus = node_state_rec->cpus;
					node_ptr->boards =
						node_state_rec->boards;
					node_ptr->tot_sockets =
						node_state_rec->tot_sockets;
					node_ptr->cores = node_state_rec->cores;
					node_ptr->tot_cores =
						node_state_rec->tot_cores;
					node_ptr->threads =
						node_state_rec->threads;
					node_ptr->real_memory =
						node_state_rec->real_memory;
					node_ptr->res_cores_per_gpu =
						node_state_rec->
						res_cores_per_gpu;
					node_ptr->tmp_disk =
						node_state_rec->tmp_disk;
				}
				if (node_state & NODE_STATE_MAINT)
					node_ptr->node_state |= NODE_STATE_MAINT;
				if (node_state & NODE_STATE_REBOOT_REQUESTED)
					node_ptr->node_state |=
						NODE_STATE_REBOOT_REQUESTED;
				if (node_state & NODE_STATE_REBOOT_ISSUED)
					node_ptr->node_state |=
						NODE_STATE_REBOOT_ISSUED;
				/*
				 * A powering up node has not responded since
				 * its boot was requested. Restore NO_RESPOND
				 * with POWERING_UP, because ping_nodes() skips
				 * the node and ResumeTimeout only downs it
				 * while NO_RESPOND is set. Without the flag a
				 * node that never boots stays POWERING_UP.
				 */
				if (node_state & NODE_STATE_POWERING_UP) {
					node_ptr->node_state |=
						(NODE_STATE_POWERING_UP |
						 NODE_STATE_NO_RESPOND);
				}
			}

			if (!node_ptr->extra) {
				node_ptr->extra = node_state_rec->extra;
				node_state_rec->extra = NULL;
			}

			if (!node_ptr->cert_token) {
				node_ptr->cert_token =
					node_state_rec->cert_token;
				node_state_rec->cert_token = NULL;
			}

			if (!node_ptr->comment) {
				node_ptr->comment = node_state_rec->comment;
				node_state_rec->comment = NULL;
			}

			if (!node_ptr->kv) {
				node_ptr->kv = node_state_rec->kv;
				node_state_rec->kv = NULL;
			}

			if (!node_ptr->instance_id) {
				node_ptr->instance_id =
					node_state_rec->instance_id;
				node_state_rec->instance_id = NULL;
			}

			if (!node_ptr->instance_type) {
				node_ptr->instance_type =
					node_state_rec->instance_type;
				node_state_rec->instance_type = NULL;
			}

			if (node_ptr->reason == NULL) {
				node_ptr->reason = node_state_rec->reason;
				node_state_rec->reason = NULL;
				node_ptr->reason_time =
					node_state_rec->reason_time;
				node_ptr->reason_uid =
					node_state_rec->reason_uid;
			}

			xfree(node_ptr->features_act);
			node_ptr->features_act = node_state_rec->features_act;
			node_state_rec->features_act = NULL;
			node_ptr->gres_list = node_state_rec->gres_list;
			node_state_rec->gres_list = NULL;
			node_ptr->gpu_spec_bitmap =
				node_state_rec->gpu_spec_bitmap;
			node_state_rec->gpu_spec_bitmap = NULL;
		} else {
			if ((IS_NODE_CLOUD(node_ptr) ||
			    (node_state & NODE_STATE_DYNAMIC_FUTURE) ||
			    (node_state & NODE_STATE_DYNAMIC_NORM)) &&
			    node_state_rec->comm_name &&
			    node_state_rec->node_hostname) {
				/* Recover NodeAddr and NodeHostName */
				set_node_comm_name(
					node_ptr,
					node_state_rec->comm_name,
					node_state_rec->node_hostname);
			}
			node_ptr->node_state    = node_state;
			xfree(node_ptr->extra);
			node_ptr->extra = node_state_rec->extra;
			node_state_rec->extra = NULL;
			xfree(node_ptr->cert_token);
			node_ptr->cert_token = node_state_rec->cert_token;
			node_state_rec->cert_token = NULL;
			xfree(node_ptr->comment);
			node_ptr->comment = node_state_rec->comment;
			node_state_rec->comment = NULL;
			FREE_NULL_DATA(node_ptr->kv);
			node_ptr->kv = node_state_rec->kv;
			node_state_rec->kv = NULL;
			xfree(node_ptr->instance_id);
			node_ptr->instance_id = node_state_rec->instance_id;
			node_state_rec->instance_id = NULL;
			xfree(node_ptr->instance_type);
			node_ptr->instance_type = node_state_rec->instance_type;
			node_state_rec->instance_type = NULL;
			xfree(node_ptr->parameters);
			node_ptr->parameters = node_state_rec->parameters;
			node_state_rec->parameters = NULL;
			xfree(node_ptr->reason);
			node_ptr->reason = node_state_rec->reason;
			node_state_rec->reason = NULL;
			node_ptr->reason_time = node_state_rec->reason_time;
			node_ptr->reason_uid = node_state_rec->reason_uid;
			xfree(node_ptr->features);
			node_ptr->features = node_state_rec->features;
			node_state_rec->features = NULL;
			xfree(node_ptr->features_act);
			node_ptr->features_act	= node_state_rec->features_act;
			node_state_rec->features_act = NULL;
			xfree(node_ptr->gres);
			node_ptr->gres = node_state_rec->gres;
			node_state_rec->gres = NULL;
			node_ptr->gres_list = node_state_rec->gres_list;
			node_state_rec->gres_list = NULL;
			node_ptr->part_cnt      = 0;
			xfree(node_ptr->part_pptr);
			node_ptr->cpu_bind = node_state_rec->cpu_bind;
			node_ptr->cpus = node_state_rec->cpus;
			node_ptr->boards = node_state_rec->boards;
			node_ptr->tot_sockets = node_state_rec->tot_sockets;
			node_ptr->cores = node_state_rec->cores;
			node_ptr->tot_cores = node_state_rec->tot_cores;
			node_ptr->threads = node_state_rec->threads;
			node_ptr->real_memory = node_state_rec->real_memory;
			node_ptr->res_cores_per_gpu =
				node_state_rec->res_cores_per_gpu;
			node_ptr->gpu_spec_bitmap =
				node_state_rec->gpu_spec_bitmap;
			node_state_rec->gpu_spec_bitmap = NULL;
			node_ptr->tmp_disk = node_state_rec->tmp_disk;
			if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION)
				node_ptr->suspend_time_configured =
					node_state_rec->suspend_time_configured;
			xfree(node_ptr->mcs_label);
			node_ptr->mcs_label = node_state_rec->mcs_label;
			node_state_rec->mcs_label = NULL;

			/*
			 * Preserve dynamic topology.
			 * If the node is powered down then keep topo
			 * as configured by current config files to stop
			 * old slurm.conf node topology from persisting.
			 * This is required even on state_only=false to prevent
			 * nodes from being completely removed from the topology
			 * if topo is removed from slurm.conf but exists in
			 * topology.conf before restart.
			 */
			if (node_state_rec->topology_str &&
			    !IS_NODE_POWERING_DOWN(node_ptr) &&
			    !IS_NODE_POWERED_DOWN(node_ptr)) {
				xfree(node_ptr->topology_str);
				node_ptr->topology_str =
					node_state_rec->topology_str;
				node_state_rec->topology_str = NULL;
			}
			/*
			 * Preserve HRES layer membership like dynamic
			 * topology. A powered/powering down node keeps the
			 * membership of its baseline: resources.yaml, or the
			 * HRES it was created with for a dynamic node.
			 */
			if (node_state_rec->hres_list &&
			    !IS_NODE_POWERING_DOWN(node_ptr) &&
			    !IS_NODE_POWERED_DOWN(node_ptr)) {
				FREE_NULL_LIST(node_ptr->hres_list);
				node_ptr->hres_list = node_state_rec->hres_list;
				node_state_rec->hres_list = NULL;
			}
		}

		if (node_ptr) {
			node_cnt++;

			/*
			 * Scrub a NO_RESPOND flag recovered on a powered down
			 * node. Such a node is never pinged, so the flag would
			 * stick forever and keep the node out of
			 * avail_node_bitmap. This also recovers nodes that were
			 * stuck by a prior version of _require_node_reg().
			 */
			if (IS_NODE_POWERED_DOWN(node_ptr) ||
			    IS_NODE_POWERING_DOWN(node_ptr))
				node_ptr->node_state &=
					(~NODE_STATE_NO_RESPOND);

			node_ptr->next_state = node_state_rec->next_state;

			if (IS_NODE_DOWN(node_ptr)) {
				if (down_nodes)
					hostlist_push(down_nodes,
						      node_state_rec->name);
				else
					down_nodes = hostlist_create(
							node_state_rec->name);
			}

			if (node_state_rec->resume_after &&
			    (IS_NODE_DOWN(node_ptr) ||
			     IS_NODE_DRAINED(node_ptr)))
				node_ptr->resume_after =
					node_state_rec->resume_after;

			node_ptr->last_response = node_state_rec->last_response;
			node_ptr->boot_req_time = node_state_rec->boot_req_time;
			node_ptr->power_save_req_time =
				node_state_rec->power_save_req_time;

			if (node_state_rec->protocol_version &&
			    (node_state_rec->protocol_version != NO_VAL16))
				node_ptr->protocol_version =
					node_state_rec->protocol_version;
			else
				node_ptr->protocol_version = protocol_version;

			/* Sanity check to make sure we can take a version we
			 * actually understand.
			 */
			if (node_ptr->protocol_version <
			    SLURM_MIN_PROTOCOL_VERSION)
				node_ptr->protocol_version =
					SLURM_MIN_PROTOCOL_VERSION;

			if (!IS_NODE_POWERED_DOWN(node_ptr))
				node_ptr->last_busy = node_state_rec->last_busy;

			_adopt_cpu_drain_state(node_ptr, node_state_rec);
			_adopt_mem_drain_state(node_ptr, node_state_rec);
		}

		/*
		 * Recompute the degraded flag from the drain data adopted
		 * above rather than restoring it from the state file, so the
		 * two cannot disagree: slurm.conf may have clamped what was
		 * adopted, and a state file written by a controller that was
		 * killed mid-update may not agree with itself.
		 */
		if (node_ptr) {
			(void) node_conf_set_degraded_flag(node_ptr);
		}

		purge_node_rec(node_state_rec);
	}

	/*
	 * Check if the ordering/count of nodes saved in state is different than
	 * the current ordering/count of nodes loaded from slurm.conf. This may
	 * change if nodes were added/removed, or if a different rank ordering
	 * is used (e.g. BlockAsNodeRank or SwitchAsNodeRank)
	 */
	is_node_table_changed = false;
	if (node_old_to_new_map &&
	    (old_node_record_count != node_record_count)) {
		is_node_table_changed = true;
	} else if (node_old_to_new_map &&
		   (old_node_record_count == node_record_count)) {
		for (int i = 0; i < old_node_record_count; i++) {
			if (node_old_to_new_map[i] != i) {
				is_node_table_changed = true;
				break;
			}
		}
	}

	/*
	 * If no changes are detected in the node record table ordering/count,
	 * node_old_to_new_map is freed as it provides no useful information.
	 */
	if (!is_node_table_changed) {
		xfree(node_old_to_new_map);
		old_node_record_count = 0;
		debug2("Node record table ordering and size from state file matches what was loaded in from slurm.conf.");
	} else if (get_log_level() >= LOG_LEVEL_DEBUG2) {
		debug2("Node record table ordering and size from state file does not match what was loaded from slurm.conf:");
		debug2("  node_record_count %u -> %d",
		       old_node_record_count, node_record_count);
		for (int i = 0; i < old_node_record_count; i++) {
			int new_inx = node_old_to_new_map[i];

			if (new_inx == -1) {
				debug2("  node at old index %d has been removed",
				       i);
				continue;
			}

			debug2("  node:%-15s old index:%-5d -> new index:%-5d",
			       node_record_table_ptr[new_inx] ?
			       node_record_table_ptr[new_inx]->name : NULL,
			       i, new_inx);
		}
	}

fini:	info("Recovered state of %d nodes", node_cnt);

	if (down_nodes) {
		char *down_host_str = NULL;
		down_host_str = hostlist_ranged_string_xmalloc(down_nodes);
		info("Down nodes: %s", down_host_str);
		xfree(down_host_str);
		hostlist_destroy(down_nodes);
	}

	FREE_NULL_BUFFER(buffer);
	return error_code;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete node data checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete node data checkpoint file");
	error_code = EFAULT;
	goto fini;
}

/* list_compare_config - compare two entry from the config list based upon
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2)
{
	config_record_t *c1 = *(config_record_t **) config_entry1;
	config_record_t *c2 = *(config_record_t **) config_entry2;

	return slurm_sort_uint32_list_asc(&c1->weight, &c2->weight);
}

static bool _is_dup_config_record(config_record_t *c1, config_record_t *c2)
{
	if (c1 == c2)
		return false; /* This is the same pointer - ignore */

	if ((c1->boards == c2->boards) &&
	    (c1->core_spec_cnt == c2->core_spec_cnt) &&
	    (c1->cores == c2->cores) &&
	    (c1->cpu_bind == c2->cpu_bind) &&
	    (!xstrcmp(c1->cpu_spec_list, c2->cpu_spec_list)) &&
	    (c1->cpus == c2->cpus) &&
	    (!xstrcmp(c1->feature, c2->feature)) &&
	    (!xstrcmp(c1->gres, c2->gres)) &&
	    (!xstrcmp(c1->max_drained_tres, c2->max_drained_tres)) &&
	    (!xstrcmp(c1->parameters, c2->parameters)) &&
	    (c1->mem_spec_limit == c2->mem_spec_limit) &&
	    (c1->real_memory == c2->real_memory) &&
	    (c1->res_cores_per_gpu == c2->res_cores_per_gpu) &&
	    (c1->suspend_time == c2->suspend_time) &&
	    (c1->threads == c2->threads) &&
	    (c1->tmp_disk == c2->tmp_disk) &&
	    (c1->tot_sockets == c2->tot_sockets) &&
	    (!xstrcmp(c1->topology_str, c2->topology_str)) &&
	    (!xstrcmp(c1->tres_weights_str, c2->tres_weights_str)) &&
	    (c1->weight == c2->weight)) {
		/* duplicate records */
		return true;
	}

	return false;
}

static void _combine_dup_config_records(config_record_t *curr_rec)
{
	bool changed = false;
	config_record_t *config_ptr;
	list_itr_t *iter;

	iter = list_iterator_create(config_list);
	while ((config_ptr = list_next(iter))) {
		if (!_is_dup_config_record(curr_rec, config_ptr))
			continue;

		changed = true;
		bit_or(curr_rec->node_bitmap, config_ptr->node_bitmap);
		list_delete_item(iter);
	}
	list_iterator_destroy(iter);

	if (!changed)
		return;

	xfree(curr_rec->nodes);
	curr_rec->nodes = bitmap2node_name(curr_rec->node_bitmap);
	debug("Consolidated duplicate config records into %s", curr_rec->nodes);

	/* Update each node_ptr in node_bitmap */
	_update_config_ptr(curr_rec->node_bitmap, curr_rec);
}

static bool _node_is_hidden(node_record_t *node_ptr,
			    pack_node_info_t *pack_info)
{
	int i;

	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (slurm_mcs_get_privatedata() == 1) &&
	    (mcs_g_check_mcs_label(pack_info->msg, node_ptr->mcs_label,
				   false) != 0))
		return true;

	if (!node_ptr->part_cnt)
		return false;

	for (i = 0; i < node_ptr->part_cnt; i++) {
		part_record_t *part_ptr = node_ptr->part_pptr[i];
		/* return false if the node belongs to any visible partition */
		for (int j = 0; pack_info->visible_parts[j]; j++)
			if (pack_info->visible_parts[j] == part_ptr)
				return false;
	}

	return true;
}

static void _free_pack_node_info_members(pack_node_info_t *pack_info)
{
	xfree(pack_info->visible_parts);
}

static bool _determine_if_node_is_hidden(
	node_record_t *node_ptr,
	pack_node_info_t *pack_info,
	uint16_t show_flags,
	bool privileged)
{
	if (!node_ptr)
		return true;

	xassert(node_ptr->magic == NODE_MAGIC);
	xassert(node_ptr->config_ptr->magic == CONFIG_MAGIC);

	/*
	 * We can't avoid packing node records without breaking
	 * the node index pointers. So pack a node with a name
	 * of NULL and let the caller deal with it.
	 */
	if (!(show_flags & SHOW_ALL) &&
	    !privileged &&
	    (_node_is_hidden(node_ptr, pack_info)))
		return true;
	else if (IS_NODE_FUTURE(node_ptr) &&
		 (!(show_flags & SHOW_FUTURE)))
		return true;
	else if (!node_ptr->name || (node_ptr->name[0] == '\0'))
		return true;

	return false;
}

extern buf_t *pack_all_nodes(slurm_msg_t *msg, uint16_t show_flags)
{
	int inx;
	uint32_t nodes_packed, tmp_offset;
	buf_t *buffer;
	time_t now = time(NULL);
	bool privileged = validate_operator_msg(msg);
	pack_node_info_t pack_info = {
		.msg = msg,
		.visible_parts = build_visible_parts(msg, privileged)
	};

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	buffer = init_buf(BUF_MIN_NODES_BYTES);
	nodes_packed = 0;

	if (msg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		bitstr_t *hidden_nodes = bit_alloc(node_record_count);
		uint32_t pack_bitmap_offset;
		bool repack_hidden = false;

		/* write header: count, time, hidden node bitmap */
		pack32(nodes_packed, buffer);
		pack_time(now, buffer);
		pack_bitmap_offset = get_buf_offset(buffer);
		pack_bit_str_hex(hidden_nodes, buffer);

		/* write node records */
		for (inx = 0; inx < node_record_count; inx++) {
			if (_determine_if_node_is_hidden(
				    node_record_table_ptr[inx],
				    &pack_info,
				    show_flags,
				    privileged)) {
				bit_set(hidden_nodes, inx);
				repack_hidden = true;
			} else {
				_pack_node(node_record_table_ptr[inx], buffer,
					   msg->protocol_version, show_flags);
			}
			nodes_packed++;
		}

		if (repack_hidden) {
			tmp_offset = get_buf_offset(buffer);
			set_buf_offset(buffer, pack_bitmap_offset);
			pack_bit_str_hex(hidden_nodes, buffer);
			set_buf_offset(buffer, tmp_offset);
		}
		FREE_NULL_BITMAP(hidden_nodes);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, msg->protocol_version);
	}

	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(nodes_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	_free_pack_node_info_members(&pack_info);

	return buffer;
}

extern buf_t *pack_one_node(slurm_msg_t *msg, uint16_t show_flags,
			    char *node_name)
{
	uint32_t nodes_packed, tmp_offset;
	buf_t *buffer;
	time_t now = time(NULL);
	node_record_t *node_ptr;
	bool privileged = validate_operator_msg(msg);
	pack_node_info_t pack_info = {
		.msg = msg,
		.visible_parts = build_visible_parts(msg, privileged)
	};

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	buffer = init_buf(INFINITE);
	nodes_packed = 0;

	if (msg->protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		pack_time(now, buffer);
		/* Mirror _unpack_node_info_msg */
		pack_bit_str_hex(NULL, buffer);

		/* write node records */
		if (node_name)
			node_ptr = find_node_record(node_name);
		else
			node_ptr = node_record_table_ptr[0];
		if (node_ptr) {
			if (!_determine_if_node_is_hidden(
				    node_ptr,
				    &pack_info,
				    show_flags,
				    privileged)) {
				_pack_node(node_ptr, buffer,
					   msg->protocol_version, show_flags);
				nodes_packed++;
			}
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, msg->protocol_version);
	}

	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack32(nodes_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	_free_pack_node_info_members(&pack_info);

	return buffer;
}

/*
 * _pack_node - dump all configuration information about a specific node in
 *	machine independent form (for network transmission)
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN/OUT buffer - buffer where data is placed, pointers automatically updated
 * IN protocol_version - slurm protocol version of client
 * IN show_flags -
 * NOTE: if you make any changes here be sure to make the corresponding changes
 * 	to _unpack_node_info_members() in common/slurm_protocol_pack.c
 */
static void _pack_node(node_record_t *dump_node_ptr, buf_t *buffer,
		       uint16_t protocol_version, uint16_t show_flags)
{
	char *gres_chunks = NULL, *gres_used = NULL;
	list_t *drained = NULL;
	node_select_stats_t *dump_node_stats;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(SELECT_NODE_LOCK, READ_LOCK));

	dump_node_stats = node_select_stats_array[dump_node_ptr->index];

	if (protocol_version >= SLURM_26_11_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);

		/* turns into cert_flags field on remote */
		if (dump_node_ptr->cert_token) {
			pack16(NODE_CERT_TOKEN_SET, buffer);
		} else {
			pack16(0, buffer);
		}

		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->gpu_spec, buffer);
		slurm_pack_list(dump_node_ptr->hres_list,
				slurm_pack_node_hres_info, buffer,
				protocol_version);
		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);
		pack16(dump_node_ptr->cpus_efctv, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack16(dump_node_ptr->res_cores_per_gpu, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);
		pack32(dump_node_ptr->suspend_time, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->last_busy, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->resume_after, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);
		pack_time(dump_node_ptr->cert_last_renewal, buffer);

		pack16(dump_node_stats->alloc_cpus, buffer);
		pack64(dump_node_stats->alloc_memory, buffer);
		packstr(dump_node_stats->alloc_tres_fmt_str, buffer);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_chunks =
				gres_get_node_chunks(dump_node_ptr->gres_list);
			gres_used =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		/* Only a degraded node has drained TRES to gather */
		if (IS_NODE_DEGRADED(dump_node_ptr))
			drained =
				node_conf_get_drained_tres_recs(dump_node_ptr);
		packstr(gres_chunks, buffer);
		packstr(gres_used, buffer);
		xfree(gres_chunks);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		if (pack_data(dump_node_ptr->kv, protocol_version, buffer))
			fatal_abort("%s: Invalid KV data", __func__);
		packstr(dump_node_ptr->extra, buffer);
		packstr(dump_node_ptr->instance_id, buffer);
		packstr(dump_node_ptr->instance_type, buffer);
		packstr(dump_node_ptr->parameters, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
		packstr(dump_node_ptr->resv_name, buffer);
		packstr(dump_node_ptr->topology_str, buffer);
		(void) slurm_pack_list(drained, drained_tres_pack, buffer,
				       protocol_version);
		FREE_NULL_LIST(drained);
		packstr(dump_node_ptr->config_ptr->max_drained_tres, buffer);
	} else if (protocol_version >= SLURM_26_05_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);

		/* turns into cert_flags field on remote */
		if (dump_node_ptr->cert_token) {
			pack16(NODE_CERT_TOKEN_SET, buffer);
		} else {
			pack16(0, buffer);
		}

		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->gpu_spec, buffer);
		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);
		pack16(dump_node_ptr->cpus_efctv, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack16(dump_node_ptr->res_cores_per_gpu, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);
		pack32(dump_node_ptr->suspend_time, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->last_busy, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->resume_after, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);
		pack_time(dump_node_ptr->cert_last_renewal, buffer);

		pack16(dump_node_stats->alloc_cpus, buffer);
		pack64(dump_node_stats->alloc_memory, buffer);
		packstr(dump_node_stats->alloc_tres_fmt_str, buffer);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_used =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		packnull(buffer);
		packstr(gres_used, buffer);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		packstr(dump_node_ptr->extra, buffer);
		packstr(dump_node_ptr->instance_id, buffer);
		packstr(dump_node_ptr->instance_type, buffer);
		packstr(dump_node_ptr->parameters, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
		packstr(dump_node_ptr->resv_name, buffer);
		packstr(dump_node_ptr->topology_str, buffer);
	} else if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);

		/* turns into cert_flags field on remote */
		if (dump_node_ptr->cert_token) {
			pack16(NODE_CERT_TOKEN_SET, buffer);
		} else {
			pack16(0, buffer);
		}

		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->gpu_spec, buffer);
		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);
		pack16(dump_node_ptr->cpus_efctv, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack16(dump_node_ptr->res_cores_per_gpu, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->last_busy, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->resume_after, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);
		pack_time(dump_node_ptr->cert_last_renewal, buffer);

		pack16(dump_node_stats->alloc_cpus, buffer);
		pack64(dump_node_stats->alloc_memory, buffer);
		packstr(dump_node_stats->alloc_tres_fmt_str, buffer);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_used =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		packnull(buffer);
		packstr(gres_used, buffer);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		packstr(dump_node_ptr->extra, buffer);
		packstr(dump_node_ptr->instance_id, buffer);
		packstr(dump_node_ptr->instance_type, buffer);
		packstr(dump_node_ptr->parameters, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
		packstr(dump_node_ptr->resv_name, buffer);
		packstr(dump_node_ptr->topology_str, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);

		/* turns into cert_flags field on remote */
		if (dump_node_ptr->cert_token) {
			pack16(NODE_CERT_TOKEN_SET, buffer);
		} else {
			pack16(0, buffer);
		}

		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->gpu_spec, buffer);
		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);
		pack16(dump_node_ptr->cpus_efctv, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack16(dump_node_ptr->res_cores_per_gpu, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->last_busy, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->resume_after, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);
		pack_time(dump_node_ptr->cert_last_renewal, buffer);

		pack16(dump_node_stats->alloc_cpus, buffer);
		pack64(dump_node_stats->alloc_memory, buffer);
		packstr(dump_node_stats->alloc_tres_fmt_str, buffer);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_used =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		packnull(buffer);
		packstr(gres_used, buffer);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		packstr(dump_node_ptr->extra, buffer);
		packstr(dump_node_ptr->instance_id, buffer);
		packstr(dump_node_ptr->instance_type, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
		packstr(dump_node_ptr->resv_name, buffer);
		packstr(dump_node_ptr->topology_str, buffer);
	} else {
		error("_pack_node: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/* Return "true" if a node's state is already "new_state". This is more
 * complex than simply comparing the state values due to flags (e.g.
 * A node might be DOWN + NO_RESPOND or IDLE + DRAIN) */
static bool _equivalent_node_state(node_record_t *node_ptr, uint32_t new_state)
{
	if (new_state == NO_VAL)	/* No change */
		return true;
	if ((new_state == NODE_STATE_DOWN)  && IS_NODE_DOWN(node_ptr))
		return true;
	if ((new_state == NODE_STATE_DRAIN) && IS_NODE_DRAIN(node_ptr))
		return true;
	if ((new_state == NODE_STATE_FAIL)  && IS_NODE_FAIL(node_ptr))
		return true;
	/* Other states might be added here */
	return false;
}

/* Confirm that the selected ActiveFeatures are a subset of AvailableFeatures */
static bool _valid_features_act(char *features_act, char *features)
{
	bool valid_subset = true;
	char *tmp_act, *last_act = NULL, *tok_act;
	char *tmp_avail, *last_avail = NULL, *tok_avail;

	if (!features_act || (features_act[0] == '\0'))
		return true;
	if (!features || (features[0] == '\0'))
		return false;

	tmp_act = xstrdup(features_act);
        tok_act = strtok_r(tmp_act, ",", &last_act);
        while (tok_act) {
		last_avail = NULL;
		tmp_avail = xstrdup(features);
		tok_avail = strtok_r(tmp_avail, ",", &last_avail);
		while (tok_avail) {
			if (!xstrcmp(tok_act, tok_avail))
				break;
		        tok_avail = strtok_r(NULL, ",", &last_avail);
		}
		xfree(tmp_avail);
		if (!tok_avail) {	/* No match found */
			valid_subset = false;
			break;
		}
                tok_act = strtok_r(NULL, ",", &last_act);
	}
	xfree(tmp_act);

	return valid_subset;
}

/*
 * Validate that reported active changeable features are a superset of the
 * current active changeable features in the controller.
 *
 * If the node doesn't report any features then don't do any validation.
 */
static bool _valid_reported_active_features(const char *reg_active_features,
					    const char *node_active_features)
{
	char *tok, *saveptr;
	char *tmp_node_act, *tmp_reg_act;
	list_t *changeable_list = NULL;
	bool valid = true;

	if (!node_active_features || !reg_active_features)
		return true;

	tmp_reg_act = xstrdup(reg_active_features);
	for (tok = strtok_r(tmp_reg_act, ",", &saveptr);
	     tok;
	     tok = strtok_r(NULL, ",", &saveptr)) {

		if (!node_features_g_changeable_feature(tok))
			continue;

		if (!changeable_list)
			changeable_list = list_create(NULL);
		list_append(changeable_list, tok);
	}

	/*
	 * The node's current active changeable features should be a subset of
	 * the changeable features in the registration message.
	 */
	if (changeable_list && list_count(changeable_list)) {
		tmp_node_act = xstrdup(node_active_features);
		for (tok = strtok_r(tmp_node_act, ",", &saveptr);
		     tok;
		     tok = strtok_r(NULL, ",", &saveptr)) {
			if (node_features_g_changeable_feature(tok) &&
			    !list_delete_all(changeable_list,
					     slurm_find_char_in_list,
					     tok)) {
				/* feature not in current active list */
				valid = false;
				break;
			}
		}
		xfree(tmp_node_act);
	}

	FREE_NULL_LIST(changeable_list);
	xfree(tmp_reg_act);

	return valid;
}

/*
 * Return a new string containing containing only changeable features.
 */
static char *_node_changeable_features(char *all_features)
{
	char *tmp_features;
	char *tok, *saveptr;
	char *changeable_features = NULL;

	tmp_features = xstrdup(all_features);
	for (tok = strtok_r(tmp_features, ",", &saveptr);
	     tok;
	     tok = strtok_r(NULL, ",", &saveptr)) {

		if (!node_features_g_changeable_feature(tok))
			continue;

		xstrfmtcat(changeable_features, "%s%s",
			   changeable_features ? "," : "",
			   tok);
	}
	xfree(tmp_features);

	return changeable_features;
}

extern void cancel_power_down_undrain(node_record_t *node_ptr)
{
	if (IS_NODE_POWER_DOWN(node_ptr) &&
	    (node_ptr->next_state == NODE_STATE_UNDRAIN))
		node_ptr->next_state = NO_VAL;
}

static void _undo_reboot_asap(node_record_t *node_ptr)
{
	if (IS_NODE_IDLE(node_ptr) || IS_NODE_ALLOCATED(node_ptr))
		bit_set(avail_node_bitmap, node_ptr->index);
	node_ptr->node_state &= (~NODE_STATE_DRAIN);
	xfree(node_ptr->reason);
}

static void _require_node_reg(node_record_t *node_ptr)
{
	if (IS_NODE_EXTERNAL(node_ptr))
		return;
	/*
	 * A powered down or powering up node is skipped by ping_nodes(), so
	 * there is nothing to require of it and nothing to ping. On a powered
	 * down node a NO_RESPOND flag set here would never be cleared. A
	 * powering up node already has NO_RESPOND, which its registration or
	 * ResumeTimeout clears. For either, refreshing last_response would
	 * record a response that was never received, and on a powering up node
	 * would make the registration's boot test fail and drop the
	 * slurmd-supplied instance fields. ping_nodes_now would schedule a ping
	 * cycle that skips this very node.
	 */
	if (IS_NODE_POWERED_DOWN(node_ptr) || IS_NODE_POWERING_DOWN(node_ptr) ||
	    IS_NODE_POWERING_UP(node_ptr))
		return;
	node_ptr->node_state |= NODE_STATE_NO_RESPOND;
	/*
	 * Only refresh an existing grace period; don't start one for a node
	 * that has never responded (last_response == 0). Otherwise an UNKNOWN
	 * node would get a last_response and ping_nodes() would start the
	 * SlurmdTimeout clock and mark it DOWN for "Not responding" even though
	 * it never responded.
	 */
	if (node_ptr->last_response)
		node_ptr->last_response = time(NULL);
	node_ptr->boot_time = 0;
	ping_nodes_now = true;
}

static int _foreach_build_part_bitmap(void *x, void *arg)
{
	build_part_bitmap(x);
	return SLURM_SUCCESS;
}

static void _update_parts(void)
{
	/* scan partition table and identify nodes in each */
	list_for_each(part_list, _foreach_build_part_bitmap, NULL);
	set_partition_tres(false);
}

static int _update_node_kv(node_record_t *node_ptr,
			   const update_node_msg_t *update_node_msg,
			   const data_t *kv_set_doc)
{
	char *err_msg = NULL;
	const data_t *src_kv = (update_node_msg->kv_op == KV_OP_SET) ?
				       kv_set_doc :
				       update_node_msg->kv;
	int rc;

	if ((rc = kv_op_apply(src_kv, update_node_msg->kv_op, &node_ptr->kv,
			      NULL, &err_msg)))
		error("Invalid KV update for node %s: %s", node_ptr->name,
		      err_msg ? err_msg : slurm_strerror(rc));

	xfree(err_msg);
	return rc;
}

/*
 * Split "gres/<name>[:<type>]" into its name and type. Returns NULL if the
 * string does not name a GRES; otherwise the caller xfrees the returned name
 * and *type_out.
 */
static char *_split_gres_tres_name(char *tres_name, char **type_out)
{
	char *name, *colon;

	if (xstrncasecmp(tres_name, "gres/", 5))
		return NULL;

	name = xstrdup(tres_name + 5);
	if (!name[0]) {
		xfree(name);
		return NULL;
	}

	if ((colon = xstrchr(name, ':'))) {
		*colon = '\0';
		*type_out = colon[1] ? xstrdup(colon + 1) : NULL;
	} else {
		*type_out = NULL;
	}

	return name;
}

/*
 * Resolve a CPU drain selector into a bitmap of schedulable cores. Mutates
 * nothing, so the caller can reject a bad request before applying any of it.
 *
 * Index= names abstract CPU ids. Any specified thread marks its entire core,
 * matching what node_conf_convert_cpu_spec_list() does for CpuSpecList. The
 * result is one bit per core (tot_cores), which is the index space
 * core_drain_bitmap stores. On a node whose CPUs are sockets (tpc 1, cpus
 * less than tot_cores), CPU i names core i, as it does for CpuSpecList.
 * tpc, not ThreadsPerCore: a node whose configured CPUs are cores is not
 * scheduling threads, and dividing by ThreadsPerCore would mark a sibling
 * core.
 */
static int _resolve_cpu_target(node_record_t *node_ptr, update_tres_msg_t *msg,
			       bitstr_t **target_out)
{
	bitstr_t *cpus;
	uint16_t tpc = node_ptr->tpc;
	int ncores = node_ptr->tot_cores;

	/* CPUs are named by index; nothing about them carries a UUID. */
	if (msg->uuid)
		return ESLURM_INVALID_TRES;

	if (ncores < 1)
		return ESLURM_INVALID_CPU_COUNT;

	/*
	 * Every core, not every CPU: on a node whose CPUs are sockets, folding
	 * all of its CPUs would only reach its first cpus cores.
	 */
	if (msg->select_all) {
		*target_out = bit_alloc(ncores);
		bit_set_all(*target_out);
		return SLURM_SUCCESS;
	}

	cpus = bit_alloc(node_ptr->cpus);

	if (msg->index) {
		if (bit_unfmt(cpus, msg->index)) {
			FREE_NULL_BITMAP(cpus);
			return ESLURM_INVALID_CPU_COUNT;
		}
	} else {
		/* CPUs have identity: a bare count cannot name them. */
		FREE_NULL_BITMAP(cpus);
		return ESLURM_INVALID_CPU_COUNT;
	}

	*target_out = _fold_cpus_to_cores(cpus, tpc, ncores);
	FREE_NULL_BITMAP(cpus);

	return SLURM_SUCCESS;
}

/*
 * Apply a resolved core drain target in the caller's action. UPDATE_SET
 * makes the drained set exactly the target, resuming cores that are drained
 * but not named.
 */
static void _apply_cpu_drain(node_record_t *node_ptr, bitstr_t *target,
			     char *reason, update_mode_t action,
			     bool *did_change)
{
	int cores = bit_size(target);

	if (node_ptr->core_drain_bitmap &&
	    (bit_size(node_ptr->core_drain_bitmap) != cores))
		bit_realloc(node_ptr->core_drain_bitmap, cores);
	drained_tres_groups_resize(&node_ptr->cpu_drain_groups, cores);

	for (int i = 0; i < cores; i++) {
		bool want = bit_test(target, i);
		bool have = node_ptr->core_drain_bitmap ?
				    bit_test(node_ptr->core_drain_bitmap, i) :
				    false;
		bool drain;

		switch (action) {
		case UPDATE_ADD:
			if (!want)
				continue;
			drain = true;
			break;
		case UPDATE_REMOVE:
			if (!want)
				continue;
			drain = false;
			break;
		case UPDATE_SET:
			drain = want;
			break;
		default:
			return;
		}

		if (drain == have) {
			/* Already in the requested state; refresh the reason. */
			if (drain &&
			    xstrcmp(drained_tres_group_reason(
					    node_ptr->cpu_drain_groups, i),
				    reason)) {
				drained_tres_group_set(
					&node_ptr->cpu_drain_groups, i, cores,
					reason);
				*did_change = true;
			}
			continue;
		}

		if (drain) {
			if (!node_ptr->core_drain_bitmap) /* lazy init */
				node_ptr->core_drain_bitmap = bit_alloc(cores);
			bit_set(node_ptr->core_drain_bitmap, i);
			drained_tres_group_set(&node_ptr->cpu_drain_groups, i,
					       cores, reason);
		} else if (node_ptr->core_drain_bitmap) {
			bit_clear(node_ptr->core_drain_bitmap, i);
			drained_tres_group_clear(&node_ptr->cpu_drain_groups,
						 i);
		}
		*did_change = true;
	}

	/* Nothing left drained: drop the map and the reasons together. */
	if (node_ptr->core_drain_bitmap &&
	    !bit_set_count(node_ptr->core_drain_bitmap)) {
		FREE_NULL_BITMAP(node_ptr->core_drain_bitmap);
		FREE_NULL_LIST(node_ptr->cpu_drain_groups);
	}
}

/*
 * Resolve a memory drain selector into a count of MB. Memory is a fungible
 * pool whose TRES have no individual identity, so only a count (or
 * "all") can name it.
 */
static int _resolve_mem_target(node_record_t *node_ptr, update_tres_msg_t *msg,
			       uint64_t *target_out)
{
	/* Drained memory is a quantity, not a set of identifiable units. */
	if (msg->uuid || msg->index)
		return ESLURM_INVALID_TRES;

	if (msg->select_all) {
		*target_out = node_ptr->real_memory;
		return SLURM_SUCCESS;
	}
	if (msg->count == NO_VAL64)
		return ESLURM_INVALID_TRES;

	*target_out = msg->count;

	return SLURM_SUCCESS;
}

/*
 * Apply a resolved memory drain target in the caller's action.
 *
 * NOTE: memory keeps a single drain reason rather than one per TRES.
 * Unlike CPUs and devices, drained memory has no identity to attach a reason
 * to (the node simply has fewer schedulable MB), so a later drain replaces
 * the reason.
 */
static void _apply_mem_drain(node_record_t *node_ptr, uint64_t mb, char *reason,
			     update_mode_t action, bool *did_change)
{
	uint64_t before = node_ptr->mem_drain.count;
	uint64_t after = before;

	switch (action) {
	case UPDATE_ADD:
		/* Capped first, so a count such as INFINITE64 cannot wrap */
		after += MIN(mb, node_ptr->real_memory);
		break;
	case UPDATE_REMOVE:
		after -= MIN(before, mb);
		break;
	case UPDATE_SET:
		after = mb;
		break;
	default:
		return;
	}

	after = MIN(after, node_ptr->real_memory);

	if (after == before) {
		if (after && (action != UPDATE_REMOVE) &&
		    xstrcmp(node_ptr->mem_drain.reason, reason)) {
			drained_tres_count_set(&node_ptr->mem_drain, after,
					       reason, true);
			*did_change = true;
		}
		return;
	}

	/*
	 * A partial resume carries no reason. Keep the reason that still
	 * applies to the memory left drained. A count of 0 clears it.
	 */
	drained_tres_count_set(&node_ptr->mem_drain, after, reason,
			       action != UPDATE_REMOVE);
	*did_change = true;
}

/*
 * Return true if the node has drained more of a node-local TRES (cpu or memory)
 * than its MaxDrainedTres limit allows, appending an explanation to *reason for
 * each TRES over its limit. Mirrors gres_node_state_list_drain_over_limit() for
 * the node-local cpu/mem drain state kept on node_record_t. Both TRES are
 * always evaluated so the reason reflects every offending resource.
 */
static bool _node_local_drain_over_limit(node_record_t *node_ptr, char **reason)
{
	char *mdt;
	bool over_limit = false;
	uint64_t drained, limit;

	if (!node_ptr->config_ptr)
		return false;
	mdt = node_ptr->config_ptr->max_drained_tres;
	if (!mdt || !mdt[0])
		return false;

	/* CPU: drained cores, counted in abstract CPUs (cores * tpc). */
	if (node_ptr->core_drain_bitmap &&
	    (drained = (uint64_t) bit_set_count(node_ptr->core_drain_bitmap) *
		       node_ptr->tpc)) {
		limit = drained_tres_max_limit(mdt, "cpu", false);
		if (drained_tres_over_limit(reason, "cpu", drained, limit))
			over_limit = true;
	}

	/* Memory: MB of RealMemory administratively drained. */
	if ((drained = node_ptr->mem_drain.count)) {
		limit = drained_tres_max_limit(mdt, "mem", false);
		if (drained_tres_over_limit(reason, "mem", drained, limit))
			over_limit = true;
	}

	return over_limit;
}

/*
 * Drain the node itself if more of any TRES is drained than its MaxDrainedTres
 * limit allows. GRES and node-local cpu/memory are both always evaluated so the
 * reason names every offender. A node already down/drained/failing is left
 * alone.
 * IN node_ptr - node to evaluate
 * IN reason_uid - uid recorded against the drain
 * IN replace_reason - drop the node's reason first; the drain it named was
 *	just cleared
 * RET true if this call drained the node
 */
static bool _enforce_max_drained_tres(node_record_t *node_ptr,
				      uint32_t reason_uid, time_t now,
				      bool replace_reason)
{
	char *drain_reason = NULL;
	bool over_limit, drained = false;

	over_limit = gres_node_state_list_drain_over_limit(
		node_ptr->gres_list,
		node_ptr->config_ptr ? node_ptr->config_ptr->max_drained_tres :
				       NULL,
		&drain_reason);
	if (_node_local_drain_over_limit(node_ptr, &drain_reason))
		over_limit = true;

	if (over_limit && !IS_NODE_DOWN(node_ptr) && !IS_NODE_DRAIN(node_ptr) &&
	    !IS_NODE_FAIL(node_ptr)) {
		if (replace_reason) {
			xfree(node_ptr->reason);
			node_ptr->reason_time = 0;
			node_ptr->reason_uid = NO_VAL;
		}
		_drain_node(node_ptr, drain_reason, reason_uid, now);
		last_node_update = now;
		validate_all_reservations(false, false);
		drained = true;
	}
	xfree(drain_reason);

	return drained;
}

/*
 * Apply the update message's administrative TRES drain to one node.
 *
 * A request names exactly one TRES, so the whole request is resolved before
 * anything is mutated: an invalid selector fails with the node untouched
 * rather than leaving it half drained.
 *
 * IN dry_run - resolve the request against the node but change nothing, so a
 *		multi-node update can reject a selector that is invalid on any
 *		one of its nodes before it has changed any of them.
 */
static int _update_node_tres_drain(node_record_t *node_ptr,
				   update_tres_msg_t *msg, bool *did_change,
				   bool dry_run, char **absent_note)
{
	char *reason = (msg->action == UPDATE_REMOVE) ? NULL : msg->reason;
	int rc;

	/* node_mgr_update_tres_drain() rejects both of these before the node loop. */
	xassert(msg->name && msg->name[0]);
	xassert((msg->action == UPDATE_REMOVE) || (reason && reason[0]));

	if (!xstrcasecmp(msg->name, "cpu")) {
		bitstr_t *target = NULL;

		if ((rc = _resolve_cpu_target(node_ptr, msg, &target)))
			return rc;
		if (!dry_run)
			_apply_cpu_drain(node_ptr, target, reason, msg->action,
					 did_change);
		FREE_NULL_BITMAP(target);

		return SLURM_SUCCESS;
	}

	if (!xstrcasecmp(msg->name, "mem") ||
	    !xstrcasecmp(msg->name, "memory")) {
		uint64_t mb = 0;

		if ((rc = _resolve_mem_target(node_ptr, msg, &mb)))
			return rc;
		if (!dry_run)
			_apply_mem_drain(node_ptr, mb, reason, msg->action,
					 did_change);

		return SLURM_SUCCESS;
	}

	if (!xstrncasecmp(msg->name, "gres/", 5)) {
		gres_drain_req_t req = {
			.action = msg->action,
			.reason = reason,
			.count = msg->count,
		};

		/*
		 * The dry run resolves a copy. Record the devices only when
		 * the live node is updated, so the note is not repeated.
		 */
		if (!dry_run) {
			req.absent_note = absent_note;
			req.node_name = node_ptr->name;
		}

		if (!(req.name = _split_gres_tres_name(msg->name, &req.type)))
			return ESLURM_INVALID_TRES;

		if (msg->select_all) {
			req.sel = GRES_DRAIN_SEL_ALL;
		} else if (msg->uuid) {
			/*
			 * A UUID names the device outright, so it is what the
			 * request selects on. An index given alongside is
			 * carried as a check that it still points at the same
			 * device rather than as a second way to select.
			 */
			req.sel = GRES_DRAIN_SEL_UUID;
			req.sel_str = msg->uuid;
			req.verify_index = msg->index;
		} else if (msg->index) {
			req.sel = GRES_DRAIN_SEL_INDEX;
			req.sel_str = msg->index;
		} else if (msg->count != NO_VAL64) {
			req.sel = GRES_DRAIN_SEL_COUNT;
		} else {
			xfree(req.name);
			xfree(req.type);
			return ESLURM_INVALID_GRES;
		}

		if (dry_run) {
			/*
			 * did_change is computed by mutating and diffing a
			 * signature, so resolve against a copy of just this
			 * TRES's state rather than touch the live node record.
			 */
			list_t *copy =
				gres_node_state_dup_one(node_ptr->gres_list,
							req.name);

			rc = gres_node_update_drain(copy, &req, did_change);
			FREE_NULL_LIST(copy);
		} else {
			rc = gres_node_update_drain(node_ptr->gres_list, &req,
						    did_change);
		}
		xfree(req.name);
		xfree(req.type);

		return rc;
	}

	return ESLURM_INVALID_TRES;
}

/*
 * Every component of an entry is recorded verbatim in the event table, and
 * each comes from configuration: the TRES name from slurm.conf/gres.conf, the
 * UUID from an autodetect mechanism that reports one. Accept only a
 * conservative character set so no component can break out of the SQL string
 * literal it is interpolated into, nor collide with the commas or "(IDX:",
 * "(UUID:", "(CNT:" groups used to assemble the entry. Vendor UUIDs (NVIDIA
 * "GPU-<uuid>", MIG "MIG-GPU-<uuid>-<gi>-<ci>", AMD serials) all pass.
 *
 * IN also_ok - additional accepted characters, "" for none. The TRES name needs
 *              '/' and ':', which are its own structure. A UUID needs ':', and
 *              '/' for a legacy MIG UUID (MIG-GPU-<uuid>/<gi>/<ci>).
 */
static bool _drained_id_is_recordable(const char *str, const char *also_ok)
{
	const char *c;

	if (!str || !str[0])
		return false;

	for (c = str; *c; c++) {
		if (!isalnum((unsigned char) *c) && (*c != '-') &&
		    (*c != '_') && (*c != '.') && !xstrchr(also_ok, *c))
			return false;
	}

	return true;
}

/*
 * Total order so the TresReference string is canonical: the event dedup
 * compares it verbatim, and qsort is not stable. Name, then UUID when the
 * record has one, else the index range.
 */
static int _sort_drained_tres_recs(void *x, void *y)
{
	drained_tres_t *rec1 = *(drained_tres_t **) x;
	drained_tres_t *rec2 = *(drained_tres_t **) y;
	int diff = xstrcmp(rec1->tres_name, rec2->tres_name);

	if (diff)
		return diff;
	if (rec1->uuid || rec2->uuid)
		return xstrcmp(rec1->uuid, rec2->uuid);

	return xstrcmp(rec1->index, rec2->index);
}

/* Emit the open (IDX:) group, if any. */
static void _flush_drained_idx_ref(drained_tres_args_t *args)
{
	char *tmp;

	if (!args->ref_idx_name)
		return;

	if (args->ref_idx_bits) {
		tmp = bit_fmt_full(args->ref_idx_bits);
		if (tmp && tmp[0])
			xstrfmtcat(args->ref_str, "%s%s(IDX:%s)",
				   args->ref_str ? "," : "", args->ref_idx_name,
				   tmp);
		xfree(tmp);
	}

	xfree(args->ref_idx_name);
	FREE_NULL_BITMAP(args->ref_idx_bits);
}

/*
 * Append one drained TRES to the event's TresReference string:
 *
 *   <tres_name>[(IDX:<range>)][(UUID:<uuid>)][(CNT:<n>)][,...]
 *
 * Groups are present only when known. An (IDX:) group is the TRES's index
 * as of the event's start. (CNT:) is used for memory and count-only GRES.
 * UUID-less devices that share a TRES name are coalesced into one (IDX:)
 * range.
 */
static void _add_drained_tres_reference(drained_tres_t *rec,
					drained_tres_args_t *args)
{
	if (!_drained_id_is_recordable(rec->tres_name, ":/")) {
		debug("%s: node %s: not recording unrepresentable identity for %s",
		      __func__, args->node_ptr->name, rec->tres_name);
		return;
	}

	if (rec->uuid) {
		if (!_drained_id_is_recordable(rec->uuid, ":/")) {
			debug("%s: node %s: not recording unrepresentable UUID for %s",
			      __func__, args->node_ptr->name, rec->tres_name);
			return;
		}

		_flush_drained_idx_ref(args);
		xstrfmtcat(args->ref_str, "%s%s", args->ref_str ? "," : "",
			   rec->tres_name);
		if (rec->index && rec->index[0])
			xstrfmtcat(args->ref_str, "(IDX:%s)", rec->index);
		xstrfmtcat(args->ref_str, "(UUID:%s)", rec->uuid);
		return;
	}

	if (rec->index && rec->index[0]) {
		if (xstrcmp(args->ref_idx_name, rec->tres_name))
			_flush_drained_idx_ref(args);
		if (!args->ref_idx_name)
			args->ref_idx_name = xstrdup(rec->tres_name);
		drained_tres_index_add(&args->ref_idx_bits, rec->index);
		return;
	}

	if (rec->count == NO_VAL64)
		return;

	_flush_drained_idx_ref(args);
	xstrfmtcat(args->ref_str, "%s%s(CNT:%" PRIu64 ")",
		   args->ref_str ? "," : "", rec->tres_name, rec->count);
}

/* How many TRES units one drained-TRES record accounts for. */
static uint64_t _drained_rec_count(drained_tres_t *rec)
{
	bitstr_t *index = NULL;
	uint64_t count;

	if (rec->count != NO_VAL64)
		return rec->count;

	/* No count and no index: nothing to account. */
	if (!rec->index || !rec->index[0])
		return 0;

	drained_tres_index_add(&index, rec->index);
	count = bit_set_count(index);
	FREE_NULL_BITMAP(index);

	return count;
}

/*
 * Add a record's TRES to its event's TRES counts. A typed GRES counts
 * against its type and against the untyped name as well, the way a job's
 * request for either is accounted.
 *
 * Caller must hold the assoc_mgr TRES read lock.
 */
static void _add_drained_rec_cnt(uint64_t *drained_cnt, drained_tres_t *rec)
{
	slurmdb_tres_rec_t tres_rec = {
		.type = "gres",
	};
	uint64_t count = _drained_rec_count(rec);
	char *sep;
	int tres_pos;

	if (!xstrcmp(rec->tres_name, "cpu")) {
		drained_cnt[TRES_ARRAY_CPU] += count;
		return;
	}

	if (!xstrcmp(rec->tres_name, "mem")) {
		drained_cnt[TRES_ARRAY_MEM] += count;
		return;
	}

	if (xstrncasecmp(rec->tres_name, "gres/", 5))
		return;

	tres_rec.name = rec->tres_name + 5;
	if ((tres_pos = assoc_mgr_find_tres_pos(&tres_rec, true)) != -1)
		drained_cnt[tres_pos] += count;

	if (!(sep = xstrchr(tres_rec.name, ':')))
		return;

	tres_rec.name = xstrndup(tres_rec.name, sep - tres_rec.name);
	if ((tres_pos = assoc_mgr_find_tres_pos(&tres_rec, true)) != -1)
		drained_cnt[tres_pos] += count;
	xfree(tres_rec.name);
}

/*
 * Fold one drained TRES into the node's counts, and record the reason it was
 * drained for if nothing has named that reason yet.
 *
 * Caller must hold the assoc_mgr TRES read lock.
 */
static int _foreach_add_drained_rec(void *x, void *arg)
{
	drained_tres_t *rec = x;
	drained_tres_args_t *args = arg;
	char *reason = rec->reason;

	/*
	 * A drained GRES reports the display placeholder rather than NULL when
	 * it carries no reason of its own, so recognize that too. Without this
	 * only CPUs and memory would reach the fallback, and a GRES drained
	 * without a reason would be reported as drained for "no reason".
	 */
	if (reason &&
	    (!reason[0] || !xstrcasecmp(reason, DRAINED_TRES_NO_REASON)))
		reason = NULL;

	/*
	 * node_ptr->reason cannot identify this event: a return to service
	 * xfrees it (see clusteracct_storage_g_node_up()) and any unrelated
	 * drain of the node overwrites it, either of which restamps an event
	 * whose drained TRES have not changed, failing the dedup and tearing
	 * one continuous drain into several rows. It is only a fallback for a
	 * TRES drained without a reason of its own, which "scontrol update
	 * TresName=" does not allow and only older state can hold.
	 */
	if (!reason)
		reason = args->fallback_reason;

	if (reason && reason[0] &&
	    !list_find_first(args->reasons, slurm_find_char_in_list, reason))
		list_append(args->reasons, xstrdup(reason));

	_add_drained_rec_cnt(args->drained_cnt, rec);
	_add_drained_tres_reference(rec, args);

	return 0;
}

/*
 * Build the accounting event describing everything the node has drained: the
 * counts of every drained TRES, the identity of every drained device, and
 * every distinct reason they were drained for.
 * IN fallback_reason - reason for TRES that carry none, may be NULL
 * OUT reason_str - every distinct reason, comma joined (caller must xfree),
 *	NULL when none of the drained TRES named one
 * OUT ref_str - compact rendering of every drained TRES (caller must xfree),
 *	NULL when nothing drained contributes a representable identity
 * RET simple-format counts of the drained TRES (caller must xfree), NULL when
 *     nothing is drained
 */
static char *_build_node_drained_tres_str(node_record_t *node_ptr,
					  char *fallback_reason,
					  char **reason_str, char **ref_str)
{
	assoc_mgr_lock_t locks = {
		.tres = READ_LOCK,
	};
	drained_tres_args_t args = {
		.fallback_reason = fallback_reason,
		.node_ptr = node_ptr,
	};
	char *drained_tres;
	list_t *recs;

	*reason_str = NULL;
	*ref_str = NULL;

	if (!(recs = node_conf_get_drained_tres_recs(node_ptr)))
		return NULL;

	/*
	 * Sort the records first: the event's TRES string is compared verbatim
	 * against the open interval's, so what goes into it must not depend on
	 * the drain/resume history that produced the records.
	 */
	list_sort(recs, _sort_drained_tres_recs);

	args.reasons = list_create(xfree_ptr);

	/*
	 * Size by g_tres_count under the lock: the positions and the string
	 * below use it, and slurmctld_tres_cnt lags it until _init_tres().
	 */
	assoc_mgr_lock(&locks);
	args.drained_cnt = xcalloc(g_tres_count, sizeof(uint64_t));
	(void) list_for_each(recs, _foreach_add_drained_rec, &args);
	drained_tres =
		assoc_mgr_make_tres_str_from_array(args.drained_cnt,
						   TRES_STR_FLAG_SIMPLE, true);
	assoc_mgr_unlock(&locks);

	/*
	 * A node has one open event interval at a time, so the reasons are
	 * joined rather than kept apart. slurm_char_list_to_xstr() sorts them
	 * before joining, so the string is canonical: it is compared verbatim
	 * against the open interval's, and the order the drains happened in
	 * must not make an unchanged set look different.
	 */
	*reason_str = slurm_char_list_to_xstr(args.reasons);
	_flush_drained_idx_ref(&args);
	*ref_str = args.ref_str;

	xfree(args.drained_cnt);
	FREE_NULL_LIST(args.reasons);
	FREE_NULL_LIST(recs);

	return drained_tres;
}

extern void node_mgr_update_degraded_event(node_record_t *node_ptr,
					   char *reason, uint32_t reason_uid,
					   time_t now)
{
	char *drained_tres, *drained_reason = NULL, *drained_ref = NULL;
	bool can_open;

	/*
	 * A node has one open interval at a time, so a node whose whole-node
	 * event already covers every TRES it has gets no drained-TRES interval:
	 * opening one would close the whole-node one and under-report what the
	 * node lost. The test mirrors the one _drain_node() uses to decide
	 * whether to send a whole-node event at all. A node draining with jobs
	 * still on it has none until the last of them exits, so its drained
	 * TRES are recorded until then.
	 */
	can_open = !IS_NODE_DOWN(node_ptr) && !IS_NODE_FUTURE(node_ptr) &&
		   !IS_NODE_POWERED_DOWN(node_ptr) &&
		   !IS_NODE_POWERING_DOWN(node_ptr) &&
		   !((IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) &&
		     !node_ptr->run_job_cnt && !node_ptr->comp_job_cnt);

	drained_tres =
		_build_node_drained_tres_str(node_ptr,
					     reason ? reason : node_ptr->reason,
					     &drained_reason, &drained_ref);

	if (drained_tres && drained_tres[0]) {
		if (!can_open)
			goto fini;
		/* slurmdbd forces the flag, but it should already be set */
		xassert(IS_NODE_DEGRADED(node_ptr));
		clusteracct_storage_g_node_tres_drain(acct_db_conn, node_ptr,
						      drained_tres, drained_ref,
						      now, drained_reason,
						      reason_uid);
	} else {
		/*
		 * Nothing drained: a blank drained_tres closes the open
		 * drained-TRES interval, the only kind recorded as DEGRADED,
		 * and leaves a whole-node one, such as a draining node's from
		 * send_nodes_to_accounting(), alone. It must not be NULL:
		 * slurmdbd turns a NULL TRES string into node_up(), which would
		 * end both and xfree node_ptr->reason under whatever lock the
		 * caller holds.
		 */
		clusteracct_storage_g_node_tres_drain(acct_db_conn, node_ptr,
						      "", NULL, now, NULL,
						      reason_uid);
	}

fini:
	xfree(drained_ref);
	xfree(drained_reason);
	xfree(drained_tres);
}

static char *_absent_gres_stay_msg(const char *list)
{
	if (!list || !list[0])
		return NULL;

	return xstrdup_printf("Absent GRES stays drained: %s", list);
}

/* Log list and, when the caller wants it, hand back the same sentence. */
static void _record_absent_gres_note(char *list, char **user_msg)
{
	char *msg = _absent_gres_stay_msg(list);

	xfree(list);
	if (!msg)
		return;

	info("%s", msg);
	if (user_msg)
		*user_msg = msg;
	else
		xfree(msg);
}

extern int update_node(slurm_msg_t *msg, update_node_msg_t *update_node_msg,
		       char **user_msg)
{
	int error_code = 0, node_cnt;
	node_record_t *node_ptr = NULL;
	char *this_node_name = NULL, *tmp_feature, *orig_features_act = NULL;
	hostlist_t *host_list = NULL, *hostaddr_list = NULL;
	hostlist_t *hostname_list = NULL;
	hostlist_t *instance_id_list = NULL;
	hostlist_t *instance_type_list = NULL;
	hostlist_t *power_skip_list = NULL;
	char *instance_id_broadcast = NULL;
	char *instance_type_broadcast = NULL;
	uint32_t base_state = 0, node_flags, state_val, resume_after = NO_VAL;
	time_t now = time(NULL);
	bool uniq = true;
	data_t *kv_set_doc = NULL;
	char *absent_note = NULL;

	if (user_msg)
		*user_msg = NULL;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, WRITE_LOCK));

	if (update_node_msg->node_names == NULL ) {
		info("%s: invalid node name", __func__);
		error_code = ESLURM_INVALID_NODE_NAME;
		goto end;
	}

	/* Validate before anything is applied, so a reject changes nothing. */
	if ((update_node_msg->suspend_time != NO_VAL) && !power_save_avail) {
		error("%s: rejecting SuspendTime update for nodes %s, automatic power save mode is disabled",
		      __func__, update_node_msg->node_names);
		error_code = ESLURM_DISABLED;
		goto end;
	}

	/*
	 * Validate replacements before any node changes so a bad request
	 * changes nothing. Patches depend on each node's document and are
	 * applied in the loop below.
	 */
	{
		char *err_msg = NULL;
		kv_op_t op = (update_node_msg->kv_op == KV_OP_PATCH) ?
				     KV_OP_NONE :
				     update_node_msg->kv_op;

		if ((error_code = kv_op_apply(update_node_msg->kv, op,
					      &kv_set_doc, NULL, &err_msg))) {
			error("%s: Invalid KV for nodes %s: %s", __func__,
			      update_node_msg->node_names, err_msg);
			xfree(err_msg);
			goto end;
		}
	}

	if (update_node_msg->node_addr || update_node_msg->node_hostname ||
	    (update_node_msg->instance_id && update_node_msg->instance_id[0]) ||
	    (update_node_msg->instance_type &&
	     update_node_msg->instance_type[0]))
		uniq = false;

	if (!(host_list = nodespec_to_hostlist(update_node_msg->node_names,
					       uniq, NULL))) {
		error_code = ESLURM_INVALID_NODE_NAME;
		goto end;
	}

	if (!(node_cnt = hostlist_count(host_list))) {
		info("%s: expansion of node specification '%s' resulted in zero nodes",
		     __func__, update_node_msg->node_names);
		error_code = ESLURM_INVALID_NODE_NAME;
		goto end;
	}

	if (update_node_msg->node_addr) {
		hostaddr_list = hostlist_create(update_node_msg->node_addr);
		if (hostaddr_list == NULL) {
			info("update_node: hostlist_create error on %s: %m",
			     update_node_msg->node_addr);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
		if (node_cnt != hostlist_count(hostaddr_list)) {
			info("update_node: nodecount mismatch");
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
	}

	if (update_node_msg->node_hostname) {
		hostname_list = hostlist_create(update_node_msg->node_hostname);
		if (hostname_list == NULL) {
			info("update_node: hostlist_create error on %s: %m",
			     update_node_msg->node_hostname);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
		if (node_cnt != hostlist_count(hostname_list)) {
			info("update_node: nodecount mismatch");
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
	}

	if (update_node_msg->instance_id && update_node_msg->instance_id[0]) {
		instance_id_list =
			hostlist_create(update_node_msg->instance_id);
		if (instance_id_list == NULL) {
			info("update_node: hostlist_create error on %s: %m",
			     update_node_msg->instance_id);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
		if ((hostlist_count(instance_id_list) != 1) &&
		    (node_cnt != hostlist_count(instance_id_list))) {
			info("update_node: nodecount mismatch");
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
	}

	if (update_node_msg->instance_type &&
	    update_node_msg->instance_type[0]) {
		instance_type_list =
			hostlist_create(update_node_msg->instance_type);
		if (instance_type_list == NULL) {
			info("update_node: hostlist_create error on %s: %m",
			     update_node_msg->instance_type);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
		if ((hostlist_count(instance_type_list) != 1) &&
		    (node_cnt != hostlist_count(instance_type_list))) {
			info("update_node: nodecount mismatch");
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
	}

	if ((max_powered_nodes != NO_VAL) &&
	    (update_node_msg->node_state != NO_VAL) &&
	    (update_node_msg->node_state & NODE_STATE_POWER_UP)) {
		bitstr_t *bmp = NULL;
		int count;

		if (hostlist2bitmap(host_list, false, &bmp)) {
			info("update_node: hostlist2bitmap failed");
			FREE_NULL_BITMAP(bmp);
			error_code = ESLURM_INVALID_NODE_NAME;
			goto end;
		}
		bit_or(bmp, power_up_node_bitmap);
		count = bit_set_count(bmp);
		FREE_NULL_BITMAP(bmp);
		if (count > max_powered_nodes) {
			error("update_node: Cannot power up more nodes due to MaxPoweredUpNodes=%d",
			      max_powered_nodes);
			error_code = ESLURM_MAX_POWERED_NODES;
			goto end;
		}
		log_flag(POWER, "powered nodes good %d", count);
	}

	/* Validate specified power action or default action for type */
	if (((update_node_msg->node_state != NO_VAL) &&
	     (update_node_msg->node_state &
	      (NODE_STATE_POWER_UP | NODE_STATE_POWER_DOWN))) ||
	    update_node_msg->power_action_name) {
		power_action_type_t type = POWER_ACTION_NONE;

		if (update_node_msg->node_state & NODE_STATE_POWER_UP)
			type = POWER_ACTION_RESUME;
		else if (update_node_msg->node_state & NODE_STATE_POWER_DOWN)
			type = POWER_ACTION_SUSPEND;

		if (!power_save_valid_action_default(
			    type, update_node_msg->power_action_name)) {
			error("Invalid power action %s for nodes %s",
			      update_node_msg->power_action_name ?
			      update_node_msg->power_action_name : "(default)",
			      update_node_msg->node_names);
			error_code = ESLURM_INVALID_POWER_ACTION;
			goto end;
		}
	}

	/*
	 * If a single instance_id/instance_type is specified for multiple
	 * nodes, broadcast the value to all nodes.
	 */
	if (instance_id_list && (hostlist_count(instance_id_list) == 1)) {
		instance_id_broadcast = hostlist_shift(instance_id_list);
		FREE_NULL_HOSTLIST(instance_id_list);
	}
	if (instance_type_list && (hostlist_count(instance_type_list) == 1)) {
		instance_type_broadcast = hostlist_shift(instance_type_list);
		FREE_NULL_HOSTLIST(instance_type_list);
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		int err_code = 0, kv_rc;
		bool acct_updated = false;
		bool resume_keep_tres = false;
		bool resume_tres = false;
		bool update_db = false;

		node_ptr = find_node_record (this_node_name);
		if (node_ptr == NULL) {
			error ("update_node: node %s does not exist",
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			xfree(this_node_name);
			break;
		}

		if (hostaddr_list) {
			xfree(node_ptr->comm_name);
			node_ptr->comm_name = hostlist_shift(hostaddr_list);
		}
		if (hostname_list) {
			xfree(node_ptr->node_hostname);
			node_ptr->node_hostname = hostlist_shift(hostname_list);
		}
		if (hostaddr_list || hostname_list) {
			/* This updates the lookup table addresses */
			slurm_reset_alias(node_ptr->name, node_ptr->comm_name,
					  node_ptr->node_hostname);
		}

		if (update_node_msg->cert_token) {
			xfree(node_ptr->cert_token);
			if (update_node_msg->cert_token[0])
				node_ptr->cert_token = xstrdup(
					update_node_msg->cert_token);
		}

		if (update_node_msg->cpu_bind) {
			char tmp_str[128];
			slurm_sprint_cpu_bind_type(tmp_str,
						   update_node_msg->cpu_bind);
			info("update_node: setting CpuBind to %s for node %s",
			     tmp_str, this_node_name);
			if (update_node_msg->cpu_bind == CPU_BIND_OFF)
				node_ptr->cpu_bind = 0;
			else
				node_ptr->cpu_bind = update_node_msg->cpu_bind;
		}

		if (update_node_msg->features || update_node_msg->features_act) {
			char *features_act = NULL, *features_avail = NULL;

			if (update_node_msg->features_act)
				features_act = update_node_msg->features_act;
			else
				features_act = node_ptr->features_act;

			if (update_node_msg->features)
				features_avail = update_node_msg->features;
			else
				features_avail = node_ptr->features;
			if (!_valid_features_act(features_act, features_avail)){
				info("%s: Invalid ActiveFeatures (\'%s\' not subset of \'%s\' on node %s)",
				     __func__, features_act, features_avail,
				     node_ptr->name);
				error_code = ESLURM_ACTIVE_FEATURE_NOT_SUBSET;
				xfree(update_node_msg->features);
				xfree(update_node_msg->features_act);
			}
		}

		if (update_node_msg->features_act) {
			if (node_ptr->features_act)
				orig_features_act =
					xstrdup(node_ptr->features_act);
			else
				orig_features_act = xstrdup(node_ptr->features);
		}
		if (update_node_msg->features) {
			if (!update_node_msg->features_act &&
			    (node_features_g_count() == 0)) {
				/*
				 * If no NodeFeatures plugin and no explicit
				 * active features, then make active and
				 * available feature values match
				 */
				update_node_msg->features_act =
					xstrdup(update_node_msg->features);
			}
			xfree(node_ptr->features);
			if (update_node_msg->features[0]) {
				node_ptr->features =
					node_features_g_node_xlate2(
						update_node_msg->features);
			}
			/*
			 * update_node_avail_features() logs and updates
			 * avail_feature_list below
			 */
		}

		if (update_node_msg->features_act) {
			tmp_feature = node_features_g_node_xlate(
					update_node_msg->features_act,
					orig_features_act, node_ptr->features,
					node_ptr->index);
			xfree(node_ptr->features_act);
			node_ptr->features_act = tmp_feature;
			error_code = update_node_active_features(
						node_ptr->name,
						node_ptr->features_act,
						FEATURE_MODE_COMB);
			xfree(orig_features_act);
		}

		if (update_node_msg->gres) {
			xfree(node_ptr->gres);
			if (update_node_msg->gres[0])
				node_ptr->gres = xstrdup(update_node_msg->gres);
			/* _update_node_gres() logs and updates config */
		}

		if (update_node_msg->extra) {
			data_t *data = NULL;
			char *extra = update_node_msg->extra;

			if (extra[0] && extra_constraints_enabled() &&
			    serialize_g_string_to_data(&data, extra,
						       strlen(extra),
						       MIME_TYPE_JSON)) {
				error("Failed to decode extra \"%s\" for node %s",
				      update_node_msg->extra, node_ptr->name);
				error_code = ESLURM_INVALID_EXTRA;
			} else {
				FREE_NULL_DATA(node_ptr->extra_data);
				node_ptr->extra_data = data;
				xfree(node_ptr->extra);
				if (update_node_msg->extra[0]) {
					node_ptr->extra =
						xstrdup(update_node_msg->extra);
				}
				/*
				 * Skip db updates for extra field changes,
				 * otherwise we'll overwhelm it with event records
				 * if someone is updating these constantly.
				 */
				// update_db = true;
			}
		}

		if (update_node_msg->comment) {
			xfree(node_ptr->comment);
			if (update_node_msg->comment[0])
				node_ptr->comment =
					xstrdup(update_node_msg->comment);
		}

		/*
		 * Like extra, a KV change deliberately leaves update_db
		 * alone: node changes reach the database as event records,
		 * and metadata churn would flood them.
		 */
		if ((kv_rc = _update_node_kv(node_ptr, update_node_msg,
					     kv_set_doc)))
			error_code = kv_rc;

		if (instance_id_broadcast) {
			xfree(node_ptr->instance_id);
			node_ptr->instance_id = xstrdup(instance_id_broadcast);
			update_db = true;
		} else if (instance_id_list) {
			xfree(node_ptr->instance_id);
			node_ptr->instance_id =
				hostlist_shift(instance_id_list);
			update_db = true;
		} else if (update_node_msg->instance_id) {
			xfree(node_ptr->instance_id);
			update_db = true;
		}

		if (instance_type_broadcast) {
			xfree(node_ptr->instance_type);
			node_ptr->instance_type =
				xstrdup(instance_type_broadcast);
			update_db = true;
		} else if (instance_type_list) {
			xfree(node_ptr->instance_type);
			node_ptr->instance_type =
				hostlist_shift(instance_type_list);
			update_db = true;
		} else if (update_node_msg->instance_type) {
			xfree(node_ptr->instance_type);
			update_db = true;
		}

		if (update_db) {
			clusteracct_storage_g_node_update(acct_db_conn,
							  node_ptr);
		}

		if ((update_node_msg->resume_after != NO_VAL) &&
		    ((update_node_msg->node_state == NODE_STATE_DOWN) ||
		     (update_node_msg->node_state == NODE_STATE_DRAIN))) {
			if (update_node_msg->resume_after == INFINITE)
				resume_after = 0;
			else
				resume_after =
					now + update_node_msg->resume_after;
		}

		if (update_node_msg->topology_str) {
			int rc = SLURM_SUCCESS;
			if ((rc = node_mgr_set_node_topology(
				     node_ptr, update_node_msg->topology_str)))
				error_code = rc;
		}

		state_val = update_node_msg->node_state;
		if (_equivalent_node_state(node_ptr, state_val)) {
			if (state_val == NODE_STATE_DRAIN)
				cancel_power_down_undrain(node_ptr);

			/* Update resume time if another equivalent update */
			if (resume_after != NO_VAL) {
				node_ptr->resume_after = resume_after;
				info("update_node: node %s will be resumed on %lu",
				     this_node_name, node_ptr->resume_after);
			}

			/*
			 * No accounting update if node state and reason are
			 * unchanged
			 */
			if(!xstrcmp(node_ptr->reason,
				    update_node_msg->reason)) {
				xfree(this_node_name);
				continue;
			}
		} else if (resume_after != NO_VAL) {
			/* Set resume time for the 1st time */
			node_ptr->resume_after = resume_after;
			info("update_node: node %s will be resumed on %lu",
			     this_node_name, node_ptr->resume_after);
		} else if (node_ptr->resume_after) {
			/*
			 * Reset resume time if the state updates to another
			 * different from down or drain
			 */
			node_ptr->resume_after = 0;
			info("update_node: ResumeAfter reset for node %s after a state change",
			     this_node_name);
		}

		if ((update_node_msg -> reason) &&
		    (update_node_msg -> reason[0])) {
			xfree(node_ptr->reason);
			node_ptr->reason = xstrdup(update_node_msg->reason);
			node_ptr->reason_time = now;
			node_ptr->reason_uid = msg->auth.uid;
			info ("update_node: node %s reason set to: %s",
				this_node_name, node_ptr->reason);
		}

		if (state_val != NO_VAL) {
			base_state = node_ptr->node_state;
			if (!_valid_node_state_change(base_state, state_val)) {
				info("Invalid node state transition requested "
				     "for node %s from=%s to=%s",
				     this_node_name,
				     node_state_string(base_state),
				     node_state_string(state_val));
				state_val = NO_VAL;
				error_code =
					ESLURM_INVALID_NODE_STATE_TRANSITION;
			}
			base_state &= NODE_STATE_BASE;
		}

		/*
		 * RESUME_KEEP_TRES is RESUME without the drained TRES coming
		 * back with the node. Fold it into RESUME now that the
		 * transition has been validated, and any rejection reported,
		 * under the name the admin asked for, so that the two share
		 * one body below.
		 */
		resume_keep_tres = (state_val == NODE_RESUME_KEEP_TRES);
		resume_tres = (state_val == NODE_RESUME);
		if (state_val == NODE_RESUME_KEEP_TRES)
			state_val = NODE_RESUME;

		if (state_val != NO_VAL) {
			node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
			if (state_val == NODE_RESUME) {
				bool reset_uuid_maps =
					resume_tres &&
					IS_NODE_DRAIN(node_ptr) &&
					node_ptr->reason &&
					!xstrcmp(node_ptr->reason,
						 GRES_UUID_ORDER_DRAIN) &&
					(node_ptr->reason_uid ==
					 slurm_conf.slurm_user_id);

				trigger_node_resume(node_ptr);
				if (IS_NODE_IDLE(node_ptr) &&
				    (IS_NODE_DRAIN(node_ptr) ||
				     IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				node_ptr->node_state &=
					(~NODE_STATE_REBOOT_REQUESTED);
				node_ptr->node_state &=
					(~NODE_STATE_REBOOT_ISSUED);

				/*
				 * State=RESUME acknowledges the automatic
				 * UUID-order drain. Drop its saved map so
				 * the next slurmd registration saves the
				 * replacement device order.
				 */
				if (reset_uuid_maps)
					gres_node_reset_uuid_maps(
						node_ptr->gres_list);

				/*
				 * Return the drained TRES that are present.
				 * An absent GRES index stays drained. Reconcile
				 * DEGRADED and the accounting event as a
				 * per-TRES resume would. The limit check can
				 * drain the node again when what remains is
				 * over MaxDrainedTres.
				 */
				if (resume_tres &&
				    node_conf_resume_all_drained_tres(
					    node_ptr)) {
					gres_node_note_absent_drain(
						node_ptr->gres_list,
						node_ptr->name, &absent_note);
					_reconcile_tres_drain(node_ptr, NULL,
							      msg->auth.uid,
							      now);
				}

				if (IS_NODE_POWERING_DOWN(node_ptr)) {
					node_ptr->node_state &=
						(~NODE_STATE_INVALID_REG);
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_DOWN);
					node_ptr->node_state |=
						NODE_STATE_POWERED_DOWN;

					if (IS_NODE_CLOUD(node_ptr))
						set_node_comm_name(
							node_ptr, NULL,
							node_ptr->name);

					node_ptr->power_save_req_time = 0;

					reset_node_active_features(node_ptr);
					reset_node_instance(node_ptr);
					reset_node_topology(node_ptr);
					hres_reset_node(node_ptr);

					clusteracct_storage_g_node_down(
						acct_db_conn,
						node_ptr, now,
						"Powered down after resume",
						node_ptr->reason_uid);
				}

				if (IS_NODE_DOWN(node_ptr)) {
					state_val = NODE_STATE_IDLE;
					_require_node_reg(node_ptr);
				} else if (IS_NODE_FUTURE(node_ptr)) {
					if (node_ptr->port == 0) {
						node_ptr->port =
							slurm_conf.slurmd_port;
					}
					state_val = NODE_STATE_IDLE;
					bit_clear(future_node_bitmap,
						  node_ptr->index);

					_require_node_reg(node_ptr);
				} else if (node_flags & NODE_STATE_DRAIN) {
					state_val = base_state;
					_require_node_reg(node_ptr);
				} else
					state_val = base_state;
			} else if (state_val == NODE_STATE_UNDRAIN) {
				if (IS_NODE_IDLE(node_ptr) &&
				    IS_NODE_DRAIN(node_ptr)) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				_require_node_reg(node_ptr);
				state_val = base_state;
			}

			if ((state_val == NODE_STATE_DOWN) ||
			    (state_val == NODE_STATE_FUTURE)) {
				/* We must set node DOWN before killing
				 * its jobs */
				_make_node_down(node_ptr, now);
				kill_running_job_by_node_ptr(node_ptr);
				if (state_val == NODE_STATE_FUTURE) {
					bool dyn_norm_node = false;
					if (IS_NODE_DYNAMIC_FUTURE(node_ptr)) {
						/* Reset comm and hostname */
						set_node_comm_name(
							node_ptr, NULL,
							node_ptr->name);
						reset_node_instance(node_ptr);
					}
					/*
					 * Preserve dynamic norm state until
					 * node is deleted.
					 */
					if (IS_NODE_DYNAMIC_NORM(node_ptr))
						dyn_norm_node = true;
					node_ptr->node_state =
						NODE_STATE_FUTURE;
					if (dyn_norm_node)
						node_ptr->node_state |=
							NODE_STATE_DYNAMIC_NORM;
					if (node_ptr->hold_cnt)
						node_ptr->node_state |=
							NODE_STATE_COMPLETING;
					bit_set(future_node_bitmap,
						node_ptr->index);
					bit_clear(power_up_node_bitmap,
						  node_ptr->index);
					clusteracct_storage_g_node_down(
						acct_db_conn,
						node_ptr, now,
						"Set to State=FUTURE",
						node_ptr->reason_uid);
				}
			} else if (state_val == NODE_STATE_IDLE) {
				/* assume they want to clear DRAIN and
				 * FAIL flags too */
				if (IS_NODE_DOWN(node_ptr)) {
					trigger_node_up(node_ptr);
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				} else if (IS_NODE_IDLE(node_ptr)   &&
					   (IS_NODE_DRAIN(node_ptr) ||
					    IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				}	/* else already fully available */
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				if (!IS_NODE_NO_RESPOND(node_ptr) ||
				     IS_NODE_POWERED_DOWN(node_ptr))
					make_node_avail(node_ptr);
				bit_set (idle_node_bitmap, node_ptr->index);
				bit_set (up_node_bitmap, node_ptr->index);
				if (IS_NODE_POWERED_DOWN(node_ptr))
					node_ptr->last_busy = 0;
				else
					node_ptr->last_busy = now;
			} else if (state_val == NODE_STATE_ALLOCATED) {
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)  &&
				    !IS_NODE_NO_RESPOND(node_ptr))
					make_node_avail(node_ptr);
				bit_set (up_node_bitmap, node_ptr->index);
				bit_clear (idle_node_bitmap, node_ptr->index);
			} else if ((state_val == NODE_STATE_DRAIN) ||
				   (state_val == NODE_STATE_FAIL)) {
				if (IS_NODE_ALLOCATED(node_ptr) &&
				    (IS_NODE_POWERED_DOWN(node_ptr) ||
				     IS_NODE_POWERING_UP(node_ptr))) {
					info("%s: DRAIN/FAIL request for node %s which is allocated and being powered up. Requeuing jobs",
					     __func__, this_node_name);
					kill_running_job_by_node_ptr(node_ptr);
				}
				/* Notify of strigger event */
				if (state_val == NODE_STATE_DRAIN) {
					trigger_node_draining(node_ptr);
					cancel_power_down_undrain(node_ptr);
				} else {
					trigger_node_failing(node_ptr);
				}
				bit_clear (avail_node_bitmap, node_ptr->index);
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				state_val = node_ptr->node_state |= state_val;
				if ((node_ptr->run_job_cnt == 0) &&
				    (node_ptr->comp_job_cnt == 0) &&
				    !node_ptr->hold_cnt) {
					if (state_val & NODE_STATE_DRAIN)
						trigger_node_drained(node_ptr);
					clusteracct_storage_g_node_down(
						acct_db_conn,
						node_ptr, now, NULL,
						node_ptr->reason_uid);
				}
			} else if (state_val & NODE_STATE_POWER_DOWN) {
				if ((state_val & NODE_STATE_POWER_UP) &&
				    (IS_NODE_POWERING_UP(node_ptr))) {
					/* Clear any reboot op in progress */
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
					node_ptr->last_response = now;
					xfree(this_node_name);
					continue;
				}

				/* Abort reboot request */
				if (IS_NODE_REBOOT_REQUESTED(node_ptr) ||
				    IS_NODE_REBOOT_ISSUED(node_ptr)) {
					/*
					 * A node is DOWN+REBOOT_ISSUED when the
					 * reboot has been issued. Set node
					 * state back to idle only if the reboot
					 * has been issued. Node will remain
					 * cleared in the avail_node_bitmap
					 * until the node is powered down.
					 */
					if (IS_NODE_REBOOT_ISSUED(node_ptr) &&
					    IS_NODE_DOWN(node_ptr)) {
						node_ptr->node_state =
							NODE_STATE_IDLE |
							(node_ptr->node_state &
							 NODE_STATE_FLAGS);
					}

					node_ptr->node_state &=
						(~NODE_STATE_REBOOT_REQUESTED);
					node_ptr->node_state &=
						(~NODE_STATE_REBOOT_ISSUED);
					xfree(node_ptr->reason);

					info("Canceling REBOOT on node %s",
					     this_node_name);
				}

				if (IS_NODE_POWERING_DOWN(node_ptr)) {
					info("ignoring power down request for node %s, already powering down",
					     this_node_name);
					if (!power_skip_list)
						power_skip_list =
							hostlist_create(NULL);
					hostlist_push_host(power_skip_list,
							   this_node_name);
					node_ptr->next_state = NO_VAL;
					xfree(this_node_name);
					continue;
				}

				if (state_val & NODE_STATE_POWERED_DOWN) {
					/* Force power down */
					_make_node_unavail(node_ptr);
					/*
					 * Kill any running jobs and requeue if
					 * possible.
					 */
					kill_running_job_by_node_ptr(node_ptr);
					node_ptr->node_state &=
						(~NODE_STATE_POWER_UP);
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
				} else if (state_val & NODE_STATE_POWER_DRAIN) {
					/* power down asap -- drain */
					if (!IS_NODE_DRAIN(node_ptr))
						node_ptr->next_state =
							NODE_STATE_UNDRAIN;
					_drain_node(node_ptr, "POWER_DOWN_ASAP",
						    node_ptr->reason_uid, now);
				}
				if (IS_NODE_DOWN(node_ptr)) {
					/* Abort any power up request */
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
				}

				if (IS_NODE_POWERED_DOWN(node_ptr))
					info("power down request repeating for node %s",
					     this_node_name);
				else
					info("powering down node %s",
					     this_node_name);

				xfree(node_ptr->power_action_name);
				node_ptr->power_action_name =
					xstrdup(update_node_msg
							->power_action_name);
				node_ptr->node_state |=
					NODE_STATE_POWER_DOWN;

				if (IS_NODE_IDLE(node_ptr)) {
					/*
					 * remove node from avail_node_bitmap so
					 * that it will power_down before jobs
					 * get on it.
					 */
					bit_clear(avail_node_bitmap,
						  node_ptr->index);
				}
				/*
				 * Clear all next_state flags except
				 * NODE_STATE_UNDRAIN, if it's set.
				 */
				if ((node_ptr->next_state != NO_VAL) &&
				    (node_ptr->next_state & NODE_STATE_UNDRAIN))
					node_ptr->next_state =
						NODE_STATE_UNDRAIN;
				else
					node_ptr->next_state = NO_VAL;
				bit_clear(rs_node_bitmap, node_ptr->index);
				xfree(this_node_name);
				continue;
			} else if (state_val & NODE_STATE_POWER_UP) {
				if (state_val & NODE_STATE_POWERED_DOWN) {
					info("force power up request for node %s",
					     this_node_name);
					/*
					 * Kill any running jobs and requeue if
					 * possible.
					 */
					kill_running_job_by_node_ptr(node_ptr);
					node_ptr->node_state |=
						NODE_STATE_POWERED_DOWN;
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_DOWN);
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
					node_ptr->node_state &=
						(~NODE_STATE_POWER_DRAIN);
					if ((node_ptr->next_state != NO_VAL) &&
					    (node_ptr->next_state &
					     NODE_STATE_UNDRAIN)) {
						node_ptr->node_state &=
							(~NODE_STATE_DRAIN);
						node_ptr->next_state = NO_VAL;
					}
				} else if (!IS_NODE_POWERED_DOWN(node_ptr)) {
					if (IS_NODE_POWERING_UP(node_ptr)) {
						info("power up request repeating for node %s",
						     this_node_name);
						node_ptr->node_state |=
							NODE_STATE_POWERED_DOWN;
					} else {
						verbose("node %s is already powered up",
							this_node_name);
						if (!power_skip_list)
							power_skip_list =
								hostlist_create(
									NULL);
						hostlist_push_host(
							power_skip_list,
							this_node_name);
						xfree(this_node_name);
						continue;
					}
				} else {
					info("powering up node %s",
					     this_node_name);
				}
				node_ptr->node_state |= NODE_STATE_POWER_UP;

				xfree(node_ptr->power_action_name);
				node_ptr->power_action_name =
					xstrdup(update_node_msg
							->power_action_name);
				bit_set(power_up_node_bitmap, node_ptr->index);
				if (!IS_NODE_POWER_DOWN(node_ptr))
					node_ptr->next_state = NO_VAL;
				bit_clear(rs_node_bitmap, node_ptr->index);
				xfree(this_node_name);
				continue;
			} else if (state_val == NODE_STATE_NO_RESPOND) {
				node_ptr->node_state |= NODE_STATE_NO_RESPOND;
				state_val = base_state;
				bit_clear(avail_node_bitmap, node_ptr->index);
			} else if (state_val == NODE_STATE_REBOOT_CANCEL) {
				if (!IS_NODE_REBOOT_ISSUED(node_ptr)) {
					node_ptr->node_state &=
						(~NODE_STATE_REBOOT_REQUESTED);
					state_val = base_state;
					if ((node_ptr->next_state != NO_VAL) &&
					    (node_ptr->next_state &
					     NODE_STATE_UNDRAIN))
						_undo_reboot_asap(node_ptr);
				} else {
					info("REBOOT on node %s already in progress -- unable to cancel",
					     this_node_name);
					err_code = error_code =
						ESLURM_REBOOT_IN_PROGRESS;
				}
			} else if (state_val == NODE_STATE_UNKNOWN) {
				/*
				 * Node has not registered yet; leave it UNKNOWN
				 * (drain/fail cleared above) until registration
				 * sets its real state.
				 */
			} else {
				info("Invalid node state specified %u",
				     state_val);
				err_code = 1;
				error_code = ESLURM_INVALID_NODE_STATE;
			}

			if (err_code == 0) {
				node_ptr->node_state = state_val |
						(node_ptr->node_state &
						 NODE_STATE_FLAGS);

				if (!IS_NODE_REBOOT_REQUESTED(node_ptr) &&
				    !IS_NODE_REBOOT_ISSUED(node_ptr) &&
				    !(IS_NODE_POWER_DOWN(node_ptr) &&
				      (node_ptr->next_state ==
				       NODE_STATE_UNDRAIN)))
					node_ptr->next_state = NO_VAL;
				bit_clear(rs_node_bitmap, node_ptr->index);

				info ("update_node: node %s state set to %s",
					this_node_name,
					node_state_string(state_val));
			}
		}

		/*
		 * RESUME_KEEP_TRES, UNDRAIN and IDLE clear the whole-node drain
		 * and leave the TRES drained. A node still over MaxDrainedTres
		 * must be redrained with the MaxDrainedTres reason
		 */
		if (resume_keep_tres ||
		    (update_node_msg->node_state == NODE_STATE_UNDRAIN) ||
		    (state_val == NODE_STATE_IDLE))
			(void) _enforce_max_drained_tres(node_ptr,
							 msg->auth.uid, now,
							 true);

		/*
		 * After all the possible state changes, check if we need to
		 * clear the asap_node_bitmap.
		 */
		if (!IS_NODE_REBOOT_ASAP(node_ptr))
			bit_clear(asap_node_bitmap, node_ptr->index);

		if (!acct_updated && !IS_NODE_DOWN(node_ptr) &&
		    !IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			if (IS_NODE_DEGRADED(node_ptr)) {
				/*
				 * node_up() would also end the drained-TRES
				 * interval. node_mgr_update_degraded_event() below
				 * replaces the whole-node event itself, so only
				 * clear the reason as node_up() does.
				 */
				xfree(node_ptr->reason);
				node_ptr->reason_time = 0;
				node_ptr->reason_uid = NO_VAL;
			} else {
				/* reason information is handled in
				   clusteracct_storage_g_node_up()
				*/
				clusteracct_storage_g_node_up(acct_db_conn,
							      node_ptr, now);
			}
		}

		/*
		 * Keep the node's drained-TRES accounting event in sync. While
		 * the node is degraded (some TRES drained but otherwise usable)
		 * this keeps an open event recording the drained TRES rather
		 * than letting the node_up above close it. It is idempotent and
		 * a no-op when nothing changed.
		 */
		if (IS_NODE_DEGRADED(node_ptr))
			node_mgr_update_degraded_event(node_ptr,
						       node_ptr->reason,
						       node_ptr->reason_uid,
						       now);

		xfree(this_node_name);
	}

	/* Write/clear log */
	(void)update_node_active_features(NULL, NULL, FEATURE_MODE_PEND);

	last_node_update = now;

	if ((error_code == SLURM_SUCCESS) && (update_node_msg->features)) {
		error_code = update_node_avail_features(
					update_node_msg->node_names,
					update_node_msg->features,
					FEATURE_MODE_IND);
	}
	if ((error_code == SLURM_SUCCESS) && (update_node_msg->gres)) {
		error_code =
			_update_node_gres(update_node_msg->node_names,
					  update_node_msg->gres, msg->auth.uid);
	}
	if ((error_code == SLURM_SUCCESS) &&
	    (update_node_msg->suspend_time != NO_VAL)) {
		error_code =
			_update_node_suspend_time(update_node_msg->node_names,
						  update_node_msg
							  ->suspend_time);
		/*
		 * A finite per-node SuspendTime can be the first thing to
		 * enable automatic power save; refresh the node-level timeouts
		 * and the enabled flag so the power-save thread acts on it
		 * without a reconfigure.
		 */
		if (error_code == SLURM_SUCCESS) {
			power_save_set_timeouts(NULL);
			power_save_exc_setup();
		}
	}

	/*
	 * Update weight. Weight is part of config_ptr,
	 * hence split config records if required
	 */
	if ((error_code == SLURM_SUCCESS) &&
	    (update_node_msg->weight != NO_VAL))	{
		error_code = _update_node_weight(update_node_msg->node_names,
						 update_node_msg->weight);
		if (error_code == SLURM_SUCCESS) {
			/* sort config_list by weight for scheduling */
			list_sort(config_list, &list_compare_config);
		}
	}

	if ((error_code == SLURM_SUCCESS) && update_node_msg->features)
		_update_parts();

end:
	FREE_NULL_HOSTLIST(host_list);
	FREE_NULL_HOSTLIST(hostaddr_list);
	FREE_NULL_HOSTLIST(hostname_list);
	FREE_NULL_HOSTLIST(instance_id_list);
	FREE_NULL_HOSTLIST(instance_type_list);
	xfree(instance_id_broadcast);
	xfree(instance_type_broadcast);
	FREE_NULL_DATA(kv_set_doc);
	_record_absent_gres_note(absent_note, user_msg);
	/* A single request is either a power up or a power down */
	if (user_msg && power_skip_list) {
		char *skipped_nodes =
			hostlist_ranged_string_xmalloc(power_skip_list);
		if (update_node_msg->node_state & NODE_STATE_POWER_DOWN)
			xstrfmtcat(
				*user_msg,
				"%sSkipping power down of nodes %s due to current node state.",
				(*user_msg ? "\n" : ""), skipped_nodes);
		else
			xstrfmtcat(
				*user_msg,
				"%sSkipping power up of nodes %s due to current node state.",
				(*user_msg ? "\n" : ""), skipped_nodes);
		xfree(skipped_nodes);
	}
	FREE_NULL_HOSTLIST(power_skip_list);

	return error_code;
}

/*
 * Reconcile a node after its drained-TRES set changed.
 *
 * The request is resolved before anything is applied, so a caller that failed
 * changed nothing and must not call this. Reconciling only on a real change
 * matters: otherwise a node can be genuinely degraded but left unflagged and
 * un-limit-checked until its next registration.
 */
static void _reconcile_tres_drain(node_record_t *node_ptr, char *reason,
				  uid_t auth_uid, time_t now)
{
	bool was_degraded = IS_NODE_DEGRADED(node_ptr);

	(void) node_conf_set_degraded_flag(node_ptr);

	/*
	 * Drain the node if the admin drained more of a TRES than the node's
	 * MaxDrainedTres limit allows.
	 */
	(void) _enforce_max_drained_tres(node_ptr, auth_uid, now, false);

	/*
	 * Record the drained-TRES accounting event here, where the drain change
	 * is known, rather than relying on a later hook that can be skipped
	 * when the node's reason is unchanged (e.g. draining several TRES with
	 * the same Reason). A node that was not degraded and still is not has
	 * nothing to record or close.
	 */
	if (was_degraded || IS_NODE_DEGRADED(node_ptr))
		node_mgr_update_degraded_event(node_ptr, reason, auth_uid, now);
}

/*
 * Resolve msg on every node in node_bitmap. A dry run only records failures;
 * otherwise also apply each change.
 */
static int _update_nodes_tres_drain(bitstr_t *node_bitmap,
				    update_tres_msg_t *msg, bool dry_run,
				    uid_t auth_uid, time_t now,
				    bitstr_t **failed_nodes,
				    bitstr_t **changed_nodes, bool *any_change,
				    char **absent_note)
{
	node_record_t *node_ptr;
	int error_code = SLURM_SUCCESS;

	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		bool did_change = false;
		int err_code =
			_update_node_tres_drain(node_ptr, msg, &did_change,
						dry_run, absent_note);

		if (err_code) {
			if (!error_code)
				error_code = err_code;
			if (!*failed_nodes)
				*failed_nodes = bit_alloc(node_record_count);
			bit_set(*failed_nodes, node_ptr->index);
		}

		if (dry_run || !did_change)
			continue;

		_reconcile_tres_drain(node_ptr, msg->reason, auth_uid, now);
		*any_change = true;
		if (!*changed_nodes)
			*changed_nodes = bit_alloc(node_record_count);
		bit_set(*changed_nodes, node_ptr->index);
	}

	return error_code;
}

/*
 * Check the shape of an Index= list: comma-separated "N" or "N-M", or one
 * "N-M:S". bit_unfmt() reads an empty segment as index 0 and stops at a
 * leading comma, so "2," would add index 0 and ",2" would select nothing.
 */
static bool _valid_index_list(const char *str)
{
	const char *p = str;

	while (true) {
		long first, last;
		char *end;

		if (!isdigit((unsigned char) *p))
			return false;
		first = strtol(p, &end, 10);
		if (*end == '-') {
			if (!isdigit((unsigned char) end[1]))
				return false;
			last = strtol(end + 1, &end, 10);
			if (last < first)
				return false;
			if (*end == ':') {
				/* A step range must be the whole list */
				if ((p != str) ||
				    !isdigit((unsigned char) end[1]))
					return false;
				return (strtol(end + 1, &end, 10) > 0) && !*end;
			}
		}
		if (!*end)
			return true;
		if (*end != ',')
			return false;
		p = end + 1;
	}
}

extern int node_mgr_update_tres_drain(update_tres_msg_t *update_tres_msg,
				      uid_t auth_uid, char **user_msg)
{
	int error_code = SLURM_SUCCESS;
	char *absent_note = NULL;
	hostlist_t *host_list = NULL;
	bitstr_t *node_bitmap = NULL;
	bitstr_t *failed_nodes = NULL, *changed_nodes = NULL;
	time_t now = time(NULL);
	bool any_change = false;
	bool has_index, has_uuid, has_count;
	int sel_cnt;

	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, WRITE_LOCK));

	if (user_msg)
		*user_msg = NULL;

	if (!update_tres_msg->name || !update_tres_msg->name[0])
		return ESLURM_INVALID_TRES;
	if ((update_tres_msg->action != UPDATE_ADD) &&
	    (update_tres_msg->action != UPDATE_REMOVE) &&
	    (update_tres_msg->action != UPDATE_SET))
		return ESLURM_INVALID_NODE_STATE_TRANSITION;

	/*
	 * A resume has nothing to record a reason against, so it must not carry
	 * one; a drain must.
	 */
	if (update_tres_msg->action == UPDATE_REMOVE) {
		if (update_tres_msg->reason && update_tres_msg->reason[0])
			return ESLURM_INVALID_NODE_STATE_TRANSITION;
	} else if (!update_tres_msg->reason || !update_tres_msg->reason[0]) {
		return ESLURM_INVALID_NODE_STATE_TRANSITION;
	}

	/*
	 * The selectors are mutually exclusive, with one exception: a uuid and
	 * an index may be given together to name one device two ways (the
	 * uuid selects it and the index is checked against where it currently
	 * sits), so a "scontrol show drainedtres" line can be handed back
	 * as-is.
	 *
	 * scontrol checks this too, but every other client (slurmrestd, or
	 * anything built on the API) fills the message in directly. Without
	 * this an over-specified request would be applied using whichever
	 * selector _update_node_tres_drain() happens to consider first, so
	 * "everything, and also a count of 2" would silently drain everything.
	 */
	has_index = (update_tres_msg->index && update_tres_msg->index[0]);
	has_uuid = (update_tres_msg->uuid && update_tres_msg->uuid[0]);
	has_count = (update_tres_msg->count != NO_VAL64);

	/*
	 * An empty string selects nothing. Drop it so that no resolver reads
	 * it as a selector alongside the one that was counted.
	 */
	if (!has_index)
		xfree(update_tres_msg->index);
	if (!has_uuid)
		xfree(update_tres_msg->uuid);

	if (has_index && !_valid_index_list(update_tres_msg->index))
		return ESLURM_INVALID_TRES;

	sel_cnt = (update_tres_msg->select_all ? 1 : 0) + has_index + has_uuid +
		  has_count;
	if (!sel_cnt) {
		/*
		 * A request must name what it acts on. An empty selector is
		 * most likely an unset shell variable, and reading it as the
		 * empty set would turn a SetDrain into a resume of everything.
		 */
		return ESLURM_INVALID_TRES;
	} else if ((sel_cnt != 1) &&
		   !((sel_cnt == 2) && has_index && has_uuid)) {
		return ESLURM_INVALID_TRES;
	}

	if (!update_tres_msg->node_names) {
		info("Update TRES: invalid node name");
		return ESLURM_INVALID_NODE_NAME;
	}

	if (!(host_list = nodespec_to_hostlist(update_tres_msg->node_names,
					       true, NULL)))
		return ESLURM_INVALID_NODE_NAME;

	if (!hostlist_count(host_list)) {
		info("Update TRES: expansion of node specification '%s' resulted in zero nodes",
		     update_tres_msg->node_names);
		FREE_NULL_HOSTLIST(host_list);
		return ESLURM_INVALID_NODE_NAME;
	}

	/*
	 * Resolve every node before changing any of them. A hostlist naming a
	 * node that does not exist is a typo, and draining the rest of the
	 * list while returning a failure leaves the admin unable to tell what
	 * was applied.
	 */
	if (hostlist2bitmap(host_list, false, &node_bitmap)) {
		info("Update TRES: invalid node name in %s; no TRES were changed",
		     update_tres_msg->node_names);
		FREE_NULL_HOSTLIST(host_list);
		FREE_NULL_BITMAP(node_bitmap);
		return ESLURM_INVALID_NODE_NAME;
	}
	FREE_NULL_HOSTLIST(host_list);

	/*
	 * Selector legality can vary by node (different CPU counts, GRES types,
	 * or UUID maps). Validate the complete target set before applying to any
	 * node so the RPC is atomic from the administrator's perspective.
	 */
	error_code =
		_update_nodes_tres_drain(node_bitmap, update_tres_msg, true,
					 auth_uid, now, &failed_nodes,
					 &changed_nodes, &any_change, NULL);
	if (failed_nodes) {
		char *failed_str = bitmap2node_name(failed_nodes);

		info("Update TRES: %s could not be updated on %s; no TRES were changed",
		     update_tres_msg->name, failed_str);
		xfree(failed_str);
		FREE_NULL_BITMAP(failed_nodes);
		FREE_NULL_BITMAP(node_bitmap);
		return error_code;
	}

	error_code = _update_nodes_tres_drain(node_bitmap, update_tres_msg,
					      false, auth_uid, now,
					      &failed_nodes, &changed_nodes,
					      &any_change, &absent_note);
	FREE_NULL_BITMAP(node_bitmap);

	/*
	 * Validation and apply run under the same node write lock, so this
	 * should only catch an internal mismatch between the two paths.
	 */
	if (failed_nodes) {
		char *failed_str = bitmap2node_name(failed_nodes);
		char *changed_str =
			changed_nodes ? bitmap2node_name(changed_nodes) : NULL;

		error("%s: %s could not be updated on %s; applied to %s",
		      __func__, update_tres_msg->name, failed_str,
		      changed_str ? changed_str : "no nodes");
		xfree(failed_str);
		xfree(changed_str);
	}
	FREE_NULL_BITMAP(failed_nodes);
	FREE_NULL_BITMAP(changed_nodes);

	if (any_change)
		last_node_update = now;

	_record_absent_gres_note(absent_note, user_msg);

	return error_code;
}

/*
 * restore_node_features - Make node and config (from slurm.conf) fields
 *	consistent for Features, Gres, Weight and SuspendTime
 * IN recover -
 *              0, 1 - use data from config record, built using slurm.conf
 *              2 = use data from node record, built from saved state
 */
extern void restore_node_features(int recover)
{
	int i, node_features_plugin_cnt;
	node_record_t *node_ptr;

	node_features_plugin_cnt = node_features_g_count();
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		char *gres_name = NULL;

		if (node_ptr->weight != node_ptr->config_ptr->weight) {
			error("Node %s Weight(%u) differ from slurm.conf",
			      node_ptr->name, node_ptr->weight);
			if (recover == 2) {
				_update_node_weight(node_ptr->name,
						    node_ptr->weight);
			} else {
				node_ptr->weight = node_ptr->config_ptr->
						   weight;
			}
		}
		if ((recover == 2) && (node_ptr->suspend_time_configured !=
				       node_ptr->config_ptr->suspend_time)) {
			char tmp_buf[12];

			info("Node %s: restoring SuspendTime(%s) from state, overriding slurm.conf",
			     node_ptr->name,
			     _suspend_time_str(
				     node_ptr->suspend_time_configured,
				     tmp_buf, sizeof(tmp_buf)));
			_update_node_suspend_time(
				node_ptr->name,
				node_ptr->suspend_time_configured);
		}
		if (xstrcmp(node_ptr->config_ptr->feature, node_ptr->features)){
			if (node_features_plugin_cnt == 0) {
				error("Node %s Features(%s) differ from slurm.conf",
				      node_ptr->name, node_ptr->features);
			}
			if (recover == 2) {
				update_node_avail_features(node_ptr->name,
							   node_ptr->features,
							   FEATURE_MODE_COMB);
			}
		}

		/*
		 * Rebuild extra_data
		 */
		if (node_ptr->extra && node_ptr->extra[0] &&
		    extra_constraints_enabled()) {
			data_t *data = NULL;
			if (serialize_g_string_to_data(&data, node_ptr->extra,
						       strlen(node_ptr->extra),
						       MIME_TYPE_JSON)) {
				info("Failed to decode extra \"%s\" for node %s",
				     node_ptr->extra, node_ptr->name);
			} else {
				node_ptr->extra_data = data;
			}
		}

		/* node_ptr->gres is set when recover == 2 */
		if (node_ptr->gres)
			gres_name = node_ptr->gres;
		else
			gres_name = node_ptr->config_ptr->gres;

		(void) gres_node_reconfig(
			node_ptr->name,
			gres_name,
			&node_ptr->gres,
			&node_ptr->gres_list,
			node_conf_overrides(node_ptr),
			node_ptr->cores,
			node_ptr->tot_sockets);
		gres_node_state_log(node_ptr->gres_list, node_ptr->name);
	}
	update_node_avail_features(NULL, NULL, FEATURE_MODE_PEND);
}

extern void node_mgr_reval_max_drained_tres(void)
{
	node_record_t *node_ptr;
	time_t now = time(NULL);

	for (int i = 0; (node_ptr = next_node(&i)); i++)
		(void) _enforce_max_drained_tres(node_ptr,
						 slurm_conf.slurm_user_id, now,
						 false);
}

/* Duplicate a configuration record except for the node names & bitmap */
config_record_t *_dup_config(config_record_t *config_ptr)
{
	config_record_t *new_config_ptr;

	new_config_ptr = create_config_record();
	new_config_ptr->magic       = config_ptr->magic;
	new_config_ptr->cpus        = config_ptr->cpus;
	new_config_ptr->cpu_spec_list = xstrdup(config_ptr->cpu_spec_list);
	new_config_ptr->boards      = config_ptr->boards;
	new_config_ptr->tot_sockets     = config_ptr->tot_sockets;
	new_config_ptr->cores       = config_ptr->cores;
	new_config_ptr->core_spec_cnt = config_ptr->core_spec_cnt;
	new_config_ptr->threads     = config_ptr->threads;
	new_config_ptr->real_memory = config_ptr->real_memory;
	new_config_ptr->res_cores_per_gpu = config_ptr->res_cores_per_gpu;
	new_config_ptr->mem_spec_limit = config_ptr->mem_spec_limit;
	new_config_ptr->tmp_disk    = config_ptr->tmp_disk;
	new_config_ptr->weight      = config_ptr->weight;
	new_config_ptr->feature     = xstrdup(config_ptr->feature);
	new_config_ptr->gres        = xstrdup(config_ptr->gres);
	new_config_ptr->topology_str = xstrdup(config_ptr->topology_str);
	new_config_ptr->cpu_bind = config_ptr->cpu_bind;
	new_config_ptr->suspend_time = config_ptr->suspend_time;
	new_config_ptr->parameters = xstrdup(config_ptr->parameters);
	new_config_ptr->tres_weights_str =
		xstrdup(config_ptr->tres_weights_str);
	if (config_ptr->tres_weights) {
		new_config_ptr->tres_weights =
			xcalloc(slurmctld_tres_cnt, sizeof(double));
		memcpy(new_config_ptr->tres_weights, config_ptr->tres_weights,
		       slurmctld_tres_cnt * sizeof(double));
	}
	new_config_ptr->max_drained_tres =
		xstrdup(config_ptr->max_drained_tres);

	_queue_consolidate_config_list();

	return new_config_ptr;
}

/*
 * _update_node_weight - Update weight associated with nodes
 *	build new config list records as needed
 * IN node_names - list of nodes to update
 * IN weight - New weight value
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_weight(char *node_names, uint32_t weight)
{
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	list_itr_t *config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	int rc, config_cnt, tmp_cnt;

	rc = node_name2bitmap(node_names, false, &node_bitmap, NULL);
	if (rc) {
		info("%s: invalid node_name", __func__);
		return rc;
	}

	/* For each config_record with one of these nodes,
	 * update it (if all nodes updated) or split it into
	 * a new entry */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr == first_new)
			break;	/* done with all original records */

		tmp_bitmap = bit_copy(node_bitmap);
		bit_and(tmp_bitmap, config_ptr->node_bitmap);
		config_cnt = bit_set_count(config_ptr->node_bitmap);
		tmp_cnt = bit_set_count(tmp_bitmap);
		if (tmp_cnt == 0) {
			/* no overlap, leave alone */
		} else if (tmp_cnt == config_cnt) {
			/* all nodes changed, update in situ */
			config_ptr->weight = weight;
		} else {
			/* partial update, split config_record */
			new_config_ptr = _dup_config(config_ptr);
			if (first_new == NULL)
				first_new = new_config_ptr;
			/* Change weight for the given nodes */
			new_config_ptr->weight      = weight;
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);
			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */
			bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(
						config_ptr->node_bitmap);
		}
		FREE_NULL_BITMAP(tmp_bitmap);
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(node_bitmap);

	info("%s: nodes %s weight set to: %u", __func__, node_names, weight);
	return SLURM_SUCCESS;
}

/*
 * _suspend_time_str - format a SuspendTime value for logging, spelling out
 *	NO_VAL and INFINITE instead of printing their raw uint32_t value
 */
static char *_suspend_time_str(uint32_t suspend_time, char *buf, size_t len)
{
	if (suspend_time == NO_VAL)
		return "unset";
	if (suspend_time == INFINITE)
		return "INFINITE";
	snprintf(buf, len, "%u", suspend_time);
	return buf;
}

/*
 * _update_node_suspend_time - Update SuspendTime associated with nodes
 *	build new config list records as needed
 * IN node_names - list of nodes to update
 * IN suspend_time - New SuspendTime value
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_suspend_time(char *node_names, uint32_t suspend_time)
{
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	hostlist_t *host_list;
	list_itr_t *config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	node_record_t *node_ptr;
	int rc, config_cnt, tmp_cnt;
	char tmp_buf[12];

	if (!(host_list = nodespec_to_hostlist(node_names, false, NULL)))
		return ESLURM_INVALID_NODE_NAME;
	rc = hostlist2bitmap(host_list, false, &node_bitmap);
	FREE_NULL_HOSTLIST(host_list);
	if (rc) {
		info("%s: invalid node_name", __func__);
		FREE_NULL_BITMAP(node_bitmap);
		return rc;
	}

	/*
	 * For each config_record with one of these nodes, update it
	 * (if all nodes updated) or split it into a new entry.
	 */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr == first_new)
			break; /* done with all original records */

		if (!bit_overlap_any(node_bitmap, config_ptr->node_bitmap))
			continue; /* no overlap, leave alone */

		tmp_bitmap = bit_copy(node_bitmap);
		bit_and(tmp_bitmap, config_ptr->node_bitmap);
		config_cnt = bit_set_count(config_ptr->node_bitmap);
		tmp_cnt = bit_set_count(tmp_bitmap);
		if (tmp_cnt == config_cnt) {
			/* all nodes changed, update in situ */
			config_ptr->suspend_time = suspend_time;
		} else {
			/* partial update, split config_record */
			new_config_ptr = _dup_config(config_ptr);
			if (!first_new)
				first_new = new_config_ptr;
			new_config_ptr->suspend_time = suspend_time;
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);
			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */
			bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes =
				bitmap2node_name(config_ptr->node_bitmap);
		}

		for (int i = 0; (node_ptr = next_node_bitmap(tmp_bitmap, &i));
		     i++)
			node_ptr->suspend_time = suspend_time;

		FREE_NULL_BITMAP(tmp_bitmap);
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(node_bitmap);

	info("%s: nodes %s SuspendTime set to: %s",
	     __func__, node_names,
	     _suspend_time_str(suspend_time, tmp_buf, sizeof(tmp_buf)));

	return SLURM_SUCCESS;
}

static inline void _update_node_features_post(
	char *node_names,
	char **last_features, char *features,
	bitstr_t **last_node_bitmap, bitstr_t **node_bitmap,
	int mode, const char *type)
{

	xassert(last_features);
	xassert(last_node_bitmap);
	xassert(node_bitmap);

	if (mode == FEATURE_MODE_IND) {
		debug2("%s: nodes %s %s features set to: %s",
		       __func__, node_names, type, features);
	} else if (*last_features && *last_node_bitmap &&
		   ((mode == FEATURE_MODE_PEND) ||
		    xstrcmp(features, *last_features))) {
		char *last_node_names = bitmap2node_name(*last_node_bitmap);
		debug2("%s: nodes %s %s features set to: %s",
		       __func__, last_node_names, type, *last_features);
		xfree(last_node_names);
		xfree(*last_features);
		FREE_NULL_BITMAP(*last_node_bitmap);
	}

	if (mode == FEATURE_MODE_COMB) {
		if (!*last_features) {
			/* Start combining records */
			*last_features = xstrdup(features);
			*last_node_bitmap = *node_bitmap;
			*node_bitmap = NULL;
		} else {
			/* Add this node to existing log info */
			bit_or(*last_node_bitmap, *node_bitmap);
		}
	}
}

extern int update_node_active_features(char *node_names, char *active_features,
				       int mode)
{
	static char *last_active_features = NULL;
	static bitstr_t *last_node_bitmap = NULL;
	bitstr_t *node_bitmap = NULL;
	int rc;

	if (mode < FEATURE_MODE_PEND) {
		/* Perform update of node active features */
		rc = node_name2bitmap(node_names, false, &node_bitmap, NULL);
		if (rc) {
			info("%s: invalid node_name (%s)", __func__,
			     node_names);
			return rc;
		}
		node_features_update_list(active_feature_list, active_features,
					  node_bitmap);
	}

	_update_node_features_post(node_names,
				   &last_active_features, active_features,
				   &last_node_bitmap, &node_bitmap,
				   mode, "active");
	FREE_NULL_BITMAP(node_bitmap);

	return SLURM_SUCCESS;
}

extern int update_node_avail_features(char *node_names, char *avail_features,
				      int mode)
{
	static char *last_avail_features = NULL;
	static bitstr_t *last_node_bitmap = NULL;
	hostlist_t *host_list = NULL;
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	list_itr_t *config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	int rc, config_cnt, tmp_cnt;

	if (mode < FEATURE_MODE_PEND) {
		host_list = nodespec_to_hostlist(node_names, false, NULL);
		if (!host_list)
			return ESLURM_INVALID_NODE_NAME;
		rc = hostlist2bitmap(host_list, false, &node_bitmap);
		if (rc) {
			FREE_NULL_BITMAP(node_bitmap);
			FREE_NULL_HOSTLIST(host_list);
			return rc;
		}

		/*
		 * For each config_record with one of these nodes, update it
		 * (if all nodes updated) or split it into a new entry
		 */
		config_iterator = list_iterator_create(config_list);
		while ((config_ptr = list_next(config_iterator))) {
			if (config_ptr == first_new)
				break;	/* done with all original records */

			tmp_bitmap = bit_copy(node_bitmap);
			bit_and(tmp_bitmap, config_ptr->node_bitmap);
			config_cnt = bit_set_count(config_ptr->node_bitmap);
			tmp_cnt = bit_set_count(tmp_bitmap);
			if (tmp_cnt == 0) {
				/* no overlap, leave alone */
			} else if (tmp_cnt == config_cnt) {
				/* all nodes changed, update in situ */
				xfree(config_ptr->feature);
				if (avail_features && avail_features[0]) {
					config_ptr->feature =
						xstrdup(avail_features);
				}
			} else {
				/* partial update, split config_record */
				new_config_ptr = _dup_config(config_ptr);
				if (first_new == NULL)
					first_new = new_config_ptr;
				xfree(new_config_ptr->feature);
				if (avail_features && avail_features[0]) {
					new_config_ptr->feature =
						xstrdup(avail_features);
				}
				new_config_ptr->node_bitmap =
						bit_copy(tmp_bitmap);
				new_config_ptr->nodes =
						bitmap2node_name(tmp_bitmap);
				_update_config_ptr(tmp_bitmap, new_config_ptr);

				/* Update remaining records */
				bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
				xfree(config_ptr->nodes);
				config_ptr->nodes = bitmap2node_name(
						    config_ptr->node_bitmap);
			}
			FREE_NULL_BITMAP(tmp_bitmap);
		}
		list_iterator_destroy(config_iterator);
		if (avail_feature_list) {	/* list not set at startup */
			node_features_update_list(avail_feature_list,
						  avail_features, node_bitmap);
		}
	}

	_update_node_features_post(node_names,
				   &last_avail_features, avail_features,
				   &last_node_bitmap, &node_bitmap,
				   mode, "available");
	FREE_NULL_HOSTLIST(host_list);
	FREE_NULL_BITMAP(node_bitmap);

	return SLURM_SUCCESS;
}

extern char *filter_out_changeable_features(const char *features)
{
	char *conf_features = NULL, *tmp_feat, *tok, *saveptr;

	if (!features)
		return NULL;

	tmp_feat = xstrdup(features);
	for (tok = strtok_r(tmp_feat, ",", &saveptr); tok;
	     tok = strtok_r(NULL, ",", &saveptr)) {
		if (node_features_g_changeable_feature(tok))
			continue;
		xstrfmtcat(conf_features, "%s%s",
			   conf_features ? "," : "", tok);
	}
	xfree(tmp_feat);

	return conf_features;
}

extern void reset_node_active_features(node_record_t *node_ptr)
{
	xassert(node_ptr);

	xfree(node_ptr->features_act);
	node_ptr->features_act =
		filter_out_changeable_features(node_ptr->features);
	update_node_active_features(node_ptr->name, node_ptr->features_act,
				    FEATURE_MODE_IND);
}

extern void reset_node_instance(node_record_t *node_ptr)
{
	xassert(node_ptr);

	xfree(node_ptr->instance_id);
	xfree(node_ptr->instance_type);
}

extern void reset_node_topology(node_record_t *node_ptr)
{
	xassert(node_ptr);

	char *old_topo = NULL;

	if (node_ptr->config_ptr->topology_str)
		old_topo = node_ptr->config_ptr->topology_str;
	else
		old_topo = node_ptr->topology_orig_str;

	if (!node_ptr->config_ptr->topology_str && !node_ptr->topology_str &&
	    !node_ptr->topology_orig_str) {
		/*
		 * No need to reset topo if the topology was never modified to
		 * start with. Topology is only from topology.conf|yaml.
		 */
	} else {
		node_mgr_set_node_topology(node_ptr, old_topo ? old_topo : "");
	}

	if (old_topo == node_ptr->topology_orig_str) {
		xfree(node_ptr->topology_str);
	}
	xfree(node_ptr->topology_orig_str);
}

/*
 * _update_node_gres - Update generic resources associated with nodes
 *	build new config list records as needed
 * IN node_names - list of nodes to update
 * IN gres - New gres value
 * IN auth_uid - uid of the user who requested the update
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_gres(char *node_names, char *gres, uid_t auth_uid)
{
	bitstr_t *changed_node_bitmap = NULL, *node_bitmap = NULL, *tmp_bitmap;
	list_itr_t *config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	node_record_t *node_ptr;
	int rc, rc2, overlap1, overlap2;
	time_t now = time(NULL);

	rc = node_name2bitmap(node_names, false, &node_bitmap, NULL);
	if (rc) {
		info("%s: invalid node_name: %s", __func__, node_names);
		return rc;
	}

	/*
	 * For each config_record with one of these nodes,
	 * update it (if all nodes updated) or split it into a new entry
	 */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr == first_new)
			break;	/* done with all original records */

		overlap1 = bit_overlap(node_bitmap, config_ptr->node_bitmap);
		if (overlap1 == 0)
			continue;  /* No changes to this config_record */

		/* At least some nodes in this config need to change */
		tmp_bitmap = bit_copy(node_bitmap);
		bit_and(tmp_bitmap, config_ptr->node_bitmap);
		for (int i = 0; (node_ptr = next_node_bitmap(tmp_bitmap, &i));
		     i++) {
			rc2 = gres_node_reconfig(
				node_ptr->name,
				gres, &node_ptr->gres,
				&node_ptr->gres_list,
				node_conf_overrides(node_ptr),
				node_ptr->cores,
				node_ptr->tot_sockets);
			if (rc2 != SLURM_SUCCESS) {
				bit_clear(tmp_bitmap, i);
				overlap1--;
				if (rc == SLURM_SUCCESS)
					rc = rc2;
			}
			gres_node_state_log(node_ptr->gres_list,
					    node_ptr->name);

			/* A new count can drop drains the node had. */
			if (IS_NODE_DEGRADED(node_ptr))
				_reconcile_tres_drain(node_ptr, NULL, auth_uid,
						      now);
		}

		overlap2 = bit_set_count(config_ptr->node_bitmap);
		if (overlap1 == 0) {
			/* No nodes actually changed in this configuration */
			FREE_NULL_BITMAP(tmp_bitmap);
		} else if (overlap1 == overlap2) {
			/* All nodes changes in this configuration */
			xfree(config_ptr->gres);
			if (gres && gres[0])
				config_ptr->gres = xstrdup(gres);
			if (changed_node_bitmap) {
				bit_or(changed_node_bitmap, tmp_bitmap);
				FREE_NULL_BITMAP(tmp_bitmap);
			} else {
				changed_node_bitmap = tmp_bitmap;
				tmp_bitmap = NULL;
			}
		} else {
			/*
			 * Some nodes changes in this configuration.
			 * Split config_record in two.
			 */
			new_config_ptr = _dup_config(config_ptr);
			if (!first_new)
				first_new = new_config_ptr;
			xfree(new_config_ptr->gres);
			if (gres && gres[0])
				new_config_ptr->gres = xstrdup(gres);
			new_config_ptr->node_bitmap = tmp_bitmap;
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);
			_update_config_ptr(tmp_bitmap, new_config_ptr);
			if (changed_node_bitmap) {
				bit_or(changed_node_bitmap, tmp_bitmap);
			} else {
				changed_node_bitmap = bit_copy(tmp_bitmap);
			}

			/* Update remaining config_record */
			bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(
						config_ptr->node_bitmap);
			tmp_bitmap = NULL;	/* Nothing left to free */
		}
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(node_bitmap);

	/* Report changes nodes, may be subset of requested nodes */
	if (changed_node_bitmap) {
		char *change_node_str = bitmap2node_name(changed_node_bitmap);
		info("%s: nodes %s gres set to: %s", __func__,
		     change_node_str, gres);
		FREE_NULL_BITMAP(changed_node_bitmap);
		xfree(change_node_str);
	}

	return rc;
}

/* Reset the config pointer for updated jobs */
static void _update_config_ptr(bitstr_t *bitmap, config_record_t *config_ptr)
{
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node_bitmap(bitmap, &i)); i++) {
		node_ptr->config_ptr = config_ptr;
	}
}

static void _drain_node(node_record_t *node_ptr, char *reason,
			uint32_t reason_uid, time_t now)
{
	xassert(node_ptr);

	if (IS_NODE_DRAIN(node_ptr)) {
		/* state already changed, nothing to do */
		return;
	}

	trigger_node_draining(node_ptr);
	node_ptr->node_state |= NODE_STATE_DRAIN;
	bit_clear(avail_node_bitmap, node_ptr->index);
	info("drain_nodes: node %s state set to DRAIN",
	     node_ptr->name);
	if ((node_ptr->reason == NULL) ||
	    (xstrncmp(node_ptr->reason, "Not responding", 14) == 0)) {
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup(reason);
		node_ptr->reason_time = now;
		node_ptr->reason_uid = reason_uid;
	}
	if ((node_ptr->run_job_cnt == 0) && (node_ptr->comp_job_cnt == 0) &&
	    !node_ptr->hold_cnt) {
		/* no jobs, node is drained */
		trigger_node_drained(node_ptr);
		clusteracct_storage_g_node_down(acct_db_conn,
						node_ptr, now, NULL,
						reason_uid);
	}
}

/*
 * drain_nodes - drain one or more nodes,
 *  no-op for nodes already drained or draining
 * IN nodes - nodes to drain
 * IN reason - reason to drain the nodes
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int drain_nodes(char *nodes, char *reason, uint32_t reason_uid)
{
	int error_code = SLURM_SUCCESS;
	node_record_t *node_ptr;
	char  *this_node_name ;
	hostlist_t *host_list;
	time_t now = time(NULL);

	if ((nodes == NULL) || (nodes[0] == '\0')) {
		error ("drain_nodes: invalid node name  %s", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	if ( (host_list = hostlist_create (nodes)) == NULL) {
		error ("hostlist_create error on %s: %m", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		if (!(node_ptr = find_node_record(this_node_name))) {
			error_code = ESLURM_INVALID_NODE_NAME;
			error("drain_nodes: node %s does not exist",
			      this_node_name);
			xfree(this_node_name);
			break;
		}
		xfree(this_node_name);
		_drain_node(node_ptr, reason, reason_uid, now);
	}
	last_node_update = now;

	hostlist_destroy (host_list);

	/*
	 * check all reservations since nodes may have been in a reservation with
	 * floating count of nodes that needs to be updated
	 */
	validate_all_reservations(false, false);

	return error_code;
}
/* Return true if admin request to change node state from old to new is valid */
static bool _valid_node_state_change(uint32_t old, uint32_t new)
{
	uint32_t base_state, node_flags;

	if (old == new)
		return true;

	base_state = old & NODE_STATE_BASE;
	node_flags = old & NODE_STATE_FLAGS;

	switch (new) {
		case NODE_STATE_DOWN:
		case NODE_STATE_DRAIN:
		case NODE_STATE_FAIL:
		case NODE_STATE_NO_RESPOND:
			return true;

		case NODE_STATE_UNDRAIN:
			if (!(node_flags & NODE_STATE_INVALID_REG))
				return true;
			break;

		case NODE_STATE_POWER_DOWN:
		case NODE_STATE_POWER_UP:
		case (NODE_STATE_POWER_DOWN | NODE_STATE_POWER_UP):
		case (NODE_STATE_POWER_UP | NODE_STATE_POWERED_DOWN):
		case (NODE_STATE_POWER_DOWN | NODE_STATE_POWERED_DOWN):
		case (NODE_STATE_POWER_DOWN | NODE_STATE_POWER_DRAIN):
			return true;

		case NODE_RESUME_KEEP_TRES:
		case NODE_RESUME:
			if (node_flags & NODE_STATE_POWERING_DOWN)
				return true;
			if (node_flags & NODE_STATE_INVALID_REG)
				return false;
			/*
			 * A node that is only DEGRADED is otherwise usable;
			 * what is wrong with it is the TRES drained on it, and
			 * RESUME returns those. RESUME_KEEP_TRES by definition
			 * does not, so accepting it here would report a state
			 * change that never happened.
			 */
			if ((new == NODE_RESUME) &&
			    (node_flags & NODE_STATE_DEGRADED))
				return true;
			if ((base_state == NODE_STATE_DOWN)   ||
			    (base_state == NODE_STATE_FUTURE) ||
			    (node_flags & NODE_STATE_DRAIN)   ||
			    (node_flags & NODE_STATE_FAIL)    ||
			    (node_flags & NODE_STATE_REBOOT_REQUESTED))
				return true;
			break;

		case NODE_STATE_REBOOT_CANCEL:
			if (node_flags & NODE_STATE_REBOOT_REQUESTED)
				return true;
			break;

		case NODE_STATE_FUTURE:
			if ((base_state == NODE_STATE_DOWN) ||
			    (base_state == NODE_STATE_IDLE))
				return true;
			break;

		case NODE_STATE_IDLE:
			if (!(node_flags & NODE_STATE_INVALID_REG) &&
			    !(node_flags & NODE_STATE_REBOOT_ISSUED) &&
			    ((base_state == NODE_STATE_DOWN) ||
			     (base_state == NODE_STATE_IDLE)))
				return true;
			break;

		case NODE_STATE_ALLOCATED:
			if (base_state == NODE_STATE_ALLOCATED)
				return true;
			break;

		default:	/* All others invalid */
			break;
	}

	return false;
}

extern int update_node_record_acct_gather_data(
	acct_gather_node_resp_msg_t *msg)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record(msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;

	memcpy(node_ptr->energy, msg->energy, sizeof(acct_gather_energy_t));

	return SLURM_SUCCESS;
}

/* A node's socket/core configuration has changed could be due to KNL NUMA
 * mode change and reboot. Update this node's config record, splitting an
 * existing record if needed. */
static void _split_node_config(node_record_t *node_ptr,
			       slurm_node_registration_status_msg_t *reg_msg)
{
	config_record_t *config_ptr, *new_config_ptr;

	if (!node_ptr)
		return;
	config_ptr = node_ptr->config_ptr;
	if (!config_ptr)
		return;

	if ((bit_set_count(config_ptr->node_bitmap) > 1) &&
	    bit_test(config_ptr->node_bitmap, node_ptr->index)) {
		new_config_ptr = _dup_config(config_ptr);
		bit_clear(config_ptr->node_bitmap, node_ptr->index);
		xfree(config_ptr->nodes);
		config_ptr->nodes = bitmap2node_name(config_ptr->node_bitmap);
		new_config_ptr->node_bitmap = bit_alloc(node_record_count);
		bit_set(new_config_ptr->node_bitmap, node_ptr->index);
		new_config_ptr->nodes = xstrdup(node_ptr->name);
		node_ptr->config_ptr = new_config_ptr;
		config_ptr = new_config_ptr;
	}
	config_ptr->cores = reg_msg->cores;
	config_ptr->tot_sockets = reg_msg->sockets;
}

static void _set_shared_gres_res_cores(gres_node_state_t *gres_ns)
{
	gres_node_state_t *alt_gres_ns;

	alt_gres_ns = gres_ns->alt_gres->gres_data;
	for (int i = 0; i < gres_ns->topo_cnt; i++) {
		if (!gres_ns->topo_res_core_bitmap[i])
			continue;
		for (int j = 0; j < alt_gres_ns->topo_cnt; j++) {
			if (!bit_overlap_any(gres_ns->topo_gres_bitmap[i],
					     alt_gres_ns->topo_gres_bitmap[j]))
				continue;
			FREE_NULL_BITMAP(alt_gres_ns->topo_res_core_bitmap[j]);
			alt_gres_ns->topo_res_core_bitmap[j] =
				bit_copy(gres_ns->topo_res_core_bitmap[i]);
			break;
		}
	}
}

static int _set_gpu_spec(node_record_t *node_ptr, char **reason_down)
{
	static uint32_t gpu_plugin_id = NO_VAL;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;
	uint32_t res_cnt = node_ptr->res_cores_per_gpu;

	xassert(reason_down);

	xfree(node_ptr->gpu_spec);
	FREE_NULL_BITMAP(node_ptr->gpu_spec_bitmap);

	if (!res_cnt)
		return SLURM_SUCCESS;

	if (gpu_plugin_id == NO_VAL)
		gpu_plugin_id = gres_build_id("gpu");

	if (!(gres_state_node = list_find_first(node_ptr->gres_list,
						gres_find_id,
						&gpu_plugin_id))) {
		/* No GPUs but we thought there were */
		xstrfmtcat(*reason_down, "%sRestrictedCoresPerGPU=%u but no gpus on node %s",
			   *reason_down ? ", " : "", res_cnt, node_ptr->name);
		return ESLURM_RES_CORES_PER_GPU_NO;
	}

	gres_ns = gres_state_node->gres_data;
	if (!gres_ns->topo_cnt || !gres_ns->topo_core_bitmap) {
		xstrfmtcat(*reason_down, "%sRestrictedCoresPerGPU=%u but the gpus given don't have any topology on node %s.",
			   *reason_down ? ", " : "", res_cnt, node_ptr->name);
		return ESLURM_RES_CORES_PER_GPU_TOPO;
	}

	if (!gres_ns->topo_res_core_bitmap)
		gres_ns->topo_res_core_bitmap = xcalloc(gres_ns->topo_cnt,
							 sizeof(bitstr_t *));

	node_ptr->gpu_spec_bitmap = bit_alloc(node_ptr->tot_cores);
	for (int i = 0; i < gres_ns->topo_cnt; i++) {
		int cnt = 0;
		uint32_t this_gpu_res_cnt;
		if (!gres_ns->topo_core_bitmap[i])
			continue;
		FREE_NULL_BITMAP(gres_ns->topo_res_core_bitmap[i]);
		gres_ns->topo_res_core_bitmap[i] =
			bit_alloc(node_ptr->tot_cores);
		this_gpu_res_cnt = res_cnt * gres_ns->topo_gres_cnt_avail[i];
		/* info("%d has %s", i, */
		/*      bit_fmt_full(gres_ns->topo_core_bitmap[i])); */
		for (int j = 0; j < node_ptr->tot_cores; j++) {
			/* Skip general spec cores */
			if (node_ptr->node_spec_bitmap &&
			    !bit_test(node_ptr->node_spec_bitmap, j))
				continue;
			/* Only look at the ones set */
			if (!bit_test(gres_ns->topo_core_bitmap[i], j))
				continue;
			/* Skip any already set */
			if (bit_test(node_ptr->gpu_spec_bitmap, j))
				continue;
			/* info("setting %d", j); */
			bit_set(node_ptr->gpu_spec_bitmap, j);
			bit_set(gres_ns->topo_res_core_bitmap[i], j);
			if (++cnt >= this_gpu_res_cnt)
				break;
		}

		if (cnt != this_gpu_res_cnt) {
			FREE_NULL_BITMAP(node_ptr->gpu_spec_bitmap);
			xstrfmtcat(*reason_down, "%sRestrictedCoresPerGPU: We can't restrict %u core(s) per gpu. GPU %s(%d) doesn't have access to that many unique cores (%d).",
				   *reason_down ? ", " : "",
				   res_cnt, gres_ns->topo_type_name[i], i, cnt);
			return ESLURM_RES_CORES_PER_GPU_UNIQUE;
		}
	}

	if (gres_ns->alt_gres)
		_set_shared_gres_res_cores(gres_ns);

	/*
	 * We want the opposite of the possible cores so
	 * we can do a simple & on it when selecting.
	 */
	/* info("set %s", bit_fmt_full(node_ptr->gpu_spec_bitmap)); */
	node_ptr->gpu_spec = bit_fmt_full(node_ptr->gpu_spec_bitmap);
	bit_not(node_ptr->gpu_spec_bitmap);
	/* info("sending back %s", bit_fmt_full(node_ptr->gpu_spec_bitmap)); */

	return SLURM_SUCCESS;
}

/*
 * Bind one CONFIGURING count-only GRES job on the specified node in place.
 * RET false only if this job needed a deferred bind on this node and it
 *     failed; true when the bind succeeded or none was needed.
 */
static bool _bind_job_gres_on_node(job_record_t *job_ptr,
				   node_record_t *node_ptr)
{
	/*
	 * Only CONFIGURING jobs with a deferred GRES bind on this node. The
	 * caller has already checked that the node is a cloud node powering
	 * up. A count-only entry for a GRES with no File topology is count-only
	 * by design, not a deferred bind; needs_bind() skips those.
	 */
	if (!IS_JOB_CONFIGURING(job_ptr) && !IS_JOB_POWER_UP_NODE(job_ptr))
		return true;
	if (!gres_stepmgr_job_needs_bind(job_ptr, node_ptr))
		return true;

	/*
	 * Complete the deferred allocation in place now that the node
	 * registered its device topology, by re-running the real allocator for
	 * this node. The job keeps its nodes and stays CONFIGURING (it is never
	 * sent back to PENDING); once bound it is no longer count-only and
	 * job_config_fini() launches it.
	 */
	if (gres_stepmgr_job_realloc_node(job_ptr, node_ptr) != SLURM_SUCCESS)
		return false;

	/* Refresh the GRES detail so scontrol shows IDX: not CNT:. */
	gres_stepmgr_job_build_details(job_ptr->gres_list_alloc, job_ptr->nodes,
				       &job_ptr->gres_detail_cnt,
				       &job_ptr->gres_detail_str,
				       &job_ptr->gres_used);
	info("Bound %pJ GRES in place on node %s after topology registration",
	     job_ptr, node_ptr->name);
	last_job_update = time(NULL);

	return true;
}

static int _foreach_bind_jobs_on_node_gres_ready(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	foreach_bind_gres_t *bind_gres = arg;

	if (_bind_job_gres_on_node(job_ptr, bind_gres->node_ptr))
		return SLURM_SUCCESS;

	/*
	 * The bind failed, so this job cannot run here. Do not fail it from
	 * inside the traversal: failing a job re-enters job_list, which
	 * list_for_each() holds locked. Collect it and act once the traversal
	 * is done.
	 */
	if (!bind_gres->failed_jobs)
		bind_gres->failed_jobs = list_create(NULL);
	list_append(bind_gres->failed_jobs, job_ptr);

	return SLURM_SUCCESS;
}

extern int node_mgr_bind_jobs_on_gres_ready(node_record_t *node_ptr)
{
	foreach_bind_gres_t bind_gres = { .node_ptr = node_ptr };
	job_record_t *job_ptr;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	/*
	 * Only a cloud node powering up can have a deferred GRES bind: a job is
	 * scheduled count-only only while the node is powered down, and the
	 * bind runs before POWERING_UP is cleared. POWERING_UP also survives a
	 * slurmctld restart. Test this before walking job_list, which is an
	 * O(jobs) scan under the list lock, so a busy node's periodic
	 * re-registration does not pay for it.
	 */
	if (!node_ptr || !IS_NODE_CLOUD(node_ptr) ||
	    !IS_NODE_POWERING_UP(node_ptr) ||
	    (!node_ptr->run_job_cnt && !node_ptr->comp_job_cnt))
		return SLURM_SUCCESS;

	list_for_each(job_list, _foreach_bind_jobs_on_node_gres_ready,
		      &bind_gres);

	if (!bind_gres.failed_jobs)
		return SLURM_SUCCESS;

	/*
	 * The node registered device topology these jobs' GRES can bind to,
	 * but binding them failed. gres_stepmgr_job_realloc_node() has already
	 * dropped their allocation on this node, so they cannot launch here.
	 * Rather than leave them CONFIGURING forever, drain the node so the
	 * operator sees the problem and fail the affected jobs as a resume
	 * failure (see fail_job_on_node()). Jobs on this node that did bind,
	 * that use no GRES, or whose GRES has no File topology are untouched.
	 *
	 * NOTE: A failed re-alloc leaves the job's GRES alloc entry for this
	 * node already cleared, so the fail path below will not double-release
	 * the node's GRES counts.
	 */
	error("Node %s: could not bind GRES for %d allocated job(s); draining node",
	      node_ptr->name, list_count(bind_gres.failed_jobs));
	drain_nodes(node_ptr->name, "GRES could not be bound for allocated job",
		    slurm_conf.slurm_user_id);

	while ((job_ptr = list_pop(bind_gres.failed_jobs))) {
		/*
		 * Failing a hetjob component fails every component, so a
		 * sibling collected here may already have been failed.
		 */
		if (!IS_JOB_CONFIGURING(job_ptr) &&
		    !IS_JOB_POWER_UP_NODE(job_ptr))
			continue;
		error("%pJ: GRES could not be bound on node %s; failing job",
		      job_ptr, node_ptr->name);
		fail_job_on_node(job_ptr, node_ptr, true);
	}
	FREE_NULL_LIST(bind_gres.failed_jobs);

	return SLURM_SUCCESS;
}

/*
 * validate_node_specs - validate the node's specifications as valid,
 *	if not set state to down, in any case update last_response
 * IN slurm_msg - get node registration message it
 * OUT newly_up - set if node newly brought into service
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 */
extern int validate_node_specs(slurm_msg_t *slurm_msg, bool *newly_up)
{
	int error_code;
	config_record_t *config_ptr;
	node_record_t *node_ptr;
	char *reason_down = NULL;
	bool uuid_order_drain = false;
	char *orig_features = NULL, *orig_features_act = NULL;
	uint32_t node_flags;
	time_t now = time(NULL);
	bool orig_node_avail;
	bool was_invalid_reg, was_powering_up = false, was_powered_down = false;
	bool slurmd_restarted;
	time_t orig_last_response = 0;
	bool was_degraded, is_degraded;
	static int node_features_cnt = -1;
	int sockets1, sockets2;	/* total sockets on node */
	int cores1, cores2;	/* total cores on node */
	int threads1, threads2;	/* total threads on node */
	static bool loaded = false;
	static double conf_node_reg_mem_percent = -1;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	slurm_node_registration_status_msg_t *reg_msg = slurm_msg->data;

	node_ptr = find_node_record(reg_msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;

	debug3("%s: validating nodes %s in state: %s",
	       __func__, reg_msg->node_name,
	       node_state_string(node_ptr->node_state));

	if (!loaded) {
		char *tmp_ptr;
		loaded = true;
		if ((tmp_ptr = conf_get_opt_str(slurm_conf.slurmctld_params,
						"node_reg_mem_percent="))) {
			if (s_p_handle_double(&conf_node_reg_mem_percent,
					      "node_reg_mem_percent",
					      tmp_ptr))
				conf_node_reg_mem_percent = -1;
			xfree(tmp_ptr);
		}
	}

	orig_node_avail = bit_test(avail_node_bitmap, node_ptr->index);

	config_ptr = node_ptr->config_ptr;
	error_code = SLURM_SUCCESS;

	node_ptr->protocol_version = slurm_msg->protocol_version;
	xfree(node_ptr->version);
	node_ptr->version = reg_msg->version;
	reg_msg->version = NULL;

	if (waiting_for_node_boot(node_ptr) ||
	    waiting_for_node_power_down(node_ptr))
		return SLURM_SUCCESS;
	bit_clear(booting_node_bitmap, node_ptr->index);

	if (node_features_cnt == -1)
		node_features_cnt = node_features_g_count();

	if (reg_msg->features_avail || reg_msg->features_active) {
		orig_features = xstrdup(node_ptr->features);
		if (node_ptr->features_act)
			orig_features_act = xstrdup(node_ptr->features_act);
		else
			orig_features_act = xstrdup(node_ptr->features);
	}
	if (reg_msg->features_avail) {
		if (reg_msg->features_active && !node_ptr->features_act) {
			node_ptr->features_act = node_ptr->features;
			node_ptr->features = NULL;
		} else {
			xfree(node_ptr->features);
		}
		node_ptr->features = node_features_g_node_xlate(
					reg_msg->features_avail,
					orig_features, orig_features,
					node_ptr->index);
		/* Only update if there was a change */
		if (xstrcmp(node_ptr->features, orig_features))
			(void) update_node_avail_features(node_ptr->name,
							  node_ptr->features,
							  FEATURE_MODE_IND);
	}
	if (reg_msg->features_active) {
		if (!_valid_reported_active_features(reg_msg->features_active,
						     node_ptr->features_act)) {
			char *active_changeable_features =
				_node_changeable_features(
					node_ptr->features_act);
			debug("Node %s reported active features (%s) are not a super set of node's active changeable features (%s)",
			      reg_msg->node_name,
			      reg_msg->features_active,
			      active_changeable_features);
			error_code  = EINVAL;
			xstrfmtcat(reason_down, "%sReported active features (%s) are not a superset of currently active changeable features (%s)",
				   reason_down ? ", " : "",
				   reg_msg->features_active,
				   active_changeable_features);
			xfree(active_changeable_features);
		} else {
			char *tmp_feature;
			tmp_feature =
				node_features_g_node_xlate(
					reg_msg->features_active,
					orig_features_act, orig_features,
					node_ptr->index);
			xfree(node_ptr->features_act);
			node_ptr->features_act = tmp_feature;
			(void) update_node_active_features(
				node_ptr->name, node_ptr->features_act,
				FEATURE_MODE_IND);
		}
	}
	xfree(orig_features);
	xfree(orig_features_act);

	/*
	 * Take the Parameters this slurmd just reported before validating the
	 * rest of its configuration so that checks below see the current
	 * values rather than those from the previous registration.
	 */
	xfree(node_ptr->parameters);
	node_ptr->parameters = reg_msg->parameters;
	reg_msg->parameters = NULL;

	sockets1 = reg_msg->sockets;
	cores1   = sockets1 * reg_msg->cores;
	threads1 = cores1   * reg_msg->threads;
	if (gres_node_config_unpack(reg_msg->gres_info,
				    node_ptr->name) != SLURM_SUCCESS) {
		error_code = SLURM_ERROR;
		xstrcat(reason_down, "Could not unpack gres data");
	} else if (gres_node_config_validate(node_ptr, reg_msg->threads,
					     reg_msg->cores, reg_msg->sockets,
					     node_conf_overrides(node_ptr),
					     &reason_down, &uuid_order_drain) !=
		   SLURM_SUCCESS) {
		error_code = EINVAL;
		/* reason_down set in function above */
	}
	was_degraded = IS_NODE_DEGRADED(node_ptr);
	is_degraded = node_conf_set_degraded_flag(node_ptr);
	gres_node_state_log(node_ptr->gres_list, node_ptr->name);

	if (node_ptr->res_cores_per_gpu) {
		/*
		 * We need to make gpu_spec_bitmap now that we know the cores
		 * used per gres.
		 */
		if (_set_gpu_spec(node_ptr, &reason_down))
			error_code = EINVAL;
		/* reason_down set in function above */
	} else {
		FREE_NULL_BITMAP(node_ptr->gpu_spec_bitmap);
	}

	/*
	 * Bind count-only GRES jobs before later validation failures or node
	 * reboot handling can kill CONFIGURING jobs (e.g. after slurmctld
	 * restart while slurmd is already running with ResumeProgram gres.conf
	 * loaded). This must also run before POWERING_UP is cleared below: no
	 * launch path checks that a job is bound, so it is that later clear
	 * that keeps an unbound job from launching.
	 *
	 * The bind re-runs _job_alloc() directly, not cons_tres selection, so
	 * it honors core affinity but not --gpus-per-socket, enforce-binding
	 * or RestrictedCoresPerGPU.
	 */
	if (error_code == SLURM_SUCCESS)
		(void) node_mgr_bind_jobs_on_gres_ready(node_ptr);

	if (!node_conf_overrides(node_ptr)) {
		/* sockets1, cores1, and threads1 are set above */
		sockets2 = config_ptr->tot_sockets;
		cores2   = sockets2 * config_ptr->cores;
		threads2 = cores2   * config_ptr->threads;

		if (threads1 < threads2) {
			debug("Node %s has low socket*core*thread count "
			      "(%d < %d)",
			      reg_msg->node_name, threads1, threads2);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low socket*core*thread count");
		}

		if (reg_msg->cpus < config_ptr->cpus) {
			debug("Node %s has low cpu count (%u < %u)",
			      reg_msg->node_name, reg_msg->cpus,
			      config_ptr->cpus);
			error_code  = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low CPUs");
		}

		if ((error_code == SLURM_SUCCESS) && running_cons_tres() &&
		    (node_features_cnt > 0) &&
		    (reg_msg->sockets != config_ptr->tot_sockets) &&
		    (reg_msg->cores   != config_ptr->cores) &&
		    ((reg_msg->sockets * reg_msg->cores) ==
		     (config_ptr->tot_sockets * config_ptr->cores))) {
			_split_node_config(node_ptr, reg_msg);
		}
	}
	if (reg_msg->boards > reg_msg->sockets) {
		error("Node %s has more boards than sockets (%u > %u), setting board count to 1",
		      reg_msg->node_name, reg_msg->boards, reg_msg->sockets);
		reg_msg->boards = 1;
	}

	if (!node_conf_overrides(node_ptr)) {
		double node_reg_mem_percent;
		if (conf_node_reg_mem_percent == -1) {
			if (IS_NODE_CLOUD(node_ptr))
				node_reg_mem_percent =
					DEFAULT_CLOUD_REG_MEM_PERCENT;
			else
				node_reg_mem_percent =
					DEFAULT_NODE_REG_MEM_PERCENT;
		} else
			node_reg_mem_percent = conf_node_reg_mem_percent;

		if (config_ptr->real_memory &&
		    ((((double)reg_msg->real_memory /
		       config_ptr->real_memory) * 100) <
		     node_reg_mem_percent)) {
			debug("Node %s has low real_memory size (%"PRIu64" / %"PRIu64") < %.2f%%",
			      reg_msg->node_name, reg_msg->real_memory,
			      config_ptr->real_memory, node_reg_mem_percent);
			error_code  = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrfmtcat(reason_down, "Low RealMemory (reported:%"PRIu64" < %.2f%% of configured:%"PRIu64")",
				   reg_msg->real_memory, node_reg_mem_percent,
				   config_ptr->real_memory);
		}

		if (reg_msg->tmp_disk < config_ptr->tmp_disk) {
			debug("Node %s has low tmp_disk size (%u < %u)",
			      reg_msg->node_name, reg_msg->tmp_disk,
			      config_ptr->tmp_disk);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low TmpDisk");
		}
	}

	if (reg_msg->cpu_spec_list) {
		bitstr_t *node_spec_bitmap_old = node_ptr->node_spec_bitmap;
		char *cpu_spec_list_old = node_ptr->cpu_spec_list;

		node_ptr->node_spec_bitmap = NULL;
		node_ptr->cpu_spec_list = reg_msg->cpu_spec_list;
		reg_msg->cpu_spec_list = NULL;	/* Nothing left to free */

		if (build_node_spec_bitmap(node_ptr) != SLURM_SUCCESS)
			error_code = EINVAL;
		else if (!(slurm_conf.task_plugin_param &
			   SLURMD_SPEC_OVERRIDE) &&
			 (!node_spec_bitmap_old ||
			  !bit_equal(node_spec_bitmap_old,
				     node_ptr->node_spec_bitmap))) {
			debug("Node %s has different spec CPUs than expected (%s, %s)",
			      reg_msg->node_name, cpu_spec_list_old,
			      node_ptr->cpu_spec_list);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "CoreSpec differ");
		}

		/* Regenerate core spec count and effective cpus */
		if (node_ptr->cpu_spec_list &&
		    (slurm_conf.task_plugin_param & SLURMD_SPEC_OVERRIDE)) {
			if (node_ptr->cpu_spec_list) {
				build_node_spec_bitmap(node_ptr);
				node_conf_convert_cpu_spec_list(node_ptr);
			} else if (node_ptr->core_spec_cnt) {
				node_conf_select_spec_cores(node_ptr);
			}
			node_ptr->cpus_efctv =
				node_ptr->cpus -
				(node_ptr->core_spec_cnt * node_ptr->tpc);
		}

		xfree(cpu_spec_list_old);
		FREE_NULL_BITMAP(node_spec_bitmap_old);
	}

	if ((slurm_conf.task_plugin_param & SLURMD_SPEC_OVERRIDE) &&
	    reg_msg->mem_spec_limit) {
		node_ptr->mem_spec_limit = reg_msg->mem_spec_limit;
	}

	xfree(node_ptr->arch);
	node_ptr->arch = reg_msg->arch;
	reg_msg->arch = NULL;	/* Nothing left to free */

	xfree(node_ptr->os);
	node_ptr->os = reg_msg->os;
	reg_msg->os = NULL;	/* Nothing left to free */

	if (node_ptr->cpu_load != reg_msg->cpu_load) {
		node_ptr->cpu_load = reg_msg->cpu_load;
		node_ptr->cpu_load_time = now;
		last_node_update = now;
	}
	if (node_ptr->free_mem != reg_msg->free_mem) {
		node_ptr->free_mem = reg_msg->free_mem;
		node_ptr->free_mem_time = now;
		last_node_update = now;
	}

	orig_last_response = node_ptr->last_response;
	slurmd_restarted =
		(node_ptr->slurmd_start_time != reg_msg->slurmd_start_time);
	node_ptr->slurmd_start_time = reg_msg->slurmd_start_time;

	if (IS_NODE_NO_RESPOND(node_ptr) ||
	    IS_NODE_POWERING_UP(node_ptr) ||
	    IS_NODE_POWERING_DOWN(node_ptr) ||
	    IS_NODE_POWERED_DOWN(node_ptr)) {
		was_powered_down = IS_NODE_POWERED_DOWN(node_ptr);

		info("Node %s now responding", node_ptr->name);

		/*
		 * Set last_busy in case that the node came up out of band or
		 * came up after ResumeTimeout so that it can be suspended at a
		 * later point.
		 */
		if (IS_NODE_POWERING_UP(node_ptr) ||
		    IS_NODE_POWERED_DOWN(node_ptr))
			node_ptr->last_busy = now;

		/*
		 * Set last_response if it's expected. Otherwise let it get
		 * marked at "unexpectedly rebooted". Not checked with
		 * IS_NODE_POWERED_DOWN() above to allow ReturnToService !=2
		 * catch nodes [re]booting unexpectedly.
		 */
		if (IS_NODE_POWERING_UP(node_ptr)) {
			node_ptr->last_response = now;
			was_powering_up = true;

			if (IS_NODE_REBOOT_ISSUED(node_ptr) &&
			    (slurm_conf.health_check_node_state &
			     HEALTH_CHECK_REBOOT_ONLY))
				run_health_check_individual(node_ptr);
			node_ptr->node_state &= (~NODE_STATE_REBOOT_ISSUED);
		}

		node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
		node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
		node_ptr->node_state &= (~NODE_STATE_POWERED_DOWN);
		node_ptr->node_state &= (~NODE_STATE_POWERING_DOWN);
		if (!is_node_in_maint_reservation(node_ptr->index))
			node_ptr->node_state &= (~NODE_STATE_MAINT);

		bit_clear(power_down_node_bitmap, node_ptr->index);
		bit_set(power_up_node_bitmap, node_ptr->index);

		last_node_update = now;

		if (was_powered_down)
			clusteracct_storage_g_node_up(acct_db_conn, node_ptr,
						      now);
	}

	/*
	 * Apply the slurmd-supplied extra, instance_id, instance_type and
	 * topology only when the node booted since it last responded and
	 * slurmd restarted since its last registration. That is true on the
	 * first registration and after a real reboot or power up. A slurmd
	 * restart does not boot the node, and a rebootless feature change does
	 * not restart slurmd, so values set with scontrol update stay.
	 * last_response is state-saved, so this also holds across a slurmctld
	 * restart. Compare against last_response from before this
	 * registration, because it is set to now above for a node leaving
	 * power save. Compare slurmd start times only with each other, because
	 * they come from the node's clock.
	 */
	if ((node_ptr->boot_time > orig_last_response) && slurmd_restarted) {
		bool update_db = false;

		if (reg_msg->extra) {
			data_t *data = NULL;

			if (extra_constraints_enabled() && reg_msg->extra[0] &&
			    serialize_g_string_to_data(&data, reg_msg->extra,
						       strlen(reg_msg->extra),
						       MIME_TYPE_JSON)) {
				info("Failed to decode extra \"%s\" for node %s",
				     reg_msg->extra, node_ptr->name);
			}
			FREE_NULL_DATA(node_ptr->extra_data);
			node_ptr->extra_data = data;

			/*
			 * Always set the extra field from the registration
			 * message, even if decoding failed.
			 */
			xfree(node_ptr->extra);
			if (reg_msg->extra[0]) {
				node_ptr->extra = xstrdup(reg_msg->extra);
				/*
				 * Skip db updates for extra field changes,
				 * otherwise we'll overwhelm it with event
				 * records if someone is updating these
				 * constantly.
				 */
				// update_db = true;
			}
		}

		if (reg_msg->instance_id) {
			xfree(node_ptr->instance_id);
			if (reg_msg->instance_id[0]) {
				node_ptr->instance_id =
					xstrdup(reg_msg->instance_id);
				update_db = true;
			}
		}

		if (reg_msg->instance_type) {
			xfree(node_ptr->instance_type);
			if (reg_msg->instance_type[0]) {
				node_ptr->instance_type =
					xstrdup(reg_msg->instance_type);
				update_db = true;
			}
		}

		if (update_db)
			clusteracct_storage_g_node_update(acct_db_conn,
							  node_ptr);

		if (reg_msg->dynamic_conf &&
		    xstrcasestr(reg_msg->dynamic_conf, "topology=")) {
			int rc = SLURM_SUCCESS;
			char *tmp_conf = NULL;
			slurm_conf_node_t *conf_node = NULL;
			s_p_hashtbl_t *node_hashtbl = NULL;

			if (!xstrncasecmp(reg_msg->dynamic_conf,
					  "NodeName=", 9))
				tmp_conf = xstrdup(reg_msg->dynamic_conf);
			else
				tmp_conf =
					xstrdup_printf("NodeName=%s %s",
						       node_ptr->name,
						       reg_msg->dynamic_conf);

			if (!(conf_node = slurm_conf_parse_nodeline(
				      tmp_conf, &node_hashtbl))) {
				error("Failed to parse dynamic nodeline '%s'",
				      reg_msg->dynamic_conf);
				error_code = EINVAL;
				xstrfmtcat(reason_down,
					   "%sFailed to parse dynamic_conf nodeline",
					   reason_down ? ", " : "");
			} else if (conf_node->topology_str &&
				   xstrcmp(conf_node->topology_str,
					   node_ptr->topology_str) &&
				   ((rc = node_mgr_set_node_topology(
					     node_ptr,
					     conf_node->topology_str)))) {
				error_code = rc;
				xstrfmtcat(reason_down,
					   "%sFailed to set topology '%s'",
					   reason_down ? ", " : "",
					   conf_node->topology_str);
			}
			s_p_hashtbl_destroy(node_hashtbl);
			xfree(tmp_conf);
		}
	}

	was_invalid_reg = IS_NODE_INVALID_REG(node_ptr);
	node_ptr->node_state &= ~NODE_STATE_INVALID_REG;
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if (error_code) {
		node_ptr->node_state |= NODE_STATE_INVALID_REG;
		if (!was_invalid_reg) {
			error("Setting node %s state to INVAL with reason:%s",
			       reg_msg->node_name, reason_down);

			if (was_powering_up || was_powered_down)
				kill_running_job_by_node_ptr(node_ptr);
		}

		if (!IS_NODE_DOWN(node_ptr)
			&& !IS_NODE_DRAIN(node_ptr)
			&& ! IS_NODE_FAIL(node_ptr)) {
			drain_nodes(reg_msg->node_name, reason_down,
			            slurm_conf.slurm_user_id);
		} else if (xstrcmp(node_ptr->reason, reason_down)) {
			if (was_invalid_reg) {
				error("Setting node %s state to INVAL with reason:%s",
				       reg_msg->node_name, reason_down);
			}
			xfree(node_ptr->reason);
			set_node_reason(node_ptr, reason_down, now);
		}
		last_node_update = time (NULL);
	} else if (reg_msg->status == ESLURMD_PROLOG_FAILED
		   || reg_msg->status == ESLURMD_SETUP_ENVIRONMENT_ERROR) {
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			char *reason;
			error("%s: Prolog or job env setup failure on node %s, "
			      "draining the node",
			      __func__, reg_msg->node_name);
			if (reg_msg->status == ESLURMD_PROLOG_FAILED)
				reason = "Prolog error";
			else
				reason = "Job env setup error";
			drain_nodes(reg_msg->node_name, reason,
			            slurm_conf.slurm_user_id);
			last_node_update = time (NULL);
		}
	} else {
		if (IS_NODE_UNKNOWN(node_ptr) || IS_NODE_FUTURE(node_ptr)) {
			bool was_future = IS_NODE_FUTURE(node_ptr);
			debug("validate_node_specs: node %s registered with "
			      "%u jobs",
			      reg_msg->node_name,reg_msg->job_count);
			if (IS_NODE_FUTURE(node_ptr)) {
				if (IS_NODE_MAINT(node_ptr) &&
				    !is_node_in_maint_reservation(
					    node_ptr->index))
					node_flags &= (~NODE_STATE_MAINT);
				node_flags &= (~NODE_STATE_REBOOT_REQUESTED);
				node_flags &= (~NODE_STATE_REBOOT_ISSUED);
			}
			if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
					node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_busy = now;
			}
			last_node_update = now;

			/* don't send this on a slurmctld unless needed */
			if (was_future || /* always send FUTURE checkins */
			    (slurmctld_init_db &&
			     !IS_NODE_DRAIN(node_ptr) &&
			     !IS_NODE_FAIL(node_ptr))) {
				/* reason information is handled in
				   clusteracct_storage_g_node_up()
				*/
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (IS_NODE_DOWN(node_ptr) &&
			   ((slurm_conf.ret2service == RETURN_TO_SERVICE_ALL) ||
			    IS_NODE_REBOOT_ISSUED(node_ptr) ||
			    ((slurm_conf.ret2service ==
			      RETURN_TO_SERVICE_NON_RESP) &&
			     !xstrcmp(node_ptr->reason, "Not responding") &&
			     (node_ptr->boot_time <
			      node_ptr->last_response)))) {
			if (IS_NODE_REBOOT_ISSUED(node_ptr) &&
			    (slurm_conf.health_check_node_state &
			     HEALTH_CHECK_REBOOT_ONLY))
				run_health_check_individual(node_ptr);
			node_flags &= (~NODE_STATE_REBOOT_ISSUED);
			if (node_ptr->next_state != NO_VAL)
				node_flags &= (~NODE_STATE_DRAIN);

			if ((node_ptr->next_state & NODE_STATE_BASE) ==
			    NODE_STATE_DOWN) {
				node_ptr->node_state = NODE_STATE_DOWN |
						       node_flags;
				set_node_reason(node_ptr, "reboot complete",
						now);
			} else if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
						       node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
						       node_flags;
				node_ptr->last_busy = now;
			}
			node_ptr->next_state = NO_VAL;
			node_ptr->resume_after = 0;
			bit_clear(rs_node_bitmap, node_ptr->index);
			bit_clear(asap_node_bitmap, node_ptr->index);

			info("node %s returned to service",
			     reg_msg->node_name);
			trigger_node_up(node_ptr);
			last_node_update = now;
			if (!IS_NODE_DRAIN(node_ptr)
			    && !IS_NODE_DOWN(node_ptr)
			    && !IS_NODE_FAIL(node_ptr)) {
				/* reason information is handled in
				 * clusteracct_storage_g_node_up() */
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (!IS_NODE_DRAINED(node_ptr) &&
			   !IS_NODE_MAINT(node_ptr) &&
			   node_ptr->last_response &&
			   (node_ptr->boot_time > node_ptr->last_response) &&
			   (slurm_conf.ret2service != RETURN_TO_SERVICE_ALL)) {
			if (!node_ptr->reason ||
			    (node_ptr->reason &&
			     (!xstrcmp(node_ptr->reason, "Not responding") ||
			      IS_NODE_REBOOT_REQUESTED(node_ptr)))) {
				xfree(node_ptr->reason);
				node_ptr->reason_time = now;
				node_ptr->reason_uid = slurm_conf.slurm_user_id;
				node_ptr->reason = xstrdup(
					"Node unexpectedly rebooted");
			}
			/* If a reboot was requested, cancel it. */
			if (IS_NODE_REBOOT_REQUESTED(node_ptr)) {
				node_ptr->node_state &=
					(~NODE_STATE_REBOOT_REQUESTED);
				if ((node_ptr->next_state != NO_VAL) &&
				    (node_ptr->next_state & NODE_STATE_UNDRAIN))
					/*
					 * Not using _undo_reboot_asap() so that
					 * the reason is preserved.
					 */
					node_ptr->node_state &=
						(~NODE_STATE_DRAIN);
				bit_clear(rs_node_bitmap, node_ptr->index);
				bit_clear(asap_node_bitmap, node_ptr->index);
			}
			info("%s: Node %s unexpectedly rebooted boot_time=%ld last response=%ld",
			     __func__, reg_msg->node_name, node_ptr->boot_time,
			     node_ptr->last_response);
			_make_node_down(node_ptr, now);
			kill_running_job_by_node_ptr(node_ptr);
			last_node_update = now;
			reg_msg->job_count = 0;
		} else if (IS_NODE_ALLOCATED(node_ptr) &&
			   (reg_msg->job_count == 0)) { /* job vanished */
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			node_ptr->last_busy = now;
			last_node_update = now;
		} else if (IS_NODE_COMPLETING(node_ptr) &&
			   (reg_msg->job_count == 0) && /* job already done */
			   !node_ptr->hold_cnt) {
			node_ptr->node_state &= (~NODE_STATE_COMPLETING);
			last_node_update = now;
			bit_clear(cg_node_bitmap, node_ptr->index);
		} else if (IS_NODE_IDLE(node_ptr) &&
			   (reg_msg->job_count != 0)) {
			if (node_ptr->run_job_cnt != 0) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
						       node_flags;
				error("Invalid state for node %s, was IDLE "
			      	      "with %u running jobs",
			      	      node_ptr->name, reg_msg->job_count);
			}
			/*
			 * there must be completing job(s) on this node since
			 * reg_msg->job_count was set (run_job_cnt +
			 * comp_job_cnt) in validate_jobs_on_node()
			 */
			if (node_ptr->comp_job_cnt != 0) {
				node_ptr->node_state |= NODE_STATE_COMPLETING;
				bit_set(cg_node_bitmap, node_ptr->index);
			}
			last_node_update = now;
		}
		if (IS_NODE_IDLE(node_ptr)) {
			node_ptr->owner = NO_VAL;
			xfree(node_ptr->mcs_label);
		}

		/*
		 * Keep the saved UUID order when the report cannot be aligned
		 * with it. A later fitting report clears only this automatic
		 * drain; an administrator's drain is left alone.
		 */
		if (uuid_order_drain) {
			drain_nodes(node_ptr->name, GRES_UUID_ORDER_DRAIN,
				    slurm_conf.slurm_user_id);
		} else if (IS_NODE_DRAIN(node_ptr) && node_ptr->reason &&
			   !xstrcmp(node_ptr->reason, GRES_UUID_ORDER_DRAIN) &&
			   (node_ptr->reason_uid == slurm_conf.slurm_user_id)) {
			/* Close the event the drain opened, as UNDRAIN does */
			if (IS_NODE_IDLE(node_ptr))
				clusteracct_storage_g_node_up(acct_db_conn,
							      node_ptr, now);
			node_ptr->node_state &= ~NODE_STATE_DRAIN;
			xfree(node_ptr->reason);
			node_ptr->reason_time = 0;
			node_ptr->reason_uid = NO_VAL;
			info("Node %s GPU UUID order matches, clearing drain",
			     node_ptr->name);
		}

		_sync_bitmaps(node_ptr, reg_msg->job_count);
	}

	/*
	 * Enforce MaxDrainedTres only after the node's final state is set:
	 * _enforce_max_drained_tres() skips DOWN nodes, so a returning node
	 * over the limit would otherwise come back into service.
	 * Drain the node before the accounting update so a node over the limit
	 * records a whole-node drain rather than a TRES interval that the
	 * drain then has to replace.
	 */
	if (error_code == SLURM_SUCCESS)
		(void) _enforce_max_drained_tres(node_ptr,
						 slurm_conf.slurm_user_id, now,
						 false);

	/*
	 * Record drained/undrained TRES in accounting now that the node's final
	 * state is set. A preceding node_up (return to service) closes every
	 * open event of the node, the drained-TRES interval included; this
	 * opens that interval again against what the registration reported.
	 * Only act when the node is or was degraded to avoid spurious events on
	 * the frequent registration path.
	 */
	if (was_degraded || is_degraded)
		node_mgr_update_degraded_event(node_ptr, NULL,
					       slurm_conf.slurm_user_id, now);

	xfree(reason_down);
	if (reg_msg->energy)
		memcpy(node_ptr->energy, reg_msg->energy,
		       sizeof(acct_gather_energy_t));

	node_ptr->last_response = now;
	node_ptr->boot_req_time = (time_t) 0;
	node_ptr->power_save_req_time = (time_t) 0;

	*newly_up = (!orig_node_avail &&
		     bit_test(avail_node_bitmap, node_ptr->index));

	if (IS_NODE_CLOUD(node_ptr) ||
	    IS_NODE_DYNAMIC_FUTURE(node_ptr) ||
	    IS_NODE_DYNAMIC_NORM(node_ptr)) {
		/* Get IP of slurmd */
		char *comm_name = _get_msg_hostname(slurm_msg);

		set_node_comm_name(node_ptr, comm_name, reg_msg->hostname);

		xfree(comm_name);
	}

	if (was_powering_up || was_powered_down)
		log_flag(POWER, "Node %s/%s/%s powered up with instance_id=%s, instance_type=%s",
			 node_ptr->name, node_ptr->node_hostname,
			 node_ptr->comm_name, reg_msg->instance_id,
			 reg_msg->instance_type);

	/*
	 * Now that the node has finished powering up, launch any configuring
	 * job waiting on it whose nodes have all become ready. This avoids
	 * waiting up to PERIODIC_TIMEOUT for the next job_time_limit() poll in
	 * the slurmctld background thread. Gated on
	 * SlurmctldParameters=fast_power_up_launch.
	 */
	if (!error_code && was_powering_up)
		launch_ready_jobs_on_node(node_ptr);

	return error_code;
}

/* Sync idle, share, and avail_node_bitmaps for a given node */
static void _sync_bitmaps(node_record_t *node_ptr, int job_count)
{
	if (job_count == 0) {
		if (!node_ptr->hold_cnt)
			bit_set(idle_node_bitmap, node_ptr->index);
		bit_set (share_node_bitmap, node_ptr->index);
	}
	if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr) ||
	    IS_NODE_FAIL(node_ptr) || IS_NODE_NO_RESPOND(node_ptr))
		bit_clear (avail_node_bitmap, node_ptr->index);
	else
		make_node_avail(node_ptr);
	if (IS_NODE_DOWN(node_ptr))
		bit_clear (up_node_bitmap, node_ptr->index);
	else
		bit_set   (up_node_bitmap, node_ptr->index);
}

static void _node_did_resp(node_record_t *node_ptr)
{
	uint32_t node_flags;
	time_t now = time(NULL);

	if (waiting_for_node_boot(node_ptr) ||
	    waiting_for_node_power_down(node_ptr) ||
	    IS_NODE_FUTURE(node_ptr))
		return;
	node_ptr->last_response = now;
	if (IS_NODE_NO_RESPOND(node_ptr) || IS_NODE_POWERING_UP(node_ptr)) {
		info("Node %s now responding", node_ptr->name);
		node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
		node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
		if (!is_node_in_maint_reservation(node_ptr->index))
			node_ptr->node_state &= (~NODE_STATE_MAINT);
		last_node_update = now;
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_UNKNOWN(node_ptr)) {
		node_ptr->last_busy = now;
		if (node_ptr->run_job_cnt) {
			node_ptr->node_state = NODE_STATE_ALLOCATED |
					       node_flags;
		} else
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		last_node_update = now;
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			clusteracct_storage_g_node_up(acct_db_conn,
						      node_ptr, now);
			if (IS_NODE_DEGRADED(node_ptr))
				node_mgr_update_degraded_event(
					node_ptr, NULL,
					slurm_conf.slurm_user_id, now);
		}
	}
	if (IS_NODE_DOWN(node_ptr) && !IS_NODE_INVALID_REG(node_ptr) &&
	    ((slurm_conf.ret2service == RETURN_TO_SERVICE_ALL) ||
	     (node_ptr->boot_req_time != 0) ||
	     ((slurm_conf.ret2service == RETURN_TO_SERVICE_NON_RESP) &&
	      !xstrcmp(node_ptr->reason, "Not responding")))) {
		node_ptr->last_busy = now;
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		node_ptr->resume_after = 0;
		info("node_did_resp: node %s returned to service",
		     node_ptr->name);
		trigger_node_up(node_ptr);
		last_node_update = now;
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			/* reason information is handled in
			   clusteracct_storage_g_node_up()
			*/
			clusteracct_storage_g_node_up(acct_db_conn,
						      node_ptr, now);
			if (IS_NODE_DEGRADED(node_ptr))
				node_mgr_update_degraded_event(
					node_ptr, NULL,
					slurm_conf.slurm_user_id, now);
		}
	}
	if (IS_NODE_IDLE(node_ptr) && !IS_NODE_COMPLETING(node_ptr)) {
		bit_set (idle_node_bitmap, node_ptr->index);
		bit_set (share_node_bitmap, node_ptr->index);
	}
	if (IS_NODE_DOWN(node_ptr) ||
	    IS_NODE_DRAIN(node_ptr) ||
	    IS_NODE_FAIL(node_ptr) ||
	    (IS_NODE_POWER_DOWN(node_ptr) && !IS_NODE_ALLOCATED(node_ptr))) {
		bit_clear (avail_node_bitmap, node_ptr->index);
	} else
		bit_set   (avail_node_bitmap, node_ptr->index);
	if (IS_NODE_DOWN(node_ptr))
		bit_clear (up_node_bitmap, node_ptr->index);
	else
		bit_set   (up_node_bitmap, node_ptr->index);
}

/*
 * node_did_resp - record that the specified node is responding
 * IN name - name of the node
 */
void node_did_resp (char *name)
{
	node_record_t *node_ptr;
	node_ptr = find_node_record (name);

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	if (node_ptr == NULL) {
		error ("node_did_resp unable to find node %s", name);
		return;
	}
	_node_did_resp(node_ptr);
	debug2("node_did_resp %s",name);
}

/*
 * node_not_resp - record that the specified node is not responding
 * IN name - name of the node
 * IN msg_time - time message was sent
 */
void node_not_resp (char *name, time_t msg_time, slurm_msg_type_t resp_type)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);

	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	/* If the slurmd on the node responded with something we don't
	 * want to ever set the node down, so mark that the node
	 * responded, but for whatever reason there was a
	 * communication error.  This makes it so we don't mark the
	 * node down if the slurmd really is there (Wrong protocol
	 * version or munge issue or whatever) so we don't kill
	 * any running jobs.  RESPONSE_FORWARD_FAILED means we
	 * couldn't contact the slurmd.
	 * last_response could be in the future if boot in progress.
	 */
	if (resp_type != RESPONSE_FORWARD_FAILED) {
		node_ptr->last_response = MAX(msg_time - 1,
					      node_ptr->last_response);
	}

	if (!IS_NODE_DOWN(node_ptr)) {
		/* Logged by node_no_resp_msg() on periodic basis */
		node_ptr->not_responding = true;
	}

	if (IS_NODE_NO_RESPOND(node_ptr) ||
	    IS_NODE_POWERING_DOWN(node_ptr) ||
	    IS_NODE_POWERED_DOWN(node_ptr))
		return;		/* Already known to be not responding */

	if (node_ptr->last_response >= msg_time) {
		debug("node_not_resp: node %s responded since msg sent",
		      node_ptr->name);
		return;
	}

	node_ptr->node_state |= NODE_STATE_NO_RESPOND;
	last_node_update = time(NULL);
	bit_clear (avail_node_bitmap, node_ptr->index);
}

/* For every node with the "not_responding" flag set, clear the flag
 * and log that the node is not responding using a hostlist expression */
extern void node_no_resp_msg(void)
{
	int i;
	node_record_t *node_ptr;
	char *host_str = NULL;
	hostlist_t *no_resp_hostlist = NULL;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (!node_ptr->not_responding ||
		    IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_DOWN(node_ptr) ||
		    IS_NODE_POWERING_UP(node_ptr))
			continue;
		if (no_resp_hostlist) {
			(void) hostlist_push_host(no_resp_hostlist,
						  node_ptr->name);
		} else
			no_resp_hostlist = hostlist_create(node_ptr->name);
		node_ptr->not_responding = false;
 	}
	if (no_resp_hostlist) {
		hostlist_uniq(no_resp_hostlist);
		host_str = hostlist_ranged_string_xmalloc(no_resp_hostlist);
		error("Nodes %s not responding", host_str);
		xfree(host_str);
		hostlist_destroy(no_resp_hostlist);
	}
}

/*
 * set_node_down - make the specified compute node's state DOWN and
 *	kill jobs as needed
 * IN name - name of the node
 * IN reason - why the node is DOWN
 */
void set_node_down (char *name, char *reason)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("set_node_down unable to find node %s", name);
		return;
	}
	set_node_down_ptr (node_ptr, reason);
}

/*
 * set_node_down_ptr - make the specified compute node's state DOWN and
 *	kill jobs as needed
 * IN node_ptr - node_ptr to the node
 * IN reason - why the node is DOWN
 */
void set_node_down_ptr(node_record_t *node_ptr, char *reason)
{
	time_t now = time(NULL);

	xassert(node_ptr);

	set_node_reason(node_ptr, reason, now);
	_make_node_down(node_ptr, now);
	(void) kill_running_job_by_node_ptr(node_ptr);
	_sync_bitmaps(node_ptr, 0);
}

/*
 * is_node_down - determine if the specified node's state is DOWN
 * IN name - name of the node
 * RET true if node exists and is down, otherwise false
 */
bool is_node_down (char *name)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("is_node_down unable to find node %s", name);
		return false;
	}

	if (IS_NODE_DOWN(node_ptr))
		return true;
	return false;
}

/*
 * is_node_resp - determine if the specified node's state is responding
 * IN name - name of the node
 * RET true if node exists and is responding, otherwise false
 */
bool is_node_resp (char *name)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("is_node_resp unable to find node %s", name);
		return false;
	}

	if (IS_NODE_NO_RESPOND(node_ptr))
		return false;
	return true;
}

/*
 * msg_to_slurmd - send given msg_type (REQUEST_RECONFIGURE or REQUEST_SHUTDOWN)
 * to every slurmd
 */
void msg_to_slurmd (slurm_msg_type_t msg_type)
{
	int i;
	shutdown_msg_t *shutdown_req;
	agent_arg_t *kill_agent_args;
	node_record_t *node_ptr;

	kill_agent_args = xmalloc (sizeof (agent_arg_t));
	kill_agent_args->msg_type = msg_type;
	kill_agent_args->retry = 0;
	kill_agent_args->hostlist = hostlist_create(NULL);
	if (msg_type == REQUEST_SHUTDOWN) {
 		shutdown_req = xmalloc(sizeof(shutdown_msg_t));
		shutdown_req->options = 0;
		kill_agent_args->msg_args = shutdown_req;
	}

	kill_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (IS_NODE_EXTERNAL(node_ptr))
			continue;
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) &&
		    (IS_NODE_POWERED_DOWN(node_ptr) ||
		     IS_NODE_POWERING_DOWN(node_ptr)))
			continue;
		if (kill_agent_args->protocol_version >
		    node_record_table_ptr[node_ptr->index]->protocol_version)
			kill_agent_args->protocol_version =
				node_record_table_ptr[node_ptr->index]->
				protocol_version;
		hostlist_push_host(kill_agent_args->hostlist, node_ptr->name);
		kill_agent_args->node_count++;
	}

	if (kill_agent_args->node_count == 0) {
		hostlist_destroy(kill_agent_args->hostlist);
		xfree (kill_agent_args);
	} else {
		debug ("Spawning agent msg_type=%s", rpc_num2string(msg_type));
		set_agent_arg_r_uid(kill_agent_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(kill_agent_args);
	}
}

/*
 * Specialized version of msg_to_slurmd that handles cross-version issues
 * when running configless.
 *
 * Since the REQUEST_RECONFIGURE message had no body, you could get away with
 * sending under the oldest format of any slurmd attached to the system.
 *
 * For configless, this would mean nothing gets sent to anyone, and those
 * older slurmds get REQUEST_RECONFIGURE_WITH_CONFIG and ignore it.
 *
 * So explicitly split the pool into three groups.
 */
#define RELEVANT_VER 4
extern void push_reconfig_to_slurmd(void)
{
	agent_arg_t *ver_args[RELEVANT_VER] = { 0 }, *curr_args;
	node_record_t *node_ptr;
	int ver;

	ver_args[0] = xmalloc(sizeof(agent_arg_t));
	ver_args[0]->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	ver_args[0]->protocol_version = SLURM_PROTOCOL_VERSION;

	ver_args[1] = xmalloc(sizeof(agent_arg_t));
	ver_args[1]->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	ver_args[1]->protocol_version = SLURM_ONE_BACK_PROTOCOL_VERSION;

	ver_args[2] = xmalloc(sizeof(agent_arg_t));
	ver_args[2]->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	ver_args[2]->protocol_version = SLURM_TWO_BACK_PROTOCOL_VERSION;

	ver_args[3] = xmalloc(sizeof(agent_arg_t));
	ver_args[3]->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	ver_args[3]->protocol_version = SLURM_MIN_PROTOCOL_VERSION;

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if (IS_NODE_FUTURE(node_ptr) || IS_NODE_EXTERNAL(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) &&
		    (IS_NODE_POWERED_DOWN(node_ptr) ||
		     IS_NODE_POWERING_DOWN(node_ptr)))
			continue;

		for (ver = 0; ver < RELEVANT_VER; ver++) {
			curr_args = ver_args[ver];
			if (node_ptr->protocol_version <
			    curr_args->protocol_version)
				continue;
			if (!curr_args->hostlist) {
				curr_args->hostlist = hostlist_create(NULL);
				curr_args->msg_args = new_config_response(true);
			}
			hostlist_push_host(curr_args->hostlist, node_ptr->name);
			curr_args->node_count++;
			break;
		}
	}

	for (ver = 0; ver < RELEVANT_VER; ver++) {
		/* This movement is needed to prevent a stack smash */
		curr_args = ver_args[ver];
		ver_args[ver] = NULL;
		if (!curr_args->node_count) {
			xfree(curr_args);
			continue;
		}

		debug("Spawning agent msg_type=%s version=%u",
		      rpc_num2string(curr_args->msg_type),
		      curr_args->protocol_version);
		set_agent_arg_r_uid(curr_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(curr_args);
	}
}


/*
 * make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(node_record_t *node_ptr, job_record_t *job_ptr)
{
	uint32_t node_flags;

	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, node_ptr->index);
	if (job_ptr->details && (job_ptr->details->share_res == 0)) {
		bit_clear(share_node_bitmap, node_ptr->index);
		(node_ptr->no_share_job_cnt)++;
	}

	if ((job_ptr->details &&
	     (job_ptr->details->whole_node & WHOLE_NODE_USER)) ||
	    (job_ptr->part_ptr &&
	     (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER))) {
		node_ptr->owner_job_cnt++;
		node_ptr->owner = job_ptr->user_id;
	}

	if (slurm_mcs_get_select(job_ptr) == 1) {
		xfree(node_ptr->mcs_label);
		node_ptr->mcs_label = xstrdup(job_ptr->mcs_label);
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	/*
	 * Only drop the reason when the node has no pending drain/fail/reboot:
	 * those flags still own the reason, and a new allocation on a
	 * shared node does not mean the node "returned to service."
	 */
	if (!(node_flags &
	      (NODE_STATE_DRAIN | NODE_STATE_FAIL |
	       NODE_STATE_REBOOT_REQUESTED | NODE_STATE_REBOOT_ISSUED))) {
		xfree(node_ptr->reason);
		node_ptr->reason_time = 0;
		node_ptr->reason_uid = NO_VAL;
	}

	if (job_ptr && job_ptr->part_ptr &&
	    (job_ptr->part_ptr->flags & PART_FLAG_PDOI)) {
		node_ptr->node_state |= NODE_STATE_POWER_DOWN;
	}

	last_node_update = time(NULL);
}

/* make_node_avail - flag specified node as available */
extern void make_node_avail(node_record_t *node_ptr)
{
	if (IS_NODE_POWER_DOWN(node_ptr) || IS_NODE_POWERING_DOWN(node_ptr))
		return;
	bit_set(avail_node_bitmap, node_ptr->index);

	/*
	 * If we are in the middle of a backfill cycle, this bitmap is
	 * used (when bf_continue is enabled) to avoid scheduling lower
	 * priority jobs on to newly available resources.
	 */
	bit_set(bf_ignore_node_bitmap, node_ptr->index);
}

extern void node_mgr_make_node_blocked(job_record_t *job_ptr, bool set)
{
	bitstr_t *tmp_bitmap;
	node_record_t *node_ptr;

	if (!IS_JOB_WHOLE_TOPO(job_ptr))
		return;

	if (!job_ptr->job_resrcs || !job_ptr->job_resrcs->node_bitmap ||
	    !job_ptr->part_ptr)
		return;

	tmp_bitmap = bit_copy(job_ptr->job_resrcs->node_bitmap);
	topology_g_whole_topo(tmp_bitmap, job_ptr->part_ptr->topology_idx);
	bit_and_not(tmp_bitmap, job_ptr->job_resrcs->node_bitmap);

	for (int i = 0; (node_ptr = next_node_bitmap(tmp_bitmap, &i)); i++) {
		if (set)
			node_ptr->node_state |= NODE_STATE_BLOCKED;
		else
			node_ptr->node_state &= (~NODE_STATE_BLOCKED);
	}

	FREE_NULL_BITMAP(tmp_bitmap);
}

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr - pointer to job that is completing
 * IN suspended - true if job was previously suspended
 */
extern void make_node_comp(node_record_t *node_ptr, job_record_t *job_ptr,
			   bool suspended)
{
	uint32_t node_flags;
	time_t now = time(NULL);

	xassert(node_ptr);
	if (suspended) {
		if (node_ptr->sus_job_cnt) {
			(node_ptr->sus_job_cnt)--;
		} else {
			error("%s: %pJ node %s sus_job_cnt underflow", __func__,
			      job_ptr, node_ptr->name);
		}
	} else {
		if (node_ptr->run_job_cnt) {
			(node_ptr->run_job_cnt)--;
		} else {
			error("%s: %pJ node %s run_job_cnt underflow", __func__,
			      job_ptr, node_ptr->name);
		}
		if (job_ptr->details && (job_ptr->details->share_res == 0)) {
			if (node_ptr->no_share_job_cnt) {
				(node_ptr->no_share_job_cnt)--;
			} else {
				error("%s: %pJ node %s no_share_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
			}
			if (node_ptr->no_share_job_cnt == 0)
				bit_set(share_node_bitmap, node_ptr->index);
		}
	}

	/* Sync up conditionals with deallocate_nodes() */
	if (!IS_NODE_DOWN(node_ptr) &&
	    !IS_NODE_POWERED_DOWN(node_ptr) &&
	    !IS_NODE_POWERING_UP(node_ptr)) {
		/* Don't verify RPC if node in DOWN or POWER_UP state */
		(node_ptr->comp_job_cnt)++;
		node_ptr->node_state |= NODE_STATE_COMPLETING;
		bit_set(cg_node_bitmap, node_ptr->index);
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if (!node_ptr->run_job_cnt && !node_ptr->comp_job_cnt &&
	    !node_ptr->hold_cnt) {
		node_ptr->last_busy = now;
		bit_set(idle_node_bitmap, node_ptr->index);
	}
	if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) {
		trigger_node_draining(node_ptr);
		if (!node_ptr->run_job_cnt && !node_ptr->comp_job_cnt &&
		    !node_ptr->hold_cnt) {
			trigger_node_drained(node_ptr);
			clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, now, NULL,
				slurm_conf.slurm_user_id);
		}
	}

	if (IS_NODE_DOWN(node_ptr)) {
		debug3("%s: Node %s being left DOWN", __func__, node_ptr->name);
	} else if (node_ptr->run_job_cnt)
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		node_ptr->last_busy = now;
	}
	last_node_update = now;
}

/*
 * Reset the statistics of a node that is powered down or downed unexpectedly
 * so that client applications do not show erroneous values.
 */
extern void node_mgr_reset_node_stats(node_record_t *node_ptr)
{
	xassert(node_ptr);
	xassert(node_ptr->energy);

	node_ptr->cpu_load = 0;
	memset(node_ptr->energy, 0, sizeof(acct_gather_energy_t));
}

/*
 * Subset of _make_node_down() except for marking node down, trigger and
 * accounting update.
 */
static void _make_node_unavail(node_record_t *node_ptr)
{
	xassert(node_ptr);

	if (!node_ptr->hold_cnt) {
		node_ptr->node_state &= (~NODE_STATE_COMPLETING);
		bit_clear(cg_node_bitmap, node_ptr->index);
	}
	bit_clear(avail_node_bitmap, node_ptr->index);
	bit_set(idle_node_bitmap, node_ptr->index);
	bit_set(share_node_bitmap, node_ptr->index);
	bit_clear(up_node_bitmap, node_ptr->index);
}

/* _make_node_down - flag specified node as down */
static void _make_node_down(node_record_t *node_ptr, time_t event_time)
{
	uint32_t node_flags;

	xassert(node_ptr);

	_make_node_unavail(node_ptr);
	node_mgr_reset_node_stats(node_ptr);
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_DOWN | node_flags;
	node_ptr->owner = NO_VAL;
	xfree(node_ptr->mcs_label);
	trigger_node_down(node_ptr);
	last_node_update = time (NULL);
	clusteracct_storage_g_node_down(acct_db_conn,
					node_ptr, event_time, NULL,
					node_ptr->reason_uid);
	/*
	 * check all reservations since node may have been in a reservation with
	 * floating count of nodes that needs to be updated
	 */
	validate_all_reservations(false, false);
}

/*
 * make_node_idle - flag specified node as having finished with a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed or NULL if not applicable
 */
void make_node_idle(node_record_t *node_ptr, job_record_t *job_ptr)
{
	uint32_t node_flags;
	time_t now = time(NULL);
	bitstr_t *node_bitmap = NULL;

	if (job_ptr) {
		if (job_ptr->node_bitmap_cg)
			node_bitmap = job_ptr->node_bitmap_cg;
		else
			node_bitmap = job_ptr->node_bitmap;
	}

	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	xassert(node_ptr);
	if (node_bitmap && (bit_test(node_bitmap, node_ptr->index))) {
		/* Not a replay */
		last_job_update = now;
		bit_clear(node_bitmap, node_ptr->index);

		if (!IS_JOB_FINISHED(job_ptr))
			job_update_tres_cnt(job_ptr, node_ptr->index);

		if (job_ptr->node_cnt) {
			/*
			 * Clean up the JOB_COMPLETING flag
			 * only if there is not the slurmctld
			 * epilog running, otherwise wait
			 * when it terminates then this
			 * function will be invoked.
			 */
			job_ptr->node_cnt--;
			cleanup_completing(job_ptr, false);
		} else if ((job_ptr->total_cpus == 0) &&
			   (job_ptr->total_nodes == 0)) {
			/* Job resized to zero nodes (expanded another job) */
		} else {
			error("%s: %pJ node_cnt underflow", __func__, job_ptr);
		}

		if (IS_JOB_SUSPENDED(job_ptr)) {
			/* Remove node from suspended job */
			if (node_ptr->sus_job_cnt)
				(node_ptr->sus_job_cnt)--;
			else
				error("%s: %pJ node %s sus_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
		} else if (IS_JOB_RUNNING(job_ptr)) {
			/* Remove node from running job */
			if (node_ptr->run_job_cnt)
				(node_ptr->run_job_cnt)--;
			else
				error("%s: %pJ node %s run_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
		} else {
			if (IS_JOB_EXPEDITING(job_ptr)) {
				debug("%s: holding %s for expedited requeue of %pJ",
				      __func__, node_ptr->name, job_ptr);
				if (!job_ptr->node_bitmap_exp_requeue)
					job_ptr->node_bitmap_exp_requeue =
						bit_alloc(node_record_count);
				bit_set(job_ptr->node_bitmap_exp_requeue,
					node_ptr->index);
				node_hold(node_ptr, job_ptr, NODE_HOLD_RECLAIM);
			}

			if (node_ptr->comp_job_cnt) {
				(node_ptr->comp_job_cnt)--;
			} else if (IS_NODE_DOWN(node_ptr)) {
				/* We were not expecting this response,
				 * ignore it */
			} else {
				error("%s: %pJ node %s comp_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
			}
			if (node_ptr->comp_job_cnt > 0)
				goto fini;	/* More jobs completing */
		}
	}

	if (!node_ptr->comp_job_cnt && !node_ptr->hold_cnt) {
		node_ptr->node_state &= (~NODE_STATE_COMPLETING);
		bit_clear(cg_node_bitmap, node_ptr->index);
		if (IS_NODE_IDLE(node_ptr)) {
			node_ptr->owner = NO_VAL;
			xfree(node_ptr->mcs_label);
		}
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_DOWN(node_ptr) || IS_NODE_FUTURE(node_ptr)) {
		debug3("%s: %pJ node %s being left %s",
		       __func__, job_ptr, node_ptr->name,
		       node_state_base_string(node_ptr->node_state));
		goto fini;
	}
	bit_set(up_node_bitmap, node_ptr->index);

	if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr) ||
	    IS_NODE_NO_RESPOND(node_ptr))
		bit_clear(avail_node_bitmap, node_ptr->index);
	else
		make_node_avail(node_ptr);

	if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) {
		trigger_node_draining(node_ptr);
		if (!node_ptr->run_job_cnt && !node_ptr->comp_job_cnt &&
		    !node_ptr->hold_cnt) {
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			bit_set(idle_node_bitmap, node_ptr->index);
			debug3("%s: %pJ node %s is DRAINED",
			       __func__, job_ptr, node_ptr->name);
			node_ptr->last_busy = now;
			trigger_node_drained(node_ptr);
			if (!IS_NODE_REBOOT_REQUESTED(node_ptr) &&
			    !IS_NODE_REBOOT_ISSUED(node_ptr))
				clusteracct_storage_g_node_down(
					acct_db_conn, node_ptr, now,
					NULL, slurm_conf.slurm_user_id);
		}
	} else if (node_ptr->run_job_cnt) {
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		     !IS_NODE_FAIL(node_ptr) && !IS_NODE_DRAIN(node_ptr))
			make_node_avail(node_ptr);
	} else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		     !IS_NODE_FAIL(node_ptr) && !IS_NODE_DRAIN(node_ptr))
			make_node_avail(node_ptr);
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		    !IS_NODE_COMPLETING(node_ptr))
			bit_set(idle_node_bitmap, node_ptr->index);
		node_ptr->last_busy = now;
	}

	if (IS_NODE_IDLE(node_ptr) && IS_NODE_POWER_DOWN(node_ptr)) {
		/*
		 * Now that the node is idle and is to be powered off, remove
		 * from the avail_node_bitmap to prevent jobs being scheduled on
		 * the node before it power's off.
		 */
		bit_clear(avail_node_bitmap, node_ptr->index);
	}

fini:
	if (job_ptr &&
	    ((job_ptr->details &&
	      (job_ptr->details->whole_node & WHOLE_NODE_USER)) ||
	     (job_ptr->part_ptr &&
	      (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)))) {
		if (node_ptr->owner_job_cnt == 0) {
			error("%s: node_ptr->owner_job_cnt underflow",
			      __func__);
		} else if (--node_ptr->owner_job_cnt == 0) {
			node_ptr->owner = NO_VAL;
			xfree(node_ptr->mcs_label);
		}
	}
	last_node_update = now;
}

extern void node_hold(node_record_t *node_ptr, job_record_t *job_ptr,
		      uint16_t type)
{
	xassert(node_ptr);
	xassert(job_ptr);

	if (type == NODE_HOLD_TEARDOWN)
		(job_ptr->hold_teardown_cnt)++;

	(node_ptr->hold_cnt)++;
	node_ptr->node_state |= NODE_STATE_COMPLETING;
	bit_set(cg_node_bitmap, node_ptr->index);
	last_node_update = time(NULL);
}

extern void node_release(node_record_t *node_ptr, job_record_t *job_ptr,
			 uint16_t type)
{
	xassert(node_ptr);
	xassert(job_ptr);

	if (type == NODE_HOLD_TEARDOWN) {
		if (job_ptr->hold_teardown_cnt)
			(job_ptr->hold_teardown_cnt)--;
		else
			error("%s: %pJ hold_teardown_cnt underflow",
			      __func__, job_ptr);
	}

	if (node_ptr->hold_cnt)
		(node_ptr->hold_cnt)--;
	else
		error("%s: %pJ node %s hold_cnt underflow",
		      __func__, job_ptr, node_ptr->name);

	make_node_idle(node_ptr, NULL);

	/* Report a drained DOWN node once its last hold is released */
	if (IS_NODE_DOWN(node_ptr) && !node_ptr->hold_cnt &&
	    !node_ptr->run_job_cnt && !node_ptr->comp_job_cnt &&
	    (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr))) {
		trigger_node_drained(node_ptr);
		clusteracct_storage_g_node_down(acct_db_conn, node_ptr,
						time(NULL), NULL,
						slurm_conf.slurm_user_id);
	}
}

extern int send_nodes_to_accounting(time_t event_time)
{
	int rc = SLURM_SUCCESS, i = 0;
	node_record_t *node_ptr = NULL;
	char *reason = NULL;
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK,
	};

 	lock_slurmctld(node_read_lock);
	/* send nodes not in 'up' state */
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (!node_ptr->name)
			continue;
		if (node_ptr->reason)
			reason = node_ptr->reason;
		else
			reason = "First Registration";
		/*
		 * Like _drain_node(), give a degraded node draining with jobs
		 * no whole-node event until they exit: its drained-TRES event
		 * stays the live one, and the next update would replace a
		 * whole-node event with it.
		 */
		if (IS_NODE_DEGRADED(node_ptr) && !IS_NODE_DOWN(node_ptr) &&
		    (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) &&
		    (node_ptr->run_job_cnt || node_ptr->comp_job_cnt)) {
			node_mgr_update_degraded_event(node_ptr,
						       node_ptr->reason,
						       node_ptr->reason_uid,
						       event_time);
			continue;
		}
		if (IS_NODE_DRAIN(node_ptr) ||
		    IS_NODE_FAIL(node_ptr) ||
		    IS_NODE_DOWN(node_ptr) ||
		    IS_NODE_FUTURE(node_ptr) ||
		    (IS_NODE_CLOUD(node_ptr) &&
		     IS_NODE_POWERED_DOWN(node_ptr))) {
			rc = clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, event_time,
				reason,
				slurm_conf.slurm_user_id);
		} else if (IS_NODE_DEGRADED(node_ptr)) {
			/*
			 * Reopen the drained-TRES interval. Other in-service
			 * nodes need nothing: whatever was open has just been
			 * closed by the cluster registration that triggered
			 * this call. Not node_up(), which xfrees
			 * node_ptr->reason and this walk only holds the node
			 * read lock.
			 */
			node_mgr_update_degraded_event(node_ptr,
						       node_ptr->reason,
						       node_ptr->reason_uid,
						       event_time);
		}
		if (rc == SLURM_ERROR)
			break;
	}
	unlock_slurmctld(node_read_lock);
	return rc;
}

/* node_fini - free all memory associated with node records */
extern void node_fini (void)
{
	node_features_free_lists();
	FREE_NULL_BITMAP(asap_node_bitmap);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(bf_ignore_node_bitmap);
	FREE_NULL_BITMAP(booting_node_bitmap);
	FREE_NULL_BITMAP(cg_node_bitmap);
	FREE_NULL_BITMAP(cloud_node_bitmap);
	FREE_NULL_BITMAP(external_node_bitmap);
	FREE_NULL_BITMAP(future_node_bitmap);
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(power_down_node_bitmap);
	FREE_NULL_BITMAP(power_up_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	FREE_NULL_BITMAP(rs_node_bitmap);
	node_fini2();
}

/* Reset a node's CPU load value */
extern void reset_node_load(char *node_name, uint32_t cpu_load)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record(node_name);
	if (node_ptr) {
		time_t now = time(NULL);
		node_ptr->cpu_load = cpu_load;
		node_ptr->cpu_load_time = now;
		last_node_update = now;
	} else
		error("reset_node_load unable to find node %s", node_name);
}

/* Reset a node's free memory value */
extern void reset_node_free_mem(char *node_name, uint64_t free_mem)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record(node_name);
	if (node_ptr) {
		time_t now = time(NULL);
		node_ptr->free_mem = free_mem;
		node_ptr->free_mem_time = now;
		last_node_update = now;
	} else
		error("reset_node_free_mem unable to find node %s", node_name);
}


/*
 * Check for node timed events
 *
 * Such as:
 * reboots - If the node hasn't booted by ResumeTimeout, mark the node as down.
 * resume_after - Resume a down|drain node after resume_after time.
 */
extern void check_node_timers(void)
{
	int i;
	node_record_t *node_ptr;
	time_t now = time(NULL);
	hostlist_t *resume_hostlist = NULL;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->resume_after && (now > node_ptr->resume_after)) {
			/* Fire resume, reset the time */
			node_ptr->resume_after = 0;

			if (!resume_hostlist)
				resume_hostlist = hostlist_create(NULL);

			hostlist_push_host(resume_hostlist, node_ptr->name);
		}
	}

	if (resume_hostlist) {
		char *host_str;
		update_node_msg_t *resume_msg = NULL;
		slurm_msg_t msg = {
			.auth = SLURM_MSG_AUTH_INIT_AS_INTERNAL,
		};

		hostlist_uniq(resume_hostlist);
		host_str = hostlist_ranged_string_xmalloc(resume_hostlist);
		hostlist_destroy(resume_hostlist);
		debug("Issuing resume request for nodes %s", host_str);

		resume_msg = xmalloc(sizeof(*resume_msg));
		slurm_init_update_node_msg(resume_msg);
		resume_msg->node_state = NODE_RESUME;
		resume_msg->node_names = host_str;

		update_node(&msg, resume_msg, NULL);

		slurm_free_update_node_msg(resume_msg);

		/* Back the node changes up */
		schedule_node_save();
	}
}

extern bool waiting_for_node_boot(node_record_t *node_ptr)
{
	xassert(node_ptr);

	if ((IS_NODE_POWERING_UP(node_ptr) ||
	     IS_NODE_REBOOT_ISSUED(node_ptr)) &&
	    (node_ptr->boot_time < node_ptr->boot_req_time)) {
		debug("Still waiting for boot of node %s", node_ptr->name);
		return true;
	}

	return false;
}

extern bool waiting_for_node_power_down(node_record_t *node_ptr)
{
	xassert(node_ptr);

	if (IS_NODE_POWERING_DOWN(node_ptr) &&
	    node_ptr->power_save_req_time &&
	    (node_ptr->boot_time <
	     (node_ptr->power_save_req_time + slurm_conf.suspend_timeout))) {
		debug("Still waiting for node '%s' to power off",
		      node_ptr->name);
		return true;
	}

	return false;
}

extern void set_node_comm_name(node_record_t *node_ptr, char *comm_name,
			       char *hostname)
{
	xfree(node_ptr->comm_name);
	node_ptr->comm_name = xstrdup(comm_name ? comm_name : hostname);

	xfree(node_ptr->node_hostname);
	node_ptr->node_hostname = xstrdup(hostname);

	slurm_reset_alias(node_ptr->name,
			  node_ptr->comm_name,
			  node_ptr->node_hostname);
}

/*
 * Never delete a node here on error; create_nodes() rolls back the whole batch.
 * Deleting the only node created so far would free config_ptr and its
 * node_bitmap before that rollback reads them.
 */
static int _build_node_callback(char *alias, char *hostname, char *address,
				char *bcast_address, uint16_t port,
				int state_val, slurm_conf_node_t *conf_node,
				config_record_t *config_ptr)
{
	int rc = SLURM_SUCCESS;
	node_record_t *node_ptr = NULL;

	if ((rc = add_node_record(alias, config_ptr, &node_ptr)))
		return rc;

	if ((state_val != NO_VAL) &&
	    (state_val != NODE_STATE_UNKNOWN))
		node_ptr->node_state = state_val;
	node_ptr->last_response = (time_t) 0;
	node_ptr->comm_name = xstrdup(address);
	node_ptr->cpu_bind  = conf_node->cpu_bind;
	node_ptr->node_hostname = xstrdup(hostname);
	node_ptr->bcast_address = xstrdup(bcast_address);
	node_ptr->port = port;
	node_ptr->reason = xstrdup(conf_node->reason);
	node_ptr->parameters = xstrdup(conf_node->parameters);

	node_ptr->node_state |= NODE_STATE_DYNAMIC_NORM;

	slurm_conf_add_node(node_ptr);

	if (config_ptr->feature) {
		node_ptr->features = xstrdup(config_ptr->feature);
		node_ptr->features_act = xstrdup(config_ptr->feature);
	}

	if (node_ptr->topology_str && topology_g_add_rm_node(node_ptr))
		return ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;

	bit_clear(power_up_node_bitmap, node_ptr->index);
	if (IS_NODE_FUTURE(node_ptr)) {
		bit_set(future_node_bitmap, node_ptr->index);
	} else if (IS_NODE_CLOUD(node_ptr) || IS_NODE_EXTERNAL(node_ptr)) {
		make_node_idle(node_ptr, NULL);
		if (IS_NODE_CLOUD(node_ptr)) {
			bit_set(cloud_node_bitmap, node_ptr->index);
			bit_set(power_down_node_bitmap, node_ptr->index);
		} else {
			bit_set(external_node_bitmap, node_ptr->index);
		}

		/*
		 * share_node_bitmap is normally set as part of a node
		 * registration/ping that external/cloud nodes cannot reach (no
		 * slurmd, or not until power on), so set bit explicitly here.
		 */
		bit_set(share_node_bitmap, node_ptr->index);

		if (conf_node->gres_conf) {
			gres_add_dynamic_gres(conf_node->gres_conf,
					      node_ptr->name);
		} else if ((rc = gres_g_node_config_load(node_ptr->config_ptr
								 ->cpus,
							 node_ptr->name,
							 node_ptr->gres_list,
							 NULL, NULL))) {
			return rc;
		}

		rc = gres_node_config_validate(node_ptr,
					       node_ptr->config_ptr->threads,
					       node_ptr->config_ptr->cores,
					       node_ptr->config_ptr
						       ->tot_sockets,
					       node_conf_overrides(node_ptr),
					       NULL, NULL);
		/*
		 * A cloud node's GRES is validated again when its slurmd
		 * registers, so as at startup in _gres_reconfig(), do not fail
		 * the create on what the controller's gres.conf shows now.
		 * With AutoDetect it shows no devices at all.
		 */
		if (IS_NODE_CLOUD(node_ptr))
			rc = SLURM_SUCCESS;
	}

	return rc;
}

extern void consolidate_config_list(bool is_locked, bool force)
{
	config_record_t *curr_rec;
	list_itr_t *iter;
	slurmctld_lock_t node_write_lock = { .node = WRITE_LOCK };

	if (is_locked)
		xassert(verify_lock(NODE_LOCK, WRITE_LOCK));

	if (force || _get_config_list_update()) {
		if (!is_locked)
			lock_slurmctld(node_write_lock);
		slurm_mutex_lock(&config_list_update_mutex);

		config_list_update = false;

		/* Use list iterator because we are changing the list */
		iter = list_iterator_create(config_list);
		while ((curr_rec = list_next(iter))) {
			_combine_dup_config_records(curr_rec);
		}
		list_iterator_destroy(iter);

		slurm_mutex_unlock(&config_list_update_mutex);
		if (!is_locked)
			unlock_slurmctld(node_write_lock);
	}
}

extern int create_nodes(update_node_msg_t *msg, char **err_msg)
{
	char *nodeline = msg->extra;
	int state_val, rc = SLURM_SUCCESS;
	slurm_conf_node_t *conf_node;
	config_record_t *config_ptr;
	s_p_hashtbl_t *node_hashtbl = NULL;
	list_t *hres_info = NULL;
	slurmctld_lock_t write_lock = {
		.conf = READ_LOCK,
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.part = WRITE_LOCK,
		.select_node = WRITE_LOCK,
	};

	xassert(nodeline);
	xassert(err_msg);

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		*err_msg = xstrdup("Node creation only compatible with select/cons_tres");
		error("%s", *err_msg);
		return ESLURM_ACCESS_DENIED;
	}

	lock_slurmctld(write_lock);

	if (!(conf_node = slurm_conf_parse_nodeline(nodeline, &node_hashtbl))) {
		*err_msg = xstrdup_printf("Failed to parse nodeline '%s'",
					  nodeline);
		error("%s", *err_msg);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* copy this so upstream logging messages are more detailed */
	xfree(msg->node_names);
	msg->node_names = xstrdup(conf_node->nodenames);

	if ((rc = _validate_nodes_vs_nodeset(conf_node->nodenames))
	    != SLURM_SUCCESS)
		goto fini;

	if (conf_node->hres_str &&
	    (rc = SERCLI_PARSE_STR(NODE_HRES_INFO_LIST, NULL, hres_info,
				   conf_node->hres_str,
				   strlen(conf_node->hres_str),
				   MIME_TYPE_JSON))) {
		*err_msg =
			xstrdup_printf("Unable to parse node %s HRES=%s: %s",
				       conf_node->nodenames,
				       conf_node->hres_str, slurm_strerror(rc));
		error("%s", *err_msg);
		goto fini;
	}

	state_val = state_str2int(conf_node->state, conf_node->nodenames);
	if ((state_val == NO_VAL) ||
	    ((state_val != NODE_STATE_FUTURE) &&
	     !(state_val & NODE_STATE_CLOUD) &&
	     !(state_val & NODE_STATE_EXTERNAL))) {
		*err_msg = xstrdup("Only State=FUTURE, CLOUD, or EXTERNAL allowed for nodes created by scontrol");
		error("%s", *err_msg);
		rc = ESLURM_INVALID_NODE_STATE;
		goto fini;
	}

	/*
	 * Allow INFINITE through: it is a valid "never suspend"
	 * default for a new node even without power save configured.
	 * update_node() rejects INFINITE too, but that is intentional.
	 */
	if ((conf_node->suspend_time != NO_VAL) &&
	    (conf_node->suspend_time != INFINITE) && !power_save_avail) {
		*err_msg = xstrdup_printf(
			"Cannot create node %s with SuspendTime=%u, automatic power save mode is disabled",
			conf_node->nodenames, conf_node->suspend_time);
		error("%s", *err_msg);
		rc = ESLURM_DISABLED;
		goto fini;
	}

	config_ptr = config_record_from_conf_node(conf_node,
						  slurmctld_tres_cnt);
	config_ptr->node_bitmap = bit_alloc(node_record_count);

	if ((rc = expand_nodeline_info(conf_node, config_ptr, err_msg,
				       _build_node_callback))) {
		bitstr_t *created_bitmap = NULL;
		node_record_t *node_ptr = NULL;
		bool orphaned = false;

		error("Failed to create a node in '%s': %s",
		      conf_node->nodenames, *err_msg);

		/*
		 * add_node_record() only sets a bit for a node it created, so
		 * this rolls back the batch without touching pre-existing
		 * nodes. Iterate a copy: deleting the last node referencing
		 * config_ptr frees it and node_bitmap with it.
		 */
		created_bitmap = bit_copy(config_ptr->node_bitmap);

		for (int i = 0;
		     (node_ptr = next_node_bitmap(created_bitmap, &i)); i++) {
			if (_delete_node_ptr(node_ptr) != SLURM_SUCCESS)
				orphaned = true;
		}

		if (orphaned)
			error("Failed to roll back every node created from '%s'",
			      conf_node->nodenames);

		/* Nothing created means nothing ever referenced config_ptr. */
		if (!bit_set_count(created_bitmap))
			list_delete_ptr(config_list, config_ptr);
		config_ptr = NULL;

		FREE_NULL_BITMAP(created_bitmap);

		goto fini;
	}

	if (hres_info) {
		if (hres_add_nodes(hres_info, conf_node->nodenames,
				   config_ptr->node_bitmap)) {
			error("%s: Ignoring invalid HRES specification for nodes %s: %s",
			      __func__, conf_node->nodenames,
			      conf_node->hres_str);
		} else {
			bitstr_t *created_nodes = config_ptr->node_bitmap;
			node_record_t *node_ptr;

			for (int i = 0;
			     (node_ptr = next_node_bitmap(created_nodes, &i));
			     i++)
				node_ptr->hres_list_config =
					node_hres_info_list_copy(hres_info);
		}
	}

	if (config_ptr->feature) {
		node_features_update_list(avail_feature_list,
					  config_ptr->feature,
					  config_ptr->node_bitmap);
		node_features_update_list(active_feature_list,
					  config_ptr->feature,
					  config_ptr->node_bitmap);
	}

	_queue_consolidate_config_list();

	set_cluster_tres(false);
	_update_parts();
	power_save_set_timeouts(NULL);
	power_save_exc_setup();
	select_g_reconfigure();

fini:
	FREE_NULL_LIST(hres_info);
	s_p_hashtbl_destroy(node_hashtbl);
	unlock_slurmctld(write_lock);

	if (rc == SLURM_SUCCESS) {
		/* Must be called outside of locks */
		clusteracct_storage_g_cluster_tres(
			acct_db_conn, NULL, NULL, 0, SLURM_PROTOCOL_VERSION);
	}

	return rc;
}

extern int create_dynamic_reg_node(slurm_msg_t *msg)
{
	config_record_t *config_ptr;
	node_record_t *node_ptr;
	int state_val = NODE_STATE_UNKNOWN, rc;
	s_p_hashtbl_t *node_hashtbl = NULL;
	slurm_conf_node_t *conf_node = NULL;
	slurm_node_registration_status_msg_t *reg_msg = msg->data;
	list_t *hres_info = NULL;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, WRITE_LOCK));

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		error("Node creation only compatible with select/cons_tres");
		return ESLURM_ACCESS_DENIED;
	}

	if (reg_msg->dynamic_conf) {
		if (!(conf_node =
		      slurm_conf_parse_nodeline(reg_msg->dynamic_conf,
						&node_hashtbl))) {
			s_p_hashtbl_destroy(node_hashtbl);
			error("Failed to parse dynamic nodeline '%s'",
			      reg_msg->dynamic_conf);
			return SLURM_ERROR;
		}

		if (_validate_nodes_vs_nodeset(conf_node->nodenames) !=
		    SLURM_SUCCESS) {
			s_p_hashtbl_destroy(node_hashtbl);
			return SLURM_ERROR;
		}

		if (conf_node->hres_str &&
		    SERCLI_PARSE_STR(NODE_HRES_INFO_LIST, NULL, hres_info,
				     conf_node->hres_str,
				     strlen(conf_node->hres_str),
				     MIME_TYPE_JSON)) {
			s_p_hashtbl_destroy(node_hashtbl);
			error("%s: Unable to parse node %s HRES=%s",
			      __func__, conf_node->nodenames,
			      conf_node->hres_str);
			FREE_NULL_LIST(hres_info);
			return SLURM_ERROR;
		}

		/*
		 * Unlike create_nodes(), do not reject the registration: the
		 * identical nodeline in slurm.conf is accepted unconditionally
		 * (no power_save_avail check anywhere in read_config.c), and a
		 * hard failure here surfaces to the caller as a bare ENOENT --
		 * the registering slurmd just silently never joins. The value
		 * is already inert whenever power save isn't configured (see
		 * power_save_set_timeouts()), so warn and let it through.
		 */
		if ((conf_node->suspend_time != NO_VAL) &&
		    (conf_node->suspend_time != INFINITE) &&
		    !power_save_avail) {
			warning("Dynamic node %s registered with SuspendTime=%u, but automatic power save mode is disabled",
				conf_node->nodenames, conf_node->suspend_time);
		}

		config_ptr = config_record_from_conf_node(conf_node,
							  slurmctld_tres_cnt);
		if (conf_node->state)
			state_val = state_str2int(conf_node->state,
						  conf_node->nodenames);
	} else {
		config_ptr = create_config_record();
		config_ptr->boards = reg_msg->boards;
		config_ptr->cores = reg_msg->cores;
		config_ptr->cpus = reg_msg->cpus;
		config_ptr->nodes = xstrdup(reg_msg->node_name);
		config_ptr->real_memory = reg_msg->real_memory;
		config_ptr->threads = reg_msg->threads;
		config_ptr->tmp_disk = reg_msg->tmp_disk;
		config_ptr->tot_sockets = reg_msg->sockets;
	}

	config_ptr->node_bitmap = bit_alloc(node_record_count);

	if ((rc = add_node_record(reg_msg->node_name, config_ptr, &node_ptr))) {
		error("%s (%s)", slurm_strerror(rc), reg_msg->node_name);
		list_delete_ptr(config_list, config_ptr);
		s_p_hashtbl_destroy(node_hashtbl);
		FREE_NULL_LIST(hres_info);
		return SLURM_ERROR;
	}

	if (conf_node && conf_node->port_str)
		node_ptr->port = strtol(conf_node->port_str, NULL, 10);

	/*
	 * Always resolve comm_name in slurmctld as hostname may resolve
	 * differently here than on the compute node that sent the RPC.
	 */
	xfree(node_ptr->comm_name);
	if (!(node_ptr->comm_name = _get_msg_hostname(msg)))
		node_ptr->comm_name = xstrdup(reg_msg->hostname);
	xfree(node_ptr->node_hostname);
	node_ptr->node_hostname = xstrdup(reg_msg->hostname);
	slurm_conf_add_node(node_ptr);
	bit_set(power_up_node_bitmap, node_ptr->index);

	node_ptr->features = xstrdup(node_ptr->config_ptr->feature);
	node_features_update_list(avail_feature_list, node_ptr->features,
				  config_ptr->node_bitmap);
	node_ptr->features_act = xstrdup(node_ptr->config_ptr->feature);
	node_features_update_list(active_feature_list, node_ptr->features_act,
				  config_ptr->node_bitmap);

	if (node_ptr->topology_str && topology_g_add_rm_node(node_ptr)) {
		error("%s Invalid node topology specified %s ignored",
		      __func__, node_ptr->topology_str);
		xfree(node_ptr->topology_str);
		topology_g_add_rm_node(node_ptr);
	}
	if (hres_info) {
		if (hres_add_nodes(hres_info, node_ptr->name,
				   config_ptr->node_bitmap))
			error("%s: Ignoring invalid HRES specification for node %s: %s",
			      __func__, node_ptr->name, conf_node->hres_str);
		else
			node_ptr->hres_list_config =
				node_hres_info_list_copy(hres_info);
	}
	FREE_NULL_LIST(hres_info);

	_queue_consolidate_config_list();

	/* Handle DOWN and DRAIN, otherwise make the node idle */
	if ((state_val == NODE_STATE_DOWN) ||
	    (state_val & NODE_STATE_DRAIN)) {
		time_t now = time(NULL);
		if (conf_node && conf_node->reason)
			set_node_reason(node_ptr, conf_node->reason, now);
		_make_node_down(node_ptr, now);
		node_ptr->node_state = state_val;
	} else
		make_node_idle(node_ptr, NULL);

	node_ptr->node_state |= NODE_STATE_DYNAMIC_NORM;

	set_cluster_tres(false);
	_update_parts();
	power_save_set_timeouts(NULL);
	power_save_exc_setup();
	select_g_reconfigure();

	s_p_hashtbl_destroy(node_hashtbl);

	return SLURM_SUCCESS;
}

static void _remove_node_from_features(node_record_t *node_ptr)
{
	bitstr_t *node_bitmap = bit_alloc(node_record_count);
	bit_set(node_bitmap, node_ptr->index);
	node_features_update_list(avail_feature_list, NULL, node_bitmap);
	node_features_update_list(active_feature_list, NULL, node_bitmap);
	FREE_NULL_BITMAP(node_bitmap);
}

/*
 * Remove from all global bitmaps
 *
 * Sync with bitmaps in _init_bitmaps()
 */
static void _remove_node_from_all_bitmaps(node_record_t *node_ptr)
{
	bit_clear(asap_node_bitmap, node_ptr->index);
	bit_clear(avail_node_bitmap, node_ptr->index);
	bit_clear(bf_ignore_node_bitmap, node_ptr->index);
	bit_clear(booting_node_bitmap, node_ptr->index);
	bit_clear(cg_node_bitmap, node_ptr->index);
	bit_clear(cloud_node_bitmap, node_ptr->index);
	bit_clear(external_node_bitmap, node_ptr->index);
	bit_clear(future_node_bitmap, node_ptr->index);
	bit_clear(idle_node_bitmap, node_ptr->index);
	bit_clear(power_down_node_bitmap, node_ptr->index);
	bit_clear(power_up_node_bitmap, node_ptr->index);
	bit_clear(rs_node_bitmap, node_ptr->index);
	bit_clear(share_node_bitmap, node_ptr->index);
	bit_clear(up_node_bitmap, node_ptr->index);
}

/*
 * Has to be in slurmctld code for locking.
 */
static int _delete_node_ptr(node_record_t *node_ptr)
{
	xassert(node_ptr);

	if (!IS_NODE_DYNAMIC_NORM(node_ptr) && !IS_NODE_EXTERNAL(node_ptr)) {
		error("Can't delete non-dynamic node '%s'.", node_ptr->name);
		return ESLURM_INVALID_NODE_STATE;
	}
	if (IS_NODE_ALLOCATED(node_ptr) || IS_NODE_COMPLETING(node_ptr) ||
	    node_ptr->hold_cnt) {
		error("Node '%s' can't be delete because it's still in use.",
		      node_ptr->name);
		return ESLURM_NODES_BUSY;
	}
	if (node_ptr->node_state & NODE_STATE_RES) {
		error("Node '%s' can't be delete because it's in a reservation.",
		      node_ptr->name);
		return ESLURM_NODES_BUSY;
	}

	xfree(node_ptr->power_action_name);
	xfree(node_ptr->topology_orig_str);
	xfree(node_ptr->topology_str);
	topology_g_add_rm_node(node_ptr);

	_remove_node_from_all_bitmaps(node_ptr);
	hres_rm_node(node_ptr);
	_remove_node_from_features(node_ptr);
	gres_node_remove(node_ptr);

	xhash_pop_str(node_hash_table, node_ptr->name);
	slurm_conf_remove_node(node_ptr->name);
	delete_node_record(node_ptr);

	return SLURM_SUCCESS;
}

static int _delete_node(char *name)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record(name);
	if (!node_ptr) {
		error("Unable to find node %s to delete", name);
		return ESLURM_INVALID_NODE_NAME;
	}
	return _delete_node_ptr(node_ptr);
}

extern int delete_nodes(char *names, char **err_msg)
{
	char *node_name;
	hostlist_t *to_delete;
	bool one_success = false;
	int ret_rc = SLURM_SUCCESS;
	hostlist_t *error_hostlist = NULL;
	slurmctld_lock_t write_lock = {
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.part = WRITE_LOCK,
		.conf = READ_LOCK,
		.select_node = WRITE_LOCK,
	};

	xassert(err_msg);

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		*err_msg = xstrdup("Node deletion only compatible with select/cons_tres");
		error("%s", *err_msg);
		return ESLURM_ACCESS_DENIED;
	}

	lock_slurmctld(write_lock);

	if (!(to_delete = nodespec_to_hostlist(names, true, NULL))) {
		ret_rc = ESLURM_INVALID_NODE_NAME;
		goto cleanup;
	}
	if (!hostlist_count(to_delete)) {
		info("%s: expansion of node specification '%s' resulted in zero nodes",
		     __func__, names);
		ret_rc = ESLURM_INVALID_NODE_NAME;
		goto cleanup;
	}

	while ((node_name = hostlist_shift(to_delete))) {
		int rc;
		if ((rc = _delete_node(node_name))) {
			error("failed to delete node '%s'", node_name);
			if (!error_hostlist)
				error_hostlist = hostlist_create(node_name);
			else
				hostlist_push_host(error_hostlist, node_name);
		} else
			one_success = true;
		ret_rc |= rc;
		xfree(node_name);
	}

	if (one_success) {
		set_cluster_tres(false);
		_update_parts();
		select_g_reconfigure();
		power_save_exc_setup();
	}
	if (error_hostlist) {
		char *nodes = hostlist_ranged_string_xmalloc(error_hostlist);
		*err_msg = xstrdup_printf("failed to delete nodes %s", nodes);
		xfree(nodes);
		FREE_NULL_HOSTLIST(error_hostlist);
	}

cleanup:
	unlock_slurmctld(write_lock);
	if (one_success) {
		/* Must be called outside of locks */
		clusteracct_storage_g_cluster_tres(
			acct_db_conn, NULL, NULL, 0, SLURM_PROTOCOL_VERSION);
	}

	FREE_NULL_HOSTLIST(to_delete);

	return ret_rc;
}

extern void set_node_reason(node_record_t *node_ptr,
			    char *message,
			    time_t time)
{
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(node_ptr);

	if (message && message[0]) {
		if (node_ptr->reason) {
			char *tmp;
			tmp = xstrdup(" : ");
			xstrcat(tmp, message);
			if (!xstrstr(node_ptr->reason, tmp))
				xstrfmtcat(node_ptr->reason, " : %s", message);
			xfree(tmp);
		} else {
			node_ptr->reason = xstrdup(message);
		}
		node_ptr->reason_time = time;
		node_ptr->reason_uid = slurm_conf.slurm_user_id;
	} else {
		xfree(node_ptr->reason);
		node_ptr->reason_time = 0;
		node_ptr->reason_uid = NO_VAL;
	}
}

extern int node_mgr_set_node_topology(node_record_t *node_ptr,
				      char *new_topology_str)
{
	int rc = SLURM_SUCCESS;
	char *topology_str_old;
	bool from_config = false;
	bool stored_orig = false;

	if (!node_ptr->topology_str && !node_ptr->topology_orig_str) {
		node_ptr->topology_orig_str =
			topology_g_get_topology_str(node_ptr);
		stored_orig = true;
	}

	if (node_ptr->topology_str) {
		topology_str_old = node_ptr->topology_str;
		node_ptr->topology_str = NULL;
	} else {
		if (stored_orig)
			topology_str_old = xstrdup(node_ptr->topology_orig_str);
		else
			topology_str_old =
				topology_g_get_topology_str(node_ptr);
		from_config = true;
	}

	if (*new_topology_str) {
		node_ptr->topology_str = xstrdup(new_topology_str);
	}

	if (topology_g_add_rm_node(node_ptr)) {
		info("Invalid node topology specified %s",
		     node_ptr->topology_str);
		xfree(node_ptr->topology_str);
		node_ptr->topology_str = topology_str_old;
		topology_g_add_rm_node(node_ptr);
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		if (from_config)
			xfree(node_ptr->topology_str);
		if (stored_orig)
			xfree(node_ptr->topology_orig_str);
	} else {
		xfree(topology_str_old);
	}

	return rc;
}
