/*****************************************************************************\
 *  openapi.c - OpenAPI plugin handler
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
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"
#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/operations.h"

#define OPENAPI_MAJOR_TYPE "openapi"

typedef struct {
	int (*init)(void);
	int (*fini)(void);
	int (*get_paths)(const openapi_path_binding_t **paths_ptr,
			 const openapi_resp_meta_t **meta_ptr);
} funcs_t;

/*
 * Must be synchronized with funcs_t above.
 */
static const char *syms[] = {
	"slurm_openapi_p_init",
	"slurm_openapi_p_fini",
	"slurm_openapi_p_get_paths",
};

typedef enum {
	OPENAPI_PATH_ENTRY_UNKNOWN = 0,
	OPENAPI_PATH_ENTRY_MATCH_STRING,
	OPENAPI_PATH_ENTRY_MATCH_PARAMETER,
	OPENAPI_PATH_ENTRY_MAX
} entry_type_t;

/*
 * This is a simplified entry since OAS allows combos of
 * parameters but we will only honor having a single parameter
 * as an dir entry for now
 */
typedef struct {
	char *entry;
	char *name;
	entry_type_t type;
	openapi_type_t parameter;
} entry_t;

typedef struct {
	const openapi_path_binding_method_t *bound;
	entry_t *entries;
	http_request_method_t method;
} entry_method_t;

#define MAGIC_PATH 0x0a0b09fd
typedef struct {
	int magic; /* MAGIC_PATH */
	char *path; /* path as string */
	const openapi_path_binding_t *bound;
	data_parser_t *parser;
	entry_method_t *methods;
	int tag;
} path_t;

typedef struct {
	const data_t *dpath;
	path_t *path;
	data_t *params;
	http_request_method_t method;
	entry_t *entry;
	int tag;
} match_path_from_data_t;

#define MAGIC_MERGE_PATH 0x22b2ae44
typedef struct {
	int magic; /* MAGIC_MERGE_PATH */
	data_t *paths;
	data_t *server_path;
} merge_path_t;

#define MAGIC_MERGE_ID_PATH 0x22b2aeae
typedef struct {
	int magic; /* MAGIC_MERGE_ID_PATH */
	data_t *server_path;
	char *operation;
	char *at;
	char *path;
	merge_path_t *merge_args;
} id_merge_path_t;

#define MAGIC_OAS 0x1218eeee
typedef struct {
	int magic; /* MAGIC_OAS */
	data_t *spec;
	data_t *tags;
	data_t *paths;
	data_t *components;
	data_t *components_schemas;
	data_t *security_schemas;
	data_t *info;
	data_t *contact;
	data_t *license;
	data_t *servers;
	data_t *security;
	/* tracked references per data_parser */
	void **references;
} openapi_spec_t;

static list_t *paths = NULL;
static int path_tag_counter = 0;
static plugins_t *plugins = NULL;
static data_parser_t **parsers = NULL; /* symlink to parser array */

static const struct {
	char *openapi_version;
	struct {
		char *title;
		char *desc;
		char *tos;
		struct {
			char *name;
			char *url;
			char *email;
		} contact;
		struct {
			char *name;
			char *url;
		} license;
	} info;
	struct {
		char *name;
		char *desc;
	} tags[3];
	struct {
		char *url;
	} servers[1];
	/* security is too complex for here */
	struct {
		struct security_scheme_s {
			char *key;
			char *type;
			char *desc;
			char *name;
			char *in;
			char *scheme;
			char *bearer_format;
		} security_schemes[3];
	} components;
} openapi_spec = {
	.openapi_version = "3.0.3",
	.info = {
		.title = "Slurm REST API",
		.desc = "API to access and control Slurm",
		.tos = "https://github.com/SchedMD/slurm/blob/master/DISCLAIMER",
		.contact = {
			.name = "SchedMD LLC",
			.url = "https://www.schedmd.com/",
			.email = "sales@schedmd.com",
		},
		.license = {
			.name = "Apache 2.0",
			.url = "https://www.apache.org/licenses/LICENSE-2.0.html",
		},
	},
	.tags = {
		{
			.name = "slurm",
			.desc = "methods that query slurmctld",
		},
		{
			.name = "slurmdb",
			.desc = "methods that query slurmdbd",
		},
		{
			.name = "openapi",
			.desc = "methods that query for generated OpenAPI specifications",
		},
	},
	.servers = {
		{
			.url = "/",
		},
	},
	.components = {
		.security_schemes = {
			{
#define SEC_SCHEME_USER_INDEX 0
				.key = "user",
				.type = "apiKey",
				.desc = "User name",
				.name = "X-SLURM-USER-NAME",
				.in = "header",
			},
			{
#define SEC_SCHEME_TOKEN_INDEX 1
				.key = "token",
				.type = "apiKey",
				.desc = "User access token",
				.name = "X-SLURM-USER-TOKEN",
				.in = "header",
			},
			{
#define SEC_SCHEME_BEARER_INDEX 2
				.key = "bearerAuth",
				.type = "http",
				.desc = "Bearer Authentication",
				.scheme = "bearer",
				.bearer_format = "JWT",
			},
		},
	},
};

static const openapi_path_binding_method_t openapi_methods[] = {
	{
		.method = HTTP_REQUEST_GET,
		.tags = (const char*[]) { "openapi", NULL },
		.summary = "Retrieve OpenAPI Specification",
		.response = {
			.type = DATA_PARSER_OPENAPI_SHARES_RESP,
			.description = "OpenAPI Specification",
		},
		.parameters = DATA_PARSER_SHARES_REQ_MSG,
	},
	{0}
};

static int _op_handler_openapi(openapi_ctxt_t *ctxt);

#define OP_FLAGS (OP_BIND_HIDDEN_OAS | OP_BIND_NO_SLURMDBD)

/*
 * Paths to generate OpenAPI specification
 */
static const openapi_path_binding_t openapi_paths[] = {
	{
		.path = "/openapi.json",
		.callback = _op_handler_openapi,
		.methods = openapi_methods,
		.flags = OP_FLAGS,
	},
	{
		.path = "/openapi.yaml",
		.callback = _op_handler_openapi,
		.methods = openapi_methods,
		.flags = OP_FLAGS,
	},
	{
		.path = "/openapi",
		.callback = _op_handler_openapi,
		.methods = openapi_methods,
		.flags = OP_FLAGS,
	},
	{
		.path = "/openapi/v3",
		.callback = _op_handler_openapi,
		.methods = openapi_methods,
		.flags = OP_FLAGS,
	},
	{0}
};

static const http_status_code_t *response_status_codes = NULL;
/*
 * Default to "default" and 200 as openapi generator breaks with only "default"
 * response code.
 */
static const http_status_code_t default_response_status_codes[] = {
	HTTP_STATUS_CODE_SUCCESS_OK,
	HTTP_STATUS_CODE_DEFAULT,
	HTTP_STATUS_NONE
};

static char *_entry_to_string(entry_t *entry);

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

static int _resolve_parser_index(data_parser_t *parser)
{
	for (int i = 0; parsers[i]; i++)
		if (parsers[i] == parser)
			return i;

	fatal_abort("Unable to find parser. This should never happen!");
}

static void _free_entry_list(entry_t *entry, int tag,
			     entry_method_t *method)
{
	entry_t *itr = entry;

	if (!entry)
		return;

	while (itr->type) {
		debug5("%s: remove path tag:%d method:%s entry:%s name:%s",
		       __func__, tag,
		       (method ? get_http_method_string(method->method) :
				       "N/A"),
		       itr->entry, itr->name);
		xfree(itr->entry);
		xfree(itr->name);
		itr++;
	}

	xfree(entry);
}

static void _list_delete_path_t(void *x)
{
	entry_method_t *em;

	if (!x)
		return;

	path_t *path = x;
	xassert(path->magic == MAGIC_PATH);
	xassert(path->tag != -1);
	em = path->methods;

	while (em->entries) {
		debug5("%s: remove path tag:%d method:%s", __func__, path->tag,
		       get_http_method_string(em->method));

		_free_entry_list(em->entries, path->tag, em);
		em->entries = NULL;
		em++;
	}

	xfree(path->methods);
	xfree(path->path);
	path->magic = ~MAGIC_PATH;
	xfree(path);
}

static entry_t *_parse_openapi_path(const char *str_path, int *count_ptr)
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
	if (count_ptr)
		*count_ptr = count;
	return entries;

fail:
	_free_entry_list(entries, -1, NULL);
	xfree(buffer);
	if (count_ptr)
		*count_ptr = -1;
	return NULL;
}

static int _print_path_tag_methods(void *x, void *arg)
{
	path_t *path = (path_t *) x;
	int *tag = (int *) arg;

	xassert(path->magic == MAGIC_PATH);

	if (path->tag != *tag)
		return 0;

	if (!path->methods->entries)
		debug4("%s: no methods found in path tag %d",
		       __func__, path->tag);

	for (entry_method_t *em = path->methods; em->entries; em++) {
		char *path_str = _entry_to_string(em->entries);

		debug4("%s: path tag %d entry: %s %s",
		       __func__, path->tag, get_http_method_string(em->method),
		       path_str);

		xfree(path_str);
	}

	/*
	 * We found the (unique) tag, so return -1 to exit early. The item's
	 * index returned by list_for_each_ro() will be negative.
	 */
	return -1;
}

extern void print_path_tag_methods(int tag)
{
	if (get_log_level() < LOG_LEVEL_DEBUG4)
		return;

	if (list_for_each_ro(paths, _print_path_tag_methods, &tag) >= 0)
		error("%s: Tag %d not found in paths", __func__, tag);
}

static void _clone_entries(entry_t **dst_ptr, entry_t *src, int count)
{
	entry_t *dst = xcalloc((count + 1), sizeof(*dst));

	xassert(!*dst_ptr);
	xassert(count > 0);

	*dst_ptr = dst;

	for (; src->type; src++, dst++) {
		dst->entry = xstrdup(src->entry);
		dst->name = xstrdup(src->name);
		dst->type = src->type;
		dst->parameter = src->parameter;
	}
}

static void _check_openapi_path_binding(const openapi_path_binding_t *op_path)
{
#ifndef NDEBUG
	xassert(op_path->path);
	xassert(op_path->callback);
	xassert((op_path->flags == OP_BIND_NONE) ||
		((op_path->flags > OP_BIND_NONE) &&
		 (op_path->flags < OP_BIND_INVALID_MAX)));

	for (int i = 0;; i++) {
		const openapi_path_binding_method_t *method =
			&op_path->methods[i];

		if (method->method == HTTP_REQUEST_INVALID)
			break;

		xassert(method->summary && method->summary[0]);
		xassert(method->response.description &&
			method->response.description[0]);
		xassert(method->method > HTTP_REQUEST_INVALID);
		xassert(method->method < HTTP_REQUEST_MAX);
		xassert(method->tags && method->tags[0]);
		xassert(method->response.type > DATA_PARSER_TYPE_INVALID);
		xassert(method->response.type < DATA_PARSER_TYPE_MAX);
		xassert(method->parameters >= DATA_PARSER_TYPE_INVALID);
		xassert(method->parameters < DATA_PARSER_TYPE_MAX);
		xassert(method->query >= DATA_PARSER_TYPE_INVALID);
		xassert(method->query < DATA_PARSER_TYPE_MAX);
		xassert(method->body.type >= DATA_PARSER_TYPE_INVALID);
		xassert(method->body.type < DATA_PARSER_TYPE_MAX);
	}
#endif /* !NDEBUG */
}

static bool _data_parser_supports_type(data_parser_t *parser,
				       data_parser_type_t type)
{
	openapi_type_t oapi_type;

	oapi_type = data_parser_g_resolve_openapi_type(parser, type, NULL);
	xassert(oapi_type >= OPENAPI_TYPE_INVALID);
	xassert(oapi_type < OPENAPI_TYPE_MAX);

	return (oapi_type != OPENAPI_TYPE_INVALID);
}

static bool _data_parser_supports_method(data_parser_t *parser,
					 const openapi_path_binding_method_t *m)
{
	/* check that parser supports each possible type if set */

	if ((m->response.type != DATA_PARSER_TYPE_INVALID) &&
	    !_data_parser_supports_type(parser, m->response.type))
		return false;

	if ((m->parameters != DATA_PARSER_TYPE_INVALID) &&
	    !_data_parser_supports_type(parser, m->parameters))
		return false;

	if ((m->query != DATA_PARSER_TYPE_INVALID) &&
	    !_data_parser_supports_type(parser, m->query))
		return false;

	if ((m->body.type != DATA_PARSER_TYPE_INVALID) &&
	    !_data_parser_supports_type(parser, m->body.type))
		return false;

	return true;
}

extern int register_path_binding(const char *in_path,
				 const openapi_path_binding_t *op_path,
				 const openapi_resp_meta_t *meta,
				 data_parser_t *parser, int *tag_ptr)
{
	entry_t *entries = NULL;
	int tag = -1, methods_count = 0, entries_count = 0;
	path_t *p = NULL;
	const char *path = (in_path ? in_path : op_path->path);

	debug4("%s: attempting to bind %s with %s",
	       __func__, (parser ? data_parser_get_plugin_version(parser) :
			  "data_parser/none"), path);

	xassert(!!in_path == !!(op_path->flags & OP_BIND_DATA_PARSER));
	_check_openapi_path_binding(op_path);

	if (!(entries = _parse_openapi_path(path, &entries_count)))
		fatal("%s: parse_openapi_path(%s) failed", __func__, path);

	for (int i = 0; op_path->methods[i].method != HTTP_REQUEST_INVALID;
	     i++) {
		if (!_data_parser_supports_method(parser, &op_path->methods[i]))
			continue;

		methods_count++;
	}

	if (!methods_count) {
		debug5("%s: skip binding %s with %s",
		       __func__, path, data_parser_get_plugin(parser));
		_free_entry_list(entries, -1, NULL);
		return ESLURM_NOT_SUPPORTED;
	}

	tag = path_tag_counter++;

	p = xmalloc(sizeof(*p));
	p->magic = MAGIC_PATH;
	p->methods = xcalloc((methods_count + 1), sizeof(*p->methods));
	p->tag = tag;
	p->bound = op_path;
	p->parser = parser;
	p->path = xstrdup(path);

	for (int i = 0, mi = 0;; i++) {
		const openapi_path_binding_method_t *m = &op_path->methods[i];
		entry_method_t *t = &p->methods[mi];
		entry_t *e;

		if (m->method == HTTP_REQUEST_INVALID)
			break;

		/*
		 * Skip method if data_parser does not support any of the in/out
		 * types which would just cause slurmrestd to abort() later.
		 */
		if (!_data_parser_supports_method(parser, m)) {
			debug5("%s: skip binding \"%s %s\" with %s",
			       __func__, get_http_method_string(m->method),
			       path, data_parser_get_plugin(parser));
			continue;
		}

		t->method = m->method;
		t->bound = m;

		if (i != 0) {
			_clone_entries(&t->entries, entries, entries_count);
			e = t->entries;
		} else {
			p->methods[0].entries = e = entries;
		}

		for (; e->type; e++) {
			if (e->type == OPENAPI_PATH_ENTRY_MATCH_PARAMETER)
				e->parameter =
					data_parser_g_resolve_openapi_type(
						parser, m->parameters, e->name);

			debug5("%s: add binded path %s entry: method=%s tag=%d entry=%s name=%s parameter=%s entry_type=%s",
			       __func__, path,
			       get_http_method_string(m->method), tag, e->entry,
			       e->name, openapi_type_to_string(e->parameter),
			       _get_entry_type_string(e->type));
		}

		/* only move to next method if populated */
		mi++;
	}

	list_append(paths, p);
	*tag_ptr = tag;
	return SLURM_SUCCESS;
}

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
		      __func__, openapi_type_to_string(entry->parameter));
		/* fall through */
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
		       openapi_type_to_string(entry->parameter),
		       str, data_get_type_string(data),
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
	return DATA_FOR_EACH_CONT;
}

static char *_entry_to_string(entry_t *entry)
{
	char *path = NULL;
	data_t *d = data_set_list(data_new());

	for (; entry->type; entry++) {
		switch (entry->type) {
		case OPENAPI_PATH_ENTRY_MATCH_STRING:
			data_set_string(data_list_append(d), entry->entry);
			break;
		case OPENAPI_PATH_ENTRY_MATCH_PARAMETER:
			data_set_string_fmt(data_list_append(d), "{%s}",
					    entry->name);
			break;
		case OPENAPI_PATH_ENTRY_UNKNOWN:
		case OPENAPI_PATH_ENTRY_MAX:
			fatal_abort("invalid entry type");
		}
	}

	serialize_g_data_to_string(&path, NULL, d, MIME_TYPE_JSON,
				   SER_FLAGS_COMPACT);

	FREE_NULL_DATA(d);
	return path;
}

static int _match_path_from_data(void *x, void *key)
{
	char *dst_path = NULL, *src_path = NULL;
	match_path_from_data_t *args = key;
	path_t *path = x;
	entry_method_t *method;
	bool matched = false;

	xassert(path->magic == MAGIC_PATH);

	if (get_log_level() >= LOG_LEVEL_DEBUG5) {
		serialize_g_data_to_string(&dst_path, NULL, args->dpath,
					   MIME_TYPE_JSON, SER_FLAGS_COMPACT);
	}

	args->path = path;
	for (method = path->methods; method->entries; method++) {
		int entries = 0;

		if (get_log_level() >= LOG_LEVEL_DEBUG5) {
			xfree(src_path);
			src_path = _entry_to_string(method->entries);
		}

		if (args->method != method->method) {
			debug5("%s: method skip for %s(%d, %s != %s) to %s(0x%"PRIXPTR")",
			       __func__, src_path, args->path->tag,
			       get_http_method_string(args->method),
			       get_http_method_string(method->method),
			       dst_path, (uintptr_t) args->dpath);
			continue;
		}

		for (args->entry = method->entries; args->entry->type;
		     entries++, args->entry++)
			/* do nothing */;

		if (data_get_list_length(args->dpath) != entries) {
			debug5("%s: skip non-matching subdirectories: registered=%u requested=%zu ",
			       __func__, entries,
			       data_get_list_length(args->dpath));
			continue;
		}

		args->entry = method->entries;
		if (data_list_for_each_const(args->dpath, _match_path,
					     args) < 0) {
			debug5("%s: match failed %s",
			       __func__, args->entry->entry);
			continue;
		}

		/*
		 * The list is NULL terminated, so if entry->type is not NULL
		 * we didn't match the whole list, but we already don't have
		 * anything to compare in request.
		 */
		if (!args->entry->type) {
			args->tag = path->tag;
			matched = true;
			break;
		}
	}

	debug5("%s: match %s for %s(%d, %s) to %s(0x%"PRIXPTR")",
	       __func__,
	       matched ? "successful" : "failed",
	       src_path,
	       args->path->tag,
	       get_http_method_string(args->method),
	       dst_path,
	       (uintptr_t) args->dpath);

	xfree(src_path);
	xfree(dst_path);

	return matched;
}

extern int find_path_tag(const data_t *dpath, data_t *params,
			 http_request_method_t method)
{
	match_path_from_data_t args = {
		.params = params,
		.dpath = dpath,
		.method = method,
		.tag = -1,
	};

	xassert(data_get_type(params) == DATA_TYPE_DICT);

	(void) list_find_first(paths, _match_path_from_data, &args);

	return args.tag;
}

static int _bind_paths(const openapi_path_binding_t *paths,
		       const openapi_resp_meta_t *meta)
{
	int rc = SLURM_SUCCESS;

	for (int i = 0; paths[i].path; i++) {
		const openapi_path_binding_t *op_path = &paths[i];

		if ((rc = bind_operation_path(op_path, meta)))
			break;
	}

	return rc;
}

extern int init_openapi(const char *plugin_list, plugrack_foreach_t listf,
			data_parser_t **parsers_ptr,
			const http_status_code_t *resp_status_codes)
{
	int rc;

	if (paths)
		fatal("%s called twice", __func__);

	if (resp_status_codes)
		response_status_codes = resp_status_codes;
	else
		response_status_codes = default_response_status_codes;

	paths = list_create(_list_delete_path_t);

	/* must have JSON plugin to parse the openapi.json */
	if ((rc = serializer_g_init(MIME_TYPE_JSON_PLUGIN, NULL)))
		fatal("Plugin serializer/json failed to load: %s",
		      slurm_strerror(rc));

	if ((rc = _bind_paths(openapi_paths, NULL)))
		fatal("Unable to bind openapi specification paths: %s",
		      slurm_strerror(rc));

	rc = load_plugins(&plugins, OPENAPI_MAJOR_TYPE, plugin_list, listf,
			  syms, ARRAY_SIZE(syms));

	if (!xstrcasecmp("list", plugin_list))
		return SLURM_SUCCESS;

	if (rc)
		fatal("Loading OpenAPI plugins failed: %s", slurm_strerror(rc));

	if ((rc = load_plugins(&plugins, OPENAPI_MAJOR_TYPE, plugin_list, listf,
			       syms, ARRAY_SIZE(syms))))
		fatal("Loading OpenAPI plugins failed: %s", slurm_strerror(rc));

	if (!plugins->count)
		fatal("No OpenAPI plugins loaded.");

	parsers = parsers_ptr;

	for (size_t i = 0; i < plugins->count; i++) {
		const funcs_t *funcs = plugins->functions[i];
		const openapi_path_binding_t *paths;
		const openapi_resp_meta_t *meta;

		if ((rc = funcs->get_paths(&paths, &meta)))
			fatal("Failure loading plugin path bindings: %s",
			      slurm_strerror(rc));

		if ((rc = _bind_paths(paths, meta)))
			fatal("Unable to bind openapi specification paths: %s",
			      slurm_strerror(rc));
	}

	/* Call init() after all plugins are fully loaded */
	for (size_t i = 0; i < plugins->count; i++) {
		const funcs_t *funcs = plugins->functions[i];
		funcs->init();
	}

	return rc;
}

extern void destroy_openapi(void)
{
	if (!paths)
		return;

	for (size_t i = 0; i < plugins->count; i++) {
		const funcs_t *funcs = plugins->functions[i];
		funcs->fini();
	}

	FREE_NULL_PLUGINS(plugins);
	FREE_NULL_LIST(paths);
}

static data_for_each_cmd_t _merge_operationId_strings(data_t *data, void *arg)
{
	id_merge_path_t *args = arg;
	char *p;

	xassert(args->magic == MAGIC_MERGE_ID_PATH);
	xassert(args->merge_args->magic == MAGIC_MERGE_PATH);

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		return DATA_FOR_EACH_FAIL;

	p = xstrdup(data_get_string(data));

	/* sub out '.' for '_' to avoid breaking compilers */
	for (int s = strlen(p), i = 0; i < s; i++)
		if ((p[i] == '.') || (p[i] == '{') || (p[i] == '}'))
			p[i] = '_';

	xstrfmtcatat(args->operation, &args->at, "%s%s",
		     (args->operation ? "_" : ""), data_get_string(data));

	xfree(p);

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _foreach_strip_params(data_t *data, void *arg)
{
	const char *item = data_get_string(data);
	data_t **last_ptr = arg;
	int len = strlen(item);

	xassert(item);
	if (!item || (item[0] != '{')) {
		char *dst = xstrdup(item);
		char *last = dst;

		/* strip out '.' */
		for (int i = 0; i < len; i++) {
			if (item[i] == '.')
				continue;

			*last = item[i];
			last++;
		}

		*last = '\0';

		data_set_string_own(data, dst);

		*last_ptr = data;
		return DATA_FOR_EACH_CONT;
	}

	xassert(len > 2);
	xassert(item[len - 1] == '}');
	if (*last_ptr &&
	    !xstrncmp(data_get_string(*last_ptr), (item + 1), (len - 2))) {
		/*
		 * Last item is the same as the parameter name which means that
		 * the item is uncountable and we need to set last as single.
		 */
		data_set_string(data, data_get_string(*last_ptr));
		data_set_string(*last_ptr, "single");
		return DATA_FOR_EACH_CONT;
	}

	return DATA_FOR_EACH_DELETE;
}

/* Caller must xfree() returned string */
static char *_get_method_operationId(openapi_spec_t *spec, path_t *path,
				     const openapi_path_binding_method_t
					     *method)
{
	data_t *merge[10] = {0}, *dpath, *merged = NULL, *last = NULL;
	const char *method_str = get_http_method_string_lc(method->method);
	int i = 0;
	merge_path_t merge_args = {
		.magic = MAGIC_MERGE_PATH,
		.paths = spec->paths,
	};
	id_merge_path_t merge_id_args = {
		.magic = MAGIC_MERGE_ID_PATH,
		.merge_args = &merge_args,
	};

	dpath = parse_url_path(path->path, false, true);

	xassert((data_get_list_length(dpath) + 1) < (ARRAY_SIZE(merge) - 1));

	(void) data_list_for_each(dpath, _foreach_strip_params, &last);

	if (data_get_list_length(dpath) < 3) {
		/* unversioned paths */
		merge[i++] = data_set_string(data_new(), method_str);
	} else {
		merge[i++] = data_list_dequeue(dpath); /* slurm vs slurmdb */
		merge[i++] = data_list_dequeue(dpath); /* v0.0.XX */
		merge[i++] = data_set_string(data_new(), method_str);
	}

	while (data_get_list_length(dpath) && (i < (ARRAY_SIZE(merge) - 1)))
		merge[i++] = data_list_dequeue(dpath);

	merged = data_list_join((const data_t **) merge, true);
	if (data_list_for_each(merged, _merge_operationId_strings,
			       &merge_id_args) < 0)
		fatal_abort("_merge_operationId_strings() failed which should never happen");

	for (i = 0; i < ARRAY_SIZE(merge); i++)
		FREE_NULL_DATA(merge[i]);
	FREE_NULL_DATA(merged);
	FREE_NULL_DATA(dpath);

	debug5("%s: [%s %s] setting OperationId: %s",
	       __func__, method_str, path->path, merge_id_args.operation);

	return merge_id_args.operation;
}

static int _populate_method(path_t *path, openapi_spec_t *spec, data_t *dpath,
			    const openapi_path_binding_method_t *method)
{
	const char **mime_types = get_mime_type_array();
	void *refs = &spec->references[_resolve_parser_index(path->parser)];
	data_t *dmethod = data_set_dict(data_key_set(dpath,
		get_http_method_string_lc(method->method)));
	data_t *dtags = data_set_list(data_key_set(dmethod, "tags"));

	for (int i = 0; method->tags[i]; i++)
		data_set_string(data_list_append(dtags), method->tags[i]);

	if (method->summary)
		data_set_string(data_key_set(dmethod, "summary"),
				method->summary);
	if (method->description)
		data_set_string(data_key_set(dmethod, "description"),
				method->description);

	{
		char *opid = _get_method_operationId(spec, path, method);
		data_set_string_own(data_key_set(dmethod, "operationId"), opid);
	}

	if (method->parameters || method->query) {
		/*
		 * Use existing replacements
		 */
		data_t *dst = data_key_set(dmethod, "parameters");

		if (data_parser_g_populate_parameters(path->parser,
						      method->parameters,
						      method->query, refs, dst,
						      spec->components_schemas))
			fatal_abort("data_parser_g_populate_parameters() failed");
	}

	if (method->response.type) {
		data_t *dresp = data_set_dict(data_key_set(dmethod, "responses"));
		data_t *resp_code = data_set_dict(data_new());
		data_t *cnt = data_set_dict(data_key_set(resp_code, "content"));

		if (method->response.description)
			data_set_string(data_set_dict(data_key_set(resp_code,
				"description")), method->response.description);

		for (int i = 0; mime_types[i]; i++) {
			data_t *dtype, *dschema;

			/*
			 * Never return URL encoded mimetype as it is only for
			 * HTTP query
			 */
			if (!xstrcmp(mime_types[i], MIME_TYPE_URL_ENCODED))
				continue;

			dtype = data_set_dict(data_key_set(cnt, mime_types[i]));
			dschema = data_set_dict(data_key_set(dtype, "schema"));

			if (data_parser_g_populate_schema(path->parser,
				method->response.type, refs, dschema,
				spec->components_schemas))
				fatal_abort("data_parser_g_populate_schema() failed");
		}

		for (int i = 0; response_status_codes[i]; i++) {
			const http_status_code_t code =
				response_status_codes[i];
			char str[64];

			if (code == HTTP_STATUS_CODE_DEFAULT)
				snprintf(str, sizeof(str), "%s",
					 get_http_status_code_string(code));
			else
				snprintf(str, sizeof(str), "%u", code);

			data_copy(data_key_set(dresp, str), resp_code);
		}

		FREE_NULL_DATA(resp_code);
	}

	if (method->body.type) {
		data_t *dbody = data_set_dict(data_key_set(dmethod,
							   "requestBody"));
		data_t *cnt = data_set_dict(data_key_set(dbody, "content"));

		if (method->body.description)
			data_set_string(data_set_dict(data_key_set(dbody,
				"description")), method->body.description);

		for (int i = 0; mime_types[i]; i++) {
			data_t *dtype, *dschema;

			/*
			 * Never return URL encoded mimetype as it is only for
			 * HTTP query
			 */
			if (!xstrcmp(mime_types[i], MIME_TYPE_URL_ENCODED))
				continue;

			dtype = data_set_dict(data_key_set(cnt, mime_types[i]));
			dschema = data_set_dict(data_key_set(dtype, "schema"));

			if (data_parser_g_populate_schema(path->parser,
				method->body.type, refs, dschema,
				spec->components_schemas))
				fatal_abort("data_parser_g_populate_schema() failed");
		}

	}

	return SLURM_SUCCESS;
}

static int _foreach_add_path(void *x, void *arg)
{
	int rc = SLURM_SUCCESS;
	path_t *path = x;
	openapi_spec_t *spec = arg;
	const openapi_path_binding_t *bound = path->bound;
	data_t *dpath;

	xassert(spec->magic == MAGIC_OAS);
	xassert(path->magic == MAGIC_PATH);

	if (!bound)
		return SLURM_SUCCESS;

	if (bound->flags & OP_BIND_HIDDEN_OAS)
		return SLURM_SUCCESS;

	xassert(!data_key_get(spec->paths, path->path));
	dpath = data_set_dict(data_key_set(spec->paths, path->path));

	for (int i = 0; !rc && bound->methods[i].method; i++)
		rc = _populate_method(path, spec, dpath, &bound->methods[i]);

	return rc;
}

static int _foreach_count_path(void *x, void *arg)
{
	path_t *path = x;
	openapi_spec_t *spec = arg;
	const openapi_path_binding_t *bound = path->bound;
	void *refs;

	xassert(spec->magic == MAGIC_OAS);
	xassert(path->magic == MAGIC_PATH);

	if (!bound)
		return SLURM_SUCCESS;

	refs = &spec->references[_resolve_parser_index(path->parser)];

	for (int i = 0; bound->methods[i].method; i++) {
		const openapi_path_binding_method_t *method =
			&bound->methods[i];
		const char **mime_types = get_mime_type_array();

		if (method->parameters &&
		    data_parser_g_increment_reference(path->parser,
						      method->parameters, refs))
			fatal_abort("data_parser_g_increment_reference() failed");

		if (method->query &&
		    data_parser_g_increment_reference(path->parser,
						      method->query, refs))
			fatal_abort("data_parser_g_increment_reference() failed");

		if (method->body.type) {
			/*
			 * Need to add 1 reference per mime type that will get
			 * dumped
			 */
			for (int i = 0; mime_types[i]; i++)
				if (data_parser_g_increment_reference(
					path->parser, method->body.type, refs))
					fatal_abort("data_parser_g_increment_reference() failed");
		}

		if (method->response.type) {
			/*
			 * Need to add 1 reference per mime type that will get
			 * dumped
			 */
			for (int i = 0; mime_types[i]; i++)
				if (data_parser_g_increment_reference(
					path->parser, method->response.type,
					refs))
					fatal_abort("data_parser_g_increment_reference() failed");
		}
	}

	return SLURM_SUCCESS;
}

extern int generate_spec(data_t *dst)
{
	openapi_spec_t spec = {
		.magic = MAGIC_OAS,
		.spec = dst,
	};
	data_t *security1, *security2, *security3;
	char *version_at = NULL;
	char *version = xstrdup_printf("Slurm-%s", SLURM_VERSION_STRING);
	int parsers_count;

	/* count the parsers present to allocate refs counts */
	for (parsers_count = 0; parsers[parsers_count]; parsers_count++);
	spec.references = xcalloc(parsers_count, sizeof(*spec.references));

	data_set_dict(spec.spec);
	spec.tags = data_set_list(data_key_set(spec.spec, "tags"));
	spec.paths = data_set_dict(data_key_set(spec.spec, "paths"));
	spec.components = data_set_dict(data_key_set(spec.spec, "components"));
	spec.components_schemas = data_set_dict(data_key_set(spec.components,
							     "schemas"));
	spec.security_schemas = data_set_dict(data_key_set(spec.components,
							   "securitySchemes"));
	spec.info = data_set_dict(data_key_set(spec.spec, "info"));
	spec.contact = data_set_dict(data_key_set(spec.info, "contact"));
	spec.license = data_set_dict(data_key_set(spec.info, "license"));
	spec.servers = data_set_list(data_key_set(spec.spec, "servers"));
	spec.security = data_set_list(data_key_set(spec.spec, "security"));
	security1 = data_set_dict(data_list_append(spec.security));
	security2 = data_set_dict(data_list_append(spec.security));
	security3 = data_set_dict(data_list_append(spec.security));

	data_set_string(data_key_set(spec.spec, "openapi"),
			openapi_spec.openapi_version);
	data_set_string(data_key_set(spec.info, "title"), openapi_spec.info.title);
	data_set_string(data_key_set(spec.info, "description"),
			openapi_spec.info.desc);
	data_set_string(data_key_set(spec.info, "termsOfService"),
			openapi_spec.info.tos);

	/* Populate OAS version */
	for (int i = 0; i < plugins->count; i++)
		xstrfmtcatat(version, &version_at, "&%s", plugins->types[i]);
	data_set_string_own(data_key_set(spec.info, "version"), version);

	data_set_string(data_key_set(spec.contact, "name"),
			openapi_spec.info.contact.name);
	data_set_string(data_key_set(spec.contact, "url"),
			openapi_spec.info.contact.url);
	data_set_string(data_key_set(spec.contact, "email"),
			openapi_spec.info.contact.email);
	data_set_string(data_key_set(spec.license, "name"),
			openapi_spec.info.license.name);
	data_set_string(data_key_set(spec.license, "url"),
			openapi_spec.info.license.url);

	for (int i = 0; i < ARRAY_SIZE(openapi_spec.tags); i++) {
		data_t *tag = data_set_dict(data_list_append(spec.tags));
		data_set_string(data_key_set(tag, "name"),
				openapi_spec.tags[i].name);
		data_set_string(data_key_set(tag, "description"),
				openapi_spec.tags[i].desc);
	}
	for (int i = 0; i < ARRAY_SIZE(openapi_spec.servers); i++) {
		data_t *server = data_set_dict(data_list_append(spec.servers));
		data_set_string(data_key_set(server, "url"),
				openapi_spec.servers[i].url);
	}

	/* Add default of no auth required */
	data_set_dict(data_list_append(spec.security));
	/* Add user and token auth */
	data_set_list(data_key_set(
		security1,
		openapi_spec.components.security_schemes[SEC_SCHEME_USER_INDEX]
			.key));
	data_set_list(data_key_set(
		security1,
		openapi_spec.components.security_schemes[SEC_SCHEME_TOKEN_INDEX]
			.key));
	/* Add only token auth */
	data_set_list(data_key_set(
		security2,
		openapi_spec.components.security_schemes[SEC_SCHEME_TOKEN_INDEX]
			.key));
	/* Add only bearer */
	data_set_list(data_key_set(
		security3, openapi_spec.components
				   .security_schemes[SEC_SCHEME_BEARER_INDEX]
				   .key));

	for (int i = 0;
	     i < ARRAY_SIZE(openapi_spec.components.security_schemes); i++) {
		const struct security_scheme_s *s =
			&openapi_spec.components.security_schemes[i];
		data_t *schema =
			data_set_dict(data_key_set(spec.security_schemas,
						   s->key));
		data_set_string(data_key_set(schema, "type"), s->type);
		data_set_string(data_key_set(schema, "description"), s->desc);

		if (s->name)
			data_set_string(data_key_set(schema, "name"), s->name);
		if (s->in)
			data_set_string(data_key_set(schema, "in"), s->in);
		if (s->scheme)
			data_set_string(data_key_set(schema, "scheme"),
					s->scheme);
		if (s->bearer_format)
			data_set_string(data_key_set(schema, "bearerFormat"),
					s->bearer_format);
	}

	(void) list_for_each(paths, _foreach_count_path, &spec);

	/* Add generated paths */
	(void) list_for_each(paths, _foreach_add_path, &spec);

	for (int i = 0; parsers[i]; i++)
		if (spec.references[i])
			data_parser_g_release_references(parsers[i],
							 &spec.references[i]);
	xfree(spec.references);

	/*
	 * We currently fatal instead of returning failure since openapi are
	 * compile time static and we should not be failing to serve the specs
	 * out
	 */
	return SLURM_SUCCESS;
}

static int _op_handler_openapi(openapi_ctxt_t *ctxt)
{
	return generate_spec(ctxt->resp);
}

static bool _on_error(void *arg, data_parser_type_t type, int error_code,
		      const char *source, const char *why, ...)
{
	va_list ap;
	char *str;
	openapi_ctxt_t *ctxt = arg;

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	openapi_resp_error(ctxt, error_code, source, "%s", str);

	xfree(str);

	return false;
}

static void _on_warn(void *arg, data_parser_type_t type, const char *source,
		     const char *why, ...)
{
	va_list ap;
	char *str;
	openapi_ctxt_t *ctxt = arg;

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	openapi_resp_warn(ctxt, source, "%s", str);

	xfree(str);
}

extern int openapi_resp_error(openapi_ctxt_t *ctxt, int error_code,
			      const char *source, const char *why, ...)
{
	openapi_resp_error_t *e;

	xassert(ctxt->errors);

	if (!ctxt->errors)
		return error_code;

	e = xmalloc(sizeof(*e));

	if (why) {
		va_list ap;
		char *str;

		va_start(ap, why);
		str = vxstrfmt(why, ap);
		va_end(ap);

		error("%s: [%s] parser=%s rc[%d]=%s -> %s",
		      (source ? source : __func__), ctxt->id,
		      data_parser_get_plugin(ctxt->parser), error_code,
		      slurm_strerror(error_code), str);

		e->description = str;
	}

	if (error_code) {
		e->num = error_code;

		if (!ctxt->rc)
			ctxt->rc = error_code;
	}

	if (source)
		e->source = xstrdup(source);

	list_append(ctxt->errors, e);

	return error_code;
}

extern void openapi_resp_warn(openapi_ctxt_t *ctxt, const char *source,
			      const char *why, ...)
{
	openapi_resp_warning_t *w;

	xassert(ctxt->warnings);

	if (!ctxt->warnings)
		return;

	w = xmalloc(sizeof(*w));

	if (why) {
		va_list ap;
		char *str;

		va_start(ap, why);
		str = vxstrfmt(why, ap);
		va_end(ap);

		debug("%s: [%s] parser=%s WARNING: %s",
		      (source ? source : __func__), ctxt->id,
		      data_parser_get_plugin(ctxt->parser), str);

		w->description = str;
	}

	if (source)
		w->source = xstrdup(source);

	list_append(ctxt->warnings, w);
}

static void _populate_openapi_results(openapi_ctxt_t *ctxt,
				      openapi_resp_meta_t *query_meta)
{
	data_t *errors, *warnings, *meta;
	int rc;

	/* need to populate meta, errors and warnings */
	errors = data_key_set(ctxt->resp,
		XSTRINGIFY(OPENAPI_RESP_STRUCT_ERRORS_FIELD_NAME));
	warnings = data_key_set(ctxt->resp,
		XSTRINGIFY(OPENAPI_RESP_STRUCT_WARNINGS_FIELD_NAME));
	meta = data_key_set(ctxt->resp,
		XSTRINGIFY(OPENAPI_RESP_STRUCT_META_FIELD_NAME));

	if (data_get_type(meta) == DATA_TYPE_NULL)
		DATA_DUMP(ctxt->parser, OPENAPI_META_PTR, query_meta, meta);

	if (data_get_type(errors) == DATA_TYPE_LIST) {
		if (!data_get_list_length(errors))
			data_set_null(errors);
		else
			xassert(list_is_empty(ctxt->errors));
	}

	if ((data_get_type(errors) == DATA_TYPE_NULL) &&
	    ((rc = DATA_DUMP(ctxt->parser, OPENAPI_ERRORS, ctxt->errors,
			     errors)))) {
		/* data_parser doesn't support OPENAPI_ERRORS parser */
		data_t *e =
			data_set_dict(data_list_append(data_set_list(errors)));
		data_set_string(data_key_set(e, "description"),
				"Requested data_parser plugin does not support OpenAPI plugin");
		data_set_int(data_key_set(e, "error_number"),
			     ESLURM_NOT_SUPPORTED);
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(ESLURM_NOT_SUPPORTED));
	}

	if (data_get_type(warnings) == DATA_TYPE_LIST) {
		if (!data_get_list_length(warnings))
			data_set_null(warnings);
		else
			xassert(list_is_empty(ctxt->warnings));
	}

	if (data_get_type(warnings) == DATA_TYPE_NULL)
		DATA_DUMP(ctxt->parser, OPENAPI_WARNINGS, ctxt->warnings,
			  warnings);
}

extern int wrap_openapi_ctxt_callback(const char *context_id,
				      http_request_method_t method,
				      data_t *parameters, data_t *query,
				      int tag, data_t *resp, void *auth,
				      data_parser_t *parser,
				      const openapi_path_binding_t *op_path,
				      const openapi_resp_meta_t *plugin_meta)
{
	int rc = SLURM_SUCCESS;
	openapi_ctxt_t ctxt = {
		.id = context_id,
		.method = method,
		.parameters = parameters,
		.query = query,
		.resp = resp,
		.tag = tag,
	};
	openapi_resp_meta_t query_meta = {{0}};
	openapi_ctxt_handler_t callback = op_path->callback;

	if (plugin_meta)
		query_meta = *plugin_meta;

	query_meta.plugin.data_parser = (char *) data_parser_get_plugin(parser);
	query_meta.plugin.accounting_storage =
		(char *) slurm_conf.accounting_storage_type;
	query_meta.client.source = (char *) context_id;
	query_meta.slurm.cluster = slurm_conf.cluster_name;

	ctxt.parent_path = data_set_list(data_new());
	ctxt.errors = list_create(free_openapi_resp_error);
	ctxt.warnings = list_create(free_openapi_resp_warning);
	ctxt.parser = data_parser_g_new(_on_error, _on_error, _on_error, &ctxt,
					_on_warn, _on_warn, _on_warn, &ctxt,
					data_parser_get_plugin(parser), NULL,
					true);

	debug("%s: [%s] %s using %s",
	      __func__, context_id, get_http_method_string(method),
	      data_parser_get_plugin(ctxt.parser));

	if (op_path->flags & OP_BIND_NO_SLURMDBD) {
		; /* Do not attempt to open a connection to slurmdbd */
	} else if (slurm_conf.accounting_storage_type &&
		   !(ctxt.db_conn = openapi_get_db_conn(auth))) {
		char *auth_error_msg = "";
		if (errno == SLURM_PROTOCOL_AUTHENTICATION_ERROR)
			auth_error_msg = ", authentication error";
		if (op_path->flags & OP_BIND_REQUIRE_SLURMDBD)
			openapi_resp_error(&ctxt, (rc = ESLURM_DB_CONNECTION),
					   XSTRINGIFY(openapi_get_db_conn),
					   "Failed to open slurmdbd connection%s",
					   auth_error_msg);
		else
			openapi_resp_warn(&ctxt,
					  XSTRINGIFY(openapi_get_db_conn),
					  "Failed to open connection to slurmdbd%s. Response fields may not be fully populated or empty.",
					  auth_error_msg);
	} else {
		rc = data_parser_g_assign(ctxt.parser,
					  DATA_PARSER_ATTR_DBCONN_PTR,
					  ctxt.db_conn);
	}

	if (!rc)
		rc = callback(&ctxt);

	if (data_get_type(ctxt.resp) == DATA_TYPE_NULL)
		data_set_dict(ctxt.resp);

	if (op_path->flags & OP_BIND_OPENAPI_RESP_FMT)
		_populate_openapi_results(&ctxt, &query_meta);

	if (!rc)
		rc = ctxt.rc;

	FREE_NULL_LIST(ctxt.errors);
	FREE_NULL_LIST(ctxt.warnings);
	FREE_NULL_DATA_PARSER(ctxt.parser);
	FREE_NULL_DATA(ctxt.parent_path);

	return rc;
}

extern bool is_spec_generation_only(bool set)
{
	static bool is_spec_only = false;

	if (set)
		is_spec_only = true;

	return is_spec_only;
}
