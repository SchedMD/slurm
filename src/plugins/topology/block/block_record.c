/*****************************************************************************\
 *  block_record.C - Determine order of nodes for job using block algo.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC
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

#include "block_record.h"

#include "src/common/xstring.h"

typedef struct slurm_conf_block {
	char *block_name; /* name of this block */
	char *nodes; /* names of nodes directly connect to this block */
} slurm_conf_block_t;

bitstr_t *blocks_nodes_bitmap = NULL;	/* nodes on any bblock */
block_record_t *block_record_table = NULL;
uint16_t bblock_node_cnt = 0;
bitstr_t *block_levels = NULL;
int block_record_cnt = 0;

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

static int _parse_block(void **dest, slurm_parser_enum_t type,
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

extern void block_record_table_destroy(void)
{
	if (!block_record_table)
		return;

	for (int i = 0; i < block_record_cnt; i++) {
		xfree(block_record_table[i].name);
		xfree(block_record_table[i].nodes);
		FREE_NULL_BITMAP(block_record_table[i].node_bitmap);
	}
	xfree(block_record_table);
	block_record_cnt = 0;
}

extern void block_record_validate(void)
{
	slurm_conf_block_t *ptr, **ptr_array;
	int i, j;
	block_record_t *block_ptr, *prior_ptr;
	hostlist_t *invalid_hl = NULL;
	char *buf;

	block_record_table_destroy();

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
