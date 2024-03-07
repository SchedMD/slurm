/*****************************************************************************\
 *  cred_context.h
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#ifndef _CRED_CONTEXT_H
#define _CRED_CONTEXT_H

extern void cred_state_init(void);
extern void cred_state_fini(void);

extern void save_cred_state(void);

extern bool cred_jobid_cached(uint32_t jobid);

/*
 * Add a jobid to the credential context without a credential state.
 * This is used by the verifier to track job ids that it has seen,
 * but not necessarily received a credential for.
 * E.g., if the prolog or batch were the first related RPCS to be processed.
 */
extern int cred_insert_jobid(uint32_t jobid);

extern int cred_revoke(uint32_t jobid, time_t time, time_t start_time);

extern bool cred_revoked(slurm_cred_t *cred);

/*
 * Begin expiration period for the revocation of credentials for jobid.
 * This should be run after slurm_cred_revoke().
 * This function is used because we may want to revoke credentials for a jobid,
 * but not purge the revocation from memory until after some other action has
 * occurred, e.g. completion of a job epilog.
 *
 * Returns 0 for success, SLURM_ERROR for failure with errno set to:
 *
 *  ESRCH  if jobid is not cached
 *  EEXIST if expiration period has already begun for jobid.
 *
 */
extern int cred_begin_expiration(uint32_t jobid);

/*
 * Check to see if this credential is a reissue of an existing credential
 * (this can happen, for instance, with "scontrol restart").
 * If this credential is a reissue, then the old credential is cleared
 * from the cred context.
 */
extern void cred_handle_reissue(slurm_cred_t *cred, bool locked);

extern bool cred_cache_valid(slurm_cred_t *cred);

#endif
