/*****************************************************************************\
 *  node_conf.c - partially manage the node records of slurm
 *                (see src/slurmctld/node_mgr.c for the set of functionalities
 *                 related to slurmctld usage of nodes)
 *	Note: there is a global node table (node_record_table_ptr), its
 *	hash table (node_hash_table), time stamp (last_node_update) and
 *	configuration list (config_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2017 SchedMD LLC.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_topology.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define _DEBUG 0

strong_alias(init_node_conf, slurm_init_node_conf);
strong_alias(build_all_nodeline_info, slurm_build_all_nodeline_info);
strong_alias(rehash_node, slurm_rehash_node);
strong_alias(hostlist2bitmap, slurm_hostlist2bitmap);

/* Global variables */
List config_list  = NULL;	/* list of config_record entries */
List front_end_list = NULL;	/* list of slurm_conf_frontend_t entries */
time_t last_node_update = (time_t) 0;	/* time of last update */
node_record_t *node_record_table_ptr = NULL;	/* node records */
xhash_t* node_hash_table = NULL;
int node_record_count = 0;		/* count in node_record_table_ptr */
uint16_t *cr_node_num_cores = NULL;
uint32_t *cr_node_cores_offset = NULL;

/* Local function definitions */
static void _delete_config_record(void);
#if _DEBUG
static void	_dump_hash (void);
#endif
static node_record_t *_find_node_record(char *name, bool test_alias,
					bool log_missing);
static void	_list_delete_config (void *config_entry);
static void _node_record_hash_identity (void* item, const char** key,
					uint32_t* key_len);

/*
 * _delete_config_record - delete all configuration records
 * RET 0 if no error, errno otherwise
 * global: config_list - list of all configuration records
 */
static void _delete_config_record(void)
{
	last_node_update = time (NULL);
	list_flush(config_list);
	list_flush(front_end_list);
}


#if _DEBUG
/*
 * helper function used by _dump_hash to print the hash table elements
 */
static void xhash_walk_helper_cbk (void* item, void* arg)
{
	static int i = 0; /* sequential walk, so just update a static i */
	int inx;
	node_record_t *node_ptr = (node_record_t *) item;

	inx = node_ptr -  node_record_table_ptr;
	debug3("node_hash[%d]:%d(%s)", i++, inx, node_ptr->name);
}
/*
 * _dump_hash - print the node_hash_table contents, used for debugging
 *	or analysis of hash technique
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indexes
 */
static void _dump_hash (void)
{
	if (node_hash_table == NULL)
		return;
	debug2("node_hash: indexing %ld elements",
	      xhash_count(node_hash_table));
	xhash_walk(node_hash_table, xhash_walk_helper_cbk, NULL);
}
#endif

/* _list_delete_config - delete an entry from the config list,
 *	see list.h for documentation */
static void _list_delete_config (void *config_entry)
{
	config_record_t *config_ptr = (config_record_t *) config_entry;

	xassert(config_ptr);
	xassert(config_ptr->magic == CONFIG_MAGIC);
	xfree(config_ptr->cpu_spec_list);
	xfree(config_ptr->feature);
	xfree(config_ptr->gres);
	xfree (config_ptr->nodes);
	FREE_NULL_BITMAP (config_ptr->node_bitmap);
	xfree(config_ptr->tres_weights);
	xfree(config_ptr->tres_weights_str);
	xfree (config_ptr);
}

/*
 * xhash helper function to index node_record per name field
 * in node_hash_table
 */
static void _node_record_hash_identity (void* item, const char** key,
					uint32_t* key_len)
{
	node_record_t *node_ptr = (node_record_t *) item;
	*key = node_ptr->name;
	*key_len = strlen(node_ptr->name);
}

/*
 * bitmap2hostlist - given a bitmap, build a hostlist
 * IN bitmap - bitmap pointer
 * RET pointer to hostlist or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
hostlist_t bitmap2hostlist (bitstr_t *bitmap)
{
	int i, first, last;
	hostlist_t hl;

	if (bitmap == NULL)
		return NULL;

	first = bit_ffs(bitmap);
	if (first == -1)
		return NULL;

	last  = bit_fls(bitmap);
	hl = hostlist_create(NULL);
	for (i = first; i <= last; i++) {
		if (bit_test(bitmap, i) == 0)
			continue;
		hostlist_push_host(hl, node_record_table_ptr[i].name);
	}
	return hl;

}

/*
 * bitmap2node_name_sortable - given a bitmap, build a list of comma
 *	separated node names. names may include regular expressions
 *	(e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * IN sort   - returned sorted list or not
 * RET pointer to node list or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char * bitmap2node_name_sortable (bitstr_t *bitmap, bool sort)
{
	hostlist_t hl;
	char *buf;

	hl = bitmap2hostlist (bitmap);
	if (hl == NULL)
		return xstrdup("");
	if (sort)
		hostlist_sort(hl);
	buf = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);
	return buf;
}

/*
 * bitmap2node_name - given a bitmap, build a list of sorted, comma
 *	separated node names. names may include regular expressions
 *	(e.g. "lx[01-10]")
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
 */
char * bitmap2node_name (bitstr_t *bitmap)
{
	return bitmap2node_name_sortable(bitmap, 1);
}

#ifdef HAVE_FRONT_END
/* Log the contents of a frontend record */
static void _dump_front_end(slurm_conf_frontend_t *fe_ptr)
{
	info("fe name:%s addr:%s port:%u state:%u reason:%s "
	     "allow_groups:%s allow_users:%s "
	     "deny_groups:%s deny_users:%s",
	     fe_ptr->frontends, fe_ptr->addresses,
	     fe_ptr->port, fe_ptr->node_state, fe_ptr->reason,
	     fe_ptr->allow_groups, fe_ptr->allow_users,
	     fe_ptr->deny_groups, fe_ptr->deny_users);
}
#endif

/*
 * build_all_frontend_info - get a array of slurm_conf_frontend_t structures
 *	from the slurm.conf reader, build table, and set values
 * is_slurmd_context: set to true if run from slurmd
 * RET 0 if no error, error code otherwise
 */
extern void build_all_frontend_info (bool is_slurmd_context)
{
	slurm_conf_frontend_t **ptr_array;
#ifdef HAVE_FRONT_END
	slurm_conf_frontend_t *fe_single, *fe_line;
	int i, count;

	count = slurm_conf_frontend_array(&ptr_array);
	if (count == 0)
		fatal("No FrontendName information available!");

	for (i = 0; i < count; i++) {
		hostlist_t hl_name, hl_addr;
		char *fe_name, *fe_addr;

		fe_line = ptr_array[i];
		hl_name = hostlist_create(fe_line->frontends);
		if (hl_name == NULL)
			fatal("Invalid FrontendName:%s", fe_line->frontends);
		hl_addr = hostlist_create(fe_line->addresses);
		if (hl_addr == NULL)
			fatal("Invalid FrontendAddr:%s", fe_line->addresses);
		if (hostlist_count(hl_name) != hostlist_count(hl_addr)) {
			fatal("Inconsistent node count between "
			      "FrontendName(%s) and FrontendAddr(%s)",
			      fe_line->frontends, fe_line->addresses);
		}
		while ((fe_name = hostlist_shift(hl_name))) {
			fe_addr = hostlist_shift(hl_addr);
			fe_single = xmalloc(sizeof(slurm_conf_frontend_t));
			list_append(front_end_list, fe_single);
			fe_single->frontends = xstrdup(fe_name);
			fe_single->addresses = xstrdup(fe_addr);
			free(fe_name);
			free(fe_addr);
			if (fe_line->allow_groups && fe_line->allow_groups[0]) {
				fe_single->allow_groups =
					xstrdup(fe_line->allow_groups);
			}
			if (fe_line->allow_users && fe_line->allow_users[0]) {
				fe_single->allow_users =
					xstrdup(fe_line->allow_users);
			}
			if (fe_line->deny_groups && fe_line->deny_groups[0]) {
				fe_single->deny_groups =
					xstrdup(fe_line->deny_groups);
			}
			if (fe_line->deny_users && fe_line->deny_users[0]) {
				fe_single->deny_users =
					xstrdup(fe_line->deny_users);
			}
			fe_single->port = fe_line->port;
			if (fe_line->reason && fe_line->reason[0])
				fe_single->reason = xstrdup(fe_line->reason);
			fe_single->node_state = fe_line->node_state;
			if ((slurm_conf.debug_flags & DEBUG_FLAG_FRONT_END) &&
			    !is_slurmd_context)
				_dump_front_end(fe_single);
		}
		hostlist_destroy(hl_addr);
		hostlist_destroy(hl_name);
	}
#else
	if (slurm_conf_frontend_array(&ptr_array) != 0)
		fatal("FrontendName information configured!");
#endif
}

static void _check_callback(char *alias, char *hostname,
			    char *address, char *bcast_address,
			    uint16_t port, int state_val,
			    slurm_conf_node_t *node_ptr,
			    config_record_t *config_ptr)
{
	node_record_t *node_rec;

	if ((node_rec = find_node_record2(alias)))
		fatal("Duplicated NodeHostName %s in config file", alias);

	node_rec = create_node_record(config_ptr, alias);
	if ((state_val != NO_VAL) &&
	    (state_val != NODE_STATE_UNKNOWN))
		node_rec->node_state = state_val;
	node_rec->last_response = (time_t) 0;
	node_rec->comm_name = xstrdup(address);
	node_rec->cpu_bind  = node_ptr->cpu_bind;
	node_rec->node_hostname = xstrdup(hostname);
	node_rec->bcast_address = xstrdup(bcast_address);
	node_rec->port      = port;
	node_rec->weight    = node_ptr->weight;
	node_rec->features  = xstrdup(node_ptr->feature);
	node_rec->reason    = xstrdup(node_ptr->reason);
}

/*
 * build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * IN set_bitmap - if true then set node_bitmap in config record (used by
 *		    slurmd), false is used by slurmctld, clients, and testsuite
 * IN tres_cnt - number of TRES configured on system (used on controller side)
 */
extern void build_all_nodeline_info(bool set_bitmap, int tres_cnt)
{
	slurm_conf_node_t *node, **ptr_array;
	config_record_t *config_ptr = NULL;
	int count, i;
	bool in_daemon;
	static bool daemon_run = false, daemon_set = false;

	in_daemon = run_in_daemon(&daemon_run, &daemon_set, "slurmctld,slurmd");

	count = slurm_conf_nodename_array(&ptr_array);
	if (count == 0)
		fatal("No NodeName information available!");

	for (i = 0; i < count; i++) {
		node = ptr_array[i];

		config_ptr = create_config_record();
		config_ptr->nodes = xstrdup(node->nodenames);
		config_ptr->cpu_bind = node->cpu_bind;
		config_ptr->cpus = node->cpus;
		config_ptr->boards = node->boards;
		config_ptr->tot_sockets = node->tot_sockets;
		config_ptr->cores = node->cores;
		config_ptr->core_spec_cnt = node->core_spec_cnt;
		config_ptr->cpu_spec_list = xstrdup(node->cpu_spec_list);
		config_ptr->threads = node->threads;
		config_ptr->real_memory = node->real_memory;
		config_ptr->mem_spec_limit = node->mem_spec_limit;
		config_ptr->tmp_disk = node->tmp_disk;

		if (tres_cnt) {
			config_ptr->tres_weights_str =
				xstrdup(node->tres_weights_str);
			config_ptr->tres_weights =
				slurm_get_tres_weight_array(
						node->tres_weights_str,
						tres_cnt, true);
		}

		config_ptr->weight = node->weight;
		if (node->feature && node->feature[0])
			config_ptr->feature = xstrdup(node->feature);
		if (in_daemon) {
			config_ptr->gres = gres_name_filter(node->gres,
							    node->nodenames);
		}

		check_nodeline_info(node, config_ptr, _check_callback);
	}

	if (set_bitmap) {
		ListIterator config_iterator;
		config_iterator = list_iterator_create(config_list);
		while ((config_ptr = list_next(config_iterator))) {
			node_name2bitmap(config_ptr->nodes, true,
					 &config_ptr->node_bitmap);
		}
		list_iterator_destroy(config_iterator);
	}
}

/*
 * check_nodeline_info - From the slurm.conf reader, build table, and set values
 * Note: Operates on common variables
 *	default_node_record - default node configuration values
 */
extern void check_nodeline_info(slurm_conf_node_t *node_ptr,
			        config_record_t *config_ptr,
			        void (*_callback) (
				       char *alias, char *hostname,
				       char *address, char *bcast_address,
				       uint16_t port, int state_val,
				       slurm_conf_node_t *node_ptr,
				       config_record_t *config_ptr))
{
	hostlist_t address_list = NULL;
	hostlist_t alias_list = NULL;
	hostlist_t bcast_list = NULL;
	hostlist_t hostname_list = NULL;
	hostlist_t port_list = NULL;
	char *address = NULL;
	char *alias = NULL;
	char *bcast_address = NULL;
	char *hostname = NULL;
	char *port_str = NULL;
	int state_val = NODE_STATE_UNKNOWN;
	int address_count, alias_count, bcast_count, hostname_count, port_count;
	uint16_t port = slurm_conf.slurmd_port;

	if (!node_ptr->nodenames || !node_ptr->nodenames[0])
		fatal("Empty NodeName in config.");

	if (node_ptr->state) {
		state_val = state_str2int(node_ptr->state, node_ptr->nodenames);
		if (state_val == NO_VAL)
			fatal("Invalid state %s from %s",
			      node_ptr->state, node_ptr->nodenames);
	}

	if (!(address_list = hostlist_create(node_ptr->addresses)))
		fatal("Unable to create NodeAddr list from %s",
		      node_ptr->addresses);

	if (!(alias_list = hostlist_create(node_ptr->nodenames)))
		fatal("Unable to create NodeName list from %s",
		      node_ptr->nodenames);

	if (!(bcast_list = hostlist_create(node_ptr->bcast_addresses)))
		fatal("Unable to create BcastAddr list from %s",
		      node_ptr->bcast_addresses);

	if (!(hostname_list = hostlist_create(node_ptr->hostnames)))
		fatal("Unable to create NodeHostname list from %s",
		      node_ptr->hostnames);

	if (node_ptr->port_str && node_ptr->port_str[0] &&
	    (node_ptr->port_str[0] != '[') &&
	    (strchr(node_ptr->port_str, '-') ||
	     strchr(node_ptr->port_str, ','))) {
		xstrfmtcat(port_str, "[%s]", node_ptr->port_str);
		port_list = hostlist_create(port_str);
		xfree(port_str);
	} else
		port_list = hostlist_create(node_ptr->port_str);

	if (!port_list)
		fatal("Unable to create Port list from %s",
		      node_ptr->port_str);

	/* some sanity checks */
	address_count  = hostlist_count(address_list);
	bcast_count = hostlist_count(bcast_list);
	alias_count    = hostlist_count(alias_list);
	hostname_count = hostlist_count(hostname_list);
	port_count     = hostlist_count(port_list);
#ifdef HAVE_FRONT_END
	if ((hostname_count != alias_count) && (hostname_count != 1))
		fatal("NodeHostname count must equal that of NodeName records of there must be no more than one");

	if ((address_count != alias_count) && (address_count != 1))
		fatal("NodeAddr count must equal that of NodeName records of there must be no more than one");
#else
#ifdef MULTIPLE_SLURMD
	if ((address_count != alias_count) && (address_count != 1))
		fatal("NodeAddr count must equal that of NodeName records of there must be no more than one");
	if (bcast_count && (bcast_count != alias_count) && (bcast_count != 1))
		fatal("BcastAddr count must equal that of NodeName records, or there must be no more than one");
#else
	if (address_count < alias_count)
		fatal("At least as many NodeAddr are required as NodeName");

	if (bcast_count && (bcast_count < alias_count))
		fatal("At least as many BcastAddr are required as NodeName");

	if (hostname_count < alias_count)
		fatal("At least as many NodeHostname are required as NodeName");
#endif	/* MULTIPLE_SLURMD */
#endif	/* HAVE_FRONT_END */
	if ((port_count != alias_count) && (port_count > 1))
		fatal("Port count must equal that of NodeName records or there must be no more than one (%u != %u)",
		      port_count, alias_count);

	/* now build the individual node structures */
	while ((alias = hostlist_shift(alias_list))) {
		if (address_count > 0) {
			address_count--;
			if (address)
				free(address);
			address = hostlist_shift(address_list);
		}
		if (bcast_count > 0) {
			bcast_count--;
			if (bcast_address)
				free(bcast_address);
			bcast_address = hostlist_shift(bcast_list);
		}
		if (hostname_count > 0) {
			hostname_count--;
			if (hostname)
				free(hostname);
			hostname = hostlist_shift(hostname_list);
		}
		if (port_count > 0) {
			int port_int;
			port_count--;
			if (port_str)
				free(port_str);
			port_str = hostlist_shift(port_list);
			port_int = atoi(port_str);
			if ((port_int <= 0) || (port_int > 0xffff))
				fatal("Invalid Port %s", node_ptr->port_str);
			port = port_int;
		}

		(*_callback)(alias, hostname, address, bcast_address,
			     port, state_val, node_ptr, config_ptr);

		free(alias);
	}
	/* free allocated storage */
	if (address)
		free(address);
	if (bcast_address)
		free(bcast_address);
	if (hostname)
		free(hostname);
	if (port_str)
		free(port_str);
	if (address_list)
		hostlist_destroy(address_list);
	if (alias_list)
		hostlist_destroy(alias_list);
	if (bcast_list)
		hostlist_destroy(bcast_list);
	if (hostname_list)
		hostlist_destroy(hostname_list);
	if (port_list)
		hostlist_destroy(port_list);
}

/*
 * create_config_record - create a config_record entry and set is values to
 *	the defaults. each config record corresponds to a line in the
 *	slurm.conf file and typically describes the configuration of a
 *	large number of nodes
 * RET pointer to the config_record
 * NOTE: memory allocated will remain in existence until
 *	_delete_config_record() is called to delete all configuration records
 */
extern config_record_t *create_config_record(void)
{
	config_record_t *config_ptr = xmalloc(sizeof(*config_ptr));

	last_node_update = time (NULL);

	config_ptr->magic = CONFIG_MAGIC;
	config_ptr->nodes = NULL;
	config_ptr->node_bitmap = NULL;

	list_append(config_list, config_ptr);

	return config_ptr;
}

/*
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when
 *	the global node table is no longer required
 */
extern node_record_t *create_node_record(config_record_t *config_ptr,
					 char *node_name)
{
	node_record_t *node_ptr;
	int old_buffer_size, new_buffer_size;
	uint32_t tot_cores;

	last_node_update = time (NULL);
	xassert(config_ptr);
	xassert(node_name);

	/* round up the buffer size to reduce overhead of xrealloc */
	old_buffer_size = (node_record_count) * sizeof(node_record_t);
	old_buffer_size =
		((int) ((old_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	new_buffer_size =
		(node_record_count + 1) * sizeof(node_record_t);
	new_buffer_size =
		((int) ((new_buffer_size / BUF_SIZE) + 1)) * BUF_SIZE;
	if (!node_record_table_ptr) {
		node_record_table_ptr = xmalloc(new_buffer_size);
	} else if (old_buffer_size != new_buffer_size) {
		xrealloc (node_record_table_ptr, new_buffer_size);
		/*
		 * You need to rehash the hash after we realloc or we will have
		 * only bad memory references in the hash.
		 */
		rehash_node();
	}
	node_ptr = node_record_table_ptr + (node_record_count++);
	node_ptr->name = xstrdup(node_name);
	if (!node_hash_table)
		node_hash_table = xhash_init(_node_record_hash_identity, NULL);
	xhash_add(node_hash_table, node_ptr);

	node_ptr->config_ptr = config_ptr;
	/* these values will be overwritten when the node actually registers */
	node_ptr->cpus = config_ptr->cpus;
	node_ptr->cpu_load = NO_VAL;
	node_ptr->free_mem = NO_VAL64;
	node_ptr->cpu_spec_list = xstrdup(config_ptr->cpu_spec_list);
	node_ptr->boards = config_ptr->boards;
	node_ptr->tot_sockets = config_ptr->tot_sockets;
	node_ptr->cores = config_ptr->cores;
	node_ptr->core_spec_cnt = config_ptr->core_spec_cnt;
	node_ptr->threads = config_ptr->threads;
	node_ptr->mem_spec_limit = config_ptr->mem_spec_limit;
	node_ptr->real_memory = config_ptr->real_memory;
	node_ptr->node_spec_bitmap = NULL;
	node_ptr->tmp_disk = config_ptr->tmp_disk;
	node_ptr->select_nodeinfo = select_g_select_nodeinfo_alloc();
	node_ptr->energy = acct_gather_energy_alloc(1);
	node_ptr->ext_sensors = ext_sensors_alloc();
	node_ptr->owner = NO_VAL;
	node_ptr->mcs_label = NULL;
	node_ptr->next_state = NO_VAL;
	node_ptr->protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	node_ptr->magic = NODE_MAGIC;
	node_ptr->resume_timeout = NO_VAL16;
	node_ptr->suspend_time = NO_VAL;
	node_ptr->suspend_timeout = NO_VAL16;

	/*
	 * Here we determine if this node is scheduling threads or not.
	 * We will set vpus to be the number of schedulable threads.
	 */
	tot_cores = config_ptr->tot_sockets * config_ptr->cores;
	if (tot_cores >= config_ptr->cpus)
		node_ptr->vpus = 1;
	else
		node_ptr->vpus = config_ptr->threads;

	return node_ptr;
}

/*
 * find_node_record - find a record for node with specified name
 * IN: name - name of the desired node
 * RET: pointer to node record or NULL if not found
 * NOTE: Logs an error if the node name is NOT found
 */
extern node_record_t *find_node_record(char *name)
{
	return _find_node_record(name, true, true);
}

/*
 * find_node_record2 - find a record for node with specified name
 * IN: name - name of the desired node
 * RET: pointer to node record or NULL if not found
 * NOTE: Does not log an error if the node name is NOT found
 */
extern node_record_t *find_node_record2(char *name)
{
	return _find_node_record(name, true, false);
}

/*
 * find_node_record_no_alias - find a record for node with specified name
 * without looking at the node's alias (NodeHostName).
 * IN: name - name of the desired node
 * RET: pointer to node record or NULL if not found
 * NOTE: Logs an error if the node name is NOT found
 */
extern node_record_t *find_node_record_no_alias(char *name)
{
	return _find_node_record(name, false, true);
}

/*
 * _find_node_record - find a record for node with specified name
 * IN: name - name of the desired node
 * IN: test_alias - if set, also test NodeHostName value
 * IN: log_missing - if set, then print an error message if the node is not found
 * RET: pointer to node record or NULL if not found
 */
static node_record_t *_find_node_record(char *name, bool test_alias,
					bool log_missing)
{
	node_record_t *node_ptr;

	if ((name == NULL) || (name[0] == '\0')) {
		info("%s: passed NULL node name", __func__);
		return NULL;
	}

	/* nothing added yet */
	if (!node_hash_table)
		return NULL;

	/* try to find via hash table, if it exists */
	if ((node_ptr = xhash_get_str(node_hash_table, name))) {
		xassert(node_ptr->magic == NODE_MAGIC);
		return node_ptr;
	}

	if ((node_record_count == 1) &&
	    (xstrcmp(node_record_table_ptr[0].name, "localhost") == 0))
		return (&node_record_table_ptr[0]);

	if (log_missing)
		error("%s: lookup failure for node \"%s\"",
		      __func__, name);

	if (test_alias) {
		char *alias = slurm_conf_get_nodename(name);
		/* look for the alias node record if the user put this in
	 	 * instead of what slurm sees the node name as */
		if (!alias)
			return NULL;

		node_ptr = xhash_get_str(node_hash_table, alias);
		if (log_missing)
			error("%s: lookup failure for node \"%s\", alias \"%s\"",
			      __func__, name, alias);
		xfree(alias);
		return node_ptr;
	}

	return NULL;
}

/*
 * init_node_conf - initialize the node configuration tables and values.
 *	this should be called before creating any node or configuration
 *	entries.
 */
extern void init_node_conf(void)
{
	last_node_update = time (NULL);
	int i;
	node_record_t *node_ptr;

	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++)
		purge_node_rec(node_ptr);

	node_record_count = 0;
	xfree(node_record_table_ptr);
	xhash_free(node_hash_table);

	if (config_list)	/* delete defunct configuration entries */
		_delete_config_record();
	else {
		config_list    = list_create (_list_delete_config);
		front_end_list = list_create (destroy_frontend);
	}
}


/* node_fini2 - free memory associated with node records (except bitmaps) */
extern void node_fini2 (void)
{
	int i;
	node_record_t *node_ptr;

	if (config_list) {
		FREE_NULL_LIST(config_list);
		FREE_NULL_LIST(front_end_list);
	}

	xhash_free(node_hash_table);
	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++)
		purge_node_rec(node_ptr);

	xfree(node_record_table_ptr);
	node_record_count = 0;
}

extern int node_name_get_inx(char *node_name)
{
	node_record_t *node_ptr = NULL;

	if (node_name)
		node_ptr = find_node_record(node_name);

	if (!node_ptr)
		return -1;

	return (node_ptr - node_record_table_ptr);
}

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap
 *	representation
 * IN node_names  - list of nodes
 * IN best_effort - if set don't return an error on invalid node name entries
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * RET 0 if no error, otherwise EINVAL
 * NOTE: call FREE_NULL_BITMAP() to free bitmap memory when no longer required
 */
extern int node_name2bitmap (char *node_names, bool best_effort,
			     bitstr_t **bitmap)
{
	int rc = SLURM_SUCCESS;
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t host_list;

	my_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	*bitmap = my_bitmap;

	if (node_names == NULL) {
		info("node_name2bitmap: node_names is NULL");
		return rc;
	}

	if ( (host_list = hostlist_create (node_names)) == NULL) {
		/* likely a badly formatted hostlist */
		error ("hostlist_create on %s error:", node_names);
		if (!best_effort)
			rc = EINVAL;
		return rc;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_record_t *node_ptr;
		node_ptr = _find_node_record(this_node_name, best_effort, true);
		if (node_ptr) {
			bit_set (my_bitmap, (bitoff_t) (node_ptr -
							node_record_table_ptr));
		} else {
			error("%s: invalid node specified: \"%s\"", __func__,
			      this_node_name);
			if (!best_effort)
				rc = EINVAL;
		}
		free (this_node_name);
	}
	hostlist_destroy (host_list);

	return rc;
}

/*
 * hostlist2bitmap - given a hostlist, build a bitmap representation
 * IN hl          - hostlist
 * IN best_effort - if set don't return an error on invalid node name entries
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * RET 0 if no error, otherwise EINVAL
 */
extern int hostlist2bitmap (hostlist_t hl, bool best_effort, bitstr_t **bitmap)
{
	int rc = SLURM_SUCCESS;
	bitstr_t *my_bitmap;
	char *name;
	hostlist_iterator_t hi;

	FREE_NULL_BITMAP(*bitmap);
	my_bitmap = (bitstr_t *) bit_alloc (node_record_count);
	*bitmap = my_bitmap;

	hi = hostlist_iterator_create(hl);
	while ((name = hostlist_next(hi))) {
		node_record_t *node_ptr;
		node_ptr = _find_node_record(name, best_effort, true);
		if (node_ptr) {
			bit_set (my_bitmap, (bitoff_t) (node_ptr -
							node_record_table_ptr));
		} else {
			error ("hostlist2bitmap: invalid node specified %s",
			       name);
			if (!best_effort)
				rc = EINVAL;
		}
		free (name);
	}

	hostlist_iterator_destroy(hi);
	return rc;

}

/* Purge the contents of a node record */
extern void purge_node_rec(node_record_t *node_ptr)
{
	xfree(node_ptr->arch);
	xfree(node_ptr->comment);
	xfree(node_ptr->comm_name);
	xfree(node_ptr->cpu_spec_list);
	xfree(node_ptr->extra);
	xfree(node_ptr->features);
	xfree(node_ptr->features_act);
	xfree(node_ptr->gres);
	FREE_NULL_LIST(node_ptr->gres_list);
	xfree(node_ptr->name);
	xfree(node_ptr->node_hostname);
	FREE_NULL_BITMAP(node_ptr->node_spec_bitmap);
	xfree(node_ptr->os);
	xfree(node_ptr->part_pptr);
	xfree(node_ptr->power);
	xfree(node_ptr->reason);
	xfree(node_ptr->version);
	acct_gather_energy_destroy(node_ptr->energy);
	ext_sensors_destroy(node_ptr->ext_sensors);
	select_g_select_nodeinfo_free(node_ptr->select_nodeinfo);
	xfree(node_ptr->tres_str);
	xfree(node_ptr->tres_fmt_str);
	xfree(node_ptr->tres_cnt);
}

/*
 * rehash_node - build a hash table of the node_record entries.
 * NOTE: using xhash implementation
 */
extern void rehash_node (void)
{
	int i;
	node_record_t *node_ptr = node_record_table_ptr;

	xhash_free (node_hash_table);
	node_hash_table = xhash_init(_node_record_hash_identity, NULL);
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;	/* vestigial record */
		xhash_add(node_hash_table, node_ptr);
	}

#if _DEBUG
	_dump_hash();
#endif
	return;
}

/* Convert a node state string to it's equivalent enum value */
extern int state_str2int(const char *state_str, char *node_name)
{
	int state_val = NO_VAL;
	int i;

	for (i = 0; i <= NODE_STATE_END; i++) {
		if (xstrcasecmp(node_state_string(i), "END") == 0)
			break;
		if (xstrcasecmp(node_state_string(i), state_str) == 0) {
			state_val = i;
			break;
		}
	}
	if (i >= NODE_STATE_END) {
		if (xstrncasecmp("CLOUD", state_str, 5) == 0)
			state_val = NODE_STATE_IDLE | NODE_STATE_CLOUD |
				    NODE_STATE_POWERED_DOWN;
		else if (xstrncasecmp("DRAIN", state_str, 5) == 0)
			state_val = NODE_STATE_UNKNOWN | NODE_STATE_DRAIN;
		else if (xstrncasecmp("FAIL", state_str, 4) == 0)
			state_val = NODE_STATE_IDLE | NODE_STATE_FAIL;
	}
	if (state_val == NO_VAL) {
		error("node %s has invalid state %s", node_name, state_str);
		errno = EINVAL;
	}
	return state_val;
}

/* (re)set cr_node_num_cores arrays */
extern void cr_init_global_core_data(node_record_t *node_ptr, int node_cnt)
{
	uint32_t n;

	cr_fini_global_core_data();

	cr_node_num_cores = xmalloc(node_cnt * sizeof(uint16_t));
	cr_node_cores_offset = xmalloc((node_cnt+1) * sizeof(uint32_t));

	for (n = 0; n < node_cnt; n++) {
		uint16_t cores = node_ptr[n].config_ptr->cores;
		cores *= node_ptr[n].config_ptr->tot_sockets;

		cr_node_num_cores[n] = cores;
		if (n > 0) {
			cr_node_cores_offset[n] = cr_node_cores_offset[n-1] +
						  cr_node_num_cores[n-1] ;
		} else
			cr_node_cores_offset[0] = 0;
	}

	/* an extra value is added to get the total number of cores */
	/* as cr_get_coremap_offset is sometimes used to get the total */
	/* number of cores in the cluster */
	cr_node_cores_offset[node_cnt] = cr_node_cores_offset[node_cnt-1] +
					 cr_node_num_cores[node_cnt-1] ;

}

extern void cr_fini_global_core_data(void)
{
	xfree(cr_node_num_cores);
	xfree(cr_node_cores_offset);
}

/* return the coremap index to the first core of the given node */

extern uint32_t cr_get_coremap_offset(uint32_t node_index)
{
	xassert(cr_node_cores_offset);
	return cr_node_cores_offset[node_index];
}

/* Return a bitmap the size of the machine in cores. On a Bluegene
 * system it will return a bitmap in cnodes. */
extern bitstr_t *cr_create_cluster_core_bitmap(int core_mult)
{
	/* DEF_TIMERS; */
	/* START_TIMER; */
	bitstr_t *core_bitmap;
	static int cnt = 0;

	if (!cnt) {
		cnt = cr_get_coremap_offset(node_record_count);
		if (core_mult)
			cnt *= core_mult;
	}
	core_bitmap = bit_alloc(cnt);
	/* END_TIMER; */
	/* info("creating of core bitmap of %d took %s", cnt, TIME_STR); */
	return core_bitmap;
}

/*
 * Determine maximum number of CPUs on this node usable by a job
 * ntasks_per_core IN - tasks-per-core to be launched by this job
 * cpus_per_task IN - number of required  CPUs per task for this job
 * total_cores IN - total number of cores on this node
 * total_cpus IN - total number of CPUs on this node
 * RET count of usable CPUs on this node usable by this job
 */
extern int adjust_cpus_nppcu(uint16_t ntasks_per_core, int cpus_per_task,
			     int total_cores, int total_cpus)
{
	int cpus = total_cpus;

//FIXME: This function ignores tasks-per-socket and tasks-per-node checks.
// Those parameters are tested later
	if ((ntasks_per_core != 0) && (ntasks_per_core != 0xffff) &&
	    (cpus_per_task != 0)) {
		cpus = MAX((total_cores * ntasks_per_core * cpus_per_task),
			   total_cpus);
	}

	return cpus;
}

extern char *find_hostname(uint32_t pos, char *hosts)
{
	hostlist_t hostlist = NULL;
	char *temp = NULL, *host = NULL;

	if (!hosts || (pos == NO_VAL) || (pos == INFINITE))
		return NULL;

	hostlist = hostlist_create(hosts);
	temp = hostlist_nth(hostlist, pos);
	if (temp) {
		host = xstrdup(temp);
		free(temp);
	}
	hostlist_destroy(hostlist);
	return host;
}
