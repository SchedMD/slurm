/*****************************************************************************\
 *  trigger_mgr.h - header to manager event triggers
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _HAVE_TRIGGERS_H
#define _HAVE_TRIGGERS_H

#include <unistd.h>
#include <sys/types.h>
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmctld/slurmctld.h"


/* User RPC processing to set, get, clear, and pull triggers */
extern int trigger_clear(uid_t uid, trigger_info_msg_t *msg);
extern trigger_info_msg_t * trigger_get(uid_t uid, trigger_info_msg_t *msg);
extern int trigger_set(uid_t uid, gid_t gid, trigger_info_msg_t *msg);
extern int trigger_pull(trigger_info_msg_t *msg);

/* Note the some event has occured and flag triggers as needed */
extern void trigger_block_error(void);
extern void trigger_front_end_down(front_end_record_t *front_end_ptr);
extern void trigger_front_end_up(front_end_record_t *front_end_ptr);
extern void trigger_node_down(struct node_record *node_ptr);
extern void trigger_node_drained(struct node_record *node_ptr);
extern void trigger_node_failing(struct node_record *node_ptr);
extern void trigger_node_up(struct node_record *node_ptr);
extern void trigger_reconfig(void);
extern void trigger_primary_ctld_fail(void);
extern void trigger_primary_ctld_res_op(void);
extern void trigger_primary_ctld_res_ctrl(void);
extern void trigger_primary_ctld_acct_full(void);
extern void trigger_backup_ctld_fail(void);
extern void trigger_backup_ctld_res_op(void);
extern void trigger_backup_ctld_as_ctrl(void);
extern void trigger_primary_dbd_fail(void);
extern void trigger_primary_dbd_res_op(void);
extern void trigger_primary_db_fail(void);
extern void trigger_primary_db_res_op(void);

/* Save and restore state for slurmctld fail-over or restart */
extern int  trigger_state_save(void);
extern void trigger_state_restore(void);

/* Free all allocated memory */
extern void trigger_fini(void);

/* Execute programs as needed for triggers that have been pulled
 * and purge any vestigial trigger records */
extern void trigger_process(void);

#endif /* !_HAVE_TRIGGERS_H */
