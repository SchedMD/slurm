/*****************************************************************************\
 *  net_aliases.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

// fixme - do something with expiration?
extern char *encode_net_aliases(slurm_node_alias_addrs_t *aliases)
{
	data_t *data = NULL, *data_net = NULL, *data_addrs;
	char *json = NULL;

	data = data_set_dict(data_new());

	data_net = data_set_dict(data_key_set(data, "net"));

	data_set_string(data_key_set(data_net, "nodes"), aliases->node_list);

	data_addrs = data_set_dict(data_key_set(data_net, "addrs"));

	for (int i = 0; i < aliases->node_cnt; i++) {
		char addrbuf[INET6_ADDRSTRLEN];

		if (aliases->node_addrs[i].ss_family == AF_INET6) {
			struct sockaddr_in6 *in6 =
				(struct sockaddr_in6 *) &aliases->node_addrs[i];
			inet_ntop(AF_INET6, &in6->sin6_addr, addrbuf,
				  INET6_ADDRSTRLEN);
			data_set_int(data_key_set(data_addrs, addrbuf),
				     in6->sin6_port);
		} else {
			struct sockaddr_in *in =
				(struct sockaddr_in *) &aliases->node_addrs[i];
			inet_ntop(AF_INET, &in->sin_addr, addrbuf,
				  INET_ADDRSTRLEN);
			data_set_int(data_key_set(data_addrs, addrbuf),
				     in->sin_port);
		}
	}

	serialize_g_data_to_string(&json, NULL, data, MIME_TYPE_JSON,
				   SER_FLAGS_COMPACT);

	FREE_NULL_DATA(data);
	return json;
}

data_for_each_cmd_t _for_each_addr(const char *key, const data_t *data,
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

extern slurm_node_alias_addrs_t *extract_net_aliases(char *json)
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

	if (data_dict_for_each_const(data_addrs, _for_each_addr, aliases) < 0) {
		error("%s: data_dict_for_each_const failed", __func__);
		FREE_NULL_DATA(data);
		slurm_free_node_alias_addrs(aliases);
		return NULL;
	}

	FREE_NULL_DATA(data);
	return aliases;
}
