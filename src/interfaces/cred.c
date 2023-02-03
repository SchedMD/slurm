/*****************************************************************************\
 *  src/common/slurm_cred.c - Slurm job and sbcast credential functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2015 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#include "config.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/interfaces/gres.h"
#include "src/common/io_hdr.h"
#include "src/common/group_cache.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/interfaces/cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifndef __sbcast_cred_t_defined
#  define  __sbcast_cred_t_defined
typedef struct sbcast_cred sbcast_cred_t;		/* opaque data type */
#endif

#define MAX_TIME 0x7fffffff

/*
 * slurm job credential state
 *
 */
typedef struct {
	time_t   ctime;		/* Time that the cred was created	*/
	time_t   expiration;    /* Time at which cred is no longer good	*/
	slurm_step_id_t step_id; /* Slurm step id for this credential	*/
} cred_state_t;

/*
 * slurm job state information
 * tracks jobids for which all future credentials have been revoked
 *
 */
typedef struct {
	time_t   ctime;         /* Time that this entry was created         */
	time_t   expiration;    /* Time at which credentials can be purged  */
	uint32_t jobid;         /* Slurm job id for this credential	*/
	time_t   revoked;       /* Time at which credentials were revoked   */
} job_state_t;


/*
 * Completion of slurm credential context
 */
enum ctx_type {
	SLURM_CRED_CREATOR,
	SLURM_CRED_VERIFIER
};

/*
 * slurm sbcast credential state
 *
 */
struct sbcast_cred {
	time_t ctime;		/* Time that the cred was created	*/
	time_t expiration;	/* Time at which cred is no longer good	*/
	uint32_t jobid;		/* Slurm job id for this credential	*/
	uint32_t het_job_id;	/* Slurm hetjob leader id for the job	*/
	uint32_t step_id;	/* StepId				*/
	uint32_t uid;		/* user for which this cred is valid	*/
	uint32_t gid;		/* user's primary group id		*/
	char *user_name;	/* user_name as a string		*/
	uint32_t ngids;		/* number of extended group ids sent in
				 * credential. if 0, these will need to
				 * be fetched locally instead.		*/
	uint32_t *gids;		/* extended group ids for user		*/
	char *nodes;		/* nodes for which credential is valid	*/
	char *signature;	/* credential signature			*/
	uint32_t siglen;	/* signature length in bytes		*/
};

/*
 * Credential context, slurm_cred_ctx_t:
 */
#define CRED_CTX_MAGIC 0x0c0c0c
struct slurm_cred_context {
	int magic;
	pthread_mutex_t mutex;
	enum ctx_type type;	/* context type (creator or verifier)	*/
	void *key;		/* private or public key		*/
	List job_list;		/* List of used jobids (for verifier)	*/
	List state_list;	/* List of cred states (for verifier)	*/

	int expiry_window;	/* expiration window for cached creds	*/

	void *exkey;		/* Old public key if key is updated	*/
	time_t exkey_exp;	/* Old key expiration time		*/
};


/*
 * Completion of slurm job credential type, slurm_cred_t:
 */
#define CRED_MAGIC 0x0b0b0b
struct slurm_job_credential {
	int magic;
	pthread_rwlock_t mutex;
	buf_t *buffer;		/* packed representation of credential */
	uint16_t buf_version;	/* version buffer was generated with */

	slurm_cred_arg_t *arg;	/* fields */

	time_t ctime;		/* time of credential creation */
	char *signature;	/* credential signature */
	uint32_t siglen;	/* signature length in bytes */
	bool verified;		/* credential has been verified successfully */
};

typedef struct {
	void *(*cred_read_private_key)	(const char *path);
	void *(*cred_read_public_key)	(const char *path);
	void  (*cred_destroy_key)	(void *key);
	int   (*cred_sign)		(void *key, char *buffer,
					 int buf_size, char **sig_pp,
					 uint32_t *sig_size_p);
	int   (*cred_verify_sign)	(void *key, char *buffer,
					 uint32_t buf_size,
					 char *signature,
					 uint32_t sig_size);
	const char *(*cred_str_error)	(int);
} slurm_cred_ops_t;

/*
 * These strings must be in the same order as the fields declared
 * for slurm_cred_ops_t.
 */
static const char *syms[] = {
	"cred_p_read_private_key",
	"cred_p_read_public_key",
	"cred_p_destroy_key",
	"cred_p_sign",
	"cred_p_verify_sign",
	"cred_p_str_error",
};

struct sbcast_cache {
	time_t       expire;	/* Time that the cred was created	*/
	uint32_t     value;	/* Slurm job id for this credential	*/
};

static slurm_cred_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static time_t cred_restart_time = (time_t) 0;
static List sbcast_cache_list = NULL;
static int cred_expire = DEFAULT_EXPIRATION_WINDOW;
static bool enable_nss_slurm = false;
static bool enable_send_gids = true;

/*
 * Verification context used in slurmd during credential unpack operations.
 * Needs to be a global here since we don't want to modify the entire
 * unpack chain ahead of slurm_cred_unpack() to pass this around.
 */
static slurm_cred_ctx_t verifier_ctx = NULL;

/*
 * Static prototypes:
 */

static slurm_cred_ctx_t _slurm_cred_ctx_alloc(void);
static slurm_cred_t *_slurm_cred_alloc(bool alloc_arg);

static int  _ctx_update_private_key(slurm_cred_ctx_t ctx, const char *path);
static int  _ctx_update_public_key(slurm_cred_ctx_t ctx, const char *path);
static bool _exkey_is_valid(slurm_cred_ctx_t ctx);

static cred_state_t * _cred_state_create(slurm_cred_ctx_t ctx, slurm_cred_t *c);
static job_state_t  * _job_state_create(uint32_t jobid);
static void           _job_state_destroy(job_state_t   *js);

static job_state_t  * _find_job_state(slurm_cred_ctx_t ctx, uint32_t jobid);
static job_state_t  * _insert_job_state(slurm_cred_ctx_t ctx,  uint32_t jobid);
static int _list_find_cred_state(void *x, void *key);

static void _insert_cred_state(slurm_cred_ctx_t ctx, slurm_cred_t *cred);
static void _clear_expired_job_states(slurm_cred_ctx_t ctx);
static void _clear_expired_credential_states(slurm_cred_ctx_t ctx);
static void _verifier_ctx_init(slurm_cred_ctx_t ctx);

static bool _credential_replayed(slurm_cred_ctx_t ctx, slurm_cred_t *cred);
static bool _credential_revoked(slurm_cred_ctx_t ctx, slurm_cred_t *cred);

static int _cred_sign(slurm_cred_ctx_t ctx, slurm_cred_t *cred);
static void _cred_verify_signature(slurm_cred_ctx_t ctx, slurm_cred_t *cred);

static int _slurm_cred_init(void);
static int _slurm_cred_fini(void);

static job_state_t *_job_state_unpack_one(buf_t *buffer);
static cred_state_t *_cred_state_unpack_one(buf_t *buffer);

static void _pack_cred(slurm_cred_arg_t *cred, buf_t *buffer,
		       uint16_t protocol_version);
static void _job_state_unpack(slurm_cred_ctx_t ctx, buf_t *buffer);
static void _job_state_pack(slurm_cred_ctx_t ctx, buf_t *buffer);
static void _cred_state_unpack(slurm_cred_ctx_t ctx, buf_t *buffer);
static void _cred_state_pack(slurm_cred_ctx_t ctx, buf_t *buffer);

static void _sbast_cache_add(sbcast_cred_t *sbcast_cred);

static int _slurm_cred_init(void)
{
	char *tok;
	char    *plugin_type = "cred";
	int	retval = SLURM_SUCCESS;

	/*					 123456789012 */
	if ((tok = xstrstr(slurm_conf.authinfo, "cred_expire="))) {
		cred_expire = atoi(tok + 12);
		if (cred_expire < 5) {
			error("AuthInfo=cred_expire=%d invalid", cred_expire);
			cred_expire = DEFAULT_EXPIRATION_WINDOW;
		}
	}

	if (xstrcasestr(slurm_conf.launch_params, "enable_nss_slurm"))
		enable_nss_slurm = true;
	else if (xstrcasestr(slurm_conf.launch_params, "disable_send_gids"))
		enable_send_gids = false;

	slurm_mutex_lock( &g_context_lock );
	if (cred_restart_time == (time_t) 0)
		cred_restart_time = time(NULL);
	if ( g_context )
		goto done;

	g_context = plugin_context_create(plugin_type,
					  slurm_conf.cred_type,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.cred_type);
		retval = SLURM_ERROR;
		goto done;
	}
	sbcast_cache_list = list_create(xfree_ptr);

done:
	slurm_mutex_unlock( &g_context_lock );

	return(retval);
}

/* Initialize the plugin. */
int slurm_cred_init(void)
{
	if (_slurm_cred_init() < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static int _slurm_cred_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	FREE_NULL_LIST(sbcast_cache_list);
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

/* Fill in user information based on what options are enabled. */
static int _fill_cred_gids(slurm_cred_arg_t *arg)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	int rc;

	if (!enable_nss_slurm && !enable_send_gids)
		return SLURM_SUCCESS;

	xassert(arg);

	rc = slurm_getpwuid_r(arg->uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (rc || !result) {
		if (!result && !rc)
			error("%s: getpwuid_r(%u): no record found",
			      __func__, arg->uid);
		else
			error("%s: getpwuid_r(%u): %s",
			      __func__, arg->uid, slurm_strerror(rc));
		return SLURM_ERROR;
	}

	arg->pw_name = xstrdup(result->pw_name);
	arg->pw_gecos = xstrdup(result->pw_gecos);
	arg->pw_dir = xstrdup(result->pw_dir);
	arg->pw_shell = xstrdup(result->pw_shell);

	arg->ngids = group_cache_lookup(arg->uid, arg->gid,
					arg->pw_name, &arg->gids);

	if (enable_nss_slurm) {
		if (arg->ngids) {
			arg->gr_names = xcalloc(arg->ngids, sizeof(char *));
			for (int i = 0; i < arg->ngids; i++)
				arg->gr_names[i] = gid_to_string(arg->gids[i]);
		}
	}

	return SLURM_SUCCESS;
}

static void _release_cred_gids(slurm_cred_arg_t *arg)
{
	if (!enable_nss_slurm && !enable_send_gids)
		return;

	xfree(arg->pw_name);
	xfree(arg->pw_gecos);
	xfree(arg->pw_dir);
	xfree(arg->pw_shell);
	xfree(arg->gids);

	if (arg->gr_names) {
		for (int i = 0; i < arg->ngids; i++)
			xfree(arg->gr_names[i]);
		xfree(arg->gr_names);
	}
	arg->ngids = 0;
}

/* Terminate the plugin and release all memory. */
extern int slurm_cred_fini(void)
{
	if (_slurm_cred_fini() < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

slurm_cred_ctx_t
slurm_cred_creator_ctx_create(const char *path)
{
	slurm_cred_ctx_t ctx = NULL;

	xassert(g_context);

	ctx = _slurm_cred_ctx_alloc();
	slurm_mutex_lock(&ctx->mutex);

	ctx->type = SLURM_CRED_CREATOR;

	ctx->key = (*(ops.cred_read_private_key))(path);
	if (!ctx->key)
		goto fail;

	slurm_mutex_unlock(&ctx->mutex);
	return ctx;

fail:
	slurm_mutex_unlock(&ctx->mutex);
	slurm_cred_ctx_destroy(ctx);
	error("Can not open data encryption key file %s", path);
	return NULL;
}

slurm_cred_ctx_t
slurm_cred_verifier_ctx_create(const char *path)
{
	slurm_cred_ctx_t ctx = NULL;

	xassert(g_context);

	ctx = _slurm_cred_ctx_alloc();
	slurm_mutex_lock(&ctx->mutex);

	ctx->type = SLURM_CRED_VERIFIER;

	ctx->key = (*(ops.cred_read_public_key))(path);
	if (!ctx->key)
		goto fail;

	_verifier_ctx_init(ctx);

	slurm_mutex_unlock(&ctx->mutex);

	verifier_ctx = ctx;
	return ctx;

fail:
	slurm_mutex_unlock(&ctx->mutex);
	slurm_cred_ctx_destroy(ctx);
	error("Can not open data encryption key file %s", path);
	return NULL;
}


void
slurm_cred_ctx_destroy(slurm_cred_ctx_t ctx)
{
	if (ctx == NULL)
		return;
	if (_slurm_cred_init() < 0)
		return;

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);

	if (ctx->exkey)
		(*(ops.cred_destroy_key))(ctx->exkey);
	if (ctx->key)
		(*(ops.cred_destroy_key))(ctx->key);
	FREE_NULL_LIST(ctx->job_list);
	FREE_NULL_LIST(ctx->state_list);

	ctx->magic = ~CRED_CTX_MAGIC;
	slurm_mutex_unlock(&ctx->mutex);
	slurm_mutex_destroy(&ctx->mutex);

	xfree(ctx);

	return;
}

int
slurm_cred_ctx_set(slurm_cred_ctx_t ctx, slurm_cred_opt_t opt, ...)
{
	int     rc  = SLURM_SUCCESS;
	va_list ap;

	xassert(ctx != NULL);

	va_start(ap, opt);

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);

	switch (opt) {
	case SLURM_CRED_OPT_EXPIRY_WINDOW:
		ctx->expiry_window = va_arg(ap, int);
		break;
	default:
		slurm_seterrno(EINVAL);
		rc = SLURM_ERROR;
		break;
	}

	slurm_mutex_unlock(&ctx->mutex);

	va_end(ap);

	return rc;
}

int
slurm_cred_ctx_get(slurm_cred_ctx_t ctx, slurm_cred_opt_t opt, ...)
{
	int rc = SLURM_SUCCESS;
	va_list ap;
	int *intp;

	xassert(ctx != NULL);

	va_start(ap, opt);

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);

	switch (opt) {
	case SLURM_CRED_OPT_EXPIRY_WINDOW:
		intp  = va_arg(ap, int *);
		*intp = ctx->expiry_window;
		break;
	default:
		slurm_seterrno(EINVAL);
		rc = SLURM_ERROR;
		break;
	}

	slurm_mutex_unlock(&ctx->mutex);

	va_end(ap);

	return rc;
}

int
slurm_cred_ctx_key_update(slurm_cred_ctx_t ctx, const char *path)
{
	if (_slurm_cred_init() < 0)
		return SLURM_ERROR;

	if (ctx->type == SLURM_CRED_CREATOR)
		return _ctx_update_private_key(ctx, path);
	else
		return _ctx_update_public_key(ctx, path);
}

slurm_cred_t *slurm_cred_create(slurm_cred_ctx_t ctx, slurm_cred_arg_t *arg,
				bool sign_it, uint16_t protocol_version)
{
	slurm_cred_t *cred = NULL;
	int i = 0, sock_recs = 0;

	xassert(ctx != NULL);
	xassert(arg != NULL);
	xassert(g_context);

	if (arg->uid == SLURM_AUTH_NOBODY) {
		error("%s: refusing to create job %u credential for invalid user nobody",
		      __func__, arg->step_id.job_id);
		goto fail;
	}

	if (arg->gid == SLURM_AUTH_NOBODY) {
		error("%s: refusing to create job %u credential for invalid group nobody",
		      __func__, arg->step_id.job_id);
		goto fail;
	}

	cred = _slurm_cred_alloc(false);
	xassert(cred->magic == CRED_MAGIC);

	if (arg->sock_core_rep_count) {
		for (i = 0; i < arg->job_nhosts; i++) {
			sock_recs += arg->sock_core_rep_count[i];
			if (sock_recs >= arg->job_nhosts)
				break;
		}
		i++;
	}
	arg->core_array_size = i;

	if (_fill_cred_gids(arg) != SLURM_SUCCESS)
		goto fail;

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type == SLURM_CRED_CREATOR);

	cred->buffer = init_buf(4096);
	cred->buf_version = protocol_version;

	_pack_cred(arg, cred->buffer, protocol_version);

	if (sign_it && _cred_sign(ctx, cred) < 0) {
		slurm_mutex_unlock(&ctx->mutex);
		goto fail;
	}

	/* Release any values populated through _fill_cred_gids(). */
	_release_cred_gids(arg);

	slurm_mutex_unlock(&ctx->mutex);

	return cred;

fail:
	slurm_cred_destroy(cred);
	return NULL;
}

slurm_cred_t *
slurm_cred_faker(slurm_cred_arg_t *arg)
{
	slurm_cred_ctx_t ctx;
	slurm_cred_t *cred = NULL;

	/*
	 * Force this on to ensure pw_name, ngid, gids are all populated.
	 */
	enable_send_gids = true;

	ctx = slurm_cred_creator_ctx_create(NULL);
	cred = slurm_cred_create(ctx, arg, true, SLURM_PROTOCOL_VERSION);
	slurm_cred_ctx_destroy(ctx);

	return cred;
}

void slurm_cred_free_args(slurm_cred_arg_t *arg)
{
	if (!arg)
		return;

	xfree(arg->pw_name);
	xfree(arg->pw_gecos);
	xfree(arg->pw_dir);
	xfree(arg->pw_shell);
	xfree(arg->gids);
	for (int i = 0; arg->gr_names && i < arg->ngids; i++)
		xfree(arg->gr_names[i]);
	xfree(arg->gr_names);
	FREE_NULL_BITMAP(arg->job_core_bitmap);
	FREE_NULL_BITMAP(arg->step_core_bitmap);
	xfree(arg->cores_per_socket);
	xfree(arg->cpu_array);
	xfree(arg->cpu_array_reps);
	FREE_NULL_LIST(arg->job_gres_list);
	FREE_NULL_LIST(arg->step_gres_list);
	xfree(arg->step_hostlist);
	xfree(arg->job_account);
	xfree(arg->job_alias_list);
	xfree(arg->job_comment);
	xfree(arg->job_constraints);
	xfree(arg->job_licenses);
	xfree(arg->job_hostlist);
	xfree(arg->sock_core_rep_count);
	xfree(arg->sockets_per_node);
	xfree(arg->job_mem_alloc);
	xfree(arg->job_mem_alloc_rep_count);
	xfree(arg->job_partition);
	xfree(arg->job_reservation);
	xfree(arg->job_std_err);
	xfree(arg->job_std_in);
	xfree(arg->job_std_out);
	xfree(arg->step_mem_alloc);
	xfree(arg->step_mem_alloc_rep_count);

	xfree(arg);
}

extern void slurm_cred_unlock_args(slurm_cred_t *cred)
{
	slurm_rwlock_unlock(&cred->mutex);
}

/*
 * Caller *must* release lock.
 */
slurm_cred_arg_t *slurm_cred_get_args(slurm_cred_t *cred)
{
	xassert(cred != NULL);

	slurm_rwlock_rdlock(&cred->mutex);
	return cred->arg;
}

extern void *slurm_cred_get(slurm_cred_t *cred,
			    cred_data_enum_t cred_data_type)
{
	void *rc = NULL;

	xassert(cred != NULL);

	slurm_rwlock_rdlock(&cred->mutex);

	if (!cred->arg) {
		slurm_rwlock_unlock(&cred->mutex);
		return NULL;
	}

	switch (cred_data_type) {
	case CRED_DATA_JOB_GRES_LIST:
		rc = (void *) cred->arg->job_gres_list;
		break;
	case CRED_DATA_JOB_ALIAS_LIST:
		rc = (void *) cred->arg->job_alias_list;
		break;
	case CRED_DATA_STEP_GRES_LIST:
		rc = (void *) cred->arg->step_gres_list;
		break;
	default:
		error("%s: Invalid arg type requested (%d)", __func__,
		      cred_data_type);

	}
	slurm_rwlock_unlock(&cred->mutex);

	return rc;
}

/*
 * Returns NULL on error.
 *
 * On success, returns a pointer to the arg structure within the credential.
 * Caller *must* release the lock.
 */
extern slurm_cred_arg_t *slurm_cred_verify(slurm_cred_ctx_t ctx,
					   slurm_cred_t *cred)
{
	time_t now = time(NULL);
	int errnum;

	xassert(ctx  != NULL);
	xassert(cred != NULL);

	xassert(g_context);

	slurm_rwlock_rdlock(&cred->mutex);
	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic  == CRED_CTX_MAGIC);
	xassert(ctx->type   == SLURM_CRED_VERIFIER);
	xassert(cred->magic == CRED_MAGIC);

	/* NOTE: the verification checks that the credential was
	 * created by SlurmUser or root */
	if (!cred->verified) {
		slurm_seterrno(ESLURMD_INVALID_JOB_CREDENTIAL);
		goto error;
	}

	if (now > (cred->ctime + ctx->expiry_window)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_EXPIRED);
		goto error;
	}

	slurm_cred_handle_reissue(ctx, cred, true);

	if (_credential_revoked(ctx, cred)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_REVOKED);
		goto error;
	}

	if (_credential_replayed(ctx, cred)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_REPLAYED);
		goto error;
	}

	slurm_mutex_unlock(&ctx->mutex);

	return cred->arg;

error:
	errnum = slurm_get_errno();
	slurm_mutex_unlock(&ctx->mutex);
	slurm_rwlock_unlock(&cred->mutex);
	slurm_seterrno(errnum);
	return NULL;
}


void
slurm_cred_destroy(slurm_cred_t *cred)
{
	if (cred == NULL)
		return;

	xassert(cred->magic == CRED_MAGIC);

	slurm_rwlock_wrlock(&cred->mutex);
	slurm_cred_free_args(cred->arg);
	FREE_NULL_BUFFER(cred->buffer);
	xfree(cred->signature);
	cred->magic = ~CRED_MAGIC;
	slurm_rwlock_unlock(&cred->mutex);
	slurm_rwlock_destroy(&cred->mutex);

	xfree(cred);
}


bool
slurm_cred_jobid_cached(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	bool retval = false;

	xassert(ctx != NULL);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	slurm_mutex_lock(&ctx->mutex);

	_clear_expired_job_states(ctx);

	/*
	 * Return true if we find a cached job state for job id `jobid'
	 */
	retval = (_find_job_state(ctx, jobid) != NULL);

	slurm_mutex_unlock(&ctx->mutex);

	return retval;
}

int
slurm_cred_insert_jobid(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	xassert(ctx != NULL);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	slurm_mutex_lock(&ctx->mutex);

	_clear_expired_job_states(ctx);
	(void) _insert_job_state(ctx, jobid);

	slurm_mutex_unlock(&ctx->mutex);

	return SLURM_SUCCESS;
}

int
slurm_cred_rewind(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	int rc = 0;

	xassert(ctx != NULL);

	if (!cred->verified)
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	rc = list_delete_all(ctx->state_list, _list_find_cred_state, cred);

	slurm_mutex_unlock(&ctx->mutex);

	return (rc > 0 ? SLURM_SUCCESS : SLURM_ERROR);
}

int
slurm_cred_revoke(slurm_cred_ctx_t ctx, uint32_t jobid, time_t time,
		  time_t start_time)
{
	job_state_t  *j = NULL;

	xassert(ctx != NULL);

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	_clear_expired_job_states(ctx);

	if (!(j = _find_job_state(ctx, jobid))) {
		/*
		 *  This node has not yet seen a job step for this
		 *   job. Insert a job state object so that we can
		 *   revoke any future credentials.
		 */
		j = _insert_job_state(ctx, jobid);
	}
	if (j->revoked) {
		if (start_time && (j->revoked < start_time)) {
			debug("job %u requeued, but started no tasks", jobid);
			j->expiration = (time_t) MAX_TIME;
		} else {
			slurm_seterrno(EEXIST);
			goto error;
		}
	}

	j->revoked = time;

	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_SUCCESS;

error:
	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_ERROR;
}

int
slurm_cred_begin_expiration(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	job_state_t  *j = NULL;

	xassert(ctx != NULL);

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	_clear_expired_job_states(ctx);

	if (!(j = _find_job_state(ctx, jobid))) {
		slurm_seterrno(ESRCH);
		goto error;
	}

	if (j->expiration < (time_t) MAX_TIME) {
		slurm_seterrno(EEXIST);
		goto error;
	}

	j->expiration  = time(NULL) + ctx->expiry_window;
	debug2("set revoke expiration for jobid %u to %ld UTS",
	       j->jobid, j->expiration);
	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_SUCCESS;

error:
	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_ERROR;
}

int
slurm_cred_get_signature(slurm_cred_t *cred, char **datap, uint32_t *datalen)
{
	xassert(cred    != NULL);
	xassert(datap   != NULL);
	xassert(datalen != NULL);

	slurm_rwlock_rdlock(&cred->mutex);

	*datap   = (char *) cred->signature;
	*datalen = cred->siglen;

	slurm_rwlock_unlock(&cred->mutex);

	return SLURM_SUCCESS;
}

extern void slurm_cred_get_mem(slurm_cred_t *credential, char *node_name,
			       const char *func_name,
			       uint64_t *job_mem_limit,
			       uint64_t *step_mem_limit)
{
	slurm_cred_arg_t *cred = credential->arg;
	int rep_idx = -1;
	int node_id = -1;

	/*
	 * Batch steps only have the job_hostlist set and will always be 0 here.
	 */
	if (cred->step_id.step_id == SLURM_BATCH_SCRIPT) {
		rep_idx = 0;
	} else if ((node_id =
		    nodelist_find(cred->job_hostlist, node_name)) >= 0) {
		rep_idx = slurm_get_rep_count_inx(cred->job_mem_alloc_rep_count,
					          cred->job_mem_alloc_size,
						  node_id);

	} else {
		error("Unable to find %s in job hostlist: `%s'",
		      node_name, cred->job_hostlist);
	}

	if (rep_idx < 0)
		error("%s: node_id=%d, not found in job_mem_alloc_rep_count requested job memory not reset.",
		      func_name, node_id);
	else
		*job_mem_limit = cred->job_mem_alloc[rep_idx];

	if (!step_mem_limit) {
		log_flag(CPU_BIND, "%s: Memory extracted from credential for %ps job_mem_limit= %"PRIu64,
			 func_name, &cred->step_id, *job_mem_limit);
		return;
	}

	if (cred->step_mem_alloc) {
		rep_idx = -1;
		if ((node_id =
		     nodelist_find(cred->step_hostlist, node_name)) >= 0) {
			rep_idx = slurm_get_rep_count_inx(
						cred->step_mem_alloc_rep_count,
						cred->step_mem_alloc_size,
						node_id);
		} else {
			error("Unable to find %s in step hostlist: `%s'",
			      node_name, cred->step_hostlist);
		}
		if (rep_idx < 0)
			error("%s: node_id=%d, not found in step_mem_alloc_rep_count",
			      func_name, node_id);
		else
			*step_mem_limit = cred->step_mem_alloc[rep_idx];
	}

	/*
	 * If we are not set or we were sent 0 go with the job_mem_limit value.
	 */
	if (!(*step_mem_limit))
		*step_mem_limit = *job_mem_limit;

	log_flag(CPU_BIND, "Memory extracted from credential for %ps job_mem_limit=%"PRIu64" step_mem_limit=%"PRIu64,
		 &cred->step_id, *job_mem_limit, *step_mem_limit);
}

/* Convert bitmap to string representation with brackets removed */
static char *_core_format(bitstr_t *core_bitmap)
{
	char str[1024], *bracket_ptr;

	bit_fmt(str, sizeof(str), core_bitmap);
	if (str[0] != '[')
		return xstrdup(str);

	/* strip off brackets */
	bracket_ptr = strchr(str, ']');
	if (bracket_ptr)
		bracket_ptr[0] = '\0';
	return xstrdup(str+1);
}

/*
 * Retrieve the set of cores that were allocated to the job and step then
 * format them in the List Format (e.g., "0-2,7,12-14"). Also return
 * job and step's memory limit.
 *
 * NOTE: caller must xfree the returned strings.
 */
void format_core_allocs(slurm_cred_t *credential, char *node_name,
			uint16_t cpus, char **job_alloc_cores,
			char **step_alloc_cores, uint64_t *job_mem_limit,
			uint64_t *step_mem_limit)
{
	slurm_cred_arg_t *cred = credential->arg;
	bitstr_t	*job_core_bitmap, *step_core_bitmap;
	hostlist_t	hset = NULL;
	int		host_index = -1;
	uint32_t	i, j, i_first_bit=0, i_last_bit=0;

	xassert(cred);
	xassert(job_alloc_cores);
	xassert(step_alloc_cores);
	if (!(hset = hostlist_create(cred->job_hostlist))) {
		error("Unable to create job hostlist: `%s'",
		      cred->job_hostlist);
		return;
	}
#ifdef HAVE_FRONT_END
	host_index = 0;
#else
	host_index = hostlist_find(hset, node_name);
#endif
	if ((host_index < 0) || (host_index >= cred->job_nhosts)) {
		error("Invalid host_index %d for job %u",
		      host_index, cred->step_id.job_id);
		error("Host %s not in hostlist %s",
		      node_name, cred->job_hostlist);
		hostlist_destroy(hset);
		return;
	}
	host_index++;	/* change from 0-origin to 1-origin */
	for (i=0; host_index; i++) {
		if (host_index > cred->sock_core_rep_count[i]) {
			i_first_bit += cred->sockets_per_node[i] *
				cred->cores_per_socket[i] *
				cred->sock_core_rep_count[i];
			host_index -= cred->sock_core_rep_count[i];
		} else {
			i_first_bit += cred->sockets_per_node[i] *
				cred->cores_per_socket[i] *
				(host_index - 1);
			i_last_bit = i_first_bit +
				cred->sockets_per_node[i] *
				cred->cores_per_socket[i];
			break;
		}
	}

	job_core_bitmap  = bit_alloc(i_last_bit - i_first_bit);
	step_core_bitmap = bit_alloc(i_last_bit - i_first_bit);
	for (i = i_first_bit, j = 0; i < i_last_bit; i++, j++) {
		if (bit_test(cred->job_core_bitmap, i))
			bit_set(job_core_bitmap, j);
		if (bit_test(cred->step_core_bitmap, i))
			bit_set(step_core_bitmap, j);
	}

	/* Scale CPU count, same as slurmd/req.c:_check_job_credential() */
	if (i_last_bit <= i_first_bit)
		error("step credential has no CPUs selected");
	else {
		uint32_t i = cpus / (i_last_bit - i_first_bit);
		if (i > 1)
			debug2("scaling CPU count by factor of %d (%u/(%u-%u)",
			       i, cpus, i_last_bit, i_first_bit);
	}

	slurm_cred_get_mem(credential, node_name, __func__, job_mem_limit,
			   step_mem_limit);

	*job_alloc_cores  = _core_format(job_core_bitmap);
	*step_alloc_cores = _core_format(step_core_bitmap);
	FREE_NULL_BITMAP(job_core_bitmap);
	FREE_NULL_BITMAP(step_core_bitmap);
	hostlist_destroy(hset);
}

/*
 * Retrieve the job and step generic resources (gres) allocate to this job
 * on this node.
 *
 * NOTE: Caller must destroy the returned lists
 */
extern void get_cred_gres(slurm_cred_t *credential, char *node_name,
			  List *job_gres_list, List *step_gres_list)
{
	slurm_cred_arg_t *cred = credential->arg;
	hostlist_t	hset = NULL;
	int		host_index = -1;

	xassert(cred);
	xassert(job_gres_list);
	xassert(step_gres_list);

	FREE_NULL_LIST(*job_gres_list);
	FREE_NULL_LIST(*step_gres_list);
	if ((cred->job_gres_list == NULL) && (cred->step_gres_list == NULL))
		return;

	if (!(hset = hostlist_create(cred->job_hostlist))) {
		error("Unable to create job hostlist: `%s'",
		      cred->job_hostlist);
		return;
	}
#ifdef HAVE_FRONT_END
	host_index = 0;
#else
	host_index = hostlist_find(hset, node_name);
#endif
	hostlist_destroy(hset);
	if ((host_index < 0) || (host_index >= cred->job_nhosts)) {
		error("Invalid host_index %d for job %u",
		      host_index, cred->step_id.job_id);
		error("Host %s not in credential hostlist %s",
		      node_name, cred->job_hostlist);
		return;
	}

	*job_gres_list = gres_job_state_extract(cred->job_gres_list,
						host_index);
	*step_gres_list = gres_step_state_extract(cred->step_gres_list,
						  host_index);
	return;
}

void slurm_cred_pack(slurm_cred_t *cred, buf_t *buffer,
		     uint16_t protocol_version)
{
	xassert(cred != NULL);
	xassert(cred->magic == CRED_MAGIC);

	slurm_rwlock_rdlock(&cred->mutex);

	xassert(cred->buffer);
	xassert(cred->buf_version == protocol_version);
	packbuf(cred->buffer, buffer);

	if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
		packmem(cred->signature, cred->siglen, buffer);
	} else {
		/*
		 * Older Slurm versions can xassert() on a 0-length signature,
		 * so ensure something is packed there even when the signature
		 * is not required.
		 */
		if (cred->siglen)
			packmem(cred->signature, cred->siglen, buffer);
		else
			packmem("-", 1, buffer);
	}

	slurm_rwlock_unlock(&cred->mutex);
}

slurm_cred_t *slurm_cred_unpack(buf_t *buffer, uint16_t protocol_version)
{
	uint32_t u32_ngids, len;
	slurm_cred_t *credential = NULL;
	/*
	 * The slightly confusing name is to avoid changing the entire unpack
	 * blocks below during refactor.
	 */
	slurm_cred_arg_t *cred = NULL;
	char *bit_fmt_str = NULL;
	char       **sigp;
	uint32_t tot_core_cnt;
	uint32_t cred_start, cred_len;

	xassert(buffer != NULL);

	/* Save current buffer position here, use it later to verify cred. */
	cred_start = get_buf_offset(buffer);

	credential = _slurm_cred_alloc(true);
	cred = credential->arg;
	if (protocol_version >= SLURM_23_02_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&cred->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&cred->uid, buffer);
		if (cred->uid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid user nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpack32(&cred->gid, buffer);
		if (cred->gid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid group nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpackstr(&cred->pw_name, buffer);
		safe_unpackstr(&cred->pw_gecos, buffer);
		safe_unpackstr(&cred->pw_dir, buffer);
		safe_unpackstr(&cred->pw_shell, buffer);
		safe_unpack32_array(&cred->gids, &u32_ngids, buffer);
		cred->ngids = u32_ngids;
		safe_unpackstr_array(&cred->gr_names, &u32_ngids, buffer);
		if (u32_ngids && cred->ngids != u32_ngids) {
			error("%s: mismatch on gr_names array, %u != %u",
			      __func__, u32_ngids, cred->ngids);
			goto unpack_error;
		}
		if (gres_job_state_unpack(&cred->job_gres_list, buffer,
					  cred->step_id.job_id,
					  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&cred->step_gres_list,
					   buffer, &cred->step_id,
					   protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred->job_core_spec, buffer);
		safe_unpackstr(&cred->job_account, buffer);
		safe_unpackstr(&cred->job_alias_list, buffer);
		safe_unpackstr(&cred->job_comment, buffer);
		safe_unpackstr(&cred->job_constraints, buffer);
		safe_unpack_time(&cred->job_end_time, buffer);
		safe_unpackstr(&cred->job_extra, buffer);
		safe_unpack16(&cred->job_oversubscribe, buffer);
		safe_unpackstr(&cred->job_partition, buffer);
		safe_unpackstr(&cred->job_reservation, buffer);
		safe_unpack16(&cred->job_restart_cnt, buffer);
		safe_unpack_time(&cred->job_start_time, buffer);
		safe_unpackstr(&cred->job_std_err, buffer);
		safe_unpackstr(&cred->job_std_in, buffer);
		safe_unpackstr(&cred->job_std_out, buffer);
		safe_unpackstr(&cred->step_hostlist, buffer);
		safe_unpack16(&cred->x11, buffer);
		safe_unpack_time(&credential->ctime, buffer);
		safe_unpack32(&tot_core_cnt, buffer);
		unpack_bit_str_hex(&cred->job_core_bitmap, buffer);
		unpack_bit_str_hex(&cred->step_core_bitmap, buffer);
		safe_unpack16(&cred->core_array_size, buffer);
		if (cred->core_array_size) {
			safe_unpack16_array(&cred->cores_per_socket, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
			safe_unpack16_array(&cred->sockets_per_node, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
			safe_unpack32_array(&cred->sock_core_rep_count, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
		}
		safe_unpack32(&cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			safe_unpack16_array(&cred->cpu_array, &len, buffer);
			if (len != cred->cpu_array_count)
				goto unpack_error;
			safe_unpack32_array(&cred->cpu_array_reps, &len,
					    buffer);
			if (len != cred->cpu_array_count)
				goto unpack_error;
		}
		safe_unpack32(&cred->job_nhosts, buffer);
		safe_unpack32(&cred->job_ntasks, buffer);
		safe_unpackstr(&cred->job_hostlist, buffer);
		safe_unpackstr(&cred->job_licenses, buffer);

		safe_unpack32(&cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			safe_unpack64_array(&cred->job_mem_alloc, &len, buffer);
			if (len != cred->job_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred->job_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred->job_mem_alloc_size)
				goto unpack_error;

		}

		safe_unpack32(&cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			safe_unpack64_array(&cred->step_mem_alloc, &len,
					    buffer);
			if (len != cred->step_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred->step_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred->step_mem_alloc_size)
				goto unpack_error;
		}

		safe_unpackstr(&cred->selinux_context, buffer);

		/* "sigp" must be last */
		cred_len = get_buf_offset(buffer) - cred_start;
		sigp = (char **) &credential->signature;
		safe_unpackmem_xmalloc(sigp, &len, buffer);
		credential->siglen = len;
	} else if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
		if (unpack_step_id_members(&cred->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&cred->uid, buffer);
		if (cred->uid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid user nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpack32(&cred->gid, buffer);
		if (cred->gid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid group nobody",
			      __func__);
			goto unpack_error;
		}
		safe_unpackstr_xmalloc(&cred->pw_name, &len, buffer);
		safe_unpackstr_xmalloc(&cred->pw_gecos, &len, buffer);
		safe_unpackstr_xmalloc(&cred->pw_dir, &len, buffer);
		safe_unpackstr_xmalloc(&cred->pw_shell, &len, buffer);
		safe_unpack32_array(&cred->gids, &u32_ngids, buffer);
		cred->ngids = u32_ngids;
		safe_unpackstr_array(&cred->gr_names, &u32_ngids, buffer);
		if (u32_ngids && cred->ngids != u32_ngids) {
			error("%s: mismatch on gr_names array, %u != %u",
			      __func__, u32_ngids, cred->ngids);
			goto unpack_error;
		}
		if (gres_job_state_unpack(&cred->job_gres_list, buffer,
					  cred->step_id.job_id,
					  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&cred->step_gres_list,
					   buffer, &cred->step_id,
					   protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred->job_core_spec, buffer);
		safe_unpackstr_xmalloc(&cred->job_account, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_alias_list, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_comment, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_constraints, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_partition, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_reservation, &len, buffer);
		safe_unpack16(&cred->job_restart_cnt, buffer);
		safe_unpackstr_xmalloc(&cred->job_std_err, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_std_in, &len, buffer);
		safe_unpackstr_xmalloc(&cred->job_std_out, &len, buffer);
		safe_unpackstr_xmalloc(&cred->step_hostlist, &len, buffer);
		safe_unpack16(&cred->x11, buffer);
		safe_unpack_time(&credential->ctime, buffer);
		safe_unpack32(&tot_core_cnt, buffer);
		unpack_bit_str_hex(&cred->job_core_bitmap, buffer);
		unpack_bit_str_hex(&cred->step_core_bitmap, buffer);
		safe_unpack16(&cred->core_array_size, buffer);
		if (cred->core_array_size) {
			safe_unpack16_array(&cred->cores_per_socket, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
			safe_unpack16_array(&cred->sockets_per_node, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
			safe_unpack32_array(&cred->sock_core_rep_count, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
		}
		safe_unpack32(&cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			safe_unpack16_array(&cred->cpu_array, &len, buffer);
			if (len != cred->cpu_array_count)
				goto unpack_error;
			safe_unpack32_array(&cred->cpu_array_reps, &len,
					    buffer);
			if (len != cred->cpu_array_count)
				goto unpack_error;
		}
		safe_unpack32(&cred->job_nhosts, buffer);
		safe_unpack32(&cred->job_ntasks, buffer);
		safe_unpackstr_xmalloc(&cred->job_hostlist, &len, buffer);

		safe_unpack32(&cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			safe_unpack64_array(&cred->job_mem_alloc, &len, buffer);
			if (len != cred->job_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred->job_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred->job_mem_alloc_size)
				goto unpack_error;

		}

		safe_unpack32(&cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			safe_unpack64_array(&cred->step_mem_alloc, &len,
					    buffer);
			if (len != cred->step_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred->step_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred->step_mem_alloc_size)
				goto unpack_error;
		}

		safe_unpackstr_xmalloc(&cred->selinux_context, &len, buffer);

		/* "sigp" must be last */
		cred_len = get_buf_offset(buffer) - cred_start;
		sigp = (char **) &credential->signature;
		safe_unpackmem_xmalloc(sigp, &len, buffer);
		credential->siglen = len;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		cred->job_restart_cnt = INFINITE16;
		if (unpack_step_id_members(&cred->step_id, buffer,
					   protocol_version) != SLURM_SUCCESS)
			goto unpack_error;
		safe_unpack32(&cred->uid, buffer);
		if (cred->uid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid user nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpack32(&cred->gid, buffer);
		if (cred->gid == SLURM_AUTH_NOBODY) {
			error("%s: refusing to unpack credential for invalid group nobody",
			      __func__);
			goto unpack_error;
		}

		safe_unpackstr_xmalloc(&cred->pw_name, &len, buffer);
		safe_unpackstr_xmalloc(&cred->pw_gecos, &len, buffer);
		safe_unpackstr_xmalloc(&cred->pw_dir, &len, buffer);
		safe_unpackstr_xmalloc(&cred->pw_shell, &len, buffer);
		safe_unpack32_array(&cred->gids, &u32_ngids, buffer);
		cred->ngids = u32_ngids;
		safe_unpackstr_array(&cred->gr_names, &u32_ngids, buffer);
		if (u32_ngids && cred->ngids != u32_ngids) {
			error("%s: mismatch on gr_names array, %u != %u",
			      __func__, u32_ngids, cred->ngids);
			goto unpack_error;
		}
		if (gres_job_state_unpack(&cred->job_gres_list, buffer,
					  cred->step_id.job_id,
					  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_step_state_unpack(&cred->step_gres_list,
					   buffer, &cred->step_id,
					   protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred->job_core_spec, buffer);
		safe_unpackstr_xmalloc(&cred->job_constraints, &len, buffer);
		safe_unpackstr_xmalloc(&cred->step_hostlist, &len, buffer);
		safe_unpack16(&cred->x11, buffer);
		safe_unpack_time(&credential->ctime, buffer);
		safe_unpack32(&tot_core_cnt, buffer);
		unpack_bit_str_hex(&cred->job_core_bitmap, buffer);
		unpack_bit_str_hex(&cred->step_core_bitmap, buffer);
		safe_unpack16(&cred->core_array_size, buffer);
		if (cred->core_array_size) {
			safe_unpack16_array(&cred->cores_per_socket, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
			safe_unpack16_array(&cred->sockets_per_node, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
			safe_unpack32_array(&cred->sock_core_rep_count, &len,
					    buffer);
			if (len != cred->core_array_size)
				goto unpack_error;
		}
		safe_unpack32(&cred->job_nhosts, buffer);
		safe_unpackstr_xmalloc(&cred->job_hostlist, &len, buffer);

		safe_unpack32(&cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			safe_unpack64_array(&cred->job_mem_alloc, &len, buffer);
			if (len != cred->job_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred->job_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred->job_mem_alloc_size)
				goto unpack_error;

		}

		safe_unpack32(&cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			safe_unpack64_array(&cred->step_mem_alloc, &len, buffer);
			if (len != cred->step_mem_alloc_size)
				goto unpack_error;

			safe_unpack32_array(&cred->step_mem_alloc_rep_count,
					    &len, buffer);
			if (len != cred->step_mem_alloc_size)
				goto unpack_error;
		}

		safe_unpackstr_xmalloc(&cred->selinux_context, &len, buffer);

		/* "sigp" must be last */
		cred_len = get_buf_offset(buffer) - cred_start;
		sigp = (char **) &credential->signature;
		safe_unpackmem_xmalloc(sigp, &len, buffer);
		credential->siglen = len;
		xassert(len > 0);
	} else {
		error("slurm_cred_unpack: protocol_version"
		      " %hu not supported", protocol_version);
		goto unpack_error;
	}

	/*
	 * Both srun and slurmd will unpack the credential just to pack it
	 * again. Hold onto a buffer with the pre-packed representation.
	 */
	if (!running_in_slurmstepd()) {
		credential->buffer = init_buf(cred_len);
		credential->buf_version = protocol_version;
		memcpy(credential->buffer->head,
		       get_buf_data(buffer) + cred_start,
		       cred_len);
		credential->buffer->processed = cred_len;
	}

	/*
	 * Using the saved position, verify the credential.
	 * This avoids needing to re-pack the entire thing just to
	 * cross-check that the signature matches up later.
	 * (Only done in slurmd.)
	 */
	if (credential->siglen && verifier_ctx)
		_cred_verify_signature(verifier_ctx, credential);

	return credential;

unpack_error:
	xfree(bit_fmt_str);
	slurm_cred_destroy(credential);
	return NULL;
}

int slurm_cred_ctx_pack(slurm_cred_ctx_t ctx, buf_t *buffer)
{
	slurm_mutex_lock(&ctx->mutex);
	_job_state_pack(ctx, buffer);
	_cred_state_pack(ctx, buffer);
	slurm_mutex_unlock(&ctx->mutex);

	return SLURM_SUCCESS;
}

int slurm_cred_ctx_unpack(slurm_cred_ctx_t ctx, buf_t *buffer)
{
	xassert(ctx != NULL);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	slurm_mutex_lock(&ctx->mutex);

	/*
	 * Unpack job state list and cred state list from buffer
	 * appening them onto ctx->state_list and ctx->job_list.
	 */
	_job_state_unpack(ctx, buffer);
	_cred_state_unpack(ctx, buffer);

	slurm_mutex_unlock(&ctx->mutex);

	return SLURM_SUCCESS;
}

static void
_verifier_ctx_init(slurm_cred_ctx_t ctx)
{
	xassert(ctx != NULL);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type == SLURM_CRED_VERIFIER);

	ctx->job_list   = list_create((ListDelF) _job_state_destroy);
	ctx->state_list = list_create(xfree_ptr);

	return;
}


static int
_ctx_update_private_key(slurm_cred_ctx_t ctx, const char *path)
{
	void *pk   = NULL;
	void *tmpk = NULL;

	xassert(ctx != NULL);

	pk = (*(ops.cred_read_private_key))(path);
	if (!pk)
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_CREATOR);

	tmpk = ctx->key;
	ctx->key = pk;

	slurm_mutex_unlock(&ctx->mutex);

	(*(ops.cred_destroy_key))(tmpk);

	return SLURM_SUCCESS;
}


static int
_ctx_update_public_key(slurm_cred_ctx_t ctx, const char *path)
{
	void *pk   = NULL;

	xassert(ctx != NULL);
	pk = (*(ops.cred_read_public_key))(path);
	if (!pk)
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	if (ctx->exkey)
		(*(ops.cred_destroy_key))(ctx->exkey);

	ctx->exkey = ctx->key;
	ctx->key   = pk;

	/*
	 * exkey expires in expiry_window seconds plus one minute.
	 * This should be long enough to capture any keys in-flight.
	 */
	ctx->exkey_exp = time(NULL) + ctx->expiry_window + 60;

	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_SUCCESS;
}


static bool
_exkey_is_valid(slurm_cred_ctx_t ctx)
{
	if (!ctx->exkey)
		return false;

	if (time(NULL) > ctx->exkey_exp) {
		debug2("old job credential key slurmd expired");
		(*(ops.cred_destroy_key))(ctx->exkey);
		ctx->exkey = NULL;
		return false;
	}

	return true;
}


static slurm_cred_ctx_t
_slurm_cred_ctx_alloc(void)
{
	slurm_cred_ctx_t ctx = xmalloc(sizeof(*ctx));
	/* Contents initialized to zero */

	slurm_mutex_init(&ctx->mutex);

	ctx->magic = CRED_CTX_MAGIC;
	ctx->expiry_window = cred_expire;
	ctx->exkey_exp     = (time_t) -1;

	return ctx;
}

static slurm_cred_t *_slurm_cred_alloc(bool alloc_arg)
{
	slurm_cred_t *cred = xmalloc(sizeof(*cred));
	/* Contents initialized to zero */

	slurm_rwlock_init(&cred->mutex);

	if (alloc_arg) {
		cred->arg = xmalloc(sizeof(slurm_cred_arg_t));
		cred->arg->uid = SLURM_AUTH_NOBODY;
		cred->arg->gid = SLURM_AUTH_NOBODY;
	}

	cred->verified = false;

	cred->magic = CRED_MAGIC;

	return cred;
}

static int _cred_sign(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	int rc;

	rc = (*(ops.cred_sign))(ctx->key,
				get_buf_data(cred->buffer),
				get_buf_offset(cred->buffer),
				&cred->signature,
				&cred->siglen);

	if (rc) {
		error("Credential sign: %s",
		      (*(ops.cred_str_error))(rc));
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static void _cred_verify_signature(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	int rc;
	void *start = get_buf_data(cred->buffer);
	uint32_t len = get_buf_offset(cred->buffer);

	debug("Checking credential with %u bytes of sig data", cred->siglen);

	rc = (*(ops.cred_verify_sign))(ctx->key, start, len,
				       cred->signature,
				       cred->siglen);
	if (rc && _exkey_is_valid(ctx)) {
		rc = (*(ops.cred_verify_sign))(ctx->exkey, start, len,
					       cred->signature,
					       cred->siglen);
	}

	if (rc) {
		error("Credential signature check: %s",
		      (*(ops.cred_str_error))(rc));
		return;
	}

	cred->verified = true;
}


static void _pack_cred(slurm_cred_arg_t *cred, buf_t *buffer,
		       uint16_t protocol_version)
{
	uint32_t tot_core_cnt = 0;
	/*
	 * The gr_names array is optional. If the array exists the length
	 * must match that of the gids array.
	 */
	uint32_t gr_names_cnt = (cred->gr_names) ? cred->ngids : 0;
	time_t ctime = time(NULL);

	if (protocol_version >= SLURM_23_02_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->pw_name, buffer);
		packstr(cred->pw_gecos, buffer);
		packstr(cred->pw_dir, buffer);
		packstr(cred->pw_shell, buffer);
		pack32_array(cred->gids, cred->ngids, buffer);
		packstr_array(cred->gr_names, gr_names_cnt, buffer);

		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);
		pack16(cred->job_core_spec, buffer);
		packstr(cred->job_account, buffer);
		packstr(cred->job_alias_list, buffer);
		packstr(cred->job_comment, buffer);
		packstr(cred->job_constraints, buffer);
		pack_time(cred->job_end_time, buffer);
		packstr(cred->job_extra, buffer);
		pack16(cred->job_oversubscribe, buffer);
		packstr(cred->job_partition, buffer);
		packstr(cred->job_reservation, buffer);
		pack16(cred->job_restart_cnt, buffer);
		pack_time(cred->job_start_time, buffer);
		packstr(cred->job_std_err, buffer);
		packstr(cred->job_std_in, buffer);
		packstr(cred->job_std_out, buffer);
		packstr(cred->step_hostlist, buffer);
		pack16(cred->x11, buffer);
		pack_time(ctime, buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);
		pack16(cred->core_array_size, buffer);
		if (cred->core_array_size) {
			pack16_array(cred->cores_per_socket,
				     cred->core_array_size,
				     buffer);
			pack16_array(cred->sockets_per_node,
				     cred->core_array_size,
				     buffer);
			pack32_array(cred->sock_core_rep_count,
				     cred->core_array_size,
				     buffer);
		}
		pack32(cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			pack16_array(cred->cpu_array,
				     cred->cpu_array_count,
				     buffer);
			pack32_array(cred->cpu_array_reps,
				     cred->cpu_array_count,
				     buffer);
		}
		pack32(cred->job_nhosts, buffer);
		pack32(cred->job_ntasks, buffer);
		packstr(cred->job_hostlist, buffer);
		packstr(cred->job_licenses, buffer);
		pack32(cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			pack64_array(cred->job_mem_alloc,
				     cred->job_mem_alloc_size,
				     buffer);
			pack32_array(cred->job_mem_alloc_rep_count,
				     cred->job_mem_alloc_size,
				     buffer);
		}
		pack32(cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			pack64_array(cred->step_mem_alloc,
				     cred->step_mem_alloc_size,
				     buffer);
			pack32_array(cred->step_mem_alloc_rep_count,
				     cred->step_mem_alloc_size,
				     buffer);
		}
		packstr(cred->selinux_context, buffer);
	} else if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->pw_name, buffer);
		packstr(cred->pw_gecos, buffer);
		packstr(cred->pw_dir, buffer);
		packstr(cred->pw_shell, buffer);
		pack32_array(cred->gids, cred->ngids, buffer);
		packstr_array(cred->gr_names, gr_names_cnt, buffer);

		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);
		pack16(cred->job_core_spec, buffer);
		packstr(cred->job_account, buffer);
		packstr(cred->job_alias_list, buffer);
		packstr(cred->job_comment, buffer);
		packstr(cred->job_constraints, buffer);
		packstr(cred->job_partition, buffer);
		packstr(cred->job_reservation, buffer);
		pack16(cred->job_restart_cnt, buffer);
		packstr(cred->job_std_err, buffer);
		packstr(cred->job_std_in, buffer);
		packstr(cred->job_std_out, buffer);
		packstr(cred->step_hostlist, buffer);
		pack16(cred->x11, buffer);
		pack_time(ctime, buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);
		pack16(cred->core_array_size, buffer);
		if (cred->core_array_size) {
			pack16_array(cred->cores_per_socket,
				     cred->core_array_size,
				     buffer);
			pack16_array(cred->sockets_per_node,
				     cred->core_array_size,
				     buffer);
			pack32_array(cred->sock_core_rep_count,
				     cred->core_array_size,
				     buffer);
		}
		pack32(cred->cpu_array_count, buffer);
		if (cred->cpu_array_count) {
			pack16_array(cred->cpu_array,
				     cred->cpu_array_count,
				     buffer);
			pack32_array(cred->cpu_array_reps,
				     cred->cpu_array_count,
				     buffer);
		}
		pack32(cred->job_nhosts, buffer);
		pack32(cred->job_ntasks, buffer);
		packstr(cred->job_hostlist, buffer);
		pack32(cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			pack64_array(cred->job_mem_alloc,
				     cred->job_mem_alloc_size,
				     buffer);
			pack32_array(cred->job_mem_alloc_rep_count,
				     cred->job_mem_alloc_size,
				     buffer);
		}
		pack32(cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			pack64_array(cred->step_mem_alloc,
				     cred->step_mem_alloc_size,
				     buffer);
			pack32_array(cred->step_mem_alloc_rep_count,
				     cred->step_mem_alloc_size,
				     buffer);
		}
		packstr(cred->selinux_context, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_step_id(&cred->step_id, buffer, protocol_version);
		pack32(cred->uid, buffer);
		pack32(cred->gid, buffer);
		packstr(cred->pw_name, buffer);
		packstr(cred->pw_gecos, buffer);
		packstr(cred->pw_dir, buffer);
		packstr(cred->pw_shell, buffer);
		pack32_array(cred->gids, cred->ngids, buffer);
		packstr_array(cred->gr_names, gr_names_cnt, buffer);

		(void) gres_job_state_pack(cred->job_gres_list, buffer,
					   cred->step_id.job_id, false,
					   protocol_version);
		gres_step_state_pack(cred->step_gres_list, buffer,
				     &cred->step_id, protocol_version);
		pack16(cred->job_core_spec, buffer);
		packstr(cred->job_constraints, buffer);
		packstr(cred->step_hostlist, buffer);
		pack16(cred->x11, buffer);
		pack_time(ctime, buffer);

		if (cred->job_core_bitmap)
			tot_core_cnt = bit_size(cred->job_core_bitmap);
		pack32(tot_core_cnt, buffer);
		pack_bit_str_hex(cred->job_core_bitmap, buffer);
		pack_bit_str_hex(cred->step_core_bitmap, buffer);
		pack16(cred->core_array_size, buffer);
		if (cred->core_array_size) {
			pack16_array(cred->cores_per_socket,
				     cred->core_array_size,
				     buffer);
			pack16_array(cred->sockets_per_node,
				     cred->core_array_size,
				     buffer);
			pack32_array(cred->sock_core_rep_count,
				     cred->core_array_size,
				     buffer);
		}
		pack32(cred->job_nhosts, buffer);
		packstr(cred->job_hostlist, buffer);
		pack32(cred->job_mem_alloc_size, buffer);
		if (cred->job_mem_alloc_size) {
			pack64_array(cred->job_mem_alloc,
				     cred->job_mem_alloc_size,
				     buffer);
			pack32_array(cred->job_mem_alloc_rep_count,
				     cred->job_mem_alloc_size,
				     buffer);
		}
		pack32(cred->step_mem_alloc_size, buffer);
		if (cred->step_mem_alloc_size) {
			pack64_array(cred->step_mem_alloc,
				     cred->step_mem_alloc_size,
				     buffer);
			pack32_array(cred->step_mem_alloc_rep_count,
				     cred->step_mem_alloc_size,
				     buffer);
		}
		packstr(cred->selinux_context, buffer);
	}
}

static int _list_find_cred_state(void *x, void *key)
{
	cred_state_t *s = (cred_state_t *) x;
	slurm_cred_t *cred = (slurm_cred_t *) key;

	if (!memcmp(&s->step_id, &cred->arg->step_id, sizeof(s->step_id)) &&
	    (s->ctime == cred->ctime))
		return 1;

	return 0;
}


static bool
_credential_replayed(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	cred_state_t *s = NULL;

	_clear_expired_credential_states(ctx);

	s = list_find_first(ctx->state_list, _list_find_cred_state, cred);

	/*
	 * If we found a match, this credential is being replayed.
	 */
	if (s)
		return true;

	/*
	 * Otherwise, save the credential state
	 */
	_insert_cred_state(ctx, cred);
	return false;
}

extern void
slurm_cred_handle_reissue(slurm_cred_ctx_t ctx, slurm_cred_t *cred, bool locked)
{
	job_state_t *j;

	if (!locked)
		slurm_mutex_lock(&ctx->mutex);

	j = _find_job_state(ctx, cred->arg->step_id.job_id);

	if (j != NULL && j->revoked && (cred->ctime > j->revoked)) {
		/* The credential has been reissued.  Purge the
		 * old record so that "cred" will look like a new
		 * credential to any ensuing commands. */
		info("reissued job credential for job %u", j->jobid);

		/* Setting j->expiration to zero will make
		 * _clear_expired_job_states() remove this
		 * job credential from the cred context. */
		j->expiration = 0;
		_clear_expired_job_states(ctx);
	}
	if (!locked)
		slurm_mutex_unlock(&ctx->mutex);
}

extern bool
slurm_cred_revoked(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	job_state_t *j;
	bool rc = false;

	slurm_mutex_lock(&ctx->mutex);

	j = _find_job_state(ctx, cred->arg->step_id.job_id);

	if (j && (j->revoked != (time_t)0) && (cred->ctime <= j->revoked))
		rc = true;

	slurm_mutex_unlock(&ctx->mutex);

	return rc;
}

static bool
_credential_revoked(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	job_state_t  *j = NULL;

	_clear_expired_job_states(ctx);

	if (!(j = _find_job_state(ctx, cred->arg->step_id.job_id))) {
		(void) _insert_job_state(ctx, cred->arg->step_id.job_id);
		return false;
	}

	if (cred->ctime <= j->revoked) {
		debug3("cred for %u revoked. expires at %ld UTS",
		       j->jobid, j->expiration);
		return true;
	}

	return false;
}

static int _list_find_job_state(void *x, void *key)
{
	job_state_t *j = (job_state_t *) x;
	uint32_t jobid = *(uint32_t *) key;
	if (j->jobid == jobid)
		return 1;
	return 0;
}

static job_state_t *
_find_job_state(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	job_state_t *j =
		list_find_first(ctx->job_list, _list_find_job_state, &jobid);
	return j;
}

static job_state_t *
_insert_job_state(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	job_state_t *j = list_find_first(
		ctx->job_list, _list_find_job_state, &jobid);;
	if (!j) {
		j = _job_state_create(jobid);
		list_append(ctx->job_list, j);
	} else
		debug2("%s: we already have a job state for job %u.  No big deal, just an FYI.",
		       __func__, jobid);
	return j;
}


static job_state_t *
_job_state_create(uint32_t jobid)
{
	job_state_t *j = xmalloc(sizeof(*j));

	j->jobid      = jobid;
	j->revoked    = (time_t) 0;
	j->ctime      = time(NULL);
	j->expiration = (time_t) MAX_TIME;

	return j;
}

static void
_job_state_destroy(job_state_t *j)
{
	debug3 ("destroying job %u state", j->jobid);
	xfree(j);
}

static int _list_find_expired_job_state(void *x, void *key)
{
	job_state_t *j = x;
	time_t curr_time = *(time_t *)key;

	if (j->revoked && (curr_time > j->expiration))
		return 1;
	return 0;
}

static void
_clear_expired_job_states(slurm_cred_ctx_t ctx)
{
	time_t        now = time(NULL);

	list_delete_all(ctx->job_list, _list_find_expired_job_state, &now);
}

static int _list_find_expired_cred_state(void *x, void *key)
{
	cred_state_t *s = (cred_state_t *)x;
	time_t curr_time = *(time_t *)key;

	if (curr_time > s->expiration)
		return 1;
	return 0;
}

static void
_clear_expired_credential_states(slurm_cred_ctx_t ctx)
{
	time_t        now = time(NULL);

	list_delete_all(ctx->state_list, _list_find_expired_cred_state, &now);
}


static void
_insert_cred_state(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	cred_state_t *s = _cred_state_create(ctx, cred);
	list_append(ctx->state_list, s);
}


static cred_state_t *
_cred_state_create(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	memcpy(&s->step_id, &cred->arg->step_id, sizeof(s->step_id));
	s->ctime      = cred->ctime;
	s->expiration = cred->ctime + ctx->expiry_window;

	return s;
}

static int _cred_state_pack_one(void *x, void *key)
{
	cred_state_t *s = x;
	buf_t *buffer = key;

	pack_step_id(&s->step_id, buffer, SLURM_PROTOCOL_VERSION);
	pack_time(s->ctime, buffer);
	pack_time(s->expiration, buffer);

	return SLURM_SUCCESS;
}


static cred_state_t *_cred_state_unpack_one(buf_t *buffer)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	if (unpack_step_id_members(&s->step_id, buffer,
				   SLURM_PROTOCOL_VERSION) != SLURM_SUCCESS)
		goto unpack_error;
	safe_unpack_time(&s->ctime, buffer);
	safe_unpack_time(&s->expiration, buffer);
	return s;

unpack_error:
	xfree(s);
	return NULL;
}

static int _job_state_pack_one(void *x, void *key)
{
	job_state_t *j = x;
	buf_t *buffer = key;

	pack32(j->jobid, buffer);
	pack_time(j->revoked, buffer);
	pack_time(j->ctime, buffer);
	pack_time(j->expiration, buffer);

	return SLURM_SUCCESS;
}


static job_state_t *_job_state_unpack_one(buf_t *buffer)
{
	job_state_t *j = xmalloc(sizeof(*j));

	safe_unpack32(&j->jobid, buffer);
	safe_unpack_time(&j->revoked, buffer);
	safe_unpack_time(&j->ctime, buffer);
	safe_unpack_time(&j->expiration, buffer);

	debug3("cred_unpack: job %u ctime:%ld revoked:%ld expires:%ld",
	       j->jobid, j->ctime, j->revoked, j->expiration);

	if ((j->revoked) && (j->expiration == (time_t) MAX_TIME)) {
		warning("revoke on job %u has no expiration", j->jobid);
		j->expiration = j->revoked + 600;
	}

	return j;

unpack_error:
	_job_state_destroy(j);
	return NULL;
}


static void _cred_state_pack(slurm_cred_ctx_t ctx, buf_t *buffer)
{
	pack32(list_count(ctx->state_list), buffer);

	list_for_each(ctx->state_list, _cred_state_pack_one, buffer);
}


static void _cred_state_unpack(slurm_cred_ctx_t ctx, buf_t *buffer)
{
	time_t        now = time(NULL);
	uint32_t      n;
	int           i   = 0;
	cred_state_t *s   = NULL;

	safe_unpack32(&n, buffer);
	if (n > NO_VAL)
		goto unpack_error;
	for (i = 0; i < n; i++) {
		if (!(s = _cred_state_unpack_one(buffer)))
			goto unpack_error;

		if (now < s->expiration)
			list_append(ctx->state_list, s);
		else
			xfree(s);
	}

	return;

unpack_error:
	error("Unable to unpack job credential state information");
	return;
}


static void _job_state_pack(slurm_cred_ctx_t ctx, buf_t *buffer)
{
	pack32((uint32_t) list_count(ctx->job_list), buffer);

	list_for_each(ctx->job_list, _job_state_pack_one, buffer);
}


static void _job_state_unpack(slurm_cred_ctx_t ctx, buf_t *buffer)
{
	time_t       now = time(NULL);
	uint32_t     n   = 0;
	int          i   = 0;
	job_state_t *j   = NULL;

	safe_unpack32(&n, buffer);
	if (n > NO_VAL)
		goto unpack_error;
	for (i = 0; i < n; i++) {
		if (!(j = _job_state_unpack_one(buffer)))
			goto unpack_error;

		if (!j->revoked || (j->revoked && (now < j->expiration)))
			list_append(ctx->job_list, j);
		else {
			debug3 ("not appending expired job %u state",
			        j->jobid);
			_job_state_destroy(j);
		}
	}

	return;

unpack_error:
	error("Unable to unpack job state information");
	return;
}

/*****************************************************************************\
 *****************       SBCAST CREDENTIAL FUNCTIONS        ******************
\*****************************************************************************/

/* Pack sbcast credential without the digital signature */
static void _pack_sbcast_cred(sbcast_cred_t *sbcast_cred, buf_t *buffer,
			      uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack_time(sbcast_cred->ctime, buffer);
		pack_time(sbcast_cred->expiration, buffer);
		pack32(sbcast_cred->jobid, buffer);
		pack32(sbcast_cred->het_job_id, buffer);
		pack32(sbcast_cred->step_id, buffer);
		pack32(sbcast_cred->uid, buffer);
		pack32(sbcast_cred->gid, buffer);
		packstr(sbcast_cred->user_name, buffer);
		pack32_array(sbcast_cred->gids, sbcast_cred->ngids, buffer);
		packstr(sbcast_cred->nodes, buffer);
	}
}

/* Create an sbcast credential for the specified job and nodes
 *	including digital signature.
 * RET the sbcast credential or NULL on error */
sbcast_cred_t *create_sbcast_cred(slurm_cred_ctx_t ctx,
				  sbcast_cred_arg_t *arg,
				  uint16_t protocol_version)
{
	buf_t *buffer;
	int rc;
	sbcast_cred_t *sbcast_cred;

	xassert(ctx);
	xassert(g_context);

	sbcast_cred = xmalloc(sizeof(struct sbcast_cred));
	sbcast_cred->ctime = time(NULL);
	sbcast_cred->expiration = arg->expiration;
	sbcast_cred->jobid = arg->job_id;
	sbcast_cred->het_job_id = arg->het_job_id;
	sbcast_cred->step_id = arg->step_id;
	sbcast_cred->uid = arg->uid;
	sbcast_cred->gid = arg->gid;
	sbcast_cred->user_name = xstrdup(arg->user_name);
	sbcast_cred->ngids = arg->ngids;
	sbcast_cred->gids = copy_gids(arg->ngids, arg->gids);
	sbcast_cred->nodes = xstrdup(arg->nodes);

	if (enable_send_gids) {
		/* this may still be null, in which case slurmd will handle */
		sbcast_cred->user_name = uid_to_string_or_null(arg->uid);
		/* lookup and send extended gids list */
		sbcast_cred->ngids = group_cache_lookup(arg->uid, arg->gid,
							sbcast_cred->user_name,
							&sbcast_cred->gids);
	}

	buffer = init_buf(4096);
	_pack_sbcast_cred(sbcast_cred, buffer, protocol_version);
	rc = (*(ops.cred_sign))(
		ctx->key, get_buf_data(buffer), get_buf_offset(buffer),
		&sbcast_cred->signature, &sbcast_cred->siglen);
	FREE_NULL_BUFFER(buffer);

	if (rc) {
		error("sbcast_cred sign: %s",
		      (*(ops.cred_str_error))(rc));
		delete_sbcast_cred(sbcast_cred);
		return NULL;
	}

	return sbcast_cred;
}

/* Delete an sbcast credential created using create_sbcast_cred() or
 *	unpack_sbcast_cred() */
void delete_sbcast_cred(sbcast_cred_t *sbcast_cred)
{
	if (!sbcast_cred)
		return;

	xfree(sbcast_cred->gids);
	xfree(sbcast_cred->user_name);
	xfree(sbcast_cred->nodes);
	xfree(sbcast_cred->signature);
	xfree(sbcast_cred);
}

static void _sbast_cache_add(sbcast_cred_t *sbcast_cred)
{
	int i;
	uint32_t sig_num = 0;
	struct sbcast_cache *new_cache_rec;

	/* Using two bytes at a time gives us a larger number
	 * and reduces the possibility of a duplicate value */
	for (i = 0; i < sbcast_cred->siglen; i += 2) {
		sig_num += (sbcast_cred->signature[i] << 8) +
			   sbcast_cred->signature[i+1];
	}

	new_cache_rec = xmalloc(sizeof(struct sbcast_cache));
	new_cache_rec->expire = sbcast_cred->expiration;
	new_cache_rec->value  = sig_num;
	list_append(sbcast_cache_list, new_cache_rec);
}

/* Extract contents of an sbcast credential verifying the digital signature.
 * NOTE: We can only perform the full credential validation once with
 *	Munge without generating a credential replay error, so we only
 *	verify the credential for block one of the executable file. All other
 *	blocks or shared object files must have a recent signature on file
 *	(in our cache) or the slurmd must have recently been restarted.
 * RET 0 on success, -1 on error */
sbcast_cred_arg_t *extract_sbcast_cred(slurm_cred_ctx_t ctx,
				       sbcast_cred_t *sbcast_cred,
				       uint16_t block_no,
				       uint16_t flags,
				       uint16_t protocol_version)
{
	sbcast_cred_arg_t *arg;
	struct sbcast_cache *next_cache_rec;
	uint32_t sig_num = 0;
	int i, rc;
	time_t now = time(NULL);
	buf_t *buffer;

	xassert(ctx);

	xassert(g_context);

	if (now > sbcast_cred->expiration)
		return NULL;

	if (block_no == 1 && !(flags & FILE_BCAST_SO)) {
		buffer = init_buf(4096);
		_pack_sbcast_cred(sbcast_cred, buffer, protocol_version);
		/* NOTE: the verification checks that the credential was
		 * created by SlurmUser or root */
		rc = (*(ops.cred_verify_sign)) (
			ctx->key, get_buf_data(buffer), get_buf_offset(buffer),
			sbcast_cred->signature, sbcast_cred->siglen);
		FREE_NULL_BUFFER(buffer);

		if (rc) {
			error("sbcast_cred verify: %s",
			      (*(ops.cred_str_error))(rc));
			return NULL;
		}
		_sbast_cache_add(sbcast_cred);

	} else {
		char *err_str = NULL;
		bool cache_match_found = false;
		ListIterator sbcast_iter;
		for (i = 0; i < sbcast_cred->siglen; i += 2) {
			sig_num += (sbcast_cred->signature[i] << 8) +
				   sbcast_cred->signature[i+1];
		}

		sbcast_iter = list_iterator_create(sbcast_cache_list);
		while ((next_cache_rec =
			(struct sbcast_cache *) list_next(sbcast_iter))) {
			if ((next_cache_rec->expire == sbcast_cred->expiration) &&
			    (next_cache_rec->value  == sig_num)) {
				cache_match_found = true;
				break;
			}
			if (next_cache_rec->expire <= now)
				list_delete_item(sbcast_iter);
		}
		list_iterator_destroy(sbcast_iter);

		if (!cache_match_found) {
			error("sbcast_cred verify: signature not in cache");
			if (SLURM_DIFFTIME(now, cred_restart_time) > 60)
				return NULL;	/* restarted >60 secs ago */
			buffer = init_buf(4096);
			_pack_sbcast_cred(sbcast_cred, buffer,
					  protocol_version);
			rc = (*(ops.cred_verify_sign)) (
				ctx->key, get_buf_data(buffer),
				get_buf_offset(buffer),
				sbcast_cred->signature, sbcast_cred->siglen);
			FREE_NULL_BUFFER(buffer);
			if (rc)
				err_str = (char *)(*(ops.cred_str_error))(rc);
			if (err_str && xstrcmp(err_str, "Credential replayed")){
				error("sbcast_cred verify: %s", err_str);
				return NULL;
			}
			info("sbcast_cred verify: signature revalidated");
			_sbast_cache_add(sbcast_cred);
		}
	}

	if (sbcast_cred->uid == SLURM_AUTH_NOBODY) {
		error("%s: refusing to create bcast credential for invalid user nobody",
		      __func__);
		return NULL;
	}

	if (sbcast_cred->gid == SLURM_AUTH_NOBODY) {
		error("%s: refusing to create bcast credential for invalid group nobody",
		      __func__);
		return NULL;
	}

	arg = xmalloc(sizeof(sbcast_cred_arg_t));
	arg->job_id = sbcast_cred->jobid;
	arg->step_id = sbcast_cred->step_id;
	arg->uid = sbcast_cred->uid;
	arg->gid = sbcast_cred->gid;
	arg->user_name = xstrdup(sbcast_cred->user_name);
	arg->ngids = sbcast_cred->ngids;
	arg->gids = copy_gids(sbcast_cred->ngids, sbcast_cred->gids);
	arg->nodes = xstrdup(sbcast_cred->nodes);
	return arg;
}

/* Pack an sbcast credential into a buffer including the digital signature */
void pack_sbcast_cred(sbcast_cred_t *sbcast_cred, buf_t *buffer,
		      uint16_t protocol_version)
{
	xassert(sbcast_cred);
	xassert(sbcast_cred->siglen > 0);

	_pack_sbcast_cred(sbcast_cred, buffer, protocol_version);
	packmem(sbcast_cred->signature, sbcast_cred->siglen, buffer);
}

/* Unpack an sbcast credential into a buffer including the digital signature */
sbcast_cred_t *unpack_sbcast_cred(buf_t *buffer, uint16_t protocol_version)
{
	sbcast_cred_t *sbcast_cred;
	uint32_t uint32_tmp;

	sbcast_cred = xmalloc(sizeof(struct sbcast_cred));
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack_time(&sbcast_cred->ctime, buffer);
		safe_unpack_time(&sbcast_cred->expiration, buffer);
		safe_unpack32(&sbcast_cred->jobid, buffer);
		safe_unpack32(&sbcast_cred->het_job_id, buffer);
		safe_unpack32(&sbcast_cred->step_id, buffer);
		safe_unpack32(&sbcast_cred->uid, buffer);
		safe_unpack32(&sbcast_cred->gid, buffer);
		safe_unpackstr_xmalloc(&sbcast_cred->user_name, &uint32_tmp,
				       buffer);
		safe_unpack32_array(&sbcast_cred->gids, &sbcast_cred->ngids,
				    buffer);
		safe_unpackstr_xmalloc(&sbcast_cred->nodes, &uint32_tmp, buffer);

		/* "sigp" must be last */
		safe_unpackmem_xmalloc(&sbcast_cred->signature,
				       &sbcast_cred->siglen, buffer);
		if (!sbcast_cred->siglen)
			goto unpack_error;
	} else
		goto unpack_error;

	return sbcast_cred;

unpack_error:
	delete_sbcast_cred(sbcast_cred);
	return NULL;
}

void  print_sbcast_cred(sbcast_cred_t *sbcast_cred)
{
	info("Sbcast_cred: JobId   %u", sbcast_cred->jobid);
	info("Sbcast_cred: StepId  %u", sbcast_cred->step_id);
	info("Sbcast_cred: Nodes   %s", sbcast_cred->nodes);
	info("Sbcast_cred: ctime   %s", slurm_ctime2(&sbcast_cred->ctime));
	info("Sbcast_cred: Expire  %s", slurm_ctime2(&sbcast_cred->expiration));
}

void sbcast_cred_arg_free(sbcast_cred_arg_t *arg)
{
	if (!arg)
		return;

	xfree(arg->gids);
	xfree(arg->nodes);
	xfree(arg->user_name);
	xfree(arg);
}

extern bool slurm_cred_send_gids_enabled(void)
{
	if (_slurm_cred_init() < 0)
		return SLURM_ERROR;

	return enable_send_gids;
}
