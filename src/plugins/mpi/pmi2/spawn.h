/*****************************************************************************\
 **  spawn.c - PMI job spawn handling
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#ifndef _SPAWN_H
#define _SPAWN_H

#include <inttypes.h>

#include "src/common/pack.h"

typedef struct spawn_subcmd {
	char *cmd;
	uint32_t max_procs;
	uint32_t argc;
	char **argv;
	uint32_t info_cnt;
	char **info_keys;
	char **info_vals;
} spawn_subcmd_t;

typedef struct spawn_req {
	uint32_t seq;
	char *from_node;
	uint32_t subcmd_cnt;
	uint32_t preput_cnt;
	char **pp_keys;
	char **pp_vals;
	spawn_subcmd_t **subcmds;
	/* TODO: Slurm specific job control info */
} spawn_req_t;

typedef struct spawn_resp {
	uint32_t seq;
	int rc;
	char *jobid;
	uint16_t pmi_port;
	uint32_t error_cnt;
	int *error_codes;
} spawn_resp_t;

extern spawn_subcmd_t *spawn_subcmd_new(void);
extern void spawn_subcmd_free(spawn_subcmd_t *subcmd);
extern spawn_req_t *spawn_req_new(void);
extern void spawn_req_free(spawn_req_t *req);
extern void spawn_req_pack(spawn_req_t *req, Buf buf);
extern int  spawn_req_unpack(spawn_req_t **req_ptr, Buf buf);
extern int  spawn_req_send_to_srun(spawn_req_t *req, spawn_resp_t **resp_ptr);

extern spawn_resp_t *spawn_resp_new(void);
extern void spawn_resp_free(spawn_resp_t *resp);
extern void spawn_resp_pack(spawn_resp_t *resp, Buf buf);
extern int  spawn_resp_unpack(spawn_resp_t **resp_ptr, Buf buf);
extern int  spawn_resp_send_to_stepd(spawn_resp_t *resp, char *node);
extern int  spawn_resp_send_to_fd(spawn_resp_t *resp, int fd);
extern int  spawn_resp_send_to_srun(spawn_resp_t *resp);

extern int  spawn_psr_enqueue(uint32_t seq, int fd, int lrank,
			      char *from_node);
extern int  spawn_psr_dequeue(uint32_t seq, int *fd, int *lrank,
			      char **from_node);

extern uint32_t spawn_seq_next(void);

extern int  spawn_job_do_spawn(spawn_req_t *req);
extern void spawn_job_wait(void);


#endif	/* _SPAWN_H */
