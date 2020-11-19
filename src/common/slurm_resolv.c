/*****************************************************************************\
 *  slurm_resolv.c - functions for DNS SRV resolution
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <netinet/in.h>
#include <resolv.h>

#include "src/common/list.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resolv.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#define SRV_RECORD "_slurmctld._tcp"

static int _sort_controllers(void *x, void *y)
{
	ctl_entry_t *ctl1 = *(ctl_entry_t **) x;
	ctl_entry_t *ctl2 = *(ctl_entry_t **) y;

	return ctl1->priority > ctl2->priority;
}

extern List resolve_ctls_from_dns_srv(void)
{
	struct __res_state res;
	ns_msg handle;
	ns_rr rr;
	unsigned char answer[512];
	int len;
	List controllers;

	if (res_ninit(&res)) {
		error("%s: res_ninit error: %m", __func__);
		return NULL;
	}

	if ((len = res_nsearch(&res, SRV_RECORD, C_IN, T_SRV,
			       answer, sizeof(answer))) < 0) {
		error("%s: res_nsearch error: %s",
		      __func__, hstrerror(h_errno));
		return NULL;
	}

	if (ns_initparse(answer, len, &handle) < 0) {
		error("%s: ns_initparse error: %m", __func__);
		return NULL;
	}

	controllers = list_create(xfree_ptr);

	for (int i = 0; i < ns_msg_count(handle, ns_s_an); i++) {
		ctl_entry_t *ctl;

		if (ns_parserr(&handle, ns_s_an, i, &rr) < 0) {
			error("%s: ns_parserr", __func__);
			continue;
		}

		if (ns_rr_type(rr) != T_SRV)
			continue;

		ctl = xmalloc(sizeof(*ctl));
		ctl->priority = ns_get16(ns_rr_rdata(rr));
		/* skip the weight field which is currently unused */
		ctl->port = ns_get16(ns_rr_rdata(rr) + 2 * NS_INT16SZ);

		if (dn_expand(ns_msg_base(handle), ns_msg_end(handle),
			      ns_rr_rdata(rr) + 3 * NS_INT16SZ,
			      ctl->hostname, sizeof(ctl->hostname)) < 0) {
			xfree(ctl);
			continue;
		}
		list_push(controllers, ctl);
	}

	if (!list_count(controllers)) {
		error("%s: no SRV records located", __func__);
		FREE_NULL_LIST(controllers);
	} else
		list_sort(controllers, _sort_controllers);

	return controllers;
}
