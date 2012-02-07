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

#define JOB_DESC_LEN 64	/* Length of job description */
#define NRT_HOSTLEN 20
#define NRT_NODECOUNT 128
#define NRT_HASHCOUNT 128
#define NRT_AUTO_WINMEM 0
#define NRT_MAX_WIN 15
#define NRT_MIN_WIN 0

char* nrt_conf = NULL;
extern bool nrt_need_state_save;

mode_t nrt_umask;

/*
 * Data structures specific to switch/nrt
 *
 * We are going to some trouble to keep these defs private so slurm
 * hackers not interested in the interconnect details can just pass around
 * the opaque types.  All use of the data structure internals is local to this
 * module.
 */

typedef struct nrt_window {
	uint16_t window_id;
	uint32_t state;
	uint16_t job_key;  /* FIXME: Perhaps change to uid or client_pid? */
} nrt_window_t;

typedef struct nrt_adapter {
	char adapter_name[NRT_MAX_DEVICENAME_SIZE];
	uint16_t adapter_type;
	uint16_t lid[MAX_SPIGOTS];
	uint64_t network_id[MAX_SPIGOTS];
	uint16_t window_count;
	nrt_window_t *window_list;
} nrt_adapter_t;

struct nrt_nodeinfo {
	uint32_t magic;
	char name[NRT_HOSTLEN];
	uint32_t adapter_count;
	nrt_adapter_t *adapter_list;
	struct nrt_nodeinfo *next;
};

struct nrt_libstate {
	uint32_t magic;
	uint32_t node_count;
	uint32_t node_max;
	nrt_nodeinfo_t *node_list;
	uint32_t hash_max;
	nrt_nodeinfo_t **hash_table;
	uint16_t key_index;
};

struct nrt_jobinfo {
	uint32_t magic;
	/* version from nrt_version() */
	/* adapter from lid in table */
	/* network_id from lid in table */
	/* uid from getuid() */
	/* pid from getpid() */
	uint16_t job_key;
	char job_desc[JOB_DESC_LEN];
	uint8_t bulk_xfer;  /* flag */
	uint16_t tables_per_task;
	nrt_tableinfo_t *tableinfo;

	hostlist_t nodenames;
	int num_tasks;
};

typedef struct {
	char adapter_names[NRT_MAX_ADAPTER_NAME_LEN];
	uint16_t adapter_type;
} nrt_cache_entry_t;

/*
 * Globals
 */
nrt_libstate_t *nrt_state = NULL;
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
#define NRT_MAX_ADAPTERS (NRT_MAX_ADAPTERS_PER_TYPE * NRT_MAX_ADAPTER_TYPES)

/* slurmd/slurmstepd global variables */
hostlist_t adapter_list;
uint16_t   adapter_type_code = 0;
static int lid_cache_size = 0;
static nrt_cache_entry_t lid_cache[NRT_MAX_ADAPTERS];

static int  _fill_in_adapter_cache(void);
static void _hash_rebuild(nrt_libstate_t *state);
static void _init_adapter_cache(void);
static int  _set_up_adapter(nrt_adapter_t *nrt_adapter, char *adapter_name,
			    uint16_t adapter_type);
static char *_win_state_str(win_state_t state);

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

static char *_win_state_str(win_state_t state)
{
	if (state == NRT_WIN_UNAVAILABLE)
		return "Unavailable";
	else if (state == NRT_WIN_INVALID)
		return "Invalid";
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

extern int
nrt_slurmctld_init(void)
{
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

#if NRT_DEBUG
/* Used by: all */
static void
_print_table(nrt_creator_per_task_input_t *table, int size)
{
	uint16_t adapter_type = RSCT_DEVTYPE_INFINIBAND;
	int i;

	assert(table);
	assert(size > 0);

	info("--Begin NRT table--");
	for (i = 0; i < size; i++) {
/* FIXME: table contains a union, it could contain either IB or HPCE data */
		if (adapter_type == RSCT_DEVTYPE_INFINIBAND) {
			nrt_creator_ib_per_task_input_t *ib_tbl_ptr;
			ib_tbl_ptr = &table[i].ib_per_task;
			info("  task_id:  %hu", ib_tbl_ptr->task_id);
			info("  win_id:   %hu", ib_tbl_ptr->win_id);
			info("  base_lid: %hu", ib_tbl_ptr->base_lid);
		} else {
			fatal("_print_table: lack HPCE code");
		}
	}
	info("--End NRT table--");
}


/* Used by: slurmd */
static void 
_print_adapter_resources(char *adapter_name, uint16_t adapter_type,
			 adap_resources_t *adapter_res)
{
	int i;

	info("--Begin Adapter Resources--");
	info("  adapter_name:%s adapter_type:%hu", adapter_name, adapter_type);
	info("  node_number:%u", adapter_res->node_number);
	info("  num_spigots:%hu", adapter_res->num_spigots);
	for (i = 0; i < adapter_res->num_spigots; i++) {
		info("  lid[%d]:%hu", i, adapter_res->lid[i]);
		info("  network_id[%d]:%"PRIu64"",
		     i, adapter_res->network_id[i]);
		info("  lmc[%d]:%hu", i, adapter_res->lmc[i]);
		info("  spigot_id[%d]:%hu", i, adapter_res->spigot_id[i]);
	}
	info("  window_count:%hu", adapter_res->window_count);
	for (i = 0; i < adapter_res->window_count; i++)
		info("  window_list[%d]:%hu", i, adapter_res->window_list[i]);
	info("  rcontext_block_count:%"PRIu64"",
	     adapter_res->rcontext_block_count);
	info("--End Adapter Resources--");
}

/* Used by: slurmd */
static void
_print_adapter_status(nrt_cmd_status_adapter_t *status_adapter)
{
	int i;

	info("--Begin Adapter Status--");
	info("  adapter_name:%s adapter_type:%hu",
	     status_adapter->adapter_name, status_adapter->adapter_type);
	for (i = 0; i < status_adapter->window_count; i++) {
		nrt_status_t *status = status_adapter->status_array[i];
		info("  client_pid[%d]:%u", i, (uint32_t)status->client_pid);
		info("  uid[%d]:%u", i, (uint32_t) status->uid);
		info("  window_id[%d]:%hu", i, status->window_id);
		info("  bulk_xfer[%d]:%hu", i, status->bulk_transfer);
		info("  rcontext_blocks[%d]:%u", i, status->rcontext_blocks);
		info("  state[%d]:%s", i, _win_state_str(status->state));
	}
	info("--End Adapter Status--");
}

/* Used by: slurmd, slurmctld */
static void
_print_jobinfo(nrt_jobinfo_t *j)
{
	char buf[128];

	assert(j);
	assert(j->magic == NRT_JOBINFO_MAGIC);

	info("--Begin Jobinfo--");
	info("  job_key: %u", j->job_key);
	info("  job_desc: %s", j->job_desc);
	info("  table_size: %u", j->tables_per_task);
	info("  bulk_xfer: %u", j->bulk_xfer);
	info("  tables_per_task: %hu", j->tables_per_task);
	hostlist_ranged_string(j->nodenames, sizeof(buf), buf);
	info("  nodenames: %s", buf);
	info("  num_tasks: %d", j->num_tasks);
	info("  tableinfo supressed");
	info("--End Jobinfo--");
}

/* Used by: slurmd, slurmctld */
static void
_print_nodeinfo(nrt_nodeinfo_t *n)
{
	int i, j;
	nrt_adapter_t *a;
	nrt_window_t *w;

	assert(n);
	assert(n->magic == NRT_NODEINFO_MAGIC);

	info("--Begin Node Info--");
	info("  node: %s", n->name);
	info("  adapter_count: %u", n->adapter_count);
	for (i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		info("  adapter: %s", a->adapter_name);
		info("    type: %hu", a->adapter_type);
		info("    window_count: %hu", a->window_count);
#if (NRT_DEBUG > 1)
		info("    lid[0]: %hu", a->lid[0]);
		info("    network_id[0]: %"PRIu64"", a->network_id[0]);
#endif
		w = a->window_list;
		for (j = 0; j < a->window_count; j++) {
#if (NRT_DEBUG < 2)
			if (w[j].state != NRT_WIN_AVAILABLE)
				continue;
#endif
			info("    window %hu: %s", w->window_id,
			     nrt_err_str(w->state));
		}
	}
	info("--End Node Info--");
}

/* Used by: slurmctld */
static void
_print_libstate(const nrt_libstate_t *l)
{
	int i;

	assert(l);
	assert(l->magic == NRT_LIBSTATE_MAGIC);

	info("--Begin libstate--\n");
	info("  node_count = %u", l->node_count);
	info("  node_max = %u", l->node_max);
	info("  hash_max = %u", l->hash_max);
	for (i = 0; i < l->node_count; i++) {
		_print_nodeinfo(&l->node_list[i]);
	}
	info("--End libstate--");
}
#endif

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
	nrt_cmd_query_adapter_names_t adapter_names;

#if NRT_DEBUG
	info("_fill_in_adapter_cache: begin");
#endif
	err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
			  &adapter_types);
	if (err != NRT_SUCCESS) {
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}

	for (i = 0; i < adapter_types.num_adapter_types[0]; i++) {
#if NRT_DEBUG
		info("adapter_type[%d]: %u",
		     i, adapter_types.adapter_types[i]);
#endif
		adapter_names.adapter_type = adapter_types.adapter_types[i];
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				  &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_command(adapter_names, %u): %s",
			      adapter_names.adapter_type, nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
		for (j = 0; j < adapter_names.num_adapter_names[0]; j++) {
#if NRT_DEBUG
			info("adapter_names[%d]: %s",
			     j, adapter_names.adapter_names[j]);
#endif
			adapter_status.adapter_name = adapter_names.
						      adapter_names[j];
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


/* Cache the lid and network_id of a given adapter.  Ex:  sni0 with lid 10
 * gets cached in array index 0 with a lid = 10 and a name = sni0.
 *
 * Used by: slurmd
 */
static void
_cache_lid(nrt_adapter_t *ap)
{
	int j;
	assert(ap);

	int adapter_num = ap->adapter_name[3] - (int) '0';

	for (j = 0; j < MAX_SPIGOTS; j++) {
		lid_cache[adapter_num].lid[j] = ap->lid[j];
		lid_cache[adapter_num].network_id[j] = ap->network_id[j];
	}
	strncpy(lid_cache[adapter_num].adapter_name, ap->adapter_name,
		NRT_MAX_DEVICENAME_SIZE);
	lid_cache[adapter_num].adapter_type = ap->adapter_type;
}


/* Check lid cache for an adapter name and return the network id.
 *
 * Used by: slurmd
 */
static uint64_t
_get_network_id_from_adapter(char *adapter_name)
{
	int i;

	for (i = 0; i < NRT_MAXADAPTERS; i++) {
		if (!strncmp(adapter_name, lid_cache[i].adapter_name,
			     NRT_MAX_DEVICENAME_SIZE)) {
/* FIXME: Return which spigot's network_id? */
			return lid_cache[i].network_id[0];
		}
	}

	return (uint16_t) -1;
}


static int _set_up_adapter(nrt_adapter_t *nrt_adapter, char *adapter_name,
			   uint16_t adapter_type)
{
	adap_resources_t res;
	nrt_status_t *status = NULL;
	nrt_window_t *tmp_winlist = NULL;
	uint16_t win_count = 0;
	int err, i, j;

	err = nrt_adapter_resources(NRT_VERSION, adapter_name, adapter_type,
				    &res);
	if (err != NRT_SUCCESS) {
		error("nrt_adapter_resources(%s, %hu): %s",
		      adapter_name, adapter_type, nrt_err_str(err));
		return SLURM_ERROR;
	}
#if NRT_DEBUG
	info("_set_up_adapter: nrt_adapter_resources():");
	_print_adapter_resources(adapter_name, adapter_type, &res);
#endif

	strncpy(nrt_adapter->adapter_name, adapter_name,
		NRT_MAX_DEVICENAME_SIZE);
	nrt_adapter->adapter_type = adapter_type;
	for (j = 0; j < MAX_SPIGOTS; j++) {
		nrt_adapter->lid[j] = res.lid[j];
		nrt_adapter->network_id[j] = res.network_id[j];
	}
	nrt_adapter->window_count = res.window_count;
	free(res.window_list);
	_cache_lid(nrt_adapter);
	err = nrt_status_adapter(NRT_VERSION, adapter_name, adapter_type,
				 &win_count, &status);
	umask(nrt_umask);
	if (err != NRT_SUCCESS) {
		error("nrt_status_adapter(%s, %u): %s", adapter_name,
		      adapter_type, nrt_err_str(err));
		slurm_seterrno_ret(ESTATUS);
	}
#if NRT_DEBUG
	info("_set_up_adapter: nrt_status_adapter():");
	_print_adapter_status(adapter_name,  adapter_type, win_count, status);
#endif
	tmp_winlist = (nrt_window_t *) xmalloc(sizeof(nrt_window_t) *
					       res.window_count);
	for (i = 0; i < res.window_count; i++) {
		tmp_winlist[i].window_id = status->window_id;
		tmp_winlist[i].state = status->state;
	}
	free(status);
	nrt_adapter->window_list = tmp_winlist;

	return SLURM_SUCCESS;
}

/* Check for existence of sniX, where X is from 0 to NRT_MAXADAPTERS.
 * For all that exist, record vital adapter info plus status for all windows
 * available on that adapter.  Cache lid to adapter name mapping locally.
 *
 * Used by: slurmd
 */
static int
_get_adapters(nrt_adapter_t *list, int *count)
{
	hostlist_iterator_t adapter_iter;
	char *adapter_name = NULL;
	int i, rc;

	assert(list != NULL);
	assert(adapter_list != NULL);

	adapter_iter = hostlist_iterator_create(adapter_list);
	for (i = 0; (adapter_name = hostlist_next(adapter_iter)); i++) {
		rc = _set_up_adapter(list + i, adapter_name,
				     adapter_type_code);
		if (rc != SLURM_SUCCESS) {
			fatal("Failed to set up adapter %s of type %hu",
			      adapter_name, adapter_type_code);
		}
		free(adapter_name);
	}
	hostlist_iterator_destroy(adapter_iter);

	assert(i > 0);
	*count = i;
	info("Number of adapters is = %d", *count);

	if (!*count)
		slurm_seterrno_ret(ENOADAPTER);

	return 0;
}

/* Used by: slurmd, slurmctld */
extern int
nrt_alloc_jobinfo(nrt_jobinfo_t **j)
{
	nrt_jobinfo_t *new;

	assert(j != NULL);
	new = (nrt_jobinfo_t *) xmalloc(sizeof(nrt_jobinfo_t));
	new->magic = NRT_JOBINFO_MAGIC;
	new->job_key = -1;
	new->tables_per_task = 0;
	new->tableinfo = NULL;
	*j = new;

	return 0;
}

/* Used by: slurmd, slurmctld */
extern int
nrt_alloc_nodeinfo(nrt_nodeinfo_t **n)
{
	nrt_nodeinfo_t *new;

 	assert(n);

	new = (nrt_nodeinfo_t *) xmalloc(sizeof(nrt_nodeinfo_t));
	new->adapter_list = (nrt_adapter_t *) xmalloc(sizeof(nrt_adapter_t) *
			    NRT_MAXADAPTERS);
	new->magic = NRT_NODEINFO_MAGIC;
	new->adapter_count = 0;
	new->next = NULL;

	*n = new;

	return 0;
}

/* Assumes a pre-allocated nodeinfo structure and uses _get_adapters
 * to do the dirty work.  We probably collect more information about
 * the adapters on a give node than we need to but it was done
 * in the interest of being prepared for future requirements.
 *
 * Used by: slurmd
 */
extern int
nrt_build_nodeinfo(nrt_nodeinfo_t *n, char *name)
{
	int count;
	int err;

	assert(n);
	assert(n->magic == NRT_NODEINFO_MAGIC);
	assert(name);

	strncpy(n->name, name, NRT_HOSTLEN);
	_lock();
	err = _get_adapters(n->adapter_list, &count);
	_unlock();
	if (err != 0)
		return err;
	n->adapter_count = count;
	return 0;
}

/* Used by: all */
extern int
nrt_pack_nodeinfo(nrt_nodeinfo_t *n, Buf buf)
{
	int i, j;
	nrt_adapter_t *a;
	int offset;

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
		packmem(a->adapter_name, NRT_MAX_DEVICENAME_SIZE, buf);
		pack16(a->adapter_type, buf);
		for (j = 0; j < MAX_SPIGOTS; j++) {
			pack16(a->lid[j], buf);
			pack64(a->network_id[j], buf);
		}
		pack16(a->window_count, buf);
		for (j = 0; j < a->window_count; j++) {
			pack16(a->window_list[j].window_id, buf);
			pack32(a->window_list[j].state, buf);
			pack16(a->window_list[j].job_key, buf);
		}
	}

	return(get_buf_offset(buf) - offset);
}

/* Used by: all */
static int
_copy_node(nrt_nodeinfo_t *dest, nrt_nodeinfo_t *src)
{
	int i, j;
	nrt_adapter_t *sa = NULL;
	nrt_adapter_t *da = NULL;

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
			NRT_MAX_DEVICENAME_SIZE);
		da->adapter_type = sa->adapter_type;
		for (j = 0; j < MAX_SPIGOTS; j++) {
			da->lid[j] = sa->lid[j];
			da->network_id[j] = sa->network_id[j];
		}
		da->window_count = sa->window_count;
		da->window_list = (nrt_window_t *)xmalloc(sizeof(nrt_window_t) *
				  da->window_count);
		for (j = 0; j < da->window_count; j++)
			da->window_list[j] = sa->window_list[j];
	}

	return SLURM_SUCCESS;
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
static nrt_nodeinfo_t *
_find_node(nrt_libstate_t *lp, char *name)
{
	int i;
	nrt_nodeinfo_t *n;

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

/* Add the hash entry for a newly created nrt_nodeinfo_t
 */
static void
_hash_add_nodeinfo(nrt_libstate_t *state, nrt_nodeinfo_t *node)
{
	int index;

	assert(state);
	assert(state->hash_table);
	assert(state->hash_max >= state->node_count);
	if (!strlen(node->name))
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
_hash_rebuild(nrt_libstate_t *state)
{
	int i;

	assert(state);

	if (state->hash_table)
		xfree(state->hash_table);
	if (state->node_count > state->hash_max || state->hash_max == 0)
		state->hash_max += NRT_HASHCOUNT;
	state->hash_table = (nrt_nodeinfo_t **)
			    xmalloc(sizeof(nrt_nodeinfo_t *) * state->hash_max);
	memset(state->hash_table, 0,
	       sizeof(nrt_nodeinfo_t *) * state->hash_max);
	for (i = 0; i < state->node_count; i++)
		_hash_add_nodeinfo(state, &(state->node_list[i]));
}

/* If the node is already in the node list then simply return
 * a pointer to it, otherwise dynamically allocate memory to the
 * node list if necessary.
 *
 * Used by: slurmctld
 */
static nrt_nodeinfo_t *
_alloc_node(nrt_libstate_t *lp, char *name)
{
	nrt_nodeinfo_t *n = NULL;
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
		new_bufsize = lp->node_max * sizeof(nrt_nodeinfo_t);
		if (lp->node_list == NULL) {
			lp->node_list = (nrt_nodeinfo_t *)xmalloc(new_bufsize);
		} else {
			lp->node_list = (nrt_nodeinfo_t *)xrealloc(lp->node_list,
								   new_bufsize);
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
	n->adapter_list = (nrt_adapter_t *) xmalloc(NRT_MAXADAPTERS *
			  sizeof(nrt_adapter_t));

	if (name != NULL) {
		strncpy(n->name, name, NRT_HOSTLEN);
		if (need_hash_rebuild || lp->node_count > lp->hash_max)
			_hash_rebuild(lp);
		else
			_hash_add_nodeinfo(lp, n);
	}

	return n;
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
	uint64_t dummy64;
	uint32_t dummy32;
	uint16_t dummy16;
	char *dummyptr;
	int i, j;

	safe_unpack32(&adapter_count, buf);
	for (i = 0; i < adapter_count; i++) {
		/* no copy, just advances buf counters */
		safe_unpackmem_ptr(&dummyptr, &dummy32, buf);
		if (dummy32 != NRT_MAX_DEVICENAME_SIZE)
			goto unpack_error;
		safe_unpack16(&dummy16, buf);
		for (j = 0; j < MAX_SPIGOTS; j++) {
			safe_unpack16(&dummy16, buf);
			safe_unpack64(&dummy64, buf);
		}
		safe_unpack16(&window_count, buf);
		for (j = 0; j < window_count; j++) {
			safe_unpack16(&dummy16, buf);
			safe_unpack32(&dummy32, buf);
			safe_unpack16(&dummy16, buf);
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
_unpack_nodeinfo(nrt_nodeinfo_t *n, Buf buf, bool believe_window_status)
{
	int i, j;
	nrt_adapter_t *tmp_a = NULL;
	nrt_window_t *tmp_w = NULL;
	uint32_t size;
	nrt_nodeinfo_t *tmp_n = NULL;
	char *name_ptr, name[NRT_HOSTLEN];
	uint32_t magic;

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

	/* Update global libstate with this nodes' info.
	 */
	tmp_n = _alloc_node(nrt_state, name);
	if (tmp_n == NULL)
		return SLURM_ERROR;
	tmp_n->magic = magic;
	safe_unpack32(&tmp_n->adapter_count, buf);
	for (i = 0; i < tmp_n->adapter_count; i++) {
		tmp_a = tmp_n->adapter_list + i;
		safe_unpackmem_ptr(&name_ptr, &size, buf);
		if (size != NRT_MAX_DEVICENAME_SIZE)
			goto unpack_error;
		memcpy(tmp_a->adapter_name, name_ptr, size);
		safe_unpack16(&tmp_a->adapter_type, buf);
		for (j = 0; j < MAX_SPIGOTS; j++) {
			safe_unpack16(&tmp_a->lid[j], buf);
			safe_unpack64(&tmp_a->network_id[j], buf);
		}
		safe_unpack16(&tmp_a->window_count, buf);
		tmp_w = (nrt_window_t *) xmalloc(sizeof(nrt_window_t) *
			tmp_a->window_count);
		for (j = 0; j < tmp_a->window_count; j++) {
			safe_unpack16(&tmp_w[j].window_id, buf);
			safe_unpack32(&tmp_w[j].state, buf);
			safe_unpack16(&tmp_w[j].job_key, buf);
			if (!believe_window_status) {
				tmp_w[j].state = NRT_WIN_AVAILABLE;
				tmp_w[j].job_key = 0;
			}
		}
		tmp_a->window_list = tmp_w;
		tmp_w = NULL;	/* don't free on unpack error of next adapter */
	}

copy_node:
	/* Only copy the node_info structure if the caller wants it */
	if ((n != NULL) && (_copy_node(n, tmp_n) != SLURM_SUCCESS))
		return SLURM_ERROR;

#if NRT_DEBUG
	info("_unpack_nodeinfo");
	_print_nodeinfo(tmp_n);
#endif

	return SLURM_SUCCESS;

unpack_error:
	xfree(tmp_w);
	slurm_seterrno_ret(EUNPACK);
}

/* Unpack nodeinfo and update persistent libstate.
 *
 * Used by: slurmctld
 */
extern int
nrt_unpack_nodeinfo(nrt_nodeinfo_t *n, Buf buf)
{
	int rc;

	_lock();
	rc = _unpack_nodeinfo(n, buf, false);
	_unlock();
	return rc;
}


/* Used by: slurmd, slurmctld */
extern void
nrt_free_nodeinfo(nrt_nodeinfo_t *n, bool ptr_into_array)
{
	nrt_adapter_t *adapter;
	int i;

	if (!n)
		return;

	assert(n->magic == NRT_NODEINFO_MAGIC);

#if NRT_DEBUG
	info("_unpack_nodeinfo");
	_print_nodeinfo(n);
#endif
	if (n->adapter_list) {
		adapter = n->adapter_list;
		for (i = 0; i < n->adapter_count; i++) {
			xfree(adapter[i].window_list);
		}
		xfree(n->adapter_list);
	}
	if (!ptr_into_array)
		xfree(n);
}

/* Assign a unique key to each job.  The key is used later to
 * gain access to the network table loaded on each node of a job.
 *
 * NRT documentation states that the job key must be greater
 * than 0 and less than 0xFFF0.
 *
 * Used by: slurmctld
 */
static uint16_t
_next_key(void)
{
	uint16_t key;

	assert(nrt_state);

	_lock();
	key = nrt_state->key_index % 0xFFF0;
	if (key == 0)
		key++;
	nrt_state->key_index = key + 1;
	_unlock();

	return key;
}

/* FIXME - this could be a little smarter than walking the whole list each time */
static nrt_window_t *
_find_free_window(nrt_adapter_t *adapter) {
	int i;
	nrt_window_t *window;

	for (i = NRT_MIN_WIN; i < adapter->window_count; i++) {
		window = &adapter->window_list[i];
		if (window->state == NRT_WIN_AVAILABLE)
			return window;
	}

	return (nrt_window_t *) NULL;
}


static nrt_window_t *
_find_window(nrt_adapter_t *adapter, uint16_t window_id) {
	int i;
	nrt_window_t *window;

	for (i = NRT_MIN_WIN; i < adapter->window_count; i++) {
		window = &adapter->window_list[i];
		if (window->window_id == window_id)
			return window;
	}

	debug3("Unable to _find_window %hu on adapter %s",
	       window_id, adapter->adapter_name);
	return (nrt_window_t *) NULL;
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
		      char *hostname, int task_id, uint16_t job_key)
{
	nrt_nodeinfo_t *node;
	nrt_adapter_t *adapter;
	nrt_window_t *window;
	int i;

	assert(tableinfo);
	assert(hostname);

	node = _find_node(nrt_state, hostname);
	if (node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}

	/* Reserve a window on each adapter for this task */
	for (i = 0; i < adapter_cnt; i++) {
		adapter = &node->adapter_list[i];
		window = _find_free_window(adapter);
		if (window == NULL) {
			error("No free windows on node %s adapter %s",
			      node->name, adapter->adapter_name);
			return SLURM_ERROR;
		}
		window->state = NRT_WIN_UNAVAILABLE;
		window->job_key = job_key;

/* FIXME: table contains a union, it could contain either IB or HPCE data */
		if (adapter->adapter_type == RSCT_DEVTYPE_INFINIBAND) {
			nrt_creator_ib_per_task_input_t *ib_table;
			ib_table = &tableinfo[i].table[task_id].ib_per_task;
			ib_table->task_id = task_id;
			ib_table->base_lid = adapter->lid[0];
			ib_table->win_id = window->window_id;
		} else {
			fatal("Missing support for adapter type %hu",
			      adapter->adapter_type);
		}

		strncpy(tableinfo[i].adapter_name, adapter->adapter_name,
			NRT_MAX_DEVICENAME_SIZE);
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
			char *hostname, int task_id, uint16_t job_key)
{
	nrt_nodeinfo_t *node;
	nrt_adapter_t *adapter = NULL;
	nrt_window_t *window;
	nrt_creator_per_task_input_t *table;
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

	/* Reserve a window on the adapter for this task */
	window = _find_free_window(adapter);
	if (window == NULL) {
		error("No free windows on node %s adapter %s",
		      node->name, adapter->adapter_name);
		return SLURM_ERROR;
	}
	window->state = NRT_WIN_UNAVAILABLE;
	window->job_key = job_key;

	table = &tableinfo[0].table[task_id];
	if (adapter->adapter_type == RSCT_DEVTYPE_INFINIBAND) {
/* FIXME: table contains a union, it could contain either IB or HPCE data */
		nrt_creator_ib_per_task_input_t *ib_tbl_ptr;
		ib_tbl_ptr = &table->ib_per_task;
		ib_tbl_ptr->task_id = task_id;
		ib_tbl_ptr->base_lid = adapter->lid[0];
		ib_tbl_ptr->win_id = window->window_id;
	} else {
		fatal("_allocate_window_single: lack HPCE code");
	}

	strncpy(tableinfo[0].adapter_name, adapter_name,
		NRT_MAX_DEVICENAME_SIZE);

	return SLURM_SUCCESS;
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
	nrt_nodeinfo_t *node = NULL;
	nrt_adapter_t *adapter = NULL;
	nrt_window_t *window = NULL;
	nrt_creator_per_task_input_t *table = NULL;
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
		table = &tableinfo[i].table[task_id];
		if (table == NULL) {
			error("tableinfo[%d].table[%d] is NULL", i, task_id);
			return SLURM_ERROR;
		}

		adapter_found = false;
		/* Find the adapter that matches the one in tableinfo */
		for (j = 0; j < node->adapter_count; j++) {
			adapter = &node->adapter_list[j];
			if (strcasecmp(adapter->adapter_name,
				       tableinfo[i].adapter_name))
				continue;
/* FIXME: table contains a union, it could contain either IB or HPCE data */
			if (adapter->adapter_type == RSCT_DEVTYPE_INFINIBAND) {
				nrt_creator_ib_per_task_input_t *ib_tbl_ptr;
				ib_tbl_ptr = &table->ib_per_task;
				if (adapter->lid[0] == ib_tbl_ptr->base_lid) {
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
			} else {
				fatal("_window_state_set: Missing support for "
				      "adapter type %hu",
				      adapter->adapter_type);

			}
		}
		if (!adapter_found) {
			error("Did not find adapter %s with lid %hu ",
			      adapter->adapter_name, adapter->lid[0]);
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


/* Find all of the windows used by this job step and set their
 * status to "state".
 *
 * Used by: slurmctld
 */
static int
_job_step_window_state(nrt_jobinfo_t *jp, hostlist_t hl, win_state_t state)
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

	if ((jp->tables_per_task == 0)
	    || (!jp->tableinfo)
	    || (jp->tableinfo[0].table_length == 0))
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

/*
 * For one node, free all of the windows belonging to a particular
 * job step (as identified by the job_key).
 */
static void inline
_free_windows_by_job_key(uint16_t job_key, char *nodename)
{
	nrt_nodeinfo_t *node;
	nrt_adapter_t *adapter;
	nrt_window_t *window;
	int i, j;

	/* debug3("_free_windows_by_job_key(%hu, %s)", job_key, nodename); */
	if ((node = _find_node(nrt_state, nodename)) == NULL)
		return;

	if (node->adapter_list == NULL) {
		error("_free_windows_by_job_key, "
		      "adapter_list NULL for node %s", nodename);
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

/* Find all of the windows used by job step "jp" on the hosts
 * designated in hostlist "hl" and mark their state NRT_WIN_AVAILABLE.
 *
 * Used by: slurmctld
 */
extern int
nrt_job_step_complete(nrt_jobinfo_t *jp, hostlist_t hl)
{
	hostlist_t uniq_hl;
	hostlist_iterator_t hi;
	char *nodename;

	xassert(!hostlist_is_empty(hl));
	xassert(jp);
	xassert(jp->magic == NRT_JOBINFO_MAGIC);

	if ((jp == NULL) || (hostlist_is_empty(hl)))
		return SLURM_ERROR;

	if ((jp->tables_per_task == 0)
	    || !jp->tableinfo
	    || (jp->tableinfo[0].table_length == 0))
		return SLURM_SUCCESS;

	/* The hl hostlist may contain duplicate nodenames (poe -hostfile
	 * triggers duplicates in the hostlist).  Since there
	 * is no reason to call _free_windows_by_job_key more than once
	 * per nodename, we create a new unique hostlist.
	 */
	uniq_hl = hostlist_copy(hl);
	hostlist_uniq(uniq_hl);
	hi = hostlist_iterator_create(uniq_hl);

	_lock();
	if (nrt_state != NULL) {
		while ((nodename = hostlist_next(hi)) != NULL) {
			_free_windows_by_job_key(jp->job_key, nodename);
			free(nodename);
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
nrt_job_step_allocated(nrt_jobinfo_t *jp, hostlist_t hl)
{
	return _job_step_window_state(jp, hl, NRT_WIN_UNAVAILABLE);
}



/* Setup everything for the job.  Assign tasks across
 * nodes based on the hostlist given and create the network table used
 * on all nodes of the job.
 *
 * Used by: slurmctld
 */
extern int
nrt_build_jobinfo(nrt_jobinfo_t *jp, hostlist_t hl, int nprocs,
		  bool sn_all, char *adapter_name, int bulk_xfer)
{
	int nnodes;
	hostlist_iterator_t hi;
	char *host = NULL;
	int proc_cnt = 0;
	int i, j;
	nrt_nodeinfo_t *node;
	int rc;
	int task_cnt;
	int full_node_cnt;
	int min_procs_per_node;
	int max_procs_per_node;

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);
	assert(!hostlist_is_empty(hl));

	if (nprocs <= 0)
		slurm_seterrno_ret(EINVAL);

	jp->bulk_xfer = (uint8_t) bulk_xfer;
	jp->job_key = _next_key();
	snprintf(jp->job_desc, JOB_DESC_LEN,
		 "slurm switch/NRT driver key=%d", jp->job_key);

	hi = hostlist_iterator_create(hl);

	if (sn_all) {
		/*
		 * Peek at the first host to figure out tables_per_task.
		 * This driver assumes that all nodes have the same number
		 * of adapters per node.  Bad Things will happen if this
		 * assumption is incorrect.
		 */
		host = hostlist_next(hi);
		_lock();
		node = _find_node(nrt_state, host);
		jp->tables_per_task = node ? node->adapter_count : 0;
		_unlock();
		if (host != NULL)
			free(host);
		hostlist_iterator_reset(hi);
	} else {
		jp->tables_per_task = 1;
	}

	/* Allocate memory for each nrt_tableinfo_t */
	jp->tableinfo = (nrt_tableinfo_t *) xmalloc(jp->tables_per_task
						    * sizeof(nrt_tableinfo_t));
	for (i = 0; i < jp->tables_per_task; i++) {
		jp->tableinfo[i].table_length = nprocs;
		jp->tableinfo[i].table = (nrt_creator_per_task_input_t *)
					 xmalloc(nprocs *
					 sizeof(nrt_creator_per_task_input_t));
	}

	debug("Allocating windows");
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
			if (adapter_name == NULL) {
				rc = _allocate_windows_all(jp->tables_per_task,
							   jp->tableinfo,
							   host, proc_cnt,
							   jp->job_key);
			} else {
				rc = _allocate_window_single(adapter_name,
							     jp->tableinfo,
							     host, proc_cnt,
							     jp->job_key);
			}
			if (rc != SLURM_SUCCESS) {
				_unlock();
				goto fail;
			}
			proc_cnt++;
		}
		free(host);
	}
	_unlock();


#if NRT_DEBUG
	info("nrt_build_jobinfo");
	_print_table(jp->tableinfo[i].table, jp->tableinfo[i].table_length);
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
_pack_tableinfo(nrt_tableinfo_t *tableinfo, Buf buf)
{
	int i;

	pack32(tableinfo->table_length, buf);
/* FIXME: table contains a union, it could contain either IB or HPCE data */
	if (tableinfo->adapter_type == RSCT_DEVTYPE_INFINIBAND) {
		nrt_creator_ib_per_task_input_t *ib_tbl_ptr;
		for (i = 0; i < tableinfo->table_length; i++) {
			ib_tbl_ptr = &tableinfo[i].table[i].ib_per_task;
			pack16(ib_tbl_ptr->task_id, buf);
			pack16(ib_tbl_ptr->base_lid, buf);
			pack16(ib_tbl_ptr->win_id, buf);
		}
	} else {
		fatal("_pack_tableinfo: Missing support for adapter "
		      "type %hu", tableinfo->adapter_type);
	}
	packmem(tableinfo->adapter_name, NRT_MAX_DEVICENAME_SIZE, buf);
}

/* Used by: all */
extern int
nrt_pack_jobinfo(nrt_jobinfo_t *j, Buf buf)
{
	int i;

	assert(j);
	assert(j->magic == NRT_JOBINFO_MAGIC);
	assert(buf);

	pack32(j->magic, buf);
	pack16(j->job_key, buf);
	packmem(j->job_desc, JOB_DESC_LEN, buf);
	pack8(j->bulk_xfer, buf);
	pack16(j->tables_per_task, buf);
	for (i = 0; i < j->tables_per_task; i++) {
		_pack_tableinfo(&j->tableinfo[i], buf);
	}

	return SLURM_SUCCESS;
}

/* return 0 on success, -1 on failure */
static int
_unpack_tableinfo(nrt_tableinfo_t *tableinfo, Buf buf)
{
	uint32_t size;
	char *name_ptr;
	int i;

	safe_unpack32(&tableinfo->table_length, buf);
/* FIXME: table contains a union, it could contain either IB or HPCE data */
	if (tableinfo->adapter_type == RSCT_DEVTYPE_INFINIBAND) {
		nrt_creator_ib_per_task_input_t *ib_tbl_ptr;
		tableinfo->table = (nrt_creator_per_task_input_t *)
				   xmalloc(tableinfo->table_length *
				   sizeof(nrt_creator_per_task_input_t));
		for (i = 0; i < tableinfo->table_length; i++) {
			ib_tbl_ptr = &tableinfo[i].table[i].ib_per_task;
			safe_unpack16(&ib_tbl_ptr->task_id, buf);
			safe_unpack16(&ib_tbl_ptr->base_lid, buf);
			safe_unpack16(&ib_tbl_ptr->win_id, buf);
		}
	} else {
		fatal("_unpack_tableinfo: Missing support for adapter "
		      "type %hu", tableinfo->adapter_type);
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
int
nrt_unpack_jobinfo(nrt_jobinfo_t *j, Buf buf)
{
	uint32_t size;
	int i;

	assert(j);
	assert(j->magic == NRT_JOBINFO_MAGIC);
	assert(buf);

	safe_unpack32(&j->magic, buf);
	assert(j->magic == NRT_JOBINFO_MAGIC);
	safe_unpack16(&j->job_key, buf);
	safe_unpackmem(j->job_desc, &size, buf);
	if (size != JOB_DESC_LEN)
		goto unpack_error;
	safe_unpack8(&j->bulk_xfer, buf);
	safe_unpack16(&j->tables_per_task, buf);

	j->tableinfo = (nrt_tableinfo_t *) xmalloc(j->tables_per_task
						   * sizeof(nrt_tableinfo_t));
	for (i = 0; i < j->tables_per_task; i++) {
		if (_unpack_tableinfo(&j->tableinfo[i], buf) != 0)
			goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	error("nrt_unpack_jobinfo error");
	if (j->tableinfo) {
		for (i = 0; i < j->tables_per_task; i++)
			xfree(j->tableinfo[i].table);
		xfree(j->tableinfo);
	}
	slurm_seterrno_ret(EUNPACK);
	return SLURM_ERROR;
}

/* Used by: all */
extern nrt_jobinfo_t *
nrt_copy_jobinfo(nrt_jobinfo_t *job)
{
	nrt_jobinfo_t *new;
	int i, k;

	assert(job);
	assert(job->magic == NRT_JOBINFO_MAGIC);

	if (nrt_alloc_jobinfo(&new)) {
		error("Allocating new jobinfo");
		slurm_seterrno(ENOMEM);
		return NULL;
	}
	memcpy(new, job, sizeof(nrt_jobinfo_t));
	/* table will be empty (and table_size == 0) when the network string
	 * from poe does not contain "us".
	 * (See man poe: -euilib or MP_EUILIB)
	 */
	if (job->tables_per_task > 0) {
		/* Allocate memory for each nrt_tableinfo_t */
		new->tableinfo = (nrt_tableinfo_t *)xmalloc(
				 job->tables_per_task * sizeof(nrt_tableinfo_t));
		memcpy(new->tableinfo, job->tableinfo,
		       sizeof(nrt_tableinfo_t) * job->tables_per_task);

		for (i = 0; i < job->tables_per_task; i++) {
			new->tableinfo[i].table =
				(nrt_creator_per_task_input_t *)
				xmalloc(job->tableinfo[i].table_length *
				sizeof(nrt_creator_per_task_input_t));
			for (k = 0; k < new->tableinfo[i].table_length; k++) {
				memcpy(&new->tableinfo[i].table[k],
				       &job->tableinfo[i].table[k],
				       sizeof(nrt_tableinfo_t));
			}
		}
	}

	return new;
}

/* Used by: all */
extern void
nrt_free_jobinfo(nrt_jobinfo_t *jp)
{
	int i;
	nrt_tableinfo_t *tableinfo;

	if (!jp) {
		return;
	}

	if (jp->magic != NRT_JOBINFO_MAGIC) {
		error("jp is not a switch/nrt nrt_jobinfo_t");
		return;
	}

	jp->magic = 0;
	if (jp->tables_per_task > 0 && jp->tableinfo != NULL) {
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

/* Return data to code for whom jobinfo is an opaque type.
 *
 * Used by: all
 */
extern int
nrt_get_jobinfo(nrt_jobinfo_t *jp, int key, void *data)
{
	nrt_tableinfo_t **tableinfo = (nrt_tableinfo_t **)data;
	int *tables_per = (int *)data;
	int *job_key = (int *)data;

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);

	switch(key) {
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
_wait_for_window_unloaded(char *adapter_name, int adapter_type,
			  nrt_window_id_t window_id, int retry)
{
	int err, i, j;
	nrt_cmd_status_adapter_t status_adapter;
	nrt_status_t *status = NULL;

	status_adapter.adapter_name = adapter_name;
	status_adapter.adapter_type = adapter_type;
	for (i = 0; i < retry; i++) {
		if (i > 0)
			sleep(1);

		err = nrt_command(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
				  &status_adapter);
		if (err != NRT_SUCCESS) {
			error("nrt_status_adapter(%s, %d): %s", adapter_name,
			      (int) adapter_type, nrt_err_str(err));
			return SLURM_ERROR;
		}
#if NRT_DEBUG
		info("_wait_for_window_unloaded");
		_print_adapter_status(&status_adapter);
#endif
		for (j = 0; j < *status_adapter.window_count; j++) {
			status = status_adapter.status_array[j];
			if (status->window_id == window_id)
				break;
		}
		if (j >= *status_adapter.window_count) {
			error("nrt_status_adapter(%s, %hu), window %hu not "
			      "found",
			      adapter_name, (int)adapter_type, window_id);
			return SLURM_ERROR;
		}
		if (status->state == NRT_WIN_AVAILABLE)
			return SLURM_SUCCESS;
		debug2("nrt_status_adapter(%s, %d), window %u state %s",
		       adapter_name, (int) adapter_type, window_id,
		       _win_state_str(status->state));
	}

	return SLURM_ERROR;
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
	nrt_window_id_t window_id;

	for (i = 0; i < tableinfo->table_length; i++) {
		if (tableinfo->adapter_type == NRT_IB) {
			nrt_ib_task_info_t *ib_tbl_ptr;
			ib_tbl_ptr = (nrt_ib_task_info_t *) tableinfo->table;
			ib_tbl_ptr += i;
			window_id = ib_tbl_ptr->win_id;
		} else if (adapter_type_code == NRT_HFI) {
			nrt_hfi_task_info_t *hfi_tbl_ptr;
			hfi_tbl_ptr = (nrt_hfi_task_info_t *) tableinfo->table;
			hfi_tbl_ptr += i;
			window_id = hfi_tbl_ptr->win_id;
		} else {
			fatal("_wait_for_all_windows: Missing support for "
			      "adapter type %hu", tableinfo->adapter_type);
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

static int
_check_rdma_job_count(char *adapter_name, uint16_t adapter_type)
{
	uint16_t job_count;
	uint16_t *job_keys;
	int err, i;

	err = nrt_rdma_jobs(NRT_VERSION, adapter_name, adapter_type,
			    &job_count, &job_keys);
	if (err != NRT_SUCCESS) {
		error("nrt_rdma_jobs(): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}
#if NRT_DEBUG
	info("_check_rdma_job_count: nrt_rdma_jobs:");
	info("adapter_name:%s adapter_type:%hu", adapter_name, adapter_type);
	for (i = 0; i < job_count; i++)
		info("  job_keys[%d]:%hu", i, job_keys[i]);
#endif
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
nrt_load_table(nrt_jobinfo_t *jp, int uid, int pid)
{
	int i;
	int err;
	char *adapter_name;
	uint64_t network_id;
	uint bulk_xfer_resources = 0;
	int rc;
	nrt_cmd_load_table_t load_table;
	nrt_table_info_t table_info;

#if NRT_DEBUG
	int j;

	info("nrt_load_table");
#endif
	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);

	for (i = 0; i < jp->tables_per_task; i++) {
#if NRT_DEBUG
		_print_table(jp->tableinfo[i].table,
			     jp->tableinfo[i].table_length);
		_print_jobinfo(jp);
#endif
		adapter_name = jp->tableinfo[i].adapter_name;
		network_id = _get_network_id_from_adapter(adapter_name);

		rc = _wait_for_all_windows(&jp->tableinfo[i]);
		if (rc != SLURM_SUCCESS)
			return rc;

		if (adapter_name == NULL)
			continue;
		if (jp->bulk_xfer && (i == 0)) {
			rc = _check_rdma_job_count(adapter_name,
						   adapter_type_code);
			if (rc != SLURM_SUCCESS)
				return rc;
		}
#if NRT_DEBUG
		info("attempting nrt_load_table:");
		info("adapter_name:%s adapter_type:%hu", adapter_name,
		     adapter_type_code);
		info("  network_id:%"PRIu64" uid:%u pid:%u", network_id,
		     (uint32_t)uid, (uint32_t)pid);
		info("  job_key:%hd job_desc:%s", jp->job_key, jp->job_desc);
		info("  bulk_xfer:%u bulk_xfer_res:%u", jp->bulk_xfer,
		     bulk_xfer_resources);
		for (j = 0; j < jp->tableinfo[i].table_length; j++) {
			if (adapter_type_code == NRT_IB) {
				nrt_ib_task_info_t *ib_tbl_ptr;
				ib_tbl_ptr = (nrt_ib_task_info_t *)
					     jp->tableinfo[i].table;
				ib_tbl_ptr += j;
				info("  task_id[%d]:%hu", j,
				     ib_tbl_ptr->task_id);
				info("  win_id[%d]:%hu", j,
				     ib_tbl_ptr->win_id);
				info("  node_number[%d]:%u", j,
				     ib_tbl_ptr->node_number);
				info("  device_name[%d]:%s", j,
				     ib_tbl_ptr->device_name);
				info("  base_lid[%d]:%hu", j,
				     ib_tbl_ptr->base_lid);
				info("  port_id[%d]:%hu", j,
				     ib_tbl_ptr->port_id);
				info("  lmc[%d]:%hu", j, ib_tbl_ptr->lmc);
				info("  port_status[%d]:%hu", j,
				     ib_tbl_ptr->port_status);
			} else if (adapter_type_code == NRT_HFI) {
				nrt_hfi_task_info_t *hfi_tbl_ptr;
				hfi_tbl_ptr = (nrt_hfi_task_info_t *)
					      jp->tableinfo[i].table;
				hfi_tbl_ptr += j;
				info("  task_id[%d]:%hu", j,
				     hfi_tbl_ptr->task_id);
				info("  lpar_id[%d]:%hu", j,
				     hfi_tbl_ptr->lpar_id);
				info("  lid[%d]:%hu", j,
				     hfi_tbl_ptr->lid);
				info("  win_id[%d]:%hu", j,
				     hfi_tbl_ptr->win_id);
			} else {
				fatal("nrt_load_table: invalid adapter type: "
				      "%hu", adapter_type_code);
			}
		}
#endif
/* FIXME: Ne need to set a bunch of these paramters appropriately */
#define TBD 0
		table_info.num_tasks = jp->tableinfo[i].table_length;
		table_info.job_key = jp->job_key;
		table_info.uid = uid;
		table_info.network_id = TBD;
		table_info.pid = pid;
		table_info.adapter_type = adapter_type_code;
		table_info.is_user_space = TBD;
		table_info.is_ipv4 = TBD;
		table_info.context_id = TBD;
		table_info.table_id = TBD;
		strncpy(table_info.job_name, "TBD", NRT_MAX_JOB_NAME_LEN);
		strncpy(table_info.protocol_name, "TBD", NRT_MAX_PROTO_NAME_LEN);
		table_info.use_bulk_transfer = jp->bulk_xfer;
		table_info.bulk_transfer_resources = TBD;
		table_info.immed_send_slots_per_win = TBD;
		table_info.num_cau_indexes = TBD;
		load_table.table_info = &table_info;
		load_table.per_task_input = jp->tableinfo[i].table;
		err = nrt_command(NRT_VERSION, NRT_CMD_LOAD_TABLE, &load_table);
		if (err != NRT_SUCCESS) {
			error("unable to load table: [%d] %s",
			      err, nrt_err_str(err));
			return SLURM_ERROR;
		}
	}
	umask(nrt_umask);

	return SLURM_SUCCESS;
}


/*
 * Try up to "retry" times to unload a window.
 */
static int
_unload_window(char *adapter_name, uint16_t adapter_type,
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
	}

	return SLURM_FAILURE;
}


/* Assumes that, on error, new switch state information will be
 * read from node.
 *
 * Used by: slurmd
 */
extern int
nrt_unload_table(nrt_jobinfo_t *jp)
{
	nrt_window_id_t window_id;
	int err, i, j, rc = SLURM_SUCCESS;
	int retry = 15;

	assert(jp);
	assert(jp->magic == NRT_JOBINFO_MAGIC);
	for (i = 0; i < jp->tables_per_task; i++) {
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
			fatal("nrt_unload_table: invalid adapter type: "
			      "%hu", adapter_type_code);
		}
		for (j = 0; j < jp->tableinfo[i].table_length; j++) {
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

static nrt_libstate_t *
_alloc_libstate(void)
{
	nrt_libstate_t *tmp;

	tmp = (nrt_libstate_t *) xmalloc(sizeof(nrt_libstate_t));
	tmp->magic = NRT_LIBSTATE_MAGIC;
	tmp->node_count = 0;
	tmp->node_max = 0;
	tmp->node_list = NULL;
	tmp->hash_max = 0;
	tmp->hash_table = NULL;
	tmp->key_index = 1;

	return tmp;
}

/* Allocate and initialize memory for the persistent libstate.
 *
 * Used by: slurmctld
 */
extern int
nrt_init(void)
{
	nrt_libstate_t *tmp;

	tmp = _alloc_libstate();
	if (!tmp)
		return SLURM_FAILURE;
	_lock();
	assert(!nrt_state);
	nrt_state = tmp;
	_unlock();

	return SLURM_SUCCESS;
}

static void
_free_libstate(nrt_libstate_t *lp)
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

extern int
nrt_fini(void)
{
	xfree(nrt_conf);
	return SLURM_SUCCESS;
}

/* Used by: slurmctld */
static int
_pack_libstate(nrt_libstate_t *lp, Buf buffer)
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
_unpack_libstate(nrt_libstate_t *lp, Buf buffer)
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
		error("Failed to recover switch state of all nodes (%d of %u)",
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
	struct nrt_nodeinfo *node;
	struct nrt_adapter *adapter;
	struct nrt_window *window;

	debug3("Clearing state on all windows in global NRT state");
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

extern int nrt_clear_node_state(void)
{
	int err, i, j, k, rc = SLURM_SUCCESS;
	nrt_cmd_query_adapter_types_t adapter_types;
	nrt_cmd_query_adapter_names_t adapter_names;
	nrt_cmd_status_adapter_t adapter_status;
	nrt_cmd_clean_window_t clean_window;

#if NRT_DEBUG
	info("nrt_clear_node_state: begin");
#endif
	err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_TYPES,
			  &adapter_types);
	if (err != NRT_SUCCESS) {
		error("nrt_command(adapter_types): %s", nrt_err_str(err));
		return SLURM_ERROR;
	}

	for (i = 0; i < adapter_types.num_adapter_types[0]; i++) {
#if NRT_DEBUG
		info("adapter_type[%d]: %u",
		     i, adapter_types.adapter_types[i]);
#endif
		adapter_names.adapter_type = adapter_types.adapter_types[i];
		err = nrt_command(NRT_VERSION, NRT_CMD_QUERY_ADAPTER_NAMES,
				  &adapter_names);
		if (err != NRT_SUCCESS) {
			error("nrt_command(adapter_names, %u): %s",
			      adapter_names.adapter_type, nrt_err_str(err));
			rc = SLURM_ERROR;
			continue;
		}
		for (j = 0; j < adapter_names.num_adapter_names[0]; j++) {
#if NRT_DEBUG
			info("adapter_names[%d]: %s",
			     j, adapter_names.adapter_names[j]);
#endif
			adapter_status.adapter_name = adapter_names.
						      adapter_names[j];
			adapter_status.adapter_type = adapter_names.
						      adapter_type;
			err = nrt_command(NRT_VERSION, NRT_CMD_STATUS_ADAPTER,
					  &adapter_status);
			if (err != NRT_SUCCESS) {
				error("nrt_command(clean_status, %s, %u): %s",
				      adapter_status.adapter_name,
				      adapter_status.adapter_type,
				      nrt_err_str(err));
				rc = SLURM_ERROR;
				continue;
			}

			for (k = 0; k < adapter_status.window_count[0]; k++) {
#if NRT_DEBUG
				info("window_id[%d]: %u", k,
				     adapter_status.status_array[k]->
				     window_id);
#endif
				clean_window.adapter_name = adapter_names.
							    adapter_names[i];
				clean_window.adapter_type = adapter_names.
							    adapter_type;
				clean_window.leave_inuse_or_kill = KILL;
				clean_window.window_id = adapter_status.
							 status_array[k]->
							 window_id;
				err = nrt_command(NRT_VERSION,
						  NRT_CMD_CLEAN_WINDOW,
						  &clean_window);
				if (err != NRT_SUCCESS) {
					error("nrt_command(clean_window, "
					      "%s, %u, %u): %s",
					      clean_window.adapter_name,
					      clean_window.adapter_type,
					      clean_window.window_id,
					      nrt_err_str(err));
					rc = SLURM_ERROR;
				}
			}
		}
	}
#if NRT_DEBUG
	info("nrt_clear_node_state: complete: %d", rc);
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
