/*****************************************************************************\
 *  kv.h - arbitrary JSON metadata documents attached to Slurm objects
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

#ifndef _KV_H
#define _KV_H

#include "slurm/slurm.h"

/*
 * Return the option name for a KV operation.
 *
 * IN op - operation to describe
 * RET static string: "KV" for KV_OP_SET, "KVPatch" for KV_OP_PATCH, or
 *	"INVALID" for any other operation
 */
extern const char *kv_op_to_string(kv_op_t op);

/*
 * Parse a KV option name, ignoring case and allowing KVPatch abbreviations.
 *
 * IN tag - option name, which may be followed by '=' and a value
 * IN taglen - length of the option name
 * RET KV_OP_SET for KV, KV_OP_PATCH for KVP through KVPatch, or KV_OP_NONE
 *	when the name is not a KV option
 */
extern kv_op_t kv_string_to_op(const char *tag, size_t taglen);

/*
 * Parse the JSON text a user gave for a KV option.
 *
 * Check the type before sending the request to the controller:
 * KV must be a JSON object, and KVPatch must be a JSON array.
 *
 * IN op - what str is: a document for KV_OP_SET, a patch for KV_OP_PATCH
 * IN str - JSON text. An empty string with KV_OP_SET clears the document,
 *	which leaves *dst NULL.
 * OUT dst - ptr to NULL, set to the parsed tree. Untouched on failure.
 * IN/OUT err_msg - optional ptr to an xmalloc()ed string (caller xfree()s).
 *	May point to NULL. A user-facing reason is appended on failure.
 *	Untouched on success.
 * RET SLURM_SUCCESS
 *	ESLURM_KV_INVALID - str is not valid JSON, or op is not supported
 *	ESLURM_KV_NOT_DICT - KV is not a JSON object
 *	ESLURM_JSON_PATCH_INVALID - KVPatch is not a JSON array
 */
extern int kv_parse(kv_op_t op, const char *str, data_t **dst, char **err_msg);

/*
 * Parse a document read back from storage, such as the accounting database.
 * The database hands the document back reformatted, and it was checked when
 * it was stored.
 *
 * IN str - JSON text, or NULL for no document
 * OUT dst - ptr to NULL, set to the parsed document. Left NULL when str is
 *	NULL or on failure.
 * RET SLURM_SUCCESS
 *	ESLURM_KV_NOT_DICT - str is not a JSON object
 *	or the error from the JSON serializer
 */
extern int kv_from_string(const char *str, data_t **dst);

/*
 * Serialize a document as compact JSON for display or storage.
 *
 * IN kv - document. NULL, null, and an empty object all mean no document.
 * RET xmalloc()ed JSON text (caller must xfree()), or NULL when there is no
 *	document or it cannot be serialized
 */
extern char *kv_to_string(const data_t *kv);

/*
 * Apply a KV operation to an object's stored document.
 *
 * The destination document is updated only after the whole operation
 * succeeds. A rejected update leaves it unchanged. An empty result is
 * stored as NULL.
 *
 * KV_OP_PATCH is applied by serializer/json-patch, which must be loaded.
 * If src_kv aliases *dst_kv, PATCH is applied to an empty object and the
 * patch is replaced with its result.
 *
 * IN src_kv - source document for KV_OP_SET, or patch for KV_OP_PATCH.
 *	NULL or null with either operation clears the destination document.
 * IN src_kv_op - operation to apply: KV_OP_SET or KV_OP_PATCH. KV_OP_NONE
 *	succeeds without changing the destination; any other operation is refused.
 * IN/OUT dst_kv - ptr to the destination document (may point to NULL).
 *	Replaced on success, or freed and set to NULL when the result is empty.
 * IN/OUT dst_kv_op - optional operation ptr. Set to KV_OP_SET for a nonempty
 *	result, or KV_OP_NONE for an empty result, including on
 *	SLURM_NO_CHANGE_IN_DATA. Untouched for KV_OP_NONE or on failure.
 * IN/OUT err_msg - optional ptr to an xmalloc()ed string (caller xfree()s).
 *	May point to NULL. A user-facing reason is appended on failure.
 *	Untouched on SLURM_SUCCESS or SLURM_NO_CHANGE_IN_DATA.
 * RET SLURM_SUCCESS for SET or NONE, or a PATCH that is NULL, null, empty,
 *	or changes the document
 *	SLURM_NO_CHANGE_IN_DATA - a nonempty PATCH leaves the document unchanged
 *	ESLURM_KV_INVALID - invalid request or document
 *	ESLURM_KV_NOT_DICT - the result would not be a JSON object
 *	or an ESLURM_JSON_* code from the patch itself
 */
extern int kv_op_apply(const data_t *src_kv, const kv_op_t src_kv_op,
		       data_t **dst_kv, kv_op_t *dst_kv_op, char **err_msg);

#endif /* _KV_H */
