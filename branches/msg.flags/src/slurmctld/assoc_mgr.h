/*****************************************************************************\
 *  assoc_mgr.h - keep track of local cache of accounting data.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifndef _SLURMCTLD_ASSOC_MGR_H 
#define _SLURMCTLD_ASSOC_MGR_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/list.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/slurmctld.h"
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

/* 
 * get info from the storage 
 * IN/OUT:  acct_user - acct_user_rec_t with the name set of the user.
 *                      "default_account" will be filled in on
 *                      successful return DO NOT FREE.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int get_default_account(void *db_conn, acct_user_rec_t *user);

/* 
 * get info from the storage 
 * IN/OUT:  acct_assoc - acct_association_rec_t with at least cluster and
 *			account set for account association.  To get user
 *			association set user, and optional partition.
 *			Sets "id" field with the association ID.
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int get_assoc_id(void *db_conn, acct_association_rec_t *assoc);

extern int assoc_mgr_init(void *db_conn);
extern int assoc_mgr_fini();

/* 
 * remove association from local cache 
 * IN:  uint32_t id (id of association to remove)
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int remove_local_association(uint32_t id);

/* 
 * remove user from local cache 
 * IN:  char * name (name of user to remove this will also remove all
 *      associations for this user)
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int remove_local_user(char *name);

/* 
 * update associations in local cache 
 * IN:  List of acct_association_rec_t's
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int update_local_associations(List update_list);

/* 
 * update users in local cache 
 * IN:  List of acct_user_rec_t's
 * RET: SLURM_SUCCESS on success (or not found) SLURM_ERROR else
 */
extern int update_local_users(List update_list);

/* 
 * validate that an association ID is still avlid 
 * IN:  assoc_id - association ID previously returned by 
 *		get_assoc_id(void *db_conn, 
 )
 * RET: SLURM_SUCCESS on success SLURM_ERROR else
 */
extern int validate_assoc_id(void *db_conn, uint32_t assoc_id);

#endif /* _SLURMCTLD_ASSOC_MGR_H */
