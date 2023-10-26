/*****************************************************************************\
 *  instant_on.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2022 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#if HAVE_JSON_C_INC
#  include <json-c/json.h>
#elif HAVE_JSON_INC
#  include <json/json.h>
#endif

#include "src/common/slurm_xlator.h"

#include "switch_hpe_slingshot.h"
#include "rest.h"

static slingshot_rest_conn_t jlope_conn;  /* Connection to jackaloped */

static bool instant_on_enabled = false;

/*
 * Read any authentication files and connect to the jackalope daemon,
 * which implements a REST interface providing Instant On data
 */
extern bool slingshot_init_instant_on(void)
{
	if (!slingshot_rest_connection(&jlope_conn, slingshot_config.jlope_url,
				       slingshot_config.jlope_auth,
				       slingshot_config.jlope_authdir,
				       SLINGSHOT_JLOPE_AUTH_BASIC_USER,
				       SLINGSHOT_JLOPE_AUTH_BASIC_PWD_FILE,
				       SLINGSHOT_JLOPE_TIMEOUT,
				       SLINGSHOT_JLOPE_CONNECT_TIMEOUT,
				       "Slingshot Jackalope daemon"))
		goto err;

	if (!slingshot_rest_connect(&jlope_conn))
		goto err;

	instant_on_enabled = true;
	return true;

err:
	info("Instant On support disabled due to errors");
	slingshot_rest_destroy_connection(&jlope_conn);
	instant_on_enabled = false;
	return false;
}

/*
 * Close connection to jackakoped REST interface, free memory
 */
extern void slingshot_fini_instant_on(void)
{
	slingshot_rest_destroy_connection(&jlope_conn);
}

/*
 * Convert string node list (i.e. "nid00000[2-3]") into JSON
 * array of node names
 */
static json_object *_node_list_to_json_array(char *node_list, uint32_t node_cnt)
{
	hostlist_t *hl = NULL;
	json_object *host_array = NULL;
	char *host;
	int ents;

	log_flag(SWITCH, "node_list=%s node_cnt=%u", node_list, node_cnt);
	if (!(host_array = json_object_new_array())) {
		error("Couldn't create host array");
		return NULL;
	}
	/* Optimization for single-node job steps */
	if (node_cnt == 1) {
		if (json_object_array_add(host_array,
				json_object_new_string(node_list))) {
			error("Couldn't add node list to host array");
			goto err;
		}
		return host_array;
	}
	if (!(hl = hostlist_create_dims(node_list, 0))) {
		error("Couldn't convert node list to hostlist");
		goto err;
	}
	for (ents = 0; (host = hostlist_shift_dims(hl, 0)); ents++) {
		if (json_object_array_add(host_array,
				json_object_new_string(host))) {
			error("Couldn't add host to host array");
			free(host);
			goto err;
		}
		free(host);
	}
	if (ents != node_cnt) {
		error("host_array ents %d != %u node_cnt", ents, node_cnt);
		goto err;
	}
	hostlist_destroy(hl);
	return host_array;
err:
	hostlist_destroy(hl);
	json_object_put(host_array);
	return NULL;
}

/*
 * Parse a single node's MAC address/device_name/numa_node arrays,
 * and append the info for each NIC to the job->nics[nicidx] array;
 * return the index of the array to use next (or -1 on error)
 */
static int _parse_jlope_node_json(slingshot_jobinfo_t *job,
				  int node_cnt, int nodeidx, int nicidx,
				  json_object *macs, json_object *devs,
				  json_object *numas)
{
	size_t macs_siz, devs_siz, numas_siz;

	if (!json_object_is_type(macs, json_type_array) ||
	    !json_object_is_type(devs, json_type_array) ||
	    !json_object_is_type(numas, json_type_array)) {
		error("Type error with jackaloped node response: macs=%d devs=%d numas=%d (should be %d)",
			json_object_get_type(macs), json_object_get_type(devs),
			json_object_get_type(numas), json_type_array);
		return -1;
	}
	macs_siz = json_object_array_length(macs);
	devs_siz = json_object_array_length(devs);
	numas_siz = json_object_array_length(numas);
	if (macs_siz != devs_siz || devs_siz != numas_siz) {
		error("Size error with jackaloped node response: macs=%zd devs=%zd numas=%zd",
			macs_siz, devs_siz, numas_siz);
		return -1;
	}

	/* Allocate/grow nics array if needed */
	if (nicidx >= job->num_nics) {
		if (job->num_nics == 0)
			job->num_nics = node_cnt * macs_siz;
		else
			job->num_nics += macs_siz;
		job->nics = xrecalloc(job->nics, job->num_nics,
				      sizeof(*(job->nics)));
		log_flag(SWITCH, "nics: nicidx/num_nics %d/%d",
			 nicidx, job->num_nics);
	}

	for (int i = 0; i < macs_siz; i++) {
		const char *mac = json_object_get_string(
					json_object_array_get_idx(macs, i));
		const char *dev = json_object_get_string(
					json_object_array_get_idx(devs, i));
		int numa = json_object_get_int(
					json_object_array_get_idx(numas, i));
		slingshot_hsn_nic_t *nic = &job->nics[nicidx];
		nic->nodeidx = nodeidx;
		nic->address_type = SLINGSHOT_ADDR_MAC;
		strlcpy(nic->address, mac, sizeof(nic->address));
		nic->numa_node = numa;
		strlcpy(nic->device_name, dev, sizeof(nic->device_name));

		log_flag(SWITCH, "nics[%d/%d].nodeidx=%d mac=%s dev=%s numa=%d",
			 nicidx, job->num_nics, nic->nodeidx, nic->address,
			 nic->device_name, nic->numa_node);
		nicidx++;
	}
	return nicidx;
}

/*
 * Parse the JSON response from jackaloped: 3 arrays of arrays of
 * MAC addresses, device names, and numa distances; looks like so:
 * {
 *   "mac": [["AA:BB:CC:DD:EE:FF", "FF:BB:CC:DD:EE:AA"]],
 *   "device": [["cxi0", "cxi1"]],
 *   "numa": [[126, 127]]
 * }
 * Add the information to the job->nics array to pass to slurmd
 */
static bool _parse_jlope_json(slingshot_jobinfo_t *job, json_object *resp,
			      int node_cnt)
{
	json_object *macs = json_object_object_get(resp, "mac");
	json_object *devs = json_object_object_get(resp, "device");
	json_object *numas = json_object_object_get(resp, "numa");
	size_t macs_siz, devs_siz, numas_siz;
	json_object *nodemacs, *nodedevs, *nodenumas;
	int nicidx = 0;

	if (!json_object_is_type(macs, json_type_array) ||
	    !json_object_is_type(devs, json_type_array) ||
	    !json_object_is_type(numas, json_type_array)) {
		error("Type error with jackaloped response: macs=%d devs=%d numas=%d (should be %d)",
			json_object_get_type(macs), json_object_get_type(devs),
			json_object_get_type(numas), json_type_array);
		return false;
	}
	macs_siz = json_object_array_length(macs);
	devs_siz = json_object_array_length(devs);
	numas_siz = json_object_array_length(numas);
	if (macs_siz != devs_siz || devs_siz != numas_siz ||
			numas_siz != node_cnt) {
		error("Size error with jackaloped response: macs=%zd devs=%zd numas=%zd nodes=%d",
			macs_siz, devs_siz, numas_siz, node_cnt);
		return false;
	}
	for (int i = nicidx = 0; i < macs_siz; i++) {
		nodemacs = json_object_array_get_idx(macs, i);
		nodedevs = json_object_array_get_idx(devs, i);
		nodenumas = json_object_array_get_idx(numas, i);
		if ((nicidx = _parse_jlope_node_json(job, node_cnt, i, nicidx,
				            nodemacs, nodedevs, nodenumas)) < 0)
			goto err;
	}

	/* Shrink 'nics' array if too large and attach to job struct */
	if (nicidx < job->num_nics) {
		job->num_nics = nicidx;
		job->nics = xrecalloc(job->nics, job->num_nics,
				      sizeof(*(job->nics)));
	}
	return true;

err:
	xfree(job->nics);
	return false;
}

/*
 * If configured with the jackaloped REST URL, contact jackaloped and
 * get Instant On data for the set of nodes in the job step
 */
extern bool slingshot_fetch_instant_on(slingshot_jobinfo_t *job,
				       char *node_list, uint32_t node_cnt)
{
	json_object *host_array = NULL;
	json_object *reqjson = NULL;
	json_object *respjson = NULL;
	long status = 0;
	bool rc = false;

	if (!slingshot_config.jlope_url || !instant_on_enabled)
		return true;

	if (!(host_array = _node_list_to_json_array(node_list, node_cnt)))
		goto out;
	if (!(reqjson = json_object_new_object()) ||
		json_object_object_add(reqjson, "hosts", host_array)) {
		error("Couldn't create instant on request json");
		json_object_put(host_array);
		goto out;
	}
	log_flag(SWITCH, "reqjson='%s'", json_object_to_json_string(reqjson));

	if (!(respjson = slingshot_rest_post(&jlope_conn, "/fabric/nics",
					     reqjson, &status))) {
		error("POST to jackaloped for instant on data failed: %ld",
		      status);
		goto out;
	}

	if (!(rc = _parse_jlope_json(job, respjson, node_cnt)))
		error("Couldn't parse jackaloped response: json='%s'",
		      json_object_to_json_string(respjson));

out:
	json_object_put(reqjson);
	json_object_put(respjson);
	return rc;
}
