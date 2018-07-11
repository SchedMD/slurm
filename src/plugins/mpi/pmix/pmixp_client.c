/*****************************************************************************\
 **  pmix_client.c - PMIx client communication code
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

#include "pmixp_common.h"
#include "pmixp_state.h"
#include "pmixp_io.h"
#include "pmixp_nspaces.h"
#include "pmixp_debug.h"
#include "pmixp_coll.h"
#include "pmixp_server.h"
#include "pmixp_dmdx.h"
#include "pmixp_client.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <pmix_server.h>

#ifdef HAVE_HWLOC
#include <hwloc.h>
#endif

// define some additional keys
#ifndef PMIX_TDIR_RMCLEAN
#define PMIX_TDIR_RMCLEAN "pmix.tdir.rmclean"
#endif

#ifndef PMIX_VERSION_MAJOR
#define PMIX_VERSION_MAJOR	1L
#define PMIXP_PMIX_PRIOR_115	1
#endif

#if (HAVE_PMIX_VER == 1L)
#if (PMIXP_PMIX_PRIOR_115 == 1)
#define PMIXP_INFO_ARRAY_SET_ARRAY(kvp, _array) \
	{ (kvp)->value.data.array.array = (struct pmix_info_t *)_array; }
#else
#define PMIXP_INFO_ARRAY_SET_ARRAY(kvp, _array) \
	{ (kvp)->value.data.array.array = (pmix_info_t *)_array; }
#endif
#endif


/* Check PMIx version */
#if (HAVE_PMIX_VER != PMIX_VERSION_MAJOR)
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#pragma message "PMIx version mismatch: the major version seen during configuration was " VALUE(HAVE_PMIX_VER) "L but found " VALUE(PMIX_VERSION_MAJOR) " compilation will most likely fail.  Please reconfigure against the new version."
#endif

#if (HAVE_PMIX_VER == 1)
#define PMIXP_INFO_ARRAY_CREATE(kvp, _array, _count)		\
{								\
	(kvp)->value.type = PMIX_INFO_ARRAY;			\
	(kvp)->value.data.array.size = _count;			\
	PMIXP_INFO_ARRAY_SET_ARRAY(kvp, _array);		\
}

#define PMIXP_VAL_SET_RANK(value, rank) {			\
	PMIX_VAL_SET(value, int, rank);				\
}

#else
#define PMIXP_INFO_ARRAY_CREATE(kvp, _array, _count)			\
{									\
	(kvp)->value.type = PMIX_DATA_ARRAY;				\
	(kvp)->value.data.darray =                                      \
		(pmix_data_array_t*)malloc(sizeof(pmix_data_array_t));	\
	(kvp)->value.data.darray->type = PMIX_INFO;			\
	(kvp)->value.data.darray->size = _count;			\
	(kvp)->value.data.darray->array = (void *)_array;		\
}

#define PMIXP_VAL_SET_RANK(value, _rank) {			\
	(value)->type = PMIX_PROC_RANK;				\
	(value)->data.rank = _rank;				\
}

#endif

static pthread_mutex_t _reg_mutex = PTHREAD_MUTEX_INITIALIZER;


typedef struct {
	pmix_status_t rc;
	volatile int active;
} register_caddy_t;

static void _release_cb(pmix_status_t status, void *cbdata)
{
	slurm_mutex_lock(&_reg_mutex);
	register_caddy_t *caddy = (register_caddy_t *)cbdata;
	caddy->rc = status;
	caddy->active = 0;
	slurm_mutex_unlock(&_reg_mutex);
}

/*
 * general proc-level attributes
 */
static void _general_proc_info(List lresp)
{
	pmix_info_t *kvp;
	/* TODO: how can we get this information in SLURM?
	 * PMIXP_ALLOC_KEY(kvp, PMIX_CPUSET);
	 * PMIX_VAL_SET(&kvp->value, string, "");
	 * list_append(lresp, kvp);
	 * TODO: what should we provide for credentials?
	 * #define PMIX_CREDENTIAL            "pmix.cred"
	 * TODO: Once spawn will be implemented we'll need to check here
	 */
	PMIXP_ALLOC_KEY(kvp, PMIX_SPAWNED);
	PMIX_VAL_SET(&kvp->value, flag, 0);
	list_append(lresp, kvp);

	/*
	 * TODO: what is the portable way to get arch string?
	 * #define PMIX_ARCH                  "pmix.arch"
	 */
}

/*
 * scratch directory locations for use by applications
 */
static void _set_tmpdirs(List lresp)
{
	pmix_info_t *kvp;
	char *p = NULL;
	bool rmclean = true;

	/* We consider two sources of the tempdir:
	 * - Slurm's slurm.conf TmpFS option;
	 * - env var SLURM_PMIX_TMPDIR;
	 * do we need to do anything else?
	 */
	p = pmixp_info_tmpdir_cli_base();
	PMIXP_ALLOC_KEY(kvp, PMIX_TMPDIR);
	PMIX_VAL_SET(&kvp->value, string, p);
	list_append(lresp, kvp);

	p = pmixp_info_tmpdir_cli();
	PMIXP_ALLOC_KEY(kvp, PMIX_NSDIR);
	PMIX_VAL_SET(&kvp->value, string, p);
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_TDIR_RMCLEAN);
	PMIX_VAL_SET(&kvp->value, flag, rmclean);
	list_append(lresp, kvp);
}

/*
 * information about relative ranks as assigned by the RM
 */
static void _set_procdatas(List lresp)
{
	pmixp_namespace_t *nsptr = pmixp_nspaces_local();
	pmix_info_t *kvp, *tkvp;
	char *p = NULL;
	int i;

	/* (char*) jobid assigned by scheduler */
	xstrfmtcat(p, "%d.%d", pmixp_info_jobid(), pmixp_info_stepid());
	PMIXP_ALLOC_KEY(kvp, PMIX_JOBID);
	PMIX_VAL_SET(&kvp->value, string, p);
	xfree(p);
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_NODEID);
	PMIX_VAL_SET(&kvp->value, uint32_t, nsptr->node_id);
	list_append(lresp, kvp);

	/* store information about local processes */
	for (i = 0; i < pmixp_info_tasks(); i++) {
		List rankinfo;
		ListIterator it;
		int count, j, localid, nodeid;
		char *nodename;
		pmix_info_t *info;

		rankinfo = list_create(pmixp_xfree_xmalloced);

		PMIXP_ALLOC_KEY(kvp, PMIX_RANK);
		PMIXP_VAL_SET_RANK(&kvp->value, i);
		list_append(rankinfo, kvp);

		/* TODO: always use 0 for now. This is not the general case
		 * though (see Slurm MIMD: man srun, section MULTIPLE PROGRAM
		 * CONFIGURATION)
		 */
		PMIXP_ALLOC_KEY(kvp, PMIX_APPNUM);
		PMIX_VAL_SET(&kvp->value, int, 0);
		list_append(rankinfo, kvp);

		/* TODO: the same as for previous here */
		PMIXP_ALLOC_KEY(kvp, PMIX_APPLDR);
		PMIX_VAL_SET(&kvp->value, int, 0);
		list_append(rankinfo, kvp);

		/* TODO: fix when several apps will appear */
		PMIXP_ALLOC_KEY(kvp, PMIX_GLOBAL_RANK);
		PMIX_VAL_SET(&kvp->value, uint32_t, i);
		list_append(rankinfo, kvp);

		/* TODO: fix when several apps will appear */
		PMIXP_ALLOC_KEY(kvp, PMIX_APP_RANK);
		PMIX_VAL_SET(&kvp->value, uint32_t, i);
		list_append(rankinfo, kvp);

		localid = pmixp_info_taskid2localid(i);
		/* this rank is local, store local info ab't it! */
		if (0 <= localid) {
			PMIXP_ALLOC_KEY(kvp, PMIX_LOCAL_RANK);
			PMIX_VAL_SET(&kvp->value, uint16_t, localid);
			list_append(rankinfo, kvp);

			/* TODO: fix when several apps will appear */
			PMIXP_ALLOC_KEY(kvp, PMIX_NODE_RANK);
			PMIX_VAL_SET(&kvp->value, uint16_t, localid);
			list_append(rankinfo, kvp);
		}

		nodeid = nsptr->task_map[i];
		nodename = hostlist_nth(nsptr->hl, nodeid);
		PMIXP_ALLOC_KEY(kvp, PMIX_HOSTNAME);
		PMIX_VAL_SET(&kvp->value, string, nodename);
		list_append(rankinfo, kvp);
		free(nodename);

		/* merge rankinfo into one PMIX_PROC_DATA key */
		count = list_count(rankinfo);
		PMIX_INFO_CREATE(info, count);
		it = list_iterator_create(rankinfo);
		j = 0;
		while (NULL != (tkvp = list_next(it))) {
			/* Just copy all the fields here. We will free
			 * original kvp's using list_destroy without free'ing
			 * their fields so it is safe to do so.
			 */
			info[j] = *tkvp;
			j++;
		}
		list_destroy(rankinfo);
		PMIXP_ALLOC_KEY(kvp, PMIX_PROC_DATA);
		PMIXP_INFO_ARRAY_CREATE(kvp, info, count);
		info = NULL;

		/* put the complex key to the list */
		list_append(lresp, kvp);
	}
}

static void _set_sizeinfo(List lresp)
{
	pmix_info_t *kvp;
	/* size information */
	PMIXP_ALLOC_KEY(kvp, PMIX_UNIV_SIZE);
	PMIX_VAL_SET(&kvp->value, uint32_t, pmixp_info_tasks_uni());
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_JOB_SIZE);
	PMIX_VAL_SET(&kvp->value, uint32_t, pmixp_info_tasks());
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_LOCAL_SIZE);
	PMIX_VAL_SET(&kvp->value, uint32_t, pmixp_info_tasks_loc());
	list_append(lresp, kvp);

	/* TODO: fix it in future */
	PMIXP_ALLOC_KEY(kvp, PMIX_NODE_SIZE);
	PMIX_VAL_SET(&kvp->value, uint32_t, pmixp_info_tasks_loc());
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_MAX_PROCS);
	PMIX_VAL_SET(&kvp->value, uint32_t, pmixp_info_tasks_uni());
	list_append(lresp, kvp);

}

/*
 * provide topology information if hwloc is available
 */
static void _set_topology(List lresp)
{
#ifdef HAVE_HWLOC
	hwloc_topology_t topology;
	unsigned long flags;
	pmix_info_t *kvp;
	char *p = NULL;
	int len;

	if (0 != hwloc_topology_init(&topology)) {
		/* error in initialize hwloc library */
		error("%s: hwloc_topology_init() failed", __func__);
		goto err_exit;
	}

#if HWLOC_API_VERSION < 0x00020000
	flags = (HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM |
		 HWLOC_TOPOLOGY_FLAG_IO_DEVICES);
	hwloc_topology_set_flags(topology, flags);
#else
	flags = HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM;
	hwloc_topology_set_flags(topology, flags);
	hwloc_topology_set_io_types_filter(topology,
					   HWLOC_TYPE_FILTER_KEEP_ALL);
#endif

	if (hwloc_topology_load(topology)) {
		error("%s: hwloc_topology_load() failed", __func__);
		goto err_release_topo;
	}

#if HWLOC_API_VERSION < 0x00020000
	if (0 != hwloc_topology_export_xmlbuffer(topology, &p, &len)) {
#else
	if (0 != hwloc_topology_export_xmlbuffer(topology, &p, &len, 0)) {
#endif
		error("%s: hwloc_topology_load() failed", __func__);
		goto err_release_topo;
	}

	PMIXP_ALLOC_KEY(kvp, PMIX_LOCAL_TOPO);
	PMIX_VAL_SET(&kvp->value, string, p);
	list_append(lresp, kvp);

	/* successful exit - fallthru */
err_release_topo:
	hwloc_topology_destroy(topology);
err_exit:
#endif
	return;
}


static int _set_mapsinfo(List lresp)
{
	pmix_info_t *kvp;
	char *regexp, *input;
	pmixp_namespace_t *nsptr = pmixp_nspaces_local();
	hostlist_t hl = nsptr->hl;
	int rc, i, j;
	int count = hostlist_count(hl);

	input = hostlist_deranged_string_malloc(hl);
	rc = PMIx_generate_regex(input, &regexp);
	free(input);
	if (PMIX_SUCCESS != rc) {
		return SLURM_ERROR;
	}
	PMIXP_ALLOC_KEY(kvp, PMIX_NODE_MAP);
	PMIX_VAL_SET(&kvp->value, string, regexp);
	regexp = NULL;
	list_append(lresp, kvp);

	input = NULL;
	for (i = 0; i < count; i++) {
		/* for each node - run through all tasks and
		 * record taskid's that reside on this node
		 */
		int first = 1;
		for (j = 0; j < nsptr->ntasks; j++) {
			if (nsptr->task_map[j] == i) {
				if (first) {
					first = 0;
				} else {
					xstrfmtcat(input, ",");
				}
				xstrfmtcat(input, "%u", j);
			}
		}
		if (i < (count - 1)) {
			xstrfmtcat(input, ";");
		}
	}
	rc = PMIx_generate_ppn(input, &regexp);
	xfree(input);
	if (PMIX_SUCCESS != rc) {
		return SLURM_ERROR;
	}

	PMIXP_ALLOC_KEY(kvp, PMIX_PROC_MAP);
	PMIX_VAL_SET(&kvp->value, string, regexp);
	regexp = NULL;
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_ANL_MAP);
	PMIX_VAL_SET(&kvp->value, string, pmixp_info_task_map());
	regexp = NULL;
	list_append(lresp, kvp);

	return SLURM_SUCCESS;
}

static void _set_localinfo(List lresp)
{
	pmix_info_t *kvp;
	uint32_t tmp;
	char *p = NULL;
	int i;

	xstrfmtcat(p, "%u", pmixp_info_taskid(0));
	tmp = pmixp_info_taskid(0);
	for (i = 1; i < pmixp_info_tasks_loc(); i++) {
		uint32_t rank = pmixp_info_taskid(i);
		xstrfmtcat(p, ",%u", rank);
		if (tmp > rank) {
			tmp = rank;
		}
	}

	PMIXP_ALLOC_KEY(kvp, PMIX_LOCAL_PEERS);
	PMIX_VAL_SET(&kvp->value, string, p);
	xfree(p);
	list_append(lresp, kvp);

	PMIXP_ALLOC_KEY(kvp, PMIX_LOCALLDR);
	PMIX_VAL_SET(&kvp->value, uint32_t, tmp);
	list_append(lresp, kvp);
}

extern int pmixp_libpmix_init(void)
{
	int rc;
	mode_t rights = (S_IRUSR | S_IWUSR | S_IXUSR) |
			(S_IRGRP | S_IWGRP | S_IXGRP);

	if (0 != (rc = pmixp_mkdir(pmixp_info_tmpdir_lib(), rights))) {
		PMIXP_ERROR_STD("Cannot create server lib tmpdir: \"%s\"",
				pmixp_info_tmpdir_lib());
		return errno;
	}

	if (0 != (rc = pmixp_mkdir(pmixp_info_tmpdir_cli(), rights))) {
		PMIXP_ERROR_STD("Cannot create client cli tmpdir: \"%s\"",
				pmixp_info_tmpdir_cli());
		return errno;
	}

	rc = pmixp_lib_init();
	if (SLURM_SUCCESS != rc) {
		PMIXP_ERROR_STD("PMIx_server_init failed with error %d\n", rc);
		return SLURM_ERROR;
	}

	/* TODO: must be deleted in future once info-key approach harden */
	setenv(PMIXP_PMIXLIB_TMPDIR, pmixp_info_tmpdir_lib(), 1);

	/*
	if( pmixp_fixrights(pmixp_info_tmpdir_lib(),
		(uid_t) pmixp_info_jobuid(), rights) ){
	}
	*/

	return 0;
}

extern int pmixp_libpmix_finalize(void)
{
	int rc = SLURM_SUCCESS, rc1;

	rc = pmixp_lib_finalize();

	rc1 = pmixp_rmdir_recursively(pmixp_info_tmpdir_lib());
	if (0 != rc1) {
		PMIXP_ERROR_STD("Failed to remove %s\n",
				pmixp_info_tmpdir_lib());
		/* Not considering this as fatal error */
	}

	rc1 = pmixp_rmdir_recursively(pmixp_info_tmpdir_cli());
	if (0 != rc1) {
		PMIXP_ERROR_STD("Failed to remove %s\n",
				pmixp_info_tmpdir_cli());
		/* Not considering this as fatal error */
	}

	return rc;
}

extern void pmixp_lib_modex_invoke(void *mdx_fn, int status,
			  const char *data, size_t ndata, void *cbdata,
			  void *rel_fn, void *rel_data)
{
	pmix_status_t rc = PMIX_SUCCESS;
	pmix_modex_cbfunc_t cbfunc = (pmix_modex_cbfunc_t)mdx_fn;
	pmix_release_cbfunc_t release_fn = (pmix_release_cbfunc_t) rel_fn;

	switch (status) {
		case SLURM_SUCCESS:
			rc = PMIX_SUCCESS;
			break;
		case PMIXP_ERR_INVALID_NAMESPACE:
			rc = PMIX_ERR_INVALID_NAMESPACE;
			break;
		case PMIXP_ERR_BAD_PARAM:
			rc = PMIX_ERR_BAD_PARAM;
			break;
		case PMIXP_ERR_TIMEOUT:
			rc = PMIX_ERR_TIMEOUT;
			break;
		default:
			rc = PMIX_ERROR;
	}
	cbfunc(rc, data, ndata, cbdata, release_fn, rel_data);
}

extern void pmixp_lib_release_invoke(void *rel_fn, void *rel_data)
{
	pmix_release_cbfunc_t cbfunc = (pmix_release_cbfunc_t)rel_fn;

	cbfunc(rel_data);
}

extern int pmixp_lib_dmodex_request(
	pmixp_proc_t *proc, void *dmdx_fn, void *caddy)
{
	pmix_status_t rc;
	pmix_proc_t proc_v1;
	pmix_dmodex_response_fn_t cbfunc = (pmix_dmodex_response_fn_t)dmdx_fn;

	proc_v1.rank = (int)proc->rank;
	strncpy(proc_v1.nspace, proc->nspace, PMIX_MAX_NSLEN);

	rc = PMIx_server_dmodex_request(&proc_v1, cbfunc, caddy);
	if (PMIX_SUCCESS != rc) {
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

extern int pmixp_lib_setup_fork(uint32_t rank, const char *nspace, char ***env)
{
	pmix_proc_t proc;
	pmix_status_t rc;

	proc.rank = rank;
	strncpy(proc.nspace, nspace, PMIX_MAX_NSLEN);
	rc = PMIx_server_setup_fork(&proc, env);
	if (PMIX_SUCCESS != rc) {
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

extern int pmixp_lib_is_wildcard(uint32_t rank)
{
	int _rank = (int)rank;
	return (PMIX_RANK_WILDCARD == _rank);
}

extern uint32_t pmixp_lib_get_wildcard(void)
{
	return (uint32_t)(PMIX_RANK_WILDCARD);
}

extern uint32_t pmixp_lib_get_version(void)
{
	return (uint32_t)PMIX_VERSION_MAJOR;
}

extern int pmixp_libpmix_job_set(void)
{
	List lresp;
	pmix_info_t *info;
	int ninfo;
	ListIterator it;
	pmix_info_t *kvp;
	int i, rc, ret = SLURM_SUCCESS;
	uid_t uid = pmixp_info_jobuid();
	gid_t gid = pmixp_info_jobgid();
	register_caddy_t *register_caddy;

	register_caddy = xmalloc(sizeof(register_caddy_t) *
				 (pmixp_info_tasks_loc() + 1));
	pmixp_debug_hang(0);

	/* Use list to safely expand/reduce key-value pairs. */
	lresp = list_create(pmixp_xfree_xmalloced);

	_general_proc_info(lresp);

	_set_tmpdirs(lresp);

	_set_procdatas(lresp);

	_set_sizeinfo(lresp);

	_set_topology(lresp);

	if (SLURM_SUCCESS != _set_mapsinfo(lresp)) {
		list_destroy(lresp);
		PMIXP_ERROR("Can't build nodemap");
		return SLURM_ERROR;
	}

	_set_localinfo(lresp);

	ninfo = list_count(lresp);
	PMIX_INFO_CREATE(info, ninfo);
	it = list_iterator_create(lresp);
	i = 0;
	while (NULL != (kvp = list_next(it))) {
		info[i] = *kvp;
		i++;
	}
	list_destroy(lresp);

	register_caddy[0].active = 1;
	rc = PMIx_server_register_nspace(pmixp_info_namespace(),
					 pmixp_info_tasks_loc(), info,
					 ninfo, _release_cb,
					 &register_caddy[0]);

	if (PMIX_SUCCESS != rc) {
		PMIXP_ERROR("Cannot register namespace %s, nlocalproc=%d, ninfo = %d",
			    pmixp_info_namespace(), pmixp_info_tasks_loc(),
			    ninfo);
		return SLURM_ERROR;
	}

	PMIXP_DEBUG("task initialization");
	for (i = 0; i < pmixp_info_tasks_loc(); i++) {
		pmix_proc_t proc;
		register_caddy[i+1].active = 1;
		strncpy(proc.nspace, pmixp_info_namespace(), PMIX_MAX_NSLEN);
		proc.rank = pmixp_info_taskid(i);
		rc = PMIx_server_register_client(&proc, uid, gid, NULL,
						 _release_cb,
						 &register_caddy[i + 1]);
		if (PMIX_SUCCESS != rc) {
			PMIXP_ERROR("Cannot register client %d(%d) in namespace %s",
				    pmixp_info_taskid(i), i,
				    pmixp_info_namespace());
			return SLURM_ERROR;
		}
	}

	/* wait for all registration actions to finish */
	while (1) {
		int exit_flag = 1;
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 100;

		// Do a preliminary scan
		for (i=0; i < pmixp_info_tasks_loc() + 1; i++) {
			if (register_caddy[i].active) {
				exit_flag = 0;
			}
		}

		if (exit_flag) {
			slurm_mutex_lock(&_reg_mutex);
			// Do a final scan with the structure locked
			for (i=0; i < pmixp_info_tasks_loc() + 1; i++) {
				if (register_caddy[i].active) {
					exit_flag = 0;
				}
				// An error may occur during registration
				if (PMIX_SUCCESS != register_caddy[i].rc) {
					PMIXP_ERROR("Failed to complete registration #%d, error: %d", i, register_caddy[i].rc);
					ret = SLURM_ERROR;
				}
			}
			slurm_mutex_unlock(&_reg_mutex);
			if (exit_flag) {
				break;
			}
		}
		nanosleep(&ts, NULL);
	}
	PMIX_INFO_FREE(info, ninfo);
	xfree(register_caddy);

	return ret;
}

extern int pmixp_lib_fence(const pmixp_proc_t procs[], size_t nprocs,
			   bool collect, char *data, size_t ndata,
			   void *cbfunc, void *cbdata)
{
	pmixp_coll_t *coll;
	pmix_status_t status;
	pmix_modex_cbfunc_t modex_cbfunc = (pmix_modex_cbfunc_t)cbfunc;
	int ret;

	/* Chooses the coll algorithm defined by user
	 * thru the env variable: SLURM_PMIXP_FENCE.
	 * By default: PMIXP_COLL_TYPE_FENCE_AUTO
	 * is used the both fence algorithms */
	pmixp_coll_type_t type = pmixp_info_srv_fence_coll_type();

	if (PMIXP_COLL_TYPE_FENCE_MAX == type) {
		type = PMIXP_COLL_TYPE_FENCE_TREE;
		if (collect) {
			type = PMIXP_COLL_TYPE_FENCE_RING;
		}
	}

	coll = pmixp_state_coll_get(type, procs, nprocs);
	if (!coll) {
		status = PMIX_ERROR;
		goto error;
	}
	ret = pmixp_coll_contrib_local(coll, type, data, ndata, cbfunc, cbdata);
	if (SLURM_SUCCESS != ret) {
		status = PMIX_ERROR;
		goto error;
	}
	return SLURM_SUCCESS;

error:
	modex_cbfunc(status, NULL, 0, cbdata, NULL, NULL);
	return SLURM_ERROR;
}
