/*****************************************************************************\
 **  info.c - job/node info related functions
 *****************************************************************************
 *  Copyright (C) 2011-2012 National University of Defense Technology.
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>.
 *  All rights reserved.
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

#include "pmi.h"
#include "setup.h"
#include "client.h"
#if !defined(__FreeBSD__)
#include <net/if.h>
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include "slurm/slurm.h"
#include "src/srun/launch.h"
#include "src/common/strlcpy.h"
#include "src/interfaces/switch.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#define NODE_ATTR_SIZE_INC 8

/* pending node attribute get request */
typedef struct nag_req {
	int fd;
	int rank;
	char key[PMI2_MAX_KEYLEN];
	struct nag_req *next;
} nag_req_t;
static nag_req_t *nag_req_list = NULL;

/* node attributes */
static int na_cnt = 0;
static int na_size = 0;
static char **node_attr = NULL;

#define KEY_INDEX(i) (i * 2)
#define VAL_INDEX(i) (i * 2 + 1)

static char *ifconfig(void);

extern int
enqueue_nag_req(int fd, int rank, char *key)
{
	nag_req_t *req;

	req = xmalloc(sizeof(nag_req_t));
	req->fd = fd;
	req->rank = rank;
	strlcpy(req->key, key, PMI2_MAX_KEYLEN);

	/* insert in the head */
	req->next = nag_req_list;
	nag_req_list = req;
	return SLURM_SUCCESS;
}

extern int
node_attr_put(char *key, char *val)
{
	nag_req_t *req = NULL, **pprev = NULL;
	client_resp_t *resp = NULL;
	int rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: node_attr_put: %s=%s", key, val);

	if (na_cnt * 2 >= na_size) {
		na_size += NODE_ATTR_SIZE_INC;
		xrealloc(node_attr, na_size * sizeof(char*));
	}
	node_attr[KEY_INDEX(na_cnt)] = xstrdup(key);
	node_attr[VAL_INDEX(na_cnt)] = xstrdup(val);
	na_cnt ++;

	/* process pending requests */
	pprev = &nag_req_list;
	req = *pprev;
	while (req != NULL) {
		if (xstrncmp(key, req->key, PMI2_MAX_KEYLEN)) {
			pprev = &req->next;
			req = *pprev;
		} else {
			debug("mpi/pmi2: found pending request from rank %d",
			      req->rank);

			/* send response msg */
			if (! resp) {
				resp = client_resp_new();
				client_resp_append(resp,
						   CMD_KEY"="
						   GETNODEATTRRESP_CMD";"
						   RC_KEY"=0;"
						   FOUND_KEY"="TRUE_VAL";"
						   VALUE_KEY"=%s;", val);
			}
			rc = client_resp_send(resp, req->fd);
			if (rc != SLURM_SUCCESS) {
				error("mpi/pmi2: failed to send '"
				      GETNODEATTRRESP_CMD "' to task %d",
				      req->rank);
			}
			/* remove the request */
			*pprev = req->next;
			xfree(req);
			req = *pprev;
		}
	}
	if (resp) {
		client_resp_free (resp);
	}
	debug3("mpi/pmi2: out node_attr_put");
	return SLURM_SUCCESS;
}

/* returned value not dup-ed */
extern char *
node_attr_get(char *key)
{
	int i;
	char *val = NULL;

	debug3("mpi/pmi2: node_attr_get: key=%s", key);

	for (i = 0; i < na_cnt; i ++) {
		if (! xstrcmp(key, node_attr[KEY_INDEX(i)])) {
			val = node_attr[VAL_INDEX(i)];
			break;
		}
	}

	debug3("mpi/pmi2: out node_attr_get: val=%s", val);
	return val;
}

/* job_attr_get_netinfo()
 */
static char *
job_attr_get_netinfo(char *key, char *attr)
{
	char *netinfo;

	/* get network information of node in netinfo, xmalloc'ed
	 */
	netinfo = ifconfig();
	snprintf(attr, PMI2_MAX_VALLEN, "%s", netinfo);
	xfree(netinfo);

	debug3("%s: netinfo %s", __func__, attr);

	return attr;
}

/* job_attr_get()
 */
extern char *
job_attr_get(char *key)
{
	static char attr[PMI2_MAX_VALLEN];

	if (!xstrcmp(key, JOB_ATTR_PROC_MAP)) {
		return job_info.proc_mapping;
	}

	if (!xstrcmp(key, JOB_ATTR_UNIV_SIZE)) {
		snprintf(attr, PMI2_MAX_VALLEN, "%d", job_info.ntasks);
		return attr;
	}

	if (!xstrcmp(key, JOB_ATTR_RESV_PORTS)) {

		if (! job_info.resv_ports)
			return NULL;

		debug3("%s: SLURM_STEP_RESV_PORTS %s", __func__, job_info.resv_ports);
		snprintf(attr, PMI2_MAX_VALLEN, "%s", job_info.resv_ports);
		return attr;
	}

	if (xstrcmp(key, JOB_ATTR_NETINFO) >= 0) {
		if (job_attr_get_netinfo(key, attr) == NULL) {
			return NULL;
		}
		return attr;
	}

	return NULL;
}

/* ifconfig()
 *
 * Return information about network interfaces.
 */
static char *
ifconfig(void)
{
	struct ifaddrs *ifaddr;
	struct ifaddrs *ifa;
	int s;
	int n;
	char addr[NI_MAXHOST];
	char hostname[HOST_NAME_MAX];
	char *buf;

	if (getifaddrs(&ifaddr) == -1) {
		error("%s: getifaddrs failed %m", __func__);
		return NULL;
	}

	n = 0;
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
		++n;
	/* this should be a good guess of the size we need.
	 */
	buf = xmalloc((HOST_NAME_MAX + n) * 64);

	gethostname(hostname, sizeof(hostname));
	n = sprintf(buf, "(%s", hostname);

	/* Walk through linked list, maintaining head pointer so we
	 * can free list later
	 */
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {

		if (ifa->ifa_addr == NULL)
			continue;
#if !defined(__FreeBSD__)
		if (ifa->ifa_flags & IFF_LOOPBACK)
			continue;
#endif
		if (ifa->ifa_addr->sa_family != AF_INET
		    && ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		if (ifa->ifa_addr->sa_family == AF_INET) {
			s = getnameinfo(ifa->ifa_addr,
					sizeof(struct sockaddr_in),
					addr, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				error("%s: AF_INET getnameinfo() failed: %s",
				      __func__, gai_strerror(s));
				continue;
			}
			n = n + sprintf(buf + n, ",(%s,%s,%s)",
					ifa->ifa_name, "IP_V4", addr);
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET6) {
			s = getnameinfo(ifa->ifa_addr,
					sizeof(struct sockaddr_in6),
					addr, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (s != 0) {
				error("%s: AF_INET6 getnameinfo() failed: %s",
				      __func__, gai_strerror(s));
				continue;
			}
			n = n + sprintf(buf + n, ",(%s,%s,%s)",
					ifa->ifa_name, "IP_V6", addr);
		}
	}
	sprintf(buf + n, ")");

	debug("%s: ifconfig %s", __func__, buf);

	freeifaddrs(ifaddr);

	return buf;
}
