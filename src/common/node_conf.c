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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "src/common/assoc_mgr.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/acct_gather_energy.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/select.h"
#include "src/interfaces/topology.h"

#define _DEBUG 0

strong_alias(init_node_conf, slurm_init_node_conf);
strong_alias(build_all_nodeline_info, slurm_build_all_nodeline_info);
strong_alias(rehash_node, slurm_rehash_node);
strong_alias(hostlist2bitmap, slurm_hostlist2bitmap);
strong_alias(bitmap2node_name, slurm_bitmap2node_name);
strong_alias(node_name2bitmap, slurm_node_name2bitmap);
strong_alias(find_node_record, slurm_find_node_record);

/* Global variables */
list_t *config_list = NULL;	/* list of config_record entries */
list_t *front_end_list = NULL;	/* list of slurm_conf_frontend_t entries */
time_t last_node_update = (time_t) 0;	/* time of last update */
node_record_t **node_record_table_ptr = NULL;	/* node records */
xhash_t* node_hash_table = NULL;
int node_record_table_size = 0;		/* size of node_record_table_ptr */
int node_record_count = 0;		/* number of node slots in
					 * node_record_table_ptr */
int active_node_record_count = 0;	/* non-null node count in
					 * node_record_table_ptr */
int last_node_index = -1;		/* index of last node in tabe */
uint16_t *cr_node_num_cores = NULL;
uint32_t *cr_node_cores_offset = NULL;
bool spec_cores_first = false;
time_t slurmd_start_time = 0;

/* Local function definitions */
static void _delete_config_record(void);
static void _delete_node_config_ptr(node_record_t *node_ptr);
#if _DEBUG
static void _dump_hash(void);
#endif
static node_record_t *_find_node_record(char *name, bool test_alias,
					bool log_missing);
static void _list_delete_config(void *config_entry);
static void _node_record_hash_identity(void *item, const char **key,
				       uint32_t *key_len);

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
static void xhash_walk_helper_cbk(void *item, void *arg)
{
	int *i_ptr = arg;
	node_record_t *node_ptr = (node_record_t *) item;

	debug3("node_hash[%d]:%d(%s)", (*i_ptr)++, node_ptr->index,
	       node_ptr->name);
}

/*
 * _dump_hash - print the node_hash_table contents, used for debugging
 *	or analysis of hash technique
 * global: node_record_table_ptr - pointer to global node table
 *         node_hash_table - table of hash indexes
 */
static void _dump_hash(void)
{
	int i = 0;
	if (node_hash_table == NULL)
		return;
	debug2("node_hash: indexing %u elements",
	       xhash_count(node_hash_table));
	xhash_walk(node_hash_table, xhash_walk_helper_cbk, &i);
}
#endif

/* _list_delete_config - delete an entry from the config list,
 *	see list.h for documentation */
static void _list_delete_config(void *config_entry)
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
static void _node_record_hash_identity(void *item, const char **key,
				       uint32_t *key_len)
{
	node_record_t *node_ptr = item;
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
extern hostlist_t *bitmap2hostlist(bitstr_t *bitmap)
{
	hostlist_t *hl;
	node_record_t *node_ptr;

	if (bitmap == NULL)
		return NULL;

	hl = hostlist_create(NULL);
	for (int i = 0; (node_ptr = next_node_bitmap(bitmap, &i)); i++) {
		hostlist_push_host(hl, node_ptr->name);
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
extern char *bitmap2node_name_sortable(bitstr_t *bitmap, bool sort)
{
	hostlist_t *hl;
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
extern char *bitmap2node_name(bitstr_t *bitmap)
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
extern void build_all_frontend_info(bool is_slurmd_context)
{
	slurm_conf_frontend_t **ptr_array;
#ifdef HAVE_FRONT_END
	slurm_conf_frontend_t *fe_single, *fe_line;
	int i, count;

	count = slurm_conf_frontend_array(&ptr_array);
	if (count == 0)
		fatal("No FrontendName information available!");

	for (i = 0; i < count; i++) {
		hostlist_t *hl_name, *hl_addr;
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

static int _check_callback(char *alias, char *hostname,
			   char *address, char *bcast_address,
			   uint16_t port, int state_val,
			   slurm_conf_node_t *node_ptr,
			   config_record_t *config_ptr)
{
	int rc = SLURM_SUCCESS;
	node_record_t *node_rec;

	if ((node_rec = find_node_record2(alias)))
		fatal("Duplicated NodeHostName %s in config file", alias);

	if ((rc = create_node_record(config_ptr, alias, &node_rec)))
		return rc;

	if ((state_val != NO_VAL) &&
	    (state_val != NODE_STATE_UNKNOWN))
		node_rec->node_state = state_val;
	node_rec->last_response = (time_t) 0;
	node_rec->comm_name = xstrdup(address);
	node_rec->cpu_bind  = node_ptr->cpu_bind;
	node_rec->node_hostname = xstrdup(hostname);
	node_rec->bcast_address = xstrdup(bcast_address);
	node_rec->port      = port;
	node_rec->features  = xstrdup(node_ptr->feature);
	node_rec->reason    = xstrdup(node_ptr->reason);

	return rc;
}

extern config_record_t *config_record_from_conf_node(
	slurm_conf_node_t *conf_node, int tres_cnt)
{
	config_record_t *config_ptr;
	bool in_daemon;
	static bool daemon_run = false, daemon_set = false;

	config_ptr = create_config_record();
	config_ptr->boards = conf_node->boards;
	config_ptr->core_spec_cnt = conf_node->core_spec_cnt;
	config_ptr->cores = conf_node->cores;
	config_ptr->cpu_bind = conf_node->cpu_bind;
	config_ptr->cpu_spec_list = xstrdup(conf_node->cpu_spec_list);
	config_ptr->cpus = conf_node->cpus;
	if (conf_node->feature && conf_node->feature[0])
		config_ptr->feature = xstrdup(conf_node->feature);
	config_ptr->mem_spec_limit = conf_node->mem_spec_limit;
	config_ptr->nodes = xstrdup(conf_node->nodenames);
	config_ptr->real_memory = conf_node->real_memory;
	config_ptr->res_cores_per_gpu = conf_node->res_cores_per_gpu;
	config_ptr->threads = conf_node->threads;
	config_ptr->tmp_disk = conf_node->tmp_disk;
	config_ptr->tot_sockets = conf_node->tot_sockets;
	config_ptr->weight = conf_node->weight;

	if (tres_cnt) {
		config_ptr->tres_weights_str =
			xstrdup(conf_node->tres_weights_str);
		config_ptr->tres_weights =
			slurm_get_tres_weight_array(conf_node->tres_weights_str,
						    tres_cnt, true);
	}

	in_daemon = run_in_daemon(&daemon_run, &daemon_set, "slurmctld,slurmd");
	if (in_daemon) {
		config_ptr->gres = gres_name_filter(conf_node->gres,
						    conf_node->nodenames);
	}

	return config_ptr;
}

/*
 * build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * IN set_bitmap - if true then set node_bitmap in config record (used by
 *		    slurmd), false is used by slurmctld, clients, and testsuite
 * IN tres_cnt - number of TRES configured on system (used on controller side)
 */
extern int build_all_nodeline_info(bool set_bitmap, int tres_cnt)
{
	slurm_conf_node_t *node, **ptr_array;
	config_record_t *config_ptr = NULL;
	int count, i;

	count = slurm_conf_nodename_array(&ptr_array);

	for (i = 0; i < count; i++) {
		int rc;
		node = ptr_array[i];
		config_ptr = config_record_from_conf_node(node, tres_cnt);
		if (((rc = expand_nodeline_info(node, config_ptr, NULL,
					       _check_callback))))
			return rc;
	}

	if (set_bitmap) {
		list_itr_t *config_iterator;
		config_iterator = list_iterator_create(config_list);
		while ((config_ptr = list_next(config_iterator))) {
			node_name2bitmap(config_ptr->nodes, true,
					 &config_ptr->node_bitmap, NULL);
		}
		list_iterator_destroy(config_iterator);
	}

	return SLURM_SUCCESS;
}

extern int build_node_spec_bitmap(node_record_t *node_ptr)
{
	uint32_t size;
	int *cpu_spec_array;
	int i;

	if (node_ptr->tpc == 0) {
		error("Node %s has invalid thread per core count (%u)",
		      node_ptr->name, node_ptr->tpc);
		return SLURM_ERROR;
	}

	if (!node_ptr->cpu_spec_list)
		return SLURM_SUCCESS;
	size = node_ptr->tot_cores;
	FREE_NULL_BITMAP(node_ptr->node_spec_bitmap);
	node_ptr->node_spec_bitmap = bit_alloc(size);
	bit_set_all(node_ptr->node_spec_bitmap);

	/* remove node's specialized cpus now */
	cpu_spec_array = bitfmt2int(node_ptr->cpu_spec_list);
	i = 0;
	while (cpu_spec_array[i] != -1) {
		int start = (cpu_spec_array[i] / node_ptr->tpc);
		int end = (cpu_spec_array[i + 1] / node_ptr->tpc);
		if (start > size) {
			error("%s: Specialized CPUs id start above the configured limit.",
			      __func__);
			break;
		}

		if (end > size) {
			error("%s: Specialized CPUs id end above the configured limit",
			      __func__);
			end = size;
		}
		/*
		 * We need to test to make sure we have these bits in this map.
		 * If the node goes from 12 cpus to 6 like scenario.
		 */
		bit_nclear(node_ptr->node_spec_bitmap, start, end);
		i += 2;
	}
	node_ptr->core_spec_cnt = bit_clear_count(node_ptr->node_spec_bitmap);
	xfree(cpu_spec_array);
	return SLURM_SUCCESS;
}

/*
 * Select cores and CPUs to be reserved for core specialization.
 */
static void _select_spec_cores(node_record_t *node_ptr)
{
	int spec_cores, res_core, res_sock, res_off;
	int from_core, to_core, incr_core, from_sock, to_sock, incr_sock;
	bitstr_t *cpu_spec_bitmap;

	spec_cores = node_ptr->core_spec_cnt;

	cpu_spec_bitmap = bit_alloc(node_ptr->cpus);
	node_ptr->node_spec_bitmap = bit_alloc(node_ptr->tot_cores);
	bit_set_all(node_ptr->node_spec_bitmap);

	if (spec_cores_first) {
		from_core = 0;
		to_core   = node_ptr->cores;
		incr_core = 1;
		from_sock = 0;
		to_sock   = node_ptr->tot_sockets;
		incr_sock = 1;
	} else {
		from_core = node_ptr->cores - 1;
		to_core   = -1;
		incr_core = -1;
		from_sock = node_ptr->tot_sockets - 1;
		to_sock   = -1;
		incr_sock = -1;
	}
	for (res_core = from_core;
	     (spec_cores && (res_core != to_core)); res_core += incr_core) {
		for (res_sock = from_sock;
		     (spec_cores && (res_sock != to_sock));
		      res_sock += incr_sock) {
			int thread_off;
			thread_off = ((res_sock * node_ptr->cores) + res_core) *
				      node_ptr->tpc;
			bit_nset(cpu_spec_bitmap, thread_off,
				 thread_off + node_ptr->tpc - 1);
			res_off = (res_sock * node_ptr->cores) + res_core;
			bit_clear(node_ptr->node_spec_bitmap, res_off);
			spec_cores--;
		}
	}

	node_ptr->cpu_spec_list = bit_fmt_full(cpu_spec_bitmap);
	FREE_NULL_BITMAP(cpu_spec_bitmap);

	return;
}
/*
 * Expand a nodeline's node names, host names, addrs, ports into separate nodes.
 */
extern int expand_nodeline_info(slurm_conf_node_t *node_ptr,
				config_record_t *config_ptr,
				char **err_msg,
				int (*_callback) (
					char *alias, char *hostname,
					char *address, char *bcast_address,
					uint16_t port, int state_val,
					slurm_conf_node_t *node_ptr,
					config_record_t *config_ptr))
{
	hostlist_t *address_list = NULL;
	hostlist_t *alias_list = NULL;
	hostlist_t *bcast_list = NULL;
	hostlist_t *hostname_list = NULL;
	hostlist_t *port_list = NULL;
	char *address = NULL;
	char *alias = NULL;
	char *bcast_address = NULL;
	char *hostname = NULL;
	char *port_str = NULL;
	int state_val = NODE_STATE_UNKNOWN, rc = SLURM_SUCCESS;
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

	if ((address_count != alias_count) && (address_count != 1))
		fatal("NodeAddr count must equal that of NodeName records or  there must be no more than one");
	if ((bcast_count != alias_count) && (bcast_count > 1))
		fatal("BcastAddr count must equal that of NodeName records or there must be no more than one");
	if ((hostname_count != alias_count) && (hostname_count != 1))
		fatal("NodeHostname count must equal that of NodeName records or there must be no more than one");
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

		if ((rc = (*_callback)(alias, hostname, address, bcast_address,
				       port, state_val, node_ptr,
				       config_ptr))) {
			if (err_msg) {
				xfree(*err_msg);
				*err_msg = xstrdup_printf("%s (%s)",
							  slurm_strerror(rc),
							  alias);
			}
			free(alias);
			break;
		}

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
	FREE_NULL_HOSTLIST(address_list);
	FREE_NULL_HOSTLIST(alias_list);
	FREE_NULL_HOSTLIST(bcast_list);
	FREE_NULL_HOSTLIST(hostname_list);
	FREE_NULL_HOSTLIST(port_list);

	return rc;
}

/*
 * Sync with _init_conf_node().
 *
 * _init_conf_node() initializes default values from slurm.conf parameters.
 * After parsing slurm.conf, build_all_nodeline_info() copies slurm_conf_node_t
 * to config_record_t. Defaults values between slurm_conf_node_t and
 * config_record_t should stay in sync in case a config_record is created
 * outside of slurm.conf parsing.
 */
static void _init_config_record(config_record_t *config_ptr)
{
	config_ptr->magic = CONFIG_MAGIC;
	config_ptr->boards = 1;
	config_ptr->cores = 1;
	config_ptr->cpus = 1;
	config_ptr->real_memory = 1;
	config_ptr->threads = 1;
	config_ptr->tot_sockets = 1;
	config_ptr->weight = 1;
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
	_init_config_record(config_ptr);
	list_append(config_list, config_ptr);

	last_node_update = time (NULL);

	return config_ptr;
}


/*
 * Convert CPU list to reserve whole cores
 * OUT:
 *	node_ptr->cpu_spec_list
 */
static int _convert_cpu_spec_list(node_record_t *node_ptr)
{
	int i;
	bitstr_t *cpu_spec_bitmap;

	/* create CPU bitmap from input CPU list */
	cpu_spec_bitmap = bit_alloc(node_ptr->cpus);

	/* Expand CPU bitmap to reserve whole cores */
	for (i = 0; i < node_ptr->tot_cores; i++) {
		if (!bit_test(node_ptr->node_spec_bitmap, i)) {
			/* typecast to int to avoid coverity error */
			bit_nset(cpu_spec_bitmap,
				 (i * (int) node_ptr->tpc),
				 ((i + 1) * (int) node_ptr->tpc) - 1);
		}
	}
	xfree(node_ptr->cpu_spec_list);
	node_ptr->cpu_spec_list = bit_fmt_full(cpu_spec_bitmap);

	FREE_NULL_BITMAP(cpu_spec_bitmap);

	return SLURM_SUCCESS;
}

static void _init_node_record(node_record_t *node_ptr,
			      config_record_t *config_ptr)
{
	/*
	 * Some of these vars will be overwritten when the node actually
	 * registers.
	 */
	node_ptr->magic = NODE_MAGIC;
	node_ptr->cpu_load = 0;
	node_ptr->energy = acct_gather_energy_alloc(1);
	node_ptr->free_mem = NO_VAL64;
	node_ptr->next_state = NO_VAL;
	node_ptr->owner = NO_VAL;
	node_ptr->port = slurm_conf.slurmd_port;
	node_ptr->protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	node_ptr->resume_timeout = NO_VAL16;
	if (running_in_slurmctld())
		node_ptr->select_nodeinfo = select_g_select_nodeinfo_alloc();
	node_ptr->suspend_time = NO_VAL;
	node_ptr->suspend_timeout = NO_VAL16;

	node_ptr->config_ptr = config_ptr;
	node_ptr->boards = config_ptr->boards;
	node_ptr->core_spec_cnt = config_ptr->core_spec_cnt;
	node_ptr->cores = config_ptr->cores;
	node_ptr->cpus = config_ptr->cpus;
	node_ptr->mem_spec_limit = config_ptr->mem_spec_limit;
	node_ptr->real_memory = config_ptr->real_memory;
	node_ptr->res_cores_per_gpu = config_ptr->res_cores_per_gpu;
	node_ptr->threads = config_ptr->threads;
	node_ptr->tmp_disk = config_ptr->tmp_disk;
	node_ptr->tot_sockets = config_ptr->tot_sockets;
	node_ptr->tot_cores = config_ptr->tot_sockets * config_ptr->cores;
	node_ptr->weight = config_ptr->weight;

	/*
	 * Here we determine if this node is scheduling threads or not.
	 * We will set tpc to be the number of schedulable threads per core.
	 */
	if (node_ptr->tot_cores >= config_ptr->cpus)
		node_ptr->tpc = 1;
	else
		node_ptr->tpc = config_ptr->threads;

	node_ptr->cpu_spec_list = xstrdup(config_ptr->cpu_spec_list);
	if (node_ptr->cpu_spec_list) {
		build_node_spec_bitmap(node_ptr);
		if (node_ptr->tpc > 1)
			_convert_cpu_spec_list(node_ptr);
	} else if (node_ptr->core_spec_cnt) {
		_select_spec_cores(node_ptr);
	}

	node_ptr->cpus_efctv = node_ptr->cpus -
		(node_ptr->core_spec_cnt * node_ptr->tpc);
}

extern void grow_node_record_table_ptr(void)
{
	node_record_table_size = node_record_count + 100;
	if (slurm_conf.max_node_cnt != NO_VAL)
		node_record_table_size = MAX(node_record_count,
					     slurm_conf.max_node_cnt);

	xrealloc(node_record_table_ptr,
		 node_record_table_size * sizeof(node_record_t *));
	/*
	 * You need to rehash the hash after we realloc or we will have
	 * only bad memory references in the hash.
	 */
	rehash_node();
}

/*
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * OUT node_ptr - node_record_t** with created node on SUCESS, NULL otherwise.
 * RET SUCESS, or error code
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when
 *	the global node table is no longer required
 */
extern int create_node_record(config_record_t *config_ptr, char *node_name,
			      node_record_t **node_ptr)
{
	xassert(config_ptr);
	xassert(node_name);
	xassert(node_ptr);

	if (node_record_count >= node_record_table_size)
		grow_node_record_table_ptr();

	if (!(*node_ptr = create_node_record_at(node_record_count,
						node_name, config_ptr)))
		return ESLURM_NODE_TABLE_FULL;
	node_record_count++;

	return SLURM_SUCCESS;
}

extern node_record_t *create_node_record_at(int index, char *node_name,
					    config_record_t *config_ptr)
{
	node_record_t *node_ptr;
	last_node_update = time(NULL);

	xassert(index <= node_record_count);
	xassert(!node_record_table_ptr[index]);

	if ((slurm_conf.max_node_cnt != NO_VAL) &&
	    (index >= slurm_conf.max_node_cnt)) {
		error("Attempting to create node record past MaxNodeCount:%d",
		      slurm_conf.max_node_cnt);
		return NULL;
	} else if (index > MAX_SLURM_NODES) {
		error("Attempting to create nodes past max node limit (%d)",
		      MAX_SLURM_NODES);
		return NULL;
	}

	if (index > last_node_index)
		last_node_index = index;

	node_ptr = node_record_table_ptr[index] = xmalloc(sizeof(*node_ptr));
	node_ptr->index = index;
	node_ptr->name = xstrdup(node_name);
	xhash_add(node_hash_table, node_ptr);
	active_node_record_count++;

	_init_node_record(node_ptr, config_ptr);

	return node_ptr;
}

extern int add_node_record(char *alias, config_record_t *config_ptr,
			   node_record_t **node_ptr)
{
	int rc = SLURM_SUCCESS;

	xassert(node_ptr);

	if ((*node_ptr = find_node_record2(alias))) {
		rc = ESLURM_NODE_ALREADY_EXISTS;
		goto end;
	}

	for (int i = 0; i < node_record_count; i++) {
		if (node_record_table_ptr[i])
			continue;

		if (!(*node_ptr =
			create_node_record_at(i, alias, config_ptr))) {
			rc = ESLURM_NODE_TABLE_FULL;
			goto end;
		}

		bit_set(config_ptr->node_bitmap, i);

		gres_init_node_config((*node_ptr)->config_ptr->gres,
				      &(*node_ptr)->gres_list);

		break;
	}
	if (!(*node_ptr))
		rc = ESLURM_NODE_TABLE_FULL;

end:
	return rc;
}

static int _find_config_ptr(void *x, void *arg)
{
	return (x == arg);
}

extern void insert_node_record_at(node_record_t *node_ptr, int index)
{
	xassert(node_ptr);
	xassert(node_ptr->config_ptr);

	if (node_record_table_ptr[index]) {
		error("existing node '%s' already exists at index %d, can't add node '%s'",
		      node_record_table_ptr[index]->name, index,
		      node_ptr->name);
		return;
	}

	if (index >= node_record_count) {
		error("trying to add node '%s' at index %d past node_record_count %d",
		      node_ptr->name, index, node_record_count);
		return;
	}

	if (index > last_node_index)
		last_node_index = index;

	if (!node_ptr->config_ptr)
		error("node should have config_ptr from previous tables");

	if (!list_find_first(config_list, _find_config_ptr,
			     node_ptr->config_ptr))
		list_append(config_list, node_ptr->config_ptr);

	node_record_table_ptr[index] = node_ptr;
	/*
	 * _build_bitmaps_pre_select() will reset bitmaps on
	 * start/reconfig. Set here to be consistent in case this is
	 * called elsewhere.
	 */
	bit_clear(node_ptr->config_ptr->node_bitmap, node_ptr->index);
	node_ptr->index = index;
	bit_set(node_ptr->config_ptr->node_bitmap, node_ptr->index);
	xhash_add(node_hash_table, node_ptr);
	active_node_record_count++;

	/* add node to conf node hash tables */
	slurm_conf_remove_node(node_ptr->name);
	slurm_conf_add_node(node_ptr);
}

extern void delete_node_record(node_record_t *node_ptr)
{
	xassert(node_ptr);

	node_record_table_ptr[node_ptr->index] = NULL;

	if (node_ptr->index == last_node_index) {
		int i = 0;
		for (i = last_node_index - 1; i >=0; i--) {
			if (node_record_table_ptr[i]) {
				last_node_index = i;
				break;
			}
		}
		if (i < 0)
			last_node_index = -1;
	}
	active_node_record_count--;

	_delete_node_config_ptr(node_ptr);

	purge_node_rec(node_ptr);
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

	if ((node_record_count == 1) && node_record_table_ptr[0] &&
	    (xstrcmp(node_record_table_ptr[0]->name, "localhost") == 0))
		return (node_record_table_ptr[0]);

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

	for (i = 0; (node_ptr = next_node(&i)); i++)
		delete_node_record(node_ptr);

	node_record_count = 0;
	node_record_table_size = 0;
	last_node_index = -1;
	xfree(node_record_table_ptr);
	xhash_free(node_hash_table);

	if (config_list)	/* delete defunct configuration entries */
		_delete_config_record();
	else {
		config_list    = list_create (_list_delete_config);
		front_end_list = list_create (destroy_frontend);
	}
	if (xstrcasestr(slurm_conf.sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;
}


/* node_fini2 - free memory associated with node records (except bitmaps) */
extern void node_fini2(void)
{
	int i;
	node_record_t *node_ptr;

	xhash_free(node_hash_table);
	for (i = 0; (node_ptr = next_node(&i)); i++)
		delete_node_record(node_ptr);

	if (config_list) {
		/*
		 * Must free after purge_node_rec as purge_node_rec will remove
		 * node config_ptr's.
		 */
		FREE_NULL_LIST(config_list);
		FREE_NULL_LIST(front_end_list);
	}

	xfree(node_record_table_ptr);
	/*
	 * Don't clear node_record_count because other plugins are relying on
	 * node_record_count to free arrays on cleanup -- e.g.
	 * free_core_array().
	 *
	 * node_record_count = 0;
	 */
}

extern int node_name_get_inx(char *node_name)
{
	node_record_t *node_ptr = NULL;

	if (node_name)
		node_ptr = find_node_record(node_name);

	if (!node_ptr)
		return -1;

	return node_ptr->index;
}

extern void add_nodes_with_feature_to_bitmap(bitstr_t *bitmap, char *feature)
{
	if (avail_feature_list) {
		node_feature_t *node_feat_ptr;
		if (!(node_feat_ptr = list_find_first_ro(avail_feature_list,
							 list_find_feature,
							 feature))) {
			debug2("unable to find nodeset feature '%s'", feature);
			return;
		}
		bit_or(bitmap, node_feat_ptr->node_bitmap);
	} else {
		node_record_t *node_ptr;
		/*
		 * The global feature bitmaps have not been set up at this
		 * point, so we'll have to scan through the node_record_table
		 * directly to locate the appropriate records.
		 */
		for (int i = 0; (node_ptr = next_node(&i)); i++) {
			char *features, *tmp, *tok, *last = NULL;

			if (!node_ptr->features)
				continue;

			features = tmp = xstrdup(node_ptr->features);

			while ((tok = strtok_r(tmp, ",", &last))) {
				if (!xstrcmp(tok, feature)) {
					bit_set(bitmap, node_ptr->index);
					break;
				}
				tmp = NULL;
			}
			xfree(features);
		}
	}
}

static int _parse_hostlist_function(bitstr_t *node_bitmap, char *node_str)
{
	int rc = SLURM_SUCCESS;
	char *start_ptr, *end_ptr;

	start_ptr = xstrchr(node_str, '{') + 1;
	end_ptr = xstrchr(start_ptr, '}');

	if (!end_ptr) {
		error("%s: invalid node specified in hostlist function: \"%s\" (missing closing '}')",
		      __func__, node_str);
		return SLURM_ERROR;
	}

	*end_ptr = '\0';

	if (!xstrncmp("blockwith{", node_str, 10) ||
	    !xstrncmp("switchwith{", node_str, 11)) {
		node_record_t *node_ptr;
		bitstr_t *tmp_bitmap = bit_alloc(node_record_count);

		node_ptr = _find_node_record(start_ptr, false, true);
		if (node_ptr) {
			bit_set(tmp_bitmap, node_ptr->index);
			topology_g_whole_topo(tmp_bitmap);
			bit_or(node_bitmap, tmp_bitmap);
		} else {
			rc = SLURM_ERROR;
			error("%s: invalid node specified in hostlist function: \"%s\"",
			      __func__, node_str);
		}

		FREE_NULL_BITMAP(tmp_bitmap);
	} else if (!xstrncmp("block{", node_str, 6) ||
		   !xstrncmp("switch{", node_str, 7)) {
		bitstr_t *tmp_bitmap = topology_g_get_bitmap(start_ptr);

		if (tmp_bitmap) {
			bit_or(node_bitmap, tmp_bitmap);
		} else {
			rc = SLURM_ERROR;
			error("%s: invalid block or switch specified in hostlist function: \"%s\"",
			      __func__, node_str);
		}
	} else if (!xstrncmp("feature{", node_str, 8)) {
		add_nodes_with_feature_to_bitmap(node_bitmap, start_ptr);
	} else {
		rc = SLURM_ERROR;
		error("Invalid hostlist_function specified: %s", node_str);
	}

	*end_ptr = '}';
	return rc;
}

extern int parse_hostlist_functions(hostlist_t **hostlist)
{
	hostlist_t *new_hostlist = hostlist_create(NULL);
	node_record_t *node_ptr;
	char *host;
	int rc = SLURM_SUCCESS;

	while ((host = hostlist_shift(*hostlist))) {
		if ((strchr(host, '{'))) {
			bitstr_t *node_bitmap = bit_alloc(node_record_count);
			if (_parse_hostlist_function(node_bitmap, host)) {
				rc = SLURM_ERROR;
			} else {
				for (int i = 0; (node_ptr = next_node_bitmap(
							 node_bitmap, &i));
				     i++) {
					hostlist_push_host(new_hostlist,
							   node_ptr->name);
				}
			}
			FREE_NULL_BITMAP(node_bitmap);
		} else {
			hostlist_push_host(new_hostlist, host);
		}
		free(host);
	}
	hostlist_destroy(*hostlist);
	*hostlist = new_hostlist;

	return rc;
}

static int _single_node_name2bitmap(char *node_name, bool test_alias,
				    bitstr_t *bitmap,
				    hostlist_t **invalid_hostlist)
{
	int rc = SLURM_SUCCESS;

	/* If there are curly brackets it must be a hostlist_function */
	if ((xstrchr(node_name, '{'))) {
		rc = _parse_hostlist_function(bitmap, node_name);
	} else {
		node_record_t *node_ptr;
		node_ptr = _find_node_record(node_name, test_alias, true);
		if (node_ptr)
			bit_set(bitmap, node_ptr->index);
		else
			rc = SLURM_ERROR;
	}

	if ((rc != SLURM_SUCCESS) && invalid_hostlist) {
		debug2("%s: invalid node specified: \"%s\"",
		       __func__, node_name);
		if (*invalid_hostlist) {
			hostlist_push_host(*invalid_hostlist, node_name);
		} else {
			*invalid_hostlist = hostlist_create(node_name);
		}
		rc = SLURM_SUCCESS;
	} else if (rc != SLURM_SUCCESS) {
		error("%s: invalid node specified: \"%s\"",
		      __func__, node_name);
		rc = EINVAL;
	}

	return rc;
}

/*
 * node_name2bitmap - given a node name regular expression, build a bitmap
 *	representation
 * IN node_names  - list of nodes
 * IN: test_alias - if set, also test NodeHostName value
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * RET 0 if no error, otherwise EINVAL
 * NOTE: call FREE_NULL_BITMAP() to free bitmap memory when no longer required
 */
extern int node_name2bitmap(char *node_names, bool test_alias,
			    bitstr_t **bitmap, hostlist_t **invalid_hostlist)
{
	int rc = SLURM_SUCCESS;
	char *this_node_name;
	hostlist_t *host_list;

	*bitmap = bit_alloc(node_record_count);

	if (!node_names) {
		info("%s: node_names is NULL", __func__);
		return rc;
	}

	if (!(host_list = hostlist_create(node_names))) {
		/* likely a badly formatted hostlist */
		error("hostlist_create on %s error:", node_names);
		return EINVAL;
	}

	while ((this_node_name = hostlist_shift(host_list))) {
		rc = _single_node_name2bitmap(this_node_name, test_alias,
					      *bitmap, invalid_hostlist);
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	return rc;
}

/*
 * hostlist2bitmap - given a hostlist, build a bitmap representation
 * IN hl          - hostlist
 * IN: test_alias - if set, also test NodeHostName value
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * RET 0 if no error, otherwise EINVAL
 */
extern int hostlist2bitmap(hostlist_t *hl, bool test_alias, bitstr_t **bitmap)
{
	int rc = SLURM_SUCCESS;
	bitstr_t *my_bitmap;
	char *name;
	hostlist_iterator_t *hi;

	FREE_NULL_BITMAP(*bitmap);
	my_bitmap = bit_alloc(node_record_count);
	*bitmap = my_bitmap;

	hi = hostlist_iterator_create(hl);
	while ((name = hostlist_next(hi))) {
		int tmp_rc;
		if ((tmp_rc = _single_node_name2bitmap(name, test_alias,
						       *bitmap, NULL)))
			rc = tmp_rc;
		free(name);
	}

	hostlist_iterator_destroy(hi);
	return rc;
}

/* Only delete config_ptr if isn't referenced by another node. */
static void _delete_node_config_ptr(node_record_t *node_ptr)
{
	bool delete = true;
	node_record_t *tmp_ptr;
	config_record_t *this_config_ptr;

	if (!node_ptr->config_ptr)
		return;

	/* clear in case config_ptr is still referenced by other nodes */
	if (node_ptr->config_ptr->node_bitmap)
		bit_clear(node_ptr->config_ptr->node_bitmap, node_ptr->index);

	this_config_ptr = node_ptr->config_ptr;
	node_ptr->config_ptr = NULL;

	for (int i = 0; (tmp_ptr = next_node(&i)); i++) {
		if (tmp_ptr->config_ptr == this_config_ptr) {
			delete = false;
			break;
		}
	}
	if (delete)
		list_delete_ptr(config_list, this_config_ptr);
}

/* Purge the contents of a node record */
extern void purge_node_rec(void *in)
{
	node_record_t *node_ptr = in;

	xfree(node_ptr->arch);
	xfree(node_ptr->cert_token);
	xfree(node_ptr->comment);
	xfree(node_ptr->comm_name);
	xfree(node_ptr->cpu_spec_list);
	xfree(node_ptr->extra);
	FREE_NULL_DATA(node_ptr->extra_data);
	xfree(node_ptr->features);
	xfree(node_ptr->features_act);
	xfree(node_ptr->gpu_spec);
	FREE_NULL_BITMAP(node_ptr->gpu_spec_bitmap);
	xfree(node_ptr->gres);
	FREE_NULL_LIST(node_ptr->gres_list);
	xfree(node_ptr->instance_id);
	xfree(node_ptr->instance_type);
	xfree(node_ptr->mcs_label);
	xfree(node_ptr->name);
	xfree(node_ptr->node_hostname);
	FREE_NULL_BITMAP(node_ptr->node_spec_bitmap);
	xfree(node_ptr->os);
	xfree(node_ptr->part_pptr);
	xfree(node_ptr->reason);
	xfree(node_ptr->resv_name);
	xfree(node_ptr->version);
	acct_gather_energy_destroy(node_ptr->energy);
	if (running_in_slurmctld())
		select_g_select_nodeinfo_free(node_ptr->select_nodeinfo);
	xfree(node_ptr->tres_str);
	xfree(node_ptr->tres_fmt_str);
	xfree(node_ptr->tres_cnt);

	xfree(node_ptr);
}

/*
 * rehash_node - build a hash table of the node_record entries.
 * NOTE: using xhash implementation
 */
extern void rehash_node(void)
{
	int i;
	node_record_t *node_ptr;

	xhash_free (node_hash_table);
	node_hash_table = xhash_init(_node_record_hash_identity, NULL);
	for (i = 0; (node_ptr = next_node(&i)); i++) {
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
extern void cr_init_global_core_data(node_record_t **node_ptr, int node_cnt)
{
	uint32_t n;
	int prev_index = 0;

	cr_fini_global_core_data();

	cr_node_num_cores = xcalloc(node_cnt, sizeof(uint16_t));
	cr_node_cores_offset = xcalloc(node_cnt + 1, sizeof(uint32_t));

	for (n = 0; n < node_cnt; n++) {
		if (!node_ptr[n])
			continue;

		cr_node_num_cores[n] = node_ptr[n]->tot_cores;
		if (n > 0) {
			cr_node_cores_offset[n] =
				cr_node_cores_offset[prev_index] +
				cr_node_num_cores[prev_index] ;
			prev_index = n;
		} else
			cr_node_cores_offset[0] = 0;
	}

	/* an extra value is added to get the total number of cores */
	/* as cr_get_coremap_offset is sometimes used to get the total */
	/* number of cores in the cluster */
	cr_node_cores_offset[node_cnt] = cr_node_cores_offset[prev_index] +
					 cr_node_num_cores[prev_index] ;

}

extern void cr_fini_global_core_data(void)
{
	xfree(cr_node_num_cores);
	xfree(cr_node_cores_offset);
}

/*
 * Return the coremap index to the first core of the given node
 *
 * If node_index points to NULL record in the node_record_table_ptr, then it
 * will attempt to find the next available node. If a valid node isn't found,
 * then the last core offset will be returned --
 * cr_node_cores_offset[node_record_count].
 */
extern uint32_t cr_get_coremap_offset(uint32_t node_index)
{
	xassert(cr_node_cores_offset);
	if (next_node((int *)&node_index))
		return cr_node_cores_offset[node_index];

	return cr_node_cores_offset[node_record_count];
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
	hostlist_t *hostlist = NULL;
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

extern node_record_t *next_node(int *index)
{
	xassert(index);

	if (!node_record_table_ptr ||
	    (*index >= node_record_count))
		return NULL;

	while (!node_record_table_ptr[*index]) {
		(*index)++;
		if (*index >= node_record_count)
			return NULL;
		if (*index > last_node_index)
			return NULL;
	}

	xassert(node_record_table_ptr[*index]->index == *index);

	return node_record_table_ptr[*index];
}

extern node_record_t *next_node_bitmap(bitstr_t *bitmap, int *index)
{
	xassert(index);

	if (!node_record_table_ptr ||
	    (*index >= node_record_count))
		return NULL;

	xassert(bitmap);
	xassert(bit_size(bitmap) == node_record_count);

	while (true) {
		*index = bit_ffs_from_bit(bitmap, *index);
		if (*index == -1)
			return NULL;
		if (node_record_table_ptr[*index])
			break;
		(*index)++;  /* Skip blank entries */
	}

	xassert(node_record_table_ptr[*index]->index == *index);

	return node_record_table_ptr[*index];
}

extern bitstr_t *node_conf_get_active_bitmap(void)
{
	bitstr_t *b = bit_alloc(node_record_count);
	node_conf_set_all_active_bits(b);
	return b;
}

extern void node_conf_set_all_active_bits(bitstr_t *b)
{
	for (int i = 0; next_node(&i); i++)
		bit_set(b, i);
}

extern char *node_conf_nodestr_tokenize(char *s, char **save_ptr)
{
	char *end;

	if (s == NULL)
		s = *save_ptr;

	xassert(s); /* If s is NULL here we are using this function wrong */

	if (*s == '\0') {
		*save_ptr = s;
		return NULL;
	}

	/* token ends with a comma not followed by a digit */
	end = s;
	while (*end && ((end[0] != ',') || isdigit(end[1])))
		end++;

	if (*end)
		*end++ = '\0';
	*save_ptr = end;
	return s;
}

extern void node_conf_create_cluster_core_bitmap(bitstr_t **core_bitmap)
{
	if (*core_bitmap)
		return;

	*core_bitmap = bit_alloc(cr_get_coremap_offset(node_record_count));
}

/*
 * Pack node_record_t to buffer.
 *
 * Used for dumping node state and passing node_record_t between ctld and
 * stepmgr.
 */
static void _node_record_pack(void *in, uint16_t protocol_version,
			      buf_t *buffer, bool pack_secrets)
{
	node_record_t *object = in;

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		if (pack_secrets)
			packstr(object->cert_token, buffer);
		else
			packnull(buffer);

		packstr(object->comm_name, buffer);
		packstr(object->name, buffer);
		packstr(object->node_hostname, buffer);
		packstr(object->comment, buffer);
		packstr(object->extra, buffer);
		packstr(object->reason, buffer);
		packstr(object->features, buffer);
		packstr(object->features_act, buffer);
		packstr(object->gres, buffer);
		packstr(object->instance_id, buffer);
		packstr(object->instance_type, buffer);
		packstr(object->cpu_spec_list, buffer);
		pack32(object->next_state, buffer);
		pack32(object->node_state, buffer);
		pack32(object->cpu_bind, buffer);
		pack16(object->cpus, buffer);
		pack16(object->boards, buffer);
		pack16(object->tot_sockets, buffer);
		pack16(object->cores, buffer);
		pack16(object->core_spec_cnt, buffer);
		pack64(object->mem_spec_limit, buffer);
		pack16(object->threads, buffer);
		pack64(object->real_memory, buffer);
		pack16(object->res_cores_per_gpu, buffer);
		pack_bit_str_hex(object->gpu_spec_bitmap, buffer);
		pack32(object->tmp_disk, buffer);
		pack32(object->reason_uid, buffer);
		pack_time(object->reason_time, buffer);
		pack_time(object->resume_after, buffer);
		pack_time(object->boot_req_time, buffer);
		pack_time(object->power_save_req_time, buffer);
		pack_time(object->last_busy, buffer);
		pack_time(object->last_response, buffer);
		pack16(object->port, buffer);
		pack16(object->protocol_version, buffer);
		pack16(object->tpc, buffer);
		packstr(object->mcs_label, buffer);
		(void) gres_node_state_pack(object->gres_list, buffer,
					    protocol_version);
		pack32(object->weight, buffer);
	} else if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		packstr(object->comm_name, buffer);
		packstr(object->name, buffer);
		packstr(object->node_hostname, buffer);
		packstr(object->comment, buffer);
		packstr(object->extra, buffer);
		packstr(object->reason, buffer);
		packstr(object->features, buffer);
		packstr(object->features_act, buffer);
		packstr(object->gres, buffer);
		packstr(object->instance_id, buffer);
		packstr(object->instance_type, buffer);
		packstr(object->cpu_spec_list, buffer);
		pack32(object->next_state, buffer);
		pack32(object->node_state, buffer);
		pack32(object->cpu_bind, buffer);
		pack16(object->cpus, buffer);
		pack16(object->boards, buffer);
		pack16(object->tot_sockets, buffer);
		pack16(object->cores, buffer);
		pack16(object->core_spec_cnt, buffer);
		pack16(object->threads, buffer);
		pack64(object->real_memory, buffer);
		pack16(object->res_cores_per_gpu, buffer);
		pack_bit_str_hex(object->gpu_spec_bitmap, buffer);
		pack32(object->tmp_disk, buffer);
		pack32(object->reason_uid, buffer);
		pack_time(object->reason_time, buffer);
		pack_time(object->resume_after, buffer);
		pack_time(object->boot_req_time, buffer);
		pack_time(object->power_save_req_time, buffer);
		pack_time(object->last_busy, buffer);
		pack_time(object->last_response, buffer);
		pack16(object->port, buffer);
		pack16(object->protocol_version, buffer);
		pack16(object->tpc, buffer);
		packstr(object->mcs_label, buffer);
		(void) gres_node_state_pack(object->gres_list, buffer,
					    protocol_version);
		pack32(object->weight, buffer);
	}
}

extern void node_record_pack(void *in, uint16_t protocol_version,
			     buf_t *buffer)
{
	_node_record_pack(in, protocol_version, buffer, false);
}

extern void node_record_pack_state(void *in, uint16_t protocol_version,
				   buf_t *buffer)
{
	_node_record_pack(in, protocol_version, buffer, true);
}

extern int node_record_unpack(void **out,
			      uint16_t protocol_version,
			      buf_t *buffer)
{
	node_record_t *object = xmalloc(sizeof(*object));
	object->magic = NODE_MAGIC;
	*out = object;

	if (protocol_version >= SLURM_24_11_PROTOCOL_VERSION) {
		safe_unpackstr(&object->cert_token, buffer);
		safe_unpackstr(&object->comm_name, buffer);
		safe_unpackstr(&object->name, buffer);
		safe_unpackstr(&object->node_hostname, buffer);
		safe_unpackstr(&object->comment, buffer);
		safe_unpackstr(&object->extra, buffer);
		safe_unpackstr(&object->reason, buffer);
		safe_unpackstr(&object->features, buffer);
		safe_unpackstr(&object->features_act, buffer);
		safe_unpackstr(&object->gres, buffer);
		safe_unpackstr(&object->instance_id, buffer);
		safe_unpackstr(&object->instance_type, buffer);
		safe_unpackstr(&object->cpu_spec_list, buffer);
		safe_unpack32(&object->next_state, buffer);
		safe_unpack32(&object->node_state, buffer);
		safe_unpack32(&object->cpu_bind, buffer);
		safe_unpack16(&object->cpus, buffer);
		safe_unpack16(&object->boards, buffer);
		safe_unpack16(&object->tot_sockets, buffer);
		safe_unpack16(&object->cores, buffer);
		safe_unpack16(&object->core_spec_cnt, buffer);
		safe_unpack64(&object->mem_spec_limit, buffer);
		safe_unpack16(&object->threads, buffer);
		safe_unpack64(&object->real_memory, buffer);
		safe_unpack16(&object->res_cores_per_gpu, buffer);
		unpack_bit_str_hex(&object->gpu_spec_bitmap, buffer);
		safe_unpack32(&object->tmp_disk, buffer);
		safe_unpack32(&object->reason_uid, buffer);
		safe_unpack_time(&object->reason_time, buffer);
		safe_unpack_time(&object->resume_after, buffer);
		safe_unpack_time(&object->boot_req_time, buffer);
		safe_unpack_time(&object->power_save_req_time, buffer);
		safe_unpack_time(&object->last_busy, buffer);
		safe_unpack_time(&object->last_response, buffer);
		safe_unpack16(&object->port, buffer);
		safe_unpack16(&object->protocol_version, buffer);
		safe_unpack16(&object->tpc, buffer);
		safe_unpackstr(&object->mcs_label, buffer);
		if (gres_node_state_unpack(&object->gres_list, buffer,
					   object->name, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&object->weight, buffer);
	} else if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		safe_unpackstr(&object->comm_name, buffer);
		safe_unpackstr(&object->name, buffer);
		safe_unpackstr(&object->node_hostname, buffer);
		safe_unpackstr(&object->comment, buffer);
		safe_unpackstr(&object->extra, buffer);
		safe_unpackstr(&object->reason, buffer);
		safe_unpackstr(&object->features, buffer);
		safe_unpackstr(&object->features_act, buffer);
		safe_unpackstr(&object->gres, buffer);
		safe_unpackstr(&object->instance_id, buffer);
		safe_unpackstr(&object->instance_type, buffer);
		safe_unpackstr(&object->cpu_spec_list, buffer);
		safe_unpack32(&object->next_state, buffer);
		safe_unpack32(&object->node_state, buffer);
		safe_unpack32(&object->cpu_bind, buffer);
		safe_unpack16(&object->cpus, buffer);
		safe_unpack16(&object->boards, buffer);
		safe_unpack16(&object->tot_sockets, buffer);
		safe_unpack16(&object->cores, buffer);
		safe_unpack16(&object->core_spec_cnt, buffer);
		safe_unpack16(&object->threads, buffer);
		safe_unpack64(&object->real_memory, buffer);
		safe_unpack16(&object->res_cores_per_gpu, buffer);
		unpack_bit_str_hex(&object->gpu_spec_bitmap, buffer);
		safe_unpack32(&object->tmp_disk, buffer);
		safe_unpack32(&object->reason_uid, buffer);
		safe_unpack_time(&object->reason_time, buffer);
		safe_unpack_time(&object->resume_after, buffer);
		safe_unpack_time(&object->boot_req_time, buffer);
		safe_unpack_time(&object->power_save_req_time, buffer);
		safe_unpack_time(&object->last_busy, buffer);
		safe_unpack_time(&object->last_response, buffer);
		safe_unpack16(&object->port, buffer);
		safe_unpack16(&object->protocol_version, buffer);
		safe_unpack16(&object->tpc, buffer);
		safe_unpackstr(&object->mcs_label, buffer);
		if (gres_node_state_unpack(&object->gres_list, buffer,
					   object->name, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&object->weight, buffer);
	} else if (protocol_version >= SLURM_23_11_PROTOCOL_VERSION) {
		safe_unpackstr(&object->comm_name, buffer);
		safe_unpackstr(&object->name, buffer);
		safe_unpackstr(&object->node_hostname, buffer);
		safe_unpackstr(&object->comment, buffer);
		safe_unpackstr(&object->extra, buffer);
		safe_unpackstr(&object->reason, buffer);
		safe_unpackstr(&object->features, buffer);
		safe_unpackstr(&object->features_act, buffer);
		safe_unpackstr(&object->gres, buffer);
		safe_unpackstr(&object->instance_id, buffer);
		safe_unpackstr(&object->instance_type, buffer);
		safe_unpackstr(&object->cpu_spec_list, buffer);
		safe_unpack32(&object->next_state, buffer);
		safe_unpack32(&object->node_state, buffer);
		safe_unpack32(&object->cpu_bind, buffer);
		safe_unpack16(&object->cpus, buffer);
		safe_unpack16(&object->boards, buffer);
		safe_unpack16(&object->tot_sockets, buffer);
		safe_unpack16(&object->cores, buffer);
		safe_unpack16(&object->core_spec_cnt, buffer);
		safe_unpack16(&object->threads, buffer);
		safe_unpack64(&object->real_memory, buffer);
		safe_unpack32(&object->tmp_disk, buffer);
		safe_unpack32(&object->reason_uid, buffer);
		safe_unpack_time(&object->reason_time, buffer);
		safe_unpack_time(&object->resume_after, buffer);
		safe_unpack_time(&object->boot_req_time, buffer);
		safe_unpack_time(&object->power_save_req_time, buffer);
		safe_unpack_time(&object->last_response, buffer);
		safe_unpack16(&object->port, buffer);
		safe_unpack16(&object->protocol_version, buffer);
		safe_unpackstr(&object->mcs_label, buffer);
		if (gres_node_state_unpack(&object->gres_list, buffer,
					   object->name, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&object->weight, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr(&object->comm_name, buffer);
		safe_unpackstr(&object->name, buffer);
		safe_unpackstr(&object->node_hostname, buffer);
		safe_unpackstr(&object->comment, buffer);
		safe_unpackstr(&object->extra, buffer);
		safe_unpackstr(&object->reason, buffer);
		safe_unpackstr(&object->features, buffer);
		safe_unpackstr(&object->features_act, buffer);
		safe_unpackstr(&object->gres, buffer);
		safe_unpackstr(&object->cpu_spec_list, buffer);
		safe_unpack32(&object->next_state, buffer);
		safe_unpack32(&object->node_state, buffer);
		safe_unpack32(&object->cpu_bind, buffer);
		safe_unpack16(&object->cpus, buffer);
		safe_unpack16(&object->boards, buffer);
		safe_unpack16(&object->tot_sockets, buffer);
		safe_unpack16(&object->cores, buffer);
		safe_unpack16(&object->core_spec_cnt, buffer);
		safe_unpack16(&object->threads, buffer);
		safe_unpack64(&object->real_memory, buffer);
		safe_unpack32(&object->tmp_disk, buffer);
		safe_unpack32(&object->reason_uid, buffer);
		safe_unpack_time(&object->reason_time, buffer);
		safe_unpack_time(&object->resume_after, buffer);
		safe_unpack_time(&object->boot_req_time, buffer);
		safe_unpack_time(&object->power_save_req_time, buffer);
		safe_unpack_time(&object->last_response, buffer);
		safe_unpack16(&object->protocol_version, buffer);
		safe_unpackstr(&object->mcs_label, buffer);
		if (gres_node_state_unpack(&object->gres_list, buffer,
					   object->name, protocol_version) !=
		    SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&object->weight, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	purge_node_rec(object);
	*out = NULL;
	return SLURM_ERROR;
}

extern config_record_t *config_record_from_node_record(node_record_t *node_ptr)
{
	config_record_t *config_ptr = create_config_record();

	config_ptr->boards = node_ptr->boards;
	config_ptr->core_spec_cnt = node_ptr->core_spec_cnt;
	config_ptr->mem_spec_limit = node_ptr->mem_spec_limit;
	config_ptr->cores = node_ptr->cores;
	config_ptr->cpu_spec_list = xstrdup(node_ptr->cpu_spec_list);
	config_ptr->cpus = node_ptr->cpus;
	config_ptr->feature = xstrdup(node_ptr->features);
	config_ptr->gres = xstrdup(node_ptr->gres);
	config_ptr->node_bitmap = bit_alloc(node_record_count);
	config_ptr->nodes = xstrdup(node_ptr->name);
	config_ptr->real_memory = node_ptr->real_memory;
	config_ptr->res_cores_per_gpu = node_ptr->res_cores_per_gpu;
	config_ptr->threads = node_ptr->threads;
	config_ptr->tmp_disk = node_ptr->tmp_disk;
	config_ptr->tot_sockets = node_ptr->tot_sockets;
	config_ptr->weight = node_ptr->weight;

	return config_ptr;
}

/*
 * list_find_feature - find an entry in the feature list, see list.h for
 *	documentation
 * IN key - is feature name or NULL for all features
 * RET 1 if found, 0 otherwise
 */
extern int list_find_feature(void *feature_entry, void *key)
{
	node_feature_t *feature_ptr = feature_entry;

	if (!key)
		return 1;

	return !xstrcmp(feature_ptr->name, (char *) key);
}
