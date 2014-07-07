/*****************************************************************************\
 *  src/common/slurm_cred.c - SLURM job and sbcast credential functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>

#if WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/gres.h"
#include "src/common/io_hdr.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifndef __sbcast_cred_t_defined
#  define  __sbcast_cred_t_defined
typedef struct sbcast_cred sbcast_cred_t;		/* opaque data type */
#endif

/*
 * Default credential information expiration window.
 * Long enough for loading user environment, running prolog,
 * and dealing with the slurmd getting paged out of memory.
 */
#define DEFAULT_EXPIRATION_WINDOW 1200

#define EXTREME_DEBUG   0
#define MAX_TIME 0x7fffffff

/*
 * slurm job credential state
 *
 */
typedef struct {
	time_t   ctime;		/* Time that the cred was created	*/
	time_t   expiration;    /* Time at which cred is no longer good	*/
	uint32_t jobid;		/* SLURM job id for this credential	*/
	uint32_t stepid;	/* SLURM step id for this credential	*/
} cred_state_t;

/*
 * slurm job state information
 * tracks jobids for which all future credentials have been revoked
 *
 */
typedef struct {
	time_t   ctime;         /* Time that this entry was created         */
	time_t   expiration;    /* Time at which credentials can be purged  */
	uint32_t jobid;         /* SLURM job id for this credential	*/
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
	time_t       ctime;	/* Time that the cred was created	*/
	time_t       expiration; /* Time at which cred is no longer good*/
	uint32_t     jobid;	/* SLURM job id for this credential	*/
	char *       nodes;	/* nodes for which credential is valid	*/
	char        *signature;	/* credential signature			*/
	unsigned int siglen;	/* signature length in bytes		*/
};

/*
 * Credential context, slurm_cred_ctx_t:
 */
struct slurm_cred_context {
#ifndef NDEBUG
#  define CRED_CTX_MAGIC 0x0c0c0c
	int magic;
#endif
#if WITH_PTHREADS
	pthread_mutex_t mutex;
#endif
	enum ctx_type  type;       /* type of context (creator or verifier) */
	void          *key;        /* private or public key                 */
	List           job_list;   /* List of used jobids (for verifier)    */
	List           state_list; /* List of cred states (for verifier)    */

	int          expiry_window;/* expiration window for cached creds    */

	void          *exkey;      /* Old public key if key is updated      */
	time_t         exkey_exp;  /* Old key expiration time               */
};


/*
 * Completion of slurm job credential type, slurm_cred_t:
 */
struct slurm_job_credential {
#ifndef NDEBUG
#  define CRED_MAGIC 0x0b0b0b
	int      magic;
#endif
#ifdef  WITH_PTHREADS
	pthread_mutex_t mutex;
#endif
	uint32_t  jobid;	/* Job ID associated with this cred	*/
	uint32_t  stepid;	/* Job step ID for this credential	*/
	uid_t     uid;		/* user for which this cred is valid	*/

	uint32_t  job_mem_limit;/* MB of memory reserved per node OR
				 * real memory per CPU | MEM_PER_CPU,
				 * default=0 (no limit) */
	uint32_t  step_mem_limit;

	uint16_t  core_array_size;	/* core/socket array size */
	uint16_t *cores_per_socket;
	uint16_t *sockets_per_node;
	uint32_t *sock_core_rep_count;

	List job_gres_list;		/* Generic resources allocated to JOB */
	List step_gres_list;		/* Generic resources allocated to STEP */

	bitstr_t *job_core_bitmap;
	uint16_t  job_core_spec;	/* Count of specialized cores */
	uint32_t  job_nhosts;	/* count of nodes allocated to JOB */
	char     *job_hostlist;	/* list of nodes allocated to JOB */
	bitstr_t *step_core_bitmap;
	time_t    ctime;	/* time of credential creation		*/
	char     *step_hostlist;/* hostnames for which the cred is ok	*/

	char     *signature; 	/* credential signature			*/
	unsigned int siglen;	/* signature length in bytes		*/
};

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job accounting
 * plugins will stop working.  If you need to add fields, add them
 * at the end of the structure.
 */
typedef struct slurm_crypto_ops {
	void *(*crypto_read_private_key)	(const char *path);
	void *(*crypto_read_public_key)		(const char *path);
	void  (*crypto_destroy_key)		(void *key);
	int   (*crypto_sign)			(void * key, char *buffer,
						 int buf_size, char **sig_pp,
						 unsigned int *sig_size_p);
	int   (*crypto_verify_sign)		(void * key, char *buffer,
						 unsigned int buf_size,
						 char *signature,
						 unsigned int sig_size);
	const char *(*crypto_str_error)		(int);
} slurm_crypto_ops_t;

/*
 * These strings must be in the same order as the fields declared
 * for slurm_crypto_ops_t.
 */
static const char *syms[] = {
	"crypto_read_private_key",
	"crypto_read_public_key",
	"crypto_destroy_key",
	"crypto_sign",
	"crypto_verify_sign",
	"crypto_str_error"
};

struct sbcast_cache {
	time_t       expire;	/* Time that the cred was created	*/
	uint32_t     value;	/* SLURM job id for this credential	*/
};

static slurm_crypto_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static time_t crypto_restart_time = (time_t) 0;
static List sbcast_cache_list = NULL;

/*
 * Static prototypes:
 */

static slurm_cred_ctx_t _slurm_cred_ctx_alloc(void);
static slurm_cred_t *    _slurm_cred_alloc(void);

static int  _ctx_update_private_key(slurm_cred_ctx_t ctx, const char *path);
static int  _ctx_update_public_key(slurm_cred_ctx_t ctx, const char *path);
static bool _exkey_is_valid(slurm_cred_ctx_t ctx);

static cred_state_t * _cred_state_create(slurm_cred_ctx_t ctx, slurm_cred_t *c);
static job_state_t  * _job_state_create(uint32_t jobid);
static void           _cred_state_destroy(cred_state_t *cs);
static void           _job_state_destroy(job_state_t   *js);

static job_state_t  * _find_job_state(slurm_cred_ctx_t ctx, uint32_t jobid);
static job_state_t  * _insert_job_state(slurm_cred_ctx_t ctx,  uint32_t jobid);
static int            _find_cred_state(cred_state_t *c, slurm_cred_t *cred);

static void _insert_cred_state(slurm_cred_ctx_t ctx, slurm_cred_t *cred);
static void _clear_expired_job_states(slurm_cred_ctx_t ctx);
static void _clear_expired_credential_states(slurm_cred_ctx_t ctx);
static void _verifier_ctx_init(slurm_cred_ctx_t ctx);

static bool _credential_replayed(slurm_cred_ctx_t ctx, slurm_cred_t *cred);
static bool _credential_revoked(slurm_cred_ctx_t ctx, slurm_cred_t *cred);

static int _slurm_cred_sign(slurm_cred_ctx_t ctx, slurm_cred_t *cred,
			    uint16_t protocol_version);
static int _slurm_cred_verify_signature(slurm_cred_ctx_t ctx, slurm_cred_t *c,
					uint16_t protocol_version);

static int _slurm_crypto_init(void);
static int _slurm_crypto_fini(void);

static job_state_t  * _job_state_unpack_one(Buf buffer);
static cred_state_t * _cred_state_unpack_one(Buf buffer);

static void _pack_cred(slurm_cred_t *cred, Buf buffer,
		       uint16_t protocol_version);
static void _job_state_unpack(slurm_cred_ctx_t ctx, Buf buffer);
static void _job_state_pack(slurm_cred_ctx_t ctx, Buf buffer);
static void _cred_state_unpack(slurm_cred_ctx_t ctx, Buf buffer);
static void _cred_state_pack(slurm_cred_ctx_t ctx, Buf buffer);
static void _job_state_pack_one(job_state_t *j, Buf buffer);
static void _cred_state_pack_one(cred_state_t *s, Buf buffer);

static void _sbast_cache_add(sbcast_cred_t *sbcast_cred);
static void _sbcast_cache_del(void *x);

#ifndef DISABLE_LOCALTIME
static char * timestr (const time_t *tp, char *buf, size_t n);
#endif

static int _slurm_crypto_init(void)
{
	char    *plugin_type = "crypto";
	char	*type = NULL;
	int	retval = SLURM_SUCCESS;

	if ( init_run && g_context )  /* mostly avoid locks for better speed */
		return retval;

	slurm_mutex_lock( &g_context_lock );
	if (crypto_restart_time == (time_t) 0)
		crypto_restart_time = time(NULL);
	if ( g_context )
		goto done;

	type = slurm_get_crypto_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	sbcast_cache_list = list_create(_sbcast_cache_del);
	init_run = true;

done:
	slurm_mutex_unlock( &g_context_lock );
	xfree(type);

	return(retval);
}

static int _slurm_crypto_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	list_destroy(sbcast_cache_list);
	sbcast_cache_list = NULL;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

/* Terminate the plugin and release all memory. */
extern int slurm_crypto_fini(void)
{
	if (_slurm_crypto_fini() < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

slurm_cred_ctx_t
slurm_cred_creator_ctx_create(const char *path)
{
	slurm_cred_ctx_t ctx = NULL;

	if (_slurm_crypto_init() < 0)
		return NULL;

	ctx = _slurm_cred_ctx_alloc();
	slurm_mutex_lock(&ctx->mutex);

	ctx->type = SLURM_CRED_CREATOR;

	ctx->key = (*(ops.crypto_read_private_key))(path);
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

	if (_slurm_crypto_init() < 0)
		return NULL;

	ctx = _slurm_cred_ctx_alloc();
	slurm_mutex_lock(&ctx->mutex);

	ctx->type = SLURM_CRED_VERIFIER;

	ctx->key = (*(ops.crypto_read_public_key))(path);
	if (!ctx->key)
		goto fail;

	_verifier_ctx_init(ctx);

	slurm_mutex_unlock(&ctx->mutex);
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
	if (_slurm_crypto_init() < 0)
		return;

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);

	if (ctx->exkey)
		(*(ops.crypto_destroy_key))(ctx->exkey);
	if (ctx->key)
		(*(ops.crypto_destroy_key))(ctx->key);
	if (ctx->job_list)
		list_destroy(ctx->job_list);
	if (ctx->state_list)
		list_destroy(ctx->state_list);

	xassert(ctx->magic = ~CRED_CTX_MAGIC);

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
	if (_slurm_crypto_init() < 0)
		return SLURM_ERROR;

	if (ctx->type == SLURM_CRED_CREATOR)
		return _ctx_update_private_key(ctx, path);
	else
		return _ctx_update_public_key(ctx, path);
}


slurm_cred_t *
slurm_cred_create(slurm_cred_ctx_t ctx, slurm_cred_arg_t *arg,
		  uint16_t protocol_version)
{
	slurm_cred_t *cred = NULL;

	xassert(ctx != NULL);
	xassert(arg != NULL);
	if (_slurm_crypto_init() < 0)
		return NULL;

	cred = _slurm_cred_alloc();
	slurm_mutex_lock(&cred->mutex);
	xassert(cred->magic == CRED_MAGIC);

	cred->jobid  = arg->jobid;
	cred->stepid = arg->stepid;
	cred->uid    = arg->uid;
	cred->job_core_spec   = arg->job_core_spec;
	cred->job_gres_list   = gres_plugin_job_state_dup(arg->job_gres_list);
	cred->step_gres_list  = gres_plugin_step_state_dup(arg->step_gres_list);
	cred->job_mem_limit   = arg->job_mem_limit;
	cred->step_mem_limit  = arg->step_mem_limit;
	cred->step_hostlist   = xstrdup(arg->step_hostlist);
#ifndef HAVE_BG
	{
		int i, sock_recs = 0;
#ifndef HAVE_ALPS_CRAY
		/* Zero compute node allocations allowed on a Cray for use
		 * of front-end nodes */
		xassert(arg->job_nhosts);
#endif
		for (i = 0; i < arg->job_nhosts; i++) {
			sock_recs += arg->sock_core_rep_count[i];
			if (sock_recs >= arg->job_nhosts)
				break;
		}
		i++;
		cred->job_core_bitmap = bit_copy(arg->job_core_bitmap);
		cred->step_core_bitmap = bit_copy(arg->step_core_bitmap);
		cred->core_array_size = i;
		cred->cores_per_socket = xmalloc(sizeof(uint16_t) * i);
		memcpy(cred->cores_per_socket, arg->cores_per_socket,
		       (sizeof(uint16_t) * i));
		cred->sockets_per_node = xmalloc(sizeof(uint16_t) * i);
		memcpy(cred->sockets_per_node, arg->sockets_per_node,
		       (sizeof(uint16_t) * i));
		cred->sock_core_rep_count = xmalloc(sizeof(uint32_t) * i);
		memcpy(cred->sock_core_rep_count, arg->sock_core_rep_count,
		       (sizeof(uint32_t) * i));
		cred->job_nhosts = arg->job_nhosts;
		cred->job_hostlist = xstrdup(arg->job_hostlist);
	}
#endif
	cred->ctime  = time(NULL);

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type == SLURM_CRED_CREATOR);
	if (_slurm_cred_sign(ctx, cred, protocol_version) < 0)
		goto fail;

	slurm_mutex_unlock(&ctx->mutex);
	slurm_mutex_unlock(&cred->mutex);

	return cred;

fail:
	slurm_mutex_unlock(&ctx->mutex);
	slurm_mutex_unlock(&cred->mutex);
	slurm_cred_destroy(cred);
	return NULL;
}

slurm_cred_t *
slurm_cred_copy(slurm_cred_t *cred)
{
	slurm_cred_t *rcred = NULL;

	xassert(cred != NULL);

	slurm_mutex_lock(&cred->mutex);

	rcred = _slurm_cred_alloc();
	slurm_mutex_lock(&rcred->mutex);
	xassert(rcred->magic == CRED_MAGIC);

	rcred->jobid  = cred->jobid;
	rcred->stepid = cred->stepid;
	rcred->uid    = cred->uid;
	rcred->job_core_spec  = cred->job_core_spec;
	rcred->job_gres_list  = gres_plugin_job_state_dup(cred->job_gres_list);
	rcred->step_gres_list = gres_plugin_step_state_dup(cred->step_gres_list);
	rcred->job_mem_limit  = cred->job_mem_limit;
	rcred->step_mem_limit = cred->step_mem_limit;
	rcred->step_hostlist  = xstrdup(cred->step_hostlist);
#ifndef HAVE_BG
	rcred->job_core_bitmap  = bit_copy(cred->job_core_bitmap);
	rcred->step_core_bitmap = bit_copy(cred->step_core_bitmap);
	rcred->core_array_size  = cred->core_array_size;
	rcred->cores_per_socket = xmalloc(sizeof(uint16_t) *
					  rcred->core_array_size);
	memcpy(rcred->cores_per_socket, cred->cores_per_socket,
	       (sizeof(uint16_t) * rcred->core_array_size));
	rcred->sockets_per_node = xmalloc(sizeof(uint16_t) *
					  rcred->core_array_size);
	memcpy(rcred->sockets_per_node, cred->sockets_per_node,
	       (sizeof(uint16_t) * rcred->core_array_size));
	cred->sock_core_rep_count = xmalloc(sizeof(uint32_t) *
					    rcred->core_array_size);
	memcpy(rcred->sock_core_rep_count, cred->sock_core_rep_count,
	       (sizeof(uint32_t) * rcred->core_array_size));
	rcred->job_nhosts = cred->job_nhosts;
	rcred->job_hostlist = xstrdup(cred->job_hostlist);
#endif
	rcred->ctime  = cred->ctime;
	rcred->siglen = cred->siglen;
	/* Assumes signature is a string,
	 * otherwise use xmalloc and strcpy here */
	rcred->signature = xstrdup(cred->signature);

	slurm_mutex_unlock(&cred->mutex);
	slurm_mutex_unlock(&rcred->mutex);

	return rcred;
}

slurm_cred_t *
slurm_cred_faker(slurm_cred_arg_t *arg)
{
	int fd, i;
	slurm_cred_t *cred = NULL;

	xassert(arg != NULL);

	cred = _slurm_cred_alloc();
	slurm_mutex_lock(&cred->mutex);

	cred->jobid    = arg->jobid;
	cred->stepid   = arg->stepid;
	cred->uid      = arg->uid;
	cred->job_core_spec  = arg->job_core_spec;
	cred->job_mem_limit  = arg->job_mem_limit;
	cred->step_mem_limit = arg->step_mem_limit;
	cred->step_hostlist  = xstrdup(arg->step_hostlist);
#ifndef HAVE_BG
	{
		int sock_recs = 0;
		for (i=0; i<arg->job_nhosts; i++) {
			sock_recs += arg->sock_core_rep_count[i];
			if (sock_recs >= arg->job_nhosts)
				break;
		}
		i++;
		cred->job_core_bitmap  = bit_copy(arg->job_core_bitmap);
		cred->step_core_bitmap = bit_copy(arg->step_core_bitmap);
		cred->core_array_size = i;
		cred->cores_per_socket = xmalloc(sizeof(uint16_t) * i);
		memcpy(cred->cores_per_socket, arg->cores_per_socket,
		       (sizeof(uint16_t) * i));
		cred->sockets_per_node = xmalloc(sizeof(uint16_t) * i);
		memcpy(cred->sockets_per_node, arg->sockets_per_node,
		       (sizeof(uint16_t) * i));
		cred->sock_core_rep_count = xmalloc(sizeof(uint32_t) * i);
		memcpy(cred->sock_core_rep_count, arg->sock_core_rep_count,
		       (sizeof(uint32_t) * i));
		cred->job_nhosts = arg->job_nhosts;
		cred->job_hostlist = xstrdup(arg->job_hostlist);
	}
#endif
	cred->ctime  = time(NULL);
	cred->siglen = SLURM_IO_KEY_SIZE;

	cred->signature = xmalloc(cred->siglen * sizeof(char));

	if ((fd = open("/dev/urandom", O_RDONLY)) >= 0) {
		if (read(fd, cred->signature, cred->siglen-1) == -1)
			error("reading fake signature from /dev/urandom: %m");
		if (close(fd) < 0)
			error("close(/dev/urandom): %m");
		for (i=0; i<cred->siglen-1; i++)
			cred->signature[i] = 'a' + (cred->signature[i] & 0xf);
	} else {	/* Note: some systems lack this file */
		struct timeval tv;
		gettimeofday(&tv, NULL);
		i = (unsigned int) (tv.tv_sec + tv.tv_usec);
		srand((unsigned int) i);
		for (i=0; i<cred->siglen-1; i++)
			cred->signature[i] = 'a' + (rand() & 0xf);
	}

	slurm_mutex_unlock(&cred->mutex);
	return cred;

}

void slurm_cred_free_args(slurm_cred_arg_t *arg)
{
	FREE_NULL_BITMAP(arg->job_core_bitmap);
	FREE_NULL_BITMAP(arg->step_core_bitmap);
	xfree(arg->cores_per_socket);
	FREE_NULL_LIST(arg->job_gres_list);
	FREE_NULL_LIST(arg->step_gres_list);
	xfree(arg->step_hostlist);
	xfree(arg->job_hostlist);
	xfree(arg->sock_core_rep_count);
	xfree(arg->sockets_per_node);
}

int slurm_cred_get_args(slurm_cred_t *cred, slurm_cred_arg_t *arg)
{
	xassert(cred != NULL);
	xassert(arg  != NULL);

	/*
	 * set arguments to cred contents
	 */
	slurm_mutex_lock(&cred->mutex);
	arg->jobid    = cred->jobid;
	arg->stepid   = cred->stepid;
	arg->uid      = cred->uid;
	arg->job_gres_list  = gres_plugin_job_state_dup(cred->job_gres_list);
	arg->step_gres_list = gres_plugin_step_state_dup(cred->step_gres_list);
	arg->job_core_spec  = cred->job_core_spec;
	arg->job_mem_limit  = cred->job_mem_limit;
	arg->step_mem_limit = cred->step_mem_limit;
	arg->step_hostlist  = xstrdup(cred->step_hostlist);
#ifdef HAVE_BG
	arg->job_core_bitmap = NULL;
	arg->step_core_bitmap = NULL;
	arg->cores_per_socket = NULL;
	arg->sockets_per_node = NULL;
	arg->sock_core_rep_count = NULL;
	arg->job_nhosts = 0;
	arg->job_hostlist = NULL;
#else
	arg->job_core_bitmap  = bit_copy(cred->job_core_bitmap);
	arg->step_core_bitmap = bit_copy(cred->step_core_bitmap);
	arg->cores_per_socket = xmalloc(sizeof(uint16_t) *
					cred->core_array_size);
	memcpy(arg->cores_per_socket, cred->cores_per_socket,
	       (sizeof(uint16_t) * cred->core_array_size));
	arg->sockets_per_node = xmalloc(sizeof(uint16_t) *
					cred->core_array_size);
	memcpy(arg->sockets_per_node, cred->sockets_per_node,
	       (sizeof(uint16_t) * cred->core_array_size));
	arg->sock_core_rep_count = xmalloc(sizeof(uint32_t) *
					   cred->core_array_size);
	memcpy(arg->sock_core_rep_count, cred->sock_core_rep_count,
	       (sizeof(uint32_t) * cred->core_array_size));
	arg->job_nhosts = cred->job_nhosts;
	arg->job_hostlist = xstrdup(cred->job_hostlist);
#endif
	slurm_mutex_unlock(&cred->mutex);

	return SLURM_SUCCESS;
}

extern int
slurm_cred_verify(slurm_cred_ctx_t ctx, slurm_cred_t *cred,
		  slurm_cred_arg_t *arg, uint16_t protocol_version)
{
	time_t now = time(NULL);
	int errnum;

	xassert(ctx  != NULL);
	xassert(cred != NULL);
	xassert(arg  != NULL);
	if (_slurm_crypto_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);
	slurm_mutex_lock(&cred->mutex);

	xassert(ctx->magic  == CRED_CTX_MAGIC);
	xassert(ctx->type   == SLURM_CRED_VERIFIER);
	xassert(cred->magic == CRED_MAGIC);

	/* NOTE: the verification checks that the credential was
	 * created by SlurmUser or root */
	if (_slurm_cred_verify_signature(ctx, cred, protocol_version) < 0) {
		slurm_seterrno(ESLURMD_INVALID_JOB_CREDENTIAL);
		goto error;
	}

	if (now > (cred->ctime + ctx->expiry_window)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_EXPIRED);
		goto error;
	}

	slurm_cred_handle_reissue(ctx, cred);

	if (_credential_revoked(ctx, cred)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_REVOKED);
		goto error;
	}

	if (_credential_replayed(ctx, cred)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_REPLAYED);
		goto error;
	}

	slurm_mutex_unlock(&ctx->mutex);

	/*
	 * set arguments to cred contents
	 */
	arg->jobid    = cred->jobid;
	arg->stepid   = cred->stepid;
	arg->uid      = cred->uid;
	arg->job_gres_list  = gres_plugin_job_state_dup(cred->job_gres_list);
	arg->step_gres_list = gres_plugin_step_state_dup(cred->step_gres_list);
	arg->job_core_spec  = cred->job_core_spec;
	arg->job_mem_limit  = cred->job_mem_limit;
	arg->step_mem_limit = cred->step_mem_limit;
	arg->step_hostlist  = xstrdup(cred->step_hostlist);

#ifdef HAVE_BG
	arg->job_core_bitmap  = NULL;
	arg->step_core_bitmap = NULL;
	arg->cores_per_socket = NULL;
	arg->sockets_per_node = NULL;
	arg->sock_core_rep_count = NULL;
	arg->job_nhosts = 0;
	arg->job_hostlist = NULL;
#else
	arg->job_core_bitmap = bit_copy(cred->job_core_bitmap);
	arg->step_core_bitmap = bit_copy(cred->step_core_bitmap);
	arg->cores_per_socket = xmalloc(sizeof(uint16_t) *
					cred->core_array_size);
	memcpy(arg->cores_per_socket, cred->cores_per_socket,
	       (sizeof(uint16_t) * cred->core_array_size));
	arg->sockets_per_node = xmalloc(sizeof(uint16_t) *
					cred->core_array_size);
	memcpy(arg->sockets_per_node, cred->sockets_per_node,
	       (sizeof(uint16_t) * cred->core_array_size));
	arg->sock_core_rep_count = xmalloc(sizeof(uint32_t) *
					   cred->core_array_size);
	memcpy(arg->sock_core_rep_count, cred->sock_core_rep_count,
	       (sizeof(uint32_t) * cred->core_array_size));
	arg->job_nhosts = cred->job_nhosts;
	arg->job_hostlist = xstrdup(cred->job_hostlist);
#endif
	slurm_mutex_unlock(&cred->mutex);

	return SLURM_SUCCESS;

error:
	errnum = slurm_get_errno();
	slurm_mutex_unlock(&ctx->mutex);
	slurm_mutex_unlock(&cred->mutex);
	slurm_seterrno(errnum);
	return SLURM_ERROR;
}


void
slurm_cred_destroy(slurm_cred_t *cred)
{
	if (cred == NULL)
		return;

	xassert(cred->magic == CRED_MAGIC);

	slurm_mutex_lock(&cred->mutex);
#ifndef HAVE_BG
	FREE_NULL_BITMAP(cred->job_core_bitmap);
	FREE_NULL_BITMAP(cred->step_core_bitmap);
	xfree(cred->cores_per_socket);
	xfree(cred->job_hostlist);
	xfree(cred->sock_core_rep_count);
	xfree(cred->sockets_per_node);
#endif
	FREE_NULL_LIST(cred->job_gres_list);
	FREE_NULL_LIST(cred->step_gres_list);
	xfree(cred->step_hostlist);
	xfree(cred->signature);
	xassert(cred->magic = ~CRED_MAGIC);

	slurm_mutex_unlock(&cred->mutex);
	slurm_mutex_destroy(&cred->mutex);

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

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	rc = list_delete_all(ctx->state_list,
			     (ListFindF) _find_cred_state, cred);

	slurm_mutex_unlock(&ctx->mutex);

	return (rc > 0 ? SLURM_SUCCESS : SLURM_FAILURE);
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
	return SLURM_FAILURE;
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
#if DEBUG_TIME
	{
		char buf[64];
		debug2("set revoke expiration for jobid %u to %s",
		       j->jobid, timestr(&j->expiration, buf, 64));
	}
#else
	debug2("set revoke expiration for jobid %u to %"PRIu64" UTS",
	       j->jobid, (uint64_t) j->expiration);
#endif
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

	slurm_mutex_lock(&cred->mutex);

	*datap   = (char *) cred->signature;
	*datalen = cred->siglen;

	slurm_mutex_unlock(&cred->mutex);

	return SLURM_SUCCESS;
}

#ifndef HAVE_BG
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
#endif

/*
 * Retrieve the set of cores that were allocated to the job and step then
 * format them in the List Format (e.g., "0-2,7,12-14"). Also return
 * job and step's memory limit.
 *
 * NOTE: caller must xfree the returned strings.
 */
void format_core_allocs(slurm_cred_t *cred, char *node_name, uint16_t cpus,
			char **job_alloc_cores, char **step_alloc_cores,
			uint32_t *job_mem_limit, uint32_t *step_mem_limit)
{
#ifdef HAVE_BG
	xassert(cred);
	xassert(job_alloc_cores);
	xassert(step_alloc_cores);
	*job_alloc_cores  = NULL;
	*step_alloc_cores = NULL;
	*job_mem_limit = cred->job_mem_limit & (~MEM_PER_CPU);
	if (cred->step_mem_limit)
		*step_mem_limit = cred->step_mem_limit & (~MEM_PER_CPU);
	else
		*step_mem_limit = *job_mem_limit;
#else
	bitstr_t	*job_core_bitmap, *step_core_bitmap;
	hostset_t	hset = NULL;
	int		host_index = -1;
	uint32_t	i, j, i_first_bit=0, i_last_bit=0;
	uint32_t	job_cpu_cnt = 0, step_cpu_cnt = 0;

	xassert(cred);
	xassert(job_alloc_cores);
	xassert(step_alloc_cores);
	if (!(hset = hostset_create(cred->job_hostlist))) {
		error("Unable to create job hostset: `%s'",
		      cred->job_hostlist);
		return;
	}
#ifdef HAVE_FRONT_END
	host_index = 0;
#else
	host_index = hostset_find(hset, node_name);
#endif
	if ((host_index < 0) || (host_index >= cred->job_nhosts)) {
		error("Invalid host_index %d for job %u",
		      host_index, cred->jobid);
		error("Host %s not in hostlist %s",
		      node_name, cred->job_hostlist);
		hostset_destroy(hset);
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
		if (bit_test(cred->job_core_bitmap, i)) {
			bit_set(job_core_bitmap, j);
			job_cpu_cnt++;
		}
		if (bit_test(cred->step_core_bitmap, i)) {
			bit_set(step_core_bitmap, j);
			step_cpu_cnt++;
		}
	}

	/* Scale CPU count, same as slurmd/req.c:_check_job_credential() */
	if (i_last_bit <= i_first_bit)
		error("step credential has no CPUs selected");
	else {
		uint32_t i = cpus / (i_last_bit - i_first_bit);
		if (i > 1) {
			debug2("scaling CPU count by factor of %d (%u/(%u-%u)",
			       i, cpus, i_last_bit, i_first_bit);
			step_cpu_cnt *= i;
			job_cpu_cnt *= i;
		}
	}

	if (cred->job_mem_limit & MEM_PER_CPU) {
		*job_mem_limit = (cred->job_mem_limit & (~MEM_PER_CPU)) *
				 job_cpu_cnt;
	} else
		*job_mem_limit = cred->job_mem_limit;
	if (cred->step_mem_limit & MEM_PER_CPU) {
		*step_mem_limit = (cred->step_mem_limit & (~MEM_PER_CPU)) *
				  step_cpu_cnt;
	} else if (cred->step_mem_limit)
		*step_mem_limit = cred->step_mem_limit;
	else
		*step_mem_limit = *job_mem_limit;

	*job_alloc_cores  = _core_format(job_core_bitmap);
	*step_alloc_cores = _core_format(step_core_bitmap);
	FREE_NULL_BITMAP(job_core_bitmap);
	FREE_NULL_BITMAP(step_core_bitmap);
	hostset_destroy(hset);
#endif
}

/*
 * Retrieve the job and step generic resources (gres) allocate to this job
 * on this node.
 *
 * NOTE: Caller must destroy the returned lists
 */
extern void get_cred_gres(slurm_cred_t *cred, char *node_name,
			  List *job_gres_list, List *step_gres_list)
{
	hostset_t	hset = NULL;
	int		host_index = -1;

	xassert(cred);
	xassert(job_gres_list);
	xassert(step_gres_list);

	*job_gres_list  = NULL;
	*step_gres_list = NULL;
	if ((cred->job_gres_list == NULL) && (cred->step_gres_list == NULL))
		return;

	if (!(hset = hostset_create(cred->job_hostlist))) {
		error("Unable to create job hostset: `%s'",
		      cred->job_hostlist);
		return;
	}
#ifdef HAVE_FRONT_END
	host_index = 0;
#else
	host_index = hostset_find(hset, node_name);
#endif
	if ((host_index < 0) || (host_index >= cred->job_nhosts)) {
		error("Invalid host_index %d for job %u",
		      host_index, cred->jobid);
		error("Host %s not in credential hostlist %s",
		      node_name, cred->job_hostlist);
		hostset_destroy(hset);
		return;
	}

	*job_gres_list = gres_plugin_job_state_extract(cred->job_gres_list,
						       host_index);
	*step_gres_list = gres_plugin_step_state_extract(cred->step_gres_list,
							 host_index);
	return;
}

void
slurm_cred_pack(slurm_cred_t *cred, Buf buffer, uint16_t protocol_version)
{
	xassert(cred != NULL);
	xassert(cred->magic == CRED_MAGIC);

	slurm_mutex_lock(&cred->mutex);

	_pack_cred(cred, buffer, protocol_version);
	xassert(cred->siglen > 0);
	packmem(cred->signature, cred->siglen, buffer);

	slurm_mutex_unlock(&cred->mutex);

	return;
}

slurm_cred_t *
slurm_cred_unpack(Buf buffer, uint16_t protocol_version)
{
	uint32_t     cred_uid, len;
	slurm_cred_t *cred = NULL;
	char        *bit_fmt = NULL;
	char       **sigp;
	uint32_t     cluster_flags = slurmdb_setup_cluster_flags();

	xassert(buffer != NULL);

	cred = _slurm_cred_alloc();
	slurm_mutex_lock(&cred->mutex);
	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		safe_unpack32(&cred->jobid, buffer);
		safe_unpack32(&cred->stepid, buffer);
		safe_unpack32(&cred_uid, buffer);
		cred->uid = cred_uid;
		if (gres_plugin_job_state_unpack(&cred->job_gres_list, buffer,
						 cred->jobid, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_plugin_step_state_unpack(&cred->step_gres_list,
						  buffer, cred->jobid,
						  cred->stepid,
						  protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack16(&cred->job_core_spec, buffer);
		safe_unpack32(&cred->job_mem_limit, buffer);
		safe_unpack32(&cred->step_mem_limit, buffer);
		safe_unpackstr_xmalloc(&cred->step_hostlist, &len, buffer);
		safe_unpack_time(&cred->ctime, buffer);

		if (!(cluster_flags & CLUSTER_FLAG_BG)) {
			uint32_t tot_core_cnt;
			safe_unpack32(&tot_core_cnt, buffer);
			safe_unpackstr_xmalloc(&bit_fmt, &len, buffer);
			cred->job_core_bitmap =
				bit_alloc((bitoff_t) tot_core_cnt);
			if (bit_unfmt(cred->job_core_bitmap, bit_fmt))
				goto unpack_error;
			xfree(bit_fmt);
			safe_unpackstr_xmalloc(&bit_fmt, &len, buffer);
			cred->step_core_bitmap =
				bit_alloc((bitoff_t) tot_core_cnt);
			if (bit_unfmt(cred->step_core_bitmap, bit_fmt))
				goto unpack_error;
			xfree(bit_fmt);
			safe_unpack16(&cred->core_array_size, buffer);
			if (cred->core_array_size) {
				safe_unpack16_array(&cred->cores_per_socket,
						    &len,
						    buffer);
				if (len != cred->core_array_size)
					goto unpack_error;
				safe_unpack16_array(&cred->sockets_per_node,
						    &len, buffer);
				if (len != cred->core_array_size)
					goto unpack_error;
				safe_unpack32_array(&cred->sock_core_rep_count,
						    &len,
						    buffer);
				if (len != cred->core_array_size)
					goto unpack_error;
			}
			safe_unpack32(&cred->job_nhosts, buffer);
			safe_unpackstr_xmalloc(&cred->job_hostlist, &len,
					       buffer);
		}

		/* "sigp" must be last */
		sigp = (char **) &cred->signature;
		safe_unpackmem_xmalloc(sigp, &len, buffer);
		cred->siglen = len;
		xassert(len > 0);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		safe_unpack32(&cred->jobid, buffer);
		safe_unpack32(&cred->stepid, buffer);
		safe_unpack32(&cred_uid, buffer);
		cred->uid = cred_uid;
		if (gres_plugin_job_state_unpack(&cred->job_gres_list, buffer,
						 cred->jobid, protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;
		if (gres_plugin_step_state_unpack(&cred->step_gres_list,
						  buffer, cred->jobid,
						  cred->stepid,
						  protocol_version)
		    != SLURM_SUCCESS) {
			goto unpack_error;
		}
		safe_unpack32(&cred->job_mem_limit, buffer);
		safe_unpack32(&cred->step_mem_limit, buffer);
		safe_unpackstr_xmalloc(&cred->step_hostlist, &len, buffer);
		safe_unpack_time(&cred->ctime, buffer);

		if (!(cluster_flags & CLUSTER_FLAG_BG)) {
			uint32_t tot_core_cnt;
			safe_unpack32(&tot_core_cnt, buffer);
			safe_unpackstr_xmalloc(&bit_fmt, &len, buffer);
			cred->job_core_bitmap =
				bit_alloc((bitoff_t) tot_core_cnt);
			if (bit_unfmt(cred->job_core_bitmap, bit_fmt))
				goto unpack_error;
			xfree(bit_fmt);
			safe_unpackstr_xmalloc(&bit_fmt, &len, buffer);
			cred->step_core_bitmap =
				bit_alloc((bitoff_t) tot_core_cnt);
			if (bit_unfmt(cred->step_core_bitmap, bit_fmt))
				goto unpack_error;
			xfree(bit_fmt);
			safe_unpack16(&cred->core_array_size, buffer);
			if (cred->core_array_size) {
				safe_unpack16_array(&cred->cores_per_socket,
						    &len,
						    buffer);
				if (len != cred->core_array_size)
					goto unpack_error;
				safe_unpack16_array(&cred->sockets_per_node,
						    &len, buffer);
				if (len != cred->core_array_size)
					goto unpack_error;
				safe_unpack32_array(&cred->sock_core_rep_count,
						    &len,
						    buffer);
				if (len != cred->core_array_size)
					goto unpack_error;
			}
			safe_unpack32(&cred->job_nhosts, buffer);
			safe_unpackstr_xmalloc(&cred->job_hostlist, &len,
					       buffer);
		}

		/* "sigp" must be last */
		sigp = (char **) &cred->signature;
		safe_unpackmem_xmalloc(sigp, &len, buffer);
		cred->siglen = len;
		xassert(len > 0);
	} else {
		error("slurm_cred_unpack: protocol_version"
		      " %hu not supported", protocol_version);
		goto unpack_error;
	}
	slurm_mutex_unlock(&cred->mutex);
	return cred;

unpack_error:
	xfree(bit_fmt);
	slurm_mutex_unlock(&cred->mutex);
	slurm_cred_destroy(cred);
	return NULL;
}

int
slurm_cred_ctx_pack(slurm_cred_ctx_t ctx, Buf buffer)
{
	slurm_mutex_lock(&ctx->mutex);
	_job_state_pack(ctx, buffer);
	_cred_state_pack(ctx, buffer);
	slurm_mutex_unlock(&ctx->mutex);

	return SLURM_SUCCESS;
}

int
slurm_cred_ctx_unpack(slurm_cred_ctx_t ctx, Buf buffer)
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

void
slurm_cred_print(slurm_cred_t *cred)
{
	if (cred == NULL)
		return;

	slurm_mutex_lock(&cred->mutex);

	xassert(cred->magic == CRED_MAGIC);

	info("Cred: Jobid             %u",  cred->jobid         );
	info("Cred: Stepid            %u",  cred->stepid        );
	info("Cred: UID               %u",  (uint32_t) cred->uid);
	info("Cred: Job_core_spec     %u",  cred->job_core_spec );
	info("Cred: Job_mem_limit     %u",  cred->job_mem_limit );
	info("Cred: Step_mem_limit    %u",  cred->step_mem_limit );
	info("Cred: Step hostlist     %s",  cred->step_hostlist );
	info("Cred: ctime             %s",  slurm_ctime(&cred->ctime) );
	info("Cred: siglen            %u",  cred->siglen        );
#ifndef HAVE_BG
	{
		int i;
		char str[128];
		info("Cred: job_core_bitmap   %s",
		     bit_fmt(str, sizeof(str), cred->job_core_bitmap));
		info("Cred: step_core_bitmap  %s",
		     bit_fmt(str, sizeof(str), cred->step_core_bitmap));
		info("Cred: sockets_per_node, cores_per_socket, rep_count");
		for (i=0; i<cred->core_array_size; i++) {
			info("      socks:%u cores:%u reps:%u",
			     cred->sockets_per_node[i],
			     cred->cores_per_socket[i],
			     cred->sock_core_rep_count[i]);
		}
		info("Cred: job_nhosts        %u",   cred->job_nhosts    );
		info("Cred: job_hostlist      %s",   cred->job_hostlist  );
	}
#endif
	slurm_mutex_unlock(&cred->mutex);

}

static void
_verifier_ctx_init(slurm_cred_ctx_t ctx)
{
	xassert(ctx != NULL);
	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type == SLURM_CRED_VERIFIER);

	ctx->job_list   = list_create((ListDelF) _job_state_destroy);
	ctx->state_list = list_create((ListDelF) _cred_state_destroy);

	return;
}


static int
_ctx_update_private_key(slurm_cred_ctx_t ctx, const char *path)
{
	void *pk   = NULL;
	void *tmpk = NULL;

	xassert(ctx != NULL);

	pk = (*(ops.crypto_read_private_key))(path);
	if (!pk)
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_CREATOR);

	tmpk = ctx->key;
	ctx->key = pk;

	slurm_mutex_unlock(&ctx->mutex);

	(*(ops.crypto_destroy_key))(tmpk);

	return SLURM_SUCCESS;
}


static int
_ctx_update_public_key(slurm_cred_ctx_t ctx, const char *path)
{
	void *pk   = NULL;

	xassert(ctx != NULL);
	pk = (*(ops.crypto_read_public_key))(path);
	if (!pk)
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	if (ctx->exkey)
		(*(ops.crypto_destroy_key))(ctx->exkey);

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
		(*(ops.crypto_destroy_key))(ctx->exkey);
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
	slurm_mutex_lock(&ctx->mutex);

	ctx->expiry_window = DEFAULT_EXPIRATION_WINDOW;
	ctx->exkey_exp     = (time_t) -1;

	xassert(ctx->magic = CRED_CTX_MAGIC);

	slurm_mutex_unlock(&ctx->mutex);
	return ctx;
}

static slurm_cred_t *
_slurm_cred_alloc(void)
{
	slurm_cred_t *cred = xmalloc(sizeof(*cred));
	/* Contents initialized to zero */

	slurm_mutex_init(&cred->mutex);
	cred->uid = (uid_t) -1;

	xassert(cred->magic = CRED_MAGIC);

	return cred;
}


#if EXTREME_DEBUG
static void
_print_data(char *data, int datalen)
{
	char buf[1024];
	size_t len = 0;
	int i;

	for (i = 0; i < datalen; i += sizeof(char))
		len += sprintf(buf+len, "%02x", data[i]);
}
#endif

static int
_slurm_cred_sign(slurm_cred_ctx_t ctx, slurm_cred_t *cred,
		 uint16_t protocol_version)
{
	Buf           buffer;
	int           rc;

	buffer = init_buf(4096);
	_pack_cred(cred, buffer, protocol_version);
	rc = (*(ops.crypto_sign))(ctx->key,
				  get_buf_data(buffer),
				  get_buf_offset(buffer),
				  &cred->signature,
				  &cred->siglen);
	free_buf(buffer);

	if (rc) {
		error("Credential sign: %s",
		      (*(ops.crypto_str_error))(rc));
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static int
_slurm_cred_verify_signature(slurm_cred_ctx_t ctx, slurm_cred_t *cred,
			     uint16_t protocol_version)
{
	Buf            buffer;
	int            rc;

	debug("Checking credential with %u bytes of sig data", cred->siglen);
	buffer = init_buf(4096);
	_pack_cred(cred, buffer, protocol_version);

	rc = (*(ops.crypto_verify_sign))(ctx->key,
					 get_buf_data(buffer),
					 get_buf_offset(buffer),
					 cred->signature,
					 cred->siglen);
	if (rc && _exkey_is_valid(ctx)) {
		rc = (*(ops.crypto_verify_sign))(ctx->exkey,
						 get_buf_data(buffer),
						 get_buf_offset(buffer),
						 cred->signature,
						 cred->siglen);
	}
	free_buf(buffer);

	if (rc) {
		error("Credential signature check: %s",
		      (*(ops.crypto_str_error))(rc));
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}


static void
_pack_cred(slurm_cred_t *cred, Buf buffer, uint16_t protocol_version)
{
	uint32_t cred_uid = (uint32_t) cred->uid;

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32(cred->jobid,          buffer);
		pack32(cred->stepid,         buffer);
		pack32(cred_uid,             buffer);

		(void) gres_plugin_job_state_pack(cred->job_gres_list, buffer,
						  cred->jobid, false,
						  SLURM_PROTOCOL_VERSION);
		gres_plugin_step_state_pack(cred->step_gres_list, buffer,
					    cred->jobid, cred->stepid,
					    SLURM_PROTOCOL_VERSION);
		pack16(cred->job_core_spec,  buffer);
		pack32(cred->job_mem_limit,  buffer);
		pack32(cred->step_mem_limit, buffer);
		packstr(cred->step_hostlist, buffer);
		pack_time(cred->ctime,       buffer);
#ifndef HAVE_BG
		{
			uint32_t tot_core_cnt;
			tot_core_cnt = bit_size(cred->job_core_bitmap);
			pack32(tot_core_cnt, buffer);
			pack_bit_fmt(cred->job_core_bitmap, buffer);
			pack_bit_fmt(cred->step_core_bitmap, buffer);
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
			pack32(cred->job_nhosts,    buffer);
			packstr(cred->job_hostlist, buffer);
		}
#endif
	} else {
		pack32(cred->jobid,          buffer);
		pack32(cred->stepid,         buffer);
		pack32(cred_uid,             buffer);

		(void) gres_plugin_job_state_pack(cred->job_gres_list, buffer,
						  cred->jobid, false,
						  SLURM_PROTOCOL_VERSION);
		gres_plugin_step_state_pack(cred->step_gres_list, buffer,
					    cred->jobid, cred->stepid,
					    SLURM_PROTOCOL_VERSION);
		pack32(cred->job_mem_limit,  buffer);
		pack32(cred->step_mem_limit, buffer);
		packstr(cred->step_hostlist, buffer);
		pack_time(cred->ctime,       buffer);
#ifndef HAVE_BG
		{
			uint32_t tot_core_cnt;
			tot_core_cnt = bit_size(cred->job_core_bitmap);
			pack32(tot_core_cnt, buffer);
			pack_bit_fmt(cred->job_core_bitmap, buffer);
			pack_bit_fmt(cred->step_core_bitmap, buffer);
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
			pack32(cred->job_nhosts,    buffer);
			packstr(cred->job_hostlist, buffer);
		}
#endif
	}
}


static bool
_credential_replayed(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	ListIterator  i = NULL;
	cred_state_t *s = NULL;

	_clear_expired_credential_states(ctx);

	i = list_iterator_create(ctx->state_list);

	while ((s = list_next(i))) {
		if ((s->jobid  == cred->jobid)  &&
		    (s->stepid == cred->stepid) &&
		    (s->ctime  == cred->ctime))
			break;
	}

	list_iterator_destroy(i);

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

#ifdef DISABLE_LOCALTIME
extern char * timestr (const time_t *tp, char *buf, size_t n)
#else
	static char * timestr (const time_t *tp, char *buf, size_t n)
#endif
{
	char fmt[] = "%y%m%d%H%M%S";
	struct tm tmval;
#ifdef DISABLE_LOCALTIME
	static int disabled = 0;
	if (buf == NULL)
		disabled = 1;
	if (disabled)
		return NULL;
#endif
	if (!localtime_r (tp, &tmval))
		error ("localtime_r: %m");
	slurm_strftime (buf, n, fmt, &tmval);
	return (buf);
}

extern void
slurm_cred_handle_reissue(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	job_state_t  *j = _find_job_state(ctx, cred->jobid);

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
}

extern bool
slurm_cred_revoked(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	job_state_t  *j = _find_job_state(ctx, cred->jobid);

	if ((j == NULL) || (j->revoked == (time_t)0))
		return false;

	if (cred->ctime <= j->revoked)
		return true;

	return false;
}

static bool
_credential_revoked(slurm_cred_ctx_t ctx, slurm_cred_t *cred)
{
	job_state_t  *j = NULL;

	_clear_expired_job_states(ctx);

	if (!(j = _find_job_state(ctx, cred->jobid))) {
		(void) _insert_job_state(ctx, cred->jobid);
		return false;
	}

	if (cred->ctime <= j->revoked) {
#if DEBUG_TIME
		char buf[64];
		debug3("cred for %u revoked. expires at %s",
		       j->jobid, timestr(&j->expiration, buf, 64));
#else
		debug3("cred for %u revoked. expires at %"PRIu64" UTS",
		       j->jobid, (uint64_t) j->expiration);
#endif
		return true;
	}

	return false;
}


static job_state_t *
_find_job_state(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	ListIterator  i = NULL;
	job_state_t  *j = NULL;

	i = list_iterator_create(ctx->job_list);
	while ((j = list_next(i))) {
		if (j->jobid == jobid)
			break;
	}
	list_iterator_destroy(i);
	return j;
}

static int
_find_cred_state(cred_state_t *c, slurm_cred_t *cred)
{
	return ((c->jobid == cred->jobid) && (c->stepid == cred->stepid) &&
		(c->ctime == cred->ctime));
}

static job_state_t *
_insert_job_state(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	job_state_t *j = _job_state_create(jobid);
	list_append(ctx->job_list, j);
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


static void
_clear_expired_job_states(slurm_cred_ctx_t ctx)
{
	static time_t last_scan = 0;
	time_t        now = time(NULL);
	ListIterator  i   = NULL;
	job_state_t  *j   = NULL;

	if ((now - last_scan) < 2)	/* Reduces slurmd overhead */
		return;
	last_scan = now;

	i = list_iterator_create(ctx->job_list);
	while ((j = list_next(i))) {
#if DEBUG_TIME
		char t1[64], t2[64], t3[64];
		if (j->revoked) {
			strcpy(t2, " revoked:");
			timestr(&j->revoked, (t2+9), (64-9));
		} else {
			t2[0] = '\0';
		}
		if (j->expiration) {
			strcpy(t3, " expires:");
			timestr(&j->revoked, (t3+9), (64-9));
		} else {
			t3[0] = '\0';
		}
		debug3("state for jobid %u: ctime:%s%s%s",
		       j->jobid, timestr(&j->ctime, t1, 64), t2, t3);
#else
		debug3("state for jobid %u: ctime:%"PRIu64" revoked:%"PRIu64" "
		       "expires:%"PRIu64"",
		       j->jobid, (uint64_t)j->ctime, (uint64_t)j->revoked,
		       (uint64_t)j->revoked);
#endif
		if (j->revoked && (now > j->expiration)) {
			list_delete_item(i);
		}
	}

	list_iterator_destroy(i);
}


static void
_clear_expired_credential_states(slurm_cred_ctx_t ctx)
{
	static time_t last_scan = 0;
	time_t        now = time(NULL);
	ListIterator  i   = NULL;
	cred_state_t *s   = NULL;

	if ((now - last_scan) < 2)	/* Reduces slurmd overhead */
		return;
	last_scan = now;

	i = list_iterator_create(ctx->state_list);
	while ((s = list_next(i))) {
		if (now > s->expiration)
			list_delete_item(i);
	}
	list_iterator_destroy(i);
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

	s->jobid      = cred->jobid;
	s->stepid     = cred->stepid;
	s->ctime      = cred->ctime;
	s->expiration = cred->ctime + ctx->expiry_window;

	return s;
}

static void
_cred_state_destroy(cred_state_t *s)
{
	xfree(s);
}


static void
_cred_state_pack_one(cred_state_t *s, Buf buffer)
{
	pack32(s->jobid, buffer);
	pack32(s->stepid, buffer);
	pack_time(s->ctime, buffer);
	pack_time(s->expiration, buffer);
}


static cred_state_t *
_cred_state_unpack_one(Buf buffer)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	safe_unpack32(&s->jobid, buffer);
	safe_unpack32(&s->stepid, buffer);
	safe_unpack_time(&s->ctime, buffer);
	safe_unpack_time(&s->expiration, buffer);
	return s;

unpack_error:
	_cred_state_destroy(s);
	return NULL;
}


static void
_job_state_pack_one(job_state_t *j, Buf buffer)
{
	pack32(j->jobid, buffer);
	pack_time(j->revoked, buffer);
	pack_time(j->ctime, buffer);
	pack_time(j->expiration, buffer);
}


static job_state_t *
_job_state_unpack_one(Buf buffer)
{
	char         t1[64], t2[64], t3[64];
	job_state_t *j = xmalloc(sizeof(*j));

	safe_unpack32(    &j->jobid,      buffer);
	safe_unpack_time( &j->revoked,    buffer);
	safe_unpack_time( &j->ctime,      buffer);
	safe_unpack_time( &j->expiration, buffer);

	if (j->revoked) {
		strcpy(t2, " revoked:");
		timestr(&j->revoked, (t2+9), (64-9));
	} else {
		t2[0] = '\0';
	}
	if (j->expiration) {
		strcpy(t3, " expires:");
		timestr(&j->revoked, (t3+9), (64-9));
	} else {
		t3[0] = '\0';
	}
	debug3("cred_unpack: job %u ctime:%s%s%s",
	       j->jobid, timestr (&j->ctime, t1, 64), t2, t3);

	if ((j->revoked) && (j->expiration == (time_t) MAX_TIME)) {
		info ("Warning: revoke on job %u has no expiration",
		      j->jobid);
		j->expiration = j->revoked + 600;
	}

	return j;

unpack_error:
	_job_state_destroy(j);
	return NULL;
}


static void
_cred_state_pack(slurm_cred_ctx_t ctx, Buf buffer)
{
	ListIterator  i = NULL;
	cred_state_t *s = NULL;

	pack32(list_count(ctx->state_list), buffer);

	i = list_iterator_create(ctx->state_list);
	while ((s = list_next(i)))
		_cred_state_pack_one(s, buffer);
	list_iterator_destroy(i);
}


static void
_cred_state_unpack(slurm_cred_ctx_t ctx, Buf buffer)
{
	time_t        now = time(NULL);
	uint32_t      n;
	int           i   = 0;
	cred_state_t *s   = NULL;

	safe_unpack32(&n, buffer);

	for (i = 0; i < n; i++) {
		if (!(s = _cred_state_unpack_one(buffer)))
			goto unpack_error;

		if (now < s->expiration)
			list_append(ctx->state_list, s);
		else
			_cred_state_destroy(s);
	}

	return;

unpack_error:
	error("Unable to unpack job credential state information");
	return;
}


static void
_job_state_pack(slurm_cred_ctx_t ctx, Buf buffer)
{
	ListIterator  i = NULL;
	job_state_t  *j = NULL;

	pack32((uint32_t) list_count(ctx->job_list), buffer);


	i = list_iterator_create(ctx->job_list);
	while ((j = list_next(i)))
		_job_state_pack_one(j, buffer);
	list_iterator_destroy(i);
}


static void
_job_state_unpack(slurm_cred_ctx_t ctx, Buf buffer)
{
	time_t       now = time(NULL);
	uint32_t     n   = 0;
	int          i   = 0;
	job_state_t *j   = NULL;

	safe_unpack32(&n, buffer);

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
static void _pack_sbcast_cred(sbcast_cred_t *sbcast_cred, Buf buffer)
{
	pack_time(sbcast_cred->ctime, buffer);
	pack_time(sbcast_cred->expiration, buffer);
	pack32(sbcast_cred->jobid, buffer);
	packstr(sbcast_cred->nodes, buffer);
}

/* Create an sbcast credential for the specified job and nodes
 *	including digital signature.
 * RET the sbcast credential or NULL on error */
sbcast_cred_t *create_sbcast_cred(slurm_cred_ctx_t ctx,
				  uint32_t job_id, char *nodes,
				  time_t expiration)
{
	Buf buffer;
	int rc;
	sbcast_cred_t *sbcast_cred;
	time_t now = time(NULL);

	xassert(ctx);
	if (_slurm_crypto_init() < 0)
		return NULL;

	sbcast_cred = xmalloc(sizeof(struct sbcast_cred));
	sbcast_cred->ctime      = now;
	sbcast_cred->expiration = expiration;
	sbcast_cred->jobid      = job_id;
	sbcast_cred->nodes      = xstrdup(nodes);

	buffer = init_buf(4096);
	_pack_sbcast_cred(sbcast_cred, buffer);
	rc = (*(ops.crypto_sign))(
		ctx->key, get_buf_data(buffer), get_buf_offset(buffer),
		&sbcast_cred->signature, &sbcast_cred->siglen);
	free_buf(buffer);

	if (rc) {
		error("sbcast_cred sign: %s",
		      (*(ops.crypto_str_error))(rc));
		delete_sbcast_cred(sbcast_cred);
		return NULL;
	}

	return sbcast_cred;
}

/* Delete an sbcast credential created using create_sbcast_cred() or
 *	unpack_sbcast_cred() */
void delete_sbcast_cred(sbcast_cred_t *sbcast_cred)
{
	if (sbcast_cred) {
		xfree(sbcast_cred->nodes);
		xfree(sbcast_cred->signature);
		xfree(sbcast_cred);
	}
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

static void _sbcast_cache_del(void *x)
{
	xfree(x);
}

/* Extract contents of an sbcast credential verifying the digital signature.
 * NOTE: We can only perform the full credential validation once with
 *	Munge without generating a credential replay error, so we only
 *	verify the credential for block one. All others must have a
 *	recent signature on file (in our cache) or the slurmd must have
 *	recently been restarted.
 * RET 0 on success, -1 on error */
int extract_sbcast_cred(slurm_cred_ctx_t ctx,
			sbcast_cred_t *sbcast_cred, uint16_t block_no,
			uint32_t *job_id, char **nodes)
{
	struct sbcast_cache *next_cache_rec;
	uint32_t sig_num = 0;
	int i, rc;
	time_t now = time(NULL);
	Buf buffer;

	*job_id = 0xffffffff;
	*nodes = NULL;
	xassert(ctx);

	if (_slurm_crypto_init() < 0)
		return -1;

	if (now > sbcast_cred->expiration)
		return -1;

	if (block_no == 1) {
		buffer = init_buf(4096);
		_pack_sbcast_cred(sbcast_cred, buffer);
		/* NOTE: the verification checks that the credential was
		 * created by SlurmUser or root */
		rc = (*(ops.crypto_verify_sign)) (
			ctx->key, get_buf_data(buffer), get_buf_offset(buffer),
			sbcast_cred->signature, sbcast_cred->siglen);
		free_buf(buffer);

		if (rc) {
			error("sbcast_cred verify: %s",
			      (*(ops.crypto_str_error))(rc));
			return -1;
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
			if (SLURM_DIFFTIME(now, crypto_restart_time) > 60)
				return -1;	/* restarted >60 secs ago */
			buffer = init_buf(4096);
			_pack_sbcast_cred(sbcast_cred, buffer);
			rc = (*(ops.crypto_verify_sign)) (
				ctx->key, get_buf_data(buffer),
				get_buf_offset(buffer),
				sbcast_cred->signature, sbcast_cred->siglen);
			free_buf(buffer);
			if (rc)
				err_str = (char *)(*(ops.crypto_str_error))(rc);
			if (err_str && strcmp(err_str, "Credential replayed")) {
				error("sbcast_cred verify: %s", err_str);
				return -1;
			}
			info("sbcast_cred verify: signature revalidated");
			_sbast_cache_add(sbcast_cred);
		}
	}

	*job_id = sbcast_cred->jobid;
	*nodes  = xstrdup(sbcast_cred->nodes);
	return 0;
}

/* Pack an sbcast credential into a buffer including the digital signature */
void pack_sbcast_cred(sbcast_cred_t *sbcast_cred, Buf buffer)
{
	static int bad_cred_test = -1;
	xassert(sbcast_cred);
	xassert(sbcast_cred->siglen > 0);

	_pack_sbcast_cred(sbcast_cred, buffer);
	if (bad_cred_test == -1) {
		char *sbcast_env = getenv("SLURM_SBCAST_AUTH_FAIL_TEST");
		if (sbcast_env)
			bad_cred_test = atoi(sbcast_env);
		else
			bad_cred_test = 0;
	}
	if (bad_cred_test > 0) {
		int i = ((int) time(NULL)) % sbcast_cred->siglen;
		char save_sig = sbcast_cred->signature[i];
		sbcast_cred->signature[i]++;
		packmem(sbcast_cred->signature, sbcast_cred->siglen, buffer);
		sbcast_cred->signature[i] = save_sig;
	} else {
		packmem(sbcast_cred->signature, sbcast_cred->siglen, buffer);
	}
}

/* Pack an sbcast credential into a buffer including the digital signature */
sbcast_cred_t *unpack_sbcast_cred(Buf buffer)
{
	uint32_t len;
	sbcast_cred_t *sbcast_cred;
	uint32_t uint32_tmp;

	sbcast_cred = xmalloc(sizeof(struct sbcast_cred));
	safe_unpack_time(&sbcast_cred->ctime, buffer);
	safe_unpack_time(&sbcast_cred->expiration, buffer);
	safe_unpack32(&sbcast_cred->jobid, buffer);
	safe_unpackstr_xmalloc(&sbcast_cred->nodes, &uint32_tmp, buffer);

	/* "sigp" must be last */
	safe_unpackmem_xmalloc(&sbcast_cred->signature, &len, buffer);
	sbcast_cred->siglen = len;
	xassert(len > 0);

	return sbcast_cred;

unpack_error:
	delete_sbcast_cred(sbcast_cred);
	return NULL;
}

void  print_sbcast_cred(sbcast_cred_t *sbcast_cred)
{
	info("Sbcast_cred: Jobid   %u", sbcast_cred->jobid         );
	info("Sbcast_cred: Nodes   %s", sbcast_cred->nodes         );
	info("Sbcast_cred: ctime   %s", slurm_ctime(&sbcast_cred->ctime) );
	info("Sbcast_cred: Expire  %s", slurm_ctime(&sbcast_cred->expiration) );
}
