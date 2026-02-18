/*****************************************************************************\
 *  sercli.h - serialize and deserialize to CLI
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

#ifndef _COMMON_SERCLI_H
#define _COMMON_SERCLI_H

#include "src/interfaces/data_parser.h"

/*
 * Generate meta instance for a CLI command
 */
extern openapi_resp_meta_t *data_parser_cli_meta(int argc, char **argv,
						 const char *mime_type);

#define DATA_PARSER_DUMP_CLI_CTXT_MAGIC 0x1BA211B3

typedef struct {
	int magic; /* DATA_PARSER_DUMP_CLI_CTXT_MAGIC */
	int rc;
	list_t *errors;
	list_t *warnings;
	const char *data_parser;
} data_parser_dump_cli_ctxt_t;

/*
 * Dump object of given type to STDOUT
 * This function is only intended for the simple dump of the data and then
 * exiting of the CLI command.
 * IN type - data parser type for *obj
 * IN obj_bytes - sizeof(*obj)
 * IN acct_db_conn - slurmdb connection or NULL
 * IN mime_type - dump object as given mime type
 * IN data_parser - data_parser parameters
 * IN meta - ptr to meta instance
 * RET SLURM_SUCCESS or error
 */
extern int data_parser_dump_cli_stdout(data_parser_type_t type, void *obj,
				       int obj_bytes, void *acct_db_conn,
				       const char *mime_type,
				       const char *data_parser,
				       data_parser_dump_cli_ctxt_t *ctxt,
				       openapi_resp_meta_t *meta);

/*
 * Dump object to stdout
 */
#define DATA_DUMP_CLI(type, src, argc, argv, db_conn, mime_type, \
		      data_parser_str, rc) \
	do { \
		data_parser_dump_cli_ctxt_t dump_ctxt = { \
			.magic = DATA_PARSER_DUMP_CLI_CTXT_MAGIC, \
			.data_parser = data_parser_str, \
		}; \
		__typeof__(src) *src_ptr = &src; \
		if (!src.OPENAPI_RESP_STRUCT_META_FIELD_NAME) \
			src.OPENAPI_RESP_STRUCT_META_FIELD_NAME = \
				data_parser_cli_meta(argc, argv, mime_type); \
		if (!src.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME) \
			src.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME = \
				dump_ctxt.errors = \
					list_create(free_openapi_resp_error); \
		else \
			dump_ctxt.errors = \
				src.OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME; \
		if (!src.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME) \
			src.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME = \
				dump_ctxt.warnings = list_create( \
					free_openapi_resp_warning); \
		else \
			dump_ctxt.warnings = \
				src.OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME; \
		rc = data_parser_dump_cli_stdout( \
			DATA_PARSER_##type, src_ptr, sizeof(*src_ptr), \
			db_conn, mime_type, data_parser_str, &dump_ctxt, \
			src.OPENAPI_RESP_STRUCT_META_FIELD_NAME); \
		FREE_OPENAPI_RESP_COMMON_CONTENTS(src_ptr); \
	} while (false)

/*
 * Dump object as single field to in common openapi response dictionary
 */
#define DATA_DUMP_CLI_SINGLE(type, src, argc, argv, db_conn, mime_type, \
			     data_parser, rc) \
	do { \
		openapi_resp_single_t openapi_resp = { \
			.response = src, \
		}; \
		DATA_DUMP_CLI(type, openapi_resp, argc, argv, db_conn, \
			      mime_type, data_parser, rc); \
	} while (false)

/* Dump a struct into a json or yaml string. All errors and warnings logged */
#define DATA_DUMP_TO_STR(type, src, ret_str, db_conn, mime_type, sflags, rc) \
	do { \
		data_parser_t *parser = NULL; \
		data_parser_dump_cli_ctxt_t ctxt = { 0 }; \
		data_t *dst = NULL; \
\
		ctxt.magic = DATA_PARSER_DUMP_CLI_CTXT_MAGIC; \
		ctxt.data_parser = SLURM_DATA_PARSER_VERSION; \
		ctxt.errors = list_create(free_openapi_resp_error); \
		ctxt.warnings = list_create(free_openapi_resp_warning); \
		parser = data_parser_cli_parser(ctxt.data_parser, &ctxt); \
		if (!parser) { \
			rc = ESLURM_DATA_INVALID_PARSER; \
			error("%s parsing of %s not supported by %s",          \
			      mime_type, XSTRINGIFY(DATA_PARSER_##type),       \
			      ctxt.data_parser); \
		} else { \
			if (db_conn) \
				data_parser_g_assign( \
					parser, DATA_PARSER_ATTR_DBCONN_PTR, \
					db_conn); \
			dst = data_new(); \
			DATA_DUMP(parser, type, src, dst); \
			list_for_each(ctxt.warnings, openapi_warn_log_foreach, \
				      NULL); \
			list_for_each(ctxt.errors, openapi_error_log_foreach, \
				      NULL); \
		} \
\
		if (data_get_type(dst) != DATA_TYPE_NULL) { \
			serializer_flags_t tmp_sflags = sflags; \
\
			if (data_parser_g_is_complex(parser)) \
				tmp_sflags |= SER_FLAGS_COMPLEX; \
			serialize_g_data_to_string(&ret_str, NULL, dst, \
						   mime_type, tmp_sflags); \
		} \
		FREE_NULL_DATA(dst); \
		FREE_NULL_LIST(ctxt.errors); \
		FREE_NULL_LIST(ctxt.warnings); \
		FREE_NULL_DATA_PARSER(parser); \
	} while (false)

/* Parse a json or yaml string into a struct. All errors and warnings logged */
#define DATA_PARSE_FROM_STR(type, str, str_len, dst, db_conn, mime_type, rc) \
	do { \
		data_t *src = NULL; \
		data_parser_dump_cli_ctxt_t ctxt = { 0 }; \
		data_t *parent_path = NULL; \
		data_parser_t *parser = NULL; \
\
		rc = serialize_g_string_to_data(&src, str, str_len, \
						mime_type); \
		if (rc) { \
			FREE_NULL_DATA(src); \
			break; \
		} \
\
		ctxt.magic = DATA_PARSER_DUMP_CLI_CTXT_MAGIC; \
		ctxt.data_parser = SLURM_DATA_PARSER_VERSION; \
		ctxt.errors = list_create(free_openapi_resp_error); \
		ctxt.warnings = list_create(free_openapi_resp_warning); \
		parser = data_parser_cli_parser(ctxt.data_parser, &ctxt); \
		if (!parser) { \
			rc = ESLURM_DATA_INVALID_PARSER; \
			error("%s parsing of %s not supported by %s",          \
			      mime_type, XSTRINGIFY(DATA_PARSER_##type),       \
			      ctxt.data_parser); \
		} else { \
			if (db_conn) \
				data_parser_g_assign( \
					parser, DATA_PARSER_ATTR_DBCONN_PTR, \
					db_conn); \
			parent_path = data_set_list(data_new()); \
			(void) data_convert_tree(src, DATA_TYPE_NONE); \
			rc = DATA_PARSE(parser, type, dst, src, parent_path); \
			list_for_each(ctxt.warnings, openapi_warn_log_foreach, \
				      NULL); \
			list_for_each(ctxt.errors, openapi_error_log_foreach, \
				      NULL); \
		} \
		FREE_NULL_DATA(src); \
		FREE_NULL_LIST(ctxt.errors); \
		FREE_NULL_LIST(ctxt.warnings); \
		FREE_NULL_DATA(parent_path); \
		FREE_NULL_DATA_PARSER(parser); \
	} while (false)

/*
 * Create data_parser instance for CLI
 * IN data_parser - data_parser parameters
 * RET parser ptr
 *	Must be freed by call to data_parser_g_free()
 */
extern data_parser_t *data_parser_cli_parser(const char *data_parser,
					     void *arg);

#endif
