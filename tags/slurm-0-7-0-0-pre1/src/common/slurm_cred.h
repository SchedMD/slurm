/*****************************************************************************\
 *  src/common/slurm_cred.h  - SLURM job credential operations
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _HAVE_SLURM_CRED_H
#define _HAVE_SLURM_CRED_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include "src/common/macros.h"
#include "src/common/pack.h"

/*
 * The incomplete slurm_cred_t type is also defined in slurm.h for
 * users of the api, so check to ensure that this header has not been
 * included after slurm.h:
 */
#ifndef __slurm_cred_t_defined
#  define __slurm_cred_t_defined
   typedef struct slurm_job_credential * slurm_cred_t;
#endif

/* 
 * The slurm_cred_ctx_t incomplete type
 */
typedef struct slurm_cred_context   * slurm_cred_ctx_t;


/*
 * Initialize current process for slurm credential creation.
 *
 * `privkey' contains the absolute path to the slurmctld private
 * key, which needs to be readable by the current process. 
 *
 * Returns 0 for success, -1 on failure and sets errno to reason.
 *
 *
 */
slurm_cred_ctx_t slurm_cred_creator_ctx_create(const char *privkey);

/*
 * Initialize current process for slurm credential verification.
 * `pubkey' contains the absolute path to the slurmctld public key.
 *
 * Returns 0 for success, -1 on failure.
 */
slurm_cred_ctx_t slurm_cred_verifier_ctx_create(const char *pubkey);

/*
 * Set and get credential context options
 *
 */
typedef enum {
	SLURM_CRED_OPT_EXPIRY_WINDOW /* expiration time of creds (int );  */
} slurm_cred_opt_t;

int slurm_cred_ctx_set(slurm_cred_ctx_t ctx, slurm_cred_opt_t opt, ...);
int slurm_cred_ctx_get(slurm_cred_ctx_t ctx, slurm_cred_opt_t opt, ...);

/*
 * Update the context's current key.
 */
int slurm_cred_ctx_key_update(slurm_cred_ctx_t ctx, const char *keypath);


/* 
 * Destroy a credential context, freeing associated memory.
 */
void slurm_cred_ctx_destroy(slurm_cred_ctx_t ctx);

/*
 * Pack and unpack slurm credential context. 
 *
 * On pack() ctx is packed in machine-independent format into the
 * buffer, on unpack() the contents of the buffer are used to 
 * intialize the state of the context ctx.
 */
int  slurm_cred_ctx_pack(slurm_cred_ctx_t ctx, Buf buffer);
int  slurm_cred_ctx_unpack(slurm_cred_ctx_t ctx, Buf buffer);


/*
 * Container for SLURM credential create and verify arguments:
 */
typedef struct {
	uint32_t jobid;
	uint32_t stepid;
	uid_t    uid;
	char    *hostlist;
        uint32_t ntask_cnt;
        uint32_t *ntask;
} slurm_cred_arg_t;

/*
 * Create a slurm credential using the values in `arg.'
 * The credential is signed using the creators public key.
 *
 * `arg' must be non-NULL and have valid values. The arguments
 * will be copied as is into the slurm job credential.
 *
 * Returns NULL on failure.
 */
slurm_cred_t slurm_cred_create(slurm_cred_ctx_t ctx, slurm_cred_arg_t *arg);


/*
 * Create a "fake" credential with bogus data in the signature.
 * This function can be used for testing, or when srun would like
 * to talk to slurmd directly, bypassing the controller
 * (which normally signs creds)
 */
slurm_cred_t slurm_cred_faker(slurm_cred_arg_t *arg);

/*
 * Verify the signed credential `cred,' and return cred contents in
 * the cred_arg structure. The credential is cached and cannot be reused.
 * 
 */
int slurm_cred_verify(slurm_cred_ctx_t ctx, slurm_cred_t cred, 
		      slurm_cred_arg_t *arg);

/*
 * Rewind the last play of credential cred. This allows the credential
 *  be used again. Returns SLURM_FAILURE if no credential state is found
 *  to be rewound, SLURM_SUCCESS otherwise.
 */
int slurm_cred_rewind(slurm_cred_ctx_t ctx, slurm_cred_t cred);

/*
 * Revoke all credentials for job id jobid
 */
int slurm_cred_revoke(slurm_cred_ctx_t ctx, uint32_t jobid);

/*
 * Report if a all credentials for a give job id have been 
 * revoked (i.e. has the job been killed)
 */
bool slurm_cred_revoked(slurm_cred_ctx_t ctx, uint32_t jobid);

/*
 * Begin expiration period for the revocation of credentials
 *  for job id jobid. This should be run after slurm_cred_revoke()
 *  This function is used because we may want to revoke credentials
 *  for a jobid, but not purge the revocation from memory until after
 *  some other action has occurred, e.g. completion of a job epilog.
 *
 * Returns 0 for success, SLURM_ERROR for failure with errno set to:
 *
 *  ESRCH  if jobid is not cached
 *  EEXIST if expiration period has already begun for jobid.
 * 
 */
int slurm_cred_begin_expiration(slurm_cred_ctx_t ctx, uint32_t jobid);


/*
 * Returns true if the credential context has a cached state for
 * job id jobid.
 */
bool slurm_cred_jobid_cached(slurm_cred_ctx_t ctx, uint32_t jobid);


/*
 * Add a jobid to the slurm credential context without inserting
 * a credential state. This is used by the verifier to track job ids
 * that it has seen, but not necessarily received a credential for.
 */
int slurm_cred_insert_jobid(slurm_cred_ctx_t ctx, uint32_t jobid);

/* Free memory associated with slurm credential `cred.'
 */
void slurm_cred_destroy(slurm_cred_t cred);

/*
 * Pack a slurm credential for network transmission
 */
void slurm_cred_pack(slurm_cred_t cred, Buf buffer);

/*
 * Unpack a slurm job credential
 */
slurm_cred_t slurm_cred_unpack(Buf buffer);

/*
 * Get a pointer to the slurm credential signature
 * (used by slurm IO connections to verify connecting agent)
 */
int slurm_cred_get_signature(slurm_cred_t cred, char **datap, int *len);


/*
 * Print a slurm job credential using the info() call
 */
void slurm_cred_print(slurm_cred_t cred);

#ifdef DISABLE_LOCALTIME
extern char * timestr (const time_t *tp, char *buf, size_t n);
#endif
#endif  /* _HAVE_SLURM_CREDS_H */
