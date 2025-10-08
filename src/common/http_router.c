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

#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/http_router.h"
#include "src/common/xhash.h"
#include "src/common/xmalloc.h"

#define REQUEST_MAX_BYTES 1024

#define MAGIC 0xdafbbaaf

typedef struct {
	int magic; /* MAGIC */
	/* formatted request string to use as key */
	char request[REQUEST_MAX_BYTES];
	/* number of characters in request string */
	int request_len;
	/* Callback for path */
	http_con_on_request_event_t on_request;
} path_t;

static struct {
	http_con_on_request_event_t on_not_found;
	xhash_t *paths;
} router = { 0 };

static size_t _print_request(char *request, const size_t bytes,
			     const http_request_method_t method,
			     const char *path)
{
	int wrote = 0;

	if ((wrote = snprintf(request, bytes, "%s %s",
			      get_http_method_string(method), path)) == bytes)
		return 0;

	return wrote;
}

static void _path_id(void *item, const char **key, uint32_t *key_len)
{
	path_t *rpath = item;

	xassert(rpath->magic == MAGIC);

	*key = rpath->request;
	*key_len = rpath->request_len;
}

static void _path_free(void *item)
{
	path_t *rpath = item;

	xassert(rpath->magic == MAGIC);
	rpath->magic = ~MAGIC;
	xfree(rpath);
}

extern void http_router_init(http_con_on_request_event_t on_not_found)
{
	xassert(!router.paths);

	router.paths = xhash_init(_path_id, _path_free);
	xassert(router.paths);

	xassert(!router.on_not_found);
	router.on_not_found = on_not_found;
	xassert(router.on_not_found);
}

extern void http_router_fini(void)
{
	xhash_free_ptr(&router.paths);
}

static path_t *_find_path(http_request_method_t method, const char *path)
{
	path_t *rpath = NULL;
	char request[REQUEST_MAX_BYTES];
	int len;

	if (!(len = _print_request(request, sizeof(request), method, path)))
		return NULL;

	rpath = xhash_get(router.paths, request, len);
	xassert(!rpath || rpath->magic == MAGIC);

	return rpath;
}

extern void http_router_bind(http_request_method_t method, const char *path,
			     http_con_on_request_event_t on_request)
{
	path_t *rpath = NULL;

	xassert(path);
	xassert(path[0] == '/');
	xassert(method != HTTP_REQUEST_INVALID);
	xassert(on_request);

	rpath = xmalloc(sizeof(*rpath));
	*rpath = (path_t) {
		.magic = MAGIC,
		.on_request = on_request,
	};

	rpath->request_len =
		_print_request(rpath->request, sizeof(rpath->request), method,
			       path);
	xassert(rpath->request_len > 0);

	xassert(!_find_path(method, path));
	(void) xhash_add(router.paths, rpath);
}

extern int http_router_on_request(http_con_t *hcon, const char *name,
				  const http_con_request_t *request, void *arg)
{
	path_t *rpath = NULL;

	if (!(rpath = _find_path(request->method, request->url.path)))
		return router.on_not_found(hcon, name, request, arg);

	xassert(rpath->magic == MAGIC);
	return rpath->on_request(hcon, name, request, arg);
}
