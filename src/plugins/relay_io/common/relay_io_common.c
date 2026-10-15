/*****************************************************************************\
 *  relay_io_common.c - record schema shared by relay_io plugins
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

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/sluid.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xutf.h"

#include "relay_io_common.h"

/* serializer will truncate at the first '\0' char so they need to be escaped */
#define NULL_ESCAPE_LEN 6
#define NULL_ESCAPE_SEQUENCE "\\u0000"

/* bytes U+FFFD takes in UTF-8, which is what ill formed input is replaced by */
#define UTF_REPLACEMENT_LEN 3

static const char *_event_str(relay_io_event_t type)
{
	switch (type) {
	case RELAY_IO_EVENT_LOG:
		return "log";
	case RELAY_IO_EVENT_JOB_START:
		return "job_start";
	case RELAY_IO_EVENT_JOB_END:
		return "job_end";
	case RELAY_IO_EVENT_STEP_START:
		return "step_start";
	case RELAY_IO_EVENT_STEP_END:
		return "step_end";
	}

	return "unknown";
}

/*
 * Render a step id the way the rest of Slurm prints it, but always as a
 * string. Keeping one JSON type for the field means a consumer never has to
 * cope with "batch" and 0 arriving under the same key.
 */
static char *_step_id_str(uint32_t step_id)
{
	switch (step_id) {
	case SLURM_BATCH_SCRIPT:
		return xstrdup("batch");
	case SLURM_EXTERN_CONT:
		return xstrdup("extern");
	case SLURM_INTERACTIVE_STEP:
		return xstrdup("interactive");
	default:
		return xstrdup_printf("%u", step_id);
	}
}

/*
 * Convert to utf8 valid characters and escape NULLs to allow string operations
 * IN record - The record containing the body to be sent (not null terminated)
 * IN limit - Maximum bytes that can be in the returned string
 *            (min value NULL_ESCAPE_LEN)
 * OUT consumed_ptr - Number of bytes from record->body used in the return
 * RET Null terminated string with valid utf8 encoding
 */
static char *_null_escape_utf8(const relay_io_record_t *record, size_t limit,
			       size_t *consumed_ptr)
{
	const utf8_t *body = (const utf8_t *) record->body;
	const utf8_t *body_ptr = body;
	const utf8_t *body_end = body + record->body_len;
	utf8_t *cleaned_body = NULL;
	size_t cleaned_body_len;
	size_t new_len = 0;
	utf8_t *out;

	/* Need to at least fit one character */
	limit = MAX(limit, NULL_ESCAPE_LEN);

	cleaned_body_len = MIN((record->body_len * NULL_ESCAPE_LEN), limit);
	cleaned_body = out = xmalloc(cleaned_body_len + 1);

	while (body_ptr < body_end) {
		if (*body_ptr == (utf8_t) '\0') {
			if ((new_len + NULL_ESCAPE_LEN) > cleaned_body_len)
				break;
			memcpy(out, NULL_ESCAPE_SEQUENCE, NULL_ESCAPE_LEN);
			out += NULL_ESCAPE_LEN;
			new_len += NULL_ESCAPE_LEN;
			body_ptr++;
		} else {
			utf_code_t utf = 0;
			int bytes = 0, utf_len = 0;

			/*
			 * An ill formed sequence, or one Slurm does not
			 * accept, comes back as U+FFFD along with the bytes
			 * it spans.
			 */
			if (utf8_read_character(body_ptr, body_end, &utf,
						&bytes, true))
				utf_len = UTF_REPLACEMENT_LEN;
			else
				utf_len = bytes;

			if ((new_len + utf_len) > cleaned_body_len)
				break;

			if (utf8_write_character(utf, out, &utf_len))
				break;

			out += utf_len;
			new_len += utf_len;
			body_ptr += bytes;
		}
	}
	*out = '\0';

	*consumed_ptr = body_ptr - body;

	return (char *) cleaned_body;
}

/*
 * '\0' is escaped and invalid utf8 is replaced. body_limit counts bytes of the
 * sanitized body, not of record->body. The body is cut on a character boundary
 * at or below it, and a cut record is marked partial so that a consumer joins
 * it to the next one. A limit below NULL_ESCAPE_LEN is raised to it, so that
 * at least one escaped character always fits. The serializer's JSON escaping
 * can still grow the body, so a plugin that needs an exact fit measures the
 * result and asks again with a smaller limit.
 */
extern void relay_io_record_pack_data(const relay_io_record_t *record,
				      data_t *dst, size_t body_limit,
				      size_t *consumed_ptr)
{
	data_t *slurm = NULL;
	char *str = NULL;

	size_t consumed = 0;

	xassert(record);
	xassert(dst);

	if (consumed_ptr)
		*consumed_ptr = 0;

	data_set_dict(dst);

	data_set_int(data_key_set(dst, "v"), RELAY_IO_SCHEMA_VERSION);
	data_set_string(data_key_set(dst, "type"), _event_str(record->type));

	/*
	 * A raw count rather than a formatted stamp: this runs for every line
	 * of every relayed job, and localtime_r() plus strftime() plus an
	 * allocation per record is real work to hand a consumer that is going
	 * to parse it straight back into a number.
	 */
	data_set_int(data_key_set(dst, "time"),
		     (((int64_t) record->time.tv_sec * NSEC_IN_SEC) +
		      record->time.tv_nsec));

	slurm = data_set_dict(data_key_set(dst, "slurm"));
	data_set_string(data_key_set(slurm, "cluster"), record->ident.cluster);
	data_set_string(data_key_set(slurm, "node"), record->ident.node);
	data_set_int(data_key_set(slurm, "job_id"), record->ident.job_id);

	str = sluid2str(record->ident.sluid);
	if (!data_set_string_own(data_key_set(slurm, "sluid"), str))
		xfree(str);

	/*
	 * These come from the credential and any of them can be absent, so
	 * leave the key out rather than emit a null a consumer has to handle.
	 */
	if (record->ident.user_name)
		data_set_string(data_key_set(slurm, "user_name"),
				record->ident.user_name);

	/* job_start and job_end describe the whole job, not one step */
	if ((record->type == RELAY_IO_EVENT_JOB_START) ||
	    (record->type == RELAY_IO_EVENT_JOB_END)) {
		/*
		 * Only meaningful for the job as a whole, so they are not
		 * repeated on every line of its output.
		 */
		data_set_int(data_key_set(slurm, "restart_count"),
			     record->ident.restart_count);
		if (record->ident.partition)
			data_set_string(data_key_set(slurm, "partition"),
					record->ident.partition);
		if (record->ident.account)
			data_set_string(data_key_set(slurm, "account"),
					record->ident.account);
		return;
	}

	str = _step_id_str(record->step_id);
	if (!data_set_string_own(data_key_set(slurm, "step_id"), str))
		xfree(str);

	if (record->type != RELAY_IO_EVENT_LOG)
		return;

	data_set_int(data_key_set(slurm, "task_id_global"),
		     record->task_id_global);
	data_set_int(data_key_set(slurm, "task_id_local"),
		     record->task_id_local);

	data_set_string(data_key_set(dst, "stream"),
			((record->stream == RELAY_IO_STREAM_STDERR) ?
				 "stderr" :
				 "stdout"));

	str = _null_escape_utf8(record, body_limit, &consumed);

	if (!data_set_string_own(data_key_set(dst, "body"), str))
		xfree(str);

	/* these two are only present when true, to keep records small */
	if (record->partial ||
	    /*
	     * A body cut short to fit does not end at a line boundary, which is
	     * all partial claims. Unbuffered records are exempt: they are cut
	     * at an arbitrary offset by definition, which "unbuffered" already
	     * says, so partial would add nothing a consumer can act on.
	     */
	    (!record->unbuffered && (consumed < record->body_len)))
		data_set_bool(data_key_set(dst, "partial"), true);
	if (record->unbuffered)
		data_set_bool(data_key_set(dst, "unbuffered"), true);

	if (consumed_ptr)
		*consumed_ptr = consumed;
}
