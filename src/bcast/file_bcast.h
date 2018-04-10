/****************************************************************************\
 *  file_bcast.h - definitions used for file broadcast functions
 *****************************************************************************
 *  Copyright (C) 2015-2016 SchedMD LLC.
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
\****************************************************************************/

#ifndef _FILE_BCAST_H
#define _FILE_BCAST_H

#include "slurm/slurm.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"

struct bcast_parameters {
	uint32_t block_size;
	uint16_t compress;
	char *dst_fname;
	int fanout;
	bool force;
	uint32_t job_id;		/* Job ID or Pack Job ID */
	uint32_t pack_job_offset;	/* Pack Job Offset or NO_VAL */
	bool preserve;
	char *src_fname;
	uint32_t step_id;
	int timeout;
	int verbose;
};

typedef struct file_bcast_info {
	void *data;		/* mmap of file data */
	int fd;			/* file descriptor */
	uint64_t file_size;	/* file size */
	char *fname;		/* filename */
	gid_t gid;		/* gid of owner */
	uint32_t job_id;	/* job id */
	time_t last_update;	/* transfer last block received */
	int received_blocks;	/* number of blocks received */
	time_t start_time;	/* transfer start time */
	uid_t uid;		/* uid of owner */
} file_bcast_info_t;

extern int bcast_file(struct bcast_parameters *params);

extern int bcast_decompress_data(file_bcast_msg_t *req);

#endif
