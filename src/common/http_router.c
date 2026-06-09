/*****************************************************************************\
 *  http_router.c - Route HTTP requests
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

#include <stddef.h>

#include "slurm/slurm_errno.h"

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_router.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define PATH_SEG_BYTES 24
#define PATH_MAGIC 0xaaffb0af

typedef struct path_s path_t;

typedef struct path_s {
	int magic; /* PATH_MAGIC */
	/* RFC3986.3.3 Path segment */
	char segment[PATH_SEG_BYTES];
	/* True if segment is an OpenAPI parameter denoted by {Name} */
	bool is_template;
	/* Ptr to child segment in path */
	path_t *child;
	/* Ptr to next relative segment in path */
	path_t *next;
	/* Callback for path */
	http_router_on_request_event_t on_request;
	const char *on_request_func;
	/* Callback for cleanup */
	http_router_on_fini on_fini;
	const char *on_fini_func;
	/* arbitrary pointer to path callbacks */
	void *path_arg;
} path_t;

#define PATH_ADD_ARGS_MAGIC 0xb3ffb0a0

typedef struct {
	int magic; /* PATH_ADD_ARGS_MAGIC */
	path_t *path;
} path_add_args_t;

#define PATH_FIND_ARGS_MAGIC 0xd3ffb0ad

typedef struct {
	int magic; /* PATH_FIND_ARGS_MAGIC  */
	path_t *path;
} path_find_args_t;

static struct {
	http_router_on_request_event_t on_not_found;
	path_t methods[HTTP_REQUEST_INVALID_MAX];
} router = { 0 };

extern void http_router_init(http_router_on_request_event_t on_not_found)
{
	for (int i = 0; i < ARRAY_SIZE(router.methods); i++)
		router.methods[i].magic = PATH_MAGIC;

	xassert(!router.on_not_found);
	router.on_not_found = on_not_found;
	xassert(router.on_not_found);
}

static void _path_free(path_t **path_ptr)
{
	path_t *path = *path_ptr;
	*path_ptr = NULL;

	if (!path)
		return;

	xassert(path->magic == PATH_MAGIC);

	_path_free(&path->child);
	_path_free(&path->next);

	if (path->on_fini) {
		debug5("%s: Calling path cleanup via %s(%p)",
		       __func__, path->on_fini_func, path->path_arg);

		path->on_fini(path->on_request, path->path_arg);
	}

	path->magic = ~PATH_MAGIC;
	xfree(path);
}

extern void http_router_fini(void)
{
	xassert(router.on_not_found);

	if (!router.on_not_found)
		return;

	for (int i = 0; i < ARRAY_SIZE(router.methods); i++) {
		path_t *method = &router.methods[i];

		_path_free(&method->child);

		xassert(method->magic == PATH_MAGIC);
		xassert(!method->segment[0]);
		method->magic = ~PATH_MAGIC;
	}
}

static int _on_path_add_entry(const char *entry, bool template, void *arg)
{
	path_add_args_t *args = arg;
	path_t *parent = args->path;
	path_t *path = parent->child;
	path_t *lpath = NULL;

	xassert(args->magic == PATH_ADD_ARGS_MAGIC);
	xassert(parent->magic == PATH_MAGIC);
	xassert(entry[0]);
	/* First pointer is always the root segment */
	xassert(!path || (!path->segment[0] && !path->is_template));

	/* Search next segments for match */
	while (path) {
		xassert(path->magic == PATH_MAGIC);
		/* Assert that there will only 1 template per segment */
		xassert(!template ||
			(!path->is_template || !xstrcmp(entry, path->segment)));

		if (!(template && path->is_template) &&
		    xstrcmp(entry, path->segment)) {
			if (path->next) {
				lpath = path;
				path = path->next;
				continue;
			}

			break;
		}

		args->path = path;
		return SLURM_SUCCESS;
	}

	xassert(strlen(entry) < sizeof(path->segment));

	/* Not found -> Add new segment */

	if (!path) {
		xassert(!parent->child);

		/* always create root place holder */
		parent->child = path = xmalloc(sizeof(*path));
		*path = (path_t) {
			.magic = PATH_MAGIC,
		};
	}

	xassert(!path->next);

	/* Templates must always be last next pointer */
	if (path->is_template) {
		path_t *end = path;

		/* there can only be 1 template per segment */
		xassert(!template);

		path = lpath->next = xmalloc(sizeof(*path));
		*path = (path_t) {
			.magic = PATH_MAGIC,
			.next = end,
		};
	} else {
		path = path->next = xmalloc(sizeof(*path));
		*path = (path_t) {
			.magic = PATH_MAGIC,
			.is_template = template,
		};
	}

	(void) strlcpy(path->segment, entry, sizeof(path->segment));
	args->path = path;
	return SLURM_SUCCESS;
}

extern void http_router_bind_funcname(http_request_method_t method,
				      const char *path,
				      http_router_on_request_event_t on_request,
				      const char *on_request_func,
				      http_router_on_fini on_fini,
				      const char *on_fini_func, void *path_arg)
{
	int rc = EINVAL;
	path_add_args_t args = {
		.magic = PATH_ADD_ARGS_MAGIC,
		.path = &router.methods[method],
	};

	if ((rc = url_path_walk(path, true, _on_path_add_entry, &args))) {
		fatal_abort("Registering path failed: %s", slurm_strerror(rc));
	} else {
		path_t *path = args.path;

		xassert(path);
		xassert(path->magic == PATH_MAGIC);
		xassert(!path->on_request);
		xassert(!path->on_fini);

		path->on_request = on_request;
		path->on_request_func = on_request_func;
		path->on_fini = on_fini;
		path->on_fini_func = on_fini_func;
		path->path_arg = path_arg;
	}
}

static int _on_path_find_entry(const char *entry, bool template, void *arg)
{
	path_find_args_t *args = arg;
	path_t *parent = args->path;
	path_t *path = parent->child;

	xassert(args->magic == PATH_FIND_ARGS_MAGIC);
	xassert(parent->magic == PATH_MAGIC);
	xassert(entry[0]);
	xassert(!template);
	/* First pointer is always the root segment */
	xassert(!path || (!path->segment[0] && !path->is_template));

	/* Search next segments for match */
	while (path) {
		xassert(path->magic == PATH_MAGIC);

		if (!path->is_template && xstrcmp(entry, path->segment)) {
			if (path->next) {
				path = path->next;
				continue;
			}

			break;
		}

		args->path = path;
		return SLURM_SUCCESS;
	}

	return ESLURM_URL_INVALID_PATH;
}

extern int http_router_on_request(http_con_t *hcon, const char *name,
				  const http_con_request_t *request, void *arg)
{
	path_find_args_t args = {
		.magic = PATH_FIND_ARGS_MAGIC,
		.path = &router.methods[request->method],
	};

	if (url_path_walk(request->url.path, false, _on_path_find_entry,
			  &args) ||
	    !args.path->on_request) {
		log_flag(NET, "%s: Unable to find URL: %s %s",
			 __func__, get_http_method_string(request->method),
			 request->url.path);
		return router.on_not_found(hcon, name, request, arg, NULL);
	} else {
		path_t *path = args.path;

		xassert(path);
		xassert(path->magic == PATH_MAGIC);
		xassert(path->on_request);

		log_flag(NET, "%s: Calling %s(%p) for URL: %s %s",
			 __func__, path->on_request_func, path->path_arg,
			 get_http_method_string(request->method),
			 request->url.path);

		return path->on_request(hcon, name, request, arg,
					path->path_arg);
	}
}
