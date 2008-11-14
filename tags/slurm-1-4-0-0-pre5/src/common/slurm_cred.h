/*****************************************************************************\
 *  src/common/slurm_cred.h  - SLURM job credential operations
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>.
 *  LLNL-CODE-402394.
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

#include "src/common/bitstring.h"
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
 * Container for SLURM credential create and verify arguments
 *
 * The core_bitmap, cores_per_socket, sockets_per_node, and 
 * sock_core_rep_count is based upon the nodes allocated to the
 * JOB, but the bits set in core_bitmap are those cores allocated
 * to this STEP
 */
typedef struct {
	uint32_t jobid;
	uint32_t stepid;
	uint32_t job_mem;	/* MB of memory reserved per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (no limit) */
	uid_t    uid;
	char    *hostlist;
	uint32_t alloc_lps_cnt;
	uint16_t *alloc_lps;

	bitstr_t *core_bitmap;
	uint16_t *cores_per_socket;
	uint16_t *sockets_per_node;
	uint32_t *sock_core_rep_count;
	uint32_t  job_nhosts;	/* count of nodes allocated to JOB */
	char     *job_hostlist;	/* list of nodes allocated to JOB */
} slurm_cred_arg_t;

/* Terminate the plugin and release all memory. */
int slurm_crypto_fini(void);

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
 * Copy a slurm credential.
 * Returns NULL on failure.
 */
slurm_cred_t slurm_cred_copy(slurm_cred_t cred);

/*
 * Create a "fake" credential with bogus data in the signature.
 * This function can be used for testing, or when srun would like
 * to talk to slurmd directly, bypassing the controller
 * (which normally signs creds)
 */
slurm_cred_t slurm_cred_faker(slurm_cred_arg_t *arg);

/* Free the credential arguments as loaded by either
 * slurm_cred_get_args() or slurm_cred_verify() */
void slurm_cred_free_args(slurm_cred_arg_t *arg);

/* Make a copy of the credential's arguements */
int slurm_cred_get_args(slurm_cred_t cred, slurm_cred_arg_t *arg);

/*
 * Verify the signed credential `cred,' and return cred contents in
 * the cred_arg structure. The credential is cached and cannot be reused.
 *
 * Will perform at least the following checks:
 *   - Credential signature is valid
 *   - Credential has not expired
 *   - If credential is reissue will purge the old credential
 *   - Credential has not been revoked
 *   - Credential has not been replayed
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
 * Check to see if this credential is a reissue of an existing credential
 * (this can happen, for instance, with "scontrol restart").  If
 * this credential is a reissue, then the old credential is cleared
 * from the cred context "ctx".
 */
void slurm_cred_handle_reissue(slurm_cred_ctx_t ctx, slurm_cred_t cred);

/*
 * Revoke all credentials for job id jobid
 * time IN - the time the job terminiation was requested by slurmctld
 *           (local time from slurmctld server)
 */
int slurm_cred_revoke(slurm_cred_ctx_t ctx, uint32_t jobid, time_t time);

/*
 * Report if a all credentials for a give job id have been 
 * revoked (i.e. has the job been killed)
 * 
 * If we are re-running the job, the new job credential is newer
 * than the revoke time, see "scontrol requeue", purge the old 
 * job record and make like it never existed
 */
bool slurm_cred_revoked(slurm_cred_ctx_t ctx, slurm_cred_t cred);

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

/*
 * Get count of allocated LPS (processors) by node
 */
int slurm_cred_get_alloc_lps(slurm_cred_t cred, char **nodes, 
			     uint32_t *alloc_lps_cnt, uint16_t **alloc_lps);
#ifdef DISABLE_LOCALTIME
extern char * timestr (const time_t *tp, char *buf, size_t n);
#endif
#endif  /* _HAVE_SLURM_CREDS_H */
