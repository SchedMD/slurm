/*****************************************************************************\
 *  write_labelled_message.h - write a message with an optional label
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bremer <dbremer@llnl.gov>
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

#ifndef _HAVE_WRITE_LABELLED_MESSAGE
#define _HAVE_WRITE_LABELLED_MESSAGE

#include "slurm/slurm.h"

/*
 * fd          is the file descriptor to write to
 * buf         is the char buffer to write
 * len         is the buffer length in bytes
 * taskid      is will be used in the label
 * label       if true, prepend each line of the buffer with a
 *               label for the task id
 * label_width is the number of digits to use for the task id
 *
 * Write as many lines from the message as possible.  Return
 * the number of bytes from the message that have been written,
 * or -1 on error.  If len==0, -1 will be returned.
 *
 * If the message ends in a partial line (line does not end
 * in a '\n'), then add a newline to the output file, but only
 * in label mode.
 */

int write_labelled_message(int fd, void *buf, int len, int taskid,
			   bool label, int label_width);

#endif
