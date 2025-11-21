/*****************************************************************************\
 *  ring_record.C - Determine order of nodes for job using ring algo.
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

#include "ring_record.h"

#include "src/common/xstring.h"

static s_p_hashtbl_t *conf_hashtbl = NULL;

static void _destroy_ring(void *ptr)
{
	slurm_conf_ring_t *s = ptr;
	if (!s)
		return;

	xfree(s->nodes);
	xfree(s->ring_name);
	xfree(s);
}

static int _parse_ring(void **dest, slurm_parser_enum_t type, const char *key,
		       const char *value, const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl;
	slurm_conf_ring_t *b;
	static s_p_options_t _ring_options[] = { { "Nodes", S_P_STRING },
						 { NULL } };

	tbl = s_p_hashtbl_create(_ring_options);
	s_p_parse_line(tbl, *leftover, leftover);

	b = xmalloc(sizeof(slurm_conf_ring_t));
	b->ring_name = xstrdup(value);
	s_p_get_string(&b->nodes, "Nodes", tbl);
	s_p_hashtbl_destroy(tbl);

	*dest = (void *) b;

	return 1;
}

/* Return count of ring configuration entries read */
static int _read_topo_file(slurm_conf_ring_t **ptr_array[], char *topo_conf,
			   ring_context_t *ctx)
{
	static s_p_options_t ring_options[] = {
		{ "RingName", S_P_ARRAY, _parse_ring, _destroy_ring }, { NULL }
	};
	int count;
	slurm_conf_ring_t **ptr;

	xassert(topo_conf);
	debug("Reading the %s file", topo_conf);

	conf_hashtbl = s_p_hashtbl_create(ring_options);
	if (s_p_parse_file(conf_hashtbl, NULL, topo_conf, false, NULL) ==
	    SLURM_ERROR) {
		s_p_hashtbl_destroy(conf_hashtbl);
		fatal("something wrong with opening/reading %s: %m",
		      topo_conf);
	}

	if (s_p_get_array((void ***) &ptr, &count, "RingName", conf_hashtbl))
		*ptr_array = ptr;
	else {
		*ptr_array = NULL;
		count = 0;
	}
	return count;
}

static void _log_rings(ring_context_t *ctx)
{
	ring_record_t *ring_ptr;

	ring_ptr = ctx->rings;
	for (int i = 0; i < ctx->ring_count; i++, ring_ptr++) {
		debug("Ring name:%s nodes:%s",
		      ring_ptr->ring_name, ring_ptr->nodes);
		for (int j = 0; j < ring_ptr->ring_size; j++) {
			node_record_t *node_ptr =
				node_record_table_ptr[ring_ptr->nodes_map[j]];
			debug("\t %d -> %s", j, node_ptr->name);
		}
	}
}

extern void ring_record_table_destroy(ring_context_t *ctx)
{
	if (!ctx->rings)
		return;

	for (int i = 0; i < ctx->ring_count; i++) {
		xfree(ctx->rings[i].ring_name);
		xfree(ctx->rings[i].nodes);
		FREE_NULL_BITMAP(ctx->rings[i].nodes_bitmap);
	}
	xfree(ctx->rings);
	ctx->ring_count = 0;
}

extern int ring_record_validate(topology_ctx_t *tctx)
{
	slurm_conf_ring_t *ptr, **ptr_array, **ptr_array_mem = NULL;
	int i, j;
	ring_record_t *ring_ptr, *prior_ptr;
	hostlist_t *invalid_hl = NULL;
	char *buf;
	int no_access_cnt = 0;
	ring_context_t *ctx = xmalloc(sizeof(*ctx));

	if (tctx->config) {
		topology_ring_config_t *ring_config = tctx->config;
		ctx->ring_count = ring_config->config_cnt;
		ptr_array_mem =
			xcalloc(ctx->ring_count, sizeof(*ptr_array_mem));
		ptr_array = ptr_array_mem;
		for (int i = 0; i < ctx->ring_count; i++)
			ptr_array[i] = &ring_config->ring_configs[i];

	} else {
		ctx->ring_count =
			_read_topo_file(&ptr_array, tctx->topo_conf, ctx);
	}

	if (ctx->ring_count == 0) {
		s_p_hashtbl_destroy(conf_hashtbl);
		xfree(ptr_array_mem);
		xfree(ctx);
		fatal("No rings configured, failed to create context for topology plugin");
	}
	ctx->rings_nodes_bitmap = bit_alloc(node_record_count);
	/*
	 *  Allocate for all rings
	 */
	ctx->rings = xcalloc(ctx->ring_count, sizeof(*(ctx->rings)));

	ring_ptr = ctx->rings;
	for (i = 0; i < ctx->ring_count; i++, ring_ptr++) {
		ptr = ptr_array[i];

		if (!ptr->ring_name) {
			fatal("Can't create a ring without a name");
		}

		ring_ptr->ring_name = xstrdup(ptr->ring_name);
		/* See if ring name has already been defined. */
		prior_ptr = ctx->rings;
		for (j = 0; j < i; j++, prior_ptr++) {
			if (xstrcmp(ring_ptr->ring_name,
				    prior_ptr->ring_name) == 0) {
				fatal("Ring (%s) has already been defined",
				      prior_ptr->ring_name);
			}
		}

		ring_ptr->ring_index = i;
		ring_ptr->ring_size = 0;

		if (ptr->nodes) {
			char *node_name;
			hostlist_t *host_list_in;
			hostlist_t *host_list_out = hostlist_create(NULL);

			if (!(host_list_in = hostlist_create(ptr->nodes))) {
				/* likely a badly formatted hostlist */
				error("hostlist_create error on %s",
				      ring_ptr->ring_name);
				return EINVAL;
			}

			ring_ptr->nodes_bitmap = bit_alloc(node_record_count);
			while ((node_name = hostlist_shift(host_list_in))) {
				node_record_t *node_ptr =
					find_node_record(node_name);
				if (ring_ptr->ring_size >= MAX_RING_SIZE)
					fatal("Ring (%s) is bigger than %d",
					      prior_ptr->ring_name,
					      MAX_RING_SIZE);
				if (node_ptr) {
					ring_ptr->nodes_map
						[ring_ptr->ring_size++] =
						node_ptr->index;
					bit_set(ring_ptr->nodes_bitmap,
						node_ptr->index);
					bit_set(ctx->rings_nodes_bitmap,
						node_ptr->index);
					hostlist_push_host(host_list_out,
							   node_ptr->name);
				} else {
					if (!invalid_hl)
						invalid_hl =
							hostlist_create(NULL);
					hostlist_push_host(invalid_hl,
							   node_name);
				}
				free(node_name);
			}
			ring_ptr->nodes =
				hostlist_ranged_string_xmalloc(host_list_out);
			hostlist_destroy(host_list_in);
			hostlist_destroy(host_list_out);
		}
	}
	no_access_cnt = bit_clear_count(ctx->rings_nodes_bitmap);
	if (no_access_cnt > 0) {
		char *tmp_nodes;
		bitstr_t *tmp_bitmap = bit_copy(ctx->rings_nodes_bitmap);
		bit_not(tmp_bitmap);
		tmp_nodes = bitmap2node_name(tmp_bitmap);
		warning("Rings lack access to %d nodes: %s", no_access_cnt,
			tmp_nodes);
		xfree(tmp_nodes);
		FREE_NULL_BITMAP(tmp_bitmap);
	}

	if (invalid_hl) {
		buf = hostlist_ranged_string_xmalloc(invalid_hl);
		warning("Invalid hostnames in ring configuration: %s", buf);
		xfree(buf);
		hostlist_destroy(invalid_hl);
	}

	s_p_hashtbl_destroy(conf_hashtbl);

	_log_rings(ctx);
	tctx->plugin_ctx = ctx;
	xfree(ptr_array_mem);
	return SLURM_SUCCESS;
}

extern void ring_record_update_ring_config(topology_ctx_t *tctx, int idx)
{
	topology_ring_config_t *ring_config = tctx->config;
	ring_context_t *ctx = tctx->plugin_ctx;

	if (!tctx->config)
		return;

	xfree(ring_config->ring_configs[idx].nodes);
	ring_config->ring_configs[idx].nodes = xstrdup(ctx->rings[idx].nodes);
}
