/*****************************************************************************\
 *  switch_generic.c - Library for managing a generic switch resources.
 *                     Can be used to optimize network communications for
 *                     parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if !defined(__FreeBSD__)
#include <net/if.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/xmalloc.h"

#define SW_GEN_HASH_MAX		1000
#define SW_GEN_LIBSTATE_MAGIC	0x3b287d0c
#define SW_GEN_NODE_INFO_MAGIC	0x3b38ac0c
#define SW_GEN_STEP_INFO_MAGIC	0x58ae93cb

/* Change GEN_STATE_VERSION value when changing the state save format */
#define GEN_STATE_VERSION      "NRT001"

typedef struct sw_gen_ifa {
	char *ifa_name;		/* "eth0", "ib1", etc. */
	char *ifa_family;	/* "AF_INET" or "AF_INET6" */
	char *ifa_addr;		/* output from inet_ntop */
} sw_gen_ifa_t;
typedef struct sw_gen_node_info {
	uint32_t magic;
	uint16_t ifa_cnt;
	sw_gen_ifa_t **ifa_array;
	char *node_name;
	struct sw_gen_node_info *next;	/* used for hash table */
} sw_gen_node_info_t;

typedef struct sw_gen_node {
	char *node_name;
	uint16_t ifa_cnt;
	sw_gen_ifa_t **ifa_array;
} sw_gen_node_t;
typedef struct sw_gen_step_info {
	uint32_t magic;
	uint32_t node_cnt;
	sw_gen_node_t **node_array;
} sw_gen_step_info_t;

typedef struct sw_gen_libstate {
	uint32_t magic;
	uint32_t node_count;
	uint32_t hash_max;
	sw_gen_node_info_t **hash_table;
} sw_gen_libstate_t;

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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "switch generic plugin";
const char plugin_type[]        = "switch/generic";
const uint32_t plugin_version   = 110;

uint64_t debug_flags = 0;
pthread_mutex_t	global_lock = PTHREAD_MUTEX_INITIALIZER;
sw_gen_libstate_t *libstate = NULL;

extern int switch_p_free_node_info(switch_node_info_t **switch_node);
extern int switch_p_alloc_node_info(switch_node_info_t **switch_node);

/* The _lock() and _unlock() functions are used to lock/unlock a
 * global mutex.  Used to serialize access to the global library
 * state variable nrt_state.
 */
static void _lock(void)
{
	int err = 1;

	while (err) {
		err = pthread_mutex_lock(&global_lock);
	}
}

static void _unlock(void)
{
	int err = 1;

	while (err) {
		err = pthread_mutex_unlock(&global_lock);
	}
}

static void
_alloc_libstate(void)
{
	xassert(!libstate);

	libstate = xmalloc(sizeof(sw_gen_libstate_t));
	libstate->magic = SW_GEN_LIBSTATE_MAGIC;
	libstate->node_count = 0;
	libstate->hash_max = SW_GEN_HASH_MAX;
	libstate->hash_table = xmalloc(sizeof(sw_gen_node_info_t *) *
				       libstate->hash_max);
}

static void
_free_libstate(void)
{
	sw_gen_node_info_t *node_ptr, *next_node_ptr;
	int i;

	if (!libstate)
		return;
	xassert(libstate->magic == SW_GEN_LIBSTATE_MAGIC);
	for (i = 0; i < libstate->hash_max; i++) {
		node_ptr = libstate->hash_table[i];
		while (node_ptr) {
			next_node_ptr = node_ptr->next;
			(void) switch_p_free_node_info((switch_node_info_t **)
						       &node_ptr);
			node_ptr = next_node_ptr;
		}
	}
	libstate->magic = 0;
	xfree(libstate->hash_table);
	xfree(libstate);
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
	index %= libstate->hash_max;

	return index;
}

/* Tries to find a node fast using the hash table
 *
 * Used by: slurmctld
 */
static sw_gen_node_info_t *
_find_node(char *node_name)
{
	int i;
	sw_gen_node_info_t *n;
	struct node_record *node_ptr;

	if (node_name == NULL) {
		error("%s: _find_node node name is NULL", plugin_type);
		return NULL;
	}
	if (libstate->node_count == 0)
		return NULL;
	xassert(libstate->magic == SW_GEN_LIBSTATE_MAGIC);
	if (libstate->hash_table) {
		i = _hash_index(node_name);
		n = libstate->hash_table[i];
		while (n) {
			xassert(n->magic == SW_GEN_NODE_INFO_MAGIC);
			if (!strcmp(n->node_name, node_name))
				return n;
			n = n->next;
		}
	}

	/* This code is only needed if NodeName and NodeHostName differ */
	node_ptr = find_node_record(node_name);
	if (node_ptr && libstate->hash_table) {
		i = _hash_index(node_ptr->node_hostname);
		n = libstate->hash_table[i];
		while (n) {
			xassert(n->magic == SW_GEN_NODE_INFO_MAGIC);
			if (!strcmp(n->node_name, node_name))
				return n;
			n = n->next;
		}
	}

	return NULL;
}

/* Add the hash entry for a newly created node record */
static void
_hash_add_nodeinfo(sw_gen_node_info_t *new_node_info)
{
	int index;

	xassert(libstate);
	xassert(libstate->hash_table);
	xassert(libstate->hash_max >= libstate->node_count);
	xassert(libstate->magic == SW_GEN_LIBSTATE_MAGIC);
	if (!new_node_info->node_name || !new_node_info->node_name[0])
		return;
	index = _hash_index(new_node_info->node_name);
	new_node_info->next = libstate->hash_table[index];
	libstate->hash_table[index] = new_node_info;
	libstate->node_count++;
}

/* Add the new node information to our libstate cache, making a copy if
 * information is new. Otherwise, swap the data and return to the user old
 * data, which is fine in this case since it is only deleted by slurmctld */
static void _cache_node_info(sw_gen_node_info_t *new_node_info)
{
	sw_gen_node_info_t *old_node_info;
	uint16_t ifa_cnt;
	sw_gen_ifa_t **ifa_array;
	struct sw_gen_node_info *next;
	bool new_alloc;      /* True if this is new node to be added to cache */

	_lock();
	old_node_info = _find_node(new_node_info->node_name);
	new_alloc = (old_node_info == NULL);
	if (new_alloc) {
		(void) switch_p_alloc_node_info((switch_node_info_t **)
						&old_node_info);
		old_node_info->node_name = xstrdup(new_node_info->node_name);
	}

	/* Swap contents */
	ifa_cnt   = old_node_info->ifa_cnt;
	ifa_array = old_node_info->ifa_array;
	next      = old_node_info->next;
	old_node_info->ifa_cnt   = new_node_info->ifa_cnt;
	old_node_info->ifa_array = new_node_info->ifa_array;
	old_node_info->next      = new_node_info->next;
	new_node_info->ifa_cnt   = ifa_cnt;
	new_node_info->ifa_array = ifa_array;
	new_node_info->next      = next;

	if (new_alloc)
		_hash_add_nodeinfo(old_node_info);
	_unlock();
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init(void)
{
	debug("%s loaded", plugin_name);
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

int fini(void)
{
	_lock();
	_free_libstate();
	_unlock();
	return SLURM_SUCCESS;
}

extern int switch_p_reconfig(void)
{
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save(char * dir_name)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_libstate_save() starting");
	/* No state saved or restored for this plugin */
	return SLURM_SUCCESS;
}

int switch_p_libstate_restore(char * dir_name, bool recover)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_libstate_restore() starting");
	/* No state saved or restored for this plugin, just initialize */
	_lock();
	_alloc_libstate();
	_unlock();

	return SLURM_SUCCESS;
}

int switch_p_libstate_clear(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_libstate_clear() starting");
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo(switch_jobinfo_t **switch_job,
			   uint32_t job_id, uint32_t step_id)
{
	sw_gen_step_info_t *gen_step_info;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_alloc_jobinfo() starting");
	xassert(switch_job);
	gen_step_info = xmalloc(sizeof(sw_gen_step_info_t));
	gen_step_info->magic = SW_GEN_STEP_INFO_MAGIC;
	*switch_job = (switch_jobinfo_t *) gen_step_info;

	return SLURM_SUCCESS;
}

int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
			   slurm_step_layout_t *step_layout, char *network)
{
	sw_gen_step_info_t *gen_step_info = (sw_gen_step_info_t *) switch_job;
	sw_gen_node_info_t *gen_node_info;
	sw_gen_node_t *node_ptr;
	hostlist_t hl = NULL;
	hostlist_iterator_t hi;
	char *host = NULL;
	int i, j;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_build_jobinfo() starting");
	xassert(gen_step_info);
	xassert(gen_step_info->magic == SW_GEN_STEP_INFO_MAGIC);
	hl = hostlist_create(step_layout->node_list);
	if (!hl)
		fatal("hostlist_create(%s): %m", step_layout->node_list);
	gen_step_info->node_cnt = hostlist_count(hl);
	gen_step_info->node_array = xmalloc(sizeof(sw_gen_node_t *) *
					    gen_step_info->node_cnt);
	hi = hostlist_iterator_create(hl);
	for (i = 0; (host = hostlist_next(hi)); i++) {
		node_ptr = xmalloc(sizeof(sw_gen_node_t));
		gen_step_info->node_array[i] = node_ptr;
		node_ptr->node_name = xstrdup(host);
		gen_node_info = _find_node(host);
		if (gen_node_info) {	/* Copy node info to this step */
			node_ptr->ifa_cnt = gen_node_info->ifa_cnt;
			node_ptr->ifa_array = xmalloc(sizeof(sw_gen_node_t *) *
						      node_ptr->ifa_cnt);
			for (j = 0; j < node_ptr->ifa_cnt; j++) {
				node_ptr->ifa_array[j] =
					xmalloc(sizeof(sw_gen_node_t));
				node_ptr->ifa_array[j]->ifa_addr = xstrdup(
					gen_node_info->ifa_array[j]->ifa_addr);
				node_ptr->ifa_array[j]->ifa_family = xstrdup(
					gen_node_info->ifa_array[j]->ifa_family);
				node_ptr->ifa_array[j]->ifa_name = xstrdup(
					gen_node_info->ifa_array[j]->ifa_name);
			}
		}
		free(host);
	}
	hostlist_iterator_destroy(hi);
	hostlist_destroy(hl);

	return SLURM_SUCCESS;
}

void switch_p_free_jobinfo(switch_jobinfo_t *switch_job)
{
	sw_gen_step_info_t *gen_step_info = (sw_gen_step_info_t *) switch_job;
	sw_gen_node_t *node_ptr;
	sw_gen_ifa_t *ifa_ptr;
	int i, j;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_free_jobinfo() starting");
	xassert(gen_step_info);
	xassert(gen_step_info->magic == SW_GEN_STEP_INFO_MAGIC);
	for (i = 0; i < gen_step_info->node_cnt; i++) {
		node_ptr = gen_step_info->node_array[i];
		xfree(node_ptr->node_name);
		for (j = 0; j < node_ptr->ifa_cnt; j++) {
			ifa_ptr = node_ptr->ifa_array[j];
			xfree(ifa_ptr->ifa_addr);
			xfree(ifa_ptr->ifa_family);
			xfree(ifa_ptr->ifa_name);
			xfree(ifa_ptr);
		}
	}
	xfree(gen_step_info->node_array);
	xfree(gen_step_info);

	return;
}

int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
			  uint16_t protocol_version)
{
	sw_gen_step_info_t *gen_step_info = (sw_gen_step_info_t *) switch_job;
	sw_gen_node_t *node_ptr;
	sw_gen_ifa_t *ifa_ptr;
	int i, j;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_pack_jobinfo() starting");
	xassert(gen_step_info);
	xassert(gen_step_info->magic == SW_GEN_STEP_INFO_MAGIC);

	pack32(gen_step_info->node_cnt, buffer);
	for (i = 0; i < gen_step_info->node_cnt; i++) {
		node_ptr = gen_step_info->node_array[i];
		packstr(node_ptr->node_name, buffer);
		pack16(node_ptr->ifa_cnt, buffer);
		for (j = 0; j < node_ptr->ifa_cnt; j++) {
			ifa_ptr = node_ptr->ifa_array[j];
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("node=%s name=%s family=%s addr=%s",
				     node_ptr->node_name, ifa_ptr->ifa_name,
				     ifa_ptr->ifa_family, ifa_ptr->ifa_addr);
			}
			packstr(ifa_ptr->ifa_addr, buffer);
			packstr(ifa_ptr->ifa_family, buffer);
			packstr(ifa_ptr->ifa_name, buffer);
		}
	}

	return SLURM_SUCCESS;
}

int switch_p_unpack_jobinfo(switch_jobinfo_t *switch_job, Buf buffer,
			    uint16_t protocol_version)
{
	sw_gen_step_info_t *gen_step_info = (sw_gen_step_info_t *) switch_job;
	sw_gen_node_t *node_ptr;
	sw_gen_ifa_t *ifa_ptr;
	uint32_t uint32_tmp;
	int i, j;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_unpack_jobinfo() starting");
	safe_unpack32(&gen_step_info->node_cnt, buffer);
	gen_step_info->node_array = xmalloc(sizeof(sw_gen_node_t *) *
					    gen_step_info->node_cnt);
	for (i = 0; i < gen_step_info->node_cnt; i++) {
		node_ptr = xmalloc(sizeof(sw_gen_node_t));
		gen_step_info->node_array[i] = node_ptr;
		safe_unpackstr_xmalloc(&node_ptr->node_name, &uint32_tmp,
				       buffer);
		safe_unpack16(&node_ptr->ifa_cnt, buffer);
		node_ptr->ifa_array = xmalloc(sizeof(sw_gen_ifa_t *) *
					      node_ptr->ifa_cnt);
		for (j = 0; j < node_ptr->ifa_cnt; j++) {
			ifa_ptr = xmalloc(sizeof(sw_gen_ifa_t));
			node_ptr->ifa_array[j] = ifa_ptr;
			safe_unpackstr_xmalloc(&ifa_ptr->ifa_addr, &uint32_tmp,
					       buffer);
			safe_unpackstr_xmalloc(&ifa_ptr->ifa_family,
					       &uint32_tmp, buffer);
			safe_unpackstr_xmalloc(&ifa_ptr->ifa_name, &uint32_tmp,
					       buffer);
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("node=%s name=%s family=%s addr=%s",
				     node_ptr->node_name, ifa_ptr->ifa_name,
				     ifa_ptr->ifa_family, ifa_ptr->ifa_addr);
			}
		}
	}

	return SLURM_SUCCESS;

unpack_error:
	for (i = 0; i < gen_step_info->node_cnt; i++) {
		node_ptr = gen_step_info->node_array[i];
		for (j = 0; j < node_ptr->ifa_cnt; j++) {
			ifa_ptr = node_ptr->ifa_array[j];
			xfree(ifa_ptr->ifa_addr);
			xfree(ifa_ptr->ifa_family);
			xfree(ifa_ptr->ifa_name);
			xfree(ifa_ptr);
		}
		xfree(node_ptr->ifa_array);
		xfree(node_ptr->node_name);
		xfree(node_ptr);
	}
	gen_step_info->node_cnt = 0;
	xfree(gen_step_info->node_array);
	return SLURM_ERROR;
}

void switch_p_print_jobinfo(FILE *fp, switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_print_jobinfo() starting");
	return;
}

char *switch_p_sprint_jobinfo(switch_jobinfo_t *switch_jobinfo, char *buf,
			      size_t size)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_sprint_jobinfo() starting");
	if ((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}
	return NULL;
}

/*
 * switch functions for job initiation
 */
int switch_p_node_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_node_init() starting");
	return SLURM_SUCCESS;
}

int switch_p_node_fini(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_node_fini() starting");
	return SLURM_SUCCESS;
}

int switch_p_job_preinit(switch_jobinfo_t *switch_job)
{
	sw_gen_step_info_t *gen_step_info = (sw_gen_step_info_t *) switch_job;
	sw_gen_node_t *node_ptr;
	sw_gen_ifa_t *ifa_ptr;
	int i, j;

	if (debug_flags & DEBUG_FLAG_SWITCH) {
		info("switch_p_job_preinit() starting");

		for (i = 0; i < gen_step_info->node_cnt; i++) {
			node_ptr = gen_step_info->node_array[i];
			for (j = 0; j < node_ptr->ifa_cnt; j++) {
				ifa_ptr = node_ptr->ifa_array[j];
				info("node=%s name=%s family=%s addr=%s",
				     node_ptr->node_name, ifa_ptr->ifa_name,
				     ifa_ptr->ifa_family, ifa_ptr->ifa_addr);
			}
		}
	}

	return SLURM_SUCCESS;
}

extern int switch_p_job_init(stepd_step_rec_t *job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_suspend_test(switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_test() starting");
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_get(switch_jobinfo_t *jobinfo,
					  void **suspend_info)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_get() starting");
	return;
}

extern void switch_p_job_suspend_info_pack(void *suspend_info, Buf buffer,
					   uint16_t protocol_version)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_pack() starting");
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, Buf buffer,
					    uint16_t protocol_version)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_unpack() starting");
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_free(void *suspend_info)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend_info_free() starting");
	return;
}

extern int switch_p_job_suspend(void *suspend_info, int max_wait)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_suspend() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_resume(void *suspend_info, int max_wait)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_resume() starting");
	return SLURM_SUCCESS;
}

int switch_p_job_fini(switch_jobinfo_t *jobinfo)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_fini() starting");
	return SLURM_SUCCESS;
}

int switch_p_job_postfini(stepd_step_rec_t *job)
{
	uid_t pgid = job->jmgr_pid;
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_postfini() starting");
	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu",
			(unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		debug("Job %u.%u: Bad pid valud %lu", job->jobid,
		      job->stepid, (unsigned long) pgid);

	return SLURM_SUCCESS;
}

int switch_p_job_attach(switch_jobinfo_t *jobinfo, char ***env,
			uint32_t nodeid, uint32_t procid, uint32_t nnodes,
			uint32_t nprocs, uint32_t rank)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_attach() starting");
	return SLURM_SUCCESS;
}

/*
 * Allocates network information in resulting_data with xmalloc
 * String result of format : (nodename,(iface,IP_V{4,6},address)*)
 */
extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job,
								int key, void *resulting_data)
{
	int node_id = key;
	sw_gen_step_info_t *stepinfo = (sw_gen_step_info_t*) switch_job;
	sw_gen_node_t *node_ptr = stepinfo->node_array[node_id];
	sw_gen_ifa_t *ifa_ptr;
	int i, s;
	int bufsize = 1024;
	char *buf = xmalloc(bufsize);

#if defined(__FreeBSD__)
#define IFNAMSIZ 16
#endif
	int triplet_len_max = IFNAMSIZ + INET6_ADDRSTRLEN + 5 + 5 + 1;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_get_jobinfo() starting");

	if (!resulting_data) {
		error("no pointer for resulting_data");
		return SLURM_ERROR;
	}

	*(char **) resulting_data = NULL;

	if (node_id < 0 || node_id >= stepinfo->node_cnt) {
		error("node_id out of range");
		return SLURM_ERROR;
	}

	s = snprintf(buf, bufsize, "(%s", node_ptr->node_name);
	/* appends in buf triplets (ifname,ipversion,address) */
	for (i = 0; i < node_ptr->ifa_cnt; i++) {
		ifa_ptr = node_ptr->ifa_array[i];
		if (s + triplet_len_max > bufsize) {
			bufsize *= 2;
			xrealloc(buf, bufsize);
		}
		s += snprintf(buf+s, bufsize-s, ",(%s,%s,%s)",
			      ifa_ptr->ifa_name, ifa_ptr->ifa_family,
			      ifa_ptr->ifa_addr);
	}
	snprintf(buf+s, bufsize-s, ")");

	*(char **)resulting_data = buf; /* return x-alloc'ed data */

	return SLURM_SUCCESS;
}

/*
 * switch functions for other purposes
 */
extern int switch_p_get_errno(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_get_errno() starting");
	return SLURM_SUCCESS;
}

extern char *switch_p_strerror(int errnum)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_strerror() starting");
	return NULL;
}

/*
 * node switch state monitoring functions
 * required for IBM Federation switch
 */
extern int switch_p_clear_node_state(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_clear_node_state() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_alloc_node_info(switch_node_info_t **switch_node)
{
	sw_gen_node_info_t *gen_node_info;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_alloc_node_info() starting");
	xassert(switch_node);
	gen_node_info = xmalloc(sizeof(sw_gen_node_info_t));
	gen_node_info->magic = SW_GEN_NODE_INFO_MAGIC;
	*switch_node = (switch_node_info_t *) gen_node_info;

	return SLURM_SUCCESS;
}

extern int switch_p_build_node_info(switch_node_info_t *switch_node)
{
	sw_gen_node_info_t *gen_node_info = (sw_gen_node_info_t *) switch_node;
	struct ifaddrs *if_array = NULL, *if_rec;
	sw_gen_ifa_t *ifa_ptr;
	void *addr_ptr = NULL;
	char addr_str[INET6_ADDRSTRLEN], *ip_family;
	char hostname[256], *tmp;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_build_node_info() starting");
	xassert(gen_node_info);
	xassert(gen_node_info->magic == SW_GEN_NODE_INFO_MAGIC);
	if (gethostname(hostname, sizeof(hostname)) < 0)
		return SLURM_ERROR;
	/* remove the domain portion, if necessary */
	tmp = strstr(hostname, ".");
	if (tmp)
		*tmp = '\0';
	gen_node_info->node_name = xstrdup(hostname);
	if (getifaddrs(&if_array) == 0) {
		for (if_rec = if_array; if_rec; if_rec = if_rec->ifa_next) {
			if (!if_rec->ifa_addr->sa_data)
				continue;
#if !defined(__FreeBSD__)
	   		if (if_rec->ifa_flags & IFF_LOOPBACK)
				continue;
#endif
			if (if_rec->ifa_addr->sa_family == AF_INET) {
				addr_ptr = &((struct sockaddr_in *)
						if_rec->ifa_addr)->sin_addr;
				ip_family = "IP_V4";
			} else if (if_rec->ifa_addr->sa_family == AF_INET6) {
				addr_ptr = &((struct sockaddr_in6 *)
						if_rec->ifa_addr)->sin6_addr;
				ip_family = "IP_V6";
			} else {
				/* AF_PACKET (statistics) and others ignored */
				continue;
			}
			(void) inet_ntop(if_rec->ifa_addr->sa_family,
					 addr_ptr, addr_str, sizeof(addr_str));
			xrealloc(gen_node_info->ifa_array,
				 sizeof(sw_gen_ifa_t *) *
				        (gen_node_info->ifa_cnt + 1));
			ifa_ptr = xmalloc(sizeof(sw_gen_ifa_t));
			ifa_ptr->ifa_addr   = xstrdup(addr_str);
			ifa_ptr->ifa_family = xstrdup(ip_family);
			ifa_ptr->ifa_name   = xstrdup(if_rec->ifa_name);
			gen_node_info->ifa_array[gen_node_info->ifa_cnt++] =
				ifa_ptr;
			if (debug_flags & DEBUG_FLAG_SWITCH) {
				info("%s: name=%s ip_family=%s address=%s",
				     plugin_type, if_rec->ifa_name, ip_family,
				     addr_str);
			}
		}
	}
	freeifaddrs(if_array);

	return SLURM_SUCCESS;
}

extern int switch_p_pack_node_info(switch_node_info_t *switch_node,
				   Buf buffer, uint16_t protocol_version)
{
	sw_gen_node_info_t *gen_node_info = (sw_gen_node_info_t *) switch_node;
	sw_gen_ifa_t *ifa_ptr;
	int i;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_pack_node_info() starting");
	xassert(gen_node_info);
	xassert(gen_node_info->magic == SW_GEN_NODE_INFO_MAGIC);
	pack16(gen_node_info->ifa_cnt, buffer);
	packstr(gen_node_info->node_name,    buffer);
	for (i = 0; i < gen_node_info->ifa_cnt; i++) {
		ifa_ptr = gen_node_info->ifa_array[i];
		packstr(ifa_ptr->ifa_addr,   buffer);
		packstr(ifa_ptr->ifa_family, buffer);
		packstr(ifa_ptr->ifa_name,   buffer);
	}

	return SLURM_SUCCESS;
}

extern int switch_p_unpack_node_info(switch_node_info_t *switch_node,
				     Buf buffer, uint16_t protocol_version)
{
	sw_gen_node_info_t *gen_node_info = (sw_gen_node_info_t *) switch_node;
	sw_gen_ifa_t *ifa_ptr;
	uint32_t uint32_tmp;
	int i;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_unpack_node_info() starting");
	safe_unpack16(&gen_node_info->ifa_cnt, buffer);
	gen_node_info->ifa_array = xmalloc(sizeof(sw_gen_ifa_t *) *
					   gen_node_info->ifa_cnt);
	safe_unpackstr_xmalloc(&gen_node_info->node_name, &uint32_tmp,
			       buffer);
	for (i = 0; i < gen_node_info->ifa_cnt; i++) {
		ifa_ptr = xmalloc(sizeof(sw_gen_ifa_t));
		gen_node_info->ifa_array[i] = ifa_ptr;
		safe_unpackstr_xmalloc(&ifa_ptr->ifa_addr, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&ifa_ptr->ifa_family, &uint32_tmp,
				       buffer);
		safe_unpackstr_xmalloc(&ifa_ptr->ifa_name, &uint32_tmp, buffer);
		if (debug_flags & DEBUG_FLAG_SWITCH) {
			info("%s: node=%s name=%s ip_family=%s address=%s",
			     plugin_type, gen_node_info->node_name,
			     ifa_ptr->ifa_name, ifa_ptr->ifa_family,
			     ifa_ptr->ifa_addr);
		}
	}

	_cache_node_info(gen_node_info);

	return SLURM_SUCCESS;

unpack_error:
	for (i = 0; i < gen_node_info->ifa_cnt; i++) {
		xfree(gen_node_info->ifa_array[i]->ifa_addr);
		xfree(gen_node_info->ifa_array[i]->ifa_family);
		xfree(gen_node_info->ifa_array[i]->ifa_name);
		xfree(gen_node_info->ifa_array[i]);
	}
	xfree(gen_node_info->ifa_array);
	xfree(gen_node_info->node_name);
	gen_node_info->ifa_cnt = 0;
	return SLURM_ERROR;
}

extern int switch_p_free_node_info(switch_node_info_t **switch_node)
{
	sw_gen_node_info_t *gen_node_info = (sw_gen_node_info_t *) *switch_node;
	int i;

	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_free_node_info() starting");
	xassert(gen_node_info);
	xassert(gen_node_info->magic == SW_GEN_NODE_INFO_MAGIC);
	for (i = 0; i < gen_node_info->ifa_cnt; i++) {
		xfree(gen_node_info->ifa_array[i]->ifa_addr);
		xfree(gen_node_info->ifa_array[i]->ifa_family);
		xfree(gen_node_info->ifa_array[i]->ifa_name);
		xfree(gen_node_info->ifa_array[i]);
	}
	xfree(gen_node_info->ifa_array);
	xfree(gen_node_info->node_name);
	xfree(gen_node_info);

	return SLURM_SUCCESS;
}

extern char *switch_p_sprintf_node_info(switch_node_info_t *switch_node,
				        char *buf, size_t size)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_sprintf_node_info() starting");

	if ((buf != NULL) && size) {
		buf[0] = '\0';
		return buf;
	}
	/* Incomplete */

	return NULL;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo,
				      char *nodelist)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_complete() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_part_comp(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_part_comp() starting");
	return SLURM_SUCCESS;
}

extern bool switch_p_part_comp(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_part_comp() starting");
	return false;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
				       char *nodelist)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_allocated() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_slurmctld_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_slurmctld_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_slurmd_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_slurmd_step_init(void)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_slurmd_step_init() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_suspend(stepd_step_rec_t *job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_pre_suspend() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_suspend(stepd_step_rec_t *job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_post_suspend() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_resume(stepd_step_rec_t *job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_pre_resume() starting");
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_resume(stepd_step_rec_t *job)
{
	if (debug_flags & DEBUG_FLAG_SWITCH)
		info("switch_p_job_step_post_resume() starting");
	return SLURM_SUCCESS;
}
