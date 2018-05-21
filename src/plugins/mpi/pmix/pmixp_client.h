/*****************************************************************************\
 **  pmix_client.h - PMIx client communication code
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
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

#ifndef PMIXP_CLIENT_H
#define PMIXP_CLIENT_H

#include "pmixp_common.h"

#define PMIXP_ALLOC_KEY(kvp, key_str)				\
{								\
	char *key = key_str;					\
	kvp = (pmix_info_t *)xmalloc(sizeof(pmix_info_t));	\
	(void)strncpy(kvp->key, key, PMIX_MAX_KEYLEN);		\
}

#define PMIXP_INFO_ADD(kvp, key_str, field, val) {			\
	int key_num = 0;						\
	char *key = key_str;						\
	if (!kvp) {							\
		kvp = (pmix_info_t *)xmalloc(sizeof(pmix_info_t));	\
	} else {							\
		key_num = xsize(kvp) / sizeof(pmix_info_t);		\
		kvp = (pmix_info_t *)xrealloc(kvp, (key_num + 1) *	\
					      sizeof(pmix_info_t));	\
	}								\
	(void)strncpy(kvp[key_num].key, key, PMIX_MAX_KEYLEN);		\
	PMIX_VAL_SET(&kvp[key_num].value, field, val);			\
}

#define PMIXP_INFO_SIZE(kvp) (xsize(kvp) / sizeof(pmix_info_t))

#define PMIXP_FREE_KEY(kvp)	\
{				\
	xfree(kvp);		\
}

int pmixp_libpmix_init(void);
int pmixp_libpmix_finalize(void);
int pmixp_libpmix_job_set(void);
void pmix_libpmix_task_set(int rank, char ***env);
void pmix_client_new_conn(int fd);

int pmixp_lib_init(void);
int pmixp_lib_finalize(void);
int pmixp_lib_setup_fork(uint32_t rank, const char *nspace, char ***env);
int pmixp_lib_dmodex_request(pmixp_proc_t *proc, void *dmdx_fn, void *caddy);
void pmixp_lib_modex_invoke(void *mdx_fn, int status, const char *data,
			    size_t ndata, void *cbdata, void *rel_fn,
			    void *rel_data);
void pmixp_lib_release_invoke(void *rel_fn, void *rel_data);
int pmixp_lib_is_wildcard(uint32_t rank);
int pmixp_lib_is_undef(uint32_t rank);
uint32_t pmixp_lib_get_wildcard(void);
uint32_t pmixp_lib_get_version(void);

#endif /* PMIXP_CLIENT_H */
