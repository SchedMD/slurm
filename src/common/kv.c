/*****************************************************************************\
 *  kv.c - arbitrary JSON metadata documents attached to Slurm objects
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

#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/data.h"
#include "src/common/kv.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

strong_alias(kv_from_string, slurm_kv_from_string);
strong_alias(kv_op_apply, slurm_kv_op_apply);
strong_alias(kv_op_to_string, slurm_kv_op_to_string);
strong_alias(kv_parse, slurm_kv_parse);
strong_alias(kv_string_to_op, slurm_kv_string_to_op);
strong_alias(kv_to_string, slurm_kv_to_string);

/* Keep hook notices without hiding a later KV failure. */
#define SET_ERR(err_msg, fmt, ...) \
	do { \
		if (err_msg) { \
			if (*(err_msg)) \
				xstrcat(*(err_msg), "\n"); \
			xstrfmtcat(*(err_msg), fmt, ##__VA_ARGS__); \
		} \
	} while (false)

#define T(_op, _str, _min_len) \
	{ .op = KV_OP_##_op, .str = _str, .min_len = _min_len }

static const struct {
	kv_op_t op;
	const char *str;
	size_t min_len;
} kv_ops[] = {
	T(SET, "KV", 2),
	T(PATCH, "KVPatch", 3),
};

#undef T

extern const char *kv_op_to_string(kv_op_t op)
{
	for (size_t i = 0; i < ARRAY_SIZE(kv_ops); i++)
		if (kv_ops[i].op == op)
			return kv_ops[i].str;

	return "INVALID";
}

extern kv_op_t kv_string_to_op(const char *tag, size_t taglen)
{
	for (size_t i = 0; i < ARRAY_SIZE(kv_ops); i++)
		if (!xstrncasecmp(tag, kv_ops[i].str,
				  MAX(taglen, kv_ops[i].min_len)))
			return kv_ops[i].op;

	return KV_OP_NONE;
}

/* RET true when kv holds no document: NULL, null or an empty object */
static bool _is_empty(const data_t *kv)
{
	if (!kv)
		return true;

	switch (data_get_type(kv)) {
	case DATA_TYPE_NULL:
		return true;
	case DATA_TYPE_DICT:
		return !data_get_dict_length(kv);
	default:
		return false;
	}
}

/*
 * Serialize a tree as compact JSON.
 *
 * Serializers only read src but do not declare it const.
 *
 * OUT dst - ptr to NULL, set with the JSON text (caller xfree()s)
 * OUT len - length of *dst
 * RET SLURM_SUCCESS or error
 */
static int _dump(const data_t *src, char **dst, size_t *len)
{
	xassert(dst && !*dst);

	return serialize_g_data_to_string(dst, len, (data_t *) src,
					  MIME_TYPE_JSON, SER_FLAGS_COMPACT);
}

extern int kv_parse(kv_op_t op, const char *str, data_t **dst, char **err_msg)
{
	const char *what = kv_op_to_string(op);
	data_t *parsed = NULL;
	int rc;

	xassert(dst && !*dst);

	if (((op != KV_OP_SET) && (op != KV_OP_PATCH)) || !str) {
		SET_ERR(err_msg, "Invalid KV update request");
		return ESLURM_KV_INVALID;
	}

	/* An empty KV= clears the document, as with other string options. */
	if ((op == KV_OP_SET) && !str[0])
		return SLURM_SUCCESS;

	serializer_required(MIME_TYPE_JSON);

	if ((rc = serialize_g_string_to_data(&parsed, str, strlen(str),
					     MIME_TYPE_JSON))) {
		SET_ERR(err_msg, "%s is not valid JSON", what);
		FREE_NULL_DATA(parsed);
		return ESLURM_KV_INVALID;
	}

	if ((op == KV_OP_PATCH) && (data_get_type(parsed) != DATA_TYPE_LIST)) {
		SET_ERR(err_msg,
			"%s must be a JSON array of operations, not %s", what,
			data_get_type_string(parsed));
		rc = ESLURM_JSON_PATCH_INVALID;
	} else if ((op == KV_OP_SET) &&
		   (data_get_type(parsed) != DATA_TYPE_DICT)) {
		SET_ERR(err_msg, "%s must be a JSON object, not %s", what,
			data_get_type_string(parsed));
		rc = ESLURM_KV_NOT_DICT;
	}

	if (rc)
		FREE_NULL_DATA(parsed);
	else
		*dst = parsed;

	return rc;
}

extern int kv_from_string(const char *str, data_t **dst)
{
	data_t *parsed = NULL;
	int rc;

	xassert(dst && !*dst);

	if (!str || !str[0])
		return SLURM_SUCCESS;

	serializer_required(MIME_TYPE_JSON);

	if ((rc = serialize_g_string_to_data(&parsed, str, strlen(str),
					     MIME_TYPE_JSON))) {
		FREE_NULL_DATA(parsed);
		return rc;
	}

	if (data_get_type(parsed) != DATA_TYPE_DICT) {
		FREE_NULL_DATA(parsed);
		return ESLURM_KV_NOT_DICT;
	}

	*dst = parsed;

	return SLURM_SUCCESS;
}

extern char *kv_to_string(const data_t *kv)
{
	char *str = NULL;
	int rc;

	if (_is_empty(kv))
		return NULL;

	serializer_required(MIME_TYPE_JSON);

	if ((rc = _dump(kv, &str, NULL))) {
		error("%s: Unable to serialize KV document: %s",
		      __func__, slurm_strerror(rc));
		xfree(str);
	}

	return str;
}

/*
 * Apply an RFC 6902 JSON Patch through serializer/json-patch.
 *
 * The plugin reads the patch as JSON text, so the operand goes back through
 * the JSON serializer first. The plugin leaves doc untouched unless every
 * operation applies, and reports SLURM_NO_CHANGE_IN_DATA when a nonempty
 * patch leaves the document unchanged.
 */
static int _json_patch(data_t **doc, const data_t *patch, char **err_msg)
{
	char *str = NULL;
	size_t len = 0;
	int rc;

	if ((rc = _dump(patch, &str, &len))) {
		SET_ERR(err_msg, "Unable to serialize KVPatch: %s",
			slurm_strerror(rc));
		return rc;
	}

	rc = serialize_g_string_to_data(doc, str, len, MIME_TYPE_JSON_PATCH);
	if (rc && (rc != SLURM_NO_CHANGE_IN_DATA))
		SET_ERR(err_msg, "Unable to apply KVPatch: %s",
			slurm_strerror(rc));

	xfree(str);

	return rc;
}

extern int kv_op_apply(const data_t *src_kv, const kv_op_t src_kv_op,
		       data_t **dst_kv, kv_op_t *dst_kv_op, char **err_msg)
{
	data_t *doc = NULL;
	int rc = SLURM_SUCCESS;

	xassert(dst_kv);

	if (src_kv_op == KV_OP_NONE)
		return SLURM_SUCCESS;

	if ((src_kv_op != KV_OP_SET) && (src_kv_op != KV_OP_PATCH)) {
		SET_ERR(err_msg, "Invalid KV update request");
		return ESLURM_KV_INVALID;
	}

	/* A NULL or null source clears the document for SET and PATCH. */
	if (!src_kv || (data_get_type(src_kv) == DATA_TYPE_NULL))
		goto store;

	/*
	 * A KVPatch is an array of operations. Validate the resulting document
	 * after applying the patch.
	 */
	if ((src_kv_op == KV_OP_PATCH) &&
	    (data_get_type(src_kv) != DATA_TYPE_LIST)) {
		SET_ERR(err_msg, "%s must be a JSON array of operations",
			kv_op_to_string(src_kv_op));
		return ESLURM_JSON_PATCH_INVALID;
	} else if ((src_kv_op == KV_OP_SET) &&
		   (data_get_type(src_kv) != DATA_TYPE_DICT)) {
		SET_ERR(err_msg, "%s must be a JSON object",
			kv_op_to_string(src_kv_op));
		return ESLURM_KV_NOT_DICT;
	}

	serializer_required(MIME_TYPE_JSON);
	if (src_kv_op == KV_OP_PATCH)
		serializer_required(MIME_TYPE_JSON_PATCH);

	/*
	 * Work on a copy so a rejected update leaves *dst_kv untouched. If no
	 * document exists yet, or the patch itself is in *dst_kv, start with
	 * an empty object.
	 */
	if (src_kv_op == KV_OP_SET)
		rc = data_clone(src_kv, doc);
	else if ((src_kv == *dst_kv) || _is_empty(*dst_kv))
		doc = data_set_dict(data_new());
	else
		rc = data_clone(*dst_kv, doc);

	if (rc) {
		SET_ERR(err_msg, "Unable to copy KV document: %s",
			slurm_strerror(rc));
		goto cleanup;
	}

	if (src_kv_op == KV_OP_PATCH) {
		rc = _json_patch(&doc, src_kv, err_msg);
		if (rc && (rc != SLURM_NO_CHANGE_IN_DATA))
			goto cleanup;
	}

	/*
	 * "replace" or "remove" against path "" can leave something that is
	 * not an object behind. The patch ran against a copy, so rejecting it
	 * here still costs nothing.
	 */
	if (data_get_type(doc) != DATA_TYPE_DICT) {
		SET_ERR(err_msg, "%s result must be a JSON object, not %s",
			kv_op_to_string(src_kv_op), data_get_type_string(doc));
		rc = ESLURM_KV_NOT_DICT;
		goto cleanup;
	}

	if (_is_empty(doc))
		FREE_NULL_DATA(doc);

store:
	FREE_NULL_DATA(*dst_kv);
	*dst_kv = doc;
	if (dst_kv_op)
		*dst_kv_op = doc ? KV_OP_SET : KV_OP_NONE;
	doc = NULL;

cleanup:
	FREE_NULL_DATA(doc);

	return rc;
}
