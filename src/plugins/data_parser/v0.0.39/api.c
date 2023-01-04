/*****************************************************************************\
 *  api.c - Slurm data parsing handlers
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "parsers.h"
#include "parsing.h"

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
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for Slurm node selection) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load select plugins if the plugin_type string has a
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Slurm Data Parser " XSTRINGIFY(DATA_VERSION);
const char plugin_type[] = "data_parser/" XSTRINGIFY(DATA_VERSION);
const uint32_t plugin_id = PLUGIN_ID;
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

extern int data_parser_p_dump(args_t *args, data_parser_type_t type, void *src,
			      ssize_t src_bytes, data_t *dst)
{
	const parser_t *const parser = find_parser_by_type(type);

	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(args->magic == MAGIC_ARGS);
	xassert(src);
	xassert(src_bytes > 0);
	xassert(dst && (data_get_type(dst) == DATA_TYPE_NULL));

	if (!parser)
		fatal("%s: invalid data parser type:0x%x", __func__, type);

	return dump(src, src_bytes, parser, dst, args);
}

extern int data_parser_p_parse(args_t *args, data_parser_type_t type, void *dst,
			       ssize_t dst_bytes, data_t *src,
			       data_t *parent_path)
{
	const parser_t *const parser = find_parser_by_type(type);

	xassert(type > DATA_PARSER_TYPE_INVALID);
	xassert(type < DATA_PARSER_TYPE_MAX);
	xassert(args->magic == MAGIC_ARGS);
	xassert(dst);
	xassert(src && (data_get_type(src) != DATA_TYPE_NONE));
	xassert(dst_bytes > 0);

	if (!parser)
		fatal("%s: invalid data parser type:0x%x", __func__, type);

	return parse(dst, dst_bytes, parser, src, args, parent_path);
}

extern args_t *data_parser_p_new(data_parser_on_error_t on_parse_error,
				 data_parser_on_error_t on_dump_error,
				 data_parser_on_error_t on_query_error,
				 void *error_arg,
				 data_parser_on_warn_t on_parse_warn,
				 data_parser_on_warn_t on_dump_warn,
				 data_parser_on_warn_t on_query_warn,
				 void *warn_arg)
{
	args_t *args = xmalloc(sizeof(*args));
	args->magic = MAGIC_ARGS;
	args->on_parse_error = on_parse_error;
	args->on_dump_error = on_dump_error;
	args->on_query_error = on_query_error;
	args->error_arg = error_arg;
	args->on_parse_warn = on_parse_warn;
	args->on_dump_warn = on_dump_warn;
	args->on_query_warn = on_query_warn;
	args->warn_arg = warn_arg;

	log_flag(DATA, "init parser 0x%" PRIxPTR, (uintptr_t) args);

	parsers_init();

	return args;
}

extern void data_parser_p_free(args_t *args)
{
	xassert(args->magic == MAGIC_ARGS);
	args->magic = ~MAGIC_ARGS;

	log_flag(DATA, "BEGIN: cleanup of parser 0x%" PRIxPTR,
		 (uintptr_t) args);

	FREE_NULL_LIST(args->tres_list);
	FREE_NULL_LIST(args->qos_list);
	FREE_NULL_LIST(args->assoc_list);
	if (args->close_db_conn)
		slurmdb_connection_close(&args->db_conn);

	log_flag(DATA, "END: cleanup of parser 0x%" PRIxPTR, (uintptr_t) args);

	xfree(args);
}

extern int data_parser_p_assign(args_t *args, data_parser_attr_type_t type,
				void *obj)
{
	xassert(args->magic == MAGIC_ARGS);

	switch (type) {
	case DATA_PARSER_ATTR_TRES_LIST:
		xassert(!args->tres_list || (args->tres_list == obj) || !obj);
		FREE_NULL_LIST(args->tres_list);
		args->tres_list = obj;

		log_flag(DATA, "assigned TRES list 0x%"PRIxPTR" to parser 0x%"PRIxPTR,
			 (uintptr_t) obj, (uintptr_t) args);
		return SLURM_SUCCESS;
	case DATA_PARSER_ATTR_DBCONN_PTR:
		xassert(!args->db_conn || (args->db_conn == obj));
		args->db_conn = obj;
		args->close_db_conn = false;

		log_flag(DATA, "assigned db_conn 0x%"PRIxPTR" to parser 0x%"PRIxPTR,
			 (uintptr_t) obj, (uintptr_t) args);
		return SLURM_SUCCESS;
	case DATA_PARSER_ATTR_QOS_LIST:
		xassert(!args->qos_list || (args->qos_list == obj) || !obj);
		FREE_NULL_LIST(args->qos_list);
		args->qos_list = obj;

		log_flag(DATA, "assigned QOS List at 0x%" PRIxPTR" to parser 0x%"PRIxPTR,
			 (uintptr_t) obj, (uintptr_t) args);
		return SLURM_SUCCESS;
	default :
		return EINVAL;
	}
}
