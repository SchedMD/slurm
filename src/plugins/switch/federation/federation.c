/*****************************************************************************\
 **  federation.c - Library routines for initiating jobs on IBM Federation
 **  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jason King <jking@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/plugins/switch/federation/federation.h"
#include "src/plugins/switch/federation/federation_keys.h"

/* 
 * Definitions local to this module
 */
#define FED_NODEINFO_MAGIC	0xc00cc00d
#define FED_JOBINFO_MAGIC	0xc00cc00e
#define FED_LIBSTATE_MAGIC	0xc00cc00f

#define FED_ADAPTERLEN 16
#define FED_HOSTLEN 20
#define FED_VERBOSE_PRINT 1
#define FED_BUFSIZ 4096
#define FED_MAX_PROCS 4096
#define FED_AUTO_WINMEM 0
#define FED_MAX_WIN 15
#define FED_MIN_WIN 0
#define FED_DEBUG 0

#define ZERO 48

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
} fed_window_t;
	
typedef struct fed_adapter {
	char name[FED_ADAPTERLEN];
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
	uint16_t next_window;
	uint16_t next_adapter;
	struct fed_nodeinfo *next;
};

struct fed_libstate {
	uint32_t magic;
	uint32_t node_count;
	fed_nodeinfo_t *node_list;
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
	uint32_t table_size;
	NTBL **table;
};

typedef struct {
	int status_number;
	char *status_msg;
} fed_status_t;

typedef struct {
	char name[FED_ADAPTERLEN];
	int lid;
} fed_cache_entry_t;

/* 
 * Globals
 */
fed_libstate_t *fed_state = NULL;
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
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
	{FED_STATUS_UNKNOWN, "UNKNOWN_RESULT_CODE"}
};

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
		j->table_size);
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
 * fed_init_cache() simply initializes the cache to sane values and 
 * needs to be called before any other cache functions are called.
 *
 * Used by: slurmd
 */
void
fed_init_cache(void)
{
	int i;
	
	for(i = 0; i < FED_MAXADAPTERS; i++) {
		lid_cache[i].name[0] = 0;
		lid_cache[i].lid = -1;
	}
}

/* Cache the lid of a given adapter.  Ex:  sni0 with lid 10 gets
 * cached in array index 0 with a lid = 10 and a name = sni0.
 *
 * Used by: slurmd
 */
static void
_cache_lid(fed_adapter_t *ap)
{
	assert(ap);
	
	int adapter_num = ap->name[3] - ZERO;

	lid_cache[adapter_num].lid = ap->lid;
	strncpy(lid_cache[adapter_num].name, ap->name, FED_ADAPTERLEN);		
}

/* Check lid cache for a given lid and return the associated adapter 
 * name. 
 *
 * Used by: slurmd
 */
static char *
_get_adapter_from_lid(int lid)
{
	int i;
	
	for(i = 0; i < FED_MAXADAPTERS; i++) {
		if(lid_cache[i].lid == lid) {
			return lid_cache[i].name;
		}		
	}
	
	return NULL;
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
	int err;
	char name[] = "sni0";
	ADAPTER_RESOURCES res;
	struct NTBL_STATUS *status = NULL;
	struct NTBL_STATUS *old = NULL;
	fed_window_t *tmp_winlist = NULL;
	int adapter_count;
	int win_count;
	int i, j;
	
	adapter_count = 0;
	for(i = 0; i < FED_MAXADAPTERS; i++) {
		name[3] = i + ZERO;
		err = ntbl_adapter_resources(NTBL_VERSION, name, &res);
		if(err != NTBL_SUCCESS) 
			continue;
		strncpy(list[adapter_count].name, name, FED_ADAPTERLEN);
		list[adapter_count].lid = res.lid;
		list[adapter_count].network_id = res.network_id;
		/* FUTURE:  check that we don't lose information when converting
		 * from 64 to 32 bit unsigned ints in the next three assignments.
		 */
		list[adapter_count].max_winmem = res.max_window_memory;
		list[adapter_count].min_winmem = res.min_window_memory;
		list[adapter_count].avail_mem = res.avail_adapter_memory;
		list[adapter_count].window_count = res.window_count;
		free(res.window_list);
		_cache_lid(&list[adapter_count]);
		err = ntbl_status_adapter(NTBL_VERSION, name, 
				&win_count, &status);
		if(err)
			slurm_seterrno_ret(ESTATUS);
		tmp_winlist = (fed_window_t *)malloc(sizeof(fed_window_t) * 
			res.window_count);
		if(!tmp_winlist)
			slurm_seterrno_ret(ENOMEM);
		for(j = 0; j < res.window_count; j++) {
			tmp_winlist[j].id = status->window_id;
			tmp_winlist[j].status = status->rc;
			old = status;
			status = status->next;
			free(old);
		}
		list[adapter_count].window_list = tmp_winlist;
		adapter_count++;
	}

	if(!adapter_count)
		slurm_seterrno_ret(ENOADAPTER);
	*count = adapter_count;

	return 0;		
}

/* Used by: slurmd, slurmctld */
int
fed_alloc_jobinfo(fed_jobinfo_t **j)
{
	fed_jobinfo_t *new;

	assert(j != NULL);
	new = (fed_jobinfo_t *)malloc(sizeof(fed_jobinfo_t));
	if (!new)
		slurm_seterrno_ret(ENOMEM);
	new->magic = FED_JOBINFO_MAGIC;
	new->table = NULL;
	*j = new;
	
	return 0;
}

/* Used by: slurmd, slurmctld */
int
fed_alloc_nodeinfo(fed_nodeinfo_t **n)
{
	fed_nodeinfo_t *new;

 	assert(n);

	new = (fed_nodeinfo_t *)malloc(sizeof(fed_nodeinfo_t));
	if(!new)
		slurm_seterrno_ret(ENOMEM);
	new->adapter_list = (fed_adapter_t *)malloc(sizeof(fed_adapter_t) 
		* FED_MAXADAPTERS);
	if(!new->adapter_list) {
		free(new);
		slurm_seterrno_ret(ENOMEM);
	}
	new->magic = FED_NODEINFO_MAGIC;
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
	err = _get_adapters(n->adapter_list, &count);
	if(err != 0)
		return err;
	n->adapter_count = count;
	n->next_adapter = 0;
	n->next_window = 0;

	return 0;
}

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

	count = snprintf(tmp, remaining, "Node: %s\n", n->name);
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
			"  Adapter: %s\n"
			"    lid: %u\n"
			"    network_id: %u\n"
			"    max_window_memory: %u\n"
			"    min_window_memory: %u\n"
			"    avail_adapter_memory: %u\n"
			"    window_count: %u\n",
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
		packmem(a->name, FED_ADAPTERLEN, buf);
		pack16(a->lid, buf);
		pack16(a->network_id, buf);
		pack32(a->max_winmem, buf);
		pack32(a->min_winmem, buf);
		pack32(a->avail_mem, buf);
		pack32(a->window_count, buf);
		for(j = 0; j < a->window_count; j++) {
			pack16(a->window_list[j].id, buf);
			pack32(a->window_list[j].status, buf);
		}
	}
	pack16(n->next_window, buf);
	pack16(n->next_adapter, buf);
	
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
		strncpy(da->name, sa->name, FED_ADAPTERLEN);
		da->lid = sa->lid;
		da->network_id = sa->network_id;
		da->max_winmem = sa->max_winmem;
		da->min_winmem = sa->min_winmem;
		da->avail_mem = sa->avail_mem;
		da->window_count = sa->window_count;
		da->window_list = (fed_window_t *)malloc(sizeof(fed_window_t) *
			da->window_count);
		if(!da->window_list) {
			slurm_seterrno_ret(ENOMEM);
		}
		for(j = 0; j < da->window_count; j++)
			da->window_list[j] = sa->window_list[j];
	}
	dest->next_window = src->next_window;
	dest->next_adapter = src->next_adapter;
	
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
	int i = 0;

	assert(name);
	assert(fed_state);
	assert(fed_state->hash_table);
	
	if (fed_state->node_count == 0)
		return 0;       /* degenerate case */

	while (*name)
		i += (int) *name++;
	i %= fed_state->node_count;
	
	return i;
}

/* Tries to find a node fast using the hash table if possible, 
 * otherwise falls back to a linear search.
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
	
	if(lp->hash_table) {
		i = _hash_index(name);
		n = lp->hash_table[i];
		while(n) {
			assert(n->magic == FED_NODEINFO_MAGIC);
			if(!strncmp(n->name, name, FED_HOSTLEN))
				return n;
			n = n->next;
		}
	} else {
		for(i = 0; i < lp->node_count; i++)
			if(!strncmp(name, lp->node_list[i].name, 
					FED_HOSTLEN))
				return(&lp->node_list[i]);
	}
	
	return NULL;
}

/* Recreates the hash table for the node list.
 *
 * Used by: slurmctld
 */
void
fed_hash_nodes(void)
{
	int i, inx;
	
	assert(fed_state);
	
	_lock();
	if(fed_state->hash_table)
		free(fed_state->hash_table);
	fed_state->hash_table = malloc(sizeof(fed_nodeinfo_t *) * 
		fed_state->node_count);
	memset(fed_state->hash_table, 0, sizeof(fed_nodeinfo_t *) * 
		fed_state->node_count);
	for(i = 0; i < fed_state->node_count; i++) {
		if(!strlen(fed_state->node_list[i].name))
			continue;
		inx = _hash_index(fed_state->node_list[i].name);
		fed_state->node_list[i].next = fed_state->hash_table[inx];
		fed_state->hash_table[inx] = &fed_state->node_list[i];
	}
	_unlock();
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
	int old_bufsize, new_bufsize;
	
	assert(lp);

	if(name != NULL)
		n = _find_node(lp, name);
	if(n != NULL)
		return n;

	old_bufsize = lp->node_count * sizeof(fed_nodeinfo_t);
	old_bufsize = ((old_bufsize / FED_BUFSIZ) + 1) * FED_BUFSIZ;
	new_bufsize = (lp->node_count + 1) *
		sizeof(fed_nodeinfo_t);
	new_bufsize = ((new_bufsize / FED_BUFSIZ) + 1) * FED_BUFSIZ;
	if(lp->node_count == 0)
		lp->node_list = 
			(fed_nodeinfo_t *)malloc(new_bufsize);
	else if (old_bufsize != new_bufsize)
		realloc(lp->node_list, new_bufsize);
	if(lp->node_list == NULL) {
		slurm_seterrno(ENOMEM);
		return NULL;
	}
	
	n = lp->node_list + (lp->node_count++);
	n->magic = FED_NODEINFO_MAGIC;
	n->name[0] = '\0';
	n->adapter_list = (fed_adapter_t *)malloc(FED_MAXADAPTERS *
		sizeof(fed_adapter_t));
	
	return n;
}

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
	for(i = 0; i < l->node_count; i++) {
		memset(buf, 0, 3000);
		fed_print_nodeinfo(&l->node_list[i], buf, 3000);
		printf("%s", buf);
	}
	printf("--End libstate--\n");
}

/* Unpack nodeinfo and update persistent libstate.
 *
 * Used by: slurmctld
 */
int
fed_unpack_nodeinfo(fed_nodeinfo_t *n, Buf buf)
{
	int i, j;
	fed_adapter_t *tmp_a = NULL;
	fed_window_t *tmp_w = NULL;
	uint16_t size;
	fed_nodeinfo_t *tmp_n = NULL;
	char name[FED_HOSTLEN];
	int magic;
	
	/* NOTE!  We don't care at this point whether n is valid.
	 * If it's NULL, we will just forego the copy at the end.
	 */	
	assert(buf);
	
	/* Extract node name from buffer and update global libstate 
	 * with this nodes' info.
	 */
	safe_unpack32(&magic, buf);
	if(magic != FED_NODEINFO_MAGIC)
		slurm_seterrno_ret(EBADMAGIC_FEDNODEINFO);
	unpackmem(name, &size, buf);
	if(size != FED_HOSTLEN)
		goto unpack_error;
	_lock();
	tmp_n =_alloc_node(fed_state, name);
	_unlock();
	if(tmp_n == NULL)
		return SLURM_ERROR;
	tmp_n->magic = magic;
	strncpy(tmp_n->name, name, FED_HOSTLEN);
	safe_unpack32(&tmp_n->adapter_count, buf);
	for(i = 0; i < tmp_n->adapter_count; i++) {
		tmp_a = tmp_n->adapter_list + i;
		unpackmem(tmp_a->name, &size, buf);
		if(size != FED_ADAPTERLEN)
			goto unpack_error;
		safe_unpack16(&tmp_a->lid, buf);
		safe_unpack16(&tmp_a->network_id, buf);
		safe_unpack32(&tmp_a->max_winmem, buf);
		safe_unpack32(&tmp_a->min_winmem, buf);
		safe_unpack32(&tmp_a->avail_mem, buf);
		safe_unpack32(&tmp_a->window_count, buf);
		tmp_w = (fed_window_t *)malloc(sizeof(fed_window_t) * 
			tmp_a->window_count);
		if(!tmp_w)
			slurm_seterrno_ret(ENOMEM);
		for(j = 0; j < tmp_a->window_count; j++) {
			safe_unpack16(&tmp_w[j].id, buf);
			safe_unpack32(&tmp_w[j].status, buf);
		}
		tmp_a->window_list = tmp_w;
	}
	safe_unpack16(&tmp_n->next_window, buf);
	safe_unpack16(&tmp_n->next_adapter, buf);

	/* Only copy the node_info structure if the caller wants it */
	if(n != NULL)
		if(_copy_node(n, tmp_n) != SLURM_SUCCESS)
			return SLURM_ERROR;

	/* FIX ME!  We shouldn't be doing this every time! */
	fed_hash_nodes();

#if FED_DEBUG
	_print_libstate(fed_state);
#endif
	return SLURM_SUCCESS;
	
unpack_error:
	/* FIX ME!  Add code here to free allocated memory */
	if(tmp_w)
		free(tmp_w);
	slurm_seterrno_ret(EUNPACK);
}

/* Used by: slurmd, slurmctld */
static void
_free_adapter(fed_adapter_t *a)
{
	assert(a);
	assert(a->window_list);
	
	free(a->window_list);
	a->window_list = NULL;
	free(a);
	a = NULL;
}

/* Used by: slurmd, slurmctld */
void
fed_free_nodeinfo(fed_nodeinfo_t *n)
{
	fed_adapter_t *a;
	int i;
	
	if(!n)
		return;
	
	assert(n->magic == FED_NODEINFO_MAGIC);
	
	if(n->adapter_list) {
		a = n->adapter_list;
		for(i = 0; i < n->adapter_count; i++) {
			_free_adapter(a + i);
		}
	}
	free(n);
	n = NULL;
}

/* Assign a unique key to each job.  The key is used later to 
 * gain access to the network table loaded on each node of a job.
 *
 * Used by: slurmctld
 */
static uint16_t
_next_key(void)
{
	uint16_t key;
	
	assert(fed_state);
	
	_lock();
	key = fed_state->key_index++;
	_unlock();
	
	return key;
}

/* Given an index into a node's adapter list, return the lid of the 
 * corresponding adapter.
 *
 * Used by: slurmctld
 */
static int
_get_lid(fed_nodeinfo_t *np, int index)
{
	fed_adapter_t *ap = np->adapter_list;

	assert(np);
	assert(index >= 0);

	return ap[index].lid;
}

/* For a given node, fill out an NTBL
 * struct (an array of these makes up the network table loaded
 * for each job).  Assign adapters, lids and switch windows to
 * each task in a job.
 *
 * Used by: slurmctld
 */
static int
_setup_table_entry(NTBL *table_entry, char *host, int id)
{
	fed_nodeinfo_t *n;
	
	assert(host);
	assert(table_entry);
	
	n = _find_node(fed_state, host);
	if(n == NULL)
		return SLURM_ERROR;
	
	table_entry->task_id = id;
	table_entry->lid = _get_lid(n, n->next_adapter);
	
	/* FIX ME! Check to make sure that our chosen 
	 * window isn't in use
	 */
	table_entry->window_id = 
		n->adapter_list[n->next_adapter].window_list[n->next_window].id;

	/* FIX ME! Possible infinite loop.  Also,
	 * should be looking at window
	 * counts and ids returned from status calls
	 */
	do {	
		if(n->next_window++ >=
		n->adapter_list[n->next_adapter].window_count) {
			n->next_window = FED_MIN_WIN;
			n->next_adapter++;
			if(n->next_adapter >= n->adapter_count)
				n->next_adapter = 0;
		}
	} while(n->adapter_list->window_list[n->next_window].status
			!= NTBL_UNLOADED_STATE);

	/* FIX ME!  Need to mark the window as used, but 
	 * need to find a way to clear it again first...
	 */
#if 0
	n->adapter_list->window_list[n->next_window].status
		= NTBL_LOADED_STATE);
#endif
	return SLURM_SUCCESS;
}

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

/* Setup everything for the job.  Assign tasks across
 * nodes in a block or cyclic fashion and create the network table used
 * on all nodes of the job.
 *
 * Used by: slurmctld
 */
int
fed_build_jobinfo(fed_jobinfo_t *jp, hostlist_t hl, int nprocs, int cyclic)
{
	int nnodes;
	hostlist_iterator_t hi;
	char *host;
	int full_node_cnt;
	int min_procs_per_node;
	int max_procs_per_node;
	int proc_cnt = 0;
	int task_cnt;
	NTBL **tmp_table;
	int i, j;
	
	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);
	assert(!hostlist_is_empty(hl));

	if((nprocs <= 0) || (nprocs > FED_MAX_PROCS))
		slurm_seterrno_ret(EINVAL);

	tmp_table = malloc(sizeof(NTBL *) * nprocs);
	if(tmp_table == NULL)
		slurm_seterrno_ret(ENOMEM);
	
	jp->job_key = _next_key();
	/* FIX ME! skip setting job_desc for now, will default to
	 * "no_job_description_given".  Also, let the adapter
	 * determine our window memory size.
	 */
	jp->window_memory = FED_AUTO_WINMEM;	
		
	nnodes = hostlist_count(hl);
	full_node_cnt = nprocs % nnodes;
	min_procs_per_node = nprocs / nnodes;
	max_procs_per_node = (nprocs + nnodes - 1) / nnodes;
	
	hi = hostlist_iterator_create(hl);

	if(cyclic) {
		// allocate 1 window per node
		debug("Allocating windows in cyclic mode");
		hostlist_iterator_reset(hi);
		while(proc_cnt < nprocs) {
			host = hostlist_next(hi);
			if(!host) {
				hostlist_iterator_reset(hi);
				host = hostlist_next(hi);
			}	
			tmp_table[proc_cnt] = malloc(sizeof(NTBL));
			if(_setup_table_entry(tmp_table[proc_cnt], host, proc_cnt)) {
				free(tmp_table);
				slurm_seterrno_ret(EWINDOW);
			}
			proc_cnt++;
			free(host);
		}
	} else {
		// allocate windows up to max_procs_per_node
		debug("Allocating windows in block mode");
		hostlist_iterator_reset(hi);
		for(i = 0; i < nnodes; i++) {
			host = hostlist_next(hi);
			if(!host)
				error("Failed to get next host");
	
			if(i < full_node_cnt)
				task_cnt = max_procs_per_node;
			else
				task_cnt = min_procs_per_node;
			
			for (j = 0; j < task_cnt; j++) {
				tmp_table[proc_cnt] = malloc(sizeof(NTBL));
				if(_setup_table_entry(tmp_table[proc_cnt], host, 
					proc_cnt)) {
					free(tmp_table);
					slurm_seterrno_ret(EWINDOW);
				}
				proc_cnt++;
			}
			free(host);
		}
	}	

	jp->table_size = nprocs;
	jp->table = tmp_table;
#if FED_DEBUG
	_print_table(jp->table, jp->table_size);
#endif
			
	return SLURM_SUCCESS;
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
	pack32(j->table_size, buf);
	for(i = 0; i < j->table_size; i++) {
		pack16(j->table[i]->task_id, buf);
		pack16(j->table[i]->lid, buf);
		pack16(j->table[i]->window_id, buf);
	}
		
	return SLURM_SUCCESS;
}

/* Used by: all */
int 
fed_unpack_jobinfo(fed_jobinfo_t *j, Buf buf)
{
	uint16_t size = DESCLEN;
	NTBL **tmp;
	int i;
	
	assert(j);
	assert(j->magic == FED_JOBINFO_MAGIC);
	assert(buf);
	
	safe_unpack32(&j->magic, buf);
	assert(j->magic == FED_JOBINFO_MAGIC);
	safe_unpack16(&j->job_key, buf);
	unpackmem(j->job_desc, &size, buf);
	if(size != DESCLEN)
		goto unpack_error;
	safe_unpack32(&j->window_memory, buf);
	safe_unpack32(&j->table_size, buf);
	tmp = (NTBL **)malloc(sizeof(NTBL *) * j->table_size);
	if(!tmp)
		slurm_seterrno_ret(ENOMEM);
	for(i = 0; i < j->table_size; i++) {
		tmp[i] = malloc(sizeof(NTBL));
		safe_unpack16(&tmp[i]->task_id, buf);
		safe_unpack16(&tmp[i]->lid, buf);
		safe_unpack16(&tmp[i]->window_id, buf);
	}
	j->table = tmp;
	
	return SLURM_SUCCESS;
	
unpack_error:
	slurm_seterrno_ret(EUNPACK);
	return SLURM_ERROR;
}

/* Used by: all */
fed_jobinfo_t *
fed_copy_jobinfo(fed_jobinfo_t *j)
{
	fed_jobinfo_t *new;
	NTBL **tmp;
	int i;
	
	if(fed_alloc_jobinfo(&new))
		return NULL;
	memcpy(new, j, sizeof(fed_jobinfo_t));
	tmp = (NTBL **)malloc(sizeof(NTBL *) * new->table_size);
	if(tmp == NULL) {
		fed_free_jobinfo(new);
		slurm_seterrno(ENOMEM);
		return NULL;
	}
	for(i = 0; i < new->table_size; i++) {
		tmp[i] = (NTBL *)malloc(sizeof(NTBL));
		memcpy(tmp[i], j->table[i], sizeof(NTBL));
	}
	new->table = tmp;
	
	return new;	
}

/* Used by: all */
void
fed_free_jobinfo(fed_jobinfo_t *jp)
{
	int i;

	if(!jp)
		return;

	if(jp->table) {
		for(i = 0; i < jp->table_size; i++) {
			if(!jp->table[i])
				free(jp->table[i]);
		}
	}
	free(jp);
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
	NTBL **table = (NTBL **)data;
	int *job_key = (int *)data;
	
	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);

	switch(key) {
	case FED_JOBINFO_TABLE:
		*table = jp->table;
		break;
	case FED_JOBINFO_KEY:
		*job_key = jp->job_key;
		break;
	default:
		slurm_seterrno_ret(EINVAL);
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
	char *adapter, *old_adapter;
	unsigned long long winmem;
	char buf[2000];
	
	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);
#if FED_DEBUG
	_print_table(jp->table, jp->table_size);
	printf("%s", fed_sprint_jobinfo(jp, buf, 2000));
#endif
	for(i = 0; i < jp->table_size; i++) {
		adapter = _get_adapter_from_lid(jp->table[i]->lid);
		/* FIX ME!  This is a crude check to see if we have already 
		 * loaded a table for this adapter (and therefore don't need 
		 * to do it again).  Make this better.
		 */
		if((adapter == NULL) || (adapter == old_adapter))
			continue;
		winmem = jp->window_memory;
		err = ntbl_load_table(
			NTBL_VERSION,
			adapter,
			1,	/* network_id:  just hardcode 1 for now */
			uid,
			pid,
			jp->job_key,
			jp->job_desc,
			&winmem,
			jp->table_size,
			jp->table);
		if(err != NTBL_SUCCESS) {
			error("unable to load table %s\n", 
				_lookup_fed_status_tab(err));
			return SLURM_ERROR;
		}
		old_adapter = adapter;
	}
	
	return SLURM_SUCCESS;
}

/* Assumes that, on error, new switch state information will be
 * read from node.
 *
 * Used by: slurmd
 */
int
fed_unload_table(fed_jobinfo_t *jp)
{
	int i;
	int err;
	char *adapter;
	
	assert(jp);
	assert(jp->magic == FED_JOBINFO_MAGIC);
	for(i = 0; i < jp->table_size; i++) {
		adapter = _get_adapter_from_lid(jp->table[i]->lid);
		if(adapter == NULL)
			continue;
		err = ntbl_unload_window(NTBL_VERSION, adapter, jp->job_key,
			jp->table[i]->window_id);
		if(err != NTBL_SUCCESS) {
			error("unloading window: %s\n",
				_lookup_fed_status_tab(err));
			slurm_seterrno_ret(EUNLOAD);
		}
	}
	
	return SLURM_SUCCESS;
}

int
fed_alloc_libstate(fed_libstate_t **l)
{
	fed_libstate_t *tmp;
	
	assert(l);
	
	tmp = (fed_libstate_t *)malloc(sizeof(fed_libstate_t));
	if(!tmp)
		slurm_seterrno_ret(ENOMEM);
	tmp->magic = FED_LIBSTATE_MAGIC;
	tmp->node_count = 0;
	*l = tmp;
	
	return SLURM_SUCCESS;
}

/* Used by: slurmctld */
static void
_copy_libstate(fed_libstate_t *dest, fed_libstate_t *src)
{
	int i;
	int err;
	fed_nodeinfo_t *tmp;
	
	assert(dest);
	assert(src);
	assert(dest->magic == FED_LIBSTATE_MAGIC);
	assert(src->magic == FED_LIBSTATE_MAGIC);
	assert(dest->node_count == 0);
	
	/* note:  dest->node_count set by alloc_node */
	for(i = 0; i < src->node_count; i++) {
		tmp = _alloc_node(dest, NULL); 
		err = _copy_node(tmp, &src->node_list[i]);
		if(err != SLURM_SUCCESS) {
			error("_copy_libstate: %m");
			break;
		}
	}
	dest->key_index = src->key_index;
}

/* Needs to be called before any using any functions that 
 * manipulate persistent libstate.
 *
 * Used by: slurmctld
 */
int
fed_init(fed_libstate_t *l)
{
	int err;
	fed_libstate_t *tmp;

	assert(!fed_state);

	err = fed_alloc_libstate(&tmp);
	if(err != SLURM_SUCCESS)
		return err;
	if(l) {
		assert(l->magic == FED_LIBSTATE_MAGIC);
		_copy_libstate(tmp, l); 
		
	} else {
		tmp->node_count = 0;
		tmp->node_list = NULL;
		tmp->hash_table = NULL;
		tmp->key_index = 1;
	}
	_lock();
	fed_state = tmp;
	_unlock();
	
	return SLURM_SUCCESS;
}

/* Used by: slurmctld */
void
fed_free_libstate(fed_libstate_t *lp)
{
	int i;
	
	if(!lp)
		return;
	for(i = 0; i < lp->node_count; i++)
		fed_free_nodeinfo(&lp->node_list[i]);
	if(lp->hash_table != NULL)
		free(lp->hash_table);
	free(lp);
}

/* Used by: slurmctld */
void
fed_destroy(fed_libstate_t *lp)
{
	assert(fed_state);
	
	_lock();
	if(lp)
		_copy_libstate(lp, fed_state);
	if(fed_state)
		fed_free_libstate(fed_state);
	fed_state = NULL;
	_unlock();
}

/* Used by: slurmctld */
int
fed_pack_libstate(fed_libstate_t *lp, Buf buffer)
{
	int offset;
	int i;
	
	assert(lp);
	assert(lp->magic == FED_LIBSTATE_MAGIC);
	
	offset = get_buf_offset(buffer);
	pack32(lp->magic, buffer);
	pack32(lp->node_count, buffer);
	for(i = 0; i < lp->node_count; i++) {
		(void)fed_pack_nodeinfo(&lp->node_list[i], buffer);
	}
	/* don't pack hash_table, we'll just rebuild on restore */
	pack16(lp->key_index, buffer);
	
	return(get_buf_offset(buffer) - offset);
}

/* Used by: slurmctld */
int
fed_unpack_libstate(fed_libstate_t *lp, Buf buffer)
{
	int offset;
	int i;
	
	assert(lp->magic == FED_LIBSTATE_MAGIC);
	
	offset = get_buf_offset(buffer);
	
	safe_unpack32(&lp->magic, buffer);
	safe_unpack32(&lp->node_count, buffer);
	for(i = 0; i < lp->node_count; i ++) {
		(void)fed_unpack_nodeinfo(NULL, buffer);
	}
	safe_unpack16(&lp->key_index, buffer);
	
	return SLURM_SUCCESS;

unpack_error:
	slurm_seterrno_ret(EBADMAGIC_FEDLIBSTATE); /* corrupted libstate */
	return SLURM_ERROR;
}

