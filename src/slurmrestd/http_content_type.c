/*****************************************************************************\
 *  http_content_type.c - Slurm REST API http content type
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

#include <ctype.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/http_content_type.h"

static void _list_delete(void *x)
{
	http_header_accept_t *obj = (http_header_accept_t *) x;

	if (!obj)
		return;

	xfree(obj->type);
	xfree(obj);
}

static int _compare_q(void *x, void *y)
{
	http_header_accept_t *xobj = (http_header_accept_t *) x;
	http_header_accept_t *yobj = (http_header_accept_t *) y;

	if (xobj->q < yobj->q)
		return -1;
	else if (xobj->q > yobj->q)
		return 1;

	return 0;
}

static void _parse_http_accept_entry(char *entry, List l)
{
	char *save_ptr = NULL;
	char *token = NULL;
	char *buffer = xstrdup(entry);
	http_header_accept_t *act = xmalloc(sizeof(*act));
	act->type = NULL;
	act->q = 1; /* default to 1 per rfc7231:5.3.1 */

	token = strtok_r(buffer, ";", &save_ptr);

	if (token) {
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

extern List parse_http_accept(const char *accept)
{
	List l = list_create(_list_delete);
	xassert(accept);
	char *save_ptr = NULL;
	char *token = NULL;
	char *buffer = xstrdup(accept);

	token = strtok_r(buffer, ",", &save_ptr);
	while (token) {
		xstrtrim(token);
		_parse_http_accept_entry(token, l);
		token = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(buffer);

	list_sort(l, _compare_q);

	return l;
}

extern bool is_mime_matching_type(const char *a, const char *b)
{
	bool rc = false;
	size_t count = 0;
	char *a_save_ptr = NULL;
	char *a_token = NULL;
	char *a_buffer = xstrdup(a);
	char *b_save_ptr = NULL;
	char *b_token = NULL;
	char *b_buffer = xstrdup(b);

	if (a == NULL || b == NULL) {
		xfree(a_buffer);
		xfree(b_buffer);
		error("%s: empty mime type string", __func__);
		return false;
	}

	a_token = strtok_r(a_buffer, "/", &a_save_ptr);
	b_token = strtok_r(b_buffer, "/", &b_save_ptr);
	while (a_token && b_token) {
		++count;
		xstrtrim(a_token);
		xstrtrim(b_token);

		if (!xstrcmp(a_token, "*") || !xstrcmp(b_token, "*") ||
		    !xstrcasecmp(a_token, b_token))
			rc = true;
		else
			rc = false;

		a_token = strtok_r(NULL, "/", &a_save_ptr);
		b_token = strtok_r(NULL, "/", &b_save_ptr);
	}
	xfree(a_buffer);
	xfree(b_buffer);

	if (count != 2) {
		error("%s: invalid mime type: %s or %s", __func__, a, b);
		rc = false;
	}

	return rc;
}

extern mime_types_t get_mime_type(const char *type)
{
	if (!xstrcasecmp(type, "application/json") ||
	    !xstrcasecmp(type, "application/jsonrequest"))
		return MIME_JSON;

	if (!xstrcasecmp(type, "application/x-www-form-urlencoded"))
		return MIME_URL_ENCODED;

	if (!xstrcasecmp(type, "application/x-yaml") ||
	    !xstrcasecmp(type, "text/yaml"))
		return MIME_YAML;

	return MIME_UNKNOWN;
}

extern mime_types_t find_matching_mime_type(const char *type)
{
	if (type == NULL)
		return MIME_UNKNOWN;

	if (is_mime_matching_type(type, "application/json") ||
	    is_mime_matching_type(type, "application/jsonrequest"))
		return MIME_JSON;

	if (is_mime_matching_type(type, "application/x-yaml") ||
	    is_mime_matching_type(type, "text/yaml"))
		return MIME_YAML;

	if (is_mime_matching_type(type, "application/x-www-form-urlencoded"))
		return MIME_URL_ENCODED;

	return MIME_UNKNOWN;
}

extern const char *get_mime_type_str(mime_types_t type)
{
	switch (type) {
	case MIME_YAML:
		return "application/x-yaml";
	case MIME_JSON:
		return "application/json";
	case MIME_URL_ENCODED:
		return "application/x-www-form-urlencoded";
	default:
		return NULL;
	}
}
