/*****************************************************************************\
 *  write_labelled_message.c - write a message with an optional label
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov> and
 *  David Bremer <dbremer@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "src/common/write_labelled_message.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static char *_build_label(int task_id, int task_id_width, uint32_t pack_offset,
			  uint32_t task_offset);
static int _write_line(int fd, char *prefix, char *suffix, void *buf, int len);

/*
 * fd             is the file descriptor to write to
 * buf            is the char buffer to write
 * len            is the buffer length in bytes
 * task_id        is will be used in the label
 * pack_offset    is the offset within a pack-job or NO_VAL
 * task_offset    is the task offset within a pack-job or NO_VAL
 * label          if true, prepend each line of the buffer with a
 *                label for the task id
 * task_id_width  is the number of digits to use for the task id
 *
 * Write as many lines from the message as possible.  Return
 * the number of bytes from the message that have been written,
 * or -1 on error.  If len==0, -1 will be returned.
 *
 * If the message ends in a partial line (line does not end
 * in a '\n'), then add a newline to the output file, but only
 * in label mode.
 */
extern int write_labelled_message(int fd, void *buf, int len, int task_id,
				  uint32_t pack_offset, uint32_t task_offset,
				  bool label, int task_id_width)
{
	void *start, *end;
	char *prefix = NULL, *suffix = NULL;
	int remaining = len;
	int written = 0;
	int line_len;
	int rc = -1;

	if (label) {
		prefix = _build_label(task_id, task_id_width, pack_offset,
				      task_offset);
	}

	while (remaining > 0) {
		start = buf + written;
		end = memchr(start, '\n', remaining);
		if (end == NULL) { /* no newline found */
			if (label)
				suffix = "\n";
			rc = _write_line(fd, prefix, suffix, start, remaining);
			if (rc <= 0) {
				goto done;
			} else {
				remaining -= rc;
				written += rc;
			}
		} else {
			line_len = (int)(end - start) + 1;
			rc = _write_line(fd, prefix, suffix, start, line_len);
			if (rc <= 0) {
				goto done;
			} else {
				remaining -= rc;
				written += rc;
			}
		}

	}
done:
	xfree(prefix);
	if (written > 0)
		return written;
	else
		return rc;
}

/*
 * Build line label. Call xfree() to release returned memory
 */
static char *_build_label(int task_id, int task_id_width,
			  uint32_t pack_offset, uint32_t task_offset)
{
	char *buf = NULL;

	if (pack_offset != NO_VAL) {
		if (task_offset != NO_VAL) {
			xstrfmtcat(buf, "%*d: ", task_id_width,
				   (task_id + task_offset));
		} else {
			xstrfmtcat(buf, "P%u %*d: ", pack_offset, task_id_width,
				   task_id);
		}
	} else {
		xstrfmtcat(buf, "%*d: ", task_id_width, task_id);
	}

	return buf;
}

/*
 * Blocks until write is complete, regardless of the file descriptor being in
 * non-blocking mode.
 * I/O from multiple pack-jobs may be present, so add prefix/suffix to buffer
 * before issuing write to avoid interleaved output from multiple components.
 */
static int _write_line(int fd, char *prefix, char *suffix, void *buf, int len)
{
	int left, n, pre = 0, post = 0;
	void *ptr, *tmp = NULL;

	if (prefix || suffix) {
		if (prefix)
			pre = strlen(prefix);
		if (suffix)
			post = strlen(suffix);
		tmp = xmalloc(pre + len + post);
		if (prefix)
			memcpy(tmp, prefix, pre);
		memcpy(tmp + pre, buf, len);
		if (suffix)
			memcpy(tmp + pre + len, suffix, post);
		ptr = tmp;
		left = pre + len + post;
	} else {
		ptr = buf;
		left = len;
	}

	while (left > 0) {
	again:
		if ((n = write(fd, ptr, left)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug3("  got EAGAIN in _write_line");
				goto again;
			}
			len = -1;
			break;
		}
		left -= n;
		ptr += n;
	}
	xfree(tmp);

	return len;
}
