/*****************************************************************************\
 *  HTTP Parser plugin interface
 ******************************************************************************
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

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/http_parser.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(http_parser_g_init, slurm_http_parser_g_init);
strong_alias(http_parser_g_fini, slurm_http_parser_g_fini);
strong_alias(http_parser_g_new_parse_request,
	     slurm_http_parser_g_new_parse_request);
strong_alias(http_parser_g_free_parse_request,
	     slurm_http_parser_g_free_parse_request);
strong_alias(http_parser_g_parse_request, slurm_http_parser_g_parse_request);

typedef struct {
	int (*new_parse_request)(const char *name,
				 const http_parser_callbacks_t *callbacks,
				 void *callback_arg,
				 http_parser_state_t **state_ptr);
	void (*free_parse_request)(http_parser_state_t **state_ptr);
	int (*parse_request)(http_parser_state_t *state, const buf_t *buffer,
			     ssize_t *bytes_parsed_ptr);
} ops_t;

/* Must be synchronized with ops_t above */
static const char *syms[] = {
	"http_parser_p_new_parse_request",
	"http_parser_p_free_parse_request",
	"http_parser_p_parse_request",
};

static ops_t ops = { 0 };
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

extern int http_parser_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = HTTP_PARSER_MAJOR_TYPE;
	char *http_parser_type = NULL;

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!(http_parser_type = xstrdup(slurm_conf.http_parser_type))) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	if (!(g_context = plugin_context_create(plugin_type, http_parser_type,
						(void **) &ops, syms,
						sizeof(syms)))) {
		error("cannot create %s context for %s",
		      plugin_type, http_parser_type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	xfree(http_parser_type);
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern void http_parser_g_fini(void)
{
	slurm_rwlock_wrlock(&context_lock);

	if (g_context) {
		(void) plugin_context_destroy(g_context);
		g_context = NULL;
	}

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_rwlock_unlock(&context_lock);
}

extern int http_parser_g_new_parse_request(const char *name,
					   const http_parser_callbacks_t
						   *callbacks,
					   void *callback_arg,
					   http_parser_state_t **state_ptr)
{
	xassert(!*state_ptr);
	xassert(name && name[0]);

	if (plugin_inited != PLUGIN_INITED)
		return ESLURM_PLUGIN_NOT_LOADED;

	return (*(ops.new_parse_request))(name, callbacks, callback_arg,
					  state_ptr);
}

extern void http_parser_g_free_parse_request(http_parser_state_t **state_ptr)
{
	if (plugin_inited != PLUGIN_INITED)
		return;

	(*(ops.free_parse_request))(state_ptr);

	xassert(!*state_ptr);
}

extern int http_parser_g_parse_request(http_parser_state_t *state,
				       const buf_t *buffer,
				       ssize_t *bytes_parsed_ptr)
{
	if (plugin_inited != PLUGIN_INITED)
		return ESLURM_PLUGIN_NOT_LOADED;

	return (*(ops.parse_request))(state, buffer, bytes_parsed_ptr);
}
