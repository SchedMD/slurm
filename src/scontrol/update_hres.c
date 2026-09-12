/*****************************************************************************\
 *  update_hres.c - hierarchical resource update functions for scontrol.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "scontrol.h"

/*
 * We cannot use parse_uint32 because it would error for negative values,
 * and -1 is allowed here.
 */
static int _parse_count(char *str, bool allow_infinite, uint32_t *count)
{
	long long llcount;
	char *endptr = NULL;

	errno = 0;
	if (!str || (str[0] == '\0'))
		return SLURM_ERROR;
	llcount = strtoll(str, &endptr, 10);
	if (errno || (endptr[0] != '\0')) {
		return SLURM_ERROR;
	} else if (allow_infinite && (llcount == -1)) {
		*count = INFINITE;
	} else if ((llcount < 0) || (llcount >= NO_VAL)) {
		/* Do not allow NO_VAL which means unset */
		return SLURM_ERROR;
	} else {
		*count = llcount;
	}
	return SLURM_SUCCESS;
}

/*
 * Convert the "true" or "false" string accepted by DisableHRES and
 * DisableLayer into the boolean sent in hres_update_msg_t.
 */
static int _parse_disable(char *str, uint16_t *disable)
{
	if (!xstrcasecmp(str, "true"))
		*disable = true;
	else if (!xstrcasecmp(str, "false"))
		*disable = false;
	else
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

static int _parse_one_base(char *str, list_t *base)
{
	hres_variable_t *hres_var;
	uint32_t count;
	char *value_str;

	if (!(value_str = xstrchr(str, ':')))
		return SLURM_ERROR;
	*value_str = '\0'; /* NULL-terminate name */
	value_str++;
	if (!str[0] || _parse_count(value_str, false, &count))
		return SLURM_ERROR;

	hres_var = xmalloc(sizeof(*hres_var));
	hres_var->name = xstrdup(str);
	hres_var->value = count;
	list_append(base, hres_var);
	return SLURM_SUCCESS;
}

/*
 * Valid Base string:
 *   Base=<name>:<value>[,<name>:<value>...]
 * An empty value (or "(null)", as shown by "scontrol show license") clears
 * the base.
 */
static int _parse_base(char *base_str, list_t **base)
{
	char *tmp_str, *tok, *saveptr = NULL;

	FREE_NULL_LIST(*base);
	*base = list_create(hres_variable_free);

	if (!base_str[0] || !xstrcasecmp(base_str, "(null)"))
		return SLURM_SUCCESS; /* empty list clears the base */

	/*
	 * Avoid modifying the original string, since the original string is
	 * used in error messages.
	 */
	tmp_str = xstrdup(base_str);

	for (tok = strtok_r(tmp_str, ",", &saveptr); tok;
	     tok = strtok_r(NULL, ",", &saveptr)) {
		if (_parse_one_base(tok, *base)) {
			FREE_NULL_LIST(*base);
			break;
		}
	}
	xfree(tmp_str);
	if (!(*base) || !list_count(*base)) {
		FREE_NULL_LIST(*base);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

extern int scontrol_update_hres(int argc, char **argv)
{
	hres_update_msg_t *msg = xmalloc(sizeof(*msg));
	int update_cnt = 0;
	int rc = SLURM_SUCCESS;

	slurm_init_hres_update_msg(msg);

	for (int i = 0; i < argc; i++) {
		char *tag = argv[i];
		char *val = xstrchr(tag, '=');

		if (!val) {
			error("Invalid input: %s", argv[i]);
			rc = SLURM_ERROR;
			goto fini;
		}

		*val = '\0';
		val++;

		if (!xstrcasecmp(tag, "HRESName")) {
			xfree(msg->hres_name);
			msg->hres_name = xstrdup(val);
		} else if (!xstrcasecmp(tag, "LayerName")) {
			xfree(msg->layer_name);
			msg->layer_name = xstrdup(val);
		} else if (!xstrcasecmp(tag, "Nodes")) {
			xfree(msg->nodes);
			msg->nodes = xstrdup(val);
			update_cnt++;
		} else if (!xstrcasecmp(tag, "Count")) {
			if (_parse_count(val, true, &msg->count)) {
				error("Invalid Count \"%s\"", val);
				rc = SLURM_ERROR;
				goto fini;
			}
			update_cnt++;
		} else if (!xstrcasecmp(tag, "Base")) {
			if (_parse_base(val, &msg->base)) {
				error("Invalid Base \"%s\"", val);
				rc = SLURM_ERROR;
				goto fini;
			}
			update_cnt++;
		} else if (!xstrcasecmp(tag, "DisableHRES")) {
			if (_parse_disable(val, &msg->disable_hres)) {
				error("Invalid DisableHRES \"%s\"; acceptable values are \"true\" or \"false\"",
				      val);
				rc = SLURM_ERROR;
				goto fini;
			}
			update_cnt++;
		} else if (!xstrcasecmp(tag, "DisableLayer")) {
			if (_parse_disable(val, &msg->disable_layer)) {
				error("Invalid DisableLayer \"%s\"; acceptable values are \"true\" or \"false\"",
				      val);
				rc = SLURM_ERROR;
				goto fini;
			}
			update_cnt++;
		} else {
			error("Invalid input: %s=%s", tag, val);
			rc = SLURM_ERROR;
			goto fini;
		}
	}
	if (!msg->hres_name) {
		error("HRESName is required");
		rc = SLURM_ERROR;
		goto fini;
	}
	if (!update_cnt) {
		error("No updates specified");
		rc = SLURM_ERROR;
		goto fini;
	}

	if ((rc = slurm_update_hres(msg)))
		error("Failed to update HRES: %s", slurm_strerror(rc));
fini:
	if (rc) {
		error("Request aborted");
		exit_code = 1;
	}
	slurm_free_hres_update_msg(msg);
	return rc;
}
