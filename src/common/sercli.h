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

#include "src/common/openapi.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

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

/*
 * Dump given target struct src into string
 * NOTE: Use SERCLI_DUMP_STR() macro instead of calling directly!
 * NOTE: Use SERDES_DUMP() for fixed size buffers instead.
 * NOTE: Use SERDES_DUMP_BUF() for buffers instead.
 * NOTE: errors logged via openapi_error_log_foreach()
 * NOTE: warnings logged via openapi_warn_log_foreach()
 * IN type - data_parser type of obj
 * IN db_conn - database connection pointer
 * IN src - ptr to struct/scalar to dump to data_t
 *	This *must* be a pointer to the object and not just a value of the
 *	object.
 * IN src_bytes - size of object pointed to by src
 * OUT dst_ptr - pointer to string to populate (caller must xfree())
 * IN mime_type - mime type for dumping
 * IN flags - optional flags to specify to serilzier to change presentation of
 *	data
 * IN caller - __func__ from caller for logging
 * RET SLURM_SUCCESS or error
 */
extern int sercli_dump_str(data_parser_type_t type, void *db_conn, void *src,
			   ssize_t src_bytes, char **dst_ptr,
			   const char *mime_type,
			   const serializer_flags_t flags, const char *caller);

#define SERCLI_DUMP_STR(type, db_conn, src, dst, mime_type, flags) \
	sercli_dump_str(DATA_PARSER_##type, db_conn, &(src), sizeof(src), \
			&(dst), mime_type, flags, __func__)

/*
 * Parse given string into target struct dst
 * NOTE: Use SERCLI_PARSE_STR() macro instead of calling directly!
 * NOTE: errors logged via openapi_error_log_foreach()
 * NOTE: warnings logged via openapi_warn_log_foreach()
 * IN parser - return from data_parser_g_new()
 * IN db_conn - database connection pointer
 * IN type - expected data_parser type of obj
 * IN dst - ptr to struct/scalar to populate
 *	This *must* be a pointer to the object and not just a value of the
 *	object.
 * IN dst_bytes - size of object pointed to by dst
 * IN src - string to parse into obj.
 * IN src_bytes - number of bytes in string to parse.
 * IN mime_type - deserialize data using given mime_type
 * IN caller - __func__ from caller
 * RET SLURM_SUCCESS or error
 */
extern int sercli_parse_str(data_parser_type_t type, void *db_conn, void *dst,
			    ssize_t dst_bytes, const char *src,
			    const size_t src_bytes, const char *mime_type,
			    const char *caller);

#define SERCLI_PARSE_STR(type, db_conn, dst, src, src_bytes, mime_type) \
	sercli_parse_str(DATA_PARSER_##type, db_conn, &dst, sizeof(dst), src, \
			 src_bytes, mime_type, __func__)

/*
 * Create data_parser instance for CLI
 * IN data_parser - data_parser parameters
 * RET parser ptr
 *	Must be freed by call to data_parser_g_free()
 */
extern data_parser_t *data_parser_cli_parser(const char *data_parser,
					     void *arg);

#endif
