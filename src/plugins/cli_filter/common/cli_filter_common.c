/*****************************************************************************\
 *  cli_filter_common.c - Common infrastructure available to all cli_filter
 *****************************************************************************
 *  Copyright (C) 2017-2019 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory (cf, DISCLAIMER).
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <ctype.h>
#include <grp.h>
#include <pwd.h>
#include "cli_filter_common.h"

#include "src/interfaces/cli_filter.h"
#include "src/interfaces/serializer.h"
#include "src/common/data.h"
#include "src/common/spank.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#define MAX_STR_LEN 524288

extern char **environ;

char *cli_filter_json_set_options(slurm_opt_t *options)
{
	int rc;
	int  argc = 0;
	char **argv = NULL;
	char *json = NULL;
	char *name = NULL;
	char *value = NULL;
	char *plugin = NULL;
	size_t st = 0;
	void *spst = NULL;
	data_t *d, *dargv;

	d = data_set_dict(data_new());

	while (slurm_option_get_next_set(options, &name, &value, &st))
		data_set_string_own(data_key_set(d, name), value);

	while (spank_option_get_next_set(&plugin, &name, &value, &spst)) {
		char *sname = xstrdup_printf("spank:%s", name);

		data_set_string_own(data_key_set(d, sname), value);

		xfree(sname);
	}

	argv = options->argv;
	argc = options->argc;

	dargv = data_set_list(data_key_set(d, "argv"));
	for (char **ptr = argv; ptr && *ptr && ptr - argv < argc; ptr++)
		data_set_string(data_list_append(dargv), *ptr);

	if ((rc = serialize_g_data_to_string(&json, NULL, d, MIME_TYPE_JSON,
					     SER_FLAGS_COMPACT))) {
		/* json will remain NULL on failure */
		error("%s: unable to serialize JSON: %s", __func__,
		      slurm_strerror(rc));
	}

	FREE_NULL_DATA(d);
	xfree(plugin);
	xfree(name);

	return json;
}

char *cli_filter_json_env(void)
{
	int rc;
	char *json = NULL;
	static size_t len = 0;
	data_t *d = data_set_dict(data_new());

	if (!len)
		len = strlen(SPANK_OPTION_ENV_PREFIX);

	for (char **ptr = environ; ptr && *ptr; ptr++) {
		char *key, *value;

		if (!xstrncmp(*ptr, "SLURM_", 6) ||
		    !xstrncmp(*ptr, "SPANK_", 6) ||
		    !xstrncmp(*ptr, SPANK_OPTION_ENV_PREFIX, len))
			continue;

		key = xstrdup(*ptr);

		if (!(value = xstrchr(key, '='))) {
			xfree(key);
			continue;
		}
		*value++ = '\0';

		data_set_string(data_key_set(d, key), value);

		xfree(key);
	}

	if ((rc = serialize_g_data_to_string(&json, NULL, d, MIME_TYPE_JSON,
					     SER_FLAGS_COMPACT))) {
		/* json will remain NULL on failure */
		error("%s: unable to serialize JSON: %s", __func__,
		      slurm_strerror(rc));
	}

	FREE_NULL_DATA(d);

	return json;
}
