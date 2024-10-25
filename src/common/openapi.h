/*****************************************************************************\
 *  openapi.h - OpenAPI definitions
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

#ifndef SLURM_OPENAPI_H
#define SLURM_OPENAPI_H

#include "slurm/slurmdb.h"
#include "src/common/data.h"
#include "src/common/macros.h"

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
extern openapi_type_t openapi_type_format_to_type(openapi_type_format_t format);

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
 * Path to where all schemas are held in openapi.json
 */
#define OPENAPI_SCHEMAS_PATH \
	OPENAPI_PATH_SEP"components"OPENAPI_PATH_SEP"schemas"OPENAPI_PATH_SEP

/*
 * Path to where all URL paths are held in openapi.json.
 */
#define OPENAPI_PATHS_PATH OPENAPI_PATH_SEP"paths"

/*
 * Common parameter name for substitution of data_parser plugin in #/paths/
 */
#define OPENAPI_DATA_PARSER_PARAM "{data_parser}"

/*
 * Field name of parameters in a given path
 */
#define OPENAPI_PATH_PARAMS_FIELD "parameters"

/*
 * OpenAPI reference tag
 */
#define OPENAPI_REF_TAG "$ref"

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

typedef struct {
	struct {
		char *type;
		char *name;
		char *data_parser;
		char *accounting_storage;
	} plugin;
	char **command; /* only array ptr is xfree()d - not the strings */
	struct {
		char *source;
		uid_t uid;
		gid_t gid;
	} client;
	struct {
		struct {
			char *major;
			char *micro;
			char *minor;
		} version;
		char *release;
		char *cluster;
	} slurm;
} openapi_resp_meta_t;

extern void free_openapi_resp_meta(void *obj);

typedef struct {
	char *description;
	int num;
	char *source;
} openapi_resp_error_t;

extern void free_openapi_resp_error(void *obj);

typedef struct {
	char *description;
	char *source;
} openapi_resp_warning_t;

extern void free_openapi_resp_warning(void *obj);

/* Macros to declare each of the common response fields */
#define OPENAPI_RESP_STRUCT_META_FIELD openapi_resp_meta_t *meta
#define OPENAPI_RESP_STRUCT_META_FIELD_NAME meta
#define OPENAPI_RESP_STRUCT_ERRORS_FIELD list_t *errors
#define OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME errors
#define OPENAPI_RESP_STRUCT_WARNINGS_FIELD list_t *warnings
#define OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME warnings

/* macro to declare a single entry OpenAPI response struct */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	void *response;
} openapi_resp_single_t;

/* release meta, errors, warnings - not response or other fields */
#define FREE_OPENAPI_RESP_COMMON_CONTENTS(resp)			\
do {								\
	if (resp) {						\
		FREE_NULL_LIST(resp->warnings);			\
		FREE_NULL_LIST(resp->errors);			\
		free_openapi_resp_meta(resp->meta);		\
		resp->meta = NULL;				\
	}							\
} while(false)

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	list_t *clusters;
	list_t *instances;
	list_t *tres;
	list_t *accounts;
	list_t *users;
	list_t *qos;
	list_t *wckeys;
	list_t *associations;
} openapi_resp_slurmdbd_config_t;

typedef struct {
	slurm_selected_step_t *id;
} openapi_job_param_t;

typedef struct {
	char *name;
} openapi_user_param_t;

typedef struct {
	bool with_deleted;
	bool with_assocs;
	bool with_coords;
	bool with_wckeys;
} openapi_user_query_t;

typedef struct {
	char *wckey;
} openapi_wckey_param_t;

typedef struct {
	char *name;
} openapi_account_param_t;

typedef struct {
	bool with_assocs;
	bool with_coords;
	bool with_deleted;
} openapi_account_query_t;

typedef struct {
	char *name;
} openapi_cluster_param_t;

typedef struct {
	char *name;
} openapi_qos_param_t;

typedef struct {
	bool with_deleted;
} openapi_qos_query_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	job_array_resp_msg_t *results;
	char *job_id;
	char *step_id;
	char *job_submit_user_msg;
} openapi_job_post_response_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	submit_response_msg_t resp;
} openapi_job_submit_response_t;

typedef struct {
	char *script;
	job_desc_msg_t *job;
	list_t *jobs; /* list of job_desc_msg_t* */
} openapi_job_submit_request_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	uint32_t job_id;
	char *job_submit_user_msg;
} openapi_job_alloc_response_t;

typedef struct {
	job_desc_msg_t *job;
	list_t *hetjob; /* list of job_desc_msg_t* */
} openapi_job_alloc_request_t;

/* mirrors job_step_info_response_msg_t */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	job_step_info_response_msg_t *steps;
	time_t last_update;
} openapi_resp_job_step_info_msg_t;

/* mirrors job_info_msg_t */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	job_info_msg_t *jobs;
	time_t last_backfill;
	time_t last_update;
} openapi_resp_job_info_msg_t;

typedef struct {
	slurm_selected_step_t job_id;
} openapi_job_info_param_t;

typedef struct {
	uint16_t signal;
	uint16_t flags;
} openapi_job_info_delete_query_t;

typedef struct {
	time_t update_time;
	uint16_t show_flags;
} openapi_job_info_query_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	list_t *listjobs_list;
} openapi_resp_listjobs_info_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	list_t *listpids_list;
} openapi_resp_listpids_info_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	list_t *liststeps_list;
} openapi_resp_liststeps_info_t;

typedef struct {
	char *node_name;
} openapi_node_param_t;

typedef struct {
	time_t update_time;
	uint16_t show_flags;
} openapi_nodes_query_t;

typedef struct {
	char *partition_name;
} openapi_partition_param_t;

typedef struct {
	time_t update_time;
	uint16_t show_flags;
} openapi_partitions_query_t;

typedef struct {
	char *reservation_name;
} openapi_reservation_param_t;

typedef struct {
	time_t update_time;
} openapi_reservation_query_t;

/* mirrors node_info_msg_t */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	node_info_msg_t *nodes;
	time_t last_update;
} openapi_resp_node_info_msg_t;

/* mirrors partition_info_msg_t */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	partition_info_msg_t *partitions;
	time_t last_update;
} openapi_resp_partitions_info_msg_t;

/* mirrors reserve_info_msg_t */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	reserve_info_msg_t *reservations;
	time_t last_update;
} openapi_resp_reserve_info_msg_t;

/* mirrors license_info_msg_t */
typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	license_info_msg_t *licenses;
	time_t last_update;
} openapi_resp_license_info_msg_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	slurmdb_add_assoc_cond_t *add_assoc;
	slurmdb_account_rec_t *acct;
} openapi_resp_accounts_add_cond_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	slurmdb_add_assoc_cond_t *add_assoc;
	slurmdb_user_rec_t *user;
} openapi_resp_users_add_cond_t;

typedef struct {
	OPENAPI_RESP_STRUCT_META_FIELD;
	OPENAPI_RESP_STRUCT_ERRORS_FIELD;
	OPENAPI_RESP_STRUCT_WARNINGS_FIELD;
	time_t last_update;
	job_state_response_msg_t *jobs;
} openapi_resp_job_state_t;

typedef struct {
	list_t *job_id_list; /* list of slurm_selected_step_t* */
} openapi_job_state_query_t;

#endif /* SLURM_OPENAPI_H */
