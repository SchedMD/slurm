/*****************************************************************************\
 *  cli_filter_common.c - Common infrastructure available to all cli_filter
 *****************************************************************************
 *  Copyright (C) 2017-2019 Regents of the University of California
 *  Produced at Lawrence Berkeley National Laboratory (cf, DISCLAIMER).
 *  Written by Douglas Jacobsen <dmjacobsen@lbl.gov>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
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

#include "src/common/cli_filter.h"
#include "src/common/plugstack.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#define MAX_STR_LEN 524288

extern char **environ;

/* Escape characters according to RFC7159, stolen from jobcomp/elasticsearch */
static char *_json_escape(const char *str)
{
	char *ret = NULL;
	int i, o, len;

	len = strlen(str) * 2 + 128;
	ret = xmalloc(len);
	for (i = 0, o = 0; str[i]; ++i) {
		if (o >= MAX_STR_LEN) {
			break;
		} else if ((o + 8) >= len) {
			len *= 2;
			ret = xrealloc(ret, len);
		}
		switch (str[i]) {
		case '\\':
			ret[o++] = '\\';
			ret[o++] = '\\';
			break;
		case '"':
			ret[o++] = '\\';
			ret[o++] = '\"';
			break;
		case '\n':
			ret[o++] = '\\';
			ret[o++] = 'n';
			break;
		case '\b':
			ret[o++] = '\\';
			ret[o++] = 'b';
			break;
		case '\f':
			ret[o++] = '\\';
			ret[o++] = 'f';
			break;
		case '\r':
			ret[o++] = '\\';
			ret[o++] = 'r';
			break;
		case '\t':
			ret[o++] = '\\';
			ret[o++] = 't';
			break;
		case '<':
			ret[o++] = '\\';
			ret[o++] = 'u';
			ret[o++] = '0';
			ret[o++] = '0';
			ret[o++] = '3';
			ret[o++] = 'C';
			break;
		case '/':
			ret[o++] = '\\';
			ret[o++] = '/';
			break;
		default:
			ret[o++] = str[i];
		}
	}
	return ret;
}

char *cli_filter_json_set_options(slurm_opt_t *options)
{
	int  argc = 0;
	char **argv = NULL;
	char *json = xmalloc(2048);
	char *name = NULL;
	char *value = NULL;
	char *plugin = NULL;
	size_t len = 0;
	size_t st = 0;
	void *spst = NULL;

	xstrcat(json, "{");
	st = 0;
	while (slurm_option_get_next_set(options, &name, &value, &st)) {
		char *lname = _json_escape(name);
		char *lvalue = _json_escape(value);
		xstrfmtcat(json, "\"%s\":\"%s\",", lname, lvalue);
		xfree(lname);
		xfree(lvalue);
		xfree(name);
		xfree(value);
	}

	while (spank_option_get_next_set(&plugin, &name, &value, &spst)) {
		char *tmp = xstrdup_printf("\"spank:%s:%s\":\"%s\",",
					   plugin, name, value);
		char *esc = _json_escape(tmp);
		xstrcat(json, esc);
		xfree(tmp);
		xfree(esc);
		xfree(plugin);
		xfree(name);
		xfree(value);
	}

	if (options->sbatch_opt) {
		argv = options->sbatch_opt->script_argv;
		argc = options->sbatch_opt->script_argc;
	} else if (options->srun_opt) {
		argv = options->srun_opt->argv;
		argc = options->srun_opt->argc;
	}

	xstrcat(json, "\"argv\": [");
	for (char **ptr = argv; ptr && *ptr && ptr - argv < argc; ptr++) {
		char *esc = _json_escape(*ptr);
		xstrfmtcat(json, "\"%s\",", esc);
		xfree(esc);
	}
	len = strlen(json);
	if (json[len - 1] == ',')
		json[len - 1] = '\0';
	xstrcat(json, "]}");
	return json;
}

char *cli_filter_json_env(void)
{
	char **ptr = NULL;
	char *json = xmalloc(4096);
	size_t len = 0;
	xstrcat(json, "{");
	len = strlen(SPANK_OPTION_ENV_PREFIX);
	for (ptr = environ; ptr && *ptr; ptr++) {
		if (!strncmp(*ptr, "SLURM_", 6))
			continue;
		if (!strncmp(*ptr, "SPANK_", 6))
			continue;
		if (!strncmp(*ptr, SPANK_OPTION_ENV_PREFIX, len))
			continue;

		char *key = xstrdup(*ptr);
		char *value = strchr(key, '=');
		*value++ = '\0';
		char *key_esc = _json_escape(key);
		char *value_esc = _json_escape(value);
		xstrfmtcat(json, "\"%s\":\"%s\",", key_esc, value_esc);
		xfree(key);
		xfree(key_esc);
		xfree(value_esc);
	}
	len = strlen(json);
	if (len > 1)
		json[len - 1] = '}';
	else
		xfree(json);
	return json;
}
