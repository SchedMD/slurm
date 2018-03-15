/*****************************************************************************\
 *  nrt.c - Library routines for initiating jobs using IBM's NRT (Network
 *          Routing Table)
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2011-2014 SchedMD LLC.
 *  Original switch/federation plugin written by Jason King <jking@llnl.gov>
 *  Largely re-written for NRT support by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
 *****************************************************************************
 *  NOTE: The NRT API communicates with IBM's Protocol Network Services Deamon
 *  (PNSD). PNSD logs are written to /tmp/serverlog.
 *
 *  NOTE: To get good POE error message it may be necessary to execute
 *  export LANG=en_US
 *
 *  NOTE: POE core files always written to /tmp
 *
 *  NOTE: POE and PMD initiallly load /usr/lib64/libpermapi.so rather than the
 *  library specified by MP_PRE_RMLIB in /etc/poe.limits. For now we need to
 *  put SLURM's libpermapi.so in /usr/lib64. IBM to address later.
\*****************************************************************************/

#include "config.h"

#include <assert.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <nrt.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/strlcpy.h"
#include "src/common/read_config.h"
#include "src/common/node_conf.h"
#include "src/plugins/switch/nrt/nrt_keys.h"
#include "src/plugins/switch/nrt/slurm_nrt.h"

/* This plugin may execute on a head node WITHOUT the libnrt.so file.
 * Dynamically load the library only on demand. */
void *nrt_handle = NULL;
char *nrt_sym[]  = {
	"nrt_command",
	NULL
};
typedef struct {
	int (*nrt_command)(int version, nrt_cmd_type_t cmd_type, void *cmd);
} nrt_api_t;
nrt_api_t nrt_api;

static int nrt_cmd_wrap(int version, nrt_cmd_type_t cmd_type, void *cmd)
{
	int i, rc;

	if (!nrt_handle) {
		void **api_pptr = (void **) &nrt_api;
#ifdef LIBNRT_SO
		nrt_handle = dlopen(LIBNRT_SO, RTLD_LAZY);
#endif
		if (!nrt_handle)
			fatal("Can not open libnrt.so");

		dlerror();	/* Clear any existing error */
		for ( i = 0; nrt_sym[i]; ++i ) {
		        api_pptr[i] = dlsym(nrt_handle, nrt_sym[i]);
		        if (!api_pptr[i]) {
				fatal("Can't find %s in libnrt.so",
				      nrt_sym[i]);
			}
		}
	}

	rc = ((*(nrt_api.nrt_command))(version, cmd_type, cmd));
	return rc;
}

extern int drain_nodes ( char *nodes, char *reason, uint32_t reason_uid );

/*
 * Definitions local to this module
 */
#define NRT_NULL_MAGIC  	0xDEAFDEAF
#define NRT_NODEINFO_MAGIC	0xc00cc00a
#define NRT_JOBINFO_MAGIC	0xc00cc00b

#define NRT_LIBSTATE_MAGIC	0xc00cc00c
#define NRT_HOSTLEN		20
#define NRT_NODECOUNT		128
#define NRT_HASHCOUNT		128
#define NRT_MAX_ADAPTERS (NRT_MAX_ADAPTERS_PER_TYPE * NRT_MAX_ADAPTER_TYPES)
#define NRT_MAX_PROTO_CNT	20

/* Use slurm protocol version as a global version number.
 */
#define NRT_STATE_VERSION      "PROTOCOL_VERSION"

pthread_mutex_t		global_lock = PTHREAD_MUTEX_INITIALIZER;
extern bool		nrt_need_state_save;
slurm_nrt_libstate_t *	nrt_state = NULL;
mode_t			nrt_umask;

/*
 * Data structures specific to switch/nrt
 *
 * We are going to some trouble to keep these defs private so slurm
 * hackers not interested in the interconnect details can just pass around
 * the opaque types.  All use of the data structure internals is local to this
 * module.
 */

/* Notes about job_key:
 * - It must be unique for every job step.
 * - It is a 32-bit quantity.
 * - We might use the bottom 16-bits of job ID an step ID, but that could
 *   result in conflicts for long-lived jobs or job steps.
 */
typedef struct slurm_nrt_window {
	nrt_window_id_t window_id;
	win_state_t state;
	nrt_job_key_t job_key;
} slurm_nrt_window_t;

typedef struct slurm_nrt_block {
	uint32_t rcontext_block_use;	/* RDMA context blocks used */
	nrt_job_key_t job_key;
} slurm_nrt_block_t;

typedef struct slurm_nrt_adapter {
	char adapter_name[NRT_MAX_ADAPTER_NAME_LEN];
	nrt_adapter_t adapter_type;
	nrt_cau_index_t cau_indexes_avail;
	nrt_cau_index_t cau_indexes_used;
	nrt_imm_send_slot_t immed_slots_avail;
	nrt_imm_send_slot_t immed_slots_used;
	in_addr_t ipv4_addr;
	struct in6_addr ipv6_addr;
	nrt_logical_id_t lid;
	nrt_network_id_t network_id;
	nrt_port_id_t port_id;
	uint64_t rcontext_block_count;	/* # of RDMA context blocks */
	uint64_t rcontext_block_used;	/* # of RDMA context blocks used */
	uint16_t block_count;
	slurm_nrt_block_t *block_list;
	uint64_t special;
	nrt_window_id_t window_count;
	slurm_nrt_window_t *window_list;
} slurm_nrt_adapter_t;

struct slurm_nrt_nodeinfo {
	uint32_t magic;
	char name[NRT_HOSTLEN];
	uint32_t adapter_count;
	slurm_nrt_adapter_t *adapter_list;
	struct slurm_nrt_nodeinfo *next;
	nrt_node_number_t node_number;
};

struct slurm_nrt_libstate {
	uint32_t magic;
	uint32_t node_count;
	uint32_t node_max;
	slurm_nrt_nodeinfo_t *node_list;
	uint32_t hash_max;
	slurm_nrt_nodeinfo_t **hash_table;
	nrt_job_key_t key_index;
};

struct slurm_nrt_jobinfo {
	uint32_t magic;
	/* version from nrt_version() */
	/* adapter from lid in table */
	/* uid from getuid() */
	/* pid from getpid() */
	nrt_job_key_t job_key;
	uint8_t bulk_xfer;	/* flag */
	uint32_t bulk_xfer_resources;
	uint16_t cau_indexes;
	uint16_t immed_slots;
	uint8_t ip_v4;		/* flag */
	uint8_t user_space;	/* flag */
	uint16_t tables_per_task;
	nrt_tableinfo_t *tableinfo;

	hostlist_t nodenames;
	uint32_t num_tasks;
};

typedef struct {
	char adapter_name[NRT_MAX_ADAPTER_NAME_LEN];
	nrt_adapter_t adapter_type;
} nrt_cache_entry_t;


typedef struct nrt_protocol_info {
	char protocol_name[NRT_MAX_PROTO_NAME_LEN];
} nrt_protocol_info_t;

typedef struct nrt_protocol_table {
	nrt_protocol_info_t protocol_table[NRT_MAX_PROTO_CNT];
	int protocol_table_cnt;	/* Count of entries in protocol_table */
} nrt_protocol_table_t;

typedef struct slurm_nrt_suspend_info {
	uint32_t job_key_count;
	uint32_t job_key_array_size;
	nrt_job_key_t *job_key;
} slurm_nrt_suspend_info_t;

static int lid_cache_size = 0;
static nrt_cache_entry_t lid_cache[NRT_MAX_ADAPTERS];
static bool dynamic_window_err = false;	/* print error only once */

/* Keep track of local ID so slurmd can determine which switch tables
 * are for that particular node */
static uint64_t my_lpar_id = 0;
static uint64_t my_lid = 0;
static bool     my_lpar_id_set = false;
static uint64_t my_network_id = 0;
static bool     my_network_id_set = false;

/* Local functions */
static char *	_adapter_type_str(nrt_adapter_t type);
static int	_add_block_use(slurm_nrt_jobinfo_t *jp,
			       slurm_nrt_adapter_t *adapter);
static int	_add_immed_use(char *hostname, slurm_nrt_jobinfo_t *jp,
			       slurm_nrt_adapter_t *adapter);
static int	_allocate_windows_all(slurm_nrt_jobinfo_t *jp, char *hostname,
			uint32_t node_id, nrt_task_id_t task_id,
			nrt_adapter_t adapter_type, int network_id,
			nrt_protocol_table_t *protocol_table, int instances,
			int task_inx);
static int	_allocate_window_single(char *adapter_name,
			slurm_nrt_jobinfo_t *jp, char *hostname,
			uint32_t node_id, nrt_task_id_t task_id,
			nrt_adapter_t adapter_type, int network_id,
			nrt_protocol_table_t *protocol_table, int instances,
			int task_inx);
static slurm_nrt_libstate_t *_alloc_libstate(void);
static slurm_nrt_nodeinfo_t *_alloc_node(slurm_nrt_libstate_t *lp, char *name);
static int	_copy_node(slurm_nrt_nodeinfo_t *dest,
			   slurm_nrt_nodeinfo_t *src);
static int	_fake_unpack_adapters(Buf buf, slurm_nrt_nodeinfo_t *n,
				      uint16_t protocol_version);
static int	_fill_in_adapter_cache(void);
static slurm_nrt_nodeinfo_t *
		_find_node(slurm_nrt_libstate_t *lp, char *name);
static slurm_nrt_window_t *
		_find_window(slurm_nrt_adapter_t *adapter, uint16_t window_id);
static slurm_nrt_window_t *_find_free_window(slurm_nrt_adapter_t *adapter);
static slurm_nrt_nodeinfo_t *_find_node(slurm_nrt_libstate_t *lp, char *name);
static bool	_free_block_use(slurm_nrt_jobinfo_t *jp,
				slurm_nrt_adapter_t *adapter);
static void	_free_libstate(slurm_nrt_libstate_t *lp);
static int	_get_adapters(slurm_nrt_nodeinfo_t *n);
static int	_get_my_id(void);
static void	_hash_add_nodeinfo(slurm_nrt_libstate_t *state,
				   slurm_nrt_nodeinfo_t *node);
static int	_hash_index(char *name);
static void	_hash_rebuild(slurm_nrt_libstate_t *state);
static void	_init_adapter_cache(void);
static preemption_state_t _job_preempt_state(nrt_job_key_t job_key);
static int	_job_step_window_state(slurm_nrt_jobinfo_t *jp,
				       hostlist_t hl, win_state_t state);
static int	_load_min_window_id(char *adapter_name,
				    nrt_adapter_t adapter_type);
static nrt_job_key_t _next_key(void);
static int	_pack_libstate(slurm_nrt_libstate_t *lp, Buf buffer,
			       uint16_t protocol_version);
static void	_pack_tableinfo(nrt_tableinfo_t *tableinfo, Buf buf,
				slurm_nrt_jobinfo_t *jp,
				uint16_t protocol_version);
static char *	_state_str(win_state_t state);
static int	_unload_window_all_jobs(char *adapter_name,
					nrt_adapter_t adapter_type,
					nrt_window_id_t window_id);
static int	_unpack_libstate(slurm_nrt_libstate_t *lp, Buf buffer);
static int	_unpack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf,
				 bool believe_window_status,
				 uint16_t protocol_version);
static int	_unpack_tableinfo(nrt_tableinfo_t *tableinfo,
				  Buf buf, slurm_nrt_jobinfo_t *jp,
				  uint16_t protocol_version);
static int	_wait_for_all_windows(nrt_tableinfo_t *tableinfo);
static int	_wait_for_window_unloaded(char *adapter_name,
					  nrt_adapter_t adapter_type,
					  nrt_window_id_t window_id,
					  int retry);
static int	_wait_job(nrt_job_key_t job_key,preemption_state_t want_state,
			  int max_wait_secs);
static char *	_win_state_str(win_state_t state);
static int	_window_state_set(slurm_nrt_jobinfo_t *jp, char *hostname,
				  win_state_t state);

/* The lid caching functions were created to avoid unnecessary
 * function calls each time we need to load network tables on a node.
 * _init_cache() simply initializes the cache to save values and
 * needs to be called before any other cache functions are called.
 *
 * Used by: slurmd/slurmstepd
 */
static void
_init_adapter_cache(void)
{
	lid_cache_size = 0;
}

/* Use nrt_adapter_resources to cache information about local adapters.
 *
 * Used by: slurmstepd
 */
static int
_fill_in_adapter_cache(void)
{
	int err, i, j, rc = SLURM_SUCCESS;
	nrt_cmd_query_adapter_types_t adapter_types;
	unsigned int num_adapter_types;
	nrt_adapter_t adapter_type[NRT_MAX_ADAPTER_TYPES];
	nrt_cmd_query_adapter_names_t adapter_names;
	unsigned int max_windows, num_adapter_names;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("_fill_in_adapter_cache: begin");

	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	for (i = 0; i < 2; i++) {
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				   &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		error("Is pnsd daemon started? Retrying...");
		/* Run "/opt/ibmhpc/pecurrent/ppe.pami/pnsd/pnsd -A" */
		sleep(5);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}

	for (i = 0; i < num_adapter_types; i++) {
		if (debug_flags & DEBUG_FLAG_SWITCH)
			info("adapter_type[%d]: %u", i, adapter_type[i]);

		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				   &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_cmd_wrap(adapter_names, %u): %s",
			      adapter_names.adapter_type, nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
		for (j = 0; j < num_adapter_names; j++) {
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("adapter_names[%d]: %s",
				     j, adapter_names.adapter_names[j]);
			}
			lid_cache[lid_cache_size].adapter_type = adapter_names.
								 adapter_type;
			strlcpy(lid_cache[lid_cache_size].adapter_name,
				adapter_names.adapter_names[j],
				NRT_MAX_ADAPTER_NAME_LEN);
			lid_cache_size++;
		}
	}
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("_fill_in_adapter_cache: complete: %d", rc);

	return rc;
}

/* The idea behind keeping the hash table was to avoid a linear
 * search of the node list each time we want to retrieve or
 * modify a node's data.  The _hash_index function translates
 * a node name to an index into the hash table.
 *
 * Used by: slurmctld
 */
static int
_hash_index(char *name)
{
	int index = 0;
	int j;

	xassert(name);

	/* Multiply each character by its numerical position in the
	 * name string to add a bit of entropy, because host names such
	 * as cluster[0001-1000] can cause excessive index collisions.
	 */
	for (j = 1; *name; name++, j++)
		index += (int)*name * j;
	index %= nrt_state->hash_max;

	return index;
}

/* Tries to find a node fast using the hash table
 *
 * Used by: slurmctld
 */
static slurm_nrt_nodeinfo_t *
_find_node(slurm_nrt_libstate_t *lp, char *name)
{
	int i;
	slurm_nrt_nodeinfo_t *n;
	struct node_record *node_ptr;

	xassert(name);
	xassert(lp);

	if (lp->node_count == 0)
		return NULL;

	if (lp->hash_table) {
		i = _hash_index(name);
		n = lp->hash_table[i];
		while (n) {
			xassert(n->magic == NRT_NODEINFO_MAGIC);
			if (!xstrncmp(n->name, name, NRT_HOSTLEN))
				return n;
			n = n->next;
		}
	}

	/* This code is only needed if NodeName and NodeHostName differ */
	node_ptr = find_node_record(name);
	if (node_ptr && lp->hash_table) {
		i = _hash_index(node_ptr->node_hostname);
		n = lp->hash_table[i];
		while (n) {
			xassert(n->magic == NRT_NODEINFO_MAGIC);
			if (!xstrncmp(n->name, node_ptr->node_hostname,
				      NRT_HOSTLEN))
				return n;
			n = n->next;
		}
	}

	return NULL;
}

/* Add the hash entry for a newly created slurm_nrt_nodeinfo_t
 */
static void
_hash_add_nodeinfo(slurm_nrt_libstate_t *state, slurm_nrt_nodeinfo_t *node)
{
	int index;

	xassert(state);
	xassert(state->hash_table);
	xassert(state->hash_max >= state->node_count);
	if (!node->name[0])
		return;
	index = _hash_index(node->name);
	node->next = state->hash_table[index];
	state->hash_table[index] = node;
}

/* Recreates the hash table for the node list.
 *
 * Used by: slurmctld
 */
static void
_hash_rebuild(slurm_nrt_libstate_t *state)
{
	int i;

	xassert(state);

	if (state->hash_table)
		xfree(state->hash_table);
	if ((state->node_count > state->hash_max) || (state->hash_max == 0))
		state->hash_max += NRT_HASHCOUNT;
	state->hash_table = (slurm_nrt_nodeinfo_t **)
			    xmalloc(sizeof(slurm_nrt_nodeinfo_t *) *
			    state->hash_max);
	for (i = 0; i < state->node_count; i++)
		_hash_add_nodeinfo(state, &(state->node_list[i]));
}

static slurm_nrt_window_t *
_find_window(slurm_nrt_adapter_t *adapter, uint16_t window_id)
{
	int i;
	slurm_nrt_window_t *window;

	for (i = 0; i < adapter->window_count; i++) {
		window = &adapter->window_list[i];
		if (window->window_id == window_id)
			return window;
	}

	debug3("Unable to _find_window %hu on adapter %s",
	       window_id, adapter->adapter_name);
	return (slurm_nrt_window_t *) NULL;
}

/*
 * For one node, free all of the RDMA blocks and windows belonging to a
 * particular job step (as identified by the job_key).
 */
static void
_free_resources_by_job(slurm_nrt_jobinfo_t *jp, char *node_name)
{
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter;
	slurm_nrt_window_t *window;
	int i, j;

	/* debug3("_free_resources_by_job_key(%u, %s)", jp->job_key, node_name); */
	if ((node = _find_node(nrt_state, node_name)) == NULL)
		return;

	if (node->adapter_list == NULL) {
		error("switch/nrt: _free_resources_by_job, "
		      "adapter_list NULL for node %s", node_name);
		return;
	}
	for (i = 0; i < node->adapter_count; i++) {
		adapter = &node->adapter_list[i];

		(void) _free_block_use(jp, adapter);
		if (adapter->window_list == NULL) {
			error("switch/nrt: _free_resources_by_job, "
			      "window_list NULL for node %s adapter %s",
			      node->name, adapter->adapter_name);
			continue;
		}
		/* We could check here to see if this adapter's name
		 * is in the nrt_jobinfo tablinfo list to avoid the next
		 * loop if the adapter isn't in use by the job step.
		 * However, the added searching and string comparisons
		 * probably aren't worth it, especially since MOST job
		 * steps will use all of the adapters.
		 */
		for (j = 0; j < adapter->window_count; j++) {
			window = &adapter->window_list[j];

			if (window->job_key == jp->job_key) {
				/* debug3("Freeing adapter %s window %d",
				   adapter->name, window->id); */
				window->state = NRT_WIN_AVAILABLE;
				window->job_key = 0;
				if (jp->immed_slots >
				    adapter->immed_slots_used) {
					error("switch/nrt: immed_slots_used "
					      "underflow");
					adapter->immed_slots_used = 0;
				} else {
					adapter->immed_slots_used -=
						jp->immed_slots;
				}
			}
		}
	}
}

/*
 * Find all of the windows used by this job step and set their
 * status to "state".
 *
 * Used by: slurmctld
 */
static int
_job_step_window_state(slurm_nrt_jobinfo_t *jp, hostlist_t hl,
		       win_state_t state)
{
	hostlist_iterator_t hi;
	char *host;
	int err, rc = SLURM_SUCCESS;

	xassert(!hostlist_is_empty(hl));

	if ((jp == NULL) || (jp->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_ERROR;
	}

	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if ((jp == NULL) || (hostlist_is_empty(hl)))
		return SLURM_ERROR;

	if ((jp->tables_per_task == 0) || (jp->tableinfo == NULL) ||
	    (jp->tableinfo[0].table_length == 0) || (!jp->user_space))
		return SLURM_SUCCESS;

	hi = hostlist_iterator_create(hl);
	slurm_mutex_lock(&global_lock);
	while ((host = hostlist_next(hi))) {
		err = _window_state_set(jp, host, state);
		rc = MAX(rc, err);
		free(host);
	}
	slurm_mutex_unlock(&global_lock);
	hostlist_iterator_destroy(hi);

	return rc;
}

static char *_state_str(win_state_t state)
{
	if (state == NRT_WIN_UNAVAILABLE)
		return "Unavailable";
	if (state == NRT_WIN_INVALID)
		return "Invalid";
	if (state == NRT_WIN_AVAILABLE)
		return "Available";
	if (state == NRT_WIN_RESERVED)
		return "Reserved";
	if (state == NRT_WIN_READY)
		return "Ready";
	if (state == NRT_WIN_RUNNING)
		return "Running";
	return "Unknown";
}

/* Find the correct NRT structs and set the state
 * of the switch windows for the specified task_id.
 *
 * Used by: slurmctld
 */
static int
_window_state_set(slurm_nrt_jobinfo_t *jp, char *hostname, win_state_t state)
{
	slurm_nrt_nodeinfo_t *node = NULL;
	slurm_nrt_adapter_t *adapter = NULL;
	slurm_nrt_window_t *window = NULL;
	int i, j;
	int rc = SLURM_SUCCESS;
	bool adapter_found;
	uint16_t win_id = 0;
	nrt_job_key_t job_key = jp->job_key;
	nrt_table_id_t table_cnt = jp->tables_per_task;
	nrt_tableinfo_t *tableinfo = jp->tableinfo;
	nrt_task_id_t task_id;

	xassert(tableinfo);
	xassert(hostname);

	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}
	if (node->adapter_list == NULL) {
		error("Found node, but adapter_list is NULL");
		return SLURM_ERROR;
	}

	for (i = 0; i < table_cnt; i++) {
		if (tableinfo[i].table == NULL) {
			error("tableinfo[%d].table is NULL", i);
			rc = SLURM_ERROR;
			continue;
		}

		adapter_found = false;
		/* Find the adapter that matches the one in tableinfo */
		for (j = 0; j < node->adapter_count; j++) {
			adapter = &node->adapter_list[j];
			if (xstrcasecmp(adapter->adapter_name,
					tableinfo[i].adapter_name))
				continue;
			for (task_id = 0; task_id < tableinfo[i].table_length;
			     task_id++) {
				if (adapter->adapter_type == NRT_IB) {
					nrt_ib_task_info_t *ib_tbl_ptr;
					ib_tbl_ptr  = tableinfo[i].table;
					ib_tbl_ptr += task_id;
					if (ib_tbl_ptr == NULL) {
						error("tableinfo[%d].table[%d]"
						      " is NULL", i, task_id);
						rc = SLURM_ERROR;
						continue;
					}
					if (adapter->lid ==
					    ib_tbl_ptr->base_lid) {
						adapter_found = true;
						win_id = ib_tbl_ptr->win_id;
						debug3("Setting status %s "
						       "adapter %s lid %hu "
						       "window %hu for task %d",
						       _state_str(state),
						       adapter->adapter_name,
						       ib_tbl_ptr->base_lid,
						       ib_tbl_ptr->win_id,
						       task_id);
					}
				} else if (adapter->adapter_type == NRT_HFI) {
					nrt_hfi_task_info_t *hfi_tbl_ptr;
					hfi_tbl_ptr  = tableinfo[i].table;
					hfi_tbl_ptr += task_id;
					if (hfi_tbl_ptr == NULL) {
						error("tableinfo[%d].table[%d]"
						      " is NULL", i, task_id);
						rc = SLURM_ERROR;
						continue;
					}
					if (adapter->lid == hfi_tbl_ptr->lid) {
						adapter_found = true;
						win_id = hfi_tbl_ptr->win_id;
						debug3("Setting status %s "
						       "adapter %s lid %hu "
						       "window %hu for task %d",
						       _state_str(state),
						       adapter->adapter_name,
						       hfi_tbl_ptr->lid,
						       hfi_tbl_ptr->win_id,
						       task_id);
					}
				}
#if NRT_VERSION < 1300
				else if ((adapter->adapter_type==NRT_HPCE) ||
					   (adapter->adapter_type==NRT_KMUX)) {
					nrt_hpce_task_info_t *hpce_tbl_ptr;
					hpce_tbl_ptr  = tableinfo[i].table;
					hpce_tbl_ptr += task_id;
					if (hpce_tbl_ptr == NULL) {
						error("tableinfo[%d].table[%d]"
						      " is NULL", i, task_id);
						rc = SLURM_ERROR;
						continue;
					}
					if (adapter->network_id ==
					    tableinfo[i].network_id) {
						adapter_found = true;
						win_id = hpce_tbl_ptr->win_id;
						debug3("Setting status %s "
						       "adapter %s window %hu "
						       "for task %d",
						       _state_str(state),
						       adapter->adapter_name,
						       hpce_tbl_ptr->win_id,
						       task_id);
					}
				}
#endif
				else {
					error("switch/nrt: _window_state_set:"
					      " Missing support for adapter "
					      "type %s",
					      _adapter_type_str(adapter->
								adapter_type));
				}

				window = _find_window(adapter, win_id);
				if (window) {
					window->state = state;
					if (state == NRT_WIN_UNAVAILABLE) {
						window->job_key = job_key;
						adapter->immed_slots_used +=
							jp->immed_slots;
					} else
						window->job_key = 0;
				}
			}  /* for each task */
			if (adapter_found) {
				_add_block_use(jp, adapter);
			} else {
				error("switch/nrt: Did not find adapter %s of "
				      "type %s with lid %hu ",
				      adapter->adapter_name,
				      _adapter_type_str(adapter->adapter_type),
				      adapter->lid);
				rc = SLURM_ERROR;
				continue;
			}
		}  /* for each adapter */
	}  /* for each table */

	return rc;
}

/* If the node is already in the node list then simply return
 * a pointer to it, otherwise dynamically allocate memory to the
 * node list if necessary.
 *
 * Used by: slurmctld
 */
static slurm_nrt_nodeinfo_t *
_alloc_node(slurm_nrt_libstate_t *lp, char *name)
{
	slurm_nrt_nodeinfo_t *n = NULL;
	int new_bufsize;
	bool need_hash_rebuild = false;

	xassert(lp);

	if (name != NULL) {
		n = _find_node(lp, name);
		if (n != NULL)
			return n;
	}

	nrt_need_state_save = true;

	if (lp->node_count >= lp->node_max) {
		lp->node_max += NRT_NODECOUNT;
		new_bufsize = lp->node_max * sizeof(slurm_nrt_nodeinfo_t);
		if (lp->node_list == NULL) {
			lp->node_list = (slurm_nrt_nodeinfo_t *)
					xmalloc(new_bufsize);
		} else {
			lp->node_list = (slurm_nrt_nodeinfo_t *)
					xrealloc(lp->node_list, new_bufsize);
		}
		need_hash_rebuild = true;
	}
	if (lp->node_list == NULL) {
		slurm_seterrno(ENOMEM);
		return NULL;
	}

	n = lp->node_list + (lp->node_count++);
	n->magic = NRT_NODEINFO_MAGIC;
	n->name[0] = '\0';
	n->adapter_list = (slurm_nrt_adapter_t *)
			  xmalloc(NRT_MAXADAPTERS *
			  sizeof(struct slurm_nrt_adapter));

	if (name != NULL) {
		strlcpy(n->name, name, NRT_HOSTLEN);
		if (need_hash_rebuild || (lp->node_count > lp->hash_max))
			_hash_rebuild(lp);
		else
			_hash_add_nodeinfo(lp, n);
	}

	return n;
}

static slurm_nrt_window_t *
_find_free_window(slurm_nrt_adapter_t *adapter)
{
	static int last_inx = 0;
	slurm_nrt_window_t *window;
	int i;

	for (i = 0; i < adapter->window_count; i++, last_inx++) {
		if (last_inx >= adapter->window_count)
			last_inx = 0;
		window = &adapter->window_list[last_inx];
		if (window->state == NRT_WIN_AVAILABLE)
			return window;
	}

	slurm_seterrno(ESLURM_INTERCONNECT_BUSY);
	return (slurm_nrt_window_t *) NULL;
}

static void _table_alloc(nrt_tableinfo_t *tableinfo, int table_inx,
			 nrt_adapter_t adapter_type)
{
	int table_size;

	if (tableinfo[table_inx].table)
		return;
	if (adapter_type == NRT_IB)
		table_size = sizeof(nrt_ib_task_info_t);
	else if (adapter_type == NRT_HFI)
		table_size = sizeof(nrt_hfi_task_info_t);
	else if (adapter_type == NRT_IPONLY)
		table_size = sizeof(nrt_ip_task_info_t);
#if NRT_VERSION < 1300
	else if ((adapter_type == NRT_HPCE) || (adapter_type == NRT_KMUX))
		table_size = sizeof(nrt_hpce_task_info_t);
#endif
	else {
		error("Missing support for adapter type %s",
		      _adapter_type_str(adapter_type));
		return;
	}
	tableinfo[table_inx].table = xmalloc(table_size *
					     tableinfo[table_inx].
					     table_length);
	return;
}

/* Track RDMA or CAU resources allocated to a job on each adapter */
static int
_add_block_use(slurm_nrt_jobinfo_t *jp, slurm_nrt_adapter_t *adapter)
{
	int i;
	slurm_nrt_block_t *block_ptr, *free_block;
	nrt_cau_index_t new_cau_count = 0;
	uint64_t new_rcontext_blocks  = 0;

	/*
	 * Validate sufficient CAU resources
	 *
	 * From Bill LePera, IBM, July 14, 2012:
	 * CAU indexes on HFI are allocated on a job-context basis.  That means
	 * the CAU indexes are shared among tables with the same job key and
	 * context ID.  In this scenario you would set the total number of CAU
	 * indexes desired in the num_cau_indexs field for all the tables with
	 * the same job key and context ID, but PNSD will only allocate that
	 * number one time for all the tables.  For example, If job key 1234,
	 * context ID 0 is striped across four networks, it will have four
	 * NRTs.  If that job requests 2 CAU indexes, the num_cau_indexes field
	 * in each NRT should be set to 2.  However, PNSD will only allocate 2
	 * indexes for that job.
	 */
	if (jp->cau_indexes) {
		if (adapter->cau_indexes_avail < jp->cau_indexes) {
			info("switch/nrt: Insufficient cau_indexes resources "
			     "on adapter %s (%hu < %hu)",
			     adapter->adapter_name, adapter->cau_indexes_avail,
			     jp->cau_indexes);
			return SLURM_ERROR;
		}
		new_cau_count = adapter->cau_indexes_used + jp->cau_indexes;
		if (adapter->cau_indexes_avail < new_cau_count) {
			info("switch/nrt: Insufficient cau_indexes resources "
			     "available on adapter %s (%hu < %hu)",
			     adapter->adapter_name, adapter->cau_indexes_avail,
			     new_cau_count);
			slurm_seterrno(ESLURM_INTERCONNECT_BUSY);
			return SLURM_ERROR;
		}
	}

	/* Validate sufficient RDMA resources */
	if (jp->bulk_xfer && jp->bulk_xfer_resources) {
		if (adapter->rcontext_block_count < jp->bulk_xfer_resources) {
			info("switch/nrt: Insufficient bulk_xfer resources on "
			     "adapter %s (%"PRIu64" < %u)",
			     adapter->adapter_name,
			     adapter->rcontext_block_count,
			     jp->bulk_xfer_resources);
			return SLURM_ERROR;
		}
		new_rcontext_blocks = adapter->rcontext_block_used +
				      jp->bulk_xfer_resources;
		if (adapter->rcontext_block_count < new_rcontext_blocks) {
			info("switch/nrt: Insufficient bulk_xfer resources "
			     "available on adapter %s (%"PRIu64" < %"PRIu64")",
			     adapter->adapter_name,
			     adapter->rcontext_block_count,
			     new_rcontext_blocks);
			slurm_seterrno(ESLURM_INTERCONNECT_BUSY);
			return SLURM_ERROR;
		}
	} else {
		jp->bulk_xfer_resources = 0;	/* match jp->bulk_xfer */
	}

	if ((new_cau_count == 0) && (new_rcontext_blocks == 0))
		return SLURM_SUCCESS;	/* No work */

	/* Add job_key to our table and update the resource used information */
	free_block = NULL;
	block_ptr = adapter->block_list;
	for (i = 0; i < adapter->block_count; i++, block_ptr++) {
		if (block_ptr->job_key == jp->job_key) {
			free_block = block_ptr;
			break;
		} else if ((block_ptr->job_key == 0) && (free_block == 0)) {
			free_block = block_ptr;
		}
	}
	if (free_block == NULL) {
		xrealloc(adapter->block_list,
			 sizeof(slurm_nrt_block_t) *
				 (adapter->block_count + 8));
		free_block = adapter->block_list + adapter->block_count;
		adapter->block_count += 8;
	}

	free_block->job_key = jp->job_key;
	free_block->rcontext_block_use = jp->bulk_xfer_resources;
	if (new_cau_count)
		adapter->cau_indexes_used    = new_cau_count;
	if (new_rcontext_blocks)
		adapter->rcontext_block_used = new_rcontext_blocks;

#if 0
	block_ptr = adapter->block_list;
	for (i = 0; i < adapter->block_count; i++, block_ptr++) {
		if (block_ptr->job_key) {
			info("adapter:%s block:%d job_key:%u blocks:%u",
			     adapter->adapter_name, i, block_ptr->job_key,
			     free_block->rcontext_block_use);
		}
	}
#endif
	return SLURM_SUCCESS;
}

static int _add_immed_use(char *hostname, slurm_nrt_jobinfo_t *jp,
			  slurm_nrt_adapter_t *adapter)
{
	if (adapter->immed_slots_avail < jp->immed_slots) {
		info("switch/nrt: Insufficient immediate slots on "
		     "node %s adapter %s",
		     hostname, adapter->adapter_name);
		return SLURM_ERROR;
	}

	adapter->immed_slots_used += jp->immed_slots;
	if (adapter->immed_slots_avail < adapter->immed_slots_used) {
		info("switch/nrt: Insufficient immediate slots available on "
		     "node %s adapter %s",
		     hostname, adapter->adapter_name);
		adapter->immed_slots_used -= jp->immed_slots;
		slurm_seterrno(ESLURM_INTERCONNECT_BUSY);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static bool
_free_block_use(slurm_nrt_jobinfo_t *jp, slurm_nrt_adapter_t *adapter)
{
	slurm_nrt_block_t *block_ptr;
	bool found_job = false;
	int i;

	if ((jp->bulk_xfer && jp->bulk_xfer_resources) || jp->cau_indexes) {
		block_ptr = adapter->block_list;
		for (i = 0; i < adapter->block_count; i++, block_ptr++) {
			if (block_ptr->job_key != jp->job_key)
				continue;

			if (jp->cau_indexes > adapter->cau_indexes_used) {
				error("switch/nrt: cau_indexes_used underflow");
				adapter->cau_indexes_used = 0;
			} else {
				adapter->cau_indexes_used -= jp->cau_indexes;
			}

			if (block_ptr->rcontext_block_use >
			    adapter->rcontext_block_used) {
				error("switch/nrt: rcontext_block_used "
				      "underflow");
				adapter->rcontext_block_used = 0;
			} else {
				adapter->rcontext_block_used -=
					block_ptr->rcontext_block_use;
			}
			block_ptr->job_key = 0;
			block_ptr->rcontext_block_use = 0;
			found_job = true;
			break;
		}
	}

	return found_job;
}

/* For a given process, fill out an nrt_creator_per_task_input_t
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign adapters, lids and switch windows to
 * each task in a job.
 *
 * Used by: slurmctld
 */
static int
_allocate_windows_all(slurm_nrt_jobinfo_t *jp, char *hostname,
		      uint32_t node_id, nrt_task_id_t task_id,
		      nrt_adapter_t adapter_type, int network_id,
		      nrt_protocol_table_t *protocol_table, int instances,
		      int task_inx)
{
	nrt_tableinfo_t *tableinfo = jp->tableinfo;
	nrt_job_key_t job_key = jp->job_key;
	bool ip_v4 = jp->ip_v4;
	bool user_space = jp->user_space;
	nrt_node_number_t node_number;
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter;
	slurm_nrt_window_t *window;
	nrt_context_id_t context_id;
	nrt_table_id_t table_id;
	int i, j, table_inx;

	xassert(tableinfo);
	xassert(hostname);

	debug2("in _allocate_windows_all");
	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}

	/* From Bill LePera, IBM, 4/18/2012:
	 * The node_number field is normally set to the 32-bit IPv4 address
	 * of the local node's host name. */
	node_number = node->node_number;

	/* Reserve a window on each adapter for this task */
	table_inx = -1;
	for (context_id = 0; context_id < protocol_table->protocol_table_cnt;
	     context_id++) {
		table_id = -1;
		for (i = 0; i < node->adapter_count; i++) {
			adapter = &node->adapter_list[i];
			if ((adapter_type != NRT_MAX_ADAPTER_TYPES) &&
			    (adapter->adapter_type != adapter_type))
				continue;
//			if ((network_id >= 0) &&
//			    (adapter->network_id != network_id))
//				continue;
			if (user_space &&
			    (adapter->adapter_type == NRT_IPONLY))
				continue;
			if ((context_id == 0) && (task_inx == 0) &&
			    (_add_block_use(jp, adapter))) {
				goto alloc_fail;
			}
			for (j = 0; j < instances; j++) {
				table_id++;
				table_inx++;
				if (table_inx >= jp->tables_per_task) {
					error("switch/nrt: adapter count too "
					      "high, host=%s", hostname);
					goto alloc_fail;
				}
				if (user_space) {
					window = _find_free_window(adapter);
					if (window == NULL) {
						info("switch/nrt: "
						      "No free windows on "
						     "node %s adapter %s",
						     node->name,
						     adapter->adapter_name);
						goto alloc_fail;
					}
					if (_add_immed_use(hostname, jp,
							   adapter))
						goto alloc_fail;
					window->state = NRT_WIN_UNAVAILABLE;
					window->job_key = job_key;
				}

				if (!user_space) {
					nrt_ip_task_info_t *ip_table;
					_table_alloc(tableinfo, table_inx,
						     NRT_IPONLY);
					ip_table = (nrt_ip_task_info_t *)
						   tableinfo[table_inx].table;
					ip_table += task_id;
					ip_table->node_number  = node_number;
					ip_table->task_id      = task_id;
					if (ip_v4) {
						memcpy(&ip_table->ip.ipv4_addr,
						       &adapter->ipv4_addr,
						       sizeof(in_addr_t));
					} else {
						memcpy(&ip_table->ip.ipv6_addr,
						       &adapter->ipv6_addr,
						       sizeof(struct in6_addr));
					}
				} else if (adapter->adapter_type == NRT_IB) {
					nrt_ib_task_info_t *ib_table;
					_table_alloc(tableinfo, table_inx,
						     adapter->adapter_type);
					ib_table = (nrt_ib_task_info_t *)
						   tableinfo[table_inx].table;
					ib_table += task_id;
					strlcpy(ib_table->device_name,
						adapter->adapter_name,
						NRT_MAX_DEVICENAME_SIZE);
					ib_table->base_lid = adapter->lid;
					ib_table->port_id  = 1;
					ib_table->lmc      = 0;
					ib_table->node_number = node_number;
					ib_table->task_id  = task_id;
					ib_table->win_id   = window->window_id;
				} else if (adapter->adapter_type == NRT_HFI) {
					nrt_hfi_task_info_t *hfi_table;
					_table_alloc(tableinfo, table_inx,
						     adapter->adapter_type);
					hfi_table = (nrt_hfi_task_info_t *)
						    tableinfo[table_inx].table;
					hfi_table += task_id;
					hfi_table->lid = adapter->lid;
					hfi_table->lpar_id = adapter->special;
					hfi_table->task_id = task_id;
					hfi_table->win_id = window->window_id;
				}
#if NRT_VERSION < 1300
				else if ((adapter->adapter_type == NRT_HPCE)||
					   (adapter->adapter_type == NRT_KMUX)){
					nrt_hpce_task_info_t *hpce_table;
					_table_alloc(tableinfo, table_inx,
						     adapter->adapter_type);
					hpce_table = (nrt_hpce_task_info_t *)
						     tableinfo[table_inx].table;
					hpce_table += task_id;
					hpce_table->node_number = node_number;
					hpce_table->task_id = task_id;
					hpce_table->win_id = window->window_id;
				}
#endif
				else {
					error("switch/nrt: Missing support "
					      "for adapter type %s",
					      _adapter_type_str(adapter->
								adapter_type));
					goto alloc_fail;
				}

				strlcpy(tableinfo[table_inx].adapter_name,
					adapter->adapter_name,
					NRT_MAX_ADAPTER_NAME_LEN);
				tableinfo[table_inx].adapter_type = adapter->
								    adapter_type;
				tableinfo[table_inx].network_id = adapter->
								  network_id;
				strlcpy(tableinfo[table_inx].protocol_name,
					protocol_table->
					protocol_table[context_id].
					protocol_name,
					NRT_MAX_PROTO_NAME_LEN);
				tableinfo[table_inx].context_id = context_id;
				tableinfo[table_inx].instance   = j + 1;
				tableinfo[table_inx].table_id   = table_id;
			}  /* for each table */
		}  /* for each context */
	}  /* for each adapter */

	if (++table_inx < jp->tables_per_task) {
		/* This node has too few adapters of this type */
		error("switch/nrt: adapter count too low, host=%s", hostname);
		drain_nodes(hostname, "Too few switch adapters", 0);
		goto alloc_fail;
	}

	return SLURM_SUCCESS;

alloc_fail:
	/* Unable to allocate all necessary resources.
	 * Free what has been allocated so far. */
	_free_resources_by_job(jp, hostname);
	return SLURM_ERROR;
}


/* For a given process, fill out an nrt_creator_per_task_input_t
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign a single adapter, lid and switch window to
 * a task in a job.
 *
 * Used by: slurmctld
 */
static int
_allocate_window_single(char *adapter_name, slurm_nrt_jobinfo_t *jp,
			char *hostname, uint32_t node_id,
			nrt_task_id_t task_id, nrt_adapter_t adapter_type,
			int network_id, nrt_protocol_table_t *protocol_table,
		        int instances, int task_inx)
{
	nrt_tableinfo_t *tableinfo = jp->tableinfo;
	nrt_job_key_t job_key = jp->job_key;
	bool ip_v4 = jp->ip_v4;
	bool user_space = jp->user_space;
	nrt_node_number_t node_number;
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter = NULL;
	slurm_nrt_window_t *window;
	int i, table_inx;
	nrt_context_id_t context_id;
	nrt_table_id_t table_id;

	xassert(tableinfo);
	xassert(hostname);

	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("switch/nrt: Failed to find node in node_list: %s",
		      hostname);
		return SLURM_ERROR;
	}
	if (!adapter_name)	/* Fix CLANG false positive */
		return SLURM_ERROR;

	/* From Bill LePera, IBM, 4/18/2012:
	 * The node_number field is normally set to the 32-bit IPv4 address
	 * of the local node's host name. */
	node_number = node->node_number;

	/* find the adapter */
	for (i = 0; i < node->adapter_count; i++) {
		debug2("adapter %s at index %d",
		       node->adapter_list[i].adapter_name, i);
		if (adapter_name) {
			if (!xstrcasecmp(node->adapter_list[i].adapter_name,
					 adapter_name)) {
				adapter = &node->adapter_list[i];
				break;
			}
			continue;
		}
		if ((adapter_type != NRT_MAX_ADAPTER_TYPES) &&
		    (node->adapter_list[i].adapter_type == adapter_type)) {
			adapter = &node->adapter_list[i];
			break;
		}
	}
	if (adapter == NULL) {
		info("switch/nrt: Failed to find adapter %s of type %s on "
		     "node %s",
		     adapter_name, _adapter_type_str(adapter_type), hostname);
		return SLURM_ERROR;
	}

	table_inx = -1;
	for (context_id = 0; context_id < protocol_table->protocol_table_cnt;
	     context_id++) {
		if ((context_id == 0) && (task_inx == 0) &&
		    (_add_block_use(jp, adapter))) {
			goto alloc_fail;
		}
		for (table_id = 0; table_id < instances; table_id++) {
			table_inx++;
			if (user_space) {
				/* Reserve a window on the adapter for task */
				window = _find_free_window(adapter);
				if (window == NULL) {
					info("switch/nrt: No free windows "
					     "on node %s adapter %s",
					     node->name,
					     adapter->adapter_name);
					goto alloc_fail;
				}
				if (_add_immed_use(hostname, jp, adapter))
					goto alloc_fail;
				window->state = NRT_WIN_UNAVAILABLE;
				window->job_key = job_key;
			}

			if (!user_space) {
				nrt_ip_task_info_t *ip_table;
				_table_alloc(tableinfo, table_inx, NRT_IPONLY);
				ip_table = (nrt_ip_task_info_t *)
					   tableinfo[table_inx].table;
				ip_table += task_id;
				ip_table->node_number  = node_number;
				ip_table->task_id      = task_id;
				if (ip_v4) {
					memcpy(&ip_table->ip.ipv4_addr,
					       &adapter->ipv4_addr,
					       sizeof(in_addr_t));
				} else {
					memcpy(&ip_table->ip.ipv6_addr,
					       &adapter->ipv6_addr,
					       sizeof(struct in6_addr));
				}
			} else if (adapter_type == NRT_IB) {
				nrt_ib_task_info_t *ib_table;
				_table_alloc(tableinfo, table_inx,
					     adapter_type);
				ib_table = (nrt_ib_task_info_t *)
					   tableinfo[table_inx].table;
				ib_table += task_id;
				strlcpy(ib_table->device_name, adapter_name,
					NRT_MAX_DEVICENAME_SIZE);
				ib_table->base_lid = adapter->lid;
				ib_table->port_id  = 1;
				ib_table->lmc      = 0;
				ib_table->node_number = node_number;
				ib_table->task_id  = task_id;
				ib_table->win_id   = window->window_id;
			} else if (adapter_type == NRT_HFI) {
				nrt_hfi_task_info_t *hfi_table;
				_table_alloc(tableinfo, table_inx,
					     adapter_type);
				hfi_table = (nrt_hfi_task_info_t *)
					    tableinfo[table_inx].table;
				hfi_table += task_id;
				hfi_table->lid = adapter->lid;
				hfi_table->lpar_id = adapter->special;
				hfi_table->task_id = task_id;
				hfi_table->win_id = window->window_id;
			}
#if NRT_VERSION < 1300
			else if ((adapter_type == NRT_HPCE) ||
				   (adapter_type == NRT_KMUX)) {
				nrt_hpce_task_info_t *hpce_table;
				_table_alloc(tableinfo, table_inx,
					     adapter_type);
				hpce_table = (nrt_hpce_task_info_t *)
					     tableinfo[table_inx].table;
				hpce_table += task_id;
				hpce_table->task_id = task_id;
				hpce_table->win_id = window->window_id;
			}
#endif
			else {
				error("Missing support for adapter type %s",
				      _adapter_type_str(adapter_type));
				goto alloc_fail;
			}

			strlcpy(tableinfo[table_inx].adapter_name, adapter_name,
				NRT_MAX_ADAPTER_NAME_LEN);
			tableinfo[table_inx].adapter_type = adapter->
							    adapter_type;
			tableinfo[table_inx].network_id = adapter->network_id;
			strlcpy(tableinfo[table_inx].protocol_name,
				protocol_table->protocol_table[context_id].
				protocol_name,
				NRT_MAX_PROTO_NAME_LEN);
			tableinfo[table_inx].context_id = context_id;
			tableinfo[table_inx].instance   = table_id + 1;
			tableinfo[table_inx].table_id   = table_id;
		}  /* for each table */
	}  /* for each context */

	return SLURM_SUCCESS;

alloc_fail:
	/* Unable to allocate all necessary resources.
	 * Free what has been allocated so far. */
	_free_resources_by_job(jp, hostname);
	return SLURM_ERROR;
}

static char *
_win_state_str(win_state_t state)
{
	if (state == NRT_WIN_UNAVAILABLE)
		return "Unavailable";
	else if (state == NRT_WIN_INVALID)
		return "Invalid";
	else if (state == NRT_WIN_AVAILABLE)
		return "Available";
	else if (state == NRT_WIN_RESERVED)
		return "Reserved";
	else if (state == NRT_WIN_READY)
		return "Ready";
	else if (state == NRT_WIN_RUNNING)
		return "Running";
	else {
		static char buf[16];
		snprintf(buf, sizeof(buf), "%d", state);
		return buf;
	}
}

static char *
_adapter_type_str(nrt_adapter_t type)
{
	static char buf[10];

	switch (type) {
	case NRT_IB:
		return "IB";
	case NRT_HFI:
		return "HFI";
	case NRT_IPONLY:
		return "IP_ONLY";
#if NRT_VERSION < 1300
	case NRT_HPCE:
		return "HPC_Ethernet";
	case NRT_KMUX:
		return "Kernel_Emulated_HPCE";
#endif
	default:
		snprintf(buf, sizeof(buf), "%d", type);
		return buf;
	}

	return NULL;	/* Never used */
}

static char *
_port_status_str(nrt_port_status_t status)
{
	if (status == 0)
		return "Down";
	else if (status == 1)
		return "Up";
	else if (status == 2)
		return "Unconfig";
	else {
		static char buf[16];
		snprintf(buf, sizeof(buf), "%d", status);
		return buf;
	}
}

/* Used by: slurmd */
static void
_print_adapter_info(nrt_adapter_info_t *adapter_info)
{
	char addr_v4_str[128], addr_v6_str[128];
	int i;

	if (!adapter_info) {
		error("_print_adapter_info with NULL adapter_info");
		return;
	}

	info("--Begin Adapter Info--");
	info("  adapter_name: %s", adapter_info->adapter_name);
	info("  adapter_type: %s",
	     _adapter_type_str(adapter_info->adapter_type));
	info("  cau_indexes_avail: %hu", adapter_info->cau_indexes_avail);
	info("  immed_slots_avail: %hu", adapter_info->immed_slots_avail);
	inet_ntop(AF_INET, &adapter_info->node_number,
		  addr_v4_str, sizeof(addr_v4_str));
	info("  node_number: %s", addr_v4_str);
	info("  num_ports: %hu", adapter_info->num_ports);
	info("  rcontext_block_count: %"PRIu64"",
	     adapter_info->rcontext_block_count);
	info("  window_count: %hu", adapter_info->window_count);
	for (i = 0; i < adapter_info->num_ports; i++) {
		inet_ntop(AF_INET,
			  &adapter_info->port[i].ipv4_addr,
			  addr_v4_str, sizeof(addr_v4_str));
		inet_ntop(AF_INET6,
			  &adapter_info->port[i].ipv6_addr,
			  addr_v6_str, sizeof(addr_v6_str));
		info("    port_id:%hu status:%s lid:%u "
		     "network_id:%lu special:%lu "
		     "ipv4_addr:%s ipv6_addr:%s/%hu",
		     adapter_info->port[i].port_id,
		     _port_status_str(adapter_info->port[i].status),
		     adapter_info->port[i].lid,
		     adapter_info->port[i].network_id,
		     adapter_info->port[i].special,
		     addr_v4_str, addr_v6_str,
		     adapter_info->port[i].ipv6_prefix_len);
	}
#if 0
	/* This always seems to count up from 0 to window_count-1 */
	for (i = 0; i < adapter_info->window_count; i++)
		info("    window_id: %u", adapter_info->window_list[i]);
#endif
	info("--End Adapter Info--");
}

/* Used by: slurmd */
static void
_print_adapter_status(nrt_cmd_status_adapter_t *status_adapter)
{
	int i;
	nrt_window_id_t window_cnt;
	nrt_status_t *status = *(status_adapter->status_array);
	char window_str[128];
	hostset_t hs;

	hs = hostset_create("");
	info("--Begin Adapter Status--");
	info("  adapter_name: %s", status_adapter->adapter_name);
	info("  adapter_type: %s",
	     _adapter_type_str(status_adapter->adapter_type));
	window_cnt = *(status_adapter->window_count);
	info("  window_count: %hu", window_cnt);
	info("  --------");
	for (i = 0; i < window_cnt; i++) {
		if ((status[i].state == NRT_WIN_AVAILABLE) &&
		    (i >= NRT_DEBUG_CNT)) {
			snprintf(window_str, sizeof(window_str), "%d",
				 status[i].window_id);
			hostset_insert(hs, window_str);
			continue;
		}
		info("  window_id: %hu", status[i].window_id);
		info("  state: %s", _win_state_str(status[i].state));
		if (status[i].state >= NRT_WIN_RESERVED) {
			info("  bulk_xfer: %hu", status[i].bulk_transfer);
			info("  client_pid: %u",
			     (uint32_t)status[i].client_pid);
			info("  rcontext_blocks: %u",
			     status[i].rcontext_blocks);
			info("  uid: %u", (uint32_t) status[i].uid);
		}
		info("  --------");
	}
	if (hostset_count(hs) > 0) {
		hostset_ranged_string(hs, sizeof(window_str), window_str);
		info("  suppress data for available windows %s", window_str);
		info("  --------");
	}
	info("--End Adapter Status--");
	hostset_destroy(hs);
}

/* Used by: slurmd, slurmctld */
static void
_print_nodeinfo(slurm_nrt_nodeinfo_t *n)
{
	int i, j;
	slurm_nrt_adapter_t *a;
	slurm_nrt_window_t *w;
	char addr_str[128];
	char window_str[128];
	hostset_t hs;

	xassert(n);
	xassert(n->magic == NRT_NODEINFO_MAGIC);

	info("--Begin Node Info--");
	info("  node: %s", n->name);
	inet_ntop(AF_INET, &n->node_number, addr_str,sizeof(addr_str));
	info("  node_number: %s", addr_str);
	info("  adapter_count: %u", n->adapter_count);
	for (i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		info("  adapter_name: %s", a->adapter_name);
		info("    adapter_type: %s",
		     _adapter_type_str(a->adapter_type));
		info("    cau_indexes_avail: %hu", a->cau_indexes_avail);
		if (a->cau_indexes_used) {
			/* This information is only available in Power7-IH
			 * and only in slurmctld's data structure */
			info("    cau_indexes_used:  %hu",a->cau_indexes_used);
		}
		info("    immed_slots_avail: %hu", a->immed_slots_avail);
		if (a->immed_slots_used) {
			/* This information is only available in Power7-IH
			 * and only in slurmctld's data structure */
			info("    immed_slots_used:  %hu",a->immed_slots_used);
		}
		inet_ntop(AF_INET, &a->ipv4_addr, addr_str, sizeof(addr_str));
		info("    ipv4_addr: %s", addr_str);
		inet_ntop(AF_INET6, &a->ipv6_addr, addr_str, sizeof(addr_str));
		info("    ipv6_addr: %s", addr_str);
		info("    lid: %u", a->lid);
		info("    network_id: %lu", a->network_id);

		info("    port_id: %hu", a->port_id);
		info("    rcontext_block_count: %"PRIu64"",
		     a->rcontext_block_count);
		info("    rcontext_block_used:  %"PRIu64"",
		     a->rcontext_block_used);
		info("    special: %lu", a->special);

		info("    window_count: %hu", a->window_count);
		hs = hostset_create("");
		w = a->window_list;
		for (j = 0; j < a->window_count; j++) {
			if ((w[j].state == NRT_WIN_AVAILABLE) &&
			    (j >= NRT_DEBUG_CNT)) {
				snprintf(window_str, sizeof(window_str), "%d",
					 w[j].window_id);
				hostset_insert(hs, window_str);
				continue;
			}
			info("      window:%hu state:%s job_key:%u",
			     w[j].window_id, _win_state_str(w[j].state),
			     w[j].job_key);
		}
		if (hostset_count(hs) > 0) {
			hostset_ranged_string(hs, sizeof(window_str),
					      window_str);
			info("      suppress data for available windows %s",
			     window_str);
			info("      -------- ");
		}
		hostset_destroy(hs);

		info("    block_count: %hu", a->block_count);
		for (j = 0; j < a->block_count; j++) {
			if (a->block_list[j].job_key) {
				info("      job_key[%d]: %u",
				     j, a->block_list[j].job_key);
			}
		}
	}
	info("--End Node Info--");
}

/* Used by: slurmctld */
static void
_print_libstate(const slurm_nrt_libstate_t *l)
{
	int i;

	xassert(l);
	xassert(l->magic == NRT_LIBSTATE_MAGIC);

	info("--Begin libstate--");
	info("  node_count = %u", l->node_count);
	info("  node_max = %u", l->node_max);
	info("  hash_max = %u", l->hash_max);
	info("  key_index = %u", l->key_index);
	for (i = 0; i < l->node_count; i++) {
		_print_nodeinfo(&l->node_list[i]);
	}
	info("--End libstate--");
}
/* Used by: all */
static void
_print_table(void *table, int size, nrt_adapter_t adapter_type, bool ip_v4)
{
	char addr_str[128];
	int i;

	xassert(table);
	xassert(size > 0);

	info("--Begin NRT table--");
	for (i = 0; i < size; i++) {
		if (adapter_type == NRT_IB) {
			nrt_ib_task_info_t *ib_tbl_ptr;
			ib_tbl_ptr = table;
			ib_tbl_ptr += i;
			info("  task_id: %u", ib_tbl_ptr->task_id);
			info("  win_id: %hu", ib_tbl_ptr->win_id);
			inet_ntop(AF_INET, &ib_tbl_ptr->node_number, addr_str,
				  sizeof(addr_str));
			info("  node_number: %s", addr_str);
/*			info("  node_number: %u", ib_tbl_ptr->node_number); */
			info("  device_name: %s", ib_tbl_ptr->device_name);
			info("  base_lid: %u", ib_tbl_ptr->base_lid);
			info("  port_id: %hu", ib_tbl_ptr->port_id);
			info("  lmc: %hu", ib_tbl_ptr->lmc);
			info("  port_status: %hu", ib_tbl_ptr->port_status);
		} else if (adapter_type == NRT_HFI) {
			nrt_hfi_task_info_t *hfi_tbl_ptr;
			hfi_tbl_ptr = table;
			hfi_tbl_ptr += i;
			info("  task_id: %u", hfi_tbl_ptr->task_id);
			info("  lpar_id: %u", hfi_tbl_ptr->lpar_id);
			info("  lid: %u", hfi_tbl_ptr->lid);
			info("  win_id: %hu", hfi_tbl_ptr->win_id);
		}
#if NRT_VERSION < 1300
		else if ((adapter_type == NRT_HPCE) ||
		           (adapter_type == NRT_KMUX)) {
			nrt_hpce_task_info_t *hpce_tbl_ptr;
			hpce_tbl_ptr = table;
			hpce_tbl_ptr += i;
			info("  task_id: %u", hpce_tbl_ptr->task_id);
			info("  win_id: %hu", hpce_tbl_ptr->win_id);
			inet_ntop(AF_INET, &hpce_tbl_ptr->node_number,
				  addr_str, sizeof(addr_str));
			info("  node_number: %s", addr_str);
/*			info("  node_number: %u", hpce_tbl_ptr->node_number); */
			info("  device_name: %s", hpce_tbl_ptr->device_name);
		}
#endif
		else if (adapter_type == NRT_IPONLY) {
			nrt_ip_task_info_t *ip_tbl_ptr;
			char addr_str[128];
			ip_tbl_ptr = table;
			ip_tbl_ptr += i;
			info("  task_id: %u", ip_tbl_ptr->task_id);
			inet_ntop(AF_INET, &ip_tbl_ptr->node_number, addr_str,
				  sizeof(addr_str));
			info("  node_number: %s", addr_str);
/*			info("  node_number: %u", ip_tbl_ptr->node_number); */
			if (ip_v4) {
				inet_ntop(AF_INET, &ip_tbl_ptr->ip.ipv4_addr,
					  addr_str, sizeof(addr_str));
				info("  ipv4_addr: %s", addr_str);
			} else {
				inet_ntop(AF_INET6, &ip_tbl_ptr->ip.ipv6_addr,
					  addr_str, sizeof(addr_str));
				info("  ipv6_addr: %s", addr_str);
			}
		} else {
			error("Unsupported adapter_type: %s",
			      _adapter_type_str(adapter_type));
		}
		info("  ------");
	}
	info("--End NRT table--");
}

/* Used by: slurmd, slurmctld */
static void
_print_jobinfo(slurm_nrt_jobinfo_t *j)
{
	int i;
	char buf[128];
	nrt_adapter_t adapter_type;

	if ((j == NULL) || (j->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return;
	}

	xassert(j->magic == NRT_JOBINFO_MAGIC);

	info("--Begin Jobinfo--");
	info("  job_key: %u", j->job_key);
	info("  bulk_xfer: %hu", j->bulk_xfer);
	info("  bulk_xfer_resources: %u", j->bulk_xfer_resources);
	info("  cau_indexes: %hu", j->cau_indexes);
	info("  immed_slots: %hu", j->immed_slots);
	info("  ip_v4: %hu", j->ip_v4);
	info("  user_space: %hu", j->user_space);
	info("  tables_per_task: %hu", j->tables_per_task);
	if (j->nodenames)
		hostlist_ranged_string(j->nodenames, sizeof(buf), buf);
	else
		strcpy(buf, "(NULL)");
	info("  nodenames: %s (slurmctld internal use only)", buf);
	info("  num_tasks: %d", j->num_tasks);
	for (i = 0; i < j->tables_per_task; i++) {
		info("--Header NRT table--");
		info("  adapter_name: %s", j->tableinfo[i].adapter_name);
		info("  adapter_type: %s",
		     _adapter_type_str(j->tableinfo[i].adapter_type));
		info("  context_id: %u", j->tableinfo[i].context_id);
		info("  instance: %u", j->tableinfo[i].instance);
		info("  network_id: %lu", j->tableinfo[i].network_id);
		info("  protocol_name: %s", j->tableinfo[i].protocol_name);
		info("  table_id: %u", j->tableinfo[i].table_id);
		if (j->user_space)
			adapter_type = j->tableinfo[i].adapter_type;
		else
			adapter_type = NRT_IPONLY;
		_print_table(j->tableinfo[i].table,
			     j->tableinfo[i].table_length, adapter_type,
			     j->ip_v4);
	}
	info("--End Jobinfo--");
}

static void
_print_load_table(nrt_cmd_load_table_t *load_table)
{
	nrt_table_info_t *table_info = load_table->table_info;
	nrt_adapter_t adapter_type;

	info("--- Begin load table ---");
	info("  num_tasks: %u", table_info->num_tasks);
	info("  job_key: %u", table_info->job_key);
	info("  uid: %u", (uint32_t)table_info->uid);
	info("  pid: %u", (uint32_t)table_info->pid);
	info("  network_id: %lu", table_info->network_id);
	info("  adapter_type: %s",_adapter_type_str(table_info->adapter_type));
	info("  is_user_space: %d", (int)table_info->is_user_space);
	info("  is_ipv4: %hu", (int)table_info->is_ipv4);
	info("  context_id: %u", table_info->context_id);
	info("  table_id: %u", table_info->table_id);
	info("  job_name: %s", table_info->job_name);
	info("  protocol_name: %s", table_info->protocol_name);
	info("  use_bulk_transfer: %hu", (int)table_info->use_bulk_transfer);
	info("  bulk_transfer_resources: %u",
	     table_info->bulk_transfer_resources);
	info("  immed_send_slots_per_win: %u",
	     table_info->immed_send_slots_per_win);
	info("  num_cau_indexes: %u", table_info->num_cau_indexes);
	if (table_info->is_user_space)
		adapter_type = table_info->adapter_type;
	else
		adapter_type = NRT_IPONLY;
	_print_table(load_table->per_task_input, table_info->num_tasks,
		     adapter_type, table_info->is_ipv4);
	info("--- End load table ---");
}

static slurm_nrt_libstate_t *
_alloc_libstate(void)
{
	slurm_nrt_libstate_t *tmp;

	tmp = (slurm_nrt_libstate_t *) xmalloc(sizeof(slurm_nrt_libstate_t));
	tmp->magic = NRT_LIBSTATE_MAGIC;
	tmp->node_count = 0;
	tmp->node_max = 0;
	tmp->node_list = NULL;
	tmp->hash_max = 0;
	tmp->hash_table = NULL;
	/* Start key from random point, old key values are cached,
	 * which seems to prevent re-use for a while */
	tmp->key_index = (nrt_job_key_t) time(NULL);

	return tmp;
}

/* Allocate and initialize memory for the persistent libstate.
 *
 * Used by: slurmctld
 */
extern int
nrt_init(void)
{
	slurm_nrt_libstate_t *tmp;

	tmp = _alloc_libstate();
	slurm_mutex_lock(&global_lock);
	xassert(!nrt_state);
	nrt_state = tmp;
	slurm_mutex_unlock(&global_lock);

	return SLURM_SUCCESS;
}

extern int
nrt_slurmctld_init(void)
{
	/* No op */
	return SLURM_SUCCESS;
}

extern int
nrt_slurmd_init(void)
{
	/*
	 * This is a work-around for the nrt_* functions calling umask(0)
	 */
	nrt_umask = umask(0077);
	umask(nrt_umask);

	return SLURM_SUCCESS;
}

extern int
nrt_slurmd_step_init(void)
{
	/*
	 * This is a work-around for the nrt_* functions calling umask(0)
	 */
	nrt_umask = umask(0077);
	umask(nrt_umask);

	_init_adapter_cache();
	_fill_in_adapter_cache();

	return SLURM_SUCCESS;
}

/* Used by: slurmd, slurmctld */
extern int
nrt_alloc_jobinfo(slurm_nrt_jobinfo_t **j)
{
	slurm_nrt_jobinfo_t *new;

	xassert(j != NULL);
	new = (slurm_nrt_jobinfo_t *) xmalloc(sizeof(slurm_nrt_jobinfo_t));
	new->magic = NRT_JOBINFO_MAGIC;
	new->job_key = (nrt_job_key_t) -1;
	*j = new;

	return 0;
}

/* Used by: slurmd, slurmctld */
extern int
nrt_alloc_nodeinfo(slurm_nrt_nodeinfo_t **n)
{
	slurm_nrt_nodeinfo_t *new;

	xassert(n);

	new = (slurm_nrt_nodeinfo_t *) xmalloc(sizeof(slurm_nrt_nodeinfo_t));
	new->adapter_list = (slurm_nrt_adapter_t *)
			    xmalloc(sizeof(slurm_nrt_adapter_t) *
			    NRT_MAX_ADAPTER_TYPES * NRT_MAX_ADAPTERS_PER_TYPE);
	new->magic = NRT_NODEINFO_MAGIC;
	new->adapter_count = 0;
	new->next = NULL;

	*n = new;

	return 0;
}

static int _get_my_id(void)
{
	int err, i, j, k, rc = SLURM_SUCCESS;
	nrt_cmd_query_adapter_types_t adapter_types;
	unsigned int num_adapter_types;
	nrt_adapter_t adapter_type[NRT_MAX_ADAPTER_TYPES];
	nrt_cmd_query_adapter_names_t adapter_names;
	unsigned int max_windows, num_adapter_names;
	nrt_cmd_query_adapter_info_t query_adapter_info;
	nrt_adapter_info_t adapter_info;
	nrt_status_t *status_array = NULL;

	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	adapter_info.window_list = NULL;
	for (i = 0; i < 2; i++) {
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				   &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		usleep(1000);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}

	for (i = 0; i < num_adapter_types; i++) {
		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				  &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_cmd_wrap(adapter_names, %s): %s",
			      _adapter_type_str(adapter_names.adapter_type),
			      nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
		for (j = 0; j < num_adapter_names; j++) {
			if (my_network_id_set && my_lpar_id_set)
				break;
			if (my_network_id_set &&
			    (adapter_names.adapter_type != NRT_HFI))
				continue;
			query_adapter_info.adapter_name = adapter_names.
							  adapter_names[j];
			query_adapter_info.adapter_type = adapter_names.
							  adapter_type;
			query_adapter_info.adapter_info = &adapter_info;
			adapter_info.window_list = xmalloc(max_windows *
						   sizeof(nrt_window_id_t));
			err = nrt_cmd_wrap(NRT_VERSION,
					   NRT_CMD_QUERY_ADAPTER_INFO,
					   &query_adapter_info);
			if (err != NRT_SUCCESS) {
				error("nrt_cmd_wrap(adapter_into, %s, %s): %s",
				      query_adapter_info.adapter_name,
				      _adapter_type_str(query_adapter_info.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
			if (!my_network_id_set &&
			    (adapter_info.node_number != 0)) {
				my_network_id  = adapter_info.node_number;
				my_network_id_set = true;
			}
			if (my_lpar_id_set ||
			    (adapter_names.adapter_type != NRT_HFI))
				continue;
			for (k = 0; k < adapter_info.num_ports; k++) {
				if (adapter_info.port[k].status != 1)
					continue;
				my_lpar_id = adapter_info.port[k].special;
				my_lid = adapter_info.port[k].lid;
				my_lpar_id_set = true;
				break;
			}
		}
		xfree(adapter_info.window_list);
	}
	if (status_array)
		free(status_array);

	return rc;
}

/* Load the minimum usable window ID on a given adapater.
 *
 * NOTES: Bill LePera, IBM: Out of 256 windows on each HFI device, the first
 * 4 are reserved for the HFI device driver's use. Next are the dynamic windows
 * (default 32), followed by the windows available to be scheduled by PNSD
 * and the job schedulers. This is why the output of nrt_status shows the
 * first window number reported as 36. */
static int
_load_min_window_id(char *adapter_name, nrt_adapter_t adapter_type)
{
	FILE *fp;
	char buf[128], path[256];
	size_t sz;
	int min_window_id = 0;

	if (adapter_type != NRT_HFI)
		return min_window_id;

	min_window_id = 4;
	snprintf(path, sizeof(path),
		 "/sys/devices/virtual/hfi/%s/num_dynamic_win", adapter_name);
	fp = fopen(path, "r");
	if (fp) {
		memset(buf, 0, sizeof(buf));
		sz = fread(buf, 1, sizeof(buf), fp);
		if (sz) {
			buf[sz] = '\0';
			min_window_id += strtol(buf, NULL, 0);
		}
		(void) fclose(fp);
	}

	return min_window_id;
}

static int
_get_adapters(slurm_nrt_nodeinfo_t *n)
{
	int err, i, j, k, rc = SLURM_SUCCESS;
	nrt_cmd_query_adapter_types_t adapter_types;
	unsigned int num_adapter_types;
	nrt_adapter_t adapter_type[NRT_MAX_ADAPTER_TYPES];
	nrt_cmd_query_adapter_names_t adapter_names;
	unsigned int max_windows, num_adapter_names;
	nrt_cmd_status_adapter_t adapter_status;
	nrt_cmd_query_adapter_info_t query_adapter_info;
	nrt_adapter_info_t adapter_info;
	nrt_status_t *status_array = NULL;
	nrt_window_id_t window_count;
	int min_window_id = 0, total_adapters = 0;
	slurm_nrt_adapter_t *adapter_ptr;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("_get_adapters: begin");

	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	adapter_info.window_list = NULL;
	for (i = 0; i < 2; i++) {
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				   &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		error("Is PNSD daemon started? Retrying...");
		/* Run "/opt/ibmhpc/pecurrent/ppe.pami/pnsd/pnsd -A" */
		sleep(5);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		for (i = 0; i < num_adapter_types; i++) {
			info("nrt_cmd_wrap(adapter_types): %s",
			     _adapter_type_str(adapter_types.adapter_types[i]));
		}
	}

	for (i = 0; i < num_adapter_types; i++) {
		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				   &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_cmd_wrap(adapter_names, %s): %s",
			      _adapter_type_str(adapter_names.adapter_type),
			      nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}

		/* Get the total adapter count here and print afterwards. */
		total_adapters += num_adapter_names;
		if (total_adapters > NRT_MAXADAPTERS)
			continue;

		if (debug_flags & DEBUG_FLAG_SWITCH) {
			for (j = 0; j < num_adapter_names; j++) {
				info("nrt_cmd_wrap(adapter_names, %s, %s) "
				     "max_windows: %hu",
				      adapter_names.adapter_names[j],
				      _adapter_type_str(adapter_names.
							adapter_type),
				      max_windows);
			}
		}

		for (j = 0; j < num_adapter_names; j++) {
			min_window_id = _load_min_window_id(
						adapter_names.adapter_names[j],
						adapter_names.adapter_type);
			if (status_array) {
				free(status_array);
				status_array = NULL;
			}
			adapter_status.adapter_name = adapter_names.
						      adapter_names[j];
			adapter_status.adapter_type = adapter_names.
						      adapter_type;
			adapter_status.status_array = &status_array;
			adapter_status.window_count = &window_count;
			err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
					   &adapter_status);
			if (err != NRT_SUCCESS) {
				error("nrt_cmd_wrap(status_adapter, %s, %s): %s",
				      adapter_status.adapter_name,
				      _adapter_type_str(adapter_status.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
			if (window_count > max_windows) {
				/* This happens if IP_ONLY devices are
				 * allocated with tables_per_task > 0 */
				char *reason;
				if (adapter_status.adapter_type == NRT_IPONLY)
					reason = ", Known libnrt bug";
				else
					reason = "";
				debug("nrt_cmd_wrap(status_adapter, %s, %s): "
				      "window_count > max_windows (%u > %hu)%s",
				      adapter_status.adapter_name,
				      _adapter_type_str(adapter_status.
							adapter_type),
				      window_count, max_windows, reason);
				/* Reset value to avoid logging bad data */
				window_count = max_windows;
			}
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("nrt_cmd_wrap(status_adapter, %s, %s)",
				     adapter_status.adapter_name,
				     _adapter_type_str(adapter_status.
						       adapter_type));
				_print_adapter_status(&adapter_status);
			}
			adapter_ptr = &n->adapter_list[n->adapter_count];
			strlcpy(adapter_ptr->adapter_name,
				adapter_status.adapter_name,
				NRT_MAX_ADAPTER_NAME_LEN);
			adapter_ptr->adapter_type = adapter_status.
						    adapter_type;
			adapter_ptr->window_count = adapter_status.
						    window_count[0];
			adapter_ptr->window_list =
				xmalloc(sizeof(slurm_nrt_window_t) *
					window_count);
			n->adapter_count++;

			for (k = 0; k < window_count; k++) {
				slurm_nrt_window_t *window_ptr;
				window_ptr = adapter_ptr->window_list + k;
				window_ptr->window_id = status_array[k].
							window_id;
				window_ptr->state = status_array[k].state;
				/* window_ptr->job_key = Not_Available */
				if ((adapter_ptr->adapter_type == NRT_HFI) &&
				    (!dynamic_window_err) &&
				    (window_ptr->window_id < min_window_id)) {
					error("switch/nrt: Dynamic window "
					      "configuration error for %s, "
					      "window_id=%u < min_window_id:%d",
					      adapter_status.adapter_name,
					      window_ptr->window_id,
					      min_window_id);
					dynamic_window_err = true;
				}
			}

			/* Now get adapter info (port_id, network_id, etc.) */
			query_adapter_info.adapter_name = adapter_names.
							  adapter_names[j];
			query_adapter_info.adapter_type = adapter_names.
							  adapter_type;
			query_adapter_info.adapter_info = &adapter_info;
			adapter_info.window_list = xmalloc(max_windows *
						   sizeof(nrt_window_id_t));
			err = nrt_cmd_wrap(NRT_VERSION,
					   NRT_CMD_QUERY_ADAPTER_INFO,
					   &query_adapter_info);
			if (err != NRT_SUCCESS) {
				error("nrt_cmd_wrap(adapter_into, %s, %s): %s",
				      query_adapter_info.adapter_name,
				      _adapter_type_str(query_adapter_info.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("nrt_cmd_wrap(adapter_info, %s, %s)",
				     query_adapter_info.adapter_name,
				     _adapter_type_str(query_adapter_info.
						       adapter_type));
				_print_adapter_info(&adapter_info);
			}
			if (adapter_info.node_number != 0) {
				n->node_number = adapter_info.node_number;
				my_network_id  = adapter_info.node_number;
				my_network_id_set = true;
			}
			adapter_ptr->cau_indexes_avail =
				adapter_info.cau_indexes_avail;
			adapter_ptr->immed_slots_avail =
				adapter_info.immed_slots_avail;
			adapter_ptr->rcontext_block_count =
				adapter_info.rcontext_block_count;
			for (k = 0; k < adapter_info.num_ports; k++) {
				if (adapter_info.port[k].status != 1)
					continue;
				adapter_ptr->ipv4_addr = adapter_info.port[k].
							 ipv4_addr;
				adapter_ptr->ipv6_addr = adapter_info.port[k].
							 ipv6_addr;
				adapter_ptr->lid = adapter_info.port[k].lid;
				adapter_ptr->network_id = adapter_info.port[k].
							  network_id;
				adapter_ptr->port_id = adapter_info.port[k].
						       port_id;
				adapter_ptr->special = adapter_info.port[k].
						       special;
				if (adapter_ptr->adapter_type == NRT_HFI) {
					my_lpar_id = adapter_ptr->special;
					my_lid = adapter_ptr->lid;
					my_lpar_id_set = true;
				}
				break;
			}
			if ((adapter_ptr->ipv4_addr == 0) &&
			    (adapter_info.num_ports > 0)) {
				adapter_ptr->ipv4_addr = adapter_info.port[0].
							 ipv4_addr;
				adapter_ptr->ipv6_addr = adapter_info.port[0].
							 ipv6_addr;
			}
			xfree(adapter_info.window_list);
		}
		if (status_array) {
			free(status_array);
			status_array = NULL;
		}

	}

	if (total_adapters > NRT_MAXADAPTERS) {
		fatal("switch/nrt: More adapters found (%u) on "
		      "node than supported (%u). "
		      "Increase NRT_MAXADAPTERS and rebuild slurm",
		      total_adapters, NRT_MAXADAPTERS);
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		_print_nodeinfo(n);
		info("_get_adapters: complete: %d", rc);
	}
	return rc;
}

/* Assumes a pre-allocated nodeinfo structure and uses _get_adapters
 * to do the dirty work.  We probably collect more information about
 * the adapters on a give node than we need to, but it was done
 * in the interest of being prepared for future requirements.
 *
 * Used by: slurmd
 */
extern int
nrt_build_nodeinfo(slurm_nrt_nodeinfo_t *n, char *name)
{
	int err;

	xassert(n);
	xassert(n->magic == NRT_NODEINFO_MAGIC);
	xassert(name);

	strlcpy(n->name, name, NRT_HOSTLEN);
	slurm_mutex_lock(&global_lock);
	err = _get_adapters(n);
	slurm_mutex_unlock(&global_lock);

	return err;
}

/* Used by: all */
extern int
nrt_pack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf,
		  uint16_t protocol_version)
{
	slurm_nrt_adapter_t *a;
	uint16_t dummy16;
	int i, j, offset;

	xassert(n);
	xassert(n->magic == NRT_NODEINFO_MAGIC);
	xassert(buf);
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_pack_nodeinfo():");
		_print_nodeinfo(n);
	}
	offset = get_buf_offset(buf);
	pack32(n->magic, buf);
	packmem(n->name, NRT_HOSTLEN, buf);
	pack32(n->node_number, buf);
	pack32(n->adapter_count, buf);
	for (i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		packmem(a->adapter_name, NRT_MAX_ADAPTER_NAME_LEN, buf);
		dummy16 = a->adapter_type;
		pack16(dummy16, buf);	/* adapter_type is an int */
		pack16(a->cau_indexes_avail, buf);
		pack16(a->immed_slots_avail, buf);
		pack32(a->ipv4_addr, buf);
		for (j = 0; j < 16; j++)
			pack8(a->ipv6_addr.s6_addr[j], buf);
		pack32(a->lid, buf);
		pack64(a->network_id, buf);
		pack8(a->port_id, buf);
		pack64(a->rcontext_block_count, buf);
		pack64(a->special, buf);
		pack16(a->window_count, buf);
		for (j = 0; j < a->window_count; j++) {
			uint32_t state = a->window_list[j].state;
			pack16(a->window_list[j].window_id, buf);
			pack32(state, buf);
			pack32(a->window_list[j].job_key, buf);
		}
	}

	return(get_buf_offset(buf) - offset);
}

/* Used by: all */
static int
_copy_node(slurm_nrt_nodeinfo_t *dest, slurm_nrt_nodeinfo_t *src)
{
	int i, j;
	slurm_nrt_adapter_t *sa = NULL;
	slurm_nrt_adapter_t *da = NULL;

	xassert(dest);
	xassert(src);
	xassert(dest->magic == NRT_NODEINFO_MAGIC);
	xassert(src->magic == NRT_NODEINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("_copy_node():");
		_print_nodeinfo(src);
	}

	strlcpy(dest->name, src->name, NRT_HOSTLEN);
	dest->node_number = src->node_number;
	dest->adapter_count = src->adapter_count;
	for (i = 0; i < dest->adapter_count; i++) {
		sa = src->adapter_list + i;
		da = dest->adapter_list +i;
		strlcpy(da->adapter_name, sa->adapter_name,
			NRT_MAX_ADAPTER_NAME_LEN);
		da->adapter_type = sa->adapter_type;
		da->cau_indexes_avail = sa->cau_indexes_avail;
		da->immed_slots_avail = sa->immed_slots_avail;
		da->ipv4_addr    = sa->ipv4_addr;
		da->ipv6_addr    = sa->ipv6_addr;
		da->lid          = sa->lid;
		da->network_id   = sa->network_id;
		da->port_id      = sa->port_id;
		da->rcontext_block_count = sa->rcontext_block_count;
		da->special      = sa->special;
		da->window_count = sa->window_count;
		da->window_list = (slurm_nrt_window_t *)
				  xmalloc(sizeof(slurm_nrt_window_t) *
				  da->window_count);
		for (j = 0; j < da->window_count; j++) {
			da->window_list[j].window_id = sa->window_list[j].
						       window_id;
			da->window_list[j].state = sa->window_list[j].state;
			da->window_list[j].job_key = sa->window_list[j].
						     job_key;
		}
	}

	return SLURM_SUCCESS;
}

static int
_cmp_ipv6(struct in6_addr *addr1, struct in6_addr *addr2)
{
	int i;

	for (i = 0; i < 16; i++) {
		if (addr1->s6_addr[i] != addr2->s6_addr[i])
			return 1;
	}

	return 0;
}

/* Throw away adapter window portion of the nodeinfo.
 *
 * Used by: _unpack_nodeinfo
 */
static int
_fake_unpack_adapters(Buf buf, slurm_nrt_nodeinfo_t *n,
		      uint16_t protocol_version)
{
	slurm_nrt_adapter_t *tmp_a = NULL;
	slurm_nrt_window_t *tmp_w = NULL;
	uint16_t dummy16;
	uint32_t dummy32;
	char *name_ptr;
	uint8_t  port_id;
	uint16_t adapter_type, cau_indexes_avail, immed_slots_avail;
	uint16_t window_count;
	uint32_t adapter_count, ipv4_addr, lid;
	uint64_t network_id, rcontext_block_count, special;
	struct in6_addr ipv6_addr;
	int i, j, k;

	safe_unpack32(&adapter_count, buf);
	if (n && (n->adapter_count != adapter_count)) {
		error("switch/nrt: node %s adapter count reset from %u to %u",
		      n->name, n->adapter_count, adapter_count);
		if (n->adapter_count < adapter_count)
			drain_nodes(n->name, "Too few switch adapters", 0);
	}
	for (i = 0; i < adapter_count; i++) {
		safe_unpackmem_ptr(&name_ptr, &dummy32, buf);
		if (dummy32 != NRT_MAX_ADAPTER_NAME_LEN)
			goto unpack_error;
		safe_unpack16(&adapter_type, buf);
		safe_unpack16(&cau_indexes_avail, buf);
		safe_unpack16(&immed_slots_avail, buf);
		safe_unpack32(&ipv4_addr, buf);
		for (j = 0; j < 16; j++)
			safe_unpack8(&ipv6_addr.s6_addr[j], buf);
		safe_unpack32(&lid, buf);
		safe_unpack64(&network_id, buf);
		safe_unpack8 (&port_id, buf);
		safe_unpack64(&rcontext_block_count, buf);
		safe_unpack64(&special, buf);
		safe_unpack16(&window_count, buf);

		/* no copy, just advances buf counters */
		for (j = 0; j < window_count; j++) {
			safe_unpack16(&dummy16, buf);	/* window_id */
			safe_unpack32(&dummy32, buf);	/* state */
			safe_unpack32(&dummy32, buf);	/* job_key */
		}

		for (j = 0; j < n->adapter_count; j++) {
			tmp_a = n->adapter_list + j;
			if (xstrcmp(tmp_a->adapter_name, name_ptr))
				continue;
			if (tmp_a->cau_indexes_avail != cau_indexes_avail) {
				info("switch/nrt: resetting cau_indexes_avail "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->cau_indexes_avail = cau_indexes_avail;
			}
			if (tmp_a->immed_slots_avail != immed_slots_avail) {
				info("switch/nrt: resetting immed_slots_avail "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->immed_slots_avail = immed_slots_avail;
			}
			if (tmp_a->ipv4_addr != ipv4_addr) {
				info("switch/nrt: resetting ipv4_addr "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->ipv4_addr = ipv4_addr;
			}
			if (_cmp_ipv6(&tmp_a->ipv6_addr, &ipv6_addr) != 0) {
				info("switch/nrt: resetting ipv6_addr "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				for (k = 0; k < 16; k++) {
					tmp_a->ipv6_addr.s6_addr[k] =
						ipv6_addr.s6_addr[k];
				}
			}
			if (tmp_a->lid != lid) {
				info("switch/nrt: resetting lid "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->lid = lid;
			}
			if (tmp_a->network_id != network_id) {
				info("switch/nrt: resetting network_id "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->network_id = network_id;
			}
			if (tmp_a->port_id != port_id) {
				info("switch/nrt: resetting port_id "
				     "on node %s adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->port_id = port_id;
			}
			if (tmp_a->rcontext_block_count !=
			    rcontext_block_count) {
				info("switch/nrt: resetting "
				     "rcontext_block_count on node %s "
				     "adapter %s",
				     n->name, tmp_a->adapter_name);
				tmp_a->rcontext_block_count =
					rcontext_block_count;
			}
			break;
		}

		if (j == n->adapter_count) {
			error("switch/nrt: node %s adapter %s being added",
			      n->name, name_ptr);
			n->adapter_count++;
			xrealloc(n->adapter_list,
				 sizeof(slurm_nrt_adapter_t) *
				 n->adapter_count);
			tmp_a = n->adapter_list + j;
			strlcpy(tmp_a->adapter_name, name_ptr,
				NRT_MAX_ADAPTER_NAME_LEN);
			tmp_a->adapter_type = adapter_type;
			/* tmp_a->block_count = 0 */
			/* tmp_a->block_list = NULL */
			tmp_a->cau_indexes_avail = cau_indexes_avail;
			tmp_a->immed_slots_avail = immed_slots_avail;
			tmp_a->ipv4_addr = ipv4_addr;
			for (k = 0; k < 16; k++) {
				tmp_a->ipv6_addr.s6_addr[k] =
					ipv6_addr.s6_addr[k];
			}
			tmp_a->lid = lid;
			tmp_a->network_id = network_id;
			tmp_a->port_id = port_id;
			tmp_a->rcontext_block_count = rcontext_block_count;
			tmp_a->special = special;
			tmp_a->window_count = window_count;
			tmp_w = (slurm_nrt_window_t *)
				xmalloc(sizeof(slurm_nrt_window_t) *
				tmp_a->window_count);
			for (k = 0; k < tmp_a->window_count; k++) {
				tmp_w[k].state = NRT_WIN_AVAILABLE;
				tmp_w[k].job_key = 0;
			}
			tmp_a->window_list = tmp_w;
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	return SLURM_ERROR;
}


/* Unpack nodeinfo and update persistent libstate.
 *
 * If believe_window_status is true, we honor the window status variables
 * from the packed nrt_nodeinfo_t.  If it is false we set the status of
 * all windows to NRT_WIN_AVAILABLE.
 *
 * Used by: slurmctld
 */
static int
_unpack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf, bool believe_window_status,
		 uint16_t protocol_version)
{
	int i, j, rc = SLURM_SUCCESS;
	slurm_nrt_adapter_t *tmp_a = NULL;
	slurm_nrt_window_t *tmp_w = NULL;
	nrt_node_number_t node_number;
	uint32_t size;
	slurm_nrt_nodeinfo_t *tmp_n = NULL;
	char *name_ptr, name[NRT_HOSTLEN];
	uint32_t magic;
	uint16_t dummy16;

	/* NOTE!  We don't care at this point whether n is valid.
	 * If it's NULL, we will just forego the copy at the end.
	 */
	xassert(buf);

	/* Extract node name from buffer */
	safe_unpack32(&magic, buf);
	if (magic != NRT_NODEINFO_MAGIC)
		slurm_seterrno_ret(EBADMAGIC_NRT_NODEINFO);
	safe_unpackmem_ptr(&name_ptr, &size, buf);
	if (size != NRT_HOSTLEN)
		goto unpack_error;
	memcpy(name, name_ptr, size);
	safe_unpack32(&node_number, buf);

	/* When the slurmctld is in normal operating mode (NOT backup mode),
	 * the global nrt_state structure should NEVER be NULL at the time that
	 * this function is called.  Therefore, if nrt_state is NULL here,
	 * we assume that the controller is in backup mode.  In backup mode,
	 * the slurmctld only unpacks RPCs to find out their identity.
	 * Most of the RPCs, including the one calling this function, are
	 * simply ignored.
	 *
	 * So, here we just do a fake unpack to advance the buffer pointer.
	 */
	if (nrt_state == NULL) {
		if (_fake_unpack_adapters(buf, NULL, protocol_version)
		    != SLURM_SUCCESS) {
			slurm_seterrno_ret(EUNPACK);
		} else {
			return SLURM_SUCCESS;
		}
	}

	/* If we already have nodeinfo for this node, we ignore this message.
	 * The slurmctld's view of window allocation is always better than
	 * the slurmd's view.  We only need the slurmd's view if the slurmctld
	 * has no nodeinfo at all for that node.
	 */
	if (name != NULL) {
		tmp_n = _find_node(nrt_state, name);
		if (tmp_n != NULL) {
			tmp_n->node_number = node_number;
			if (_fake_unpack_adapters(buf, tmp_n, protocol_version)
			    != SLURM_SUCCESS) {
				slurm_seterrno_ret(EUNPACK);
			} else {
				goto copy_node;
			}
		}
	}

	/*
	 * Update global libstate with this nodes' info.
	 */
	tmp_n = _alloc_node(nrt_state, name);
	if (tmp_n == NULL)
		return SLURM_ERROR;
	tmp_n->magic = magic;
	tmp_n->node_number = node_number;
	safe_unpack32(&tmp_n->adapter_count, buf);
	if (tmp_n->adapter_count > NRT_MAXADAPTERS) {
		error("switch/nrt: More adapters found on node %s than "
		      "supported. Increase NRT_MAXADAPTERS and rebuild slurm",
		      name);
		tmp_n->adapter_count = NRT_MAXADAPTERS;
	}
	for (i = 0; i < tmp_n->adapter_count; i++) {
		tmp_a = tmp_n->adapter_list + i;
		safe_unpackmem_ptr(&name_ptr, &size, buf);
		if (size != NRT_MAX_ADAPTER_NAME_LEN)
			goto unpack_error;
		memcpy(tmp_a->adapter_name, name_ptr, size);
		safe_unpack16(&dummy16, buf);
		tmp_a->adapter_type = dummy16;	/* adapter_type is an int */
		safe_unpack16(&tmp_a->cau_indexes_avail, buf);
		safe_unpack16(&tmp_a->immed_slots_avail, buf);
		safe_unpack32(&tmp_a->ipv4_addr, buf);
		for (j = 0; j < 16; j++) {
			safe_unpack8(&tmp_a->ipv6_addr.s6_addr[j], buf);
		}
		safe_unpack32(&tmp_a->lid, buf);
		safe_unpack64(&tmp_a->network_id, buf);
		safe_unpack8(&tmp_a->port_id, buf);
		safe_unpack64(&tmp_a->rcontext_block_count, buf);
		safe_unpack64(&tmp_a->special, buf);
		safe_unpack16(&tmp_a->window_count, buf);
		tmp_w = (slurm_nrt_window_t *)
			xmalloc(sizeof(slurm_nrt_window_t) *
			tmp_a->window_count);
		for (j = 0; j < tmp_a->window_count; j++) {
			safe_unpack16(&tmp_w[j].window_id, buf);
			safe_unpack32(&tmp_w[j].state, buf);
			safe_unpack32(&tmp_w[j].job_key, buf);
			if (!believe_window_status) {
				tmp_w[j].state = NRT_WIN_AVAILABLE;
				tmp_w[j].job_key = 0;
			}
		}
		tmp_a->window_list = tmp_w;
		tmp_w = NULL;  /* don't free if unpack error on next adapter */
	}
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("_unpack_nodeinfo");
		_print_nodeinfo(tmp_n);
	}

copy_node:
	/* Only copy the node_info structure if the caller wants it */
	if ((n != NULL) && (_copy_node(n, tmp_n) != SLURM_SUCCESS))
		rc = SLURM_ERROR;
	return rc;

unpack_error:
	xfree(tmp_w);
	slurm_seterrno_ret(EUNPACK);
}

/* Unpack nodeinfo and update persistent libstate.
 *
 * Used by: slurmctld
 */
extern int
nrt_unpack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf, uint16_t protocol_version)
{
	int rc;

	slurm_mutex_lock(&global_lock);
	rc = _unpack_nodeinfo(n, buf, false, protocol_version);
	slurm_mutex_unlock(&global_lock);
	return rc;
}

/* Used by: slurmd, slurmctld */
extern void
nrt_free_nodeinfo(slurm_nrt_nodeinfo_t *n, bool ptr_into_array)
{
	slurm_nrt_adapter_t *adapter;
	int i;

	if (!n)
		return;

	xassert(n->magic == NRT_NODEINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_free_nodeinfo");
		_print_nodeinfo(n);
	}

	if (n->adapter_list) {
		adapter = n->adapter_list;
		for (i = 0; i < n->adapter_count; i++)
			xfree(adapter[i].window_list);
		xfree(n->adapter_list);
	}
	if (!ptr_into_array)
		xfree(n);
}

/* Find all of the windows used by job step "jp" on the hosts
 * designated in hostlist "hl" and mark their state NRT_WIN_AVAILABLE.
 *
 * Used by: slurmctld
 */
extern int
nrt_job_step_complete(slurm_nrt_jobinfo_t *jp, hostlist_t hl)
{
	hostlist_t uniq_hl;
	hostlist_iterator_t hi;
	char *node_name;

	xassert(!hostlist_is_empty(hl));
	if ((jp == NULL) || (jp->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_ERROR;
	}

	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if ((jp == NULL) || (hostlist_is_empty(hl)))
		return SLURM_ERROR;

	if ((jp->tables_per_task == 0) || (jp->tableinfo == NULL) ||
	    (jp->tableinfo[0].table_length == 0))
		return SLURM_SUCCESS;

	/* The hl hostlist may contain duplicate node_names (poe -hostfile
	 * triggers duplicates in the hostlist).  Since there
	 * is no reason to call _free_resources_by_job more than once
	 * per node_name, we create a new unique hostlist.
	 */
	uniq_hl = hostlist_copy(hl);
	hostlist_uniq(uniq_hl);
	hi = hostlist_iterator_create(uniq_hl);

	slurm_mutex_lock(&global_lock);
	if (nrt_state != NULL) {
		while ((node_name = hostlist_next(hi)) != NULL) {
			_free_resources_by_job(jp, node_name);
			free(node_name);
		}
	} else { /* nrt_state == NULL */
		/* If there is no state at all, the job is already cleaned
		 * up. :)  This should really only happen when the backup
		 * controller is calling job_fini() just before it takes over
		 * the role of active controller.
		 */
		debug("nrt_job_step_complete called when nrt_state == NULL");
	}
	slurm_mutex_unlock(&global_lock);

	hostlist_iterator_destroy(hi);
	hostlist_destroy(uniq_hl);
	return SLURM_SUCCESS;
}

/* Find all of the windows used by job step "jp" and mark their
 * state NRT_WIN_UNAVAILABLE.
 *
 * Used by the slurmctld at startup time to restore the allocation
 * status of any job steps that were running at the time the previous
 * slurmctld was shutdown.  Also used to restore the allocation
 * status after a call to switch_g_clear().
 */
extern int
nrt_job_step_allocated(slurm_nrt_jobinfo_t *jp, hostlist_t hl)
{
	int rc;

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_job_step_allocated: resetting window state");
		_print_jobinfo(jp);
	}

	rc = _job_step_window_state(jp, hl, NRT_WIN_UNAVAILABLE);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_job_step_allocated: window state reset complete");
		_print_libstate(nrt_state);
	}

	return rc;
}

/* Assign a unique key to each job.  The key is used later to
 * gain access to the network table loaded on each node of a job.
 *
 * Used by: slurmctld
 */
static nrt_job_key_t
_next_key(void)
{
	nrt_job_key_t key;

	xassert(nrt_state);

	slurm_mutex_lock(&global_lock);
	key = nrt_state->key_index;
	if (key == 0)
		key++;
	nrt_state->key_index = (nrt_job_key_t) (key + 1);
	slurm_mutex_unlock(&global_lock);

	return key;
}

/* Translate a protocol string (e.g. "lapi,mpi" into a table.
 * Caller must free returned value. */
static nrt_protocol_table_t *_get_protocol_table(char *protocol)
{
	nrt_protocol_table_t *protocol_table;
	char *protocol_str, *save_ptr = NULL, *token;
	int i;

	protocol_table = xmalloc(sizeof(nrt_protocol_table_t));

	if (!protocol)
		protocol = "mpi";
	protocol_str = xstrdup(protocol);
	token = strtok_r(protocol_str, ",", &save_ptr);
	while (token) {
		for (i = 0; i < protocol_table->protocol_table_cnt; i++) {
			if (!xstrcmp(token, protocol_table->protocol_table[i].
					    protocol_name))
				break;
		}
		if ((i >= protocol_table->protocol_table_cnt) &&
		    (i < NRT_MAX_PROTO_CNT)) {
			/* Need to add new protocol type */
			strlcpy(protocol_table->protocol_table[i].protocol_name,
				token, NRT_MAX_PROTO_NAME_LEN);
			protocol_table->protocol_table_cnt++;
		}
		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(protocol_str);

	return protocol_table;
}

/* For an adapter type, return it's relative priority to use as a default */
static inline int
_adapter_type_pref(nrt_adapter_t adapter_type)
{
	if (adapter_type == NRT_IPONLY)
		return 9;
	if (adapter_type == NRT_HFI)
		return 8;
	if (adapter_type == NRT_IB)
		return 7;
#if NRT_VERSION < 1300
	if (adapter_type == NRT_HPCE)
		return 6;
	if (adapter_type == NRT_KMUX)
		return 5;
#endif
	return 0;
}

/* Setup everything for the job.  Assign tasks across
 * nodes based on the hostlist given and create the network table used
 * on all nodes of the job.
 *
 * Used by: slurmctld
 */
extern int
nrt_build_jobinfo(slurm_nrt_jobinfo_t *jp, hostlist_t hl,
		  uint16_t *tasks_per_node, uint32_t **tids, bool sn_all,
		  char *adapter_name, nrt_adapter_t dev_type,
		  bool bulk_xfer, uint32_t bulk_xfer_resources,
		  bool ip_v4, bool user_space, char *protocol, int instances,
		  int cau, int immed)
{
	int nnodes, nprocs = 0;
	hostlist_iterator_t hi;
	char *host = NULL;
	int i, j;
	slurm_nrt_nodeinfo_t *node;
	int rc;
	nrt_adapter_t adapter_type = NRT_MAX_ADAPTER_TYPES;
	int network_id = -1;
	nrt_protocol_table_t *protocol_table = NULL;
	nrt_adapter_t def_adapter_type = NRT_ADAP_UNSUPPORTED;
	int def_adapter_count = 0;
	int def_adapter_inx   = -1;


	if ((jp == NULL) || (jp->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_ERROR;
	}

	xassert(jp->magic == NRT_JOBINFO_MAGIC);
	xassert(tasks_per_node);

	if (dev_type != NRT_MAX_ADAPTER_TYPES)
		adapter_type = dev_type;

	nnodes = hostlist_count(hl);
	for (i = 0; i < nnodes; i++)
		nprocs += tasks_per_node[i];

	if ((nnodes <= 0) || (nprocs <= 0))
		slurm_seterrno_ret(EINVAL);

	jp->bulk_xfer   = (uint8_t) bulk_xfer;
	jp->bulk_xfer_resources = bulk_xfer_resources;
	jp->ip_v4       = (uint8_t) ip_v4;
	jp->job_key     = _next_key();
	jp->nodenames   = hostlist_copy(hl);
	jp->num_tasks   = nprocs;
	jp->user_space  = (uint8_t) user_space;

	/*
	 * Peek at the first host to figure out tables_per_task and adapter
	 * type. This driver assumes that all nodes have the same number of
	 * adapters per node.  Bad things will happen if this assumption is
	 * incorrect.
	 */
	hi = hostlist_iterator_create(hl);
	host = hostlist_next(hi);
	slurm_mutex_lock(&global_lock);
	node = _find_node(nrt_state, host);
	if (host != NULL)
		free(host);
	if (node && node->adapter_list) {
		for (i = 0; i < node->adapter_count; i++) {
			nrt_adapter_t ad_type;
			/* Match specific adapter name */
			if (adapter_name &&
			    xstrcmp(adapter_name,
				    node->adapter_list[i].adapter_name)) {
				continue;
			}
			/* Match specific adapter type (IB, HFI, etc) */
			ad_type = node->adapter_list[i].adapter_type;
			if ((adapter_type != NRT_MAX_ADAPTER_TYPES) &&
			    (adapter_type != ad_type))
				continue;
			if (jp->user_space && (ad_type == NRT_IPONLY))
				continue;

			/* Identify highest-priority adapter type */
			if (_adapter_type_pref(def_adapter_type) <
			    _adapter_type_pref(ad_type)) {
				def_adapter_type  = ad_type;
				def_adapter_count = 1;
				def_adapter_inx   = i;
			} else if (_adapter_type_pref(def_adapter_type) ==
			           _adapter_type_pref(ad_type)) {
				def_adapter_count++;
			}
		}
		if (!sn_all && (def_adapter_count > 0)) {
			if (!adapter_name) {
				adapter_name = node->
					       adapter_list[def_adapter_inx].
					       adapter_name;
			}
			network_id = node->adapter_list[def_adapter_inx].
				     network_id;
			def_adapter_count = 1;
		}
		if ((adapter_type == NRT_MAX_ADAPTER_TYPES) &&
		    (def_adapter_count > 0))
			adapter_type = def_adapter_type;
	}
	if (def_adapter_count >= 1) {
		jp->tables_per_task = def_adapter_count;
	} else {
		jp->tables_per_task = 0;
		info("switch/nrt: no adapter found for job");
	}
	slurm_mutex_unlock(&global_lock);
	if (jp->tables_per_task == 0) {
		hostlist_iterator_destroy(hi);
		return SLURM_FAILURE;
	}
	hostlist_iterator_reset(hi);

	/* Even for 1 node jobs the network needs to be set up. */

	if (adapter_type == NRT_IPONLY) {
		/* If tables_per_task != 0 for adapter_type == NRT_IPONLY
		 * then the device's window count in NRT is incremented.
		 * When we later read the adapter information, the adapter
		 * reports a maximum window count of zero and a current
		 * window count that is non zero. However, setting the value
		 * to zero results in the MPI job failing. This appears to
		 * be due to a bug in IBM's NRT library. */
		/* jp->tables_per_task = 0; */
	}
	if ((adapter_type == NRT_HFI) && jp->user_space) {
		jp->cau_indexes = (uint16_t) cau;
		jp->immed_slots = (uint16_t) immed;
	} else {
		/* The table load will always fail if cau_indexes or
		 * immed_slots are non-zero unless running on an HFI network
		 * with User Space communications, so ignore user options.
		 * Alternately we can check for non-zero user option and
		 * return SLURM_FAILURE here. */
		if ((cau != 0) || (immed != 0)) {
			debug("switch/nrt: cau:%hu immed:%hu ignored for job",
			      cau, immed);
		}
		jp->cau_indexes = 0;
		jp->immed_slots = 0;
	}

	if (instances <= 0) {
		info("switch/nrt: invalid instances specification (%d)",
		     instances);
		hostlist_iterator_destroy(hi);
		return SLURM_FAILURE;
	}
	jp->tables_per_task *= instances;

	protocol_table = _get_protocol_table(protocol);
	if ((protocol_table == NULL) ||
	    (protocol_table->protocol_table_cnt <= 0)) {
		info("switch/nrt: invalid protocol specification (%s)",
		     protocol);
		xfree(protocol_table);
		hostlist_iterator_destroy(hi);
		return SLURM_FAILURE;
	}
	jp->tables_per_task *= protocol_table->protocol_table_cnt;

	/* Allocate memory for each nrt_tableinfo_t */
	jp->tableinfo = (nrt_tableinfo_t *) xmalloc(jp->tables_per_task *
						    sizeof(nrt_tableinfo_t));
	for (i = 0; i < jp->tables_per_task; i++) {
		jp->tableinfo[i].table_length = nprocs;
		/* jp->tableinfo[i].table allocated with windows function */
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("Allocating windows: adapter_name:%s adapter_type:%s",
		     adapter_name, _adapter_type_str(adapter_type));
	} else {
		debug3("Allocating windows");
	}

	if (jp->tables_per_task) {
		slurm_mutex_lock(&global_lock);
		for  (i = 0; i < nnodes; i++) {
			host = hostlist_next(hi);
			if (!host)
				error("Failed to get next host");

			for (j = 0; j < tasks_per_node[i]; j++) {
				if (adapter_name == NULL) {
					rc = _allocate_windows_all(jp, host, i,
								tids[i][j],
								adapter_type,
								network_id,
								protocol_table,
								instances, j);
				} else {
					rc = _allocate_window_single(
								adapter_name,
								jp, host, i,
								tids[i][j],
								adapter_type,
								network_id,
								protocol_table,
								instances, j);
				}
				if (rc != SLURM_SUCCESS) {
					slurm_mutex_unlock(&global_lock);
					goto fail;
				}
			}
			free(host);
		}
		slurm_mutex_unlock(&global_lock);
	}


	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_build_jobinfo");
		_print_jobinfo(jp);
	}

	hostlist_iterator_destroy(hi);
	xfree(protocol_table);
	return SLURM_SUCCESS;

fail:
	free(host);
	hostlist_iterator_destroy(hi);
	xfree(protocol_table);
	(void) nrt_job_step_complete(jp, hl);	/* Release resources already
						 * allocated */
	/* slurmctld will call nrt_free_jobinfo(jp) to free memory */
	return SLURM_FAILURE;
}

static void
_pack_tableinfo(nrt_tableinfo_t *tableinfo, Buf buf, slurm_nrt_jobinfo_t *jp,
		uint16_t protocol_version)
{
	uint32_t adapter_type;
	bool ip_v4;
	int i, j;

	xassert(tableinfo);
	xassert(jp);

	ip_v4 = jp->ip_v4;
	packmem(tableinfo->adapter_name, NRT_MAX_ADAPTER_NAME_LEN, buf);
	adapter_type = tableinfo->adapter_type;
	pack32(adapter_type, buf);
	pack16(tableinfo->context_id, buf);
	pack32(tableinfo->instance, buf);
	pack64(tableinfo->network_id, buf);
	packmem(tableinfo->protocol_name, NRT_MAX_PROTO_NAME_LEN, buf);
	if (!jp->user_space)
		adapter_type = NRT_IPONLY;
	pack16(tableinfo->table_id, buf);
	pack32(tableinfo->table_length, buf);

	if (adapter_type == NRT_IB) {
		nrt_ib_task_info_t *ib_tbl_ptr;
		for (i = 0, ib_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, ib_tbl_ptr++) {
			packmem(ib_tbl_ptr->device_name,
				NRT_MAX_DEVICENAME_SIZE, buf);
			pack32(ib_tbl_ptr->base_lid, buf);
			pack8(ib_tbl_ptr->lmc, buf);
			pack32(ib_tbl_ptr->node_number, buf);
			pack8(ib_tbl_ptr->port_id, buf);
			pack32(ib_tbl_ptr->task_id, buf);
			pack16(ib_tbl_ptr->win_id, buf);
		}
	} else if (adapter_type == NRT_IPONLY) {
		nrt_ip_task_info_t *ip_tbl_ptr;
		for (i = 0, ip_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, ip_tbl_ptr++) {
			if (ip_v4) {
				packmem((char *) &ip_tbl_ptr->ip.ipv4_addr,
					sizeof(in_addr_t), buf);
			} else {
				for (j = 0; j < 16; j++) {
					pack8(ip_tbl_ptr->ip.ipv6_addr.
					      s6_addr[j], buf);
				}
			}
			pack32(ip_tbl_ptr->node_number, buf);
			pack16(ip_tbl_ptr->reserved, buf);
			pack32(ip_tbl_ptr->task_id, buf);
		}
	} else if (adapter_type == NRT_HFI) {
		nrt_hfi_task_info_t *hfi_tbl_ptr;
		for (i = 0, hfi_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, hfi_tbl_ptr++) {
			uint16_t tmp_16;
			uint8_t  tmp_8;
			pack32(hfi_tbl_ptr->task_id, buf);
			tmp_16 = hfi_tbl_ptr->lid;
			pack16(tmp_16, buf);
			tmp_8 = hfi_tbl_ptr->lpar_id;
			pack8(tmp_8, buf);
			tmp_8 = hfi_tbl_ptr->win_id;
			pack8(tmp_8, buf);
		}
	}
#if NRT_VERSION < 1300
	else if ((adapter_type == NRT_HPCE) || (adapter_type == NRT_KMUX)) {
		nrt_hpce_task_info_t *hpce_tbl_ptr;
		for (i = 0, hpce_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, hpce_tbl_ptr++) {
			pack32(hpce_tbl_ptr->task_id, buf);
			pack16(hpce_tbl_ptr->win_id, buf);
			pack32(hpce_tbl_ptr->node_number, buf);
			packmem(hpce_tbl_ptr->device_name,
				NRT_MAX_DEVICENAME_SIZE, buf);
		}
	}
#endif
	else {
		error("_pack_tableinfo: Missing support for adapter type %s",
		      _adapter_type_str(adapter_type));
	}
}

/* Used by: all */
extern int
nrt_pack_jobinfo(slurm_nrt_jobinfo_t *j, Buf buf, uint16_t protocol_version)
{
	int i;

	xassert(buf);

	/*
	 * There is nothing to pack, so pack in magic telling unpack not to
	 * attempt to unpack anything.
	 */
	if ((j == NULL) || (j->magic == NRT_NULL_MAGIC)) {
		pack32(NRT_NULL_MAGIC, buf);
		return SLURM_SUCCESS;
	}

	xassert(j->magic == NRT_JOBINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_pack_jobinfo:");
		_print_jobinfo(j);
	}

	pack32(j->magic, buf);
	pack32(j->job_key, buf);
	pack8(j->bulk_xfer, buf);
	pack32(j->bulk_xfer_resources, buf);
	pack16(j->cau_indexes, buf);
	pack16(j->immed_slots, buf);
	pack8(j->ip_v4, buf);
	pack8(j->user_space, buf);
	pack16(j->tables_per_task, buf);
	pack32(j->num_tasks, buf);

	for (i = 0; i < j->tables_per_task; i++)
		_pack_tableinfo(&j->tableinfo[i], buf, j, protocol_version);

	return SLURM_SUCCESS;
}

/* return 0 on success, -1 on failure */
static int
_unpack_tableinfo(nrt_tableinfo_t *tableinfo, Buf buf, slurm_nrt_jobinfo_t *jp,
		  uint16_t protocol_version)
{
	uint32_t tmp_32, adapter_type;
	uint16_t tmp_16;
	uint8_t  tmp_8;
	char *name_ptr;
	int i, j;
	bool ip_v4;

	xassert(jp);
	xassert(tableinfo);

	safe_unpackmem_ptr(&name_ptr, &tmp_32, buf);
	if (tmp_32 != NRT_MAX_ADAPTER_NAME_LEN)
		goto unpack_error;
	memcpy(tableinfo->adapter_name, name_ptr, tmp_32);
	safe_unpack32(&adapter_type, buf);
	tableinfo->adapter_type = (int) adapter_type;
	safe_unpack16(&tableinfo->context_id, buf);
	safe_unpack32(&tableinfo->instance, buf);
	safe_unpack64(&tableinfo->network_id, buf);
	safe_unpackmem_ptr(&name_ptr, &tmp_32, buf);
	if (tmp_32 != NRT_MAX_PROTO_NAME_LEN)
		goto unpack_error;
	memcpy(tableinfo->protocol_name, name_ptr, tmp_32);
	ip_v4 = jp->ip_v4;
	if (!jp->user_space)
		adapter_type = NRT_IPONLY;
	safe_unpack16(&tableinfo->table_id, buf);
	safe_unpack32(&tableinfo->table_length, buf);

	if (adapter_type == NRT_IB) {
		nrt_ib_task_info_t *ib_tbl_ptr;
		tableinfo->table = (nrt_ib_task_info_t *)
				   xmalloc(tableinfo->table_length *
				   sizeof(nrt_ib_task_info_t));
		for (i = 0, ib_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, ib_tbl_ptr++) {
			safe_unpackmem(ib_tbl_ptr->device_name, &tmp_32, buf);
			if (tmp_32 != NRT_MAX_DEVICENAME_SIZE)
				goto unpack_error;
			safe_unpack32(&ib_tbl_ptr->base_lid, buf);
			safe_unpack8(&ib_tbl_ptr->lmc, buf);
			safe_unpack32(&ib_tbl_ptr->node_number, buf);
			safe_unpack8(&ib_tbl_ptr->port_id, buf);
			safe_unpack32(&ib_tbl_ptr->task_id, buf);
			safe_unpack16(&ib_tbl_ptr->win_id, buf);
		}
	} else if (adapter_type == NRT_IPONLY) {
		nrt_ip_task_info_t *ip_tbl_ptr;
		tableinfo->table = (nrt_ip_task_info_t *)
				   xmalloc(tableinfo->table_length *
				   sizeof(nrt_ip_task_info_t));
		for (i = 0, ip_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, ip_tbl_ptr++) {
			if (ip_v4) {
				safe_unpackmem((char *)
					       &ip_tbl_ptr->ip.ipv4_addr,
					       &tmp_32, buf);
				if (tmp_32 != sizeof(in_addr_t))
					goto unpack_error;
			} else {
				for (j = 0; j < 16; j++) {
					safe_unpack8(&ip_tbl_ptr->ip.ipv6_addr.
						     s6_addr[j], buf);
				}
			}
			safe_unpack32(&ip_tbl_ptr->node_number, buf);
			safe_unpack16(&ip_tbl_ptr->reserved, buf);
			safe_unpack32(&ip_tbl_ptr->task_id, buf);
		}
	} else if (adapter_type == NRT_HFI) {
		nrt_hfi_task_info_t *hfi_tbl_ptr;
		tableinfo->table = (nrt_hfi_task_info_t *)
				   xmalloc(tableinfo->table_length *
				   sizeof(nrt_hfi_task_info_t));
		for (i = 0, hfi_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, hfi_tbl_ptr++) {

			safe_unpack32(&hfi_tbl_ptr->task_id, buf);
			safe_unpack16(&tmp_16, buf);
			hfi_tbl_ptr->lid = tmp_16;
			safe_unpack8(&tmp_8, buf);
			hfi_tbl_ptr->lpar_id = tmp_8;
			safe_unpack8(&tmp_8, buf);
			hfi_tbl_ptr->win_id = tmp_8;
		}
	}
#if NRT_VERSION < 1300
	else if ((adapter_type == NRT_HPCE) || (adapter_type == NRT_KMUX)) {
		nrt_hpce_task_info_t *hpce_tbl_ptr;
		tableinfo->table = (nrt_hpce_task_info_t *)
				   xmalloc(tableinfo->table_length *
				   sizeof(nrt_hpce_task_info_t));
		for (i = 0, hpce_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, hpce_tbl_ptr++) {
			safe_unpack32(&hpce_tbl_ptr->task_id, buf);
			safe_unpack16(&hpce_tbl_ptr->win_id, buf);
			safe_unpack32(&hpce_tbl_ptr->node_number, buf);
			safe_unpackmem(hpce_tbl_ptr->device_name, &tmp_32,buf);
			if (tmp_32 != NRT_MAX_DEVICENAME_SIZE)
				goto unpack_error;
		}
	}
#endif
	else {
		error("_unpack_tableinfo: Missing support for adapter type %s",
		      _adapter_type_str(adapter_type));
	}

	return SLURM_SUCCESS;

unpack_error: /* safe_unpackXX are macros which jump to unpack_error */
	error("unpack error in _unpack_tableinfo");
	return SLURM_ERROR;
}

/* Used by: all */
extern int
nrt_unpack_jobinfo(slurm_nrt_jobinfo_t **j_pptr, Buf buf,
		   uint16_t protocol_version)
{
	int i;
	slurm_nrt_jobinfo_t *j;

	xassert(j_pptr);
	xassert(buf);

	nrt_alloc_jobinfo(j_pptr);
	j = *j_pptr;

	safe_unpack32(&j->magic, buf);

	if (j->magic == NRT_NULL_MAGIC) {
		debug2("(%s: %d: %s) Nothing to unpack.",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_SUCCESS;
	}

	xassert(j->magic == NRT_JOBINFO_MAGIC);

	safe_unpack32(&j->job_key, buf);
	safe_unpack8(&j->bulk_xfer, buf);
	safe_unpack32(&j->bulk_xfer_resources, buf);
	safe_unpack16(&j->cau_indexes, buf);
	safe_unpack16(&j->immed_slots, buf);
	safe_unpack8(&j->ip_v4, buf);
	safe_unpack8(&j->user_space, buf);
	safe_unpack16(&j->tables_per_task, buf);
	safe_unpack32(&j->num_tasks, buf);

	j->tableinfo = (nrt_tableinfo_t *) xmalloc(j->tables_per_task *
						   sizeof(nrt_tableinfo_t));
	for (i = 0; i < j->tables_per_task; i++) {
		if (_unpack_tableinfo(&j->tableinfo[i], buf, j,
				      protocol_version))
			goto unpack_error;
	}

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_unpack_jobinfo:");
		_print_jobinfo(j);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("nrt_unpack_jobinfo error");

	nrt_free_jobinfo(*j_pptr);
	*j_pptr = NULL;

	slurm_seterrno_ret(EUNPACK);
	return SLURM_ERROR;
}

/* Used by: all */
extern void
nrt_free_jobinfo(slurm_nrt_jobinfo_t *jp)
{
	int i;
	nrt_tableinfo_t *tableinfo;

	if (!jp)
		return;

	if (jp->magic != NRT_JOBINFO_MAGIC) {
		error("jp is not a switch/nrt slurm_nrt_jobinfo_t");
		return;
	}

	jp->magic = 0;
	if ((jp->tables_per_task > 0) && (jp->tableinfo != NULL)) {
		for (i = 0; i < jp->tables_per_task; i++) {
			tableinfo = &jp->tableinfo[i];
			xfree(tableinfo->table);
		}
	}
	xfree(jp->tableinfo);
	if (jp->nodenames)
		hostlist_destroy(jp->nodenames);

	xfree(jp);
	jp = NULL;

	return;
}

/* Return data to code for which jobinfo is an opaque type.
 *
 * Used by: all
 */
extern int
nrt_get_jobinfo(slurm_nrt_jobinfo_t *jp, int key, void *data)
{
	nrt_tableinfo_t **tableinfo = (nrt_tableinfo_t **) data;
	int *tables_per = (int *) data;
	int *job_key = (int *) data;

	if ((jp == NULL) || (jp->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_SUCCESS;
	}

	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	switch (key) {
		case NRT_JOBINFO_TABLEINFO:
			*tableinfo = jp->tableinfo;
			break;
		case NRT_JOBINFO_TABLESPERTASK:
			*tables_per = (int) jp->tables_per_task;
			break;
		case NRT_JOBINFO_KEY:
			*job_key = (int) jp->job_key;
			break;
		default:
			slurm_seterrno_ret(EINVAL);
	}

	return SLURM_SUCCESS;
}

/*
 * Check up to "retry" times for "window_id" on "adapter_name"
 * to switch to the NRT_WIN_AVAILABLE.  Sleep one second between
 * each retry.
 *
 * Used by: slurmd
 */
static int
_wait_for_window_unloaded(char *adapter_name, nrt_adapter_t adapter_type,
			  nrt_window_id_t window_id, int retry)
{
	int err, i, j;
	int rc = SLURM_ERROR;
	nrt_cmd_status_adapter_t status_adapter;
	nrt_status_t *status_array = NULL;
	nrt_window_id_t window_count;

	status_adapter.adapter_name = adapter_name;
	status_adapter.adapter_type = adapter_type;
	status_adapter.status_array = &status_array;
	status_adapter.window_count = &window_count;

	for (i = 0; i < retry; i++) {
		if (i > 0)
			usleep(100000);

		if (status_array) {
			free(status_array);
			status_array = NULL;
		}
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
				   &status_adapter);
		if (err != NRT_SUCCESS) {
			error("nrt_status_adapter(%s, %s): %s", adapter_name,
			      _adapter_type_str(adapter_type),
			      nrt_err_str(err));
			break;
		}
		if (debug_flags & DEBUG_FLAG_SWITCH) {
			info("_wait_for_window_unloaded");
			_print_adapter_status(&status_adapter);
		}
		if (!status_array)	/* Fix for CLANG false positive */
			break;
		for (j = 0; j < window_count; j++) {
			if (status_array[j].window_id == window_id)
				break;
		}
		if (j >= window_count) {
			error("nrt_status_adapter(%s, %s), window %hu not "
			      "found",
			      adapter_name, _adapter_type_str(adapter_type),
			      window_id);
			break;
		}
		debug2("nrt_status_adapter(%s, %s), window %u state %s",
		       adapter_name,
		       _adapter_type_str(adapter_type), window_id,
		       _win_state_str(status_array[j].state));
		if (status_array[j].state == NRT_WIN_AVAILABLE) {
			rc = SLURM_SUCCESS;
			break;
		}
	}
	if (status_array)
		free(status_array);

	return rc;
}

/*
 * Look through the table and find all of the NRT that are for an adapter on
 * this node.  Wait until the window from each local NRT is in the
 * NRT_WIN_AVAILABLE.
 *
 * Used by: slurmd
 */
static int
_wait_for_all_windows(nrt_tableinfo_t *tableinfo)
{
	int err, i, rc = SLURM_SUCCESS;
	int retry = 15;
	nrt_window_id_t window_id = 0;

	if (!my_lpar_id_set && !my_network_id_set)
		_get_my_id();

	for (i = 0; i < tableinfo->table_length; i++) {
		if (tableinfo->adapter_type == NRT_IB) {
			nrt_ib_task_info_t *ib_tbl_ptr;
			ib_tbl_ptr = (nrt_ib_task_info_t *) tableinfo->table;
			ib_tbl_ptr += i;
			if (ib_tbl_ptr->node_number != my_network_id)
				continue;
			window_id = ib_tbl_ptr->win_id;
		} else if (tableinfo->adapter_type == NRT_HFI) {
			nrt_hfi_task_info_t *hfi_tbl_ptr;
			hfi_tbl_ptr = (nrt_hfi_task_info_t *) tableinfo->table;
			hfi_tbl_ptr += i;
			if ((hfi_tbl_ptr->lpar_id != my_lpar_id) ||
			    (hfi_tbl_ptr->lid != my_lid))
				continue;
			window_id = hfi_tbl_ptr->win_id;
		}
#if NRT_VERSION < 1300
		else if ((tableinfo->adapter_type == NRT_HPCE) ||
		           (tableinfo->adapter_type == NRT_KMUX)) {
			nrt_hpce_task_info_t *hpce_tbl_ptr;
			hpce_tbl_ptr = (nrt_hpce_task_info_t *) tableinfo->
								table;
			hpce_tbl_ptr += i;
			if (hpce_tbl_ptr->node_number != my_network_id)
				continue;
			window_id = hpce_tbl_ptr->win_id;
		}
#endif
		else {
			error("_wait_for_all_windows: Missing support for "
			      "adapter_type %s",
			      _adapter_type_str(tableinfo->adapter_type));
		}

		err = _wait_for_window_unloaded(tableinfo->adapter_name,
						tableinfo->adapter_type,
						window_id, retry);
		if (err != SLURM_SUCCESS) {
			error("Window %hu adapter %s did not "
			      "become free within %d seconds",
			      window_id, tableinfo->adapter_name, retry);
			rc = err;
		}
	}

	return rc;
}

/* Load a network table on node.  If table contains more than one window
 * for a given adapter, load the table only once for that adapter.
 *
 * Used by: slurmd
 *
 * Notes on context_id and table_id from Bill LePera, IBM, 6/7/2012:
 *
 * Each NRT is uniquely identified by a combination of three elements: job_key,
 * context_id, and table_id.  context_id and table_id usually start at zero and
 * are incremented based on how many NRTs are required to define all the
 * resources used for a job, based on factors like striping, instances, and
 * number of protocols.
 *
 * For example, a scheduler building an NRT for a job using a single protocol,
 * single network (no striping), and a single instance would set both
 * context_id and table_id to zero.  A multi-protocol job (one that used both
 * MPI and PAMI, for example), would build at least one NRT for each protocol.
 * In this case, there would be two NRTs, with context_id 0 and 1.  If you are
 * still using a single network and single instance, the table_id's for both
 * NRTs would be zero, and these would be the only two NRTs needed for the job.
 *
 * The table_id is incremented on a per-protocol basis, based on number of
 * networks (or stripes) and number of instances.  For example, a single-
 * protocol job running across two networks using four instances would need
 * 2 * 4 = 8 NRTs, with context_id set to 0 for each, and table_id 0 - 7.  If
 * this same job was a multi-protocol job, you would need 16 NRTs total
 * (2 protocols * 2 networks * 4 instances), with context_id 0 and 1, and
 * table_id 0-7 within each protocol.
 */
extern int
nrt_load_table(slurm_nrt_jobinfo_t *jp, int uid, int pid, char *job_name)
{
	int i;
	int err;
	char *adapter_name;
	int rc;
	nrt_cmd_load_table_t load_table;
	nrt_table_info_t table_info;

	if ((jp == NULL) || (jp->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_ERROR;
	}

	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_load_table");
		_print_jobinfo(jp);
	}

	for (i = 0; i < jp->tables_per_task; i++) {
		if (debug_flags & DEBUG_FLAG_SWITCH) {
			nrt_adapter_t adapter_type;
			if (jp->user_space)
				adapter_type = jp->tableinfo[i].adapter_type;
			else
				adapter_type = NRT_IPONLY;
			_print_table(jp->tableinfo[i].table,
				     jp->tableinfo[i].table_length,
				     adapter_type, jp->ip_v4);
		}

		adapter_name = jp->tableinfo[i].adapter_name;
		if (jp->user_space) {
			rc = _wait_for_all_windows(&jp->tableinfo[i]);
			if (rc != SLURM_SUCCESS)
				return rc;
		}

		if (adapter_name == NULL)
			continue;

		memset(&table_info, 0, sizeof(nrt_table_info_t));
		table_info.num_tasks = jp->tableinfo[i].table_length;
		table_info.job_key = jp->job_key;
		/* Enable job preeption and release of resources */
#ifdef PREEMPT_RELEASE_RESOURCES_MASK
		table_info.job_options = PREEMPT_RELEASE_RESOURCES_MASK;
#endif
		table_info.uid = uid;
		table_info.network_id = jp->tableinfo[i].network_id;
		table_info.pid = pid;
		table_info.adapter_type = jp->tableinfo[i].adapter_type;
		if (jp->user_space)
			table_info.is_user_space = 1;
		if (jp->ip_v4)
			table_info.is_ipv4 = 1;
		/* IP V6: table_info.is_ipv4 initialized above by memset() */
		table_info.context_id = jp->tableinfo[i].context_id;
		table_info.table_id = jp->tableinfo[i].table_id;
		if (job_name) {
			char *sep = strrchr(job_name,'/');
			if (sep)
				sep++;
			else
				sep = job_name;
			strlcpy(table_info.job_name, sep,
				NRT_MAX_JOB_NAME_LEN);
		} else {
			table_info.job_name[0] = '\0';
		}
		strlcpy(table_info.protocol_name,
			jp->tableinfo[i].protocol_name,
			NRT_MAX_PROTO_NAME_LEN);
		table_info.use_bulk_transfer = jp->bulk_xfer;
		table_info.bulk_transfer_resources = jp->bulk_xfer_resources;
		/* The following fields only apply to Power7 processors
		 * and have no effect on x86 processors:
		 * immed_send_slots_per_win
		 * num_cau_indexes */
		table_info.num_cau_indexes = jp->cau_indexes;
		table_info.immed_send_slots_per_win = jp->immed_slots;
		load_table.table_info = &table_info;
		load_table.per_task_input = jp->tableinfo[i].table;

		if (debug_flags & DEBUG_FLAG_SWITCH)
			_print_load_table(&load_table);

		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_LOAD_TABLE,
				   &load_table);
		if (err != NRT_SUCCESS) {
			error("nrt_cmd_wrap(load table): %s", nrt_err_str(err));
			return SLURM_ERROR;
		}
	}
	umask(nrt_umask);

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_load_table complete");

	return SLURM_SUCCESS;
}

static int
_unload_window_all_jobs(char *adapter_name, nrt_adapter_t adapter_type,
			nrt_window_id_t window_id)
{
	int err, i;
	nrt_cmd_unload_window_t unload_window;
	nrt_cmd_query_jobs_t nrt_jobs;
	nrt_job_key_t job_count, *job_keys = NULL;

	nrt_jobs.adapter_name = adapter_name;
	nrt_jobs.adapter_type = adapter_type;
	nrt_jobs.job_count = &job_count;
	nrt_jobs.job_keys = &job_keys;
	err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_JOBS, &nrt_jobs);
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(query_jobs, %s, %s): %s",
		       adapter_name, _adapter_type_str(adapter_type),
		       nrt_err_str(err));
		if (job_keys)
			free(job_keys);
		return err;
	}

	for (i = 0; i < job_count; i++) {
		unload_window.adapter_name = adapter_name;
		unload_window.adapter_type = adapter_type;
		unload_window.job_key = job_keys[i];
		unload_window.window_id = window_id;

		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_UNLOAD_WINDOW,
				   &unload_window);
		if (err == NRT_SUCCESS) {
			info("nrt_cmd_wrap(unload_window, %s, %s, %u, %hu)",
			      adapter_name, _adapter_type_str(adapter_type),
			      job_keys[i], window_id);
		}
	}

	if (job_keys)
		free(job_keys);
	return SLURM_FAILURE;
}

static int _unload_job_table(slurm_nrt_jobinfo_t *jp)
{
	int err, i, rc = SLURM_SUCCESS;
	nrt_cmd_unload_table_t unload_table;

	unload_table.job_key = jp->job_key;
	for (i = 0; i < jp->tables_per_task; i++) {
		unload_table.context_id = jp->tableinfo[i].context_id;
		unload_table.table_id   = jp->tableinfo[i].table_id;
		if (debug_flags & DEBUG_FLAG_SWITCH) {
			info("Unload table for job_key:%u "
			     "context_id:%u table_id:%u",
			     unload_table.job_key, unload_table.context_id,
			     unload_table.table_id);
			}
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_UNLOAD_TABLE,
				   &unload_table);
		if (err != NRT_SUCCESS) {
			error("Unable to unload table for job_key:%u "
			      "context_id:%u table_id:%u error:%s",
			      unload_table.job_key, unload_table.context_id,
			      unload_table.table_id, nrt_err_str(err));
			rc = SLURM_ERROR;
		}
	}
	return rc;
}

/* Assumes that, on error, new switch state information will be
 * read from node.
 *
 * Used by: slurmd
 */
extern int
nrt_unload_table(slurm_nrt_jobinfo_t *jp)
{
	if ((jp == NULL) || (jp->magic == NRT_NULL_MAGIC)) {
		debug2("(%s: %d: %s) job->switch_job was NULL",
		       THIS_FILE, __LINE__, __func__);
		return SLURM_ERROR;
	}

	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("nrt_unload_table");
		_print_jobinfo(jp);
	}

	return _unload_job_table(jp);
}

extern int
nrt_fini(void)
{
	return SLURM_SUCCESS;
}

static void
_free_libstate(slurm_nrt_libstate_t *lp)
{
	int i;

	if (!lp)
		return;
	if (lp->node_list != NULL) {
		for (i = 0; i < lp->node_count; i++)
			nrt_free_nodeinfo(&lp->node_list[i], true);
		xfree(lp->node_list);
	}
	xfree(lp->hash_table);
	xfree(lp);
}

/* Used by: slurmctld */
static int
_pack_libstate(slurm_nrt_libstate_t *lp, Buf buffer, uint16_t protocol_version)
{
	int offset;
	int i;

	xassert(lp);
	xassert(lp->magic == NRT_LIBSTATE_MAGIC);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
 		info("_pack_libstate");
		_print_libstate(lp);
	}

	offset = get_buf_offset(buffer);
	packstr(NRT_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack32(lp->magic, buffer);
	pack32(lp->node_count, buffer);
	for (i = 0; i < lp->node_count; i++)
		(void)nrt_pack_nodeinfo(&lp->node_list[i], buffer,
					protocol_version);
	/* don't pack hash_table, we'll just rebuild on restore */
	pack32(lp->key_index, buffer);

	return(get_buf_offset(buffer) - offset);
}

/* Used by: slurmctld */
extern void
nrt_libstate_save(Buf buffer, bool free_flag)
{
	slurm_mutex_lock(&global_lock);

	if (nrt_state != NULL)
		_pack_libstate(nrt_state, buffer, SLURM_PROTOCOL_VERSION);

	/* Clean up nrt_state since backup slurmctld can repeatedly
	 * save and restore state */
	if (free_flag) {
		_free_libstate(nrt_state);
		nrt_state = NULL;	/* freed above */
	}
	slurm_mutex_unlock(&global_lock);
}

/* Used by: slurmctld */
static int
_unpack_libstate(slurm_nrt_libstate_t *lp, Buf buffer)
{
	char *ver_str = NULL;
	uint32_t ver_str_len;
	uint16_t protocol_version = NO_VAL16;
	uint32_t node_count;
	int i;

	/* Validate state version */
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in job_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, NRT_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);

	if (protocol_version == NO_VAL16) {
		error("******************************************************");
		error("Can not recover switch/nrt state, incompatible version");
		error("******************************************************");
		xfree(ver_str);
		return EFAULT;
	}
	xfree(ver_str);

	xassert(lp->magic == NRT_LIBSTATE_MAGIC);
	safe_unpack32(&lp->magic, buffer);
	safe_unpack32(&node_count, buffer);
	for (i = 0; i < node_count; i++) {
		if (_unpack_nodeinfo(NULL, buffer, false,
				     protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
	}
	if (lp->node_count != node_count) {
		error("Failed to recover switch state of all nodes (%u of %u)",
		      lp->node_count, node_count);
		return SLURM_ERROR;
	}
	safe_unpack32(&lp->key_index, buffer);

	if (debug_flags & DEBUG_FLAG_SWITCH) {
	 	info("_unpack_libstate");
		_print_libstate(lp);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("unpack error in _unpack_libstate");
	slurm_seterrno_ret(EBADMAGIC_NRT_LIBSTATE);
	return SLURM_ERROR;
}

/* Used by: slurmctld */
extern int
nrt_libstate_restore(Buf buffer)
{
	int rc;

	slurm_mutex_lock(&global_lock);
	xassert(!nrt_state);

	nrt_state = _alloc_libstate();
	if (!nrt_state) {
		error("nrt_libstate_restore nrt_state is NULL");
		slurm_mutex_unlock(&global_lock);
		return SLURM_FAILURE;
	}
	rc = _unpack_libstate(nrt_state, buffer);
	slurm_mutex_unlock(&global_lock);

	return rc;
}

extern int
nrt_libstate_clear(void)
{
	int i, j, k;
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter;
	slurm_nrt_window_t *window;


	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("Clearing state on all windows in global NRT state");
	else
		debug3("Clearing state on all windows in global NRT state");

	slurm_mutex_lock(&global_lock);
	if (!nrt_state || !nrt_state->node_list) {
		error("nrt_state or node_list not initialized!");
		slurm_mutex_unlock(&global_lock);
		return SLURM_ERROR;
	}

	for (i = 0; i < nrt_state->node_count; i++) {
		node = &nrt_state->node_list[i];
		if (!node->adapter_list)
			continue;
		for (j = 0; j < node->adapter_count; j++) {
			adapter = &node->adapter_list[i];
			if (!adapter || !adapter->window_list)
				continue;
			for (k = 0; k < adapter->window_count; k++) {
				window = &adapter->window_list[k];
				if (!window)
					continue;
				window->state = NRT_WIN_UNAVAILABLE;
			}
		}
	}
	slurm_mutex_unlock(&global_lock);

	return SLURM_SUCCESS;
}

extern int
nrt_clear_node_state(void)
{
	static bool first_use = true;
	int err, i, j, k, rc = SLURM_SUCCESS;
	nrt_cmd_query_adapter_types_t adapter_types;
	unsigned int num_adapter_types;
	nrt_adapter_t adapter_type[NRT_MAX_ADAPTER_TYPES];
	nrt_cmd_query_adapter_names_t adapter_names;
	unsigned int max_windows, num_adapter_names;
	nrt_cmd_status_adapter_t adapter_status;
	nrt_window_id_t window_count;
	nrt_status_t *status_array = NULL;
	win_state_t state;
	nrt_cmd_clean_window_t clean_window;
	char window_str[128];
	bool orphan_procs = false;
	hostset_t hs = NULL;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_clear_node_state: begin");

	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	for (i = 0; i < 2; i++) {
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				   &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		error("Is pnsd daemon started? Retrying...");
		/* Run "/opt/ibmhpc/pecurrent/ppe.pami/pnsd/pnsd -A" */
		sleep(5);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		for (i = 0; i < num_adapter_types; i++) {
			info("nrt_cmd_wrap(adapter_types): %s",
			    _adapter_type_str(adapter_types.adapter_types[i]));
		}
	}

	for (i = 0; i < num_adapter_types; i++) {
		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				   &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_cmd_wrap(adapter_names, %s): %s",
			      _adapter_type_str(adapter_names.adapter_type),
			      nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
		if (debug_flags & DEBUG_FLAG_SWITCH) {
			for (j = 0; j < num_adapter_names; j++) {
				info("nrt_cmd_wrap(adapter_names, %s, %s) "
				     "max_windows: %hu",
				     adapter_names.adapter_names[j],
				     _adapter_type_str(adapter_names.
						       adapter_type),
				     max_windows);
			}
		}

		for (j = 0; j < num_adapter_names; j++) {
			if (status_array) {
				free(status_array);
				status_array = NULL;
			}
			adapter_status.adapter_name = adapter_names.
						      adapter_names[j];
			adapter_status.adapter_type = adapter_names.
						      adapter_type;
			adapter_status.status_array = &status_array;
			adapter_status.window_count = &window_count;
			err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
					   &adapter_status);
			if (err != NRT_SUCCESS) {
				error("nrt_cmd_wrap(status_adapter, %s, %s): %s",
				      adapter_status.adapter_name,
				      _adapter_type_str(adapter_status.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
			if (window_count > max_windows) {
				/* This happens if IP_ONLY devices are
				 * allocated with tables_per_task > 0 */
				char *reason;
				if (adapter_status.adapter_type == NRT_IPONLY)
					reason = ", Known libnrt bug";
				else
					reason = "";
				if (first_use) {
					error("nrt_cmd_wrap(status_adapter, "
					      "%s, %s): window_count > "
					      "max_windows (%u > %hu)%s",
					      adapter_status.adapter_name,
					      _adapter_type_str(adapter_status.
								adapter_type),
					      window_count, max_windows,
					      reason);
				} else {
					debug("nrt_cmd_wrap(status_adapter, "
					      "%s, %s): window_count > "
					      "max_windows (%u > %hu)%s",
					      adapter_status.adapter_name,
					      _adapter_type_str(adapter_status.
								adapter_type),
					      window_count, max_windows,
					      reason);
				}
				/* Reset value to avoid logging bad data */
				window_count = max_windows;
			}
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("nrt_cmd_wrap(status_adapter, %s, %s) "
				     "window_count: %hu",
				     adapter_status.adapter_name,
				     _adapter_type_str(adapter_status.
						       adapter_type),
				     window_count);
				for (k = 0; k < window_count; k++) {
					win_state_t state = status_array[k].
							    state;
					if ((state == NRT_WIN_AVAILABLE) &&
					    (k >= NRT_DEBUG_CNT))
						continue;
					info("window_id:%d uid:%d pid:%d "
					     "state:%s",
					     status_array[k].window_id,
					     status_array[k].uid,
					     status_array[k].client_pid,
					     _win_state_str(state));
				}

				hs = hostset_create("");
			}
			for (k = 0; k < window_count; k++) {
				if (debug_flags & DEBUG_FLAG_SWITCH) {
					snprintf(window_str,
						 sizeof(window_str), "%d",
						 clean_window.window_id);
					hostset_insert(hs, window_str);
				}
				state = status_array[k].state;
				if ((state == NRT_WIN_RESERVED) ||
				    (state == NRT_WIN_READY) ||
				    (state == NRT_WIN_RUNNING)) {
					_unload_window_all_jobs(
						adapter_status.adapter_name,
						adapter_status.adapter_type,
						status_array[k].window_id);
				}
				clean_window.adapter_name = adapter_names.
							    adapter_names[j];
				clean_window.adapter_type = adapter_names.
							    adapter_type;
				clean_window.leave_inuse_or_kill = KILL;
				clean_window.window_id = status_array[k].
							 window_id;
				err = nrt_cmd_wrap(NRT_VERSION,
						   NRT_CMD_CLEAN_WINDOW,
						   &clean_window);
				if (err == NRT_WRONG_WINDOW_STATE)
					orphan_procs = true;
				if (err != NRT_SUCCESS) {
					error("nrt_cmd_wrap(clean_window, "
					      "%s, %s, %u): %s",
					      clean_window.adapter_name,
					      _adapter_type_str(clean_window.
								adapter_type),
					      clean_window.window_id,
					      nrt_err_str(err));
					rc = SLURM_ERROR;
					continue;
				}
			}
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				if (hostset_count(hs) > 0) {
					hostset_ranged_string(hs,
							      sizeof(window_str),
							      window_str);
					info("nrt_cmd_wrap(clean_window, "
					     "%s, %s, %s)",
					     adapter_names.adapter_names[j],
					     _adapter_type_str(adapter_names.
							       adapter_type),
					     window_str);
				}
				hostset_destroy(hs);
			}
		}
	}
	if (status_array)
		free(status_array);
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_clear_node_state: complete:%d", rc);
	if (orphan_procs) {
		error("switch/nrt: THERE APPEAR TO BE ORPHAN PROCESSES "
		      "HOLDING SWITCH WINDOWS");
		error("switch/nrt: You must manually find and kill these "
		      "processes before using this node");
		error("switch/nrt: Use of ProctrackType=proctrack/cgroup "
		      "generally prevents this");
	}

	return rc;
}

extern char *nrt_err_str(int rc)
{
	static char str[16];

	switch (rc) {
	case NRT_ALREADY_LOADED:
		return "Already loaded";
	case NRT_BAD_VERSION:
		return "Bad version";
	case NRT_CAU_EXCEEDED:
		return "CAU index request exeeds available resources";
	case NRT_CAU_RESERVE:
		return "Error during CAU index reserve";
	case NRT_CAU_UNRESERVE:
		return "Error during CAU index unreserve";
	case NRT_EADAPTER:
		return "Invalid adapter name";
	case NRT_EADAPTYPE:
		return "Invalid adapter type";
	case NRT_EAGAIN:
		return "Try call again later";
	case NRT_EINVAL:
		return "Invalid input paramter";
	case NRT_EIO:
		return "Adapter reported a DOWN state";
	case NRT_EMEM:
		return "Memory allocation error";
	case NRT_EPERM:
		return "Permission denied, not root";
	case NRT_ERR_COMMAND_TYPE:
		return "Invalid command type";
	case NRT_ESYSTEM:
		return "A system error occured";
	case NRT_IMM_SEND_RESERVE:
		return "Error during immediate send slot reserve";
	case NRT_NO_FREE_WINDOW:
		return "No free window";
	case NRT_NO_RDMA_AVAIL:
		return "No RDMA windows available";
	case NRT_NTBL_LOAD_FAILED:
		return "Failed to load NTBL";
	case NRT_NTBL_NOT_FOUND:
		return "NTBL not found";
	case NRT_NTBL_UNLOAD_FAILED:
		return "Failed to unload NTBL";
	case NRT_OP_NOT_VALID:
		return "Requested operation not valid for given device";
	case NRT_PNSDAPI:
		return "Error communicating with Protocol Network Services "
		       "Daemon";
	case NRT_RDMA_CLEAN_FAILED:
		return "Task RDMA cleanup failed";
	case NRT_SUCCESS:
		return "Success";
	case NRT_TIMEOUT:
		return "No response back from PNSD/job";
	case NRT_UNKNOWN_ADAPTER:
		return "Unknown adaper";
	case NRT_WIN_CLOSE_FAILED:
		return "Task can not close window";
	case NRT_WIN_OPEN_FAILED:
		return "Task can not open window";
	case NRT_WRONG_PREEMPT_STATE:
		return "Invalid preemption state";
	case NRT_WRONG_WINDOW_STATE:
		return "Wrong window state";
	}

	snprintf(str, sizeof(str), "%d", rc);
	return str;
}


/* Determine if a token is the name of an adapter
 * IN token - token from job's "network" specification
 * IN list - hostlist of allocated nodes
 * RET - True if token is a adapter name, false otherwise */
extern bool nrt_adapter_name_check(char *token, hostlist_t hl)
{
	int i;
	hostlist_iterator_t hi;
	slurm_nrt_nodeinfo_t *node;
	char *host;
	bool name_found = false;

	if (!token || !hl)
		return name_found;

	hi = hostlist_iterator_create(hl);
	host = hostlist_next(hi);
	hostlist_iterator_destroy(hi);
	slurm_mutex_lock(&global_lock);
	node = _find_node(nrt_state, host);
	if (host)
		free(host);
	if (node && node->adapter_list) {
		for (i = 0; i < node->adapter_count; i++) {
			if (xstrcmp(token,node->adapter_list[i].adapter_name))
				continue;
			name_found = true;
			break;
		}
	}
	slurm_mutex_unlock(&global_lock);

	return name_found;
}

static preemption_state_t _job_preempt_state(nrt_job_key_t job_key)
{
	nrt_cmd_query_preemption_state_t preempt_state;
	preemption_state_t state;
	int err;

	preempt_state.job_key	= job_key;
	preempt_state.state	= &state;
	err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_QUERY_PREEMPTION_STATE,
			   &preempt_state);
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(preempt_state, %u): %s",
		      job_key, nrt_err_str(err));
		return PES_INIT;	/* No good return value for error */
	}
	return state;
}

static char *_job_state_str(preemption_state_t curr_state)
{
	static char buf[10];

	switch (curr_state) {
	case PES_INIT:
		return "Init";
	case PES_JOB_RUNNING:
		return "Running";
	case PES_PREEMPTION_INPROGRESS:
		return "Preemption_in_progress";
	case PES_JOB_PREEMPTED:
		return "Preempted";
	case PES_PREEMPTION_FAILED:
		return "Preemption_failed";
	case PES_RESUME_INPROGRESS:
		return "Resume_in_progress";
	case PES_RESUME_FAILED:
		return "Resume_failed";
	default:
		snprintf(buf, sizeof(buf), "%d", curr_state);
		return buf;
	}
}

/* Return 0 when job in desired state, -1 on error */
static int _wait_job(nrt_job_key_t job_key, preemption_state_t want_state,
		     int max_wait_secs)
{
	preemption_state_t curr_state;
	char *state_str = NULL;
	time_t start_time = time(NULL), now;
	int i;

	for (i = 0; ; i++) {
		if (i)
			usleep(100000);
		curr_state = _job_preempt_state(job_key);
		/* Job's state is initially PES_INIT, even when running.
		 * It only goes to state PES_JOB_RUNNING after suspend and
		 * resume. */
		if ((curr_state == want_state) ||
		    ((curr_state == PES_INIT) &&
		     (want_state == PES_JOB_RUNNING))) {
			debug("switch/nrt: Desired job state in %d msec",
			      (100 * i));
			return 0;
		}
		/* info("job_key:%u state:%d", job_key, curr_state); */
		if ((curr_state == PES_PREEMPTION_FAILED) ||
		    (curr_state == PES_RESUME_FAILED))
			return -1;
		if (want_state == PES_JOB_RUNNING) {
			if ((curr_state != PES_INIT) &&
			    (curr_state != PES_RESUME_INPROGRESS))
				return -1;
		} else if (want_state == PES_JOB_PREEMPTED) {
			if (curr_state != PES_PREEMPTION_INPROGRESS)
				return 0;
		} else {
			error("_wait_job: invalid desired state: %d",
			      want_state);
			return -1;
		}
		if (max_wait_secs) {
			now = time(NULL);
			if ((now - start_time) > max_wait_secs)
				break;
		}
	}

	if (want_state == PES_JOB_RUNNING)
		state_str = "Running";
	else if (want_state == PES_JOB_PREEMPTED)
		state_str = "Preempted";
	error("switch/nrt: Desired job state of %s not reached in %d sec, "
	      "Current job state is %s",
	      state_str, (int)(now - start_time), _job_state_str(curr_state));
	return -1;
}

extern int nrt_preempt_job_test(slurm_nrt_jobinfo_t *jp)
{
#ifdef PREEMPT_RELEASE_RESOURCES_MASK
	if (jp->cau_indexes) {
		info("Unable to preempt job with allocated CAU");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
#else
	info("switch/nrt: This version of libnrt.so does not support job "
	     "suspend/resume");
	return SLURM_ERROR;
#endif
}

extern void nrt_suspend_job_info_get(slurm_nrt_jobinfo_t *jp,
				     void **suspend_info)
{
	slurm_nrt_suspend_info_t *susp_info_ptr;
	if (!jp)
		return;
	if (*suspend_info == NULL) {
		susp_info_ptr = xmalloc(sizeof(slurm_nrt_suspend_info_t));
		susp_info_ptr->job_key_array_size = 8;
		susp_info_ptr->job_key = xmalloc(sizeof(nrt_job_key_t) * 8);
		*suspend_info = susp_info_ptr;
	} else {
		susp_info_ptr = *suspend_info;
		if ((susp_info_ptr->job_key_count + 1) >=
		    susp_info_ptr->job_key_array_size) {
			susp_info_ptr->job_key_array_size *= 2;
			xrealloc(susp_info_ptr->job_key,
				 sizeof(nrt_job_key_t) *
				 susp_info_ptr->job_key_array_size);
		}
	}
	susp_info_ptr->job_key[susp_info_ptr->job_key_count++] = jp->job_key;
}

extern void nrt_suspend_job_info_pack(void *suspend_info, Buf buffer,
				      uint16_t protocol_version)
{
	slurm_nrt_suspend_info_t *susp_info_ptr;

	if (!suspend_info) {
		uint32_t tmp_32 = 0;
		pack32(tmp_32, buffer);
		return;
	}
	susp_info_ptr = (slurm_nrt_suspend_info_t *) suspend_info;
	pack32(susp_info_ptr->job_key_count, buffer);
	pack32_array(susp_info_ptr->job_key, susp_info_ptr->job_key_count,
		     buffer);
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		int i;
		for (i = 0; i < susp_info_ptr->job_key_count; i++) {
			info("nrt_suspend_job_info_pack: job_key[%d]:%u",
			     i, susp_info_ptr->job_key[i]);
		}
	}
}

extern int nrt_suspend_job_info_unpack(void **suspend_info, Buf buffer,
				       uint16_t protocol_version)
{
	slurm_nrt_suspend_info_t *susp_info_ptr = NULL;
	uint32_t tmp_32;

	*suspend_info = NULL;
	safe_unpack32(&tmp_32, buffer);
	if (tmp_32 == 0)
		return SLURM_SUCCESS;

	susp_info_ptr = xmalloc(sizeof(slurm_nrt_suspend_info_t));
	susp_info_ptr->job_key_count = tmp_32;
	susp_info_ptr->job_key_array_size = tmp_32;
	safe_unpack32_array(&susp_info_ptr->job_key, &tmp_32, buffer);
	if (tmp_32 != susp_info_ptr->job_key_count)
		goto unpack_error;
	*suspend_info = susp_info_ptr;
	if (debug_flags & DEBUG_FLAG_SWITCH) {
		int i;
		for (i = 0; i < susp_info_ptr->job_key_count; i++) {
			info("nrt_suspend_job_info_pack: job_key[%d]:%u",
			     i, susp_info_ptr->job_key[i]);
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	error("nrt_suspend_job_info_unpack: unpack error");
	xfree(susp_info_ptr->job_key);
	xfree(susp_info_ptr);
	return SLURM_ERROR;
}

extern void nrt_suspend_job_info_free(void *suspend_info)
{
	slurm_nrt_suspend_info_t *susp_info_ptr;

	susp_info_ptr = (slurm_nrt_suspend_info_t *) suspend_info;
	if (susp_info_ptr) {
		xfree(susp_info_ptr->job_key);
		xfree(susp_info_ptr);
	}
}

static int _preempt_job(nrt_job_key_t job_key, int max_wait_secs)
{
	nrt_cmd_preempt_job_t preempt_job;
	int err;

	preempt_job.job_key	= job_key;
#ifdef PREEMPT_RELEASE_RESOURCES_MASK
	preempt_job.option	= PREEMPT_RELEASE_RESOURCES_MASK;
#else
	preempt_job.option	= 0x0001;
#endif
	preempt_job.timeout_val	= NULL;    /* Should be set? What value? */
	if (_wait_job(job_key, PES_JOB_RUNNING, max_wait_secs))
		return SLURM_ERROR;
	/* NOTE: This function is non-blocking.
	 * To detect completeion, poll on NRT_CMD_QUERY_PREEMPTION_STATE */
	err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_PREEMPT_JOB, &preempt_job);
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(preempt job, %u): %s", job_key,
		      nrt_err_str(err));
		return SLURM_ERROR;
	}
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_cmd_wrap(preempting job, %u)", job_key);
	if (_wait_job(job_key, PES_JOB_PREEMPTED, max_wait_secs))
		return SLURM_ERROR;
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_cmd_wrap(preempted job, %u)", job_key);
	return SLURM_SUCCESS;
}

extern int nrt_preempt_job(void *suspend_info, int max_wait_secs)
{
	slurm_nrt_suspend_info_t *susp_info_ptr;
	int err, i, rc = SLURM_SUCCESS;

	susp_info_ptr = (slurm_nrt_suspend_info_t *) suspend_info;
	if (susp_info_ptr) {
		for (i = 0; i < susp_info_ptr->job_key_count; i++) {
			err = _preempt_job(susp_info_ptr->job_key[i],
					   max_wait_secs);
			if (err != SLURM_SUCCESS)
				rc = err;
		}
	}
	return rc;
}

static int _resume_job(nrt_job_key_t job_key, int max_wait_secs)
{
	nrt_cmd_resume_job_t resume_job;
	int err;

	resume_job.job_key	= job_key;
#ifdef PREEMPT_RELEASE_RESOURCES_MASK
	resume_job.option	= PREEMPT_RELEASE_RESOURCES_MASK;
#else
	resume_job.option	= 0x0001;
#endif
	resume_job.timeout_val	= NULL;    /* Should be set? What value? */
	/* NOTE: This function is non-blocking.
	 * To detect completeion, poll on NRT_CMD_QUERY_PREEMPTION_STATE */
	err = nrt_cmd_wrap(NRT_VERSION, NRT_CMD_RESUME_JOB, &resume_job);
	if (err != NRT_SUCCESS) {
		error("nrt_cmd_wrap(resume job, %u): %s", job_key,
		      nrt_err_str(err));
		return SLURM_ERROR;
	}
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_cmd_wrap(resuming job, %u)", job_key);
	if (_wait_job(job_key, PES_JOB_RUNNING, max_wait_secs))
		return SLURM_ERROR;
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("nrt_cmd_wrap(resumed job, %u)", job_key);
	return SLURM_SUCCESS;
}

extern int nrt_resume_job(void *suspend_info, int max_wait_secs)
{
	slurm_nrt_suspend_info_t *susp_info_ptr;
	int err, i, rc = SLURM_SUCCESS;

	susp_info_ptr = (slurm_nrt_suspend_info_t *) suspend_info;
	if (susp_info_ptr) {
		for (i = 0; i < susp_info_ptr->job_key_count; i++) {
			err = _resume_job(susp_info_ptr->job_key[i],
					  max_wait_secs);
			if (err != SLURM_SUCCESS)
				rc = err;
		}
	}
	return rc;
}
