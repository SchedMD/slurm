/*****************************************************************************\
 *  http_mime.c - handling HTTP mime types
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/http_con.h"
#include "src/common/http_mime.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#define MAGIC_HEADER_ACCEPT 0xDF9EFABB

typedef struct {
	int magic; /* MAGIC_HEADER_ACCEPT */
	char *type; /* mime type and sub type unchanged */
	float q; /* quality factor (priority) */
} accept_t;

#define MAGIC_FOREACH_ACCEPT 0xBFFEFABB

typedef struct {
	int magic; /* MAGIC_FOREACH_ACCEPT  */
	const char **write_mime_ptr;
	const char *name;
} foreach_accept_t;

static void _parse_accept(const char *accept, list_t *l)
{
	char *save_ptr = NULL;
	char *token = NULL;
	char *buffer = xstrdup(accept);
	accept_t *act = xmalloc(sizeof(*act));

	*act = (accept_t) {
		.magic = MAGIC_HEADER_ACCEPT,
		.type = NULL,
		.q = 1, /* default to 1 per rfc7231:5.3.1 */
	};

	if ((token = strtok_r(buffer, ";", &save_ptr))) {
		/* first token is the mime type */
		xstrtrim(token);
		act->type = xstrdup(token);
	}

	while ((token = strtok_r(NULL, ",", &save_ptr))) {
		xstrtrim(token);
		sscanf(token, "q=%f", &act->q);
	}

	xfree(buffer);

	debug5("%s: found %s with q=%f", __func__, act->type, act->q);

	list_append(l, act);
}

static void _http_accept_list_delete(accept_t *obj)
{
	if (!obj)
		return;

	xassert(obj->magic == MAGIC_HEADER_ACCEPT);
	obj->magic = ~MAGIC_HEADER_ACCEPT;

	xfree(obj->type);
	xfree(obj);
}

static int _parse_each_accept(void *x, void *arg)
{
	accept_t *ptr = x;
	foreach_accept_t *args = arg;
	const char *name = args->name;
	const char **write_mime_ptr = args->write_mime_ptr;

	xassert(ptr->magic == MAGIC_HEADER_ACCEPT);
	xassert(args->magic == MAGIC_FOREACH_ACCEPT);

	debug4("%s: [%s] accepts %s with q=%f",
	       __func__, name, ptr->type, ptr->q);

	if ((*write_mime_ptr = resolve_mime_type(ptr->type, NULL))) {
		debug4("%s: [%s] found accepts %s=%s with q=%f",
		       __func__, name, ptr->type, *write_mime_ptr, ptr->q);
		/* stop list_for_each() */
		return -1;
	}

	debug4("%s: [%s] rejecting accepts %s with q=%f",
	       __func__, name, ptr->type, ptr->q);
	return 0;
}

static int _compare_q(void *x, void *y)
{
	accept_t **xobj_ptr = x;
	accept_t **yobj_ptr = y;
	accept_t *xobj = *xobj_ptr;
	accept_t *yobj = *yobj_ptr;

	xassert(xobj->magic == MAGIC_HEADER_ACCEPT);
	xassert(yobj->magic == MAGIC_HEADER_ACCEPT);

	if (xobj->q < yobj->q)
		return -1;
	else if (xobj->q > yobj->q)
		return 1;

	return 0;
}

static void _parse_http_accept(const char *name,
			       const http_con_request_t *request,
			       const char **write_mime_ptr)
{
	char *save_ptr = NULL;
	char *token = NULL;
	foreach_accept_t args = {
		.magic = MAGIC_FOREACH_ACCEPT,
		.write_mime_ptr = write_mime_ptr,
		.name = name,
	};
	char *buffer = xstrdup(request->accept);
	list_t *l = list_create((ListDelF) _http_accept_list_delete);

	token = strtok_r(buffer, ",", &save_ptr);
	while (token) {
		xstrtrim(token);
		_parse_accept(token, l);
		token = strtok_r(NULL, ",", &save_ptr);
	}

	xfree(buffer);

	list_sort(l, _compare_q);

	(void) list_for_each(l, _parse_each_accept, &args);

	FREE_NULL_LIST(l);
}

extern int http_resolve_mime_types(const char *name,
				   const http_con_request_t *request,
				   const char **read_mime_ptr,
				   const char **write_mime_ptr)
{
	xassert(!*read_mime_ptr);
	xassert(!*write_mime_ptr);

	if (!(*read_mime_ptr = request->content_type)) {
		*read_mime_ptr = MIME_TYPE_URL_ENCODED;

		debug4("%s: [%s] did not provide a known content type header. Assuming URL encoded.",
		       __func__, name);
	}

	if (request->accept) {
		_parse_http_accept(name, request, write_mime_ptr);
	} else {
		debug3("%s: [%s] Accept header not specified. Defaulting to JSON.",
		       __func__, name);
		*write_mime_ptr = MIME_TYPE_JSON;
	}

	if (!*write_mime_ptr)
		return ESLURM_HTTP_UNKNOWN_ACCEPT_MIME_TYPE;

	/*
	 * RFC7230 3.3: Allows for any request to have a BODY but doesn't require
	 * the server do anything with it.
	 *	Request message framing is independent of method semantics, even
	 *	if the method does not define any use for a message body.
	 * RFC7231 Appendix B:
	 *	To be consistent with the method-neutral parsing algorithm of
	 *	[RFC7230], the definition of GET has been relaxed so that
	 *	requests can have a body, even though a body has no meaning for
	 *	GET.  (Section 4.3.1)
	 *
	 * In order to avoid confusing the client when their query or body gets
	 * ignored, reject request when both query and body are provided.
	 */
	if ((request->content_bytes > 0) && request->url.query &&
	    request->url.query[0])
		return ESLURM_HTTP_UNEXPECTED_BODY;

	if (xstrcasecmp(*read_mime_ptr, MIME_TYPE_URL_ENCODED) &&
	    !request->content_bytes) {
		/*
		 * RFC7273#3.1.1.5 only specifies a sender SHOULD send
		 * the correct content-type header but allows for them to be
		 * wrong and expects the server to handle that gracefully.
		 *
		 * We will instead override the mime type if there is empty body
		 * content to avoid unneccesssily rejecting otherwise compliant
		 * requests.
		 */
		debug("%s: [%s] Overriding content type from %s to %s for %s",
		      __func__, name, *read_mime_ptr, MIME_TYPE_URL_ENCODED,
		      get_http_method_string(request->method));

		*read_mime_ptr = MIME_TYPE_URL_ENCODED;
	}

	debug3("%s: [%s] mime read: %s write: %s",
	       __func__, name, *read_mime_ptr, *write_mime_ptr);
	return SLURM_SUCCESS;
}
