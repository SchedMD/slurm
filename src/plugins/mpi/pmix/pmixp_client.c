/*****************************************************************************\
 **  pmix_client.c - PMIx client communication code
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015-2017 Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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

/* Check PMIx version */
#if (HAVE_PMIX_VER != PMIX_VERSION_MAJOR)
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#pragma message "PMIx version mismatch: the major version seen during configuration was " VALUE(HAVE_PMIX_VER) "L but found " VALUE(PMIX_VERSION_MAJOR) " compilation will most likely fail.  Please reconfigure against the new version."
#endif

int pmixp_libpmix_init(void)
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

int pmixp_libpmix_finalize(void)
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

void pmixp_lib_modex_invoke(void *mdx_fn, int status,
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

void pmixp_lib_release_invoke(void *rel_fn, void *rel_data)
{
	pmix_release_cbfunc_t cbfunc = (pmix_release_cbfunc_t)rel_fn;

	cbfunc(rel_data);
}

int pmixp_lib_dmodex_request(pmixp_proc_t *proc, void *dmdx_fn, void *caddy)
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

int pmixp_lib_setup_fork(uint32_t rank, const char *nspace, char ***env)
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

int pmixp_lib_is_wildcard(uint32_t rank)
{
	int _rank = (int)rank;
	return (PMIX_RANK_WILDCARD == _rank);
}

int pmixp_lib_is_undef(uint32_t rank)
{
	int _rank = (int)rank;
	return (PMIX_RANK_UNDEF == _rank);
}

uint32_t pmixp_lib_get_wildcard(void)
{
	return (uint32_t)(PMIX_RANK_WILDCARD);
}

uint32_t pmixp_lib_get_version(void)
{
	return (uint32_t)PMIX_VERSION_MAJOR;
}
