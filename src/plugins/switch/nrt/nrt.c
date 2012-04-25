/*****************************************************************************\
 *  nrt.c - Library routines for initiating jobs using IBM's NRT (Network
 *          Routing Table)
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2011-2012 SchedMD LLC.
 *  Original switch/federation plugin written by Jason King <jking@llnl.gov>
 *  Largely re-written for NRT support by Morris Jette <jette@schedmd.com>
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
 *****************************************************************************
 *  NOTE: The NRT API communicates with IBM's Protocol Network Services Deamon
 *  (PNSD). PNSD logs are written to /tmp/serverlog.
\*****************************************************************************/

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_LIBNRT
# include <nrt.h>
#else
# error "Must have libnrt to compile this module!"
#endif

#include <sys/stat.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/read_config.h"
#include "src/plugins/switch/nrt/nrt_keys.h"
#include "src/plugins/switch/nrt/slurm_nrt.h"

/*
 * Definitions local to this module
 */
#define NRT_NODEINFO_MAGIC	0xc00cc00a
#define NRT_JOBINFO_MAGIC	0xc00cc00b
#define NRT_LIBSTATE_MAGIC	0xc00cc00c
#define NRT_HOSTLEN		20
#define NRT_NODECOUNT		128
#define NRT_HASHCOUNT		128
#define NRT_MAX_ADAPTERS (NRT_MAX_ADAPTERS_PER_TYPE * NRT_MAX_ADAPTER_TYPES)

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

typedef struct slurm_nrt_window {
	nrt_window_id_t window_id;
	win_state_t state;
	nrt_job_key_t job_key;  /* FIXME: Perhaps change to uid or client_pid? */
} slurm_nrt_window_t;

typedef struct slurm_nrt_adapter {
	char adapter_name[NRT_MAX_ADAPTER_NAME_LEN];
	nrt_adapter_t adapter_type;
	in_addr_t ipv4_addr;
	nrt_logical_id_t lid;
	nrt_network_id_t network_id;
	nrt_port_id_t port_id;
	uint64_t special;
	nrt_window_id_t window_count;
	slurm_nrt_window_t *window_list;
} slurm_nrt_adapter_t;

struct slurm_nrt_nodeinfo {
	uint32_t magic;
	char name[NRT_HOSTLEN];
	uint32_t adapter_count;
	struct slurm_nrt_adapter *adapter_list;
	struct slurm_nrt_nodeinfo *next;
};

struct slurm_nrt_libstate {
	uint32_t magic;
	uint32_t node_count;
	uint32_t node_max;
	slurm_nrt_nodeinfo_t *node_list;
	uint32_t hash_max;
	slurm_nrt_nodeinfo_t **hash_table;
	uint16_t key_index;
};

struct slurm_nrt_jobinfo {
	uint32_t magic;
	/* version from nrt_version() */
	/* adapter from lid in table */
	nrt_network_id_t network_id;
	/* uid from getuid() */
	/* pid from getpid() */
	nrt_job_key_t job_key;
	uint8_t bulk_xfer;	/* flag */
	uint8_t ip_v6;		/* flag */
	uint8_t user_space;	/* flag */
	char *protocol;		/* MPI, UPC, LAPI, PAMI, etc. */
	uint16_t tables_per_task;
	nrt_tableinfo_t *tableinfo;

	hostlist_t nodenames;
	uint32_t num_tasks;
};

typedef struct {
	char adapter_name[NRT_MAX_ADAPTER_NAME_LEN];
	nrt_adapter_t adapter_type;
} nrt_cache_entry_t;

static int lid_cache_size = 0;
static nrt_cache_entry_t lid_cache[NRT_MAX_ADAPTERS];


/* Local functions */
static char *	_adapter_type_str(nrt_adapter_t type);
static int	_allocate_windows_all(int adapter_cnt,
			nrt_tableinfo_t *tableinfo, char *hostname,
			int node_id, nrt_task_id_t task_id,
			nrt_job_key_t job_key,
			nrt_adapter_t adapter_type, nrt_logical_id_t base_lid,
			bool user_space);
static int	_allocate_window_single(char *adapter_name,
			nrt_tableinfo_t *tableinfo, char *hostname,
			int node_id, nrt_task_id_t task_id,
			nrt_job_key_t job_key,
			nrt_adapter_t adapter_type, nrt_logical_id_t base_lid,
			bool user_space);
static slurm_nrt_libstate_t *_alloc_libstate(void);
static slurm_nrt_nodeinfo_t *_alloc_node(slurm_nrt_libstate_t *lp, char *name);
static int	_check_rdma_job_count(char *adapter_name,
				      nrt_adapter_t adapter_type);
static int	_copy_node(slurm_nrt_nodeinfo_t *dest,
			   slurm_nrt_nodeinfo_t *src);
static int	_fake_unpack_adapters(Buf buf);
static int	_fill_in_adapter_cache(void);
static slurm_nrt_nodeinfo_t *
		_find_node(slurm_nrt_libstate_t *lp, char *name);
static slurm_nrt_window_t *
		_find_window(slurm_nrt_adapter_t *adapter, uint16_t window_id);
static slurm_nrt_window_t *_find_free_window(slurm_nrt_adapter_t *adapter);
static slurm_nrt_nodeinfo_t *_find_node(slurm_nrt_libstate_t *lp, char *name);
static void	_free_libstate(slurm_nrt_libstate_t *lp);
static int	_get_adapters(slurm_nrt_nodeinfo_t *n);
static void	_hash_add_nodeinfo(slurm_nrt_libstate_t *state,
				   slurm_nrt_nodeinfo_t *node);
static int	_hash_index(char *name);
static void	_hash_rebuild(slurm_nrt_libstate_t *state);
static void	_init_adapter_cache(void);
static int	_job_step_window_state(slurm_nrt_jobinfo_t *jp,
				       hostlist_t hl, win_state_t state);
static void	_lock(void);
static nrt_job_key_t _next_key(void);
static int	_pack_libstate(slurm_nrt_libstate_t *lp, Buf buffer);
static void	_pack_tableinfo(nrt_tableinfo_t *tableinfo,
				nrt_adapter_t adapter_type, Buf buf);
static char *	_port_status_str(nrt_port_status_t status);
static void	_unlock(void);
static int	_unpack_libstate(slurm_nrt_libstate_t *lp, Buf buffer);
static int	_unpack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf,
				 bool believe_window_status);
static int	_unpack_tableinfo(nrt_tableinfo_t *tableinfo,
				  nrt_adapter_t adapter_type, Buf buf);
static int	_wait_for_all_windows(nrt_tableinfo_t *tableinfo);
static int	_wait_for_window_unloaded(char *adapter_name,
					  nrt_adapter_t adapter_type,
					  nrt_window_id_t window_id, int retry,
					  unsigned int max_windows);
static char *	_win_state_str(win_state_t state);
static int	_window_state_set(int adapter_cnt, nrt_tableinfo_t *tableinfo,
				  char *hostname, int task_id,
				  win_state_t state, uint16_t job_key);

/* The _lock() and _unlock() functions are used to lock/unlock a
 * global mutex.  Used to serialize access to the global library
 * state variable nrt_state.
 */
static void
_lock(void)
{
	int err = 1;

	while (err) {
		err = pthread_mutex_lock(&global_lock);
	}
}

static void
_unlock(void)
{
	int err = 1;

	while (err) {
		err = pthread_mutex_unlock(&global_lock);
	}
}

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

#if NRT_DEBUG
	info("_fill_in_adapter_cache: begin");
#endif
	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	for (i = 0; i < 2; i++) {
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				  &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		error("Is pnsd daemon started? Retrying...");
		/* Run "/opt/ibmhpc/pecurrent/ppe.pami/pnsd/pnsd -A" */
		sleep(5);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}

	for (i = 0; i < num_adapter_types; i++) {
#if NRT_DEBUG
		info("adapter_type[%d]: %u", i, adapter_type[i]);
#endif
		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				  &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_command(adapter_names, %u): %s",
			      adapter_names.adapter_type, nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
		for (j = 0; j < num_adapter_names; j++) {
#if NRT_DEBUG
			info("adapter_names[%d]: %s",
			     j, adapter_names.adapter_names[j]);
#endif
			lid_cache[lid_cache_size].adapter_type = adapter_names.
								 adapter_type;
			strncpy(lid_cache[lid_cache_size].adapter_name,
				adapter_names.adapter_names[j],
				NRT_MAX_ADAPTER_NAME_LEN);
			lid_cache_size++;
		}
	}
#if NRT_DEBUG
	info("_fill_in_adapter_cache: complete: %d", rc);
#endif
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

	assert(name);

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

	assert(name);
	assert(lp);

	if (lp->node_count == 0)
		return NULL;

	if (lp->hash_table) {
		i = _hash_index(name);
		n = lp->hash_table[i];
		while (n) {
			assert(n->magic == NRT_NODEINFO_MAGIC);
			if (!strncmp(n->name, name, NRT_HOSTLEN))
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

	assert(state);
	assert(state->hash_table);
	assert(state->hash_max >= state->node_count);
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

	assert(state);

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
 * For one node, free all of the windows belonging to a particular
 * job step (as identified by the job_key).
 */
static void
_free_windows_by_job_key(uint16_t job_key, char *node_name)
{
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter;
	slurm_nrt_window_t *window;
	int i, j;

	/* debug3("_free_windows_by_job_key(%hu, %s)", job_key, node_name); */
	if ((node = _find_node(nrt_state, node_name)) == NULL)
		return;

	if (node->adapter_list == NULL) {
		error("_free_windows_by_job_key, "
		      "adapter_list NULL for node %s", node_name);
		return;
	}
	for (i = 0; i < node->adapter_count; i++) {
		adapter = &node->adapter_list[i];
		if (adapter->window_list == NULL) {
			error("_free_windows_by_job_key, "
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

			if (window->job_key == job_key) {
				/* debug3("Freeing adapter %s window %d",
				   adapter->name, window->id); */
				window->state = NRT_WIN_UNAVAILABLE;
				window->job_key = 0;
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
	int proc_cnt;
	int nprocs;
	int nnodes;
	int i, j;
	int err, rc = SLURM_SUCCESS;
	int task_cnt;
	int full_node_cnt;
	int min_procs_per_node;
	int max_procs_per_node;

	xassert(!hostlist_is_empty(hl));
	xassert(jp);
	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if ((jp == NULL) || (hostlist_is_empty(hl)))
		return SLURM_ERROR;

	if ((jp->tables_per_task == 0) || (jp->tableinfo == NULL) ||
	    (jp->tableinfo[0].table_length == 0))
		return SLURM_SUCCESS;

	debug3("jp->tables_per_task = %d", jp->tables_per_task);
	nprocs = jp->tableinfo[0].table_length;
	hi = hostlist_iterator_create(hl);

	debug("Finding windows");
	nnodes = hostlist_count(hl);
	full_node_cnt = nprocs % nnodes;
	min_procs_per_node = nprocs / nnodes;
	max_procs_per_node = (nprocs + nnodes - 1) / nnodes;

	proc_cnt = 0;
	_lock();
	for  (i = 0; i < nnodes; i++) {
		host = hostlist_next(hi);
		if (!host)
			error("Failed to get next host");

		if (i < full_node_cnt)
			task_cnt = max_procs_per_node;
		else
			task_cnt = min_procs_per_node;

		for (j = 0; j < task_cnt; j++) {
			err = _window_state_set(jp->tables_per_task,
						jp->tableinfo,
						host, proc_cnt,
						state, jp->job_key);
			rc = MAX(rc, err);
			proc_cnt++;
		}
		free(host);
	}
	_unlock();

	hostlist_iterator_destroy(hi);
	return rc;
}


/* Find the correct NRT structs and set the state
 * of the switch windows for the specified task_id.
 *
 * Used by: slurmctld
 */
static int
_window_state_set(int adapter_cnt, nrt_tableinfo_t *tableinfo,
		  char *hostname, int task_id, win_state_t state,
		  uint16_t job_key)
{
	slurm_nrt_nodeinfo_t *node = NULL;
	slurm_nrt_adapter_t *adapter = NULL;
	slurm_nrt_window_t *window = NULL;
	int i, j;
	bool adapter_found;
	uint16_t win_id = 0;

	assert(tableinfo);
	assert(hostname);
	assert(adapter_cnt <= NRT_MAXADAPTERS);

	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}
	if (node->adapter_list == NULL) {
		error("Found node, but adapter_list is NULL");
		return SLURM_ERROR;
	}

	for (i = 0; i < adapter_cnt; i++) {
		if (tableinfo[i].table == NULL) {
			error("tableinfo[%d].table is NULL", i);
			return SLURM_ERROR;
		}

		adapter_found = false;
		/* Find the adapter that matches the one in tableinfo */
		for (j = 0; j < node->adapter_count; j++) {
			adapter = &node->adapter_list[j];
			if (strcasecmp(adapter->adapter_name,
				       tableinfo[i].adapter_name))
				continue;
			if (adapter->adapter_type == NRT_IB) {
				nrt_ib_task_info_t *ib_tbl_ptr;
				ib_tbl_ptr = tableinfo[i].table + task_id;
				if (ib_tbl_ptr == NULL) {
					error("tableinfo[%d].table[%d] is "
					      "NULL", i, task_id);
					return SLURM_ERROR;
				}
				if (adapter->lid == ib_tbl_ptr->base_lid) {
					adapter_found = true;
					win_id = ib_tbl_ptr->win_id;
					debug3("Setting status %s adapter %s "
					       "lid %hu window %hu for task %d",
					       state == NRT_WIN_UNAVAILABLE ?
					       "UNLOADED" : "LOADED",
					       adapter->adapter_name,
					       ib_tbl_ptr->base_lid,
					       ib_tbl_ptr->win_id, task_id);
					break;
				}
			} else if (adapter->adapter_type == NRT_HFI) {
				nrt_hfi_task_info_t *hfi_tbl_ptr;
				hfi_tbl_ptr = tableinfo[i].table + task_id;
				if (hfi_tbl_ptr == NULL) {
					error("tableinfo[%d].table[%d] is "
					      "NULL", i, task_id);
					return SLURM_ERROR;
				}
				if (adapter->lid == hfi_tbl_ptr->lid) {
					adapter_found = true;
					win_id = hfi_tbl_ptr->win_id;
					debug3("Setting status %s adapter %s "
					       "lid %hu window %hu for task %d",
					       state == NRT_WIN_UNAVAILABLE ?
					       "UNLOADED" : "LOADED",
					       adapter->adapter_name,
					       hfi_tbl_ptr->lid,
					       hfi_tbl_ptr->win_id, task_id);
					break;
				}
			} else {
				fatal("_window_state_set: Missing support for "
				      "adapter type %hu",
				      adapter->adapter_type);

			}
		}
		if (!adapter_found) {
			error("Did not find adapter %s with lid %hu ",
			      adapter->adapter_name, adapter->lid);
			return SLURM_ERROR;
		}

		window = _find_window(adapter, win_id);
		if (window) {
			window->state = state;
			window->job_key =
				(state == NRT_WIN_UNAVAILABLE) ? 0 : job_key;
		}
	}

	return SLURM_SUCCESS;
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

	assert(lp);

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
	n->adapter_list = (struct slurm_nrt_adapter *)
			  xmalloc(NRT_MAXADAPTERS *
			  sizeof(struct slurm_nrt_adapter));

	if (name != NULL) {
		strncpy(n->name, name, NRT_HOSTLEN);
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
	int i;
	slurm_nrt_window_t *window;

	for (i = 0; i < adapter->window_count; i++) {
		window = &adapter->window_list[i];
		if (window->state == NRT_WIN_AVAILABLE)
			return window;
	}

	return (slurm_nrt_window_t *) NULL;
}

/* For a given process, fill out an nrt_creator_per_task_input_t
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign adapters, lids and switch windows to
 * each task in a job.
 *
 * Used by: slurmctld
 */
static int
_allocate_windows_all(int adapter_cnt, nrt_tableinfo_t *tableinfo,
		      char *hostname, int node_id, nrt_task_id_t task_id,
		      nrt_job_key_t job_key, nrt_adapter_t adapter_type,
		      nrt_logical_id_t base_lid, bool user_space)
{
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter;
	slurm_nrt_window_t *window;
	int i;

	assert(tableinfo);
	assert(hostname);

	debug("in _allocate_windows_all");
	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}

	/* Reserve a window on each adapter for this task */
	for (i = 0; i < adapter_cnt; i++) {
		adapter = &node->adapter_list[i];
		if (adapter->adapter_type != adapter_type)
			continue;
		if (user_space) {
			window = _find_free_window(adapter);
			if (window == NULL) {
				error("No free windows on node %s adapter %s",
				      node->name, adapter->adapter_name);
				return SLURM_ERROR;
			}
			window->state = NRT_WIN_UNAVAILABLE;
			window->job_key = job_key;
		}

		if (!user_space || (adapter_type == NRT_IPONLY)) {
			nrt_ip_task_info_t *ip_table;
			ip_table = (nrt_ip_task_info_t *) tableinfo[i].table;
			ip_table += task_id;
			ip_table->node_number  = node_id;
			ip_table->task_id      = task_id;
			memcpy(&ip_table->ip.ipv4_addr, &adapter->ipv4_addr,
			       sizeof(in_addr_t));
		} else if (adapter_type == NRT_IB) {
			nrt_ib_task_info_t *ib_table;
			ib_table = (nrt_ib_task_info_t *) tableinfo[i].table;
			ib_table += task_id;
			strncpy(ib_table->device_name, adapter->adapter_name,
				NRT_MAX_DEVICENAME_SIZE);
			ib_table += task_id;
			ib_table->base_lid = base_lid;
			ib_table->port_id  = 1;
			ib_table->lmc      = 0;
			ib_table->task_id  = task_id;
			ib_table->win_id   = window->window_id;
		} else if (adapter_type == NRT_HFI) {
			nrt_hfi_task_info_t *hfi_table;
			hfi_table = (nrt_hfi_task_info_t *) tableinfo[i].table;
			hfi_table += task_id;
			hfi_table += task_id;
			hfi_table->task_id = task_id;
			hfi_table->win_id = window->window_id;
		} else {
			fatal("Missing support for adapter type %d",
			      adapter_type);
		}

		strncpy(tableinfo[i].adapter_name, adapter->adapter_name,
			NRT_MAX_ADAPTER_NAME_LEN);
		tableinfo[i].adapter_type = adapter_type;
	}

	return SLURM_SUCCESS;
}


/* For a given process, fill out an nrt_creator_per_task_input_t
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign a single adapter, lid and switch window to
 * a task in a job.
 *
 * Used by: slurmctld
 */
static int
_allocate_window_single(char *adapter_name, nrt_tableinfo_t *tableinfo,
			char *hostname, int node_id, nrt_task_id_t task_id,
			nrt_job_key_t job_key, nrt_adapter_t adapter_type,
			nrt_logical_id_t base_lid, bool user_space)
{
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter = NULL;
	slurm_nrt_window_t *window;
	int i;

	assert(tableinfo);
	assert(hostname);

	debug("in _allocate_window_single");
	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}

	/* find the adapter */
	for (i = 0; i < node->adapter_count; i++) {
		debug("adapter %s at index %d",
		      node->adapter_list[i].adapter_name, i);
		if (strcasecmp(node->adapter_list[i].adapter_name,
			       adapter_name) == 0) {
			adapter = &node->adapter_list[i];
			debug("Found adapter %s", adapter_name);
			break;
		}
	}
	if (adapter == NULL) {
		error("Failed to find adapter %s on node %s",
		      adapter_name, hostname);
		return SLURM_ERROR;
	}

	if (user_space) {
		/* Reserve a window on the adapter for this task */
		window = _find_free_window(adapter);
		if (window == NULL) {
			error("No free windows on node %s adapter %s",
			      node->name, adapter->adapter_name);
			return SLURM_ERROR;
		}
		window->state = NRT_WIN_UNAVAILABLE;
		window->job_key = job_key;
	}

	if (!user_space || (adapter_type == NRT_IPONLY)) {
		nrt_ip_task_info_t *ip_table;
		ip_table = (nrt_ip_task_info_t *) tableinfo[0].table;
		ip_table += task_id;
		ip_table->node_number  = node_id;
		ip_table->task_id      = task_id;
		memcpy(&ip_table->ip.ipv4_addr, &adapter->ipv4_addr,
		       sizeof(in_addr_t));
	} else if (adapter_type == NRT_IB) {
		nrt_ib_task_info_t *ib_table;
		ib_table = (nrt_ib_task_info_t *) tableinfo[0].table;
		ib_table += task_id;
		strncpy(ib_table->device_name, adapter_name,
			NRT_MAX_DEVICENAME_SIZE);
		ib_table += task_id;
		ib_table->base_lid = base_lid;
		ib_table->port_id  = 1;
		ib_table->lmc      = 0;
		ib_table->task_id  = task_id;
		ib_table->win_id   = window->window_id;
	} else if (adapter_type == NRT_HFI) {
		nrt_hfi_task_info_t *hfi_table;
		hfi_table = (nrt_hfi_task_info_t *) tableinfo[0].table;
		hfi_table += task_id;
		hfi_table += task_id;
		hfi_table->task_id = task_id;
		hfi_table->win_id = window->window_id;
	} else {
		fatal("Missing support for adapter type %d", adapter_type);
	}

	strncpy(tableinfo[0].adapter_name, adapter_name,
		NRT_MAX_ADAPTER_NAME_LEN);

	return SLURM_SUCCESS;
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
	case NRT_HPCE:
		return "HPC_Ethernet";
	case NRT_KMUX:
		return "Kernel_Emulated_HPCE";
	default:
		snprintf(buf, sizeof(buf), "%d", type);
		return buf;
	}

	return NULL;	/* Never used */
}

#if NRT_DEBUG
/* Used by: slurmd */
static void
_print_adapter_status(nrt_cmd_status_adapter_t *status_adapter)
{
	int i;
	nrt_window_id_t window_cnt;
	nrt_status_t *status = *(status_adapter->status_array);

	info("--Begin Adapter Status--");
	info("  adapter_name: %s", status_adapter->adapter_name);
	info("  adapter_type: %s",
	     _adapter_type_str(status_adapter->adapter_type));
	window_cnt = *(status_adapter->window_count);
	info("  window_count: %hu", window_cnt);
	info("  --------");
	for (i = 0; i < MIN(window_cnt, NRT_DEBUG_CNT); i++) {
		info("  bulk_xfer: %hu", status[i].bulk_transfer);
		info("  client_pid: %u", (uint32_t)status[i].client_pid);
		info("  rcontext_blocks: %u", status[i].rcontext_blocks);
		info("  state: %s", _win_state_str(status[i].state));
		info("  uid: %u", (uint32_t) status[i].uid);
		info("  window_id: %hu", status[i].window_id);
		info("  --------");
	}
	if (i < window_cnt) {
		info("  suppress data for windows %hu through %hu",
		     status[i].window_id, status[--window_cnt].window_id);
		info("  --------");
	}
	info("--End Adapter Status--");
}

/* Used by: slurmd, slurmctld */
static void
_print_nodeinfo(slurm_nrt_nodeinfo_t *n)
{
	int i, j;
	struct slurm_nrt_adapter *a;
	slurm_nrt_window_t *w;
	unsigned char *p;

	assert(n);
	assert(n->magic == NRT_NODEINFO_MAGIC);

	info("--Begin Node Info--");
	info("  node: %s", n->name);
	info("  adapter_count: %u", n->adapter_count);
	for (i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		info("  adapter_name: %s", a->adapter_name);
		info("    adapter_type: %s",
		     _adapter_type_str(a->adapter_type));
		p = (unsigned char *) &a->ipv4_addr;
		info("    ipv4_addr: %d.%d.%d.%d", p[0], p[1], p[2], p[3]);
		info("    ipv6_addr: TBD");
		info("    lid: %u", a->lid);
		info("    network_id: %lu", a->network_id);
		info("    port_id: %hu", a->port_id);
		info("    special: %lu", a->special);
		info("    window_count: %hu", a->window_count);
		w = a->window_list;
		for (j = 0; j < MIN(a->window_count, NRT_DEBUG_CNT); j++) {
#if (NRT_DEBUG < 2)
			if (w[j].state != NRT_WIN_AVAILABLE)
				continue;
#endif
			info("      window %hu: %s", w[j].window_id,
			     _win_state_str(w->state));
			info("      job_key %hu", w[j].job_key);
		}
	}
	info("--End Node Info--");
}

/* Used by: slurmctld */
static void
_print_libstate(const slurm_nrt_libstate_t *l)
{
	int i;

	assert(l);
	assert(l->magic == NRT_LIBSTATE_MAGIC);

	info("--Begin libstate--");
	info("  node_count = %u", l->node_count);
	info("  node_max = %u", l->node_max);
	info("  hash_max = %u", l->hash_max);
	info("  key_index = %hu", l->key_index);
	for (i = 0; i < l->node_count; i++) {
		_print_nodeinfo(&l->node_list[i]);
	}
	info("--End libstate--");
}
/* Used by: all */
static void
_print_table(void *table, int size, nrt_adapter_t adapter_type)
{
	int i;

	assert(table);
	assert(size > 0);

	info("--Begin NRT table--");
	for (i = 0; i < size; i++) {
		if (adapter_type == NRT_IB) {
			nrt_ib_task_info_t *ib_tbl_ptr;
			ib_tbl_ptr = table;
			ib_tbl_ptr += i;
			info("  task_id: %u", ib_tbl_ptr->task_id);
			info("  win_id: %hu", ib_tbl_ptr->win_id);
			info("  node_number: %u", ib_tbl_ptr->node_number);
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
		} else if ((adapter_type == NRT_IPONLY) ||
			   (adapter_type == NRT_HPCE)) {   /* HPC Ethernet */
			nrt_ip_task_info_t *ip_tbl_ptr;
			unsigned char *p;
			ip_tbl_ptr = table;
			ip_tbl_ptr += i;
			info("  task_id: %u", ip_tbl_ptr->task_id);
			info("  node_number: %u", ip_tbl_ptr->node_number);
			p = (unsigned char *) &ip_tbl_ptr->ip.ipv4_addr;
			info("  ipv4_addr: %d.%d.%d.%d",
			     p[0], p[1], p[2], p[3]);
			info("  ipv6_addr: TBD");
		} else {
			fatal("Unsupported adapter_type: %s",
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

	assert(j);
	assert(j->magic == NRT_JOBINFO_MAGIC);

	info("--Begin Jobinfo--");
	info("  job_key: %u", j->job_key);
	info("  network_id: %lu", j->network_id);
	info("  table_size: %u", j->tables_per_task);
	info("  bulk_xfer: %hu", j->bulk_xfer);
	info("  ip_v6: %hu", j->ip_v6);
	info("  user_space: %hu", j->user_space);
	info("  tables_per_task: %hu", j->tables_per_task);
	info("  protocol: %s", j->protocol);
	if (j->nodenames)
		hostlist_ranged_string(j->nodenames, sizeof(buf), buf);
	else
		strcpy(buf, "(NULL)");
	info("  nodenames: %s (slurmctld internal use only)", buf);
	info("  num_tasks: %d", j->num_tasks);
	for (i = 0; i < j->tables_per_task; i++) {
		if (j->user_space)
			adapter_type = j->tableinfo[i].adapter_type;
		else
			adapter_type = NRT_IPONLY;
		_print_table(j->tableinfo[i].table,
			     j->tableinfo[i].table_length, adapter_type);
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
		     adapter_type);
	info("--- End load table ---");
}
#endif

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
	tmp->key_index = (uint16_t) time(NULL);

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
	_lock();
	assert(!nrt_state);
	nrt_state = tmp;
	_unlock();

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

	assert(j != NULL);
	new = (slurm_nrt_jobinfo_t *) xmalloc(sizeof(slurm_nrt_jobinfo_t));
	new->magic = NRT_JOBINFO_MAGIC;
	new->job_key = -1;
	new->tables_per_task = 0;
	new->tableinfo = NULL;
	*j = new;

	return 0;
}

/* Used by: slurmd, slurmctld */
extern int
nrt_alloc_nodeinfo(slurm_nrt_nodeinfo_t **n)
{
	slurm_nrt_nodeinfo_t *new;

 	assert(n);

	new = (slurm_nrt_nodeinfo_t *) xmalloc(sizeof(slurm_nrt_nodeinfo_t));
	new->adapter_list = (struct slurm_nrt_adapter *)
			    xmalloc(sizeof(struct slurm_nrt_adapter) *
			    NRT_MAX_ADAPTER_TYPES * NRT_MAX_ADAPTERS_PER_TYPE);
	new->magic = NRT_NODEINFO_MAGIC;
	new->adapter_count = 0;
	new->next = NULL;

	*n = new;

	return 0;
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
	nrt_status_t **status_array = NULL;
	nrt_window_id_t window_count;

#if NRT_DEBUG
	info("_get_adapters: begin");
#endif
	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	adapter_info.window_list = NULL;
	for (i = 0; i < 2; i++) {
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				  &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		error("Is PNSD daemon started? Retrying...");
		/* Run "/opt/ibmhpc/pecurrent/ppe.pami/pnsd/pnsd -A" */
		sleep(5);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}
#if NRT_DEBUG
	for (i = 0; i < num_adapter_types; i++) {
		info("nrt_command(adapter_types): %s",
		    _adapter_type_str(adapter_types.adapter_types[i]));
	}
#endif

	for (i = 0; i < num_adapter_types; i++) {
		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				  &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_command(adapter_names, %s): %s",
			      _adapter_type_str(adapter_names.adapter_type),
			      nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
#if NRT_DEBUG
		for (j = 0; j < num_adapter_names; j++) {
			info("nrt_command(adapter_names, %s, %s) "
			     "max_windows: %hu",
			      adapter_names.adapter_names[j],
			      _adapter_type_str(adapter_names.adapter_type),
			      max_windows);
		}
#endif
		status_array = xmalloc(sizeof(nrt_status_t *) * max_windows);
		for (j = 0; j < max_windows; j++) {
			/*
			 * WARNING: DO NOT USE xmalloc here!
			 *	
			 * The nrt_command(NRT_CMD_STATUS_ADAPTER) function
			 * changes pointer values and returns memory that is
			 * allocated with malloc() and deallocated with free()
			 */
			status_array[j] = malloc(sizeof(nrt_status_t) *
						 max_windows);
		}
		for (j = 0; j < num_adapter_names; j++) {
			slurm_nrt_adapter_t *adapter_ptr;
			adapter_status.adapter_name = adapter_names.
						      adapter_names[j];
			adapter_status.adapter_type = adapter_names.
						      adapter_type;
			adapter_status.status_array = status_array;
			adapter_status.window_count = &window_count;
			err = nrt_command(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
					  &adapter_status);
			if (err != NRT_SUCCESS) {
				error("nrt_command(status_adapter, %s, %s): %s",
				      adapter_status.adapter_name,
				      _adapter_type_str(adapter_status.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
#if NRT_DEBUG
			info("nrt_command(status_adapter, %s, %s)",
			     adapter_status.adapter_name,
			     _adapter_type_str(adapter_status.adapter_type));
			_print_adapter_status(&adapter_status);
#endif
			adapter_ptr = &n->adapter_list[n->adapter_count];
			strncpy(adapter_ptr->adapter_name,
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
				window_ptr->window_id = (*status_array)[k].
							window_id;
				window_ptr->state = (*status_array)[k].state;
				window_ptr->job_key = (*status_array)[k].
						      client_pid;
			}

			/* Now get adapter info (port_id, network_id, etc.) */
			query_adapter_info.adapter_name = adapter_names.
							  adapter_names[j];
			query_adapter_info.adapter_type = adapter_names.
							  adapter_type;
			query_adapter_info.adapter_info = &adapter_info;
			adapter_info.window_list = xmalloc(max_windows *
						   sizeof(nrt_window_id_t));
			err = nrt_command(NRT_VERSION,
					  NRT_CMD_QUERY_ADAPTER_INFO,
					  &query_adapter_info);
			if (err != NRT_SUCCESS) {
				error("nrt_command(adapter_into, %s, %s): %s",
				      query_adapter_info.adapter_name,
				      _adapter_type_str(query_adapter_info.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
#if NRT_DEBUG
			info("nrt_command(adapter_info, %s, %s), ports:%hu",
			     query_adapter_info.adapter_name,
			     _adapter_type_str(query_adapter_info.adapter_type),
			     adapter_info.num_ports);
			for (k = 0; k < adapter_info.num_ports; k++) {
				unsigned char *p;
				p = (unsigned char *) &adapter_info.port[k].
						      ipv4_addr;
				info("port_id:%hu status:%s lid:%u "
				     "network_id:%lu special:%lu "
				     "ipv4_addr:%d.%d.%d.%d",
				     adapter_info.port[k].port_id,
				     _port_status_str(adapter_info.port[k].
						      status),
				     adapter_info.port[k].lid,
				     adapter_info.port[k].network_id,
				     adapter_info.port[k].special,
				     p[0], p[1], p[2], p[3]);
			}
#endif
			for (k = 0; k < adapter_info.num_ports; k++) {
				if (adapter_info.port[k].status != 1)
					continue;
				adapter_ptr->ipv4_addr = adapter_info.port[k].
							 ipv4_addr;
				adapter_ptr->lid = adapter_info.port[k].lid;
				adapter_ptr->network_id = adapter_info.port[k].
							  network_id;
				adapter_ptr->port_id = adapter_info.port[k].
						       port_id;
				adapter_ptr->special = adapter_info.port[k].
						       special;
				break;
			}
			if ((adapter_ptr->ipv4_addr == 0) &&
			    (adapter_info.num_ports > 0)) {
				adapter_ptr->ipv4_addr = adapter_info.port[0].
							 ipv4_addr;
			}
		}
		if (status_array) {
			for (j = 0; j < max_windows; j++) {
				free(status_array[j]);
			}
			xfree(status_array);
		}
		xfree(adapter_info.window_list);
	}
#if NRT_DEBUG
	_print_nodeinfo(n);
	info("_get_adapters: complete: %d", rc);
#endif
	return rc;
}

/* Assumes a pre-allocated nodeinfo structure and uses _get_adapters
 * to do the dirty work.  We probably collect more information about
 * the adapters on a give node than we need to but it was done
 * in the interest of being prepared for future requirements.
 *
 * Used by: slurmd
 */
extern int
nrt_build_nodeinfo(slurm_nrt_nodeinfo_t *n, char *name)
{
	int err;

	assert(n);
	assert(n->magic == NRT_NODEINFO_MAGIC);
	assert(name);

	strncpy(n->name, name, NRT_HOSTLEN);
	_lock();
	err = _get_adapters(n);
	_unlock();

	return err;
}

/* Used by: all */
extern int
nrt_pack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf)
{
	struct slurm_nrt_adapter *a;
	uint16_t dummy16;
	int i, j, offset;

	assert(n);
	assert(n->magic == NRT_NODEINFO_MAGIC);
	assert(buf);
#if NRT_DEBUG
	info("nrt_pack_nodeinfo():");
	_print_nodeinfo(n);
#endif
	offset = get_buf_offset(buf);
	pack32(n->magic, buf);
	packmem(n->name, NRT_HOSTLEN, buf);
	pack32(n->adapter_count, buf);
	for (i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		packmem(a->adapter_name, NRT_MAX_ADAPTER_NAME_LEN, buf);
		dummy16 = a->adapter_type;
		pack16(dummy16, buf);	/* adapter_type is an int */
		pack32(a->ipv4_addr, buf);
		pack32(a->lid, buf);
		pack64(a->network_id, buf);
		pack8(a->port_id, buf);
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
	struct slurm_nrt_adapter *sa = NULL;
	struct slurm_nrt_adapter *da = NULL;

	assert(dest);
	assert(src);
	assert(dest->magic == NRT_NODEINFO_MAGIC);
	assert(src->magic == NRT_NODEINFO_MAGIC);
#if NRT_DEBUG
	info("_copy_node():");
	_print_nodeinfo(src);
#endif
	strncpy(dest->name, src->name, NRT_HOSTLEN);
	dest->adapter_count = src->adapter_count;
	for (i = 0; i < dest->adapter_count; i++) {
		sa = src->adapter_list + i;
		da = dest->adapter_list +i;
		strncpy(da->adapter_name, sa->adapter_name,
			NRT_MAX_ADAPTER_NAME_LEN);
		da->adapter_type = sa->adapter_type;
		da->ipv4_addr    = sa->ipv4_addr;
		da->lid          = sa->lid;
		da->network_id   = sa->network_id;
		da->port_id      = sa->port_id;
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

/* Throw away adapter portion of the nodeinfo.
 *
 * Used by: _unpack_nodeinfo
 */
static int
_fake_unpack_adapters(Buf buf)
{
	uint32_t adapter_count;
	uint16_t window_count;
	uint8_t  dummy8;
	uint16_t dummy16;
	uint32_t dummy32;
	uint64_t dummy64;
	char *dummyptr;
	int i, j;

	safe_unpack32(&adapter_count, buf);
	for (i = 0; i < adapter_count; i++) {
		/* no copy, just advances buf counters */
		safe_unpackmem_ptr(&dummyptr, &dummy32, buf);
		if (dummy32 != NRT_MAX_ADAPTER_NAME_LEN)
			goto unpack_error;
		safe_unpack16(&dummy16, buf);
		safe_unpack32(&dummy32, buf);
		safe_unpack32(&dummy32, buf);
		safe_unpack64(&dummy64, buf);
		safe_unpack8 (&dummy8, buf);
		safe_unpack64(&dummy64, buf);
		safe_unpack16(&window_count, buf);
		for (j = 0; j < window_count; j++) {
			safe_unpack16(&dummy16, buf);
			safe_unpack32(&dummy32, buf);
			safe_unpack32(&dummy32, buf);
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
_unpack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf, bool believe_window_status)
{
	int i, j, rc = SLURM_SUCCESS;
	struct slurm_nrt_adapter *tmp_a = NULL;
	slurm_nrt_window_t *tmp_w = NULL;
	uint32_t size;
	slurm_nrt_nodeinfo_t *tmp_n = NULL;
	char *name_ptr, name[NRT_HOSTLEN];
	uint32_t magic;
	uint16_t dummy16;

	/* NOTE!  We don't care at this point whether n is valid.
	 * If it's NULL, we will just forego the copy at the end.
	 */
	assert(buf);

	/* Extract node name from buffer
	 */
	safe_unpack32(&magic, buf);
	if (magic != NRT_NODEINFO_MAGIC)
		slurm_seterrno_ret(EBADMAGIC_NRT_NODEINFO);
	safe_unpackmem_ptr(&name_ptr, &size, buf);
	if (size != NRT_HOSTLEN)
		goto unpack_error;
	memcpy(name, name_ptr, size);

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
		if (_fake_unpack_adapters(buf) != SLURM_SUCCESS) {
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
			if (_fake_unpack_adapters(buf) != SLURM_SUCCESS) {
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
	safe_unpack32(&tmp_n->adapter_count, buf);
	for (i = 0; i < tmp_n->adapter_count; i++) {
		tmp_a = tmp_n->adapter_list + i;
		safe_unpackmem_ptr(&name_ptr, &size, buf);
		if (size != NRT_MAX_ADAPTER_NAME_LEN)
			goto unpack_error;
		memcpy(tmp_a->adapter_name, name_ptr, size);
		safe_unpack16(&dummy16, buf);
		tmp_a->adapter_type = dummy16;	/* adapter_type is an int */
		safe_unpack32(&tmp_a->ipv4_addr, buf);
		safe_unpack32(&tmp_a->lid, buf);
		safe_unpack64(&tmp_a->network_id, buf);
		safe_unpack8(&tmp_a->port_id, buf);
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
		tmp_w = NULL;	/* don't free if unpack error on next adapter */
	}
#if NRT_DEBUG
	info("_unpack_nodeinfo");
	_print_nodeinfo(tmp_n);
#endif

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
nrt_unpack_nodeinfo(slurm_nrt_nodeinfo_t *n, Buf buf)
{
	int rc;

	_lock();
	rc = _unpack_nodeinfo(n, buf, false);
	_unlock();
	return rc;
}

/* Used by: slurmd, slurmctld */
extern void
nrt_free_nodeinfo(slurm_nrt_nodeinfo_t *n, bool ptr_into_array)
{
	struct slurm_nrt_adapter *adapter;
	int i;

	if (!n)
		return;

	assert(n->magic == NRT_NODEINFO_MAGIC);

#if NRT_DEBUGX
	info("nrt_free_nodeinfo");
	_print_nodeinfo(n);
#endif
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
	xassert(jp);
	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if ((jp == NULL) || (hostlist_is_empty(hl)))
		return SLURM_ERROR;

	if ((jp->tables_per_task == 0) || (jp->tableinfo == NULL) ||
	    (jp->tableinfo[0].table_length == 0))
		return SLURM_SUCCESS;

	/* The hl hostlist may contain duplicate node_names (poe -hostfile
	 * triggers duplicates in the hostlist).  Since there
	 * is no reason to call _free_windows_by_job_key more than once
	 * per node_name, we create a new unique hostlist.
	 */
	uniq_hl = hostlist_copy(hl);
	hostlist_uniq(uniq_hl);
	hi = hostlist_iterator_create(uniq_hl);

	_lock();
	if (nrt_state != NULL) {
		while ((node_name = hostlist_next(hi)) != NULL) {
			_free_windows_by_job_key(jp->job_key, node_name);
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
	_unlock();

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
 * status after a call to switch_clear().
 */
extern int
nrt_job_step_allocated(slurm_nrt_jobinfo_t *jp, hostlist_t hl)
{
	return _job_step_window_state(jp, hl, NRT_WIN_UNAVAILABLE);
}

/* Assign a unique key to each job.  The key is used later to
 * gain access to the network table loaded on each node of a job.
 *
 * Used by: slurmctld
 */
static nrt_job_key_t
_next_key(void)
{
	uint16_t key;

	assert(nrt_state);

	_lock();
	key = nrt_state->key_index;
	if (key == 0)
		key++;
	nrt_state->key_index = key + 1;
	_unlock();

	return key;
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
		  char *adapter_name, bool bulk_xfer, bool ip_v6,
		  bool user_space, char *protocol)
{
	int nnodes, nprocs = 0;
	hostlist_iterator_t hi;
	char *host = NULL;
	int i, j;
	slurm_nrt_nodeinfo_t *node;
	int rc;
	nrt_adapter_t adapter_type = NRT_MAX_ADAPTER_TYPES;
	nrt_logical_id_t base_lid = 0xffffff;
	int adapter_type_count = 0;
	int table_rec_len = 0;

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);
	assert(tasks_per_node);

	nnodes = hostlist_count(hl);
	for (i = 0; i < nnodes; i++)
		nprocs += tasks_per_node[i];

	if ((nnodes <= 0) || (nprocs <= 0))
		slurm_seterrno_ret(EINVAL);

	jp->bulk_xfer  = (uint8_t) bulk_xfer;
	jp->ip_v6      = (uint8_t) ip_v6;
	jp->job_key    = _next_key();
	jp->nodenames  = hostlist_copy(hl);
	jp->num_tasks  = nprocs;
	jp->user_space = (uint8_t) user_space;
	jp->protocol   = xstrdup(protocol);

	hi = hostlist_iterator_create(hl);

	/*
	 * Peek at the first host to figure out tables_per_task and adapter
	 * type. This driver assumes that all nodes have the same number of
	 * adapters per node.  Bad things will happen if this assumption is
	 * incorrect.
	 */
	host = hostlist_next(hi);
	_lock();
	node = _find_node(nrt_state, host);
	if (node && node->adapter_list) {
		for (i = 0; i < node->adapter_count; i++) {
			nrt_adapter_t ad_type;
			if (adapter_name &&
			    strcmp(adapter_name,
				   node->adapter_list[i].adapter_name)) {
				continue;
			}
			ad_type = node->adapter_list[i].adapter_type;
			if ((ad_type == NRT_IPONLY) ||
			    (ad_type == NRT_HPCE)) {
				if (jp->user_space)
					continue;
			}
			if (adapter_type == NRT_MAX_ADAPTER_TYPES)
				adapter_type = ad_type;
			else if (adapter_type != ad_type)
				continue;
			adapter_type_count++;
/* FIXME: It's unclear how this works, each node would have different logical_id
 * although the network_id seems to be common for our IB switches */
			base_lid = MIN(base_lid,
				       node->adapter_list[i].lid);
			jp->network_id = node->adapter_list[i].network_id;
		}
	}
	if (sn_all) {
		jp->tables_per_task = adapter_type_count;
	} else if (adapter_type_count >= 1) {
		jp->tables_per_task = 1;
	} else {
		jp->tables_per_task = 0;
		info("switch/nrt: no adapter found for job");
	}
	_unlock();
	if (host != NULL)
		free(host);
	hostlist_iterator_reset(hi);
	if (jp->tables_per_task == 0)
		return SLURM_FAILURE;

	/* Allocate memory for each nrt_tableinfo_t */
	jp->tableinfo = (nrt_tableinfo_t *) xmalloc(jp->tables_per_task *
						    sizeof(nrt_tableinfo_t));
	if (!jp->user_space)
		table_rec_len = sizeof(nrt_ip_task_info_t);
	else if (adapter_type == NRT_IB)
		table_rec_len = sizeof(nrt_ib_task_info_t);
	else if (adapter_type == NRT_HFI)
		table_rec_len = sizeof(nrt_hfi_task_info_t);
	else {
		fatal("Unsupported adapter_type: %s",
		      _adapter_type_str(adapter_type));
	}
	for (i = 0; i < jp->tables_per_task; i++) {
		jp->tableinfo[i].table_length = nprocs;
		jp->tableinfo[i].table = xmalloc(nprocs * table_rec_len);
	}

#if NRT_DEBUG
	info("Allocating windows: adapter_name:%s adapter_type:%s",
	     adapter_name, _adapter_type_str(adapter_type));
#else
	debug("Allocating windows");
#endif
	_lock();
	for  (i = 0; i < nnodes; i++) {
		host = hostlist_next(hi);
		if (!host)
			error("Failed to get next host");

		for (j = 0; j < tasks_per_node[i]; j++) {
			if (adapter_name == NULL) {
				rc = _allocate_windows_all(jp->tables_per_task,
							   jp->tableinfo,
							   host, i, tids[i][j],
							   jp->job_key,
							   adapter_type,
							   base_lid,
							   jp->user_space);
			} else {
				rc = _allocate_window_single(adapter_name,
							     jp->tableinfo,
							     host, i,
							     tids[i][j],
							     jp->job_key,
							     adapter_type,
							     base_lid,
							     jp->user_space);
			}
			if (rc != SLURM_SUCCESS) {
				_unlock();
				goto fail;
			}
		}
		free(host);
	}
	_unlock();


#if NRT_DEBUG
	info("nrt_build_jobinfo");
	_print_jobinfo(jp);
#endif

	hostlist_iterator_destroy(hi);
	return SLURM_SUCCESS;

fail:
	free(host);
	hostlist_iterator_destroy(hi);
	/* slurmctld will call nrt_free_jobinfo on jp */
	return SLURM_FAILURE;
}

static void
_pack_tableinfo(nrt_tableinfo_t *tableinfo, nrt_adapter_t adapter_type,
		Buf buf)
{
	int i;

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
			pack8(ib_tbl_ptr->port_id, buf);
			pack32(ib_tbl_ptr->task_id, buf);
			pack16(ib_tbl_ptr->win_id, buf);
		}
	} else if (adapter_type == NRT_IPONLY) {
		nrt_ip_task_info_t *ip_tbl_ptr;
		for (i = 0, ip_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, ip_tbl_ptr++) {
			packmem((char *) &ip_tbl_ptr->ip.ipv4_addr,
				sizeof(in_addr_t), buf);
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
			tmp_8 = hfi_tbl_ptr->win_id;
			pack8(tmp_8, buf);
		}
	} else {
		fatal("_pack_tableinfo: Missing support for adapter type %hu",
		      adapter_type);
	}
	packmem(tableinfo->adapter_name, NRT_MAX_DEVICENAME_SIZE, buf);
}

/* Used by: all */
extern int
nrt_pack_jobinfo(slurm_nrt_jobinfo_t *j, Buf buf)
{
	int i;
	nrt_adapter_t adapter_type;

	assert(j);
	assert(j->magic == NRT_JOBINFO_MAGIC);
	assert(buf);

#if NRT_DEBUG
	info("nrt_pack_jobinfo:");
	_print_jobinfo(j);
#endif
	pack32(j->magic, buf);
	pack32(j->job_key, buf);
	pack8(j->bulk_xfer, buf);
	pack8(j->ip_v6, buf);
	pack8(j->user_space, buf);
	pack16(j->tables_per_task, buf);
	pack64(j->network_id, buf);
	pack32(j->num_tasks, buf);
	packstr(j->protocol, buf);

	for (i = 0; i < j->tables_per_task; i++) {
		if (!j->user_space)
			adapter_type = NRT_IPONLY;
		else
			adapter_type = j->tableinfo[i].adapter_type;
		_pack_tableinfo(&j->tableinfo[i], adapter_type, buf);
	}

	return SLURM_SUCCESS;
}

/* return 0 on success, -1 on failure */
static int
_unpack_tableinfo(nrt_tableinfo_t *tableinfo, nrt_adapter_t adapter_type,
		  Buf buf)
{
	uint32_t size;
	char *name_ptr;
	int i;

	safe_unpack32(&tableinfo->table_length, buf);
	if (adapter_type == NRT_IB) {
		nrt_ib_task_info_t *ib_tbl_ptr;
		tableinfo->table = (nrt_ib_task_info_t *)
				   xmalloc(tableinfo->table_length *
				   sizeof(nrt_ib_task_info_t));
		for (i = 0, ib_tbl_ptr = tableinfo->table;
		     i < tableinfo->table_length;
		     i++, ib_tbl_ptr++) {
			safe_unpackmem(ib_tbl_ptr->device_name, &size, buf);
			if (size != NRT_MAX_DEVICENAME_SIZE)
				goto unpack_error;
			safe_unpack32(&ib_tbl_ptr->base_lid, buf);
			safe_unpack8(&ib_tbl_ptr->lmc, buf);
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
			safe_unpackmem((char *) &ip_tbl_ptr->ip.ipv4_addr,
				       &size, buf);
			if (size != sizeof(in_addr_t))
				goto unpack_error;
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
			uint16_t tmp_16;
			uint8_t  tmp_8;
			safe_unpack32(&hfi_tbl_ptr->task_id, buf);
			safe_unpack16(&tmp_16, buf);
			hfi_tbl_ptr->lid = tmp_16;
			safe_unpack8(&tmp_8, buf);
			hfi_tbl_ptr->win_id = tmp_8;
		}
	} else {
		fatal("_unpack_tableinfo: Missing support for adapter "
		      "type %hu", adapter_type);
	}
	safe_unpackmem_ptr(&name_ptr, &size, buf);
	if (size != NRT_MAX_DEVICENAME_SIZE)
		goto unpack_error;
	memcpy(tableinfo->adapter_name, name_ptr, size);
	return 0;

unpack_error: /* safe_unpackXX are macros which jump to unpack_error */
	error("unpack error in _unpack_tableinfo");
	return -1;
}

/* Used by: all */
extern int
nrt_unpack_jobinfo(slurm_nrt_jobinfo_t *j, Buf buf)
{
	nrt_adapter_t adapter_type;
	uint32_t uint32_tmp;
	int i;

	assert(j);
	assert(j->magic == NRT_JOBINFO_MAGIC);
	assert(buf);

	safe_unpack32(&j->magic, buf);
	assert(j->magic == NRT_JOBINFO_MAGIC);
	safe_unpack32(&j->job_key, buf);
	safe_unpack8(&j->bulk_xfer, buf);
	safe_unpack8(&j->ip_v6, buf);
	safe_unpack8(&j->user_space, buf);
	safe_unpack16(&j->tables_per_task, buf);
	safe_unpack64(&j->network_id, buf);
	safe_unpack32(&j->num_tasks, buf);
	safe_unpackstr_xmalloc(&j->protocol, &uint32_tmp, buf);

	j->tableinfo = (nrt_tableinfo_t *) xmalloc(j->tables_per_task *
						   sizeof(nrt_tableinfo_t));
	for (i = 0; i < j->tables_per_task; i++) {
		if (!j->user_space)
			adapter_type = NRT_IPONLY;
		else
			adapter_type = j->tableinfo[i].adapter_type;
		if (_unpack_tableinfo(&j->tableinfo[i], adapter_type, buf))
			goto unpack_error;
	}

#if NRT_DEBUG
	info("nrt_unpack_jobinfo:");
	_print_jobinfo(j);
#endif
	return SLURM_SUCCESS;

unpack_error:
	error("nrt_unpack_jobinfo error");
	xfree(j->protocol);
	if (j->tableinfo) {
		for (i = 0; i < j->tables_per_task; i++)
			xfree(j->tableinfo[i].table);
		xfree(j->tableinfo);
	}
	slurm_seterrno_ret(EUNPACK);
	return SLURM_ERROR;
}

/* Used by: all */
extern slurm_nrt_jobinfo_t *
nrt_copy_jobinfo(slurm_nrt_jobinfo_t *job)
{
	slurm_nrt_jobinfo_t *new;
	int i;
	int base_size = 0, table_size;

	assert(job);
	assert(job->magic == NRT_JOBINFO_MAGIC);

	if (nrt_alloc_jobinfo(&new)) {
		error("Allocating new jobinfo");
		slurm_seterrno(ENOMEM);
		return NULL;
	}
	memcpy(new, job, sizeof(slurm_nrt_jobinfo_t));

	/* table will be empty (and table_size == 0) when the network string
	 * from poe does not contain "us".
	 * (See man poe: -euilib or MP_EUILIB)
	 */
	new->tableinfo = (nrt_tableinfo_t *) xmalloc(job->tables_per_task *
						     sizeof(nrt_table_info_t));
	for (i = 0; i < job->tables_per_task; i++) {
		if (job->tableinfo->adapter_type == NRT_IB) {
			base_size = sizeof(nrt_ib_task_info_t);
		} else if (job->tableinfo->adapter_type == NRT_HFI) {
			base_size = sizeof(nrt_hfi_task_info_t);
		} else {
			fatal("nrt_copy_jobinfo: Missing support for adapter "
			      "type %hu", job->tableinfo->adapter_type);
		}
		new->tableinfo[i].table_length = job->tableinfo[i].table_length;
		table_size = base_size * job->tableinfo[i].table_length;
		new->tableinfo->table = xmalloc(table_size);
		memcpy(new->tableinfo[i].table, job->tableinfo[i].table,
		       table_size);
	}

	return new;
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
	xfree(jp->protocol);
	if ((jp->tables_per_task > 0) && (jp->tableinfo != NULL)) {
		for (i = 0; i < jp->tables_per_task; i++) {
			tableinfo = &jp->tableinfo[i];
			xfree(tableinfo->table);
		}
		xfree(jp->tableinfo);
	}

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
	nrt_tableinfo_t **tableinfo = (nrt_tableinfo_t **)data;
	int *tables_per = (int *)data;
	int *job_key = (int *)data;

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);

	switch (key) {
		case NRT_JOBINFO_TABLEINFO:
			*tableinfo = jp->tableinfo;
			break;
		case NRT_JOBINFO_TABLESPERTASK:
			*tables_per = jp->tables_per_task;
			break;
		case NRT_JOBINFO_KEY:
			*job_key = jp->job_key;
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
			  nrt_window_id_t window_id, int retry,
			  unsigned int max_windows)
{
	int err, i, j;
	int rc = SLURM_ERROR;
	nrt_cmd_status_adapter_t status_adapter;
	nrt_status_t **status_array = NULL;
	nrt_window_id_t window_count;

	status_array = xmalloc(sizeof(nrt_status_t *) * max_windows);
 	for (j = 0; j < max_windows; j++) {
		/*
		 * WARNING: DO NOT USE xmalloc here!
		 *	
		 * The nrt_command(NRT_CMD_STATUS_ADAPTER) function
		 * changes pointer values and returns memory that is
		 * allocated with malloc() and deallocated with free()
		 */
		status_array[j] = malloc(sizeof(nrt_status_t) * max_windows);
 	}
	status_adapter.adapter_name = adapter_name;
	status_adapter.adapter_type = adapter_type;
	status_adapter.status_array = status_array;
	status_adapter.window_count = &window_count;

	for (i = 0; i < retry; i++) {
		if (i > 0)
			sleep(1);

		err = nrt_command(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
				  &status_adapter);
		if (err != NRT_SUCCESS) {
			error("nrt_status_adapter(%s, %s): %s", adapter_name,
			      _adapter_type_str(adapter_type),
			      nrt_err_str(err));
			break;
		}
#if NRT_DEBUG
		info("_wait_for_window_unloaded");
		_print_adapter_status(&status_adapter);
#endif
		for (j = 0; j < window_count; j++) {
			if ((*status_array)[j].window_id == window_id)
				break;
		}
		if (j >= window_count) {
			error("nrt_status_adapter(%s, %s), window %hu not "
			      "found",
			      adapter_name, _adapter_type_str(adapter_type),
			      window_id);
			break;
		}
		if ((*status_array)[j].state == NRT_WIN_AVAILABLE) {
			rc = SLURM_SUCCESS;
			break;
		}
		debug2("nrt_status_adapter(%s, %s), window %u state %s",
		       adapter_name,
		       _adapter_type_str(adapter_type), window_id,
		       _win_state_str((*status_array)[j].state));
	}

 	for (j = 0; j < max_windows; j++) {
		free(status_array[j]);
 	}
	xfree(status_array);

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
	nrt_cmd_query_adapter_names_t adapter_names;
	unsigned int max_windows, num_adapter_names;

	adapter_names.adapter_type = tableinfo->adapter_type;
	adapter_names.max_windows = &max_windows;
	adapter_names.num_adapter_names = &num_adapter_names;
	err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
			  &adapter_names);
	if (err != NRT_SUCCESS) {
		error("nrt_command(adapter_names, %s): %s",
		      _adapter_type_str(adapter_names.adapter_type),
		      nrt_err_str(err));
		rc = SLURM_ERROR;
		max_windows = 16;	/* FIXME: What should this be? */
	}
	
	for (i = 0; i < tableinfo->table_length; i++) {
		if (tableinfo->adapter_type == NRT_IB) {
			nrt_ib_task_info_t *ib_tbl_ptr;
			ib_tbl_ptr = (nrt_ib_task_info_t *) tableinfo->table;
			ib_tbl_ptr += i;
			window_id = ib_tbl_ptr->win_id;
		} else if (adapter_names.adapter_type == NRT_HFI) {
			nrt_hfi_task_info_t *hfi_tbl_ptr;
			hfi_tbl_ptr = (nrt_hfi_task_info_t *) tableinfo->table;
			hfi_tbl_ptr += i;
			window_id = hfi_tbl_ptr->win_id;
		} else {
			fatal("_wait_for_all_windows: Missing support for "
			      "adapter_type:%s",
			      _adapter_type_str(tableinfo->adapter_type));
		}

		err = _wait_for_window_unloaded(tableinfo->adapter_name,
						tableinfo->adapter_type,
						window_id, retry, max_windows);
		if (err != SLURM_SUCCESS) {
			error("Window %hu adapter %s did not "
			      "become free within %d seconds",
			      window_id, tableinfo->adapter_name, retry);
			rc = err;
		}
	}

	return rc;
}

static int
_check_rdma_job_count(char *adapter_name, nrt_adapter_t adapter_type)
{
	uint16_t job_count = 0;
	uint16_t *job_keys = NULL;
	int err, i;

#if 1
	err = NRT_SUCCESS;
#else
/* FIXME: Address this later, RDMA jobs are those using bulk transters */
	err = nrt_rdma_jobs(NRT_VERSION, adapter_name, adapter_type,
			    &job_count, &job_keys);
#endif
	if (err != NRT_SUCCESS) {
		error("nrt_rdma_jobs(): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}
#if NRT_DEBUG
	info("_check_rdma_job_count: nrt_rdma_jobs:");
	info("adapter_name:%s adapter_type:%s", adapter_name,
	     _adapter_type_str(adapter_type));
	for (i = 0; i < job_count; i++)
		info("  job_keys[%d]:%hu", i, job_keys[i]);
#endif
	if (job_keys)
		free(job_keys);
	if (job_count >= 4) {
		error("RDMA job_count is too high: %hu", job_count);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

/* Load a network table on node.  If table contains more than one window
 * for a given adapter, load the table only once for that adapter.
 *
 * Used by: slurmd
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

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);

#if NRT_DEBUG
	info("nrt_load_table");
	_print_jobinfo(jp);
#endif
	for (i = 0; i < jp->tables_per_task; i++) {
#if NRT_DEBUG
		nrt_adapter_t adapter_type;
		if (jp->user_space)
			adapter_type = jp->tableinfo[i].adapter_type;
		else
			adapter_type = NRT_IPONLY;
		_print_table(jp->tableinfo[i].table,
			     jp->tableinfo[i].table_length, adapter_type);
#endif
		adapter_name = jp->tableinfo[i].adapter_name;
		if (jp->user_space) {
			rc = _wait_for_all_windows(&jp->tableinfo[i]);
			if (rc != SLURM_SUCCESS)
				return rc;
		}

		if (adapter_name == NULL)
			continue;
		if (jp->bulk_xfer && (i == 0)) {
			rc = _check_rdma_job_count(adapter_name,
						   jp->tableinfo[i].
						   adapter_type);
			if (rc != SLURM_SUCCESS)
				return rc;
		}

/* FIXME: Ne need to set a bunch of these paramters appropriately */
#define TBD0 0
		bzero(&table_info, sizeof(nrt_table_info_t));
		table_info.num_tasks = jp->tableinfo[i].table_length;
		table_info.job_key = jp->job_key;
		table_info.uid = uid;
		table_info.network_id = jp->network_id;
		table_info.pid = pid;
		table_info.adapter_type = jp->tableinfo[i].adapter_type;
		if (jp->user_space) {
			table_info.is_ipv4 = 0;
			table_info.is_user_space = 1;
		} else if (jp->ip_v6) {
			table_info.is_ipv4 = 0;
			table_info.is_user_space = 0;
		} else {
			table_info.is_ipv4 = 1;
			table_info.is_user_space = 0;
		}
		table_info.context_id = 0;
		table_info.table_id = TBD0;
		if (job_name) {
			char *sep = strrchr(job_name,'/');
			if (sep)
				sep++;
			else
				sep = job_name;
			strncpy(table_info.job_name, sep,
				NRT_MAX_JOB_NAME_LEN);
		} else {
			table_info.job_name[0] = '\0';
		}
		if (jp->protocol) {
			strncpy(table_info.protocol_name, jp->protocol,
				NRT_MAX_PROTO_NAME_LEN);
		}
		table_info.use_bulk_transfer = jp->bulk_xfer;
		table_info.bulk_transfer_resources = TBD0;
		/* The following fields only apply to Power7 processors
		 * and have no effect on x86 processors:
		 * immed_send_slots_per_win
		 * num_cau_indexes */
		table_info.immed_send_slots_per_win = 0;
		table_info.num_cau_indexes = 0;
		load_table.table_info = &table_info;
		load_table.per_task_input = jp->tableinfo[i].table;
#if NRT_DEBUG
		_print_load_table(&load_table);
#endif
		err = nrt_command(NRT_VERSION, NRT_CMD_LOAD_TABLE,
				  &load_table);
		if (err != NRT_SUCCESS) {
			error("nrt_command(load table): %s", nrt_err_str(err));
			return SLURM_ERROR;
		}
	}
	umask(nrt_umask);

#if NRT_DEBUG
	info("nrt_load_table complete");
#endif
	return SLURM_SUCCESS;
}

/*
 * Try up to "retry" times to unload a window.
 */
static int
_unload_window(char *adapter_name, nrt_adapter_t adapter_type,
	       nrt_job_key_t job_key, nrt_window_id_t window_id, int retry)
{
	int err, i;
	nrt_cmd_clean_window_t  clean_window;
	nrt_cmd_unload_window_t unload_window;

	for (i = 0; i < retry; i++) {
		if (i > 0) {
			sleep(1);
		} else {
			unload_window.adapter_name = adapter_name;
			unload_window.adapter_type = adapter_type;
			unload_window.job_key = job_key;
			unload_window.window_id = window_id;
		}
#if NRT_DEBUG
		info("nrt_command(unload_window, %s, %u, %u, %hu)",
		      adapter_name, adapter_type, job_key, window_id);
#endif
		err = nrt_command(NRT_VERSION, NRT_CMD_UNLOAD_WINDOW,
				  &unload_window);
		if (err == NRT_SUCCESS)
			return SLURM_SUCCESS;
		debug("Unable to unload window for job_key %hu, "
		      "nrt_unload_window(%s, %u): %s",
		      job_key, adapter_name, adapter_type, nrt_err_str(err));

		if (i == 0) {
			clean_window.adapter_name = adapter_name;
			clean_window.adapter_type = adapter_type;
			clean_window.leave_inuse_or_kill = KILL;
			clean_window.window_id = window_id;
		}
		err = nrt_command(NRT_VERSION, NRT_CMD_CLEAN_WINDOW,
				  &clean_window);
		if (err == NRT_SUCCESS)
			return SLURM_SUCCESS;
		error("Unable to clean window for job_key %hu, "
		      "nrt_clean_window(%s, %u): %s",
		      job_key, adapter_name, adapter_type, nrt_err_str(err));
		if (err != NRT_EAGAIN)
			break;
	}

	return SLURM_FAILURE;
}


/* Assumes that, on error, new switch state information will be
 * read from node.
 *
 * Used by: slurmd
 */
extern int
nrt_unload_table(slurm_nrt_jobinfo_t *jp)
{
	nrt_window_id_t window_id = 0;
	int err, i, j, rc = SLURM_SUCCESS;
	int retry = 15;

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);
#if NRT_DEBUG
	info("nrt_unload_table");
	_print_jobinfo(jp);
#endif
	if (!jp->user_space)
		return rc;
	for (i = 0; i < jp->tables_per_task; i++) {
		for (j = 0; j < jp->tableinfo[i].table_length; j++) {
			if (jp->tableinfo[i].adapter_type == NRT_IB) {
				nrt_ib_task_info_t *ib_tbl_ptr;
				ib_tbl_ptr = (nrt_ib_task_info_t *)
					     jp->tableinfo[i].table;
				ib_tbl_ptr += j;
				window_id = ib_tbl_ptr->win_id;
			} else if (jp->tableinfo[i].adapter_type == NRT_HFI) {
				nrt_hfi_task_info_t *hfi_tbl_ptr;
				hfi_tbl_ptr = (nrt_hfi_task_info_t *)
					      jp->tableinfo[i].table;
				hfi_tbl_ptr += j;
				window_id = hfi_tbl_ptr->win_id;
			} else {
				fatal("nrt_unload_table: invalid adapter "
				      "type: %s",
				      _adapter_type_str(jp->tableinfo[i].
							adapter_type));
			}
			err = _unload_window(jp->tableinfo[i].adapter_name,
					     jp->tableinfo[i].adapter_type,
					     jp->job_key,
					     window_id, retry);
			if (err != NRT_SUCCESS)
				rc = SLURM_ERROR;
		}
	}
	return rc;
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
_pack_libstate(slurm_nrt_libstate_t *lp, Buf buffer)
{
	int offset;
	int i;

	assert(lp);
	assert(lp->magic == NRT_LIBSTATE_MAGIC);

#if NRT_DEBUG
 	info("_pack_libstate");
	_print_libstate(lp);
#endif
	offset = get_buf_offset(buffer);
	pack32(lp->magic, buffer);
	pack32(lp->node_count, buffer);
	for (i = 0; i < lp->node_count; i++)
		(void)nrt_pack_nodeinfo(&lp->node_list[i], buffer);
	/* don't pack hash_table, we'll just rebuild on restore */
	pack16(lp->key_index, buffer);

	return(get_buf_offset(buffer) - offset);
}

/* Used by: slurmctld */
extern void
nrt_libstate_save(Buf buffer, bool free_flag)
{
	_lock();

	if (nrt_state != NULL)
		_pack_libstate(nrt_state, buffer);

	/* Clean up nrt_state since backup slurmctld can repeatedly
	 * save and restore state */
	if (free_flag) {
		_free_libstate(nrt_state);
		nrt_state = NULL;	/* freed above */
	}
	_unlock();
}

/* Used by: slurmctld */
static int
_unpack_libstate(slurm_nrt_libstate_t *lp, Buf buffer)
{
	uint32_t node_count;
	int i;

	assert(lp->magic == NRT_LIBSTATE_MAGIC);

	safe_unpack32(&lp->magic, buffer);
	safe_unpack32(&node_count, buffer);
	for (i = 0; i < node_count; i++) {
		if (_unpack_nodeinfo(NULL, buffer, false) != SLURM_SUCCESS)
			goto unpack_error;
	}
	if (lp->node_count != node_count) {
		error("Failed to recover switch state of all nodes (%u of %u)",
		      lp->node_count, node_count);
		return SLURM_ERROR;
	}
	safe_unpack16(&lp->key_index, buffer);
#if NRT_DEBUG
 	info("_unpack_libstate");
	_print_libstate(lp);
 #endif
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
	_lock();
	assert(!nrt_state);

	nrt_state = _alloc_libstate();
	if (!nrt_state) {
		error("nrt_libstate_restore nrt_state is NULL");
		_unlock();
		return SLURM_FAILURE;
	}
	_unpack_libstate(nrt_state, buffer);
	_unlock();

	return SLURM_SUCCESS;
}

extern int
nrt_libstate_clear(void)
{
	int i, j, k;
	slurm_nrt_nodeinfo_t *node;
	slurm_nrt_adapter_t *adapter;
	slurm_nrt_window_t *window;


#if NRT_DEBUG
	info("Clearing state on all windows in global NRT state");
#else
	debug3("Clearing state on all windows in global NRT state");
#endif
	_lock();
	if (!nrt_state || !nrt_state->node_list) {
		error("nrt_state or node_list not initialized!");
		_unlock();
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
	_unlock();

	return SLURM_SUCCESS;
}

extern int
nrt_clear_node_state(void)
{
	int err, i, j, k, rc = SLURM_SUCCESS;
	nrt_cmd_query_adapter_types_t adapter_types;
	unsigned int num_adapter_types;
	nrt_adapter_t adapter_type[NRT_MAX_ADAPTER_TYPES];
	nrt_cmd_query_adapter_names_t adapter_names;
	unsigned int max_windows, num_adapter_names;
	nrt_cmd_status_adapter_t adapter_status;
	nrt_window_id_t window_count;
	nrt_status_t **status_array = NULL;
	nrt_cmd_clean_window_t clean_window;

#if NRT_DEBUG
	info("nrt_clear_node_state: begin");
#endif
	adapter_types.num_adapter_types = &num_adapter_types;
	adapter_types.adapter_types = adapter_type;
	for (i = 0; i < 2; i++) {
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
				  &adapter_types);
		if (err != NRT_EAGAIN)
			break;
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		error("Is pnsd daemon started? Retrying...");
		/* Run "/opt/ibmhpc/pecurrent/ppe.pami/pnsd/pnsd -A" */
		sleep(5);
	}
	if (err != NRT_SUCCESS) {
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}
#if NRT_DEBUG
	for (i = 0; i < num_adapter_types; i++) {
		info("nrt_command(adapter_types): %s",
		    _adapter_type_str(adapter_types.adapter_types[i]));
	}
#endif

	for (i = 0; i < num_adapter_types; i++) {
		adapter_names.adapter_type = adapter_type[i];
		adapter_names.num_adapter_names = &num_adapter_names;
		adapter_names.max_windows = &max_windows;
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				  &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_command(adapter_names, %s): %s",
			      _adapter_type_str(adapter_names.adapter_type),
			      nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
#if NRT_DEBUG
		for (j = 0; j < num_adapter_names; j++) {
			info("nrt_command(adapter_names, %s, %s) "
			     "max_windows: %hu",
			     adapter_names.adapter_names[j],
			     _adapter_type_str(adapter_names.adapter_type),
			     max_windows);
		}
#endif
		status_array = xmalloc(sizeof(nrt_status_t *) * max_windows);
		for (j = 0; j < max_windows; j++) {
			/*
			 * WARNING: DO NOT USE xmalloc here!
			 *	
			 * The nrt_command(NRT_CMD_STATUS_ADAPTER) function
			 * changes pointer values and returns memory that is
			 * allocated with malloc() and deallocated with free()
			 */
			status_array[j] = malloc(sizeof(nrt_status_t) *
						 max_windows);
		}
		for (j = 0; j < num_adapter_names; j++) {
			adapter_status.adapter_name = adapter_names.
						      adapter_names[j];
			adapter_status.adapter_type = adapter_names.
						      adapter_type;
			adapter_status.status_array = status_array;
			adapter_status.window_count = &window_count;
			err = nrt_command(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
					  &adapter_status);
			if (err != NRT_SUCCESS) {
				error("nrt_command(status_adapter, %s, %s): %s",
				      adapter_status.adapter_name,
				      _adapter_type_str(adapter_status.
							adapter_type),
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}
#if NRT_DEBUG
			info("nrt_command(status_adapter, %s, %s) "
			     "window_count: %hu",
			     adapter_status.adapter_name,
			     _adapter_type_str(adapter_status.adapter_type),
			     window_count);
			/* Only log first NRT_DEBUG_CNT windows here */
			for (k = 0; k < MIN(window_count, NRT_DEBUG_CNT); k++){
				info("window_id:%d uid:%d pid:%d state:%s",
				     (*status_array)[k].window_id,
				     (*status_array)[k].uid,
				     (*status_array)[k].client_pid,
				     _win_state_str((*status_array)[k].state));
			}
#endif
			for (k = 0; k < window_count; k++) {
				clean_window.adapter_name = adapter_names.
							    adapter_names[j];
				clean_window.adapter_type = adapter_names.
							    adapter_type;
				clean_window.leave_inuse_or_kill = KILL;
				clean_window.window_id = (*status_array)[k].
							 window_id;
				err = nrt_command(NRT_VERSION,
						  NRT_CMD_CLEAN_WINDOW,
						  &clean_window);
				if (err != NRT_SUCCESS) {
					error("nrt_command(clean_window, "
					      "%s, %s, %u): %s",
					      clean_window.adapter_name,
					      _adapter_type_str(clean_window.
								adapter_type),
					      clean_window.window_id,
					      nrt_err_str(err));
					rc = SLURM_ERROR;
					continue;
				}
#if NRT_DEBUG
				if (k < NRT_DEBUG_CNT) {
					info("nrt_command(clean_window, "
					     "%s, %s, %u)",
					     clean_window.adapter_name,
					     _adapter_type_str(clean_window.
							       adapter_type),
					     clean_window.window_id);
				}
#endif
			}
		}
		for (j = 0; j < max_windows; j++) {
			free(status_array[j]);
		}
		xfree(status_array);
	}
#if NRT_DEBUG
	info("nrt_clear_node_state: complete:%d", rc);
#endif
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


/* return an adapter name from within a job's "network" string
 * IN network - job's "network" specification
 * IN list - hostlist of allocated nodes
 * RET - A network name, must xfree() or NULL if none found */
extern char *nrt_adapter_name_check(char *network, hostlist_t hl)
{
	int i;
	hostlist_iterator_t hi;
	slurm_nrt_nodeinfo_t *node;
	char *host, *net_str = NULL, *token = NULL, *last = NULL;
	char *adapter_name = NULL;

	if (!network || !hl)
		return NULL;

	hi = hostlist_iterator_create(hl);
	host = hostlist_next(hi);
	hostlist_iterator_destroy(hi);
	_lock();
	node = _find_node(nrt_state, host);
	if (node && node->adapter_list) {
		net_str = xstrdup(network);
		token = strtok_r(network, ",", &last);
	}
	while (token) {
		for (i = 0; i < node->adapter_count; i++) {
			if (!strcmp(token,node->adapter_list[i].adapter_name)){
				adapter_name = xstrdup(token);
				break;
			}
		}
		if (adapter_name)
			break;
		token = strtok_r(NULL, ",", &last);
	}
	_unlock();
	xfree(net_str);
	return adapter_name;
}
