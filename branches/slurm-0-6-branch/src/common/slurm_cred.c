/*****************************************************************************\
 * src/common/slurm_cred.c - SLURM job credential functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <slurm/slurm_errno.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>

/*
 * OpenSSL includes
 */
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#if WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include "src/common/macros.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/common/slurm_cred.h"

/* 
 * Default credential information expiration window:
 */
#define DEFAULT_EXPIRATION_WINDOW 600

#define MAX_TIME 0x7fffffff

/* 
 * slurm job credential state 
 * 
 */
typedef struct {
	uint32_t jobid;		/* SLURM job id for this credential         */
	uint32_t stepid;	/* SLURM step id for this credential        */
	time_t   expiration;    /* Time at which cred is no longer good     */
} cred_state_t;

/*
 * slurm job state information
 * tracks jobids for which all future credentials have been revoked
 *
 */
typedef struct {
	uint32_t jobid;         
	bool     revoked;       /* True if all creds for jobid are revoked  */
	time_t   ctime;         /* Time that this entry was created         */
	time_t   expiration;    /* Time at which credentials were revoked   */
} job_state_t;


/*
 * Completion of slurm credential context
 */
enum ctx_type {
	SLURM_CRED_CREATOR,
	SLURM_CRED_VERIFIER
};

struct slurm_cred_context {
#ifndef NDEBUG
#  define CRED_CTX_MAGIC 0x0c0c0c
	int magic;
#endif
#if WITH_PTHREADS
	pthread_mutex_t mutex;
#endif
	enum ctx_type  type;       /* type of context (creator or verifier) */
	EVP_PKEY      *key;        /* private or public key                 */
	List           job_list;   /* List of used jobids (for verifier)    */
	List           state_list; /* List of cred states (for verifier)    */

	int   expiry_window;       /* expiration window for cached creds    */

	EVP_PKEY      *exkey;      /* Old public key if key is updated      */
	time_t         exkey_exp;  /* Old key expiration time               */
};


/*
 * Completion of slurm job credential type:
 *
 */
struct slurm_job_credential {
#ifndef NDEBUG
#  define CRED_MAGIC 0x0b0b0b
	int      magic;
#endif
#ifdef  WITH_PTHREADS
	pthread_mutex_t mutex;
#endif
	uint32_t jobid;        /* Job ID associated with this credential    */
	uint32_t stepid;       /* Job step ID for this credential           */
	uid_t    uid;          /* user for which this cred is valid         */
	time_t   ctime;        /* time of credential creation               */
	char    *nodes;        /* list of hostnames for which the cred is ok*/
        uint32_t ntask_cnt;    /* Number of hosts in the list above         */
        uint32_t *ntask;       /* Number of tasks on each host              */

	unsigned char *signature; /* credential signature                   */
	unsigned int siglen;      /* signature length in bytes              */
};



/*
 * Static prototypes:
 */

static slurm_cred_ctx_t _slurm_cred_ctx_alloc(void);
static slurm_cred_t     _slurm_cred_alloc(void);

static int  _ctx_update_private_key(slurm_cred_ctx_t ctx, const char *path);
static int  _ctx_update_public_key(slurm_cred_ctx_t ctx, const char *path);
static bool _exkey_is_valid(slurm_cred_ctx_t ctx);

static cred_state_t * _cred_state_create(slurm_cred_ctx_t ctx, slurm_cred_t c);
static job_state_t  * _job_state_create(uint32_t jobid);
static void           _cred_state_destroy(cred_state_t *cs);
static void           _job_state_destroy(job_state_t   *js);

static job_state_t  * _find_job_state(slurm_cred_ctx_t ctx, uint32_t jobid);
static job_state_t  * _insert_job_state(slurm_cred_ctx_t ctx,  uint32_t jobid);
static int            _find_cred_state(cred_state_t *c, slurm_cred_t cred);

static void _insert_cred_state(slurm_cred_ctx_t ctx, slurm_cred_t cred);
static void _clear_expired_job_states(slurm_cred_ctx_t ctx);
static void _clear_expired_credential_states(slurm_cred_ctx_t ctx);
static void _verifier_ctx_init(slurm_cred_ctx_t ctx);

static bool _credential_replayed(slurm_cred_ctx_t ctx, slurm_cred_t cred);
static bool _credential_revoked(slurm_cred_ctx_t ctx, slurm_cred_t cred);

static EVP_PKEY * _read_private_key(const char *path);
static EVP_PKEY * _read_public_key(const char  *path);

static int _slurm_cred_sign(slurm_cred_ctx_t ctx, slurm_cred_t cred);
static int _slurm_cred_verify_signature(slurm_cred_ctx_t ctx, slurm_cred_t c);

static job_state_t  * _job_state_unpack_one(Buf buffer);
static cred_state_t * _cred_state_unpack_one(Buf buffer);

static void _pack_cred(slurm_cred_t cred, Buf buffer);
static void _job_state_unpack(slurm_cred_ctx_t ctx, Buf buffer);
static void _job_state_pack(slurm_cred_ctx_t ctx, Buf buffer);
static void _cred_state_unpack(slurm_cred_ctx_t ctx, Buf buffer);
static void _cred_state_pack(slurm_cred_ctx_t ctx, Buf buffer);
static void _job_state_pack_one(job_state_t *j, Buf buffer);
static void _cred_state_pack_one(cred_state_t *s, Buf buffer);

#ifndef DISABLE_LOCALTIME
static char * timestr (const time_t *tp, char *buf, size_t n);
#endif

slurm_cred_ctx_t 
slurm_cred_creator_ctx_create(const char *path)
{
	slurm_cred_ctx_t ctx = NULL;
	
	xassert(path != NULL);

	ctx = _slurm_cred_ctx_alloc();
	slurm_mutex_lock(&ctx->mutex);

	ctx->type = SLURM_CRED_CREATOR;

	if (!(ctx->key = _read_private_key(path))) 
		goto fail;

	slurm_mutex_unlock(&ctx->mutex);
	return ctx;

    fail:
	slurm_mutex_unlock(&ctx->mutex);
	slurm_cred_ctx_destroy(ctx);
	return NULL;
}


slurm_cred_ctx_t 
slurm_cred_verifier_ctx_create(const char *path)
{
	slurm_cred_ctx_t ctx = NULL;

	xassert(path != NULL);

	ctx = _slurm_cred_ctx_alloc();
	slurm_mutex_lock(&ctx->mutex);

	ctx->type = SLURM_CRED_VERIFIER;

	if (!(ctx->key = _read_public_key(path)))
		goto fail;

	_verifier_ctx_init(ctx);

	slurm_mutex_unlock(&ctx->mutex);
	return ctx;

    fail:
	slurm_mutex_unlock(&ctx->mutex);
	slurm_cred_ctx_destroy(ctx);
	return NULL;
}


void
slurm_cred_ctx_destroy(slurm_cred_ctx_t ctx)
{
	if (ctx == NULL)
		return;

	slurm_mutex_lock(&ctx->mutex);
	xassert(ctx->magic == CRED_CTX_MAGIC);

	if (ctx->key)
		EVP_PKEY_free(ctx->key);
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
	if (ctx->type == SLURM_CRED_CREATOR)
		return _ctx_update_private_key(ctx, path);
	else
		return _ctx_update_public_key(ctx, path);
}


slurm_cred_t
slurm_cred_create(slurm_cred_ctx_t ctx, slurm_cred_arg_t *arg)
{
	slurm_cred_t cred = NULL;

	xassert(ctx != NULL);
	xassert(arg != NULL);

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type == SLURM_CRED_CREATOR);

	cred = _slurm_cred_alloc();

	xassert(cred != NULL);

	slurm_mutex_lock(&cred->mutex);

	xassert(cred->magic == CRED_MAGIC);

	cred->jobid  = arg->jobid;
	cred->stepid = arg->stepid;
	cred->uid    = arg->uid;
	cred->nodes  = xstrdup(arg->hostlist);
        cred->ntask_cnt = arg->ntask_cnt;
        cred->ntask  = NULL;
        if (cred->ntask_cnt > 0) {
                cred->ntask =  xmalloc(cred->ntask_cnt * sizeof(int));
                memcpy(cred->ntask, arg->ntask, cred->ntask_cnt * sizeof(int));
        }
	cred->ctime  = time(NULL);

	if (_slurm_cred_sign(ctx, cred) < 0) 
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

slurm_cred_t
slurm_cred_faker(slurm_cred_arg_t *arg)
{
	int fd;
	slurm_cred_t cred = NULL;

	xassert(arg != NULL);

	cred = _slurm_cred_alloc();

	slurm_mutex_lock(&cred->mutex);

	cred->jobid  = arg->jobid;
	cred->stepid = arg->stepid;
        cred->uid    = arg->uid;
	cred->nodes  = xstrdup(arg->hostlist);
        cred->ntask_cnt = arg->ntask_cnt;
        cred->ntask  = NULL;
        if (cred->ntask_cnt > 0) {
                 cred->ntask =  xmalloc(cred->ntask_cnt * sizeof(int));
                 memcpy(cred->ntask, arg->ntask, cred->ntask_cnt * sizeof(int));
        }
	cred->ctime  = time(NULL);
	cred->siglen = SLURM_IO_KEY_SIZE;

	cred->signature = xmalloc(cred->siglen * sizeof(char));

	if ((fd = open("/dev/urandom", O_RDONLY)) >= 0) {
		read(fd, cred->signature, cred->siglen);
		if (close(fd) < 0)
		error ("close(/dev/urandom): %m");
	} else {	/* Note: some systems lack this file */
		unsigned int i;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		i = (unsigned int) (tv.tv_sec + tv.tv_usec);
		srand((unsigned int) i);
		for (i=0; i<cred->siglen; i++)
			cred->signature[i] = (rand() & 0xff);
	}

	slurm_mutex_unlock(&cred->mutex);
	return cred;
    	
}


int
slurm_cred_verify(slurm_cred_ctx_t ctx, slurm_cred_t cred, 
		  slurm_cred_arg_t *arg)
{
	time_t now = time(NULL);

	xassert(ctx  != NULL);
	xassert(cred != NULL);
	xassert(arg  != NULL);

	slurm_mutex_lock(&ctx->mutex);
	slurm_mutex_lock(&cred->mutex);

	xassert(ctx->magic  == CRED_CTX_MAGIC);
	xassert(ctx->type   == SLURM_CRED_VERIFIER);
	xassert(cred->magic == CRED_MAGIC);

	if (_slurm_cred_verify_signature(ctx, cred) < 0) {
		slurm_seterrno(ESLURMD_INVALID_JOB_CREDENTIAL);
		goto error;
	}

	if (now > (cred->ctime + ctx->expiry_window)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_EXPIRED);
		goto error;
	}

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
	arg->hostlist = xstrdup(cred->nodes);
        arg->ntask_cnt = cred->ntask_cnt;
        arg->ntask     = NULL;
        if (arg->ntask_cnt > 0) {
                arg->ntask =  xmalloc(arg->ntask_cnt * sizeof(int));
                memcpy(arg->ntask, cred->ntask, arg->ntask_cnt * sizeof(int));
        }

	slurm_mutex_unlock(&cred->mutex);

	return SLURM_SUCCESS;

    error:
	slurm_mutex_unlock(&ctx->mutex);
	slurm_mutex_unlock(&cred->mutex);
	return SLURM_ERROR;
}


void
slurm_cred_destroy(slurm_cred_t cred)
{
	if (cred == NULL)
		return;

	xassert(cred->magic == CRED_MAGIC);

	slurm_mutex_lock(&cred->mutex);
	if (cred->nodes)
		xfree(cred->nodes);
        if (cred->ntask)
                xfree(cred->ntask);
        cred->ntask = NULL;
	if (cred->signature)
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
slurm_cred_rewind(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	int rc = 0;

	xassert(ctx != NULL);

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	rc = list_delete_all(ctx->state_list, (ListFindF) _find_cred_state, cred);

	slurm_mutex_unlock(&ctx->mutex);

	return (rc > 0 ? SLURM_SUCCESS : SLURM_FAILURE); 
}

int
slurm_cred_revoke(slurm_cred_ctx_t ctx, uint32_t jobid)
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

	if (j->revoked == true) {
		slurm_seterrno(EEXIST);
		goto error;
	}

	j->revoked = true;

	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_SUCCESS;

    error:
	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_FAILURE;
}

int
slurm_cred_begin_expiration(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	char buf[64];
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

	debug2 ("set revoke expiration for jobid %u to %s",
	        j->jobid, timestr (&j->expiration, buf, 64) );

	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_SUCCESS;

    error:
	slurm_mutex_unlock(&ctx->mutex);
	return SLURM_ERROR;
}

int
slurm_cred_get_signature(slurm_cred_t cred, char **datap, int *datalen)
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

void
slurm_cred_pack(slurm_cred_t cred, Buf buffer)
{
	xassert(cred != NULL);
	xassert(cred->magic == CRED_MAGIC);

	slurm_mutex_lock(&cred->mutex);

	_pack_cred(cred, buffer);
	xassert(cred->siglen > 0);
	packmem((char *) cred->signature, (uint16_t) cred->siglen, buffer);

	slurm_mutex_unlock(&cred->mutex);

	return;
}

slurm_cred_t
slurm_cred_unpack(Buf buffer)
{
	uint16_t     len;
	uint32_t     tmpint;
	slurm_cred_t cred = NULL;
	char       **sigp;

	xassert(buffer != NULL);

	cred = _slurm_cred_alloc();
	slurm_mutex_lock(&cred->mutex);

	sigp = (char **) &cred->signature;

	safe_unpack32(          &cred->jobid,        buffer);
	safe_unpack32(          &cred->stepid,       buffer);
	safe_unpack32(          &tmpint,             buffer);
	cred->uid = tmpint;
	safe_unpackstr_xmalloc( &cred->nodes, &len,  buffer);
	safe_unpack32(          &cred->ntask_cnt,     buffer);
        if (cred->ntask_cnt > 0)
                safe_unpack32_array(&cred->ntask, &tmpint,  buffer);
	safe_unpack_time(       &cred->ctime,        buffer);
	safe_unpackmem_xmalloc( sigp,         &len,  buffer);

	xassert(len > 0);

	cred->siglen = len;

	slurm_mutex_unlock(&cred->mutex);
	return cred;

    unpack_error:
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
slurm_cred_print(slurm_cred_t cred)
{
        int i;

	if (cred == NULL)
		return;

	slurm_mutex_lock(&cred->mutex);

	xassert(cred->magic == CRED_MAGIC);

	info("Cred: Jobid   %u",  cred->jobid         );
	info("Cred: Stepid  %u",  cred->jobid         );
	info("Cred: UID     %lu", (u_long) cred->uid  );
	info("Cred: Nodes   %s",  cred->nodes         );
	info("Cred: ntask_cnt %d", cred->ntask_cnt     ); 
        info("Cred: ntask: ");                            
        for (i=0; i<cred->ntask_cnt; i++)                 
                info("ntask[%d] = %d ", i, cred->ntask[i]);
	info("Cred: ctime   %s",  ctime(&cred->ctime) );
	info("Cred: siglen  %d",  cred->siglen        );
	slurm_mutex_unlock(&cred->mutex);

}


static EVP_PKEY *
_read_private_key(const char *path)
{
	FILE     *fp = NULL;
	EVP_PKEY *pk = NULL;

	xassert(path != NULL);

	if (!(fp = fopen(path, "r"))) {
		error ("can't open key file '%s' : %m", path);
		return NULL;
	}

	if (!PEM_read_PrivateKey(fp, &pk, NULL, NULL))
		error ("PEM_read_PrivateKey [%s]: %m", path);

	fclose(fp);

	return pk;
}


static EVP_PKEY *
_read_public_key(const char *path)
{
	FILE     *fp = NULL;
	EVP_PKEY *pk = NULL;

	xassert(path != NULL);

	if ((fp = fopen(path, "r")) == NULL) {
		error ("can't open public key '%s' : %m ", path);
		return NULL;
	}

	if (!PEM_read_PUBKEY(fp, &pk, NULL, NULL)) 
		error("PEM_read_PUBKEY[%s]: %m", path);

	fclose(fp);

	return pk;
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
	EVP_PKEY *pk   = NULL;
	EVP_PKEY *tmpk = NULL;

	xassert(ctx != NULL);

	if (!(pk = _read_private_key(path)))
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_CREATOR);

	tmpk = ctx->key;
	ctx->key = pk;

	slurm_mutex_unlock(&ctx->mutex);

	EVP_PKEY_free(tmpk);

	return SLURM_SUCCESS;
}


static int
_ctx_update_public_key(slurm_cred_ctx_t ctx, const char *path)
{
	EVP_PKEY *pk   = NULL;

	xassert(ctx != NULL);

	if (!(pk = _read_public_key(path)))
		return SLURM_ERROR;

	slurm_mutex_lock(&ctx->mutex);

	xassert(ctx->magic == CRED_CTX_MAGIC);
	xassert(ctx->type  == SLURM_CRED_VERIFIER);

	if (ctx->exkey) 
		EVP_PKEY_free(ctx->exkey);

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
	if (!ctx->exkey) return false;
	
	if (time(NULL) > ctx->exkey_exp) {
		debug2("old job credential key slurmd expired");
		EVP_PKEY_free(ctx->exkey);
		ctx->exkey = NULL;
		return false;
	}

	return true;
}


static slurm_cred_ctx_t
_slurm_cred_ctx_alloc(void)
{
	slurm_cred_ctx_t ctx = xmalloc(sizeof(*ctx));

	slurm_mutex_init(&ctx->mutex);
	slurm_mutex_lock(&ctx->mutex);

	ctx->key           = NULL;
	ctx->job_list      = NULL;
	ctx->state_list    = NULL;
	ctx->expiry_window = DEFAULT_EXPIRATION_WINDOW;

	ctx->exkey         = NULL;
	ctx->exkey_exp     = (time_t) -1;

	xassert(ctx->magic = CRED_CTX_MAGIC);

	slurm_mutex_unlock(&ctx->mutex);
	return ctx;
}

static slurm_cred_t 
_slurm_cred_alloc(void)
{
	slurm_cred_t cred = xmalloc(sizeof(*cred));

	slurm_mutex_init(&cred->mutex);

	cred->jobid     = 0;
	cred->stepid    = 0;
	cred->uid       = (uid_t) -1;
	cred->nodes     = NULL;
        cred->ntask_cnt  = 0; 
	cred->ntask     = NULL;
	cred->signature = NULL;
	cred->siglen    = 0;

	xassert(cred->magic = CRED_MAGIC);

	return cred;
}

static const char *
_ssl_error(void)
{
	return ERR_reason_error_string(ERR_get_error()); 
}

#ifdef EXTREME_DEBUG
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
_slurm_cred_sign(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	EVP_MD_CTX ectx;
	Buf           buffer;
	int           rc    = SLURM_SUCCESS;
	unsigned int *lenp  = &cred->siglen;
	int           ksize = EVP_PKEY_size(ctx->key);

	/*
	 * Allocate memory for signature: at most EVP_PKEY_size() bytes
	 */
	cred->signature = xmalloc(ksize * sizeof(unsigned char));

	buffer = init_buf(4096);
	_pack_cred(cred, buffer);

	EVP_SignInit(&ectx, EVP_sha1());
	EVP_SignUpdate(&ectx, get_buf_data(buffer), get_buf_offset(buffer));

	if (!(EVP_SignFinal(&ectx, cred->signature, lenp, ctx->key))) {
		ERR_print_errors_fp(log_fp());
		rc = SLURM_ERROR;
	}

#ifdef HAVE_EVP_MD_CTX_CLEANUP
	/* Note: Likely memory leak if this function is absent */
	EVP_MD_CTX_cleanup(&ectx);
#endif
	free_buf(buffer);

	return rc;
}

static int
_slurm_cred_verify_signature(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	EVP_MD_CTX     ectx;
	Buf            buffer;
	int            rc;
	unsigned char *sig    = cred->signature;
	int            siglen = cred->siglen; 

	buffer = init_buf(4096);
	_pack_cred(cred, buffer);

	debug("Checking credential with %d bytes of sig data", siglen);

	EVP_VerifyInit(&ectx, EVP_sha1());
	EVP_VerifyUpdate(&ectx, get_buf_data(buffer), get_buf_offset(buffer));

	if (!(rc = EVP_VerifyFinal(&ectx, sig, siglen, ctx->key))) {
		/*
		 * Check against old key if one exists and is valid
		 */
		if (_exkey_is_valid(ctx))
			rc = EVP_VerifyFinal(&ectx, sig, siglen, ctx->exkey);
	}

	if (!rc) {
		ERR_load_crypto_strings();
		info("Credential signature check: %s", _ssl_error());
		rc = SLURM_ERROR;
	} else
		rc = SLURM_SUCCESS;

#ifdef HAVE_EVP_MD_CTX_CLEANUP
	/* Note: Likely memory leak if this function is absent */
	EVP_MD_CTX_cleanup(&ectx);
#endif
	free_buf(buffer);

	return rc;
}


static void
_pack_cred(slurm_cred_t cred, Buf buffer)
{
	pack32(           cred->jobid,  buffer);
	pack32(           cred->stepid, buffer);
	pack32((uint32_t) cred->uid,    buffer);
	packstr(          cred->nodes,  buffer);
	pack32(           cred->ntask_cnt, buffer);
        if (cred->ntask_cnt > 0)
                pack32_array( cred->ntask, cred->ntask_cnt, buffer);
	pack_time(        cred->ctime,  buffer);
}


static bool
_credential_replayed(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	ListIterator  i = NULL;
	cred_state_t *s = NULL;

	_clear_expired_credential_states(ctx);
	
	i = list_iterator_create(ctx->state_list);

	while ((s = list_next(i))) {
		if ((s->jobid == cred->jobid) && (s->stepid == cred->stepid))
			break;
	}

	list_iterator_destroy(i);

	/*
	 * If we found a match, this credential is being replayed.
	 */
	if (s) return true; 

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
	if (buf == NULL) disabled=1;
	if (disabled) return NULL;
#endif
	if (!localtime_r (tp, &tmval))
		error ("localtime: %m");
	strftime (buf, n, fmt, &tmval);
	return (buf);
}

extern bool
slurm_cred_revoked(slurm_cred_ctx_t ctx, uint32_t jobid)
{
	job_state_t  *j = _find_job_state(ctx, jobid);
	if (j && j->revoked)
		return true;
	return false;
}

static bool
_credential_revoked(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	job_state_t  *j = NULL;

	_clear_expired_job_states(ctx);

	if (!(j = _find_job_state(ctx, cred->jobid))) 
		(void) _insert_job_state(ctx, cred->jobid);
	else if (j->revoked) {
		char buf[64];
		debug ("cred for %d revoked. expires at %s", 
                       j->jobid, timestr (&j->expiration, buf, 64));
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
	while ((j = list_next(i)) && (j->jobid != jobid)) {;}
	list_iterator_destroy(i);
	return j;
}

static int
_find_cred_state(cred_state_t *c, slurm_cred_t cred)
{
	return ((c->jobid == cred->jobid) && (c->stepid == cred->stepid));
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
	j->revoked    = false;
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
	char          t1[64], t2[64];
	time_t        now = time(NULL);
	ListIterator  i   = NULL;
	job_state_t  *j   = NULL;

	i = list_iterator_create(ctx->job_list);

	while ((j = list_next(i))) {
		debug3 ("job state %u: ctime:%s%s%s",
		        j->jobid, timestr (&j->ctime, t1, 64),
			j->revoked ? " revoked:" : " expires:",
		        timestr (&j->ctime, t1, 64),
			j->revoked ? timestr (&j->expiration, t2, 64) : "");

		if (j->revoked && (now > j->expiration)) {
			list_delete(i);
		}
	}

	list_iterator_destroy(i);
}


static void
_clear_expired_credential_states(slurm_cred_ctx_t ctx)
{
	time_t        now = time(NULL);
	ListIterator  i   = NULL;
	cred_state_t *s   = NULL;

	i = list_iterator_create(ctx->state_list);

	while ((s = list_next(i))) {
		if (now > s->expiration)
			list_delete(i);
	}

	list_iterator_destroy(i);
}


static void 
_insert_cred_state(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	cred_state_t *s = _cred_state_create(ctx, cred);
	list_append(ctx->state_list, s);
}


static cred_state_t *
_cred_state_create(slurm_cred_ctx_t ctx, slurm_cred_t cred)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	s->jobid      = cred->jobid;
	s->stepid     = cred->stepid;
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
	pack_time(s->expiration, buffer);
}


static cred_state_t *
_cred_state_unpack_one(Buf buffer)
{
	cred_state_t *s = xmalloc(sizeof(*s));

	safe_unpack32(&s->jobid, buffer);
	safe_unpack32(&s->stepid, buffer);
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
	pack16((uint16_t) j->revoked, buffer);
	pack_time(j->ctime, buffer);
	pack_time(j->expiration, buffer);
}


static job_state_t *
_job_state_unpack_one(Buf buffer)
{
	char         buf1[64], buf2[64];
	uint16_t     revoked = 0;
	job_state_t *j = xmalloc(sizeof(*j));

	safe_unpack32(    &j->jobid,      buffer);
	safe_unpack16(    &revoked,       buffer);
	safe_unpack_time( &j->ctime,      buffer);
	safe_unpack_time( &j->expiration, buffer);

	debug3("cred_unpack:job %d ctime:%s%s%s",
               j->jobid, 
	       timestr (&j->ctime, buf1, 64), 
	       (revoked ? " revoked:" : " expires:"),
	       revoked ? timestr (&j->expiration, buf2, 64) : "");

	if (revoked) {
		j->revoked = true;
		if (j->expiration == (time_t) MAX_TIME) {
			info ("Warning: revoke on job %d has no expiration", 
			      j->jobid);
		}
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
		else
			debug3 ("not appending expired job %u state", j->jobid);
	}

	return;

    unpack_error:
	error("Unable to unpack job state information");
	return;
}


