/*****************************************************************************\
 *  job_submit_tpu.c - Validate and expand --layout for TPU accelerators.
 *****************************************************************************
 *  Copyright (C) 2026 Google LLC.
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

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/tpu_conf.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Job submit TPU plugin";
const char plugin_type[] = "job_submit/tpu";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static tpu_conf_t *tpu_conf = NULL;

extern int init(void)
{
	int rc;

	if ((rc = tpu_conf_load(&tpu_conf))) {
		/*
		 * Not fatal. slurmctld stays up and every TPU submission is
		 * rejected with an explanation, which is far easier for an
		 * admin to diagnose than a controller that refuses to start.
		 */
		error("%s: no usable %s: %s. TPU layout requests will be rejected.",
		      plugin_type, "tpu.yaml", slurm_strerror(rc));
	}

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	tpu_conf_destroy(tpu_conf);
	tpu_conf = NULL;
}

/*
 * Copy all comma-separated entries from tres_per_node except gres/tpu
 * specifications, so updating Layout= via job_modify() replaces any previous
 * TPU GRES while preserving unrelated GRES on the job.
 */
static char *_copy_non_tpu_tres(const char *tres_per_node)
{
	char *copy, *tok, *save_ptr = NULL, *out = NULL;

	if (!tres_per_node)
		return NULL;

	copy = xstrdup(tres_per_node);
	for (tok = strtok_r(copy, ",", &save_ptr); tok;
	     tok = strtok_r(NULL, ",", &save_ptr)) {
		if (!xstrncmp(tok, "gres/tpu", 8) &&
		    ((tok[8] == '\0') || (tok[8] == ':')))
			continue;
		xstrfmtcat(out, "%s%s", (out ? "," : ""), tok);
	}
	xfree(copy);

	return out;
}

/*
 * Hand a user facing error message to the caller.
 * OUT err_msg - caller's message pointer, may be NULL
 * IN err - xmalloc()'d message, ownership is taken
 */
static void _set_err_msg(char **err_msg, char *err)
{
	if (err_msg) {
		xfree(*err_msg);
		*err_msg = err;
	} else {
		xfree(err);
	}
}

/*
 * Find the --layout pair that requests TPUs.
 *
 * --layout is a list of key=value pairs that Slurm does not interpret, so
 * pairs meant for other plugins pass through untouched. A pair requests TPUs
 * if its key names a configured accelerator family, or if its value is a chip
 * geometry such as "2x2x4", so a misspelled family is still claimed and
 * rejected.
 *
 * IN layout - raw --layout value, may be NULL
 * OUT pair - xmalloc()'d "<family>=<shape>" pair, or NULL if no pair requests
 *	TPUs; caller must xfree()
 * OUT err - on failure, an xmalloc()'d message for the user; caller must
 *	xfree()
 * RET SLURM_SUCCESS, or ESLURM_INVALID_LAYOUT if more than one pair requests
 *	TPUs
 */
static int _find_tpu_pair(const char *layout, char **pair, char **err)
{
	char *copy, *tok, *save_ptr = NULL;
	int rc = SLURM_SUCCESS;

	*pair = NULL;

	if (!layout || !layout[0])
		return SLURM_SUCCESS;

	copy = xstrdup(layout);
	for (tok = strtok_r(copy, ",", &save_ptr); tok;
	     tok = strtok_r(NULL, ",", &save_ptr)) {
		tpu_geometry_t geom;
		char *sep;
		bool is_family;

		if (!(sep = xstrchr(tok, '=')))
			continue;

		*sep = '\0';
		is_family = tpu_conf_find_type(tpu_conf, tok);
		*sep = '=';

		if (!is_family && !tpu_shape_parse(sep + 1, &geom))
			continue;

		if (*pair) {
			xstrfmtcat(
				*err,
				"--layout can request TPUs from one accelerator family only, got '%s' and '%s'",
				*pair, tok);
			xfree(*pair);
			rc = ESLURM_INVALID_LAYOUT;
			break;
		}

		*pair = xstrdup(tok);
	}
	xfree(copy);

	return rc;
}

/*
 * Shared implementation for job_submit() and job_modify().
 *
 * IN/OUT job_desc - job being submitted or updated; node count and TRES
 *	request are filled in here on success
 * IN job_ptr - existing job record for job_modify(), or NULL for job_submit()
 * IN pair - the "<family>=<shape>" pair of --layout that requests TPUs
 * OUT err_msg - user facing explanation on failure
 */
static int _apply_layout(job_desc_msg_t *job_desc, job_record_t *job_ptr,
			 const char *pair, char **err_msg)
{
	tpu_type_entry_t *entry;
	tpu_geometry_t geom;
	char *family = NULL, *err = NULL, *names = NULL;
	uint32_t nodes = 0;
	int rc = ESLURM_INVALID_LAYOUT;

	if (!tpu_layout_parse(pair, &family, &geom)) {
		xstrfmtcat(
			err,
			"Malformed --layout '%s': expected <family>=<x>x<y>[x<z>] with positive dimensions",
			pair);
		goto fail;
	}

	if (!(entry = tpu_conf_find_type(tpu_conf, family))) {
		names = tpu_conf_type_list_str(tpu_conf);
		xstrfmtcat(
			err,
			"Unknown accelerator family '%s'. Available families: %s",
			family, names);
		xfree(names);
		goto fail;
	}

	if (tpu_layout_validate(entry, &geom, &nodes, &err))
		goto fail;

	if ((job_desc->min_nodes != NO_VAL) && (job_desc->min_nodes != nodes)) {
		xstrfmtcat(
			err,
			"Layout %s needs exactly %u nodes, but %u were requested. Omit -N and let the layout size the job",
			pair, nodes, job_desc->min_nodes);
		rc = ESLURM_INVALID_NODE_COUNT;
		goto fail;
	}

	if (xstrstr(job_desc->tres_per_node, "gres/tpu")) {
		xstrfmtcat(
			err,
			"--layout already requests TPUs; drop the gres/tpu in '%s'",
			job_desc->tres_per_node);
		rc = ESLURM_INVALID_GRES;
		goto fail;
	}

	job_desc->min_nodes = job_desc->max_nodes = nodes;

	if (!job_desc->tres_per_node && job_ptr && job_ptr->tres_per_node)
		job_desc->tres_per_node =
			_copy_non_tpu_tres(job_ptr->tres_per_node);

	/*
	 * Append rather than overwrite: the job may legitimately be asking for
	 * other generic resources at the same time.
	 */
	xstrfmtcat(job_desc->tres_per_node, "%sgres/tpu:%s:%u",
		   job_desc->tres_per_node ? "," : "", entry->type,
		   entry->chips_per_node);

	xfree(family);

	return SLURM_SUCCESS;

fail:
	_set_err_msg(err_msg, err);
	xfree(family);

	return rc;
}

extern int job_submit(slurm_msg_t *msg, job_desc_msg_t *job_desc,
		      char **err_msg)
{
	char *pair = NULL, *err = NULL;
	int rc;

	if ((rc = _find_tpu_pair(job_desc->layout, &pair, &err)))
		_set_err_msg(err_msg, err);
	else if (pair)
		rc = _apply_layout(job_desc, NULL, pair, err_msg);

	xfree(pair);

	return rc;
}

extern int job_modify(slurm_msg_t *msg, job_desc_msg_t *job_desc,
		      job_record_t *job_ptr, char **err_msg)
{
	char *new_pair = NULL, *old_pair = NULL, *err = NULL;
	int rc;

	if (!job_desc->layout)
		return SLURM_SUCCESS;

	if ((rc = _find_tpu_pair(job_desc->layout, &new_pair, &err))) {
		_set_err_msg(err_msg, err);
		return rc;
	}

	/*
	 * The stored layout was checked when it was set, so an error here only
	 * means tpu.yaml changed since then. The new layout alone decides what
	 * happens next.
	 */
	(void) _find_tpu_pair(job_ptr->layout, &old_pair, &err);
	xfree(err);

	/* Pairs for other plugins may change at any time */
	if (!xstrcmp(new_pair, old_pair)) {
		rc = SLURM_SUCCESS;
		goto cleanup;
	}

	/*
	 * Changing the TPU request of a running job would leave the allocation
	 * it already holds inconsistent with the geometry it claims.
	 */
	if (!IS_JOB_PENDING(job_ptr)) {
		_set_err_msg(
			err_msg,
			xstrdup("Cannot change --layout of a job that is no longer pending"));
		rc = ESLURM_JOB_NOT_PENDING;
		goto cleanup;
	}

	if (!new_pair) {
		if (!job_desc->tres_per_node && job_ptr->tres_per_node) {
			char *non_tpu =
				_copy_non_tpu_tres(job_ptr->tres_per_node);
			job_desc->tres_per_node =
				non_tpu ? non_tpu : xstrdup("");
		}
		rc = SLURM_SUCCESS;
		goto cleanup;
	}

	rc = _apply_layout(job_desc, job_ptr, new_pair, err_msg);

cleanup:
	xfree(new_pair);
	xfree(old_pair);

	return rc;
}
