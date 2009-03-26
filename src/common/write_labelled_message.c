/*****************************************************************************\
 *  write_labelled_message.c - write a message with an optional label
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov> and 
 *  David Bremer <dbremer@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "src/common/write_labelled_message.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"

static int _write_label(int fd, int taskid, int label_width);
static int _write_line(int fd, void *buf, int len);
static int _write_newline(int fd);



int write_labelled_message(int fd, void *buf, int len, int taskid,
			   bool label, int label_width)
{
	void *start;
	void *end;
	int remaining = len;
	int written = 0;
	int line_len;
	int rc = -1;

	while (remaining > 0) {
		start = buf + written;
		end = memchr(start, '\n', remaining);
		if (label)
			if (_write_label(fd, taskid, label_width)
			    != SLURM_SUCCESS)
				goto done;
		if (end == NULL) { /* no newline found */
			rc = _write_line(fd, start, remaining);
			if (rc <= 0) {
				goto done;
			} else {
				remaining -= rc;
				written += rc;
			}
			if (label)
				if (_write_newline(fd) != SLURM_SUCCESS)
					goto done;
		} else {
			line_len = (int)(end - start) + 1;
			rc = _write_line(fd, start, line_len);
			if (rc <= 0) {
				goto done;
			} else {
				remaining -= rc;
				written += rc;
			}
		}

	}
done:
	if (written > 0)
		return written;
	else
		return rc;
}


static int _write_label(int fd, int taskid, int label_width)
{
	int n;
	int left = label_width + 2;
	char buf[16];
	void *ptr = buf;

	snprintf(buf, 16, "%0*d: ", label_width, taskid);
	while (left > 0) {
	again:
		if ((n = write(fd, ptr, left)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug3("  got EAGAIN in _write_label");
				goto again;
			}
			error("In _write_label: %m");
			return SLURM_ERROR;
		}
		left -= n;
		ptr += n;
	}

	return SLURM_SUCCESS;
}


static int _write_newline(int fd)
{
	int n;

	debug2("Called _write_newline");
again:
	if ((n = write(fd, "\n", 1)) < 0) {
		if (errno == EINTR
		    || errno == EAGAIN
		    || errno == EWOULDBLOCK) {
			goto again;
		}
		error("In _write_newline: %m");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}


/*
 * Blocks until write is complete, regardless of the file
 * descriptor being in non-blocking mode.
 */
static int _write_line(int fd, void *buf, int len)
{
	int n;
	int left = len;
	void *ptr = buf;

	debug2("Called _write_line");
	while (left > 0) {
	again:
		if ((n = write(fd, ptr, left)) < 0) {
			if (errno == EINTR)
				goto again;
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				debug3("  got EAGAIN in _write_line");
				goto again;
			}
			return -1;
		}
		left -= n;
		ptr += n;
	}
	
	return len;
}
