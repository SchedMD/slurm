/*****************************************************************************\
 *  block_record.C - Determine order of nodes for job using block algo.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
#include <math.h>

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

	*dest = (void *)b;

	return 1;
}

/* Return count of block configuration entries read */
static int _read_topo_file(slurm_conf_block_t **ptr_array[], char *topo_conf,
			   block_context_t *ctx)
{
	static s_p_options_t block_options[] = {
		{"BlockName", S_P_ARRAY, _parse_block, _destroy_block},
		{"BlockSizes", S_P_STRING},
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

	FREE_NULL_BITMAP(ctx->block_levels);
	ctx->block_levels = bit_alloc(MAX_BLOCK_LEVELS);

	if (!s_p_get_string(&tmp_str, "BlockSizes", conf_hashtbl)) {
		bit_nset(ctx->block_levels, 0, 4);
	} else {
		char *save_ptr = NULL;
		char *str_bsize = strtok_r(tmp_str, ",", &save_ptr);
		int bsize = -1;
		while (str_bsize) {
			int block_level;
			double tmp;
			bsize = atoi(str_bsize);
			if (bsize <= 0)
				break;
			if (!ctx->bblock_node_cnt)
				ctx->bblock_node_cnt = bsize;
			if (bsize % ctx->bblock_node_cnt) {
				bsize = -1;
				break;
			}
			block_level = bsize / ctx->bblock_node_cnt;
			tmp = log2(block_level);
			if (tmp != floor(tmp)) {
				bsize = -1;
				break;
			}
			block_level = tmp;

			if (block_level > 15) {
				bsize = -1;
				break;
			}
			bit_set(ctx->block_levels, block_level);

			str_bsize = strtok_r(NULL, ",", &save_ptr);
		}
		if (bsize <= 0) {
			s_p_hashtbl_destroy(conf_hashtbl);
			fatal("Invalid BlockSizes value: %s", str_bsize);
		}
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

static void _log_blocks(block_context_t *ctx)
{
	int i;
	block_record_t *block_ptr;

	block_ptr = ctx->block_record_table;
	for (i = 0; i < ctx->block_count; i++, block_ptr++) {
		debug("Block name:%s nodes:%s",
		      block_ptr->name, block_ptr->nodes);
	}

	for (i = 0; i < ctx->ablock_count; i++, block_ptr++) {
		debug("Aggregated Block name:%s nodes:%s",
		      block_ptr->name, block_ptr->nodes);
	}
}

extern void block_record_table_destroy(block_context_t *ctx)
{
	if (!ctx->block_record_table)
		return;

	for (int i = 0; i < ctx->block_count + ctx->ablock_count; i++) {
		xfree(ctx->block_record_table[i].name);
		xfree(ctx->block_record_table[i].nodes);
		FREE_NULL_BITMAP(ctx->block_record_table[i].node_bitmap);
	}
	xfree(ctx->block_record_table);
	FREE_NULL_BITMAP(ctx->block_levels);
	ctx->block_count = 0;
	ctx->block_sizes_cnt = 0;
	ctx->ablock_count = 0;
}

static int _cmp_block_level(const void *x, const void *y)
{
	const block_record_t *b1 = x, *b2 = y;

	if (b1->level > b2->level)
		return 1;
	else if (b1->level < b2->level)
		return -1;
	return 0;
}

static int _list_to_bitmap(void *x, void *arg)
{
	int *size = x;
	bitstr_t *block_levels = arg;

	if (*size >= MAX_BLOCK_LEVELS)
		return 1;

	bit_set(block_levels, *size);

	return 0;
}

extern int block_record_validate(topology_ctx_t *tctx)
{
	slurm_conf_block_t *ptr, **ptr_array, **ptr_array_mem = NULL;
	int i, j;
	block_record_t *block_ptr, *prior_ptr;
	hostlist_t *invalid_hl = NULL;
	char *buf;
	int level = 0;
	int record_inx = 0;
	int *aggregated_inx;
	block_context_t *ctx = xmalloc(sizeof(*ctx));

	if (tctx->config) {
		topology_block_config_t *block_config = tctx->config;
		ctx->block_count = block_config->config_cnt;
		ptr_array_mem =
			xcalloc(ctx->block_count, sizeof(*ptr_array_mem));
		ptr_array = ptr_array_mem;
		for (int i = 0; i < ctx->block_count; i++)
			ptr_array[i] = &block_config->block_configs[i];

		ctx->block_levels = bit_alloc(MAX_BLOCK_LEVELS);

		if (!list_count(block_config->block_sizes)) {
			bit_nset(ctx->block_levels, 0, 4);
		} else {
			list_for_each(block_config->block_sizes,
				      _list_to_bitmap, ctx->block_levels);
			bit_set(ctx->block_levels, 0);
		}

	} else {
		ctx->block_count =
			_read_topo_file(&ptr_array, tctx->topo_conf, ctx);
	}

	if (ctx->block_count == 0) {
		error("No blocks configured");
		s_p_hashtbl_destroy(conf_hashtbl);
		xfree(ctx);
		xfree(ptr_array_mem);
		return SLURM_ERROR;
	}
	/*
	 *  Allocate more than enough space for all aggregated blocks
	 */
	ctx->block_record_table =
		xcalloc((2 * ctx->block_count + MAX_BLOCK_LEVELS),
			sizeof(block_record_t));
	block_ptr = ctx->block_record_table;
	for (i = 0; i < ctx->block_count; i++, block_ptr++) {
		ptr = ptr_array[i];
		block_ptr->name = xstrdup(ptr->block_name);
		/* See if block name has already been defined. */
		prior_ptr = ctx->block_record_table;
		for (j = 0; j < i; j++, prior_ptr++) {
			if (xstrcmp(block_ptr->name, prior_ptr->name) == 0) {
				fatal("Block (%s) has already been defined",
				      prior_ptr->name);
			}
		}

		block_ptr->block_index = i;

		if (ptr->nodes) {
			block_ptr->nodes = xstrdup(ptr->nodes);
			if (node_name2bitmap(ptr->nodes, true,
					     &block_ptr->node_bitmap,
					     &invalid_hl)) {
				fatal("Invalid node name (%s) in block config (%s)",
				      ptr->nodes, ptr->block_name);
			}
			if (ctx->blocks_nodes_bitmap) {
				bit_or(ctx->blocks_nodes_bitmap,
				       block_ptr->node_bitmap);
			} else {
				ctx->blocks_nodes_bitmap =
					bit_copy(block_ptr->node_bitmap);
			}
			if (ctx->bblock_node_cnt == 0) {
				ctx->bblock_node_cnt =
					bit_set_count(block_ptr->node_bitmap);
			}

		} else {
			block_ptr->node_bitmap = bit_alloc(node_record_count);
		}
	}
	if (!ctx->bblock_node_cnt)
		fatal("Blocks do not contain any nodes and the BlockSizes are not set");
	if (ctx->blocks_nodes_bitmap) {
		i = bit_clear_count(ctx->blocks_nodes_bitmap);
		if (i > 0) {
			char *tmp_nodes;
			bitstr_t *tmp_bitmap =
				bit_copy(ctx->blocks_nodes_bitmap);
			bit_not(tmp_bitmap);
			tmp_nodes = bitmap2node_name(tmp_bitmap);
			warning("Blocks lack access to %d nodes: %s",
				i, tmp_nodes);
			xfree(tmp_nodes);
			FREE_NULL_BITMAP(tmp_bitmap);
		}
	} else {
		ctx->blocks_nodes_bitmap = bit_alloc(node_record_count);
		warning("Blocks do not contain any nodes");
	}

	if (invalid_hl) {
		buf = hostlist_ranged_string_xmalloc(invalid_hl);
		warning("Invalid hostnames in block configuration: %s", buf);
		xfree(buf);
		hostlist_destroy(invalid_hl);
	}

	while ((level = bit_ffs_from_bit(ctx->block_levels, level)) >= 0) {
		if ((ctx->block_sizes[ctx->block_sizes_cnt++] = (1 << level)) >=
		    ctx->block_count)
			break;

		level++;
	}

	ctx->blocks_nodes_cnt = bit_set_count(ctx->blocks_nodes_bitmap);

	s_p_hashtbl_destroy(conf_hashtbl);

	aggregated_inx = xcalloc(ctx->block_sizes_cnt, sizeof(int));
	record_inx = ctx->block_count;

	for (i = 0; i < ctx->block_count; i++) {
		for (j = 1; j < ctx->block_sizes_cnt; j++) {
			if (!(i % ctx->block_sizes[j])) {
				int remaining_blocks = (ctx->block_count - i);

				if ((ctx->block_sizes[j] > remaining_blocks) &&
				    (ctx->block_sizes[j - 1] >=
				     remaining_blocks)) {
					aggregated_inx[j] = -1;
					continue;
				}
				ctx->block_record_table[record_inx]
					.block_index = record_inx;
				ctx->block_record_table[record_inx].name =
					xstrdup(ctx->block_record_table[i]
							.name);
				ctx->block_record_table[record_inx]
					.node_bitmap =
					bit_copy(ctx->block_record_table[i]
							 .node_bitmap);
				ctx->block_record_table[record_inx].level = j;
				aggregated_inx[j] = record_inx;
				record_inx++;
			} else {
				int tmp = aggregated_inx[j];
				if (tmp < 0)
					continue;
				xstrfmtcat(ctx->block_record_table[tmp].name,
					   ",%s",
					   ctx->block_record_table[i].name);
				bit_or(ctx->block_record_table[tmp].node_bitmap,
				       ctx->block_record_table[i].node_bitmap);
			}
		}
	}
	xfree(aggregated_inx);

	ctx->ablock_count = (record_inx - ctx->block_count);

	qsort(ctx->block_record_table + ctx->block_count, ctx->ablock_count,
	      sizeof(block_record_t), _cmp_block_level);

	for (i = ctx->block_count; i < record_inx; i++) {
		char *tmp_list = ctx->block_record_table[i].name;
		hostlist_t *hl = hostlist_create(tmp_list);

		if (hl == NULL)
			fatal("Invalid BlockName: %s", tmp_list);

		ctx->block_record_table[i].name =
			hostlist_ranged_string_xmalloc(hl);
		ctx->block_record_table[i].nodes =
			bitmap2node_name(ctx->block_record_table[i]
						 .node_bitmap);

		hostlist_destroy(hl);
		xfree(tmp_list);
	}

	_log_blocks(ctx);
	tctx->plugin_ctx = ctx;
	xfree(ptr_array_mem);
	return SLURM_SUCCESS;
}
