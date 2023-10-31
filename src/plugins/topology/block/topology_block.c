/*****************************************************************************\
 *  topology_block.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Dominik Bartkiewicz <bart@schedmd.com>
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

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/interfaces/topology.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#include "../common/common_topo.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern uint16_t bblock_node_cnt __attribute__((weak_import));
extern block_record_t *block_record_table __attribute__((weak_import));
extern bitstr_t *block_levels __attribute__((weak_import));
extern bitstr_t *blocks_nodes_bitmap __attribute__((weak_import));
extern int block_record_cnt __attribute__((weak_import));
extern int active_node_record_count __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
int node_record_count;
uint16_t bblock_node_cnt;
block_record_t *block_record_table;
bitstr_t *block_levels;
bitstr_t *blocks_nodes_bitmap;
int block_record_cnt;
int active_node_record_count;
#endif

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "topology block plugin";
const char plugin_type[]        = "topology/block";
const uint32_t plugin_id = TOPOLOGY_PLUGIN_BLOCK;
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef struct slurm_conf_block {
	char *block_name; /* name of this block */
	char *nodes; /* names of nodes directly connect to this block */
} slurm_conf_block_t;

typedef struct topoinfo_bblock {
	uint16_t block_index;
	char *name;
	char *nodes;
} topoinfo_bblock_t;

typedef struct topoinfo_block {
	uint32_t record_count; /* number of records */
	topoinfo_bblock_t *topo_array;/* the block topology records */
} topoinfo_block_t;


static s_p_hashtbl_t *conf_hashtbl = NULL;

static void _destroy_block(void *ptr)
{
	slurm_conf_block_t *s = ptr;
	if (!s)
		return;

	xfree(s->nodes);
	xfree(s->block_name);
	xfree(s);
}

static int  _parse_block(void **dest, slurm_parser_enum_t type,
			 const char *key, const char *value,
			 const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl;
	slurm_conf_block_t *b;
	static s_p_options_t _block_options[] = {
		{"Nodes", S_P_STRING},
		{NULL}
	};

	tbl = s_p_hashtbl_create(_block_options);
	s_p_parse_line(tbl, *leftover, leftover);

	b = xmalloc(sizeof(slurm_conf_block_t));
	b->block_name = xstrdup(value);
	s_p_get_string(&b->nodes, "Nodes", tbl);
	s_p_hashtbl_destroy(tbl);

	if (!b->nodes) {
		error("block %s hasn't got nodes",
		      b->block_name);
		_destroy_block(b);
		return -1;
	}

	*dest = (void *)b;

	return 1;
}

/* Return count of block configuration entries read */
static int _read_topo_file(slurm_conf_block_t **ptr_array[])
{
	static s_p_options_t block_options[] = {
		{"BlockName", S_P_ARRAY, _parse_block, _destroy_block},
		{"BlockLevels", S_P_STRING},
		{NULL}
	};
	int count;
	slurm_conf_block_t **ptr;
	char *tmp_str = NULL;

	xassert(topo_conf);
	debug("Reading the %s file", topo_conf);

	conf_hashtbl = s_p_hashtbl_create(block_options);
	if (s_p_parse_file(conf_hashtbl, NULL, topo_conf, false, NULL) ==
	    SLURM_ERROR) {
		s_p_hashtbl_destroy(conf_hashtbl);
		fatal("something wrong with opening/reading %s: %m",
		      topo_conf);
	}

	FREE_NULL_BITMAP(block_levels);
	block_levels = bit_alloc(16);

	if (!s_p_get_string(&tmp_str, "BlockLevels", conf_hashtbl)) {
		bit_nset(block_levels, 0, 4);
	} else if (bit_unfmt(block_levels, tmp_str)) {
		s_p_hashtbl_destroy(conf_hashtbl);
		fatal("Invalid BlockLevels");
	}
	xfree(tmp_str);

	if (s_p_get_array((void ***)&ptr, &count, "BlockName", conf_hashtbl))
		*ptr_array = ptr;
	else {
		*ptr_array = NULL;
		count = 0;
	}
	return count;
}

static void _log_blocks(void)
{
	int i;
	block_record_t *block_ptr;

	block_ptr = block_record_table;
	for (i = 0; i < block_record_cnt; i++, block_ptr++) {
		debug("Block name:%s nodes:%s",
		      block_ptr->name, block_ptr->nodes);
	}
}

/*
 * _node_name2bitmap - given a node name regular expression, build a bitmap
 *	representation, any invalid hostnames are added to a hostlist
 * IN node_names  - set of node namess
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * IN/OUT invalid_hostlist - hostlist of invalid host names, initialize to NULL
 * RET 0 if no error, otherwise EINVAL
 * NOTE: call FREE_NULL_BITMAP(bitmap) and hostlist_destroy(invalid_hostlist)
 *       to free memory when variables are no longer required
 */
static int _node_name2bitmap(char *node_names, bitstr_t **bitmap,
			     hostlist_t **invalid_hostlist)
{
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t *host_list;

	my_bitmap = bit_alloc(node_record_count);
	*bitmap = my_bitmap;

	if (!node_names) {
		error("_node_name2bitmap: node_names is NULL");
		return EINVAL;
	}

	if (!(host_list = hostlist_create(node_names))) {
		/* likely a badly formatted hostlist */
		error("_node_name2bitmap: hostlist_create(%s) error",
		      node_names);
		return EINVAL;
	}

	while ((this_node_name = hostlist_shift(host_list)) ) {
		node_record_t *node_ptr;
		node_ptr = find_node_record(this_node_name);
		if (node_ptr) {
			bit_set(my_bitmap, node_ptr->index);
		} else {
			debug2("invalid node specified %s", this_node_name);
			if (*invalid_hostlist) {
				hostlist_push_host(*invalid_hostlist,
						   this_node_name);
			} else {
				*invalid_hostlist =
					hostlist_create(this_node_name);
			}
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	return SLURM_SUCCESS;
}

/* Free all memory associated with block_record_table structure */
static void _free_block_record_table(void)
{
	int i;

	if (block_record_table) {
		for (i = 0; i < block_record_cnt; i++) {
			xfree(block_record_table[i].name);
			xfree(block_record_table[i].nodes);
			FREE_NULL_BITMAP(block_record_table[i].node_bitmap);
		}
		xfree(block_record_table);
		block_record_cnt = 0;
	}
}

static void _validate_blocks(void)
{
	slurm_conf_block_t *ptr, **ptr_array;
	int i, j;
	block_record_t *block_ptr, *prior_ptr;
	hostlist_t *invalid_hl = NULL;
	char *buf;

	_free_block_record_table();

	block_record_cnt = _read_topo_file(&ptr_array);
	if (block_record_cnt == 0) {
		error("No blocks configured");
		s_p_hashtbl_destroy(conf_hashtbl);
		return;
	}

	block_record_table = xcalloc(block_record_cnt,
				     sizeof(block_record_t));
	block_ptr = block_record_table;
	for (i = 0; i < block_record_cnt; i++, block_ptr++) {
		ptr = ptr_array[i];
		block_ptr->name = xstrdup(ptr->block_name);
		/* See if block name has already been defined. */
		prior_ptr = block_record_table;
		for (j = 0; j < i; j++, prior_ptr++) {
			if (xstrcmp(block_ptr->name, prior_ptr->name) == 0) {
				fatal("Block (%s) has already been defined",
				      prior_ptr->name);
			}
		}

		block_ptr->block_index = i;

		if (ptr->nodes) {
			block_ptr->nodes = xstrdup(ptr->nodes);
			if (_node_name2bitmap(ptr->nodes,
					      &block_ptr->node_bitmap,
					      &invalid_hl)) {
				fatal("Invalid node name (%s) in block "
				      "config (%s)",
				      ptr->nodes, ptr->block_name);
			}
			if (blocks_nodes_bitmap) {
				bit_or(blocks_nodes_bitmap,
				       block_ptr->node_bitmap);
			} else {
				blocks_nodes_bitmap = bit_copy(block_ptr->
							       node_bitmap);
			}
			if (bblock_node_cnt == 0) {
				bblock_node_cnt =
					bit_set_count(block_ptr->node_bitmap);
			} else if (bit_set_count(block_ptr->node_bitmap) !=
				   bblock_node_cnt) {
				fatal("Block configuration (%s) children count no equal bblock_node_cnt",
				      ptr->block_name);
			}
		} else {
			fatal("Block configuration (%s) lacks children",
			      ptr->block_name);
		}
	}

	if (blocks_nodes_bitmap) {
		i = bit_clear_count(blocks_nodes_bitmap);
		if (i > 0) {
			char *tmp_nodes;
			bitstr_t *tmp_bitmap = bit_copy(blocks_nodes_bitmap);
			bit_not(tmp_bitmap);
			tmp_nodes = bitmap2node_name(tmp_bitmap);
			warning("blocks lack access to %d nodes: %s",
				i, tmp_nodes);
			xfree(tmp_nodes);
			FREE_NULL_BITMAP(tmp_bitmap);
		}
	} else
		fatal("blocks contain no nodes");

	if (invalid_hl) {
		buf = hostlist_ranged_string_xmalloc(invalid_hl);
		warning("Invalid hostnames in block configuration: %s", buf);
		xfree(buf);
		hostlist_destroy(invalid_hl);
	}

	s_p_hashtbl_destroy(conf_hashtbl);
	_log_blocks();
}

static void _print_topo_record(topoinfo_bblock_t * topo_ptr, char **out)
{
	char *env, *line = NULL, *pos = NULL;

	/****** Line 1 ******/
	xstrfmtcatat(line, &pos, "BlockName=%s BlockIndex=%u", topo_ptr->name,
		     topo_ptr->block_index);

	if (topo_ptr->nodes)
		xstrfmtcatat(line, &pos, " Nodes=%s", topo_ptr->nodes);

	if ((env = getenv("SLURM_TOPO_LEN")))
		xstrfmtcat(*out, "%.*s\n", atoi(env), line);
	else
		xstrfmtcat(*out, "%s\n", line);

	xfree(line);

}

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	_free_block_record_table();
	FREE_NULL_BITMAP(blocks_nodes_bitmap);
	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topology_p_build_config(void)
{
	if (node_record_count)
		_validate_blocks();
	return SLURM_SUCCESS;
}

/*
 * When TopologyParam=SwitchAsNodeRank is set, this plugin assigns a unique
 * node_rank for all nodes belonging to the same bblock.
 */
extern bool topology_p_generate_node_ranking(void)
{
	return true;
}

/*
 * topo_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 *
 * example of output :
 *      address : b8.tux1
 *      pattern : block.node
 */
extern int topology_p_get_node_addr(char *node_name, char **paddr,
				    char **ppattern)
{
	return SLURM_SUCCESS;
}

extern int topology_p_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width)
{
	return common_topo_split_hostlist_treewidth(
		hl, sp_hl, count, tree_width);
}

extern int topology_p_topology_free(void *topoinfo_ptr)
{
	int i = 0;
	topoinfo_block_t *topoinfo = topoinfo_ptr;
	if (topoinfo) {
		if (topoinfo->topo_array) {
			for (i = 0; i < topoinfo->record_count; i++) {
				xfree(topoinfo->topo_array[i].name);
				xfree(topoinfo->topo_array[i].nodes);
			}
			xfree(topoinfo->topo_array);
		}
		xfree(topoinfo);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topology_get(void **topoinfo_pptr)
{
	int i = 0;
	topoinfo_block_t *topoinfo_ptr =
		xmalloc(sizeof(topoinfo_block_t));

	*topoinfo_pptr = topoinfo_ptr;
	topoinfo_ptr->record_count = block_record_cnt;
	topoinfo_ptr->topo_array = xcalloc(topoinfo_ptr->record_count,
					   sizeof(topoinfo_bblock_t));

	for (i = 0; i < topoinfo_ptr->record_count; i++) {
		topoinfo_ptr->topo_array[i].block_index =
			block_record_table[i].block_index;
		topoinfo_ptr->topo_array[i].name =
			xstrdup(block_record_table[i].name);
		topoinfo_ptr->topo_array[i].nodes =
			xstrdup(block_record_table[i].nodes);
	}

	return SLURM_SUCCESS;
}

extern int topology_p_topology_pack(void *topoinfo_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	int i;
	topoinfo_block_t *topoinfo = topoinfo_ptr;

	pack32(topoinfo->record_count, buffer);
	for (i = 0; i < topoinfo->record_count; i++) {
		pack16(topoinfo->topo_array[i].block_index, buffer);
		packstr(topoinfo->topo_array[i].name, buffer);
		packstr(topoinfo->topo_array[i].nodes, buffer);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topology_print(void *topoinfo_ptr, char *nodes_list,
				     char **out)
{
	int i, match, match_cnt = 0;;
	topoinfo_block_t *topoinfo = topoinfo_ptr;

	*out = NULL;

	if ((nodes_list == NULL) || (nodes_list[0] == '\0')) {
		if (topoinfo->record_count == 0) {
			error("No topology information available");
			return SLURM_SUCCESS;
		}

		for (i = 0; i < topoinfo->record_count; i++)
			_print_topo_record(&topoinfo->topo_array[i], out);

		return SLURM_SUCCESS;
	}

	/* Search for matching block name */
	for (i = 0; i < topoinfo->record_count; i++) {
		if (xstrcmp(topoinfo->topo_array[i].name, nodes_list))
			continue;
		_print_topo_record(&topoinfo->topo_array[i], out);
		return SLURM_SUCCESS;
	}

	/* Search for matching node name */
	for (i = 0; i < topoinfo->record_count; i++) {
		hostset_t *hs;

		if ((topoinfo->topo_array[i].nodes == NULL) ||
		    (topoinfo->topo_array[i].nodes[0] == '\0'))
			continue;
		hs = hostset_create(topoinfo->topo_array[i].nodes);
		if (hs == NULL)
			fatal("hostset_create: memory allocation failure");
		match = hostset_within(hs, nodes_list);
		hostset_destroy(hs);
		if (!match)
			continue;
		match_cnt++;
		_print_topo_record(&topoinfo->topo_array[i], out);
	}

	if (match_cnt == 0) {
		error("Topology information contains no block or "
		      "node named %s", nodes_list);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topology_unpack(void **topoinfo_pptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	int i = 0;
	topoinfo_block_t *topoinfo_ptr =
		xmalloc(sizeof(topoinfo_block_t));

	*topoinfo_pptr = topoinfo_ptr;
	safe_unpack32(&topoinfo_ptr->record_count, buffer);
	safe_xcalloc(topoinfo_ptr->topo_array, topoinfo_ptr->record_count,
		     sizeof(topoinfo_bblock_t));
	for (i = 0; i < topoinfo_ptr->record_count; i++) {
		safe_unpack16(&topoinfo_ptr->topo_array[i].block_index, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].name, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].nodes, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	topology_p_topology_free(topoinfo_ptr);
	*topoinfo_pptr = NULL;
	return SLURM_ERROR;
}
