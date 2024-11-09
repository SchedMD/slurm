/*****************************************************************************\
 *  api.c - Slurm data parsing handlers
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "api.h"
#include "events.h"
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
	xassert(!src || (src_bytes > 0));
	xassert(dst && (data_get_type(dst) == DATA_TYPE_NULL));

	if (!parser) {
		char *path = NULL;
		on_warn(DUMPING, type, args, NULL, __func__,
			"%s does not support parser %u for dumping. Output may be incomplete.",
			plugin_type, type);
		xfree(path);
		return ESLURM_NOT_SUPPORTED;
	}

	return dump(src, src_bytes, NULL, parser, dst, args);
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

	if (!parser) {
		char *path = NULL;
		on_warn(PARSING, type, args,
			set_source_path(&path, args, parent_path), __func__,
			"%s does not support parser %u for parsing. Output may be incomplete.",
			plugin_type, type);
		xfree(path);
		return ESLURM_NOT_SUPPORTED;
	}

	return parse(dst, dst_bytes, parser, src, args, parent_path);
}

static void _parse_param(const char *param, args_t *args)
{
	const parser_t *const parser = find_parser_by_type(DATA_PARSER_FLAGS);

	for (int i = 0; i < parser->flag_bit_array_count; i++) {
		const flag_bit_t *bit = &parser->flag_bit_array[i];

		xassert(bit->magic == MAGIC_FLAG_BIT);

		if (bit->type != FLAG_BIT_TYPE_BIT)
			continue;

		if (xstrcasecmp(bit->name, param))
			continue;

		if (bit->value == FLAG_PREFER_REFS) {
			info("%s ignoring default flag %s",
			     plugin_type, bit->flag_name);
			return;
		}

		debug("%s activated flag %s", plugin_type, bit->flag_name);

		args->flags |= bit->value;
		return;
	}

	warning("%s ignoring unknown flag %s", plugin_type, param);
}

extern args_t *data_parser_p_new(data_parser_on_error_t on_parse_error,
				 data_parser_on_error_t on_dump_error,
				 data_parser_on_error_t on_query_error,
				 void *error_arg,
				 data_parser_on_warn_t on_parse_warn,
				 data_parser_on_warn_t on_dump_warn,
				 data_parser_on_warn_t on_query_warn,
				 void *warn_arg, const char *params)
{
	args_t *args;
	char *param, *last = NULL, *dup;

	args = xmalloc(sizeof(*args));
	args->magic = MAGIC_ARGS;
	args->on_parse_error = on_parse_error;
	args->on_dump_error = on_dump_error;
	args->on_query_error = on_query_error;
	args->error_arg = error_arg;
	args->on_parse_warn = on_parse_warn;
	args->on_dump_warn = on_dump_warn;
	args->on_query_warn = on_query_warn;
	args->warn_arg = warn_arg;
	args->flags = FLAG_NONE;

	log_flag(DATA, "init %s(0x%"PRIxPTR") with params=%s",
		 plugin_type, (uintptr_t) args, params);

	if ((dup = xstrdup(params))) {
		param = strtok_r(dup, SLURM_DATA_PARSER_PLUGIN_PARAMS_CHAR,
				 &last);
		while (param) {
			if (param[0])
				_parse_param(param, args);

			param = strtok_r(NULL,
					 SLURM_DATA_PARSER_PLUGIN_PARAMS_CHAR,
					 &last);
		}
		xfree(dup);
	}

	parsers_init();

	return args;
}

extern void data_parser_p_free(args_t *args)
{
	if (!args)
		return;

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

		if (args->tres_list != obj)
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

		if (args->qos_list != obj)
			FREE_NULL_LIST(args->qos_list);
		args->qos_list = obj;

		log_flag(DATA, "assigned QOS List at 0x%" PRIxPTR" to parser 0x%"PRIxPTR,
			 (uintptr_t) obj, (uintptr_t) args);
		return SLURM_SUCCESS;
	default:
		return EINVAL;
	}
}

extern openapi_type_t data_parser_p_resolve_openapi_type(
	args_t *args,
	data_parser_type_t type,
	const char *field)
{
	const parser_t *const parser = find_parser_by_type(type);

	xassert(args->magic == MAGIC_ARGS);

	if (!parser)
		return OPENAPI_TYPE_INVALID;

	if (parser->model == PARSER_MODEL_ALIAS)
		return openapi_type_format_to_type(unalias_parser(
			find_parser_by_type(parser->type))->obj_openapi);

	if (!field)
		return openapi_type_format_to_type(parser->obj_openapi);

	for (int i = 0; i < parser->field_count; i++) {
		if (!xstrcasecmp(parser->fields[i].field_name, field)) {
			const parser_t *p = unalias_parser(
				find_parser_by_type(parser->fields[i].type));

			return openapi_type_format_to_type(p->obj_openapi);
		}
	}

	return OPENAPI_TYPE_INVALID;
}

extern const char *data_parser_p_resolve_type_string(args_t *args,
						     data_parser_type_t type)
{
	const parser_t *parser = find_parser_by_type(type);

	xassert(args->magic == MAGIC_ARGS);

	if (!parser)
		return NULL;

	parser = unalias_parser(parser);

	return parser->type_string;
}
