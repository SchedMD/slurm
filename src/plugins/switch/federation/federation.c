/*****************************************************************************\
 *  federation.c - Library routines for initiating jobs on IBM Federation
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
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

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_LIBNTBL
# include <ntbl.h>
#else
# error "Must have libntbl to compile this module!"
#endif 

#include <sys/stat.h>
#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/common/read_config.h"
#include "src/plugins/switch/federation/federation.h"
#include "src/plugins/switch/federation/federation_keys.h"

/* 
 * Definitions local to this module
 */
#define FED_NODEINFO_MAGIC	0xc00cc00d
#define FED_JOBINFO_MAGIC	0xc00cc00e
#define FED_LIBSTATE_MAGIC	0xc00cc00f

#define FED_ADAPTERNAME_LEN 5
#define FED_HOSTLEN 20
#define FED_VERBOSE_PRINT 0
#define FED_NODECOUNT 128
#define FED_HASHCOUNT 128
#define FED_AUTO_WINMEM 0
#define FED_MAX_WIN 15
#define FED_MIN_WIN 0
#define FED_DEBUG 0

#define BUFSIZE 4096

char* fed_conf = NULL;
extern bool fed_need_state_save;

mode_t fed_umask;

/*
 * Data structures specific to Federation 
 *
 * We are going to some trouble to keep these defs private so slurm
 * hackers not interested in the interconnect details can just pass around
 * the opaque types.  All use of the data structure internals is local to this
 * module.
 */
 
typedef struct fed_window {
	uint16_t id;
	uint32_t status;
	uint16_t job_key;
} fed_window_t;
	
typedef struct fed_adapter {
	char name[FED_ADAPTERNAME_LEN];
	uint16_t lid;
	uint16_t network_id;
	uint32_t max_winmem;
	uint32_t min_winmem;
	uint32_t avail_mem;
	uint32_t window_count;
	fed_window_t *window_list;
} fed_adapter_t;

struct fed_nodeinfo {
	uint32_t magic;
	char name[FED_HOSTLEN];
	uint32_t adapter_count;
	fed_adapter_t *adapter_list;
	struct fed_nodeinfo *next;
};

struct fed_libstate {
	uint32_t magic;
	uint32_t node_count;
	uint32_t node_max;
	fed_nodeinfo_t *node_list;
	uint32_t hash_max;
	fed_nodeinfo_t **hash_table;
	uint16_t key_index;
};

struct fed_jobinfo {
	uint32_t magic;
	/* version from ntbl_version() */
	/* adapter from lid in table */
	/* network_id from lid in table */
	/* uid from getuid() */
	/* pid from getpid() */
	uint16_t job_key;
	char job_desc[DESCLEN];
	uint32_t window_memory;
	uint8_t bulk_xfer;  /* flag */
	uint16_t tables_per_task;
	fed_tableinfo_t *tableinfo;

	hostlist_t nodenames;
	int num_tasks;
};

typedef struct {
	int status_number;
	char *status_msg;
} fed_status_t;

typedef struct {
	char name[FED_ADAPTERNAME_LEN];
	uint16_t lid;
	uint16_t network_id;
} fed_cache_entry_t;

/* 
 * Globals
 */
fed_libstate_t *fed_state = NULL;
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

/* slurmd/slurmstepd global variables */
hostlist_t adapter_list;
static fed_cache_entry_t lid_cache[FED_MAXADAPTERS];


#define FED_STATUS_UNKNOWN 99
static fed_status_t fed_status_tab[]= {
	{0, "NTBL_SUCCESS"},
	{1, "NTBL_EINVAL"},
	{2, "NTBL_EPERM"},
	{3, "NTBL_EIOCTL"},
	{4, "NTBL_EADAPTER"},
	{5, "NTBL_ESYSTEM"},
	{6, "NTBL_EMEM"},
	{7, "NTBL_ELID"},
	{8, "NTBL_EIO"},
	{9, "NTBL_UNLOADED_STATE"},
	{10, "NTBL_LOADED_STATE"},
	{11, "NTBL_DISABLED_STATE"},
	{12, "NTBL_ACTIVE_STATE"},
	{13, "NTBL_BUSY_STATE"},
	{14, "NTBL_NO_RDMA_AVAIL"},
	{FED_STATUS_UNKNOWN, "UNKNOWN_RESULT_CODE"}
};

static void _hash_rebuild(fed_libstate_t *state);
static int _set_up_adapter(fed_adapter_t *fed_adapter, char *adapter_name);
static int _parse_fed_file(hostlist_t *adapter_list);
static void _init_adapter_cache(void);
static int _fill_in_adapter_cache(void);

/* The _lock() and _unlock() functions are used to lock/unlock a 
 * global mutex.  Used to serialize access to the global library 
 * state variable fed_state. 
 */
static void
_lock(void)
{
	int err = 1;
	
	while(err) {
		err = pthread_mutex_lock(&global_lock);
	}
}

static void
_unlock(void)
{
	int err = 1;
	
	while(err) {
		err = pthread_mutex_unlock(&global_lock);
	}
}

int
fed_slurmctld_init(void)
{
	return SLURM_SUCCESS;
}

int
fed_slurmd_init(void)
{
	/*
	 * This is a work-around for the ntbl_* functions calling umask(0)
	 */
	fed_umask = umask(0077);
	umask(fed_umask);

	/*_init_adapter_cache();*/

	adapter_list = hostlist_create(NULL);
	if (_parse_fed_file(&adapter_list) != SLURM_SUCCESS)
		return SLURM_FAILURE;
	assert(hostlist_count(adapter_list) <= FED_MAXADAPTERS);
	return SLURM_SUCCESS;
}

int
fed_slurmd_step_init(void)
{
	/*
	 * This is a work-around for the ntbl_* functions calling umask(0)
	 */
	fed_umask = umask(0077);
	umask(fed_umask);

	_init_adapter_cache();

	adapter_list = hostlist_create(NULL);
	if (_parse_fed_file(&adapter_list) != SLURM_SUCCESS)
		return SLURM_FAILURE;
	assert(hostlist_count(adapter_list) <= FED_MAXADAPTERS);

	_fill_in_adapter_cache();

	return SLURM_SUCCESS;
}

static char *
_lookup_fed_status_tab(int status)
{
	char *res = NULL;
	int i;
	
	for(i = 0; i < sizeof(fed_status_tab) / sizeof(fed_status_t); i++) {
		if(fed_status_tab[i].status_number == status) {
			res = fed_status_tab[i].status_msg;
			break;
		}
	}
	
	if(!res)
		res = fed_status_tab[FED_STATUS_UNKNOWN].status_msg;
		
	return res;
}

/* Used by: slurmd, slurmctld */
void fed_print_jobinfo(FILE *fp, fed_jobinfo_t *jobinfo)
{
	assert(jobinfo->magic == FED_JOBINFO_MAGIC);
	
	/* stubbed out */
}

/* Used by: slurmd, slurmctld */
char *fed_sprint_jobinfo(fed_jobinfo_t *j, char *buf,
    size_t size)
{
	int count;
	char *tmp = buf;
	int remaining = size;
	
	assert(buf);
	assert(j);
	assert(j->magic == FED_JOBINFO_MAGIC);

	count = snprintf(tmp, remaining,
		"--Begin Jobinfo--\n"
		"  job_key: %u\n"
		"  job_desc: %s\n"
		"  window_memory: %u\n"
		"  table_size: %u\n"
		"--End Jobinfo--\n",
		j->job_key,
		j->job_desc,
		j->window_memory,
		j->tables_per_task);
	if(count < 0)
		return buf;
	remaining -= count;
	tmp += count;
	if(remaining < 1)
		return buf;

	return buf;
}

/* The lid caching functions were created to avoid unnecessary
 * function calls each time we need to load network tables on a node.
 * _init_cache() simply initializes the cache to sane values and 
 * needs to be called before any other cache functions are called.
 *
 * Used by: slurmd/slurmstepd
 */
static void
_init_adapter_cache(void)
{
	int i;
	
	for(i = 0; i < FED_MAXADAPTERS; i++) {
		lid_cache[i].name[0] = 0;
		lid_cache[i].lid = -1;
		lid_cache[i].network_id = -1;
	}
}

/* Use ntbl_adapter_resources to cache information about local adapters.
 *
 * Used by: slurmstepd
 */
static int
_fill_in_adapter_cache(void)
{
	hostlist_iterator_t adapters;
	char *adapter_name = NULL;
	ADAPTER_RESOURCES res;
	int num;
	int rc;
	int i;
	
	adapters = hostlist_iterator_create(adapter_list);
	for (i = 0; (adapter_name = hostlist_next(adapters)); i++) {
		rc = ntbl_adapter_resources(NTBL_VERSION, adapter_name, &res);
		if (rc != NTBL_SUCCESS)
			return SLURM_ERROR;

		num = adapter_name[3] - (int)'0';
		assert(num < FED_MAXADAPTERS);
		lid_cache[num].lid = res.lid;
		lid_cache[num].network_id = res.network_id;
		strncpy(lid_cache[num].name, adapter_name, FED_ADAPTERNAME_LEN);

		free(res.window_list);
		free(adapter_name);
	}
	hostlist_iterator_destroy(adapters);
	umask(fed_umask);

	return SLURM_SUCCESS;
}


/* Cache the lid and network_id of a given adapter.  Ex:  sni0 with lid 10
 * gets cached in array index 0 with a lid = 10 and a name = sni0.
 *
 * Used by: slurmd
 */
static void
_cache_lid(fed_adapter_t *ap)
{
	assert(ap);
	
	int adapter_num = ap->name[3] - (int) '0';

	lid_cache[adapter_num].lid = ap->lid;
	lid_cache[adapter_num].network_id = ap->network_id;
	strncpy(lid_cache[adapter_num].name, ap->name, FED_ADAPTERNAME_LEN);		
}


/* Check lid cache for an adapter name and return the network id. 
 *
 * Used by: slurmd
 */
static uint16_t
_get_network_id_from_adapter(char *adapter_name)
{
	int i;

	for (i = 0; i < FED_MAXADAPTERS; i++) {
		if (!strncmp(adapter_name, lid_cache[i].name,
			     FED_ADAPTERNAME_LEN))
			return lid_cache[i].network_id;
	}

        return (uint16_t) -1;
}


/* Check lid cache for an adapter name and return the lid. 
 *
 * Used by: slurmd
 */
static uint16_t
_get_lid_from_adapter(char *adapter_name)
{
	int i;

	for (i = 0; i < FED_MAXADAPTERS; i++) {
		if (!strncmp(adapter_name, lid_cache[i].name,
			     FED_ADAPTERNAME_LEN))
			return lid_cache[i].lid;
	}

        return (uint16_t) -1;
}


static int _set_up_adapter(fed_adapter_t *fed_adapter, char *adapter_name)
{
	ADAPTER_RESOURCES res;
	struct NTBL_STATUS *status = NULL;
	struct NTBL_STATUS *old = NULL;
	fed_window_t *tmp_winlist = NULL;
	int win_count = 0, i;
	int error_code;

	info("adapter_name is %s", adapter_name);
	
	error_code = ntbl_adapter_resources(NTBL_VERSION, 
					    adapter_name, 
					    &res);
	if(error_code != NTBL_SUCCESS) 
		return SLURM_ERROR;
	strncpy(fed_adapter->name, 
		adapter_name, 
		FED_ADAPTERNAME_LEN);
	fed_adapter->lid = res.lid;
	fed_adapter->network_id = res.network_id;
	/* FUTURE:  check that we don't lose information when converting
	 * from 64 to 32 bit unsigned ints in the next three assignments.
	 */
	fed_adapter->max_winmem = res.max_window_memory;
	fed_adapter->min_winmem = res.min_window_memory;
	fed_adapter->avail_mem = res.avail_adapter_memory;
	fed_adapter->window_count = res.window_count;
	free(res.window_list);
	_cache_lid(fed_adapter);
	error_code = ntbl_status_adapter(NTBL_VERSION, 
					 adapter_name, 
					 &win_count, 
					 &status);
	umask(fed_umask);
	if(error_code)
		slurm_seterrno_ret(ESTATUS);
	tmp_winlist = (fed_window_t *)xmalloc(sizeof(fed_window_t) * 
					     res.window_count);
	if(!tmp_winlist)
		slurm_seterrno_ret(ENOMEM);
	for(i = 0; i < res.window_count; i++) {
		tmp_winlist[i].id = status->window_id;
		tmp_winlist[i].status = status->rc;
		old = status;
		status = status->next;
		free(old);
	}
	fed_adapter->window_list = tmp_winlist;
	return SLURM_SUCCESS;
}

static char *_get_fed_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc;
	int i;

	if (!val)
		return xstrdup(FEDERATION_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("federation.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "federation.conf");
	return rc;
}

static int _parse_fed_file(hostlist_t *adapter_list)
{
	s_p_options_t options[] = {{"AdapterName", S_P_STRING}, {NULL}};
	s_p_hashtbl_t *tbl;
	char *adapter_name;

	debug("Reading the federation.conf file");
	if (!fed_conf)
		fed_conf = _get_fed_conf();

	tbl = s_p_hashtbl_create(options);
	if(s_p_parse_file(tbl, fed_conf) == SLURM_ERROR)
		fatal("something wrong with opening/reading federation "
		      "conf file");
	
	if (s_p_get_string(&adapter_name, "AdapterName", tbl)) {
		int rc;
		rc = hostlist_push(*adapter_list, adapter_name);
		if (rc == 0)
			error("Adapter name format is incorrect.");
		xfree(adapter_name);
	}

	s_p_hashtbl_destroy(tbl);
	
	return SLURM_SUCCESS;
}

/* Check for existence of sniX, where X is from 0 to FED_MAXADAPTERS.
 * For all that exist, record vital adapter info plus status for all windows
 * available on that adapter.  Cache lid to adapter name mapping locally.
 *
 * Used by: slurmd
 */
static int 
_get_adapters(fed_adapter_t *list, int *count)
{
	hostlist_iterator_t adapter_iter;
	char *adapter = NULL;
	int i;

	assert(list != NULL);
	assert(adapter_list != NULL);
		
	adapter_iter = hostlist_iterator_create(adapter_list);
	for (i = 0; (adapter = hostlist_next(adapter_iter)); i++) {
		if(_set_up_adapter(list + i, adapter) == SLURM_ERROR)
			fatal("Failed to set up adapter %s.", adapter);
		free(adapter);
	}
	hostlist_iterator_destroy(adapter_iter);

	assert(i > 0);
	*count = i;
	info("Number of adapters is = %d", *count);
	
	if(!*count)
		slurm_seterrno_ret(ENOADAPTER);
	
	return 0;		
}

/* Used by: slurmd, slurmctld */
int
fed_alloc_jobinfo(fed_jobinfo_t **j)
{
	fed_jobinfo_t *new;

	assert(j != NULL);
	new = (fed_jobinfo_t *)xmalloc(sizeof(fed_jobinfo_t));
	if (!new)
		slurm_seterrno_ret(ENOMEM);
	new->magic = FED_JOBINFO_MAGIC;
	new->job_key = -1;
	new->window_memory = 0;
	new->tables_per_task = 0;
	new->tableinfo = NULL;
	*j = new;
	
	return 0;
}

/* Used by: slurmd, slurmctld */
int
fed_alloc_nodeinfo(fed_nodeinfo_t **n)
{
	fed_nodeinfo_t *new;

 	assert(n);

	new = (fed_nodeinfo_t *)xmalloc(sizeof(fed_nodeinfo_t));
	if(!new)
		slurm_seterrno_ret(ENOMEM);
	new->adapter_list = (fed_adapter_t *)xmalloc(sizeof(fed_adapter_t) 
		* FED_MAXADAPTERS);
	if(!new->adapter_list) {
		xfree(new);
		slurm_seterrno_ret(ENOMEM);
	}
	new->magic = FED_NODEINFO_MAGIC;
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
int
fed_build_nodeinfo(fed_nodeinfo_t *n, char *name)
{
	int count;
	int err;
	
	assert(n);
	assert(n->magic == FED_NODEINFO_MAGIC);
	assert(name);

	strncpy(n->name, name, FED_HOSTLEN);
	_lock();
	err = _get_adapters(n->adapter_list, &count);
	_unlock();
	if(err != 0)
		return err;
	n->adapter_count = count;
	return 0;
}
#if FED_DEBUG
static int
_print_adapter_resources(ADAPTER_RESOURCES *r, char *buf, size_t size)
{
	int count;
	
	assert(r);
	assert(buf);
	assert(size > 0);
	
	count = snprintf(buf, size,
			"--Begin Adapter Resources--\n"
			"  device_type = %x\n"
			"  lid = %d\n"
			"  network_id = %d\n"
			"  max_window_memory = %lld\n"
			"  min_window_memory = %lld\n"
			"  avail_adapter_memory = %lld\n"
			"  fifo_slot_size = %lld\n"
			"  window_count = %d\n"
			"  window_list = %d\n"
#if NTBL_VERSION == 120
			"  reserved = %lld\n"
#else
			"  rcontext_block_count = %lld\n"
#endif
			"--End Adapter Resources--\n",
			r->device_type,
			r->lid,
			r->network_id,
			r->max_window_memory,
			r->min_window_memory,
			r->avail_adapter_memory,
			r->fifo_slot_size,
			r->window_count,
			r->window_list[0],
#if NTBL_VERSION == 120
			r->reserved);
#else
			r->rcontext_block_count);
#endif
	
	return count;
}

static int
_print_window_status(struct NTBL_STATUS *s, char *buf, size_t size)
{
	int count;
	
	assert(s);
	assert(buf);
	assert(size > 0);
	
	switch(s->rc) {
	case NTBL_UNLOADED_STATE:
		count = snprintf(buf, size,
#if FED_VERBOSE_PRINT
			"--Begin NTBL Status For Window %d on %s--\n"
			"  window_id = %u\n"
			"  adapter = %s\n"
			"  return code = %s\n"
			"--End NTBL Status For Window %d on %s--\n",
			s->window_id, s->adapter,
			s->window_id,
			s->adapter,
			_lookup_fed_status_tab(s->rc),
			s->window_id, s->adapter);
#else
			"window %u on %s: %s\n",
			s->window_id, s->adapter, 
			_lookup_fed_status_tab(s->rc));			
#endif
		break;
	case NTBL_LOADED_STATE:
	case NTBL_DISABLED_STATE:
	case NTBL_ACTIVE_STATE:
	case NTBL_BUSY_STATE:
		count = snprintf(buf, size,
#if FED_VERBOSE_PRINT
			"--Begin NTBL Status For Window %d on %s--\n"
			"  user_name = %s\n"
			"  client_pid = %d\n"
			"  uid = %d\n"
			"  window_id = %u\n"
			"  adapter = %s\n"
			"  memory_requested = %llu\n"
			"  memory_allocated = %llu\n"
			"  time_loaded = %s\n"
			"  description = %s\n"
			"  return code = %s\n"
			"--End NTBL Status For Window %d on %s--\n",
			s->window_id, s->adapter,
			s->user_name,
			s->client_pid,
			s->uid,
			s->window_id,
			s->adapter,
			s->memory_requested,
			s->memory_allocated,
			s->time_loaded,
			s->description,
			_lookup_fed_status_tab(s->rc),
			s->window_id, s->adapter);
#else
			"window %u on %s: %s\n",
			s->window_id, s->adapter,
			_lookup_fed_status_tab(s->rc));			
#endif			
		break;
	default:
		count = snprintf(buf, size,
			"Uknown NTBL Return Code For Window %d: %s\n",
			 s->window_id, 
			 _lookup_fed_status_tab(s->rc));
	}					
	
	return count;		
}
#endif
static int
_print_window_struct(fed_window_t *w, char *buf, size_t size)
{
	int count;
	
	assert(w);
	assert(buf);
	assert(size > 0);


	count = snprintf(buf, size,
		"      Window %u: %s\n",
		w->id,
		_lookup_fed_status_tab(w->status));

	return count;
}

/* Writes out nodeinfo structure to a buffer.  Maintains the
 * snprintf semantics by only filling the buffer up to the value 
 * of size.  If FED_VERBOSE_PRINT is defined this function will 
 * dump the entire structure, otherwise only the "useful" part.
 *
 * Used by: slurmd, slurmctld
 */
char *
fed_print_nodeinfo(fed_nodeinfo_t *n, char *buf, size_t size)
{
	fed_adapter_t *a;
	int i,j;
	fed_window_t *w;
	int remaining = size;
	int count;
	char *tmp = buf;
	
	assert(n);
	assert(buf);
	assert(size > 0);
	assert(n->magic == FED_NODEINFO_MAGIC);

	count = snprintf(tmp, remaining, 
			 "Node: %s\n",
			 n->name);
	if(count < 0)
		return buf;
	remaining -= count;
	tmp += count;
	if(remaining < 1)
		return buf;
	for(i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		count = snprintf(tmp, remaining,
#if FED_VERBOSE_PRINT
			"    Adapter: %s\n"
			"      lid: %u\n"
			"      network_id: %u\n"
			"      max_window_memory: %u\n"
			"      min_window_memory: %u\n"
			"      avail_adapter_memory: %u\n"
			"      window_count: %u\n",
			a->name,
			a->lid,
			a->network_id,
			a->max_winmem,
			a->min_winmem,
			a->avail_mem,
			a->window_count);
#else
			"  Adapter: %s\n"
			"    Window count: %d\n"
			"    Active windows:\n",
			a->name,
			a->window_count);
#endif
		if(count < 0)
			return buf;
		remaining -= count;
		tmp += count;
		if(remaining < 1)
			return buf;
			
		w = a->window_list;
		for(j = 0; j < a->window_count; j++) {
#if FED_VERBOSE_PRINT
			count = _print_window_struct(&w[j], tmp, remaining);
#else
			
			if(w[j].status != NTBL_UNLOADED_STATE)
				count = _print_window_struct(&w[j], tmp, 
						remaining);
			else
				count = 0;
#endif
			if(count < 0)
				return buf;
			remaining -= count;
			tmp += count;
			if(remaining < 1)
				return buf;
		}			
	}
	
	return buf;
}

/* Note that when collecting max_winmem, min_winmem and avail_mem
 * we convert these values from 64 to 32 bit unisgned integers.  This 
 * was to make the pack/unpack implementation easier.  I am taking a 
 * chance here that IBM will not release Federation adapters with more 
 * than 4GB of memory.
 *
 * Used by: all
 */
int
fed_pack_nodeinfo(fed_nodeinfo_t *n, Buf buf)
{
	int i,j;
	fed_adapter_t *a;
	int offset;
	
	assert(n);
	assert(n->magic == FED_NODEINFO_MAGIC);
	assert(buf);
	
	offset = get_buf_offset(buf);
	pack32(n->magic, buf);
	packmem(n->name, FED_HOSTLEN, buf);
	pack32(n->adapter_count, buf);
	for(i = 0; i < n->adapter_count; i++) {
		a = n->adapter_list + i;
		packmem(a->name, FED_ADAPTERNAME_LEN, buf);
		pack16(a->lid, buf);
		pack16(a->network_id, buf);
		pack32(a->max_winmem, buf);
		pack32(a->min_winmem, buf);
		pack32(a->avail_mem, buf);
		pack32(a->window_count, buf);
		for(j = 0; j < a->window_count; j++) {
			pack16(a->window_list[j].id, buf);
			pack32(a->window_list[j].status, buf);
			pack16(a->window_list[j].job_key, buf);
		}
	}

	return(get_buf_offset(buf) - offset);
}

/* Used by: all */
static int
_copy_node(fed_nodeinfo_t *dest, fed_nodeinfo_t *src)
{
	int i,j;
	fed_adapter_t *sa = NULL;
	fed_adapter_t *da = NULL;
	
	assert(dest);
	assert(src);
	assert(dest->magic == FED_NODEINFO_MAGIC);
	assert(src->magic == FED_NODEINFO_MAGIC);

	strncpy(dest->name, src->name, FED_HOSTLEN);
	dest->adapter_count = src->adapter_count;
	for(i = 0; i < dest->adapter_count; i++) {
		sa = src->adapter_list + i;
		da = dest->adapter_list +i;
		strncpy(da->name, sa->name, FED_ADAPTERNAME_LEN);
		da->lid = sa->lid;
		da->network_id = sa->network_id;
		da->max_winmem = sa->max_winmem;
		da->min_winmem = sa->min_winmem;
		da->avail_mem = sa->avail_mem;
		da->window_count = sa->window_count;
		da->window_list = (fed_window_t *)xmalloc(sizeof(fed_window_t) *
			da->window_count);
		if(!da->window_list) {
			slurm_seterrno_ret(ENOMEM);
		}
		for(j = 0; j < da->window_count; j++)
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
_hash_index (char *name)
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
	index %= fed_state->hash_max;
	
	return index;
}

/* Tries to find a node fast using the hash table
 *
 * Used by: slurmctld
 */
static fed_nodeinfo_t *
_find_node(fed_libstate_t *lp, char *name)
{
	int i;
	fed_nodeinfo_t *n;
	
	assert(name);
	assert(lp);

	if (lp->node_count == 0)
		return NULL;

	if (lp->hash_table) {
		i = _hash_index(name);
		n = lp->hash_table[i];
		while(n) {
			assert(n->magic == FED_NODEINFO_MAGIC);
			if(!strncmp(n->name, name, FED_HOSTLEN))
				return n;
			n = n->next;
		}
	}

	return NULL;
}

/* Add the hash entry for a newly created fed_nodeinfo_t
 */
static void
_hash_add_nodeinfo(fed_libstate_t *state, fed_nodeinfo_t *node)
{
	int index;

	assert(state);
	assert(state->hash_table);
	assert(state->hash_max >= state->node_count);
	if(!strlen(node->name))
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
_hash_rebuild(fed_libstate_t *state)
{
	int i;
	
	assert(state);
	
	if(state->hash_table)
		xfree(state->hash_table);
	if (state->node_count > state->hash_max || state->hash_max == 0)
		state->hash_max += FED_HASHCOUNT;
	state->hash_table = (fed_nodeinfo_t **)
		xmalloc(sizeof(fed_nodeinfo_t *) * state->hash_max);
	memset(state->hash_table, 0,
	       sizeof(fed_nodeinfo_t *) * state->hash_max);
	for(i = 0; i < state->node_count; i++)
		_hash_add_nodeinfo(state, &(state->node_list[i]));
}

/* If the node is already in the node list then simply return
 * a pointer to it, otherwise dynamically allocate memory to the 
 * node list if necessary.
 * 
 * Used by: slurmctld
 */
static fed_nodeinfo_t *
_alloc_node(fed_libstate_t *lp, char *name)
{
	fed_nodeinfo_t *n = NULL;
	int new_bufsize;
	bool need_hash_rebuild = false;

	assert(lp);

	if(name != NULL) {
		n = _find_node(lp, name);
		if(n != NULL)
			return n;
	}

	fed_need_state_save = true;

	if(lp->node_count >= lp->node_max) {
		lp->node_max += FED_NODECOUNT;
		new_bufsize = lp->node_max * sizeof(fed_nodeinfo_t);
		if(lp->node_list == NULL)
			lp->node_list = (fed_nodeinfo_t *)xmalloc(new_bufsize);
		else
			lp->node_list = (fed_nodeinfo_t *)xrealloc(lp->node_list,
								   new_bufsize);
		need_hash_rebuild = true;
	}
	if(lp->node_list == NULL) {
		slurm_seterrno(ENOMEM);
		return NULL;
	}
	
	n = lp->node_list + (lp->node_count++);
	n->magic = FED_NODEINFO_MAGIC;
	n->name[0] = '\0';
	n->adapter_list = (fed_adapter_t *)xmalloc(FED_MAXADAPTERS *
		sizeof(fed_adapter_t));

	if(name != NULL) {
		strncpy(n->name, name, FED_HOSTLEN);
		if (need_hash_rebuild || lp->node_count > lp->hash_max)
			_hash_rebuild(lp);
		else
			_hash_add_nodeinfo(lp, n);
	}

	return n;
}
#if FED_DEBUG
/* Used by: slurmctld */
static void
_print_libstate(const fed_libstate_t *l)
{
	int i;
	char buf[3000];
	
	assert(l);
	
	printf("--Begin libstate--\n");
	printf("  magic = %u\n", l->magic);
	printf("  node_count = %u\n", l->node_count);
	printf("  node_max = %u\n", l->node_max);
	printf("  hash_max = %u\n", l->hash_max);
	for(i = 0; i < l->node_count; i++) {
		memset(buf, 0, 3000);
		fed_print_nodeinfo(&l->node_list[i], buf, 3000);
		printf("%s", buf);
	}
	printf("--End libstate--\n");
}
#endif


/* Throw away adapter portion of the nodeinfo.
 *
 * Used by: _unpack_nodeinfo
 */
static int _fake_unpack_adapters(Buf buf)
{
	uint32_t adapter_count;
	uint32_t window_count;
	uint32_t dummy32;
	uint16_t dummy16;
	char *dummyptr;
	int i, j;

	safe_unpack32(&adapter_count, buf);
	for (i = 0; i < adapter_count; i++) {
		/* no copy, just advances buf counters */
		safe_unpackmem_ptr(&dummyptr, &dummy32, buf);
		if (dummy32 != FED_ADAPTERNAME_LEN)
			goto unpack_error;
		safe_unpack16(&dummy16, buf);
		safe_unpack16(&dummy16, buf);
		safe_unpack32(&dummy32, buf);
		safe_unpack32(&dummy32, buf);
		safe_unpack32(&dummy32, buf);
		safe_unpack32(&window_count, buf);
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
 * from the packed fed_nodeinfo_t.  If it is false we set the status of
 * all windows to NTBL_UNLOADED_STATE.
 *
 * Used by: slurmctld
 */
static int
_unpack_nodeinfo(fed_nodeinfo_t *n, Buf buf, bool believe_window_status)
{
	int i, j;
	fed_adapter_t *tmp_a = NULL;
	fed_window_t *tmp_w = NULL;
	uint32_t size;
	fed_nodeinfo_t *tmp_n = NULL;
	char *name_ptr, name[FED_HOSTLEN];
	int magic;

	/* NOTE!  We don't care at this point whether n is valid.
	 * If it's NULL, we will just forego the copy at the end.
	 */	
	assert(buf);
	
	/* Extract node name from buffer
	 */
	safe_unpack32(&magic, buf);
	if(magic != FED_NODEINFO_MAGIC)
		slurm_seterrno_ret(EBADMAGIC_FEDNODEINFO);
	safe_unpackmem_ptr(&name_ptr, &size, buf);
	if(size != FED_HOSTLEN)
		goto unpack_error;
	memcpy(name, name_ptr, size);

	/* When the slurmctld is in normal operating mode (NOT backup mode),
	 * the global fed_state structure should NEVER be NULL at the time that
	 * this function is called.  Therefore, if fed_state is NULL here,
	 * we assume that the controller is in backup mode.  In backup mode,
	 * the slurmctld only unpacks RPCs to find out their identity.
	 * Most of the RPCs, including the one calling this function, are
	 * simply ignored.
	 * 
	 * So, here we just do a fake unpack to advance the buffer pointer.
	 */
	if (fed_state == NULL) {
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
		tmp_n = _find_node(fed_state, name);
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
	tmp_n = _alloc_node(fed_state, name);
	if(tmp_n == NULL)
		return SLURM_ERROR;
	tmp_n->magic = magic;
	safe_unpack32(&tmp_n->adapter_count, buf);
	for(i = 0; i < tmp_n->adapter_count; i++) {
		tmp_a = tmp_n->adapter_list + i;
		safe_unpackmem_ptr(&name_ptr, &size, buf);
		if(size != FED_ADAPTERNAME_LEN)
			goto unpack_error;
		memcpy(tmp_a->name, name_ptr, size);
		safe_unpack16(&tmp_a->lid, buf);
		safe_unpack16(&tmp_a->network_id, buf);
		safe_unpack32(&tmp_a->max_winmem, buf);
		safe_unpack32(&tmp_a->min_winmem, buf);
		safe_unpack32(&tmp_a->avail_mem, buf);
		safe_unpack32(&tmp_a->window_count, buf);
		tmp_w = (fed_window_t *)xmalloc(sizeof(fed_window_t) * 
			tmp_a->window_count);
		if(!tmp_w)
			slurm_seterrno_ret(ENOMEM);
		for(j = 0; j < tmp_a->window_count; j++) {
			safe_unpack16(&tmp_w[j].id, buf);
			safe_unpack32(&tmp_w[j].status, buf);
			safe_unpack16(&tmp_w[j].job_key, buf);
			if (!believe_window_status) {
				tmp_w[j].status = NTBL_UNLOADED_STATE;
				tmp_w[j].job_key = 0;
			}
		}
		tmp_a->window_list = tmp_w;
		tmp_w = NULL;	/* don't free on unpack error of next adapter */
	}
	
copy_node:
	/* Only copy the node_info structure if the caller wants it */
	if(n != NULL)
		if(_copy_node(n, tmp_n) != SLURM_SUCCESS)
			return SLURM_ERROR;

#if FED_DEBUG
	_print_libstate(fed_state);
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
int
fed_unpack_nodeinfo(fed_nodeinfo_t *n, Buf buf)
{
	int rc;

	_lock();
	rc = _unpack_nodeinfo(n, buf, false);
	_unlock();
	return rc;
}


/* Used by: slurmd, slurmctld */
void
fed_free_nodeinfo(fed_nodeinfo_t *n, bool ptr_into_array)
{
	fed_adapter_t *adapter;
	int i;
	
	if(!n)
		return;
	
	assert(n->magic == FED_NODEINFO_MAGIC);
	
	if(n->adapter_list) {
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
 * Federation documentation states that the job key must be greater
 * than 0 and less than 0xFFF0.
 *
 * Used by: slurmctld
 */
static uint16_t
_next_key(void)
{
	uint16_t key;
	
	assert(fed_state);
	
	_lock();
	key = fed_state->key_index % 0xFFF0;
	if (key == 0)
		key++;
	fed_state->key_index = key + 1;
	_unlock();
	
	return key;
}

/* FIXME - this could be a little smarter than walking the whole list each time */
static fed_window_t *
_find_free_window(fed_adapter_t *adapter) {
	int i;
	fed_window_t *window;

	for (i = FED_MIN_WIN; i < adapter->window_count; i++) {
		window = &adapter->window_list[i];
		if (window->status == NTBL_UNLOADED_STATE)
			return window;
	}

	return (fed_window_t *) NULL;
}


static fed_window_t *
_find_window(fed_adapter_t *adapter, int window_id) {
	int i;
	fed_window_t *window;

	for (i = FED_MIN_WIN; i < adapter->window_count; i++) {
		window = &adapter->window_list[i];
		if (window->id == window_id)
			return window;
	}

	debug3("Unable to _find_window %d on adapter %s",
	       window_id, adapter->name);
	return (fed_window_t *) NULL;
}


/* For a given process, fill out an NTBL
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign adapters, lids and switch windows to
 * each task in a job.
 *
 * Used by: slurmctld
 */
static int
_allocate_windows_all(int adapter_cnt, fed_tableinfo_t *tableinfo,
		      char *hostname, int task_id, uint16_t job_key)
{
	fed_nodeinfo_t *node;
	fed_adapter_t *adapter;
	fed_window_t *window;
	NTBL *table;
	int i;
	
	assert(tableinfo);
	assert(hostname);
	
	node = _find_node(fed_state, hostname);
	if(node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}
	
	/* Reserve a window on each adapter for this task */
	for (i = 0; i < adapter_cnt; i++) {
		adapter = &node->adapter_list[i];
		window = _find_free_window(adapter);
		if (window == NULL) {
			error("No free windows on node %s adapter %s",
			      node->name, adapter->name);
			return SLURM_ERROR;
		}
		window->status = NTBL_LOADED_STATE;
		window->job_key = job_key;

		table = tableinfo[i].table[task_id];
		table->task_id = task_id;
		table->lid = adapter->lid;
		table->window_id = window->id;

		strncpy(tableinfo[i].adapter_name, adapter->name,
			FED_ADAPTERNAME_LEN);
	}
	
	return SLURM_SUCCESS;
}


/* For a given process, fill out an NTBL
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign a single adapter, lid and switch window to
 * a task in a job.
 *
 * Used by: slurmctld
 */
static int
_allocate_window_single(char *adapter_name, fed_tableinfo_t *tableinfo,
			char *hostname, int task_id, uint16_t job_key)
{
	fed_nodeinfo_t *node;
	fed_adapter_t *adapter = NULL;
	fed_window_t *window;
	NTBL *table;
	int i;
	
	assert(tableinfo);
	assert(hostname);
	
	debug("in _allocate_window_single");
	node = _find_node(fed_state, hostname);
	if(node == NULL) {
		error("Failed to find node in node_list: %s", hostname);
		return SLURM_ERROR;
	}
	
	/* find the adapter */
	for (i = 0; i < node->adapter_count; i++) {
		debug("adapter %s at index %d", node->adapter_list[i].name, i);
		if (strcasecmp(node->adapter_list[i].name, adapter_name)
		    == 0) {
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
		      node->name, adapter->name);
		return SLURM_ERROR;
	}
	window->status = NTBL_LOADED_STATE;
	window->job_key = job_key;

	table = tableinfo[0].table[task_id];
	table->task_id = task_id;
	table->lid = adapter->lid;
	table->window_id = window->id;

	strncpy(tableinfo[0].adapter_name, adapter_name,
		FED_ADAPTERNAME_LEN);
	
	return SLURM_SUCCESS;
}


/* Find the correct NTBL structs and set the state 
 * of the switch windows for the specified task_id.
 *
 * Used by: slurmctld
 */
static int
_window_state_set(int adapter_cnt, fed_tableinfo_t *tableinfo,
		  char *hostname, int task_id, enum NTBL_RC state,
		  uint16_t job_key)
{
	fed_nodeinfo_t *node = NULL;
	fed_adapter_t *adapter = NULL;
	fed_window_t *window = NULL;
	NTBL *table = NULL;
	int i, j;
	bool adapter_found;
	
	assert(tableinfo);
	assert(hostname);
	assert(adapter_cnt <= FED_MAXADAPTERS);
	
	node = _find_node(fed_state, hostname);
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
		table = tableinfo[i].table[task_id];
		if (table == NULL) {
			error("tableinfo[%d].table[%d] is NULL", i, task_id);
			return SLURM_ERROR;
		}

		adapter_found = false;
		/* Find the adapter that matches the one in tableinfo */
		for (j = 0; j < node->adapter_count; j++) {
			adapter = &node->adapter_list[j];
			if (strcasecmp(adapter->name,
				       tableinfo[i].adapter_name) == 0
			    && adapter->lid == table->lid) {
				adapter_found = true;
				break;
			}
		}
		if (!adapter_found) {
			if (table->lid != 0)
				error("Did not find the correct adapter: "
				      "%hu vs. %hu",
				      adapter->lid, table->lid);
			return SLURM_ERROR;
		}

		debug3("Setting status %s adapter %s, "
		       "lid %hu, window %hu for task %d",
		       state == NTBL_UNLOADED_STATE ? "UNLOADED" : "LOADED",
		       adapter->name,
		       table->lid, table->window_id, task_id);
		window = _find_window(adapter, table->window_id);
		if (window) {
			window->status = state;
			window->job_key =
				(state == NTBL_UNLOADED_STATE) ? 0 : job_key;
		}
	}
	
	return SLURM_SUCCESS;
}


#if FED_DEBUG
/* Used by: all */
static void
_print_table(NTBL **table, int size)
{
	int i;
	
	assert(table);
	assert(size > 0);
	
	printf("--Begin NTBL table--\n");
	for(i = 0; i < size; i++) {
		printf("  task_id: %u\n", table[i]->task_id);
		printf("  window_id: %u\n", table[i]->window_id);
		printf("  lid: %u\n", table[i]->lid);
	}
	printf("--End NTBL table--\n");
}

/* Used by: all */
static void
_print_index(char *index, int size)
{
	int i;
	
	assert(index);
	assert(size > 0);
	
	printf("--Begin lid index--\n");
	for(i = 0; i < size; i++) {
		printf("  task_id: %u\n", i);
		printf("  name: %s\n", index + (i * FED_ADAPTERNAME_LEN));
	}
	printf("--End lid index--\n");
}
#endif


/* Find all of the windows used by this job step and set their
 * status to "state".
 *
 * Used by: slurmctld
 */
static int
_job_step_window_state(fed_jobinfo_t *jp, hostlist_t hl, enum NTBL_RC state)
{
	hostlist_iterator_t hi;
	char *host;
	int proc_cnt;
	int nprocs;
	int nnodes;
	int i, j;
	int rc;
	int task_cnt;
	int full_node_cnt;
	int min_procs_per_node;
	int max_procs_per_node;
	
	xassert(!hostlist_is_empty(hl));
	xassert(jp);
	xassert(jp->magic == FED_JOBINFO_MAGIC);

	if ((jp == NULL)
	    || (jp->magic != FED_JOBINFO_MAGIC)
	    || (hostlist_is_empty(hl)))
		return SLURM_ERROR;

	if ((jp->tables_per_task == 0)
	    || !jp->tableinfo
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
		if(!host)
			error("Failed to get next host");
		
		if(i < full_node_cnt)
			task_cnt = max_procs_per_node;
		else
			task_cnt = min_procs_per_node;
		
		for (j = 0; j < task_cnt; j++) {
			rc = _window_state_set(jp->tables_per_task,
					       jp->tableinfo,
					       host, proc_cnt,
					       state, jp->job_key);
			proc_cnt++;
		}
		free(host);
	}
	_unlock();
	
	hostlist_iterator_destroy(hi);
	return SLURM_SUCCESS;
}

/*
 * For one node, free all of the windows belonging to a particular
 * job step (as identified by the job_key).
 */
static void inline
_free_windows_by_job_key(uint16_t job_key, char *nodename)
{
	fed_nodeinfo_t *node;
	fed_adapter_t *adapter;
	fed_window_t *window;
	int i, j;

	/* debug3("_free_windows_by_job_key(%hu, %s)", job_key, nodename); */
	if ((node = _find_node(fed_state, nodename)) == NULL)
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
			      nodename, adapter->name);
			continue;
		}
		/* We could check here to see if this adapter's name
		 * is in the fed_jobinfo tablinfo list to avoid the next
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
				window->status = NTBL_UNLOADED_STATE;
				window->job_key = 0;
			}
		}
	}
}

/* Find all of the windows used by job step "jp" on the hosts
 * designated in hostlist "hl" and mark their state NTBL_UNLOADED_STATE.
 *
 * Used by: slurmctld
 */
int
fed_job_step_complete(fed_jobinfo_t *jp, hostlist_t hl)
{
	hostlist_t uniq_hl;
	hostlist_iterator_t hi;
	char *nodename;

	xassert(!hostlist_is_empty(hl));
	xassert(jp);
	xassert(jp->magic == FED_JOBINFO_MAGIC);

	if ((jp == NULL)
	    || (jp->magic != FED_JOBINFO_MAGIC)
	    || (hostlist_is_empty(hl)))
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
	if (fed_state != NULL) {
		while((nodename = hostlist_next(hi)) != NULL) {
			_free_windows_by_job_key(jp->job_key, nodename);
			free(nodename);
		}
	} else { /* fed_state == NULL */
		/* If there is no state at all, the job is already cleaned
		 * up. :)  This should really only happen when the backup
		 * controller is calling job_fini() just before it takes over
		 * the role of active controller.
		 */
		debug("fed_job_step_complete called when fed_state == NULL");
	}
	_unlock();
	
	hostlist_iterator_destroy(hi);
	hostlist_destroy(uniq_hl);
	return SLURM_SUCCESS;
}


/* Find all of the windows used by job step "jp" and mark their
 * state NTBL_LOADED_STATE.
 *
 * Used by the slurmctld at startup time to restore the allocation
 * status of any job steps that were running at the time the previous
 * slurmctld was shutdown.  Also used to restore the allocation
 * status after a call to switch_clear().
 */
int
fed_job_step_allocated(fed_jobinfo_t *jp, hostlist_t hl)
{
	return _job_step_window_state(jp, hl, NTBL_LOADED_STATE);
}



/* Setup everything for the job.  Assign tasks across
 * nodes based on the hostlist given and create the network table used
 * on all nodes of the job.
 *
 * Used by: slurmctld
 */
int
fed_build_jobinfo(fed_jobinfo_t *jp, hostlist_t hl, int nprocs,
		  bool sn_all, char *adapter_name, int bulk_xfer)
{
	int nnodes;
	hostlist_iterator_t hi;
	char *host = NULL;
	int proc_cnt = 0;
	int i, j;
	fed_nodeinfo_t *node;
	int rc;
	int task_cnt;
	int full_node_cnt;
	int min_procs_per_node;
	int max_procs_per_node;

	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);
	assert(!hostlist_is_empty(hl));

	if(nprocs <= 0)
		slurm_seterrno_ret(EINVAL);

	jp->bulk_xfer = (uint8_t) bulk_xfer;
	jp->job_key = _next_key();
	snprintf(jp->job_desc, DESCLEN,
		 "slurm federation driver key=%d", jp->job_key);
	jp->window_memory = FED_AUTO_WINMEM;

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
		node = _find_node(fed_state, host);
		jp->tables_per_task = node ? node->adapter_count : 0;
		_unlock();
		if (host != NULL)
			free(host);
		hostlist_iterator_reset(hi);
	} else {
		jp->tables_per_task = 1;
	}

	/* Allocate memory for each fed_tableinfo_t */
	jp->tableinfo = (fed_tableinfo_t *) xmalloc(jp->tables_per_task
						    * sizeof(fed_tableinfo_t));
	for (i = 0; i < jp->tables_per_task; i++) {
		jp->tableinfo[i].table_length = nprocs;
		jp->tableinfo[i].table = (NTBL **) xmalloc(nprocs
							   * sizeof(NTBL *));
		for (j = 0; j < nprocs; j++) {
			jp->tableinfo[i].table[j] =
				(NTBL *) xmalloc(sizeof(NTBL));
		}
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
		if(!host)
			error("Failed to get next host");
		
		if(i < full_node_cnt)
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
	

#if FED_DEBUG
	_print_table(jp->tableinfo[i].table, jp->tableinfo[i].table_length);
#endif
			
	hostlist_iterator_destroy(hi);
	return SLURM_SUCCESS;

fail:
	free(host);
	hostlist_iterator_destroy(hi);
	/* slurmctld will call fed_free_jobinfo on jp */
	return SLURM_FAILURE;
}

void
_pack_tableinfo(fed_tableinfo_t *tableinfo, Buf buf)
{
	int i;

	pack32(tableinfo->table_length, buf);
	for (i = 0; i < tableinfo->table_length; i++) {
		pack16(tableinfo->table[i]->task_id, buf);
		pack16(tableinfo->table[i]->lid, buf);
		pack16(tableinfo->table[i]->window_id, buf);
	}
	packmem(tableinfo->adapter_name, FED_ADAPTERNAME_LEN, buf);
}

/* Used by: all */
int
fed_pack_jobinfo(fed_jobinfo_t *j, Buf buf)
{
	int i;

	assert(j);
	assert(j->magic == FED_JOBINFO_MAGIC);
	assert(buf);
	
	pack32(j->magic, buf);
	pack16(j->job_key, buf);
	packmem(j->job_desc, DESCLEN, buf);
	pack32(j->window_memory, buf);
	pack8(j->bulk_xfer, buf);
	pack16(j->tables_per_task, buf);
	for (i = 0; i < j->tables_per_task; i++) {
		_pack_tableinfo(&j->tableinfo[i], buf);
	}

	return SLURM_SUCCESS;
}

/* return 0 on success, -1 on failure */
static int
_unpack_tableinfo(fed_tableinfo_t *tableinfo, Buf buf)
{
	uint32_t size;
	char *name_ptr;
	int i;

	safe_unpack32(&tableinfo->table_length, buf);
	tableinfo->table = (NTBL **) xmalloc(tableinfo->table_length
					     * sizeof(NTBL *));
	for (i = 0; i < tableinfo->table_length; i++) {
		tableinfo->table[i] = (NTBL *) xmalloc(sizeof(NTBL));

		safe_unpack16(&tableinfo->table[i]->task_id, buf);
		safe_unpack16(&tableinfo->table[i]->lid, buf);
		safe_unpack16(&tableinfo->table[i]->window_id, buf);
	}
	safe_unpackmem_ptr(&name_ptr, &size, buf);
	if (size != FED_ADAPTERNAME_LEN)
		goto unpack_error;
	memcpy(tableinfo->adapter_name, name_ptr, size);
	return 0;

unpack_error: /* safe_unpackXX are macros which jump to unpack_error */
	error("unpack error in _unpack_tableinfo");
	return -1;
}

/* Used by: all */
int 
fed_unpack_jobinfo(fed_jobinfo_t *j, Buf buf)
{
	uint32_t size;
	int i, k;
	
	assert(j);
	assert(j->magic == FED_JOBINFO_MAGIC);
	assert(buf);
	
	safe_unpack32(&j->magic, buf);
	assert(j->magic == FED_JOBINFO_MAGIC);
	safe_unpack16(&j->job_key, buf);
	safe_unpackmem(j->job_desc, &size, buf);
	if(size != DESCLEN)
		goto unpack_error;
	safe_unpack32(&j->window_memory, buf);
	safe_unpack8(&j->bulk_xfer, buf);
	safe_unpack16(&j->tables_per_task, buf);

	j->tableinfo = (fed_tableinfo_t *) xmalloc(j->tables_per_task
						   * sizeof(fed_tableinfo_t));
	if(!j->tableinfo)
		slurm_seterrno_ret(ENOMEM);
	for (i = 0; i < j->tables_per_task; i++) {
		if (_unpack_tableinfo(&j->tableinfo[i], buf) != 0)
			goto unpack_error;
	}

	return SLURM_SUCCESS;
	
unpack_error:
	error("fed_unpack_jobinfo error");
	if (j->tableinfo) {
		for (i = 0; i < j->tables_per_task; i++) {
			for (k=0; k<j->tableinfo[i].table_length; k++)
				xfree(j->tableinfo[i].table[k]);
			xfree(j->tableinfo[i].table);
		}
		xfree(j->tableinfo);
	}
	slurm_seterrno_ret(EUNPACK);
	return SLURM_ERROR;
}

/* Used by: all */
fed_jobinfo_t *
fed_copy_jobinfo(fed_jobinfo_t *job)
{
	fed_jobinfo_t *new;
	int i, k;

	assert(job);
	assert(job->magic == FED_JOBINFO_MAGIC);

	if(fed_alloc_jobinfo(&new)) {
		debug("fed_alloc_jobinfo failed");
		goto cleanup1;
	}
	memcpy(new, job, sizeof(fed_jobinfo_t));
	/* table will be empty (and table_size == 0) when the network string
	 * from poe does not contain "us".
	 * (See man poe: -euilib or MP_EUILIB)
	 */
	if (job->tables_per_task > 0) {
		/* Allocate memory for each fed_tableinfo_t */
		new->tableinfo = (fed_tableinfo_t *)xmalloc(
			job->tables_per_task * sizeof(fed_tableinfo_t));
		if (new->tableinfo == NULL)
			goto cleanup2;
		memcpy(new->tableinfo, job->tableinfo,
		       sizeof(fed_tableinfo_t) * job->tables_per_task);
		
		for (i = 0; i < job->tables_per_task; i++) {
			new->tableinfo[i].table =
				(NTBL **) xmalloc(job->tableinfo[i].table_length
						  * sizeof(NTBL *));
			if (new->tableinfo[i].table == NULL)
				goto cleanup3;

			for (k = 0; k < new->tableinfo[i].table_length; k++) {
				new->tableinfo[i].table[k] =
					(NTBL *) xmalloc(sizeof(NTBL));
				if (new->tableinfo[i].table[k] == NULL)
					goto cleanup4;
				memcpy(new->tableinfo[i].table[k],
				       job->tableinfo[i].table[k],
				       sizeof(fed_tableinfo_t));
			}


		}
	}

	return new;

cleanup4:
	k--;
	for ( ; k >= 0; k--)
		xfree(new->tableinfo[i].table[k]);
cleanup3:
	i--;
	for ( ; i >= 0; i--) {
		for (k = 0; k < new->tableinfo[i].table_length; k++)
			xfree(new->tableinfo[i].table[k]);
		xfree(new->tableinfo[i].table);
	}
	xfree(new->tableinfo);
cleanup2:
	fed_free_jobinfo(new);
cleanup1:
	error("Allocating new jobinfo");
	slurm_seterrno(ENOMEM);
	return NULL;
}

/* Used by: all */
void
fed_free_jobinfo(fed_jobinfo_t *jp)
{
	int i, j;
	fed_tableinfo_t *tableinfo;

	if (!jp) {
		return;
	}

	if (jp->magic != FED_JOBINFO_MAGIC) {
		error("jp is not a switch/federation fed_jobinfo_t");
		return;
	}

	jp->magic = 0;
	if (jp->tables_per_task > 0 && jp->tableinfo != NULL) {
		for (i = 0; i < jp->tables_per_task; i++) {
			tableinfo = &jp->tableinfo[i];
			if (tableinfo->table == NULL)
				continue;
			for (j = 0; j < tableinfo->table_length; j++) {
				if (tableinfo->table[j] == NULL)
					continue;
				xfree(tableinfo->table[j]);
			}
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
int
fed_get_jobinfo(fed_jobinfo_t *jp, int key, void *data)
{
	fed_tableinfo_t **tableinfo = (fed_tableinfo_t **)data;
	int *tables_per = (int *)data;
	int *job_key = (int *)data;
	
	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);

	switch(key) {
	case FED_JOBINFO_TABLEINFO:
		*tableinfo = jp->tableinfo;
		break;
	case FED_JOBINFO_TABLESPERTASK:
		*tables_per = jp->tables_per_task;
		break;
	case FED_JOBINFO_KEY:
		*job_key = jp->job_key;
		break;
	default:
		slurm_seterrno_ret(EINVAL);
	}
	
	return SLURM_SUCCESS;
}


/*
 * Check up to "retry" times for "window_id" on "adapter_name"
 * to switch to the NTBL_UNLOADED_STATE.  Sleep one second between 
 * each retry.
 *
 * Used by: slurmd
 */
static int
_wait_for_window_unloaded(char *adapter_name, unsigned short window_id,
			  int retry)
{
	int status;
	int i;

	for (i = 0; i < retry; i++) {
		ntbl_query_window(NTBL_VERSION, adapter_name,
				  window_id, &status);
		if (status == NTBL_UNLOADED_STATE)
			break;
		debug2("Window %hu adapter %s is in use, sleeping 1 second",
		       window_id, adapter_name);
		sleep(1);
	}

	if (status != NTBL_UNLOADED_STATE)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}


/*
 * Look through the table and find all of the NTBL that are for an adapter on
 * this node.  Wait until the window from each local NTBL is in the
 * NTBL_UNLOADED_STATE.
 *
 * Used by: slurmd
 */
static int
_wait_for_all_windows(fed_tableinfo_t *tableinfo)
{
	uint16_t lid;
	int i;
	int err;
	int rc = SLURM_SUCCESS;
	int retry = 15;

	lid = _get_lid_from_adapter(tableinfo->adapter_name);

	for (i = 0; i < tableinfo->table_length; i++) {
		if (tableinfo->table[i]->lid == lid) {
			err = _wait_for_window_unloaded(
				tableinfo->adapter_name,
				tableinfo->table[i]->window_id,
				retry);
			if (err != SLURM_SUCCESS) {
				error("Window %hu adapter %s did not become"
				      " free within %d seconds",
				      lid, tableinfo->table[i]->window_id, 
				      retry);
				rc = err;
				retry = 2;
			}
		}
	}

	return rc;
}


static int
_check_rdma_job_count(char *adapter)
{
	unsigned int job_count;
	unsigned int *job_keys;
	int rc, z;

	rc = ntbl_rdma_jobs(NTBL_VERSION, adapter,
			    &job_count, &job_keys);
	if (rc != NTBL_SUCCESS) {
		error("ntbl_rdma_jobs(): %d", rc);
		return SLURM_ERROR;
	}

	debug3("Adapter %s, RDMA job_count = %u",
	       adapter, job_count);
	for (z = 0; z < job_count; z++)
		debug3("  job key = %u", job_keys[z]);
	free(job_keys);
	if (job_count >= 4) {
		error("RDMA job_count is too high: %u", job_count);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}


/* Load a network table on node.  If table contains more than
 * one window for a given adapter, load the table only once for that
 * adapter.
 *
 * Used by: slurmd
 */
int
fed_load_table(fed_jobinfo_t *jp, int uid, int pid)
{
	int i;
	int err;
	unsigned long long winmem;
	char *adapter;
	uint16_t network_id;
/* 	ADAPTER_RESOURCES res; */
	int rc;

#if FED_DEBUG
	char buf[2000];
#endif
	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);

	for(i = 0; i < jp->tables_per_task; i++) {
#if FED_DEBUG
		_print_table(jp->tableinfo[i].table, jp->tableinfo[i].table_length);
		printf("%s", fed_sprint_jobinfo(jp, buf, 2000));
#endif
		adapter = jp->tableinfo[i].adapter_name;
		network_id = _get_network_id_from_adapter(adapter);

		rc = _wait_for_all_windows(&jp->tableinfo[i]);
		if (rc != SLURM_SUCCESS)
			return rc;

		if(adapter == NULL)
			continue;
		winmem = jp->window_memory;
		if(jp->bulk_xfer) {
			if (i == 0) {
				rc = _check_rdma_job_count(adapter);
				if (rc != SLURM_SUCCESS)
					return rc;
			}

			err = ntbl_load_table_rdma(
				NTBL_VERSION,
				adapter,
				network_id,
				uid,
				pid,
				jp->job_key,
				jp->job_desc,
				jp->bulk_xfer,
				0,             /* rcontext_blocks */
				jp->tableinfo[i].table_length,
				jp->tableinfo[i].table);
		} else {
			err = ntbl_load_table(
				NTBL_VERSION,
				adapter,
				network_id,
				uid,
				pid,
				jp->job_key,
				jp->job_desc,
				&winmem,
				jp->tableinfo[i].table_length,
				jp->tableinfo[i].table);
		}
		if(err != NTBL_SUCCESS) {
			error("unable to load table: [%d] %s\n", 
			      err, _lookup_fed_status_tab(err));
			return SLURM_ERROR;
		}
	}
	umask(fed_umask);
	
	return SLURM_SUCCESS;
}


/*
 * Try up to "retry" times to unload a window.
 */
static int
_unload_window(char *adapter, unsigned short job_key, unsigned short window_id,
	       int retry)
{
	int i;
	int err;

	for (i = 0; i < retry; i++) {
		err = ntbl_unload_window(NTBL_VERSION, adapter,
					 job_key, window_id);
		if (err == NTBL_SUCCESS)
			return SLURM_SUCCESS;
		debug("Unable to unload window %hu adapter %s job_key %hu: %s",
		      window_id, adapter, job_key, _lookup_fed_status_tab(err));

		err = ntbl_clean_window(NTBL_VERSION, adapter,
					ALWAYS_KILL, window_id);
		if (err == NTBL_SUCCESS)
			return SLURM_SUCCESS;
		error("Unable to clean window %hu adapter %s job_key %hu: %s",
		      window_id, adapter, job_key, _lookup_fed_status_tab(err));

		sleep(1);
	}
	
	return SLURM_FAILURE;
}


/* Assumes that, on error, new switch state information will be
 * read from node.
 *
 * Used by: slurmd
 */
int
fed_unload_table(fed_jobinfo_t *jp)
{
	int i, j;
	int err;
	char *adapter_name;
	NTBL **table;
	uint32_t table_length;
	int local_lid;
	int rc = SLURM_SUCCESS;
	int retry = 15;

        assert(jp);
        assert(jp->magic == FED_JOBINFO_MAGIC);
	for (i = 0; i < jp->tables_per_task; i++) {
		table        = jp->tableinfo[i].table;
		table_length = jp->tableinfo[i].table_length;
		adapter_name = jp->tableinfo[i].adapter_name;
		local_lid = _get_lid_from_adapter(adapter_name);

		for(j = 0; j < table_length; j++) {
			if(table[j]->lid != local_lid)
				continue;
			debug3("freeing adapter %s lid %d window %d job_key %d",
			       adapter_name, table[j]->lid,
			       table[j]->window_id, jp->job_key);
			err = _unload_window(adapter_name,
					     jp->job_key,
					     table[j]->window_id,
					     retry);
			if(err != SLURM_SUCCESS) {
				rc = err;
				slurm_seterrno(EUNLOAD);
				retry = 2;
			}
		}
	}
	return rc;
}

static fed_libstate_t *
_alloc_libstate(void)
{
	fed_libstate_t *tmp;
	
	tmp = (fed_libstate_t *)xmalloc(sizeof(fed_libstate_t));
	if(!tmp) {
		slurm_seterrno(ENOMEM);
		return NULL;
	}
	tmp->magic = FED_LIBSTATE_MAGIC;
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
int
fed_init(void)
{
	fed_libstate_t *tmp;

	tmp = _alloc_libstate();
	if(!tmp)
		return SLURM_FAILURE;
	_lock();
	assert(!fed_state);
	fed_state = tmp;
	_unlock();
	
	return SLURM_SUCCESS;
}

static void
_free_libstate(fed_libstate_t *lp)
{
	int i;

	if (!lp)
		return;
	if (lp->node_list != NULL) {
		for (i = 0; i < lp->node_count; i++)
			fed_free_nodeinfo(&lp->node_list[i], true);
		xfree(lp->node_list);
	}
	xfree(lp->hash_table);
	xfree(lp);
}

int
fed_fini(void)
{
	xfree(fed_conf);
	return SLURM_SUCCESS;
}

/* Used by: slurmctld */
static int
_pack_libstate(fed_libstate_t *lp, Buf buffer)
{
	int offset;
	int i;
	
	assert(lp);
	assert(lp->magic == FED_LIBSTATE_MAGIC);
	
	offset = get_buf_offset(buffer);
	pack32(lp->magic, buffer);
	pack32(lp->node_count, buffer);
	for(i = 0; i < lp->node_count; i++)
		(void)fed_pack_nodeinfo(&lp->node_list[i], buffer);
	/* don't pack hash_table, we'll just rebuild on restore */
	pack16(lp->key_index, buffer);
	
	return(get_buf_offset(buffer) - offset);
}

/* Used by: slurmctld */
void
fed_libstate_save(Buf buffer, bool free_flag)
{
	_lock();

	if (fed_state != NULL)
                _pack_libstate(fed_state, buffer);

	/* Clean up fed_state since backup slurmctld can repeatedly 
	 * save and restore state */
	if (free_flag) {
		_free_libstate(fed_state);
		fed_state = NULL;	/* freed above */
	}
	_unlock();
}

/* Used by: slurmctld */
static int
_unpack_libstate(fed_libstate_t *lp, Buf buffer)
{
	int offset;
	int node_count;
	int i;
	
	assert(lp->magic == FED_LIBSTATE_MAGIC);
	
	offset = get_buf_offset(buffer);
	
	safe_unpack32(&lp->magic, buffer);
	safe_unpack32(&node_count, buffer);
	for(i = 0; i < node_count; i++) {
		if (_unpack_nodeinfo(NULL, buffer, false) != SLURM_SUCCESS)
			goto unpack_error;
	}
	if(lp->node_count != node_count) {
		error("Failed to recover switch state of all nodes (%d of %u)",
		      lp->node_count, node_count);
		return SLURM_ERROR;
	}
	safe_unpack16(&lp->key_index, buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	error("unpack error in _unpack_libstate");
	slurm_seterrno_ret(EBADMAGIC_FEDLIBSTATE);
	return SLURM_ERROR;
}

/* Used by: slurmctld */
int
fed_libstate_restore(Buf buffer)
{
	_lock();
	assert(!fed_state);

	fed_state = _alloc_libstate();
	if(!fed_state) {
		error("fed_libstate_restore fed_state is NULL");
		_unlock();
		return SLURM_FAILURE;
	}
	_unpack_libstate(fed_state, buffer);
	_unlock();

	return SLURM_SUCCESS;
}

int
fed_libstate_clear(void)
{
	int i, j, k;
	struct fed_nodeinfo *node;
	struct fed_adapter *adapter;
	struct fed_window *window;

	debug3("Clearing state on all windows in global fed state");
	_lock();
	if (!fed_state || !fed_state->node_list) {
		error("fed_state or node_list not initialized!");
		_unlock();
		return SLURM_ERROR;
	}

	for (i = 0; i < fed_state->node_count; i++) {
		node = &fed_state->node_list[i];
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
				window->status = NTBL_UNLOADED_STATE;
			}
		}
	}
	_unlock();

	return SLURM_SUCCESS;
}
