/*****************************************************************************\
 *  URL Parser plugin interface
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

#include "src/common/http.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/interfaces/http_parser.h"
#include "src/interfaces/url_parser.h"

/* Define slurm-specific aliases for use by plugins, see slurm_xlator.h. */
strong_alias(url_parser_g_init, slurm_url_parser_g_init);
strong_alias(url_parser_g_fini, slurm_url_parser_g_fini);
strong_alias(url_parser_g_parse, slurm_url_parser_g_parse);

typedef struct {
	int (*parse)(const char *name, const buf_t *buffer, url_t *dst);
} ops_t;

/* Must be synchronized with opts_t above */
static const char *syms[] = {
	"url_parser_p_parse",
};

static ops_t ops = { 0 };
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_rwlock_t context_lock = PTHREAD_RWLOCK_INITIALIZER;

extern int url_parser_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *plugin_type = URL_PARSER_MAJOR_TYPE;
	const char *url_parser_type = NULL;

	slurm_rwlock_wrlock(&context_lock);

	if (plugin_inited != PLUGIN_NOT_INITED)
		goto done;

	if (!(url_parser_type = slurm_conf.url_parser_type)) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	/* Check for overloaded libhttp plugin and set correct plugin name */
	if (!xstrcmp(url_parser_type, LIBHTTP_PARSER_PLUGIN) ||
	    !xstrcmp(url_parser_type, URL_PARSER_PREFIX LIBHTTP_PARSER_PLUGIN))
		url_parser_type = HTTP_PARSER_PREFIX LIBHTTP_PARSER_PLUGIN;

	if (!(g_context = plugin_context_create(plugin_type, url_parser_type,
						(void **) &ops, syms,
						sizeof(syms)))) {
		error("cannot create %s context for %s",
		      plugin_type, url_parser_type);
		rc = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;

done:
	slurm_rwlock_unlock(&context_lock);
	return rc;
}

extern void url_parser_g_fini(void)
{
	slurm_rwlock_wrlock(&context_lock);

	if (g_context) {
		(void) plugin_context_destroy(g_context);
		g_context = NULL;
	}

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_rwlock_unlock(&context_lock);
}

extern int url_parser_g_parse(const char *name, const buf_t *buffer, url_t *url)
{
	if (plugin_inited != PLUGIN_INITED)
		return ESLURM_PLUGIN_NOT_LOADED;

	return (*(ops.parse))(name, buffer, url);
}
