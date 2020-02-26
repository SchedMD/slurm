/*****************************************************************************\
 *  openapi.c - Slurm REST API openapi handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#include "config.h"

#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/http.h"
#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/ref.h"
#include "src/slurmrestd/xjson.h"

decl_static_data(openapi_json);

#define MAGIC_PATH 0x1111beef

static pthread_rwlock_t paths_lock = PTHREAD_RWLOCK_INITIALIZER;
static List paths = NULL;
static int path_tag_counter = 0;
static const data_t *spec = NULL;

typedef enum {
	OPENAPI_TYPE_UNKNOWN = 0,
	OPENAPI_TYPE_INTEGER,
	OPENAPI_TYPE_NUMBER,
	OPENAPI_TYPE_STRING,
	OPENAPI_TYPE_BOOL,
	OPENAPI_TYPE_OBJECT,
	OPENAPI_TYPE_ARRAY,
	OPENAPI_TYPE_MAX
} parameter_type_t;

/*
 * Parse OAS type.
 * IN str string to parse
 * RET parameter_type_t  or OPENAPI_TYPE_UNKNOWN if unknown
 */
static parameter_type_t _get_parameter_type(const char *str)
{
	if (!str)
		return OPENAPI_TYPE_UNKNOWN;
	if (!xstrcasecmp(str, "integer"))
		return OPENAPI_TYPE_INTEGER;
	if (!xstrcasecmp(str, "number"))
		return OPENAPI_TYPE_NUMBER;
	if (!xstrcasecmp(str, "string"))
		return OPENAPI_TYPE_STRING;
	if (!xstrcasecmp(str, "boolean") || !xstrcasecmp(str, "bool"))
		return OPENAPI_TYPE_BOOL;
	if (!xstrcasecmp(str, "object"))
		return OPENAPI_TYPE_OBJECT;
	if (!xstrcasecmp(str, "array"))
		return OPENAPI_TYPE_ARRAY;

	return OPENAPI_TYPE_UNKNOWN;
}

static const char *_get_parameter_type_string(parameter_type_t type)
{
	switch (type) {
	case OPENAPI_TYPE_UNKNOWN:
		return "unknown";
	case OPENAPI_TYPE_INTEGER:
		return "integer";
	case OPENAPI_TYPE_NUMBER:
		return "number";
	case OPENAPI_TYPE_STRING:
		return "string";
	case OPENAPI_TYPE_BOOL:
		return "boolean";
	case OPENAPI_TYPE_OBJECT:
		return "object";
	case OPENAPI_TYPE_ARRAY:
		return "array";
	default:
		xassert(false);
		return "unknown";
	}
}

typedef enum {
	OPENAPI_PATH_ENTRY_UNKNOWN = 0,
	OPENAPI_PATH_ENTRY_MATCH_STRING,
	OPENAPI_PATH_ENTRY_MATCH_PARAMETER,
	OPENAPI_PATH_ENTRY_MAX
} entry_type_t;

static const char *_get_entry_type_string(entry_type_t type)
{
	switch (type) {
	case OPENAPI_PATH_ENTRY_MATCH_STRING:
		return "string";
	case OPENAPI_PATH_ENTRY_MATCH_PARAMETER:
		return "parameter";
	default:
		return "invalid";
	}
}

/*
 * This is a simplified entry since OAS allows combos of
 * parameters but we will only honor having a single parameter
 * as an dir entry for now
 */
typedef struct {
	char *entry;
	char *name;
	entry_type_t type;
	parameter_type_t parameter;
} entry_t;

typedef struct {
	entry_t *entries;
	http_request_method_t method;
} entry_method_t;

typedef struct {
	entry_method_t *methods;
	int tag;
} path_t;

static entry_t *_parse_openapi_path(const char *str_path)
{
	char *save_ptr = NULL;
	char *buffer = xstrdup(str_path);
	char *token = strtok_r(buffer, "/", &save_ptr);
	entry_t *entries = NULL;
	entry_t *entry = NULL;
	int count = 0;

	/* find max bound on number of entries */
	for (const char *i = str_path; *i; i++)
		if (*i == '/')
			count++;

	if (count > 1024)
		fatal_abort("%s: url %s is way too long", str_path, __func__);

	entry = entries = xcalloc((count + 1), sizeof(entry_t));

	while (token) {
		const size_t slen = strlen(token);

		/* ignore // entries */
		if (slen <= 0)
			goto again;

		entry->entry = xstrdup(token);

		if (!xstrcmp(token, ".") || !xstrcmp(token, "..")) {
			/*
			 * there should not be a .. or . in a path
			 * definition, it just doesn't make any sense
			 */
			error("%s: invalid %s at entry",
			      __func__, token);
			goto fail;
		} else if (slen > 3 && token[0] == '{' &&
			   token[slen - 1] == '}') {
			entry->type = OPENAPI_PATH_ENTRY_MATCH_PARAMETER;
			entry->name = xstrndup(token + 1, slen - 2);

			debug5("%s: parameter %s at entry %s",
			       __func__, entry->name, token);
		} else { /* not a variable */
			entry->type = OPENAPI_PATH_ENTRY_MATCH_STRING;
			entry->name = NULL;

			debug5("%s: string match entry %s",
			       __func__, token);
		}

		entry++;
		xassert(entry <= entries + count);
again:
		token = strtok_r(NULL, "/", &save_ptr);
	}

	/* last is always NULL */
	xassert(!entry->type);
	xfree(buffer);
	return entries;

fail:
	xfree(entries);
	xfree(buffer);
	return NULL;
}

typedef struct {
	const char *str_path;
	const data_t *found;
} match_path_string_t;

static data_for_each_cmd_t _match_path_string(const char *key,
					      const data_t *data,
					      void *arg)
{
	match_path_string_t *args = arg;

	if (!xstrcasecmp(args->str_path, key)) {
		args->found = data;
		return DATA_FOR_EACH_STOP;
	}

	return DATA_FOR_EACH_CONT;
}

static const data_t *_find_spec_path(const char *str_path)
{
	const data_t *path_list = data_resolve_dict_path(spec, "/paths");
	match_path_string_t args = { .str_path = str_path };

	if (!path_list)
		return NULL;

	if (data_get_type(path_list) != DATA_TYPE_DICT)
		return NULL;

	if (data_dict_for_each_const(path_list, _match_path_string, &args) < 0)
		return NULL;

	return args.found;
}

typedef struct {
	entry_method_t *method;
	entry_t *entries;
	path_t *path;
} populate_methods_t;


static data_for_each_cmd_t _populate_parameters(const data_t *data, void *arg)
{
	populate_methods_t *args = arg;
	entry_t *entry;
	const char *key = NULL;
	const data_t *dname = data_key_get_const(data, "name");

	if (!dname || !(key = data_get_string(dname)) || !key[0]) {
		/* parameter doesn't have a name! */
		return DATA_FOR_EACH_FAIL;
	}

	for (entry = args->entries; entry->type; entry++)
		if (entry->type == OPENAPI_PATH_ENTRY_MATCH_PARAMETER &&
		    !xstrcasecmp(entry->name, key)) {
			char *buffer = NULL;
			if (!data_retrieve_dict_path_string(data, "schema/type",
							    &buffer)) {
				entry->parameter = _get_parameter_type(buffer);
				if (entry->parameter == OPENAPI_TYPE_UNKNOWN)
					fatal("%s: invalid type for %s",
					      __func__, key);
			} else
				fatal("%s: missing schema type for %s",
				      __func__, key);
			xfree(buffer);
			return DATA_FOR_EACH_CONT;
		}

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _populate_methods(const char *key,
					     const data_t *data,
					     void *arg)
{
	populate_methods_t *args = arg;
	populate_methods_t nargs = *args;
	entry_method_t *method = args->method;
	const data_t *para;
	int count = 0;
	entry_t *entry;

	if (data_get_type(data) != DATA_TYPE_DICT)
		return DATA_FOR_EACH_FAIL;

	if ((method->method = get_http_method(key)) == HTTP_REQUEST_INVALID)
		return DATA_FOR_EACH_FAIL;

	for (entry = args->entries; entry->type; entry++)
		count++;

	xassert(!method->entries);
	method->entries = xcalloc((count + 1), sizeof(entry_t));
	/* count is already bounded */
	memcpy(method->entries, args->entries, (count * sizeof(entry_t)));

	/* clone over any strings */
	for (entry = method->entries; entry->type; entry++) {
		entry->entry = xstrdup(entry->entry);
		entry->name = xstrdup(entry->name);
	}
	/* point to new entries clone */
	nargs.entries = method->entries;

	para = data_key_get_const(data, "parameters");
	if (!para)
		return DATA_FOR_EACH_CONT;
	if (data_get_type(para) != DATA_TYPE_LIST)
		return DATA_FOR_EACH_FAIL;
	if (data_list_for_each_const(para, _populate_parameters, &nargs) < 0)
		return DATA_FOR_EACH_FAIL;

	/* increment to next method entry */
	args->method++;

	if (get_log_level() >= LOG_LEVEL_DEBUG5)
		for (entry = method->entries; entry->type; entry++) {
			debug5("%s: add method:%s for path tag:%d entry:%s name:%s parameter:%s entry_type:%s",
			       __func__, key, args->path->tag, entry->entry,
			       entry->name,
			       _get_parameter_type_string(entry->parameter),
			       _get_entry_type_string(entry->type));
		}

	return DATA_FOR_EACH_CONT;
}

extern int register_path_tag(const char *str_path)
{
	path_t *path = NULL;
	entry_t *entries = _parse_openapi_path(str_path);
	const data_t *spec_entry;
	populate_methods_t args = {0};

	if (!entries)
		return -1;

	spec_entry = _find_spec_path(str_path);
	if (!spec_entry)
		return -1;

	if (data_get_type(spec_entry) != DATA_TYPE_DICT)
		return -1;

	path = xmalloc(sizeof(*path));
	path->tag = path_tag_counter++;
	path->methods = xcalloc((data_get_dict_length(spec_entry) + 1),
				sizeof(*path->methods));

	args.method = path->methods;
	args.entries = entries;
	args.path = path;
	if (data_dict_for_each_const(spec_entry, _populate_methods, &args) < 0)
		fatal_abort("%s: failed", __func__);

	for (entry_t *entry = entries; entry->type; entry++) {
		xfree(entry->entry);
		xfree(entry->name);
	}
	xfree(entries);
	list_append(paths, path);

	return path->tag;
}

static int _rm_path_by_tag(void *x, void *tptr)
{
	path_t *path = (path_t *)x;
	const int tag = *(int*)tptr;

	if (path->tag != tag)
		return 0;

	debug5("%s: removing tag %d", __func__, path->tag);

	return 1;
}

extern void unregister_path_tag(int tag)
{
	slurm_rwlock_wrlock(&paths_lock);

	if (paths)
		list_delete_all(paths, _rm_path_by_tag, &tag);

	slurm_rwlock_unlock(&paths_lock);
}

typedef struct {
	bool matched;
	const data_t *dpath;
	path_t *path;
	data_t *params;
	http_request_method_t method;
	entry_t *entry;
} match_path_from_data_t;

/*
 * Check if the entry matches based on the OAS type
 * and if it does, then add that matched parameter
 */
static bool _match_param(const data_t *data, match_path_from_data_t *args)
{
	bool matched = false;
	entry_t *entry = args->entry;
	data_t *params = args->params;
	data_t *match = data_new();

	data_copy(match, data);

	switch (entry->parameter) {
	case OPENAPI_TYPE_NUMBER:
	{
		if (data_convert_type(match, DATA_TYPE_FLOAT) ==
		    DATA_TYPE_FLOAT) {
			data_set_float(data_key_set(params, entry->name),
				       data_get_float(match));
			matched = true;
		}
		break;
	}
	case OPENAPI_TYPE_INTEGER:
	{
		if (data_convert_type(match, DATA_TYPE_INT_64) ==
		    DATA_TYPE_INT_64) {
			data_set_int(data_key_set(params, entry->name),
				     data_get_int(match));
			matched = true;
		}
		break;
	}
	default: /* assume string */
		debug("%s: unknown parameter type %s",
		      __func__, _get_parameter_type_string(entry->parameter));
	case OPENAPI_TYPE_STRING:
	{
		if (data_convert_type(match, DATA_TYPE_STRING) ==
		    DATA_TYPE_STRING) {
			data_set_string(data_key_set(params, entry->name),
					data_get_string(match));
			matched = true;
		}
		break;
	}
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG5) {
		char *str = NULL;
		data_get_string_converted(data, &str);

		debug5("%s: parameter %s[%s]->%s[%s] result=%s",
		       __func__, entry->name,
		       _get_parameter_type_string(entry->parameter),
		       str, data_type_to_string(data_get_type(data)),
		       (matched ? "matched" : "failed"));

		xfree(str);
	}

	FREE_NULL_DATA(match);
	return matched;
}

static data_for_each_cmd_t _match_path(const data_t *data, void *y)
{
	match_path_from_data_t *args = y;
	entry_t *entry = args->entry;
	args->matched = false;

	if (!entry->type)
		return DATA_FOR_EACH_FAIL;

	if (entry->type == OPENAPI_PATH_ENTRY_MATCH_STRING) {
		bool match;

		if (data_get_type(data) != DATA_TYPE_STRING)
			return DATA_FOR_EACH_FAIL;

		match = !xstrcmp(data_get_string(data), entry->entry);

		debug5("%s: string attempt match %s to %s: %s",
		       __func__, entry->entry, data_get_string(data),
		       (match ? "SUCCESS" : "FAILURE"));

		if (!match)
			return DATA_FOR_EACH_FAIL;
	} else if (entry->type == OPENAPI_PATH_ENTRY_MATCH_PARAMETER) {
		if (!_match_param(data, args))
			return DATA_FOR_EACH_FAIL;
	} else
		fatal_abort("%s: unknown OAS path entry match type",
			    __func__);

	args->entry++;
	args->matched = true;
	return DATA_FOR_EACH_CONT;
}

static int _match_path_from_data(void *x, void *key)
{
	match_path_from_data_t *args = key;
	path_t *path = x;
	entry_method_t *method;

	args->path = path;
	for (method = path->methods; method->entries; method++) {
		args->entry = method->entries;
		data_list_for_each_const(args->dpath, _match_path, args);

		if (args->matched)
			break;
	}

	if (get_log_level() >= LOG_LEVEL_DEBUG5) {
		char *str_path = dump_json(args->dpath,
					   DUMP_JSON_FLAGS_COMPACT);
		if (args->matched)
			debug5("%s: match successful for tag %d to %s(0x%"PRIXPTR")",
			       __func__, args->path->tag, str_path,
			       (uintptr_t) args->dpath);
		else
			debug5("%s: match failed for tag %d to %s(0x%"PRIXPTR")",
			       __func__, args->path->tag, str_path,
			       (uintptr_t) args->dpath);
		xfree(str_path);
	}

	return (args->matched ? 1 : 0);
}

extern int find_path_tag(const data_t *dpath, data_t *params,
			 http_request_method_t method)
{
	path_t *path;
	match_path_from_data_t args = { .params = params, .dpath = dpath };
	int tag = -1;

	xassert(data_get_type(params) == DATA_TYPE_DICT);
	slurm_rwlock_rdlock(&paths_lock);

	path = list_find_first(paths, _match_path_from_data, &args);
	if (path)
		tag = path->tag;

	slurm_rwlock_unlock(&paths_lock);

	return tag;
}

extern const data_t *get_openapi_specification(void)
{
	return spec;
}

static void _list_delete_path_t(void *x)
{
	entry_method_t *method;

	if (!x)
		return;

	path_t *path = x;
	xassert(path->tag != -1);
	method = path->methods;

	while (method->method) {
		entry_t *entry = method->entries;

		debug5("%s: remove path tag:%d method:%s",
		       __func__, path->tag,
		       get_http_method_string(method->method));

		while (entry->type) {
			debug5("%s: remove path tag:%d method:%s entry:%s name:%s",
			       __func__, path->tag,
			       get_http_method_string(method->method),
			       entry->entry, entry->name);

			xfree(entry->entry);
			xfree(entry->name);
			entry++;
		}

		xfree(method->entries);
		method++;
	}

	xfree(path->methods);
	xfree(path);
}

extern int init_openapi(void)
{
	slurm_rwlock_wrlock(&paths_lock);

	if (spec)
		fatal_abort("%s called twice", __func__);

	paths = list_create(_list_delete_path_t);

	static_ref_json_to_data_t(spec, openapi_json);

	slurm_rwlock_unlock(&paths_lock);

	return SLURM_SUCCESS;
}

extern void destroy_openapi(void)
{
	slurm_rwlock_wrlock(&paths_lock);

	FREE_NULL_LIST(paths);

	/* const_cast spec since this is only place that frees it */
	data_t *dspec = (data_t *) spec;
	FREE_NULL_DATA(dspec);
	spec = NULL;

	slurm_rwlock_unlock(&paths_lock);
}
