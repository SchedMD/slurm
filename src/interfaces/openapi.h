/*****************************************************************************\
 *  openapi.h - OpenAPI plugin handler
 *****************************************************************************
 *  Copyright (C) 2019-2021 SchedMD LLC.
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

/*
 * Based on OpenAPI 3.0.2 spec (OAS):
 * 	https://github.com/OAI/OpenAPI-Specification/blob/master/versions/3.0.2.md
 */

#ifndef SLURM_OPENAPI_H
#define SLURM_OPENAPI_H

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/http.h"
#include "src/common/list.h"
#include "src/common/plugrack.h"

/*
 * Opaque type for tracking state
 */
struct openapi_s;
typedef struct openapi_s openapi_t;

/*
 * Callback from openapi caller.
 * we are not passing any http information to make this generic.
 * RET SLURM_SUCCESS or error to kill the connection
 */
typedef int (*openapi_handler_t)(
	const char *context_id, /* context id of client */
	http_request_method_t method, /* request method */
	data_t *parameters, /* openapi parameters */
	data_t *query, /* query sent by client */
	int tag, /* tag associated with path */
	data_t *resp, /* data to populate with response */
	void *auth /* authentication context */
);

typedef enum {
	OAS_FLAG_NONE = 0,
	OAS_FLAG_MANGLE_OPID = SLURM_BIT(0), /* mangle operationid */
	OAS_FLAG_MAX = SLURM_BIT(63) /* place holder */
} openapi_spec_flags_t;

/*
 * Register a given unique tag against a path.
 *
 * IN path - path to assign to given tag
 * RET -1 on error or >0 tag value for path.
 *
 * Can safely be called multiple times for same path.
 */
extern int register_path_tag(openapi_t *oas, const char *path);

/*
 * Unregister a given unique tag against a path.
 *
 * IN tag - path tag to remove
 */
extern void unregister_path_tag(openapi_t *oas, int tag);

/*
 * Find tag assigned to given path
 * IN path - split up path to match
 * IN/OUT params - on match, will populate any OAS parameters in path.
 * 	params must be DATA_TYPE_DICT.
 *
 * IN method - HTTP method to match
 * RET -1 if path tag was not found, or
 *     -2 if path tag was found, but method wasn't found within path tag, or
 *     the tag assigned to the given path.
 */
extern int find_path_tag(openapi_t *oas, const data_t *path, data_t *params,
			 http_request_method_t method);

/*
 * Print registered methods for the requested tag at log level DEBUG4.
 */
extern void print_path_tag_methods(openapi_t *oas, int tag);

/*
 * Init the OAS data structs.
 * IN/OUT oas - openapi state (must point to NULL)
 * IN plugins - comma delimited list of plugins or "list"
 * 	pass NULL to load all found or "" to load none of them
 * IN listf - function to call if plugins="list" (may be NULL)
 * RET SLURM_SUCCESS or error
 */
extern int init_openapi(openapi_t **oas, const char *plugins,
			plugrack_foreach_t listf);

/*
 * Free openapi
 */
extern void destroy_openapi(openapi_t *oas);


/*
 * Joins all of the loaded specs into a single spec
 */
extern int get_openapi_specification(openapi_t *oas, data_t *resp);

/*
 * Extracts the db_conn using given auth context
 * Note: This must be implemented in process calling openapi functions.
 */
extern void *openapi_get_db_conn(void *ctxt);

typedef enum {
	OPENAPI_TYPE_INVALID = 0,
	OPENAPI_TYPE_INTEGER,
	OPENAPI_TYPE_NUMBER,
	OPENAPI_TYPE_STRING,
	OPENAPI_TYPE_BOOL,
	OPENAPI_TYPE_OBJECT, /* map/dictionary */
	OPENAPI_TYPE_ARRAY, /* list */
	OPENAPI_TYPE_MAX /* place holder */
} openapi_type_t;

extern const char *openapi_type_to_string(openapi_type_t type);
extern openapi_type_t openapi_string_to_type(const char *str);

typedef enum {
	OPENAPI_FORMAT_INVALID = 0,
	OPENAPI_FORMAT_INT, /* unbounded integer */
	OPENAPI_FORMAT_INT32,
	OPENAPI_FORMAT_INT64,
	OPENAPI_FORMAT_NUMBER, /* unbounded floating point number */
	OPENAPI_FORMAT_FLOAT,
	OPENAPI_FORMAT_DOUBLE,
	OPENAPI_FORMAT_STRING,
	OPENAPI_FORMAT_PASSWORD,
	OPENAPI_FORMAT_BOOL,
	OPENAPI_FORMAT_OBJECT, /* map/dictionary */
	OPENAPI_FORMAT_ARRAY, /* list */
	OPENAPI_FORMAT_MAX /* place holder */
} openapi_type_format_t;

extern const char *openapi_type_format_to_format_string(
	openapi_type_format_t format);
extern const char *openapi_type_format_to_type_string(
	openapi_type_format_t format);
extern data_type_t openapi_type_format_to_data_type(
	openapi_type_format_t format);
extern openapi_type_format_t openapi_string_to_type_format(const char *str);
extern openapi_type_format_t openapi_data_type_to_type_format(data_type_t type);

/*
 * Separator used to split up a relative path.
 * OpenAPI specification 3.1.0 explicitly requires $ref paths must be compliant
 * with RFC3986 URIs. It is expected that inside of "$ref" path that the
 * relative path use "/" to delimit components and that the relative paths start
 * with "#".
 */
#define OPENAPI_PATH_SEP "/"
#define OPENAPI_PATH_REL "#"

/*
 * Generate formated path string from relative path
 * IN/OUT str_ptr - ptr to path string to set/replace
 * IN relative_path - data list with each component of relative path
 * RET ptr to path string (to allow jit generation for logging)
 */
extern char *openapi_fmt_rel_path_str(char **str_ptr, data_t *relative_path);

/*
 * Fork parent_path and append list index to last component
 * IN parent_path - data list with each each component of relative path
 * IN index - index of entry in list
 * RET new relative path (caller must release with FREE_NULL_DATA())
 */
extern data_t *openapi_fork_rel_path_list(data_t *relative_path, int index);

/*
 * Append split up sub_path to existing relative path list
 * IN/OUT relative_path - data list with each component of relative path
 * IN sub_path - additional sub path components to append.
 * 	May start with #/ or have the components delimited by /
 * RET SLURM_SUCCESS or error
 */
extern int openapi_append_rel_path(data_t *relative_path, const char *sub_path);

#endif /* SLURM_OPENAPI_H */
