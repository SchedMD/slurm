/*****************************************************************************\
 *  cred.c - Slurm job and sbcast credential functions
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
#include "src/common/group_cache.h"
#include "src/common/identity.h"
#include "src/common/io_hdr.h"
#include "src/common/job_resources.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_time.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/cred.h"
#include "src/interfaces/gres.h"

typedef struct {
	slurm_cred_t *(*cred_create)	(slurm_cred_arg_t *cred_arg,
					 bool sign_it,
					 uint16_t protocol_version);
	slurm_cred_t *(*cred_unpack)	(buf_t *buffer,
					 uint16_t protocol_version);
	char *(*create_net_cred)	(void *addrs,
					 uint16_t protocol_version);
	void *(*extract_net_cred)	(char *net_cred,
					 uint16_t protocol_version);
	sbcast_cred_t *(*sbcast_create)	(sbcast_cred_arg_t *cred,
					 uint16_t protocol_version);
	sbcast_cred_t *(*sbcast_unpack)	(buf_t *buffer, bool verify,
					 uint16_t protocol_version);
} slurm_cred_ops_t;

/*
 * These strings must be in the same order as the fields declared
 * for slurm_cred_ops_t.
 */
static const char *syms[] = {
	"cred_p_create",
	"cred_p_unpack",
	"cred_p_create_net_cred",
	"cred_p_extract_net_cred",
	"sbcast_p_create",
	"sbcast_p_unpack",
};

static slurm_cred_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static time_t cred_restart_time = (time_t) 0;
static int cred_expire = DEFAULT_EXPIRATION_WINDOW;
static bool enable_nss_slurm = false;
static bool enable_send_gids = true;

/* Initialize the plugin. */
extern int cred_g_init(void)
{
	char *tok;
	char *plugin_type = "cred";
	int retval = SLURM_SUCCESS;
	char *type = NULL;

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

	/* allow the bare plugin name. also handle slurm type quirks */
	type = slurm_conf.cred_type;
	if (!xstrncmp(type, "auth/", 5) || !xstrncmp(type, "cred/", 5))
		type += 5;
	if (!xstrcmp(type, "slurm"))
		type = xstrdup("auth/slurm");
	else
		type = xstrdup_printf("cred/%s", type);

	slurm_mutex_lock(&g_context_lock);
	if (cred_restart_time == (time_t) 0)
		cred_restart_time = time(NULL);
	if (g_context)
		goto done;

	g_context = plugin_context_create(plugin_type, type,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.cred_type);
		retval = SLURM_ERROR;
		goto done;
	}

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);

	return(retval);
}

/* Terminate the plugin and release all memory. */
extern int cred_g_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

extern int cred_expiration(void)
{
	return cred_expire;
}

extern slurm_cred_t *slurm_cred_create(slurm_cred_arg_t *arg, bool sign_it,
				       uint16_t protocol_version)
{
	slurm_cred_t *cred = NULL;
	int i = 0, sock_recs = 0;
	bool release_id = false;
	identity_t fake_id = { .uid = arg->uid, .gid = arg->gid, .fake = true };

	xassert(arg);
	xassert(g_context);

	if (arg->uid == SLURM_AUTH_NOBODY) {
		error("%s: refusing to create job %u credential for invalid user nobody",
		      __func__, arg->step_id.job_id);
		return NULL;
	}

	if (arg->gid == SLURM_AUTH_NOBODY) {
		error("%s: refusing to create job %u credential for invalid group nobody",
		      __func__, arg->step_id.job_id);
		return NULL;
	}

	if (arg->sock_core_rep_count) {
		for (i = 0; i < arg->job_nhosts; i++) {
			sock_recs += arg->sock_core_rep_count[i];
			if (sock_recs >= arg->job_nhosts)
				break;
		}
		i++;
	}
	arg->core_array_size = i;

	if (!arg->id && (enable_nss_slurm || enable_send_gids)) {
		release_id = true;
		if (!(arg->id = fetch_identity(arg->uid, arg->gid,
					       enable_nss_slurm))) {
			error("%s: fetch_identity() failed", __func__);
			return NULL;
		}
	} else if (!arg->id) {
		arg->id = &fake_id;
	}

	identity_debug2(arg->id, __func__);
	cred = (*(ops.cred_create))(arg, sign_it, protocol_version);

	/* Release any values populated through _fill_cred_gids(). */
	if (release_id)
		FREE_NULL_IDENTITY(arg->id);

	return cred;
}

extern slurm_cred_t *slurm_cred_faker(slurm_cred_arg_t *arg)
{
	/*
	 * Force this on to ensure pw_name, ngid, gids are all populated.
	 */
	enable_send_gids = true;

	return slurm_cred_create(arg, true, SLURM_PROTOCOL_VERSION);
}

extern void slurm_cred_free_args(slurm_cred_arg_t *arg)
{
	if (!arg)
		return;

	FREE_NULL_IDENTITY(arg->id);
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
	xfree(arg->job_node_addrs);
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
extern slurm_cred_arg_t *slurm_cred_get_args(slurm_cred_t *cred)
{
	xassert(cred);

	slurm_rwlock_rdlock(&cred->mutex);
	return cred->arg;
}

extern void *slurm_cred_get(slurm_cred_t *cred,
			    cred_data_enum_t cred_data_type)
{
	void *rc = NULL;

	xassert(cred);

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
	case CRED_DATA_JOB_NODE_ADDRS:
		rc = (void *) cred->arg->job_node_addrs;
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
extern slurm_cred_arg_t *slurm_cred_verify(slurm_cred_t *cred)
{
	time_t now = time(NULL);
	int errnum;

	xassert(cred);
	xassert(g_context);

	slurm_rwlock_rdlock(&cred->mutex);
	xassert(cred->magic == CRED_MAGIC);

	/* NOTE: the verification checks that the credential was
	 * created by SlurmUser or root */
	if (!cred->verified) {
		slurm_seterrno(ESLURMD_INVALID_JOB_CREDENTIAL);
		goto error;
	}

	if (now > (cred->ctime + cred_expire)) {
		slurm_seterrno(ESLURMD_CREDENTIAL_EXPIRED);
		goto error;
	}

	/* coverity[missing_unlock] */
	return cred->arg;

error:
	errnum = slurm_get_errno();
	slurm_rwlock_unlock(&cred->mutex);
	slurm_seterrno(errnum);
	return NULL;
}


extern void slurm_cred_destroy(slurm_cred_t *cred)
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

extern char *slurm_cred_get_signature(slurm_cred_t *cred)
{
	char *sig = NULL;

	xassert(cred);

	slurm_rwlock_rdlock(&cred->mutex);
	sig = xstrdup(cred->signature);
	slurm_rwlock_unlock(&cred->mutex);

	return sig;
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
extern void format_core_allocs(slurm_cred_t *credential, char *node_name,
			       uint16_t cpus, char **job_alloc_cores,
			       char **step_alloc_cores, uint64_t *job_mem_limit,
			       uint64_t *step_mem_limit)
{
	slurm_cred_arg_t *cred = credential->arg;
	bitstr_t	*job_core_bitmap, *step_core_bitmap;
	hostlist_t *hset = NULL;
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

	/* Scale CPU count, same as slurmd/req.c:_get_ncpus() */
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
			  list_t **job_gres_list, list_t **step_gres_list)
{
	slurm_cred_arg_t *cred = credential->arg;
	hostlist_t *hset = NULL;
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

extern void slurm_cred_pack(slurm_cred_t *cred, buf_t *buffer,
			    uint16_t protocol_version)
{
	xassert(cred);
	xassert(cred->magic == CRED_MAGIC);

	slurm_rwlock_rdlock(&cred->mutex);

	xassert(cred->buffer);
	xassert(cred->buf_version == protocol_version);
	packbuf(cred->buffer, buffer);

	slurm_rwlock_unlock(&cred->mutex);
}

extern slurm_cred_t *slurm_cred_unpack(buf_t *buffer, uint16_t protocol_version)
{
	return (*(ops.cred_unpack))(buffer, protocol_version);
}

extern slurm_cred_t *slurm_cred_alloc(bool alloc_arg)
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

/*****************************************************************************\
 *****************       SBCAST CREDENTIAL FUNCTIONS        ******************
\*****************************************************************************/

/* Create an sbcast credential for the specified job and nodes
 *	including digital signature.
 * RET the sbcast credential or NULL on error */
extern sbcast_cred_t *create_sbcast_cred(sbcast_cred_arg_t *arg,
					 uid_t uid, gid_t gid,
					 uint16_t protocol_version)
{
	sbcast_cred_t *sbcast_cred;
	bool release_id = false;
	identity_t fake_id = { .uid = uid, .gid = gid, .fake = true };

	xassert(g_context);

	if (!arg->id && enable_send_gids) {
		release_id = true;
		if (!(arg->id = fetch_identity(uid, gid, false))) {
			error("%s: fetch_identity() failed", __func__);
			return NULL;
		}
	} else if (!arg->id) {
		arg->id = &fake_id;
	}

	if (!(sbcast_cred = (*(ops.sbcast_create))(arg, protocol_version)))
		error("%s: failed to create sbcast credential", __func__);

	if (release_id)
		FREE_NULL_IDENTITY(arg->id);

	return sbcast_cred;
}

/* Delete an sbcast credential created using create_sbcast_cred() or
 *	unpack_sbcast_cred() */
extern void delete_sbcast_cred(sbcast_cred_t *sbcast_cred)
{
	if (!sbcast_cred)
		return;

	FREE_NULL_IDENTITY(sbcast_cred->arg.id);
	xfree(sbcast_cred->arg.nodes);
	FREE_NULL_BUFFER(sbcast_cred->buffer);
	xfree(sbcast_cred->signature);
	xfree(sbcast_cred);
}

/* Pack an sbcast credential into a buffer including the digital signature */
extern void pack_sbcast_cred(sbcast_cred_t *sbcast_cred, buf_t *buffer,
			     uint16_t protocol_version)
{
	xassert(sbcast_cred);

	if (sbcast_cred->buffer) {
		/* already includes signature */
		packbuf(sbcast_cred->buffer, buffer);
	} else {
		/* credential only uses signature */
		packstr(sbcast_cred->signature, buffer);
	}
}

extern sbcast_cred_t *unpack_sbcast_cred(buf_t *buffer, void *msg,
					 uint16_t protocol_version)
{
	file_bcast_msg_t *bmsg = msg;
	bool verify = false;

	if (bmsg && (bmsg->block_no == 1) && !(bmsg->flags & FILE_BCAST_SO))
		verify = true;

	return (*(ops.sbcast_unpack))(buffer, verify, protocol_version);
}

extern void print_sbcast_cred(sbcast_cred_t *sbcast_cred)
{
	info("Sbcast_cred: JobId   %u", sbcast_cred->arg.job_id);
	info("Sbcast_cred: StepId  %u", sbcast_cred->arg.step_id);
	info("Sbcast_cred: Nodes   %s", sbcast_cred->arg.nodes);
	info("Sbcast_cred: ctime   %s", slurm_ctime2(&sbcast_cred->ctime));
	info("Sbcast_cred: Expire  %s", slurm_ctime2(&sbcast_cred->arg.expiration));
}

extern char *create_net_cred(void *addrs, uint16_t protocol_version)
{
	xassert(g_context);

	if (!addrs) {
		error("%s: addrs not provided", __func__);
		return NULL;
	}

	return (*(ops.create_net_cred))(addrs, protocol_version);
}

extern void *extract_net_cred(char *net_cred, uint16_t protocol_version)
{
	xassert(g_context);

	if (!net_cred) {
		error("%s: net_cred not provided", __func__);
		return NULL;
	}

	return (*(ops.extract_net_cred))(net_cred, protocol_version);
}
