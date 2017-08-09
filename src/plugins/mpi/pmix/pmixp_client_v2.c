/*****************************************************************************\
 **  pmix_client_v2.c - PMIx v2 client communication code
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>,
 *             Boris Karasev <karasev.b@gmail.com, boriska@mellanox.com>.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#define PMIXP_INFO_ARRAY_CREATE(kvp, _array, _count)			\
{									\
	(kvp)->value.type = PMIX_DATA_ARRAY;				\
	(kvp)->value.data.darray =                                      \
		(pmix_data_array_t*)malloc(sizeof(pmix_data_array_t));	\
	(kvp)->value.data.darray->type = PMIX_INFO;			\
	(kvp)->value.data.darray->size = _count;			\
	(kvp)->value.data.darray->array = (void *)_array;		\
}

static int client_connected(const pmix_proc_t *proc, void *server_object,
			    pmix_op_cbfunc_t cbfunc, void *cbdata)
{
	/* we don't do anything by now */
	return PMIX_SUCCESS;
}

static void op_callbk(pmix_status_t status, void *cbdata)
{
	PMIXP_DEBUG("op callback is called with status=%d", status);
}

static void errhandler_reg_callbk(pmix_status_t status,
				  size_t errhandler_ref, void *cbdata)
{
	PMIXP_DEBUG("Error handler registration callback is called with status=%d, ref=%d",
		    status, (int)errhandler_ref);
}

static pmix_status_t client_finalized(const pmix_proc_t *proc,
				      void *server_object,
				      pmix_op_cbfunc_t cbfunc,
				      void *cbdata)
{
	/* don'n do anything by now */
	if (NULL != cbfunc) {
		cbfunc(PMIX_SUCCESS, cbdata);
	}
	return PMIX_SUCCESS;
}

static pmix_status_t abort_fn(const pmix_proc_t *proc,
			      void *server_object, int status,
			      const char msg[], pmix_proc_t procs[],
			      size_t nprocs, pmix_op_cbfunc_t cbfunc,
			      void *cbdata);

static pmix_status_t fencenb_fn(const pmix_proc_t procs[], size_t nprocs,
				const pmix_info_t info[], size_t ninfo,
				char *data, size_t ndata,
				pmix_modex_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t dmodex_fn(const pmix_proc_t *proc,
			       const pmix_info_t info[], size_t ninfo,
			       pmix_modex_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t publish_fn(const pmix_proc_t *proc,
				const pmix_info_t info[], size_t ninfo,
				pmix_op_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t lookup_fn(const pmix_proc_t *proc, char **keys,
			      const pmix_info_t info[], size_t ninfo,
			      pmix_lookup_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t unpublish_fn(const pmix_proc_t *proc, char **keys,
				  const pmix_info_t info[], size_t ninfo,
				  pmix_op_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t spawn_fn(const pmix_proc_t *proc,
			      const pmix_info_t job_info[], size_t ninfo,
			      const pmix_app_t apps[], size_t napps,
			      pmix_spawn_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t connect_fn(const pmix_proc_t procs[], size_t nprocs,
				const pmix_info_t info[], size_t ninfo,
				pmix_op_cbfunc_t cbfunc, void *cbdata);

static pmix_status_t disconnect_fn(const pmix_proc_t procs[], size_t nprocs,
				   const pmix_info_t info[], size_t ninfo,
				   pmix_op_cbfunc_t cbfunc, void *cbdata);

static	pmix_server_module_t _slurm_pmix_cb = {
	client_connected,
	client_finalized,
	abort_fn,
	fencenb_fn,
	dmodex_fn,
	publish_fn,
	lookup_fn,
	unpublish_fn,
	spawn_fn,
	connect_fn,
	disconnect_fn,
	NULL,
	NULL
};

static void errhandler(size_t evhdlr_registration_id,
		       pmix_status_t status,
		       const pmix_proc_t *source,
		       pmix_info_t info[], size_t ninfo,
		       pmix_info_t *results, size_t nresults,
		       pmix_event_notification_cbfunc_fn_t cbfunc,
		       void *cbdata)
{
	/* TODO: do something more sophisticated here */
	/* FIXME: use proper specificator for nranges */
	PMIXP_ERROR_STD("Error handler invoked: status = %d",
			status);
	slurm_kill_job_step(pmixp_info_jobid(), pmixp_info_stepid(), SIGKILL);
}

/*
 * general proc-level attributes
 */
static void _general_proc_info(List lresp)
{
	pmix_info_t *kvp;
	/*      TODO: how can we get this information in SLURM?
	 *      PMIXP_ALLOC_KEY(kvp, PMIX_CPUSET);
	 *      PMIX_VAL_SET(&kvp->value, string, "");
	 *      list_append(lresp, kvp);
	 *      TODO: what should we provide for credentials?
	 *      #define PMIX_CREDENTIAL            "pmix.cred"
	 *      TODO: Once spawn will be implemented we'll need to check here
	 */
	 PMIXP_ALLOC_KEY(kvp, PMIX_SPAWNED);
	 PMIX_VAL_SET(&kvp->value, flag, 0);
	 list_append(lresp, kvp);

	 /*
	 *       TODO: what is the portable way to get arch string?
	 *       #define PMIX_ARCH                  "pmix.arch"
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
	 * - SLURM's slurm.conf TmpFS option;
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
		PMIX_VAL_SET(&kvp->value, int, i);
		list_append(rankinfo, kvp);

		/* TODO: always use 0 for now. This is not the general case
		 * though (see SLURM MIMD: man srun, section MULTIPLE PROGRAM
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

	if( 0 != hwloc_topology_init(&topology) ) {
		/* error in initialize hwloc library */
		error("%s: hwloc_topology_init() failed", __func__);
		goto err_exit;
	}

	flags = (HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM |
		 HWLOC_TOPOLOGY_FLAG_IO_DEVICES);
	hwloc_topology_set_flags(topology, flags);

	if (hwloc_topology_load(topology)) {
		error("%s: hwloc_topology_load() failed", __func__);
		goto err_release_topo;
	}

	if (0 != hwloc_topology_export_xmlbuffer(topology, &p, &len)) {
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


typedef struct {
	volatile int active;
} _register_caddy_t;

static void _release_cb(pmix_status_t status, void *cbdata)
{
	(void)status;
	_register_caddy_t *caddy = (_register_caddy_t *)cbdata;
	caddy->active = 0;
}

int pmixp_libpmix_job_set(void)
{
	List lresp;
	pmix_info_t *info;
	int ninfo;
	ListIterator it;
	pmix_info_t *kvp;

	int i, rc;
	uid_t uid = pmixp_info_jobuid();
	gid_t gid = pmixp_info_jobgid();
	_register_caddy_t *register_caddy;

	register_caddy = xmalloc(sizeof(_register_caddy_t)*(pmixp_info_tasks_loc()+1));
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
					 ninfo, _release_cb, &register_caddy[0]);

	if (PMIX_SUCCESS != rc) {
		PMIXP_ERROR("Cannot register namespace %s, nlocalproc=%d, "
			    "ninfo = %d", pmixp_info_namespace(),
			    pmixp_info_tasks_loc(), ninfo);
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

		for (i = 0; i <  pmixp_info_tasks_loc() + 1; i++){
			if( register_caddy[i].active ){
				exit_flag = 0;
			}
		}
		if (exit_flag){
			break;
		}
		nanosleep(&ts, NULL);
	}
	PMIX_INFO_FREE(info, ninfo);
	xfree(register_caddy);

	return SLURM_SUCCESS;
}

static pmix_status_t abort_fn(const pmix_proc_t *proc, void *server_object,
			      int status, const char msg[], pmix_proc_t procs[],
			      size_t nprocs, pmix_op_cbfunc_t cbfunc, void *cbdata)
{
	/* Just kill this stepid for now. Think what we can do for FT here? */
	PMIXP_DEBUG("called: status = %d, msg = %s", status, msg);
	slurm_kill_job_step(pmixp_info_jobid(), pmixp_info_stepid(), SIGKILL);

	if (NULL != cbfunc) {
		cbfunc(PMIX_SUCCESS, cbdata);
	}
	return PMIX_SUCCESS;
}

pmix_status_t fencenb_fn(const pmix_proc_t procs_v2[], size_t nprocs,
			 const pmix_info_t info[], size_t ninfo,
			 char *data, size_t ndata,
			 pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	pmixp_coll_t *coll;
	pmixp_coll_type_t type = PMIXP_COLL_TYPE_FENCE;
	pmix_status_t status = PMIX_SUCCESS;
	int ret;
	size_t i;
	pmixp_proc_t *procs = xmalloc(sizeof(*procs) * nprocs);

	for (i = 0; i < nprocs; i++) {
		procs[i].rank = procs_v2[i].rank;
		strncpy(procs[i].nspace, procs_v2[i].nspace, PMIXP_MAX_NSLEN);
	}
	coll = pmixp_state_coll_get(type, procs, nprocs);
	ret = pmixp_coll_contrib_local(coll, data, ndata, cbfunc, cbdata);
	xfree(procs);

	if (SLURM_SUCCESS != ret) {
		status = PMIX_ERROR;
		goto error;
	}
	return PMIX_SUCCESS;
error:
	cbfunc(status, NULL, 0, cbdata, NULL, NULL);

	return status;
}

static pmix_status_t dmodex_fn(const pmix_proc_t *proc,
			       const pmix_info_t info[], size_t ninfo,
			       pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
	int rc;
	PMIXP_DEBUG("called");

	rc = pmixp_dmdx_get(proc->nspace, proc->rank, cbfunc, cbdata);

	return (SLURM_SUCCESS == rc) ? PMIX_SUCCESS : PMIX_ERROR;
}

static pmix_status_t publish_fn(const pmix_proc_t *proc,
				const pmix_info_t info[], size_t ninfo,
				pmix_op_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	return PMIX_ERR_NOT_SUPPORTED;
}

static pmix_status_t lookup_fn(const pmix_proc_t *proc, char **keys,
			       const pmix_info_t info[], size_t ninfo,
			       pmix_lookup_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	return PMIX_ERR_NOT_SUPPORTED;
}

static pmix_status_t unpublish_fn(const pmix_proc_t *proc, char **keys,
				  const pmix_info_t info[], size_t ninfo,
				  pmix_op_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	return PMIX_ERR_NOT_SUPPORTED;
}

static pmix_status_t spawn_fn(const pmix_proc_t *proc,
			      const pmix_info_t job_info[], size_t ninfo,
			      const pmix_app_t apps[], size_t napps,
			      pmix_spawn_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	return PMIX_ERR_NOT_SUPPORTED;
}

static pmix_status_t connect_fn(const pmix_proc_t procs[], size_t nprocs,
				const pmix_info_t info[], size_t ninfo,
				pmix_op_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	return PMIX_ERR_NOT_SUPPORTED;
}

static pmix_status_t disconnect_fn(const pmix_proc_t procs[], size_t nprocs,
				   const pmix_info_t info[], size_t ninfo,
				   pmix_op_cbfunc_t cbfunc, void *cbdata)
{
	PMIXP_DEBUG("called");
	return PMIX_ERR_NOT_SUPPORTED;
}


int pmixp_lib_init(void)
{
	pmix_info_t *kvp = NULL;
	pmix_status_t rc;

	PMIXP_INFO_ADD(kvp, PMIX_USERID, uint32_t, pmixp_info_jobuid());

#ifdef PMIX_SERVER_TMPDIR
	PMIXP_INFO_ADD(kvp, PMIX_SERVER_TMPDIR, string,
		       pmixp_info_tmpdir_lib());
#endif

	/* setup the server library */
	if (PMIX_SUCCESS != (rc = PMIx_server_init(&_slurm_pmix_cb, kvp,
						   PMIXP_INFO_SIZE(kvp)))) {
		PMIXP_ERROR_STD("PMIx_server_init failed with error %d\n", rc);
		return SLURM_ERROR;
	}

	PMIXP_FREE_KEY(kvp);
	/* register the errhandler */
	PMIx_Register_event_handler(NULL, 0, NULL, 0, errhandler,
				    errhandler_reg_callbk, NULL);

	return SLURM_SUCCESS;
}

int pmixp_lib_finalize(void)
{
	int rc = SLURM_SUCCESS;
	/* deregister the errhandler */
	PMIx_Deregister_event_handler(0, op_callbk, NULL);

	if (PMIX_SUCCESS != PMIx_server_finalize()) {
		rc = SLURM_ERROR;
	}
	return rc;
}
