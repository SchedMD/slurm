/*****************************************************************************\
 *  net_aliases.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <arpa/inet.h>
#include <jwt.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_util.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

typedef struct {
	slurm_node_alias_addrs_t *aliases;
	hostlist_t *hl;
} _foreach_alias_addr_t;

// fixme - do something with expiration?
extern char *encode_net_aliases(slurm_node_alias_addrs_t *aliases)
{
	data_t *data = NULL, *data_net = NULL, *data_addrs = NULL;
	data_t *data_netcred = NULL,  *data_netcred_addrs = NULL;
	hostlist_t *hostlist;
	char *json = NULL;
	char *node_name = NULL;
	uint16_t port;

	data = data_set_dict(data_new());

	/* V1 */
	data_net = data_set_dict(data_key_set(data, "net"));
	data_set_string(data_key_set(data_net, "nodes"), aliases->node_list);
	data_addrs = data_set_dict(data_key_set(data_net, "addrs"));

	/* V2 */
	data_netcred = data_set_dict(data_key_set(data, "netcred"));
	data_netcred_addrs = data_set_list(data_key_set(data_netcred, "addrs"));

	hostlist = hostlist_create(aliases->node_list);
	xassert(hostlist_count(hostlist) == aliases->node_cnt);

	for (int i = 0; i < aliases->node_cnt; i++) {
		data_t *addr_dict = NULL;
		char addrbuf[INET6_ADDRSTRLEN];
		node_name = hostlist_shift(hostlist);
		if (!node_name)
			break;

		if (aliases->node_addrs[i].ss_family == AF_INET6) {
			struct sockaddr_in6 *in6 =
				(struct sockaddr_in6 *) &aliases->node_addrs[i];
			inet_ntop(AF_INET6, &in6->sin6_addr, addrbuf,
				  INET6_ADDRSTRLEN);
			port = in6->sin6_port;
		} else {
			struct sockaddr_in *in =
				(struct sockaddr_in *) &aliases->node_addrs[i];
			inet_ntop(AF_INET, &in->sin_addr, addrbuf,
				  INET_ADDRSTRLEN);
			port = in->sin_port;
		}
		/* V1 */
		data_set_int(data_key_set(data_addrs, addrbuf), port);

		/* V2 */
		addr_dict = data_set_dict(data_list_append(data_netcred_addrs));
		data_set_string(data_key_set(addr_dict, "name"), node_name);
		data_set_string(data_key_set(addr_dict, "ip"), addrbuf);
		/* Use slurm_get_port to get non-byte ordered port # */
		data_set_int(data_key_set(addr_dict, "port"),
			     slurm_get_port(&aliases->node_addrs[i]));

		free(node_name);
	}

	serialize_g_data_to_string(&json, NULL, data, MIME_TYPE_JSON,
				   SER_FLAGS_COMPACT);

	FREE_NULL_DATA(data);
	FREE_NULL_HOSTLIST(hostlist);
	return json;
}

data_for_each_cmd_t _for_each_list_addr(data_t *data, void *arg)
{
	_foreach_alias_addr_t *foreach_addr = arg;
	slurm_node_alias_addrs_t *aliases = foreach_addr->aliases;
	slurm_addr_t *addr = &aliases->node_addrs[aliases->node_cnt];
	data_for_each_cmd_t rc = DATA_FOR_EACH_CONT;
	const data_t *data_ptr = NULL;
	char *address = NULL;
	char *node_name = NULL;
	int64_t port;

	if (data_get_type(data) != DATA_TYPE_DICT) {
		error("%s: data parsing failed, data type not dict", __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (!(data_ptr = data_key_get_const(data, "name"))) {
		error("%s: data parsing failed, no name for host entry",
		      __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (data_get_string_converted(data_ptr, &node_name)) {
		error("%s: data parsing failed, failed to parse host entry node name",
		      __func__);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (!(data_ptr = data_key_get_const(data, "ip"))) {
		error("%s: data parsing failed, no ip for host entry (%s)",
		      __func__, node_name);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (data_get_string_converted(data_ptr, &address)) {
		error("%s: data parsing failed, failed to parse address (%s)",
		      __func__, node_name);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (!(data_ptr = data_key_get_const(data, "port"))) {
		error("%s: data parsing failed, no port for host entry (%s, %s)",
		      __func__, node_name, address);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (data_get_int_converted(data_ptr, &port)) {
		error("%s: data parsing failed, failed to parse port (%s, %s)",
		      __func__, node_name, address);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (port > UINT16_MAX) {
		error("%s: data parsing failed, int greater than 16 bits (%s, %s:%lu)",
		      __func__, node_name, address, port);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	if (strchr(address, ':')) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) addr;
		addr->ss_family = AF_INET6;
		if (inet_pton(AF_INET6, address, &in6->sin6_addr.s6_addr) != 1) {
			rc = DATA_FOR_EACH_FAIL;
			goto cleanup;
		}
	} else {
		struct sockaddr_in *in = (struct sockaddr_in *) addr;
		addr->ss_family = AF_INET;
		if (inet_pton(AF_INET, address, &in->sin_addr.s_addr) != 1) {
			rc = DATA_FOR_EACH_FAIL;
			goto cleanup;
		}
	}
	/* Use slurm_set_port to set port from non-byte ordered port # */
	slurm_set_port(addr, port);

	hostlist_push(foreach_addr->hl, node_name);
	aliases->node_cnt++;
cleanup:
	xfree(node_name);
	xfree(address);
	return rc;
}

data_for_each_cmd_t _for_each_dict_addr(const char *key, const data_t *data,
					void *arg)
{
	slurm_node_alias_addrs_t *aliases = arg;
	slurm_addr_t *addr = &aliases->node_addrs[aliases->node_cnt];

	if (strchr(key, ':')) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) addr;
		addr->ss_family = AF_INET6;
		if (inet_pton(AF_INET6, key, &in6->sin6_addr.s6_addr) != 1)
			return DATA_FOR_EACH_FAIL;
		in6->sin6_port = data_get_int(data);
	} else {
		struct sockaddr_in *in = (struct sockaddr_in *) addr;
		addr->ss_family = AF_INET;
		if (inet_pton(AF_INET, key, &in->sin_addr.s_addr) != 1)
			return DATA_FOR_EACH_FAIL;
		in->sin_port = data_get_int(data);
	}

	aliases->node_cnt++;

	return DATA_FOR_EACH_CONT;
}

static slurm_node_alias_addrs_t *_extract_net_aliases_v2(char *json)
{
	data_t *data = NULL, *data_addrs = NULL;
	slurm_node_alias_addrs_t *aliases = NULL;
	int node_count;
	_foreach_alias_addr_t foreach_addr = {0};

	if (serialize_g_string_to_data(&data, json, strlen(json),
				       MIME_TYPE_JSON)) {
		error("%s: failed to decode net field", __func__);
		return NULL;
	}

	if ((data_addrs = data_key_get(data, "addrs"))) {
		hostlist_t *hl = hostlist_create(NULL);
		aliases = xmalloc(sizeof(*aliases));

		foreach_addr.aliases = aliases;
		foreach_addr.hl = hl;

		node_count = data_get_list_length(data_addrs);
		aliases->node_addrs = xcalloc(node_count, sizeof(slurm_addr_t));
		if (data_list_for_each(data_addrs,
		                       _for_each_list_addr,
			               &foreach_addr) < 0) {
			error("%s: data_list_for_each_const failed", __func__);
			FREE_NULL_DATA(data);
			FREE_NULL_HOSTLIST(hl);
			slurm_free_node_alias_addrs(aliases);
			return NULL;
		}

		aliases->node_list = hostlist_ranged_string_xmalloc(hl);
		xassert(aliases->node_cnt == hostlist_count(hl));
		FREE_NULL_HOSTLIST(hl);
	} else {
		error("%s: hosts or addrs key not found in net aliases",
		      __func__);
		FREE_NULL_DATA(data);
		slurm_free_node_alias_addrs(aliases);
		return NULL;
	}

	FREE_NULL_DATA(data);
	return aliases;
}

static slurm_node_alias_addrs_t *_extract_net_aliases_v1(char *json)
{
	data_t *data = NULL, *data_addrs = NULL;
	slurm_node_alias_addrs_t *aliases;
	int node_count;

	if (serialize_g_string_to_data(&data, json, strlen(json),
				       MIME_TYPE_JSON)) {
		error("%s: failed to decode net field", __func__);
		return NULL;
	}

	aliases = xmalloc(sizeof(*aliases));
	aliases->node_list =
		xstrdup(data_get_string(data_key_get(data, "nodes")));

	data_addrs = data_key_get(data, "addrs");
	node_count = data_get_dict_length(data_addrs);
	aliases->node_addrs = xcalloc(node_count, sizeof(slurm_addr_t));

	if (data_dict_for_each_const(data_addrs,
				     _for_each_dict_addr, aliases) < 0) {
		error("%s: data_dict_for_each_const failed", __func__);
		FREE_NULL_DATA(data);
		slurm_free_node_alias_addrs(aliases);
		return NULL;
	}

	FREE_NULL_DATA(data);
	return aliases;
}

extern slurm_node_alias_addrs_t *extract_net_aliases(jwt_t *jwt)
{
	slurm_node_alias_addrs_t *addrs = NULL;
	char *json_net = NULL;

	if ((json_net = jwt_get_grants_json(jwt, "netcred"))) {
		if (!(addrs = _extract_net_aliases_v2(json_net))) {
			error("%s: extract_net_aliases_v2() failed", __func__);
			goto unpack_error;
		}
	} else if ((json_net = jwt_get_grants_json(jwt, "net"))) {
		if (!(addrs = _extract_net_aliases_v1(json_net))) {
			error("%s: extract_net_aliases_v1() failed", __func__);
			goto unpack_error;
		}
	} else {
		error("%s: jwt_get_grants_json() failure for net cred", __func__);
		goto unpack_error;
	}

	free(json_net);
	return addrs;

unpack_error:
	if (json_net)
		free(json_net);
	return NULL;
}
