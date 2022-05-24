/*****************************************************************************\
 *  slurm_protocol_api.c - high-level slurm communication functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
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

/* GLOBAL INCLUDES */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* PROJECT INCLUDES */
#include "src/common/assoc_mgr.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/hash.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_route.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"

strong_alias(convert_num_unit2, slurm_convert_num_unit2);
strong_alias(convert_num_unit, slurm_convert_num_unit);
strong_alias(revert_num_unit, slurm_revert_num_unit);
strong_alias(get_convert_unit_val, slurm_get_convert_unit_val);
strong_alias(get_unit_type, slurm_get_unit_type);

/* EXTERNAL VARIABLES */

/* #DEFINES */

/* STATIC VARIABLES */
static int message_timeout = -1;

/* STATIC FUNCTIONS */
static char *_global_auth_key(void);
static void  _remap_slurmctld_errno(void);
static uid_t _unpack_msg_uid(buf_t *buffer, uint16_t protocol_version);
static bool  _is_port_ok(int, uint16_t, bool);

/* define slurmdbd_conf here so we can treat its existence as a flag */
slurmdbd_conf_t *slurmdbd_conf = NULL;

/**********************************************************************\
 * protocol configuration functions
\**********************************************************************/

/* Free memory space returned by _slurm_api_get_comm_config() */
static void _slurm_api_free_comm_config(slurm_protocol_config_t *proto_conf)
{
	if (proto_conf) {
		xfree(proto_conf->controller_addr);
		xfree(proto_conf);
	}
}

/*
 * Get communication data structure based upon configuration file
 * RET communication information structure, call _slurm_api_free_comm_config
 *	to release allocated memory
 */
static slurm_protocol_config_t *_slurm_api_get_comm_config(void)
{
	slurm_protocol_config_t *proto_conf = NULL;
	slurm_addr_t controller_addr;
	slurm_conf_t *conf;
	int i;

	conf = slurm_conf_lock();

	if (!conf->control_cnt ||
	    !conf->control_addr || !conf->control_addr[0]) {
		error("Unable to establish controller machine");
		goto cleanup;
	}
	if (conf->slurmctld_port == 0) {
		error("Unable to establish controller port");
		goto cleanup;
	}
	if (conf->control_cnt == 0) {
		error("No slurmctld servers configured");
		goto cleanup;
	}

	memset(&controller_addr, 0, sizeof(slurm_addr_t));
	slurm_set_addr(&controller_addr, conf->slurmctld_port,
		       conf->control_addr[0]);
	if (slurm_addr_is_unspec(&controller_addr)) {
		error("Unable to establish control machine address");
		goto cleanup;
	}

	proto_conf = xmalloc(sizeof(slurm_protocol_config_t));
	proto_conf->controller_addr = xcalloc(conf->control_cnt,
					      sizeof(slurm_addr_t));
	proto_conf->control_cnt = conf->control_cnt;
	memcpy(&proto_conf->controller_addr[0], &controller_addr,
	       sizeof(slurm_addr_t));

	for (i = 1; i < proto_conf->control_cnt; i++) {
		if (conf->control_addr[i]) {
			slurm_set_addr(&proto_conf->controller_addr[i],
				       conf->slurmctld_port,
				       conf->control_addr[i]);
		}
	}

	if (conf->slurmctld_addr) {
		proto_conf->vip_addr_set = true;
		slurm_set_addr(&proto_conf->vip_addr, conf->slurmctld_port,
			       conf->slurmctld_addr);
	}

cleanup:
	slurm_conf_unlock();
	return proto_conf;
}

static int _check_hash(buf_t *buffer, header_t *header, slurm_msg_t *msg,
		       void *cred)
{
	char *cred_hash = NULL;
	uint32_t cred_hash_len = 0;
	int rc;
	static time_t config_update = (time_t) -1;
	static bool block_null_hash = true;
	static bool block_zero_hash = true;

	if (config_update != slurm_conf.last_update) {
		block_null_hash = (xstrcasestr(slurm_conf.comm_params,
					       "block_null_hash"));
		block_zero_hash = (xstrcasestr(slurm_conf.comm_params,
					       "block_zero_hash"));
		config_update = slurm_conf.last_update;
	}

	if (!slurm_get_plugin_hash_enable(msg->auth_index))
		return SLURM_SUCCESS;

	rc = auth_g_get_data(cred, &cred_hash, &cred_hash_len);
	if (cred_hash_len) {
		log_flag_hex(NET_RAW, cred_hash, cred_hash_len,
			     "%s: cred_hash:", __func__);
		if (cred_hash[0] == HASH_PLUGIN_NONE) {
			/*
			 * Unfortunately the older versions did not normalize
			 * msg_type to network-byte order when this was added
			 * to the payload, so the sequence may be flipped and
			 * either ordering must be permitted.
			 */
			uint16_t msg_type_nb = htons(msg->msg_type);
			char *type = (char *) &msg_type_nb;

			if (block_zero_hash || (cred_hash_len != 3))
				rc = SLURM_ERROR;
			else if ((cred_hash[1] == type[0]) &&
				 (cred_hash[2] == type[1]))
				msg->hash_index = HASH_PLUGIN_NONE;
			else if ((msg->protocol_version <=
				  SLURM_21_08_PROTOCOL_VERSION) &&
				 (cred_hash[1] == type[1]) &&
				 (cred_hash[2] == type[0]))
				msg->hash_index = HASH_PLUGIN_NONE;
			else
				rc = SLURM_ERROR;
		} else {
			char *data;
			uint32_t size = header->body_length;
			slurm_hash_t hash = { 0 };
			int h_len;
			uint16_t msg_type = htons(msg->msg_type);

			data = get_buf_data(buffer) + get_buf_offset(buffer);
			hash.type = cred_hash[0];

			h_len = hash_g_compute(data, size, (char *) &msg_type,
					       sizeof(msg_type), &hash);
			if ((h_len + 1) != cred_hash_len ||
			    memcmp(cred_hash + 1, hash.hash, h_len))
				rc = SLURM_ERROR;
			else
				msg->hash_index = hash.type;
			log_flag_hex(NET_RAW, &hash, sizeof(hash),
				     "%s: hash:", __func__);
		}
	} else if (block_null_hash)
		rc = SLURM_ERROR;

	xfree(cred_hash);
	return rc;
}

static int _compute_hash(buf_t *buffer, slurm_msg_t *msg, slurm_hash_t *hash)
{
	int h_len = 0;

	if (slurm_get_plugin_hash_enable(msg->auth_index)) {
		uint16_t msg_type = htons(msg->msg_type);

		if (msg->protocol_version <= SLURM_21_08_PROTOCOL_VERSION) {
			/*
			 * Unfortuantely 21.08.8 and 20.11.9 did not normalize
			 * this to network order, and require host-byte order.
			 */
			msg_type = msg->msg_type;
			hash->type = HASH_PLUGIN_NONE;
		} else if (msg->hash_index != HASH_PLUGIN_DEFAULT)
			hash->type = msg->hash_index;

		if (hash->type == HASH_PLUGIN_NONE) {
			memcpy(hash->hash, &msg_type, sizeof(msg_type));
			h_len = sizeof(msg->msg_type);
		} else {
			h_len = hash_g_compute(get_buf_data(buffer),
					       get_buf_offset(buffer),
					       (char *) &msg_type,
					       sizeof(msg_type), hash);
		}

		if (h_len < 0)
			return h_len;
		h_len++;
	}

	return h_len;

}

static int _get_tres_id(char *type, char *name)
{
	slurmdb_tres_rec_t tres_rec;
	memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
	tres_rec.type = type;
	tres_rec.name = name;

	return assoc_mgr_find_tres_pos(&tres_rec, false);
}

static int _tres_weight_item(double *weights, char *item_str)
{
	char *type = NULL, *value_str = NULL, *val_unit = NULL, *name = NULL;
	int tres_id;
	double weight_value = 0;

	if (!item_str) {
		error("TRES weight item is null");
		return SLURM_ERROR;
	}

	type = strtok_r(item_str, "=", &value_str);
	if (type == NULL) {
		error("\"%s\" is an invalid TRES weight entry", item_str);
		return SLURM_ERROR;
	}
	if (strchr(type, '/'))
		type = strtok_r(type, "/", &name);

	if (!value_str || !*value_str) {
		error("\"%s\" is an invalid TRES weight entry", item_str);
		return SLURM_ERROR;
	}

	if ((tres_id = _get_tres_id(type, name)) == -1) {
		error("TRES weight '%s%s%s' is not a configured TRES type.",
		      type, (name) ? ":" : "", (name) ? name : "");
		return SLURM_ERROR;
	}

	errno = 0;
	weight_value = strtod(value_str, &val_unit);
	if (errno) {
		error("Unable to convert %s value to double in %s",
		      __func__, value_str);
		return SLURM_ERROR;
	}

	if (val_unit && *val_unit) {
		int base_unit = slurmdb_get_tres_base_unit(type);
		int convert_val = get_convert_unit_val(base_unit, *val_unit);
		if (convert_val == SLURM_ERROR)
			return SLURM_ERROR;
		if (convert_val > 0) {
			weight_value /= convert_val;
		}
	}

	weights[tres_id] = weight_value;

	return SLURM_SUCCESS;
}

/* slurm_get_tres_weight_array
 * IN weights_str - string of tres and weights to be parsed.
 * IN tres_cnt - count of how many tres' are on the system (e.g.
 * 		slurmctld_tres_cnt).
 * IN fail - whether to fatal or not if there are parsing errors.
 * RET double* of tres weights.
 */
double *slurm_get_tres_weight_array(char *weights_str, int tres_cnt, bool fail)
{
	double *weights;
	char *tmp_str;
	char *token, *last = NULL;

	if (!weights_str || !*weights_str || !tres_cnt)
		return NULL;

	tmp_str = xstrdup(weights_str);
	weights = xcalloc(tres_cnt, sizeof(double));

	token = strtok_r(tmp_str, ",", &last);
	while (token) {
		if (_tres_weight_item(weights, token)) {
			xfree(weights);
			xfree(tmp_str);
			if (fail)
				fatal("failed to parse tres weights str '%s'",
				      weights_str);
			else
				error("failed to parse tres weights str '%s'",
				      weights_str);
			return NULL;
		}
		token = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_str);
	return weights;
}

/* slurm_get_stepd_loc
 * get path to the slurmstepd
 *      1. configure --sbindir concatenated with slurmstepd.
 *	2. configure --prefix concatenated with /sbin/slurmstepd.
 * RET char * - absolute path to the slurmstepd, MUST be xfreed by caller
 */
extern char *slurm_get_stepd_loc(void)
{
#ifdef SBINDIR
	return xstrdup_printf("%s/slurmstepd", SBINDIR);
#elif defined SLURM_PREFIX
	return xstrdup_printf("%s/sbin/slurmstepd", SLURM_PREFIX);
#endif
}

/* slurm_get_tmp_fs
 * returns the TmpFS configuration parameter from slurm_conf object
 * RET char *    - tmp_fs, MUST be xfreed by caller
 */
extern char *slurm_get_tmp_fs(char *node_name)
{
	char *tmp_fs = NULL;
	slurm_conf_t *conf = NULL;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		if (!node_name)
			tmp_fs = xstrdup(conf->tmp_fs);
		else
			tmp_fs = slurm_conf_expand_slurmd_path(
				conf->tmp_fs, node_name, NULL);
		slurm_conf_unlock();
	}
	return tmp_fs;
}

/* slurm_get_track_wckey
 * returns the value of track_wckey in slurm_conf object
 */
extern uint16_t slurm_get_track_wckey(void)
{
	uint16_t track_wckey = 0;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
		track_wckey = slurmdbd_conf->track_wckey;
	} else {
		conf = slurm_conf_lock();
		track_wckey = conf->conf_flags & CTL_CONF_WCKEY ? 1 : 0;
		slurm_conf_unlock();
	}
	return track_wckey;
}

/* slurm_with_slurmdbd
 * returns true if operating with slurmdbd
 */
bool slurm_with_slurmdbd(void)
{
	static bool with_slurmdbd = false;
	static bool is_set = false;
	slurm_conf_t *conf;

	/*
	 * Since accounting_storage_type is a plugin and plugins can't change
	 * on reconfigure, we don't need to worry about reconfigure with this
	 * static variable.
	 */
	if (is_set)
		return with_slurmdbd;

	conf = slurm_conf_lock();
	with_slurmdbd = !xstrcasecmp(conf->accounting_storage_type,
	                             "accounting_storage/slurmdbd");
	is_set = true;
	slurm_conf_unlock();
	return with_slurmdbd;
}

/*
 * Convert AuthInfo to a socket path. Accepts two input formats:
 * 1) <path>		(Old format)
 * 2) socket=<path>[,]	(New format)
 * NOTE: Caller must xfree return value
 */
extern char *slurm_auth_opts_to_socket(char *opts)
{
	char *socket = NULL, *sep, *tmp;

	if (!opts)
		return NULL;

	tmp = strstr(opts, "socket=");
	if (tmp) {
		/* New format */
		socket = xstrdup(tmp + 7);
		sep = strchr(socket, ',');
		if (sep)
			sep[0] = '\0';
	} else if (strchr(opts, '=')) {
		/* New format, but socket not specified */
		;
	} else {
		/* Old format */
		socket = xstrdup(opts);
	}

	return socket;
}

/* slurm_get_auth_ttl
 * returns the credential Time To Live option from the AuthInfo parameter
 * cache value in local buffer for best performance
 * RET int - Time To Live in seconds or 0 if not specified
 */
extern int slurm_get_auth_ttl(void)
{
	static int ttl = -1;
	char *tmp;

	if (ttl >= 0)
		return ttl;

	if (!slurm_conf.authinfo)
		return 0;

	tmp = strstr(slurm_conf.authinfo, "ttl=");
	if (tmp) {
		ttl = atoi(tmp + 4);
		if (ttl < 0)
			ttl = 0;
	} else {
		ttl = 0;
	}

	return ttl;
}

/* _global_auth_key
 * returns the storage password from slurm_conf or slurmdbd_conf object
 * cache value in local buffer for best performance
 * RET char *    - storage password
 */
static char *_global_auth_key(void)
{
	static bool loaded_storage_pass = false;
	static char storage_pass[512] = "\0";
	static char *storage_pass_ptr = NULL;

	if (loaded_storage_pass)
		return storage_pass_ptr;

	if (slurmdbd_conf) {
		if (slurm_conf.authinfo) {
			if (strlcpy(storage_pass, slurm_conf.authinfo,
				    sizeof(storage_pass))
			    >= sizeof(storage_pass))
				fatal("AuthInfo is too long");
			storage_pass_ptr = storage_pass;
		}
	} else {
		slurm_conf_t *conf = slurm_conf_lock();
		if (conf->accounting_storage_pass) {
			if (strlcpy(storage_pass, conf->accounting_storage_pass,
				    sizeof(storage_pass))
			    >= sizeof(storage_pass))
				fatal("AccountingStoragePass is too long");
			storage_pass_ptr = storage_pass;
		}
		slurm_conf_unlock();
	}

	loaded_storage_pass = true;
	return storage_pass_ptr;
}

/* slurm_get_interconnect_accounting_type
 * get InterconnectAccountingType from slurm_conf object
 * RET char *   - interconnect_accounting type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_interconnect_type(void)
{
	char *acct_gather_interconnect_type = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		acct_gather_interconnect_type =
			xstrdup(conf->acct_gather_interconnect_type);
		slurm_conf_unlock();
	}
	return acct_gather_interconnect_type;
}

/* slurm_get_filesystem_accounting_type
 * get FilesystemAccountingType from slurm_conf object
 * RET char *   - filesystem_accounting type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_filesystem_type(void)
{
	char *acct_gather_filesystem_type = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		acct_gather_filesystem_type =
			xstrdup(conf->acct_gather_filesystem_type);
		slurm_conf_unlock();
	}
	return acct_gather_filesystem_type;
}


extern uint16_t slurm_get_acct_gather_node_freq(void)
{
	uint16_t freq = 0;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		freq = conf->acct_gather_node_freq;
		slurm_conf_unlock();
	}
	return freq;
}

/* slurm_get_ext_sensors_type
 * get ExtSensorsType from slurm_conf object
 * RET char *   - ext_sensors type, MUST be xfreed by caller
 */
char *slurm_get_ext_sensors_type(void)
{
	char *ext_sensors_type = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		ext_sensors_type =
			xstrdup(conf->ext_sensors_type);
		slurm_conf_unlock();
	}
	return ext_sensors_type;
}

extern uint16_t slurm_get_ext_sensors_freq(void)
{
	uint16_t freq = 0;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		freq = conf->ext_sensors_freq;
		slurm_conf_unlock();
	}
	return freq;
}

/*
 * returns the configured GpuFreqDef value
 * RET char *    - GpuFreqDef value,  MUST be xfreed by caller
 */
char *slurm_get_gpu_freq_def(void)
{
	char *gpu_freq_def = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		gpu_freq_def = xstrdup(conf->gpu_freq_def);
		slurm_conf_unlock();
	}
	return gpu_freq_def;
}

/* slurm_get_preempt_type
 * get PreemptType from slurm_conf object
 * RET char *   - preempt type, MUST be xfreed by caller
 */
char *slurm_get_preempt_type(void)
{
	char *preempt_type = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		preempt_type = xstrdup(conf->preempt_type);
		slurm_conf_unlock();
	}
	return preempt_type;
}

/* slurm_get_select_type
 * get select_type from slurm_conf object
 * RET char *   - select_type, MUST be xfreed by caller
 */
char *slurm_get_select_type(void)
{
	char *select_type = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		select_type = xstrdup(conf->select_type);
		slurm_conf_unlock();
	}
	return select_type;
}

/** Return true if (remote) system runs Cray Aries */
bool is_cray_select_type(void)
{
	bool result = false;

	if (slurmdbd_conf) {
	} else {
		slurm_conf_t *conf = slurm_conf_lock();
		result = !xstrcasecmp(conf->select_type, "select/cray_aries");
		slurm_conf_unlock();
	}
	return result;
}

/*  slurm_get_srun_port_range()
 */
uint16_t *
slurm_get_srun_port_range(void)
{
	uint16_t *ports = NULL;
	slurm_conf_t *conf;

	if (slurmdbd_conf) {
	} else {
		conf = slurm_conf_lock();
		ports = conf->srun_port_range;
		slurm_conf_unlock();
	}
	return ports;	/* CLANG false positive */
}

/* slurm_get_core_spec_plugin
 * RET core_spec plugin name, must be xfreed by caller */
char *slurm_get_core_spec_plugin(void)
{
	char *core_spec_plugin = NULL;
	slurm_conf_t *conf;

	conf = slurm_conf_lock();
	core_spec_plugin = xstrdup(conf->core_spec_plugin);
	slurm_conf_unlock();
	return core_spec_plugin;
}

/* Change general slurm communication errors to slurmctld specific errors */
static void _remap_slurmctld_errno(void)
{
	int err = slurm_get_errno();

	if (err == SLURM_COMMUNICATIONS_CONNECTION_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);
	else if (err ==  SLURM_COMMUNICATIONS_SEND_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_SEND_ERROR);
	else if (err == SLURM_COMMUNICATIONS_RECEIVE_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR);
	else if (err == SLURM_COMMUNICATIONS_SHUTDOWN_ERROR)
		slurm_seterrno(SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR);
}

/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections. Retry if bind() or listen() fail
 *      even if asked for an ephemeral port.
 *
 * IN  port     - port to bind the msg server to
 * RET int      - file descriptor of the connection created
 */
int slurm_init_msg_engine_port(uint16_t port)
{
	int cc;
	slurm_addr_t addr;
	int i;

	slurm_setup_addr(&addr, port);
	cc = slurm_init_msg_engine(&addr, (port == 0));
	if ((cc < 0) && (port == 0) && (errno == EADDRINUSE)) {
		/* All ephemeral ports are in use, test other ports */
		for (i = 10001; i < 65536; i++) {
			slurm_set_port(&addr, i);
			cc = slurm_init_msg_engine(&addr, true);
			if (cc >= 0)
				break;
		}
		if (cc < 0)
			error("%s: all ephemeral ports, and the range (10001, 65536) are exhausted, cannot establish listening port",
			      __func__);
	}
	return cc;
}

/* slurm_init_msg_engine_ports()
 */
int slurm_init_msg_engine_ports(uint16_t *ports)
{
	slurm_addr_t addr;
	int cc;
	int val;
	int s;
	int port;

	slurm_setup_addr(&addr, 0);

	s = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0)
		return -1;

	val = 1;
	cc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (cc < 0) {
		close(s);
		return -1;
	}

	if ((port = sock_bind_listen_range(s, ports, false)) < 0)
		return -1;

	return s;
}

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/*
 * Creates a SOCK_STREAM (TCP) socket and calls connect() on it.
 * Will only receive messages from the address (w/port) argument.
 * IN slurm_address	- slurm_addr_t of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
int slurm_open_msg_conn(slurm_addr_t * slurm_address)
{
	int fd = slurm_open_stream(slurm_address, false);

	return fd;
}

/*
 * Calls connect to make a connection-less datagram connection
 *	primary or secondary slurmctld message engine
 * IN/OUT addr       - address of controller contacted
 * IN/OUT use_backup - IN: whether to try the backup first or not
 *                     OUT: set to true if connection established with backup
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET slurm_fd	- file descriptor of the connection created
 */
extern int slurm_open_controller_conn(slurm_addr_t *addr, bool *use_backup,
				      slurmdb_cluster_rec_t *comm_cluster_rec)
{
	int fd = -1;
	slurm_protocol_config_t *proto_conf = NULL;
	int i, retry, max_retry_period;
	uint16_t port;

	if (!comm_cluster_rec) {
		/* This means the addr wasn't set up already */
		if (!(proto_conf = _slurm_api_get_comm_config()))
			return SLURM_ERROR;

		for (i = 0; i < proto_conf->control_cnt; i++) {
			port = slurm_conf.slurmctld_port +
				((time(NULL) + getpid()) %
				 slurm_conf.slurmctld_port_count);
			slurm_set_port(&(proto_conf->controller_addr[i]), port);
		}

		if (proto_conf->vip_addr_set) {
			port = slurm_conf.slurmctld_port +
				((time(NULL) + getpid()) %
				 slurm_conf.slurmctld_port_count);
			slurm_set_port(&(proto_conf->vip_addr), port);
		}
	}

#ifdef HAVE_NATIVE_CRAY
	max_retry_period = 180;
#else
	max_retry_period = slurm_conf.msg_timeout;
#endif
	for (retry = 0; retry < max_retry_period; retry++) {
		if (retry)
			sleep(1);
		if (comm_cluster_rec) {
			if (slurm_addr_is_unspec(
				&comm_cluster_rec->control_addr)) {
				slurm_set_addr(
					&comm_cluster_rec->control_addr,
					comm_cluster_rec->control_port,
					comm_cluster_rec->control_host);
			}
			addr = &comm_cluster_rec->control_addr;

			fd = slurm_open_msg_conn(addr);
			if (fd >= 0)
				goto end_it;
			log_flag(NET, "%s: Failed to contact controller(%pA): %m",
				 __func__, addr);
		} else if (proto_conf->vip_addr_set) {
			fd = slurm_open_msg_conn(&proto_conf->vip_addr);
			if (fd >= 0)
				goto end_it;
			log_flag(NET, "%s: Failed to contact controller(%pA): %m",
				 __func__, &proto_conf->vip_addr);
		} else {
			if (!*use_backup) {
				fd = slurm_open_msg_conn(
						&proto_conf->controller_addr[0]);
				if (fd >= 0) {
					*use_backup = false;
					goto end_it;
				}
				log_flag(NET,"%s: Failed to contact primary controller(%pA): %m",
					 __func__,
					 &proto_conf->controller_addr[0]);
			}
			if ((proto_conf->control_cnt > 1) || *use_backup) {
				for (i = 1; i < proto_conf->control_cnt; i++) {
					fd = slurm_open_msg_conn(
						&proto_conf->controller_addr[i]);
					if (fd >= 0) {
						log_flag(NET, "%s: Contacted backup controller(%pA) attempt:%d",
							 __func__,
							 &proto_conf->controller_addr[i],
							 (i - 1));
						*use_backup = true;
						goto end_it;
					}
				}
				*use_backup = false;
				log_flag(NET, "%s: Failed to contact backup controller: %m",
					 __func__);
			}
		}
	}
	addr = NULL;
	_slurm_api_free_comm_config(proto_conf);
	slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);

end_it:
	_slurm_api_free_comm_config(proto_conf);
	return fd;
}

/*
 * Calls connect to make a connection-less datagram connection to a specific
 *	primary or backup slurmctld message engine
 * IN dest      - controller to contact (0=primary, 1=backup, 2=backup2, etc.)
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET int      - file descriptor of the connection created
 */
extern int slurm_open_controller_conn_spec(int dest,
				      slurmdb_cluster_rec_t *comm_cluster_rec)
{
	slurm_protocol_config_t *proto_conf = NULL;
	slurm_addr_t *addr;
	int rc;

	if (comm_cluster_rec) {
		if (slurm_addr_is_unspec(&comm_cluster_rec->control_addr)) {
			slurm_set_addr(
				&comm_cluster_rec->control_addr,
				comm_cluster_rec->control_port,
				comm_cluster_rec->control_host);
		}
		addr = &comm_cluster_rec->control_addr;
	} else {	/* Some backup slurmctld */
		if (!(proto_conf = _slurm_api_get_comm_config())) {
			debug3("Error: Unable to set default config");
			return SLURM_ERROR;
		}
		addr = NULL;
		if ((dest >= 0) && (dest <= proto_conf->control_cnt))
			addr = &proto_conf->controller_addr[dest];
		if (!addr) {
			rc = SLURM_ERROR;
			goto fini;
		}
	}

	rc = slurm_open_msg_conn(addr);
	if (rc == -1) {
		log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
			 __func__, addr);
		_remap_slurmctld_errno();
	}
fini:	_slurm_api_free_comm_config(proto_conf);
	return rc;
}

extern int slurm_unpack_received_msg(slurm_msg_t *msg, int fd, buf_t *buffer)
{
	header_t header;
	int rc;
	void *auth_cred = NULL;
	char *peer = NULL;

	if (slurm_conf.debug_flags & (DEBUG_FLAG_NET | DEBUG_FLAG_NET_RAW)) {
		/*
		* cache to avoid resolving multiple times
		* this call is expensive
		*/
		peer = fd_resolve_peer(fd);
	}

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		uid_t uid = _unpack_msg_uid(buffer, header.version);

		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] Invalid Protocol Version %u from uid=%u: %m",
		      __func__, peer, header.version, uid);

		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	if (header.ret_cnt > 0) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] we received more than one message back use slurm_receive_msgs instead",
		      __func__, peer);
		header.ret_cnt = 0;
		FREE_NULL_LIST(header.ret_list);
		header.ret_list = NULL;
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] We need to forward this to other nodes use slurm_receive_msg_and_forward instead",
		      __func__, peer);
	}

	if (!(auth_cred = auth_g_unpack(buffer, header.version))) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_unpack: %s has authentication error: %m",
		      __func__, peer, rpc_num2string(header.msg_type));
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	msg->auth_index = slurm_auth_index(auth_cred);
	if (header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = auth_g_verify(auth_cred, _global_auth_key());
	} else {
		rc = auth_g_verify(auth_cred, slurm_conf.authinfo);
	}

	if (rc != SLURM_SUCCESS) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_verify: %s has authentication error: %s",
		      __func__, peer, rpc_num2string(header.msg_type),
		      slurm_strerror(rc));
		(void) auth_g_destroy(auth_cred);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}

	msg->auth_uid = auth_g_get_uid(auth_cred);
	msg->auth_uid_set = true;

	/*
	 * Unpack message body
	 */
	msg->protocol_version = header.version;
	msg->msg_type = header.msg_type;
	msg->flags = header.flags;

	msg->body_offset =  get_buf_offset(buffer);

	if ((header.body_length > remaining_buf(buffer)) ||
	    _check_hash(buffer, &header, msg, auth_cred) ||
	    (unpack_msg(msg, buffer) != SLURM_SUCCESS)) {
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		(void) auth_g_destroy(auth_cred);
		goto total_return;
	}

	msg->auth_cred = (void *)auth_cred;

	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	slurm_seterrno(rc);
	if (rc != SLURM_SUCCESS) {
		msg->auth_cred = (void *) NULL;
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] %s", __func__, peer, slurm_strerror(rc));
		rc = -1;
		usleep(10000);	/* Discourage brute force attack */
	} else {
		rc = 0;
	}
	xfree(peer);
	return rc;
}

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/*
 * NOTE: memory is allocated for the returned msg must be freed at
 *       some point using the slurm_free_functions.
 * IN fd	- file descriptor to receive msg on
 * OUT msg	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
int slurm_receive_msg(int fd, slurm_msg_t *msg, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	int rc;
	buf_t *buffer;
	bool keep_buffer = false;

	if (msg->flags & SLURM_MSG_KEEP_BUFFER)
		keep_buffer = true;

	if (msg->conn) {
		persist_msg_t persist_msg;

		buffer = slurm_persist_recv_msg(msg->conn);
		if (!buffer) {
			error("%s: No response to persist_init", __func__);
			slurm_persist_conn_close(msg->conn);
			return SLURM_ERROR;
		}
		memset(&persist_msg, 0, sizeof(persist_msg_t));
		rc = slurm_persist_msg_unpack(msg->conn, &persist_msg, buffer);

		if (keep_buffer)
			msg->buffer = buffer;
		else
			free_buf(buffer);

		if (rc) {
			error("%s: Failed to unpack persist msg", __func__);
			slurm_persist_conn_close(msg->conn);
			return SLURM_ERROR;
		}

		msg->msg_type = persist_msg.msg_type;
		msg->data = persist_msg.data;

		return SLURM_SUCCESS;
	}

	xassert(fd >= 0);

	msg->conn_fd = fd;

	if (timeout <= 0)
		/* convert secs to msec */
		timeout = slurm_conf.msg_timeout * MSEC_IN_SEC;

	else if (timeout > (slurm_conf.msg_timeout * MSEC_IN_SEC * 10)) {
		/* consider 10x the timeout to be very long */
		log_flag(NET, "%s: You are receiving a message with very long timeout of %d seconds",
			 __func__, (timeout / MSEC_IN_SEC));
	} else if (timeout < MSEC_IN_SEC) {
		/* consider a less than 1 second to be very short */
		error("%s: You are receiving a message with a very short "
		      "timeout of %d msecs", __func__, timeout);
	}

	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		rc = errno;
		if (!rc)
			rc = SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR;
		goto endit;
	}

	log_flag_hex(NET_RAW, buf, buflen, "%s: read", __func__);
	buffer = create_buf(buf, buflen);

	rc = slurm_unpack_received_msg(msg, fd, buffer);

	if (keep_buffer)
		msg->buffer = buffer;
	else
		free_buf(buffer);

endit:
	slurm_seterrno(rc);

	return rc;
}

/*
 * NOTE: memory is allocated for the returned list
 *       and must be freed at some point using the list_destroy function.
 * IN open_fd	- file descriptor to receive msg on
 * IN steps	- how many steps down the tree we have to wait for
 * IN timeout	- how long to wait in milliseconds
 * RET List	- List containing the responses of the children (if any) we
 *		  forwarded the message to. List containing type
 *		  (ret_data_info_t).
 */
List slurm_receive_msgs(int fd, int steps, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	slurm_msg_t msg;
	buf_t *buffer;
	ret_data_info_t *ret_data_info = NULL;
	List ret_list = NULL;
	int orig_timeout = timeout;
	char *peer = NULL;

	xassert(fd >= 0);

	if (slurm_conf.debug_flags & (DEBUG_FLAG_NET | DEBUG_FLAG_NET_RAW)) {
		/*
		 * cache to avoid resolving multiple times
		 * this call is expensive
		 */
		peer = fd_resolve_peer(fd);
	}

	slurm_msg_t_init(&msg);
	msg.conn_fd = fd;

	if (timeout <= 0) {
		/* convert secs to msec */
		timeout = slurm_conf.msg_timeout * 1000;
		orig_timeout = timeout;
	}
	if (steps) {
		if (message_timeout < 0)
			message_timeout = slurm_conf.msg_timeout * 1000;
		orig_timeout = (timeout -
				(message_timeout*(steps-1)))/steps;
		steps--;
	}

	log_flag(NET, "%s: [%s] orig_timeout was %d we have %d steps and a timeout of %d",
		 __func__, peer, orig_timeout, steps, timeout);
	/* we compare to the orig_timeout here because that is really
	 *  what we are going to wait for each step
	 */
	if (orig_timeout >= (slurm_conf.msg_timeout * 10000)) {
		log_flag(NET, "%s: [%s] Sending a message with timeout's greater than %d seconds, requested timeout is %d seconds",
			 __func__, peer, (slurm_conf.msg_timeout * 10),
			 (timeout/1000));
	} else if (orig_timeout < 1000) {
		log_flag(NET, "%s: [%s] Sending a message with a very short timeout of %d milliseconds each step in the tree has %d milliseconds",
			 __func__, peer, timeout, orig_timeout);
	}


	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward);
		rc = errno;
		goto total_return;
	}

	log_flag_hex(NET_RAW, buf, buflen, "%s: [%s] read", __func__, peer);
	buffer = create_buf(buf, buflen);

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		uid_t uid = _unpack_msg_uid(buffer, header.version);

		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] Invalid Protocol Version %u from uid=%d: %m",
		      __func__, peer, header.version, uid);

		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	//info("ret_cnt = %d",header.ret_cnt);
	if (header.ret_cnt > 0) {
		if (header.ret_list)
			ret_list = header.ret_list;
		else
			ret_list = list_create(destroy_data_info);
		header.ret_cnt = 0;
		header.ret_list = NULL;
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] We need to forward this to other nodes use slurm_receive_msg_and_forward instead",
		      __func__, peer);
	}

	if (!(auth_cred = auth_g_unpack(buffer, header.version))) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_unpack: %m", __func__, peer);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	msg.auth_index = slurm_auth_index(auth_cred);
	if (header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = auth_g_verify(auth_cred, _global_auth_key());
	} else {
		rc = auth_g_verify(auth_cred, slurm_conf.authinfo);
	}

	if (rc != SLURM_SUCCESS) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_verify: %s has authentication error: %m",
		      __func__, peer, rpc_num2string(header.msg_type));
		(void) auth_g_destroy(auth_cred);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}

	msg.auth_uid = auth_g_get_uid(auth_cred);
	msg.auth_uid_set = true;

	/*
	 * Unpack message body
	 */
	msg.protocol_version = header.version;
	msg.msg_type = header.msg_type;
	msg.flags = header.flags;

	if ((header.body_length > remaining_buf(buffer)) ||
	    _check_hash(buffer, &header, &msg, auth_cred) ||
	    (unpack_msg(&msg, buffer) != SLURM_SUCCESS)) {
		(void) auth_g_destroy(auth_cred);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	auth_g_destroy(auth_cred);

	free_buf(buffer);
	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	if (rc != SLURM_SUCCESS) {
		if (ret_list) {
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			ret_data_info->err = rc;
			ret_data_info->type = RESPONSE_FORWARD_FAILED;
			ret_data_info->data = NULL;
			list_push(ret_list, ret_data_info);
		}

		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] failed: %s",
		      __func__, peer, slurm_strerror(rc));
		usleep(10000);	/* Discourage brute force attack */
	} else {
		if (!ret_list)
			ret_list = list_create(destroy_data_info);
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		ret_data_info->err = rc;
		ret_data_info->node_name = NULL;
		ret_data_info->type = msg.msg_type;
		ret_data_info->data = msg.data;
		list_push(ret_list, ret_data_info);
	}

	errno = rc;
	xfree(peer);
	return ret_list;

}

extern List slurm_receive_resp_msgs(int fd, int steps, int timeout)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	slurm_msg_t msg;
	buf_t *buffer;
	ret_data_info_t *ret_data_info = NULL;
	List ret_list = NULL;
	int orig_timeout = timeout;
	char *peer = NULL;

	xassert(fd >= 0);

	if (slurm_conf.debug_flags & (DEBUG_FLAG_NET | DEBUG_FLAG_NET_RAW)) {
		/*
		* cache to avoid resolving multiple times
		* this call is expensive
		*/
		peer = fd_resolve_peer(fd);
	}

	slurm_msg_t_init(&msg);
	msg.conn_fd = fd;

	if (timeout <= 0) {
		/* convert secs to msec */
		timeout = slurm_conf.msg_timeout * 1000;
		orig_timeout = timeout;
	}

	if (steps) {
		if (message_timeout < 0)
			message_timeout = slurm_conf.msg_timeout * 1000;
		orig_timeout = timeout - (message_timeout * (steps - 1));
		orig_timeout /= steps;
		steps--;
	}

	log_flag(NET, "%s: [%s] orig_timeout was %d we have %d steps and a timeout of %d",
		 __func__, peer, orig_timeout, steps, timeout);
	/*
	 * Compare to the orig_timeout here, because that is what we are
	 * going to wait for each step.
	 */
	if (orig_timeout >= (slurm_conf.msg_timeout * 10000)) {
		log_flag(NET, "%s: [%s] Sending a message with timeouts greater than %d seconds, requested timeout is %d seconds",
			 __func__, peer, (slurm_conf.msg_timeout * 10),
			 (timeout / 1000));
	} else if (orig_timeout < 1000) {
		log_flag(NET, "%s: [%s] Sending a message with a very short timeout of %d milliseconds, each step in the tree has %d milliseconds",
			 __func__, peer, timeout, orig_timeout);
	}

	/*
	 * Receive a msg. slurm_msg_recvfrom_timeout() will read the message
	 * length and allocate space on the heap for a buffer containing the
	 * message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0, timeout) < 0) {
		forward_init(&header.forward);
		rc = errno;
		goto total_return;
	}

	log_flag_hex(NET_RAW, buf, buflen, "%s: [%s] read", __func__, peer);
	buffer = create_buf(buf, buflen);

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] Invalid Protocol Version %u: %m",
		      __func__, peer, header.version);

		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}

	if (header.ret_cnt > 0) {
		if (header.ret_list)
			ret_list = header.ret_list;
		else
			ret_list = list_create(destroy_data_info);
		header.ret_cnt = 0;
		header.ret_list = NULL;
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] We need to forward this to other nodes use slurm_receive_msg_and_forward instead",
		      __func__, peer);
	}

	/*
	 * Skip credential verification here. This is on the reply path, so the
	 * connections have been previously verified in the opposite direction.
	 */
	if (!(auth_cred = auth_g_unpack(buffer, header.version))) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_unpack: %m", __func__, peer);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	auth_g_destroy(auth_cred);

	/*
	 * Unpack message body
	 */
	msg.protocol_version = header.version;
	msg.msg_type = header.msg_type;
	msg.flags = header.flags;

	if ((header.body_length > remaining_buf(buffer)) ||
	    (unpack_msg(&msg, buffer) != SLURM_SUCCESS)) {
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	free_buf(buffer);
	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	if (rc != SLURM_SUCCESS) {
		if (ret_list) {
			ret_data_info = xmalloc(sizeof(ret_data_info_t));
			ret_data_info->err = rc;
			ret_data_info->type = RESPONSE_FORWARD_FAILED;
			ret_data_info->data = NULL;
			list_push(ret_list, ret_data_info);
		}
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] failed: %s",
		      __func__, peer, slurm_strerror(rc));
		usleep(10000);	/* Discourage brute force attack */
	} else {
		if (!ret_list)
			ret_list = list_create(destroy_data_info);
		ret_data_info = xmalloc(sizeof(ret_data_info_t));
		ret_data_info->err = rc;
		ret_data_info->node_name = NULL;
		ret_data_info->type = msg.msg_type;
		ret_data_info->data = msg.data;
		list_push(ret_list, ret_data_info);
	}

	errno = rc;
	xfree(peer);
	return ret_list;
}

/*
 * Try to determine the UID associated with a message with different
 * message header version, return INFINITE ((uid_t) -1) if we can't tell.
 */
static uid_t _unpack_msg_uid(buf_t *buffer, uint16_t protocol_version)
{
	uid_t uid = INFINITE;
	void *auth_cred = NULL;

	if (!(auth_cred = auth_g_unpack(buffer, protocol_version)))
		return uid;
	if (auth_g_verify(auth_cred, slurm_conf.authinfo))
		return uid;

	uid = auth_g_get_uid(auth_cred);
	auth_g_destroy(auth_cred);

	return uid;
}

/*
 * NOTE: memory is allocated for the returned msg and the returned list
 *       both must be freed at some point using the slurm_free_functions
 *       and list_destroy function.
 * IN open_fd	- file descriptor to receive msg on
 * IN/OUT msg	- a slurm_msg struct to be filled in by the function
 *		  we use the orig_addr from this var for forwarding.
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
int slurm_receive_msg_and_forward(int fd, slurm_addr_t *orig_addr,
				  slurm_msg_t *msg)
{
	char *buf = NULL;
	size_t buflen = 0;
	header_t header;
	int rc;
	void *auth_cred = NULL;
	buf_t *buffer;
	char *peer = NULL;

	xassert(fd >= 0);

	if (slurm_conf.debug_flags & (DEBUG_FLAG_NET | DEBUG_FLAG_NET_RAW)) {
		/*
		 * cache to avoid resolving multiple times
		 * this call is expensive
		 */
		peer = fd_resolve_peer(fd);
	}

	if (msg->forward.init != FORWARD_INIT)
		slurm_msg_t_init(msg);
	/* set msg connection fd to accepted fd. This allows
	 *  possibility for slurmd_req () to close accepted connection
	 */
	msg->conn_fd = fd;
	/* this always is the connection */
	memcpy(&msg->address, orig_addr, sizeof(slurm_addr_t));

	/* where the connection originated from, this
	 * might change based on the header we receive */
	memcpy(&msg->orig_addr, orig_addr, sizeof(slurm_addr_t));

	msg->ret_list = list_create(destroy_data_info);


	/*
	 * Receive a msg. slurm_msg_recvfrom() will read the message
	 *  length and allocate space on the heap for a buffer containing
	 *  the message.
	 */
	if (slurm_msg_recvfrom_timeout(fd, &buf, &buflen, 0,
				       (slurm_conf.msg_timeout * 1000)) < 0) {
		forward_init(&header.forward);
		rc = errno;
		goto total_return;
	}

	log_flag_hex(NET_RAW, buf, buflen, "%s: [%s] read", __func__, peer);
	buffer = create_buf(buf, buflen);

	if (unpack_header(&header, buffer) == SLURM_ERROR) {
		free_buf(buffer);
		rc = SLURM_COMMUNICATIONS_RECEIVE_ERROR;
		goto total_return;
	}

	if (check_header_version(&header) < 0) {
		uid_t uid = _unpack_msg_uid(buffer, header.version);

		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] Invalid Protocol Version %u from uid=%d: %m",
		      __func__, peer, header.version, uid);

		free_buf(buffer);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto total_return;
	}
	if (header.ret_cnt > 0) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] we received more than one message back use slurm_receive_msgs instead",
		      __func__, peer);
		header.ret_cnt = 0;
		FREE_NULL_LIST(header.ret_list);
		header.ret_list = NULL;
	}

	/*
	 * header.orig_addr will be set to where the first message
	 * came from if this is a forward else we set the
	 * header.orig_addr to our addr just in case we need to send it off.
	 */
	if (!slurm_addr_is_unspec(&header.orig_addr)) {
		memcpy(&msg->orig_addr, &header.orig_addr, sizeof(slurm_addr_t));
	} else {
		memcpy(&header.orig_addr, orig_addr, sizeof(slurm_addr_t));
	}

	/* Forward message to other nodes */
	if (header.forward.cnt > 0) {
		log_flag(NET, "%s: [%s] forwarding to %u nodes",
			 __func__, peer, header.forward.cnt);
		msg->forward_struct = xmalloc(sizeof(forward_struct_t));
		slurm_mutex_init(&msg->forward_struct->forward_mutex);
		slurm_cond_init(&msg->forward_struct->notify, NULL);

		msg->forward_struct->buf_len = remaining_buf(buffer);
		msg->forward_struct->buf =
			xmalloc(msg->forward_struct->buf_len);
		memcpy(msg->forward_struct->buf,
		       &buffer->head[buffer->processed],
		       msg->forward_struct->buf_len);

		msg->forward_struct->ret_list = msg->ret_list;
		/* take out the amount of timeout from this hop */
		msg->forward_struct->timeout = header.forward.timeout;
		if (!msg->forward_struct->timeout)
			msg->forward_struct->timeout = message_timeout;
		msg->forward_struct->fwd_cnt = header.forward.cnt;

		log_flag(NET, "%s: [%s] forwarding messages to %u nodes with timeout of %d",
			 __func__, peer, msg->forward_struct->fwd_cnt,
			 msg->forward_struct->timeout);

		if (forward_msg(msg->forward_struct, &header) == SLURM_ERROR) {
			/* peer may have not been resolved already */
			if (!peer)
				peer = fd_resolve_peer(fd);

			error("%s: [%s] problem with forward msg",
			      __func__, peer);
		}
	}

	if (!(auth_cred = auth_g_unpack(buffer, header.version))) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_unpack: %s has authentication error: %m",
		      __func__, peer, rpc_num2string(header.msg_type));
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	msg->auth_index = slurm_auth_index(auth_cred);
	if (header.flags & SLURM_GLOBAL_AUTH_KEY) {
		rc = auth_g_verify(auth_cred, _global_auth_key());
	} else {
		rc = auth_g_verify(auth_cred, slurm_conf.authinfo);
	}

	if (rc != SLURM_SUCCESS) {
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] auth_g_verify: %s has authentication error: %m",
		      __func__, peer, rpc_num2string(header.msg_type));
		(void) auth_g_destroy(auth_cred);
		free_buf(buffer);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto total_return;
	}

	msg->auth_uid = auth_g_get_uid(auth_cred);
	msg->auth_uid_set = true;

	/*
	 * Unpack message body
	 */
	msg->protocol_version = header.version;
	msg->msg_type = header.msg_type;
	msg->flags = header.flags;

	if ( (header.body_length > remaining_buf(buffer)) ||
	    _check_hash(buffer, &header, msg, auth_cred) ||
	     (unpack_msg(msg, buffer) != SLURM_SUCCESS) ) {
		(void) auth_g_destroy(auth_cred);
		free_buf(buffer);
		rc = ESLURM_PROTOCOL_INCOMPLETE_PACKET;
		goto total_return;
	}
	msg->auth_cred = (void *) auth_cred;

	free_buf(buffer);
	rc = SLURM_SUCCESS;

total_return:
	destroy_forward(&header.forward);

	slurm_seterrno(rc);
	if (rc != SLURM_SUCCESS) {
		msg->msg_type = RESPONSE_FORWARD_FAILED;
		msg->auth_cred = (void *) NULL;
		msg->data = NULL;
		/* peer may have not been resolved already */
		if (!peer)
			peer = fd_resolve_peer(fd);

		error("%s: [%s] failed: %s",
		      __func__, peer, slurm_strerror(rc));
		usleep(10000);	/* Discourage brute force attack */
	} else {
		rc = 0;
	}
	xfree(peer);
	return rc;

}

/**********************************************************************\
 * send message functions
\**********************************************************************/

/*
 *  Send a slurm message over an open file descriptor `fd'
 *    Returns the size of the message sent in bytes, or -1 on failure.
 */
int slurm_send_node_msg(int fd, slurm_msg_t * msg)
{
	header_t header;
	msg_bufs_t buffers = { 0 };
	int      rc;
	void *   auth_cred;
	time_t   start_time = time(NULL);
	slurm_hash_t hash = { 0 };
	int h_len;

	if (msg->conn) {
		persist_msg_t persist_msg;
		buf_t *buffer;

		memset(&persist_msg, 0, sizeof(persist_msg_t));
		persist_msg.msg_type  = msg->msg_type;
		persist_msg.data      = msg->data;
		persist_msg.data_size = msg->data_size;

		buffer = slurm_persist_msg_pack(msg->conn, &persist_msg);
		if (!buffer)    /* pack error */
			return SLURM_ERROR;

		rc = slurm_persist_send_msg(msg->conn, buffer);
		free_buf(buffer);

		if ((rc < 0) && (errno == ENOTCONN)) {
			log_flag(NET, "%s: persistent connection has disappeared for msg_type=%u",
				 __func__, msg->msg_type);
		} else if (rc < 0) {
			slurm_addr_t peer_addr;
			if (!slurm_get_peer_addr(msg->conn->fd, &peer_addr)) {
				error("slurm_persist_send_msg: address:port=%pA msg_type=%u: %m",
				      &peer_addr, msg->msg_type);
			} else
				error("slurm_persist_send_msg: msg_type=%u: %m",
				      msg->msg_type);
		}

		return rc;
	}

	if (!msg->restrict_uid_set)
		fatal("%s: restrict_uid is not set", __func__);
	/*
	 * Pack message into buffer
	 */
	buffers.body = init_buf(BUF_SIZE);
	pack_msg(msg, buffers.body);
	log_flag_hex(NET_RAW, get_buf_data(buffers.body),
		     get_buf_offset(buffers.body),
		     "%s: packed body", __func__);

	/*
	 * Initialize header with Auth credential and message type.
	 * We get the credential now rather than later so the work can
	 * can be done in parallel with waiting for message to forward,
	 * but we may need to generate the credential again later if we
	 * wait too long for the incoming message.
	 */
	h_len = _compute_hash(buffers.body, msg, &hash);
	if (h_len < 0) {
		error("%s: hash_g_compute: %s has error",
		      __func__, rpc_num2string(msg->msg_type));
		free_buf(buffers.body);
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}
	log_flag_hex(NET_RAW, &hash, sizeof(hash),
		     "%s: hash:", __func__);
	if (msg->flags & SLURM_GLOBAL_AUTH_KEY) {
		auth_cred = auth_g_create(msg->auth_index, _global_auth_key(),
					  msg->restrict_uid, &hash, h_len);
	} else {
		auth_cred = auth_g_create(msg->auth_index, slurm_conf.authinfo,
					  msg->restrict_uid, &hash, h_len);
	}

	if (msg->forward.init != FORWARD_INIT) {
		forward_init(&msg->forward);
		msg->ret_list = NULL;
	}

	if (!msg->forward.tree_width)
		msg->forward.tree_width = slurm_conf.tree_width;

	forward_wait(msg);

	if (difftime(time(NULL), start_time) >= 60) {
		(void) auth_g_destroy(auth_cred);
		if (msg->flags & SLURM_GLOBAL_AUTH_KEY) {
			auth_cred = auth_g_create(msg->auth_index,
						  _global_auth_key(),
						  msg->restrict_uid, &hash,
						  h_len);
		} else {
			auth_cred = auth_g_create(msg->auth_index,
						  slurm_conf.authinfo,
						  msg->restrict_uid, &hash,
						  h_len);
		}
	}
	if (auth_cred == NULL) {
		error("%s: auth_g_create: %s has authentication error",
		      __func__, rpc_num2string(msg->msg_type));
		free_buf(buffers.body);
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	init_header(&header, msg, msg->flags);

	/*
	 * Pack auth credential
	 */
	buffers.auth = init_buf(BUF_SIZE);

	rc = auth_g_pack(auth_cred, buffers.auth, header.version);
	if (rc) {
		error("%s: auth_g_pack: %s has  authentication error: %m",
		      __func__, rpc_num2string(header.msg_type));
		(void) auth_g_destroy(auth_cred);
		free_buf(buffers.auth);
		free_buf(buffers.body);
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}
	(void) auth_g_destroy(auth_cred);
	log_flag_hex(NET_RAW, get_buf_data(buffers.auth),
		     get_buf_offset(buffers.auth),
		     "%s: packed auth_cred", __func__);

	/*
	 * Pack header into buffer for transmission
	 */
	update_header(&header, get_buf_offset(buffers.body));
	buffers.header = init_buf(BUF_SIZE);
	pack_header(&header, buffers.header);
	log_flag_hex(NET_RAW, get_buf_data(buffers.header),
		     get_buf_offset(buffers.header),
		     "%s: packed header", __func__);

	/*
	 * Send message
	 */
	rc = slurm_bufs_sendto(fd, buffers);

	if ((rc < 0) && (errno == ENOTCONN)) {
		log_flag(NET, "%s: peer has disappeared for msg_type=%u",
			 __func__, msg->msg_type);
	} else if (rc < 0) {
		slurm_addr_t peer_addr;
		if (!slurm_get_peer_addr(fd, &peer_addr)) {
			error("slurm_msg_sendto: address:port=%pA msg_type=%u: %m",
			      &peer_addr, msg->msg_type);
		} else if (errno == ENOTCONN) {
			log_flag(NET, "%s: peer has disappeared for msg_type=%u",
				 __func__, msg->msg_type);
		} else
			error("slurm_msg_sendto: msg_type=%u: %m",
			      msg->msg_type);
	}

	free_buf(buffers.header);
	free_buf(buffers.auth);
	free_buf(buffers.body);
	return rc;
}

/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t slurm_write_stream(int open_fd, char *buffer, size_t size)
{
	return slurm_send_timeout(open_fd, buffer, size,
	                          SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
	                          (slurm_conf.msg_timeout * 1000));
}

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd	- file descriptor to read from
 * OUT buffer   - buffer to receive into
 * IN size	- size of buffer
 * IN timeout	- how long to wait in milliseconds
 * RET size_t	- bytes read , or -1 on errror
 */
size_t slurm_read_stream(int open_fd, char *buffer, size_t size)
{
	return slurm_recv_timeout(open_fd, buffer, size,
	                          SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
	                          (slurm_conf.msg_timeout * 1000));
}

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_get_ip_str
 * given a slurm_address it returns its ip address as a string
 * IN slurm_address	- slurm_addr_t to be queried
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
void slurm_get_ip_str(slurm_addr_t *addr, char *ip, unsigned int buf_len)
{
	if (addr->ss_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *) addr;
		inet_ntop(AF_INET6, &sin->sin6_addr, ip, buf_len);
	} else {
		struct sockaddr_in *sin = (struct sockaddr_in *) addr;
		inet_ntop(AF_INET, &sin->sin_addr, ip, buf_len);
	}
}

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
int slurm_get_peer_addr(int fd, slurm_addr_t * slurm_address)
{
	slurm_addr_t name;
	socklen_t namelen = (socklen_t) sizeof(name);
	int rc;

	if ((rc = getpeername((int) fd, (struct sockaddr *) &name, &namelen)))
		return rc;
	memcpy(slurm_address, &name, sizeof(slurm_addr_t));
	return 0;
}

/**********************************************************************\
 * slurm_addr_t pack routines
\**********************************************************************/

/* slurm_pack_addr_array
 * packs an array of slurm_addrs into a buffer
 * OUT addr_array	- slurm_addr_t[] to pack
 * IN size_val  	- how many to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr_t from
 * returns		- Slurm error code
 */
extern void slurm_pack_addr_array(slurm_addr_t *addr_array, uint32_t size_val,
				  buf_t *buffer)
{
	pack32(size_val, buffer);

	for (int i = 0; i < size_val; i++)
		slurm_pack_addr(&addr_array[i], buffer);
}

/* slurm_unpack_addr_array
 * unpacks an array of slurm_addrs from a buffer
 * OUT addr_array_ptr	- slurm_addr_t[] to unpack to
 * IN/OUT size_val  	- how many to unpack
 * IN/OUT buffer	- buffer to upack the slurm_addr_t from
 * returns		- Slurm error code
 */
extern int slurm_unpack_addr_array(slurm_addr_t **addr_array_ptr,
				   uint32_t *size_val, buf_t *buffer)
{
	slurm_addr_t *addr_array = NULL;

	safe_unpack32(size_val, buffer);
	addr_array = xcalloc(*size_val, sizeof(slurm_addr_t));

	for (int i = 0; i < *size_val; i++) {
		if (slurm_unpack_addr_no_alloc(&addr_array[i], buffer))
			goto unpack_error;
	}

	*addr_array_ptr = addr_array;
	return SLURM_SUCCESS;

unpack_error:
	*size_val = 0;
	xfree(addr_array);
	return SLURM_ERROR;
}

static void _resp_msg_setup(slurm_msg_t *msg, slurm_msg_t *resp_msg,
			    uint16_t msg_type, void *data)
{
	slurm_msg_t_init(resp_msg);
	resp_msg->address = msg->address;
	resp_msg->auth_index = msg->auth_index;
	resp_msg->conn = msg->conn;
	resp_msg->data = data;
	resp_msg->flags = msg->flags;
	resp_msg->forward = msg->forward;
	resp_msg->forward_struct = msg->forward_struct;
	resp_msg->hash_index = msg->hash_index;
	resp_msg->msg_type = msg_type;
	resp_msg->protocol_version = msg->protocol_version;
	resp_msg->ret_list = msg->ret_list;
	resp_msg->orig_addr = msg->orig_addr;
	/*
	 * Extra sanity check. This should always be set. But if for some
	 * reason it isn't, restrict the decode to avoid leaking an
	 * unrestricted authentication token.
	 *
	 * Implicitly trust communications initiated by SlurmUser and
	 * SlurmdUser. In future releases this won't matter - there's
	 * no point packing an auth token on the reply as it isn't checked,
	 * but we're stuck doing that on older protocol versions for
	 * backwards-compatibility.
	 */
	if (!msg->auth_uid_set)
		slurm_msg_set_r_uid(resp_msg, SLURM_AUTH_NOBODY);
	else if ((msg->auth_uid != slurm_conf.slurm_user_id) &&
		 (msg->auth_uid != slurm_conf.slurmd_user_id))
		slurm_msg_set_r_uid(resp_msg, msg->auth_uid);
	else
		slurm_msg_set_r_uid(resp_msg, SLURM_AUTH_UID_ANY);
}

static void _rc_msg_setup(slurm_msg_t *msg, slurm_msg_t *resp_msg,
			  return_code_msg_t *rc_msg, int rc)
{
	memset(rc_msg, 0, sizeof(return_code_msg_t));
	rc_msg->return_code = rc;

	_resp_msg_setup(msg, resp_msg, RESPONSE_SLURM_RC, rc_msg);
}


/**********************************************************************\
 * simplified communication routines
 * They open a connection do work then close the connection all within
 * the function
\**********************************************************************/

/* slurm_send_msg
 * given the original request message this function sends a
 *	arbitrary message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN msg_type          - message type being returned
 * IN resp_msg		- the message being returned to the client
 */
int slurm_send_msg(slurm_msg_t *msg, uint16_t msg_type, void *resp)
{
	slurm_msg_t resp_msg;

	if (msg->conn_fd < 0) {
		slurm_seterrno(ENOTCONN);
		return SLURM_ERROR;
	}
	_resp_msg_setup(msg, &resp_msg, msg_type, resp);

	/* send message */
	return slurm_send_node_msg(msg->conn_fd, &resp_msg);
}

/* slurm_send_rc_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 */
int slurm_send_rc_msg(slurm_msg_t *msg, int rc)
{
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;

	if (msg->conn_fd < 0) {
		slurm_seterrno(ENOTCONN);
		return SLURM_ERROR;
	}
	_rc_msg_setup(msg, &resp_msg, &rc_msg, rc);

	/* send message */
	return slurm_send_node_msg(msg->conn_fd, &resp_msg);
}

/* slurm_send_rc_err_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 * IN err_msg   	- message for user
 */
int slurm_send_rc_err_msg(slurm_msg_t *msg, int rc, char *err_msg)
{
	slurm_msg_t resp_msg;
	return_code2_msg_t rc_msg;

	if (msg->conn_fd < 0) {
		slurm_seterrno(ENOTCONN);
		return SLURM_ERROR;
	}
	rc_msg.return_code = rc;
	rc_msg.err_msg     = err_msg;

	_resp_msg_setup(msg, &resp_msg, RESPONSE_SLURM_RC_MSG, &rc_msg);

	/* send message */
	return slurm_send_node_msg(msg->conn_fd, &resp_msg);
}

/*
 * Sends back reroute_msg_t which directs the client to make the request to
 * another cluster.
 *
 * IN msg	  - msg to respond to.
 * IN cluster_rec - cluster to direct msg to.
 */
int slurm_send_reroute_msg(slurm_msg_t *msg, slurmdb_cluster_rec_t *cluster_rec)
{
	slurm_msg_t resp_msg;
	reroute_msg_t reroute_msg = {0};

	if (msg->conn_fd < 0) {
		slurm_seterrno(ENOTCONN);
		return SLURM_ERROR;
	}

	/* Don't free the cluster_rec, it's pointing to the actual object. */
	reroute_msg.working_cluster_rec = cluster_rec;

	_resp_msg_setup(msg, &resp_msg, RESPONSE_SLURM_REROUTE_MSG,
			&reroute_msg);

	/* send message */
	return slurm_send_node_msg(msg->conn_fd, &resp_msg);
}

/*
 * Send and recv a slurm request and response on the open slurm descriptor
 * Doesn't close the connection.
 * IN fd	- file descriptor to receive msg on
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT resp	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_send_recv_msg(int fd, slurm_msg_t *req,
			       slurm_msg_t *resp, int timeout)
{
	int rc = -1;
	slurm_msg_t_init(resp);

	/* If we are using a persistent connection make sure it is the one we
	 * actually want.  This should be the correct one already, but just make
	 * sure.
	 */
	if (req->conn) {
		fd = req->conn->fd;
		resp->conn = req->conn;
	}

	if (slurm_send_node_msg(fd, req) >= 0) {
		/* no need to adjust and timeouts here since we are not
		   forwarding or expecting anything other than 1 message
		   and the regular timeout will be altered in
		   slurm_receive_msg if it is 0 */
		rc = slurm_receive_msg(fd, resp, timeout);
	}

	return rc;
}

/*
 * Send and recv a slurm request and response on the open slurm descriptor
 * Closes the connection.
 * IN fd	- file descriptor to receive msg on
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT resp	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
static int
_send_and_recv_msg(int fd, slurm_msg_t *req,
		   slurm_msg_t *resp, int timeout)
{
	int rc = slurm_send_recv_msg(fd, req, resp, timeout);

	if (close(fd))
		error("%s: closing fd:%d error: %m",
		      __func__, fd);

	return rc;
}

/*
 * Send and recv a slurm request and response on the open slurm descriptor
 * with a list containing the responses of the children (if any) we
 * forwarded the message to. List containing type (ret_data_info_t).
 * IN fd	- file descriptor to receive msg on
 * IN req	- a slurm_msg struct to be sent by the function
 * IN timeout	- how long to wait in milliseconds
 * RET List	- List containing the responses of the children (if any) we
 *		  forwarded the message to. List containing type
 *		  (ret_data_info_t).
 */
static List
_send_and_recv_msgs(int fd, slurm_msg_t *req, int timeout)
{
	List ret_list = NULL;
	int steps = 0;

	if (!req->forward.timeout) {
		if (!timeout)
			timeout = slurm_conf.msg_timeout * 1000;
		req->forward.timeout = timeout;
	}
	if (slurm_send_node_msg(fd, req) >= 0) {
		if (req->forward.cnt > 0) {
			/* figure out where we are in the tree and set
			 * the timeout for to wait for our children
			 * correctly
			 * (timeout+message_timeout sec per step)
			 * to let the child timeout */
			if (message_timeout < 0)
				message_timeout =
					slurm_conf.msg_timeout * 1000;
			steps = req->forward.cnt + 1;
			if (!req->forward.tree_width)
				req->forward.tree_width =
					slurm_conf.tree_width;
			if (req->forward.tree_width)
				steps /= req->forward.tree_width;
			timeout = (message_timeout * steps);
			steps++;

			timeout += (req->forward.timeout*steps);
		}
		ret_list = slurm_receive_msgs(fd, steps, timeout);
	}

	(void) close(fd);

	return ret_list;
}

/*
 * slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message,
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET int 		- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_send_recv_controller_msg(slurm_msg_t * request_msg,
				slurm_msg_t * response_msg,
				slurmdb_cluster_rec_t *comm_cluster_rec)
{
	int fd = -1;
	int rc = 0;
	time_t start_time = time(NULL);
	int retry = 1;
	slurm_conf_t *conf;
	bool have_backup;
	uint16_t slurmctld_timeout;
	slurm_addr_t ctrl_addr;
	static bool use_backup = false;
	slurmdb_cluster_rec_t *save_comm_cluster_rec = comm_cluster_rec;

	/*
	 * Just in case the caller didn't initialize his slurm_msg_t, and
	 * since we KNOW that we are only sending to one node (the controller),
	 * we initialize some forwarding variables to disable forwarding.
	 */
	forward_init(&request_msg->forward);
	request_msg->ret_list = NULL;
	request_msg->forward_struct = NULL;
	slurm_msg_set_r_uid(request_msg, SLURM_AUTH_UID_ANY);

tryagain:
	retry = 1;
	if (comm_cluster_rec)
		request_msg->flags |= SLURM_GLOBAL_AUTH_KEY;

	if ((fd = slurm_open_controller_conn(&ctrl_addr, &use_backup,
					     comm_cluster_rec)) < 0) {
		rc = -1;
		goto cleanup;
	}

	conf = slurm_conf_lock();
	have_backup = conf->control_cnt > 1;
	slurmctld_timeout = conf->slurmctld_timeout;
	slurm_conf_unlock();

	while (retry) {
		/*
		 * If the backup controller is in the process of assuming
		 * control, we sleep and retry later
		 */
		retry = 0;
		rc = _send_and_recv_msg(fd, request_msg, response_msg, 0);
		if (response_msg->auth_cred)
			auth_g_destroy(response_msg->auth_cred);
		else
			rc = -1;

		if ((rc == 0) && (!comm_cluster_rec)
		    && (response_msg->msg_type == RESPONSE_SLURM_RC)
		    && ((((return_code_msg_t *) response_msg->data)->return_code
			 == ESLURM_IN_STANDBY_MODE) ||
		        (((return_code_msg_t *) response_msg->data)->return_code
			 == ESLURM_IN_STANDBY_USE_BACKUP))
		    && (have_backup)
		    && (difftime(time(NULL), start_time)
			< (slurmctld_timeout + (slurmctld_timeout / 2)))) {
			if (((return_code_msg_t *)
			     response_msg->data)->return_code
			     == ESLURM_IN_STANDBY_MODE) {
				log_flag(NET, "%s: Primary not responding, backup not in control. Sleeping and retry.",
					 __func__);
				sleep(slurmctld_timeout / 2);
				use_backup = false;
			} else {
				log_flag(NET, "%s: Primary was contacted, but says it is the backup in standby.  Trying the backup",
					 __func__);
				use_backup = true;
			}
			slurm_free_return_code_msg(response_msg->data);
			if ((fd = slurm_open_controller_conn(&ctrl_addr,
							     &use_backup,
							     comm_cluster_rec))
			    < 0) {
				rc = -1;
			} else {
				retry = 1;
			}
		}

		if (rc == -1)
			break;
	}

	if (!rc && (response_msg->msg_type == RESPONSE_SLURM_REROUTE_MSG)) {
		reroute_msg_t *rr_msg = (reroute_msg_t *)response_msg->data;

		/*
		 * Don't expect mutliple hops but in the case it does
		 * happen, free the previous rr cluster_rec.
		 */
		if (comm_cluster_rec &&
		    (comm_cluster_rec != save_comm_cluster_rec))
			slurmdb_destroy_cluster_rec(comm_cluster_rec);

		comm_cluster_rec = rr_msg->working_cluster_rec;
		slurmdb_setup_cluster_rec(comm_cluster_rec);
		rr_msg->working_cluster_rec = NULL;
		goto tryagain;
	}

	if (comm_cluster_rec != save_comm_cluster_rec)
		slurmdb_destroy_cluster_rec(comm_cluster_rec);

cleanup:
	if (rc != 0)
 		_remap_slurmctld_errno();

	return rc;
}

/* slurm_send_recv_node_msg
 * opens a connection to node, sends the node a message, listens
 * for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * IN timeout		- how long to wait in milliseconds
 * RET int		- returns 0 on success, -1 on failure and sets errno
 */
int slurm_send_recv_node_msg(slurm_msg_t *req, slurm_msg_t *resp, int timeout)
{
	int fd = -1;

	resp->auth_cred = NULL;
	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
			 __func__, &req->address);
		return -1;
	}

	return _send_and_recv_msg(fd, req, resp, timeout);

}

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * IN comm_cluster_rec	- Communication record (host/port/version)
 * RET int		- return code
 * NOTE: NOT INTENDED TO BE CROSS-CLUSTER
 */
extern int slurm_send_only_controller_msg(slurm_msg_t *req,
				slurmdb_cluster_rec_t *comm_cluster_rec)
{
	int      rc = SLURM_SUCCESS;
	int fd = -1;
	slurm_addr_t ctrl_addr;
	bool     use_backup = false;

	/*
	 *  Open connection to Slurm controller:
	 */
	if ((fd = slurm_open_controller_conn(&ctrl_addr, &use_backup,
					     comm_cluster_rec)) < 0) {
		rc = SLURM_ERROR;
		goto cleanup;
	}

	slurm_msg_set_r_uid(req, slurm_conf.slurm_user_id);

	if ((rc = slurm_send_node_msg(fd, req)) < 0) {
		rc = SLURM_ERROR;
	} else {
		log_flag(NET, "%s: sent %d", __func__, rc);
		rc = SLURM_SUCCESS;
	}

	(void) close(fd);

cleanup:
	if (rc != SLURM_SUCCESS)
		_remap_slurmctld_errno();
	return rc;
}

/*
 *  Open a connection to the "address" specified in the slurm msg `req'
 *   Then, immediately close the connection w/out waiting for a reply.
 *
 *   Returns SLURM_SUCCESS on success SLURM_ERROR (< 0) for failure.
 *
 * DO NOT USE THIS IN NEW CODE
 * Use slurm_send_recv_rc_msg_only_one() or something similar instead.
 *
 * By not waiting for a response message, the message to be transmitted
 * may never be received by the remote end. The remote TCP stack may
 * acknowledge the data while the application itself has not had a chance
 * to receive it. The only way to tell that the application has processed
 * a given packet is for it to send back a message across the socket itself.
 *
 * The receive side looks like: poll() && read(), close(). If the poll() times
 * out, the kernel may still ACK the data while the application has jumped to
 * closing the connection. The send side cannot then distinguish between the
 * close happening as a result of the timeout vs. as a normal message shutdown.
 *
 * This is only one example of the many races inherent in this approach.
 *
 * See "UNIX Network Programming" Volume 1 (Third Edition) Section 7.5 on
 * SO_LINGER for a description of the subtle hazards inherent in abusing
 * TCP as a unidirectional pipe.
 */
int slurm_send_only_node_msg(slurm_msg_t *req)
{
	int rc = SLURM_SUCCESS;
	int fd = -1;
	struct pollfd pfd;
	int value = -1;
	int pollrc;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
			 __func__, &req->address);
		return SLURM_ERROR;
	}

	if ((rc = slurm_send_node_msg(fd, req)) < 0) {
		rc = SLURM_ERROR;
	} else {
		log_flag(NET, "%s: sent %d", __func__, rc);
		rc = SLURM_SUCCESS;
	}

	/*
	 * Make sure message was received by remote, and that there isn't
	 * and outstanding write() or that the connection has been reset.
	 *
	 * The shutdown() call intentionally falls through to the next block,
	 * the poll() should hit POLLERR which gives the TICOUTQ count as an
	 * additional diagnostic element.
	 *
	 * The steps below may result in a false-positive on occassion, in
	 * which case the code path above may opt to retransmit an already
	 * received message. If this is a concern, you should not be using
	 * this function.
	 */
	if (shutdown(fd, SHUT_WR))
		log_flag(NET, "%s: shutdown call failed: %m", __func__);

again:
	pfd.fd = fd;
	pfd.events = POLLIN;
	pollrc = poll(&pfd, 1, (slurm_conf.msg_timeout * 1000));
	if (pollrc == -1) {
		if (errno == EINTR)
			goto again;
		log_flag(NET, "%s: poll error: %m", __func__);
		(void) close(fd);
		return SLURM_ERROR;
	}

	if (pollrc == 0) {
		if (ioctl(fd, TIOCOUTQ, &value))
			log_flag(NET, "%s: TIOCOUTQ ioctl failed",
				 __func__);
		log_flag(NET, "%s: poll timed out with %d outstanding: %m",
			 __func__, value);
		(void) close(fd);
		return SLURM_ERROR;
	}

	if (pfd.revents & POLLERR) {
		int value = -1;
		int rc;
		int err = SLURM_SUCCESS;

		if (ioctl(fd, TIOCOUTQ, &value))
			log_flag(NET, "%s: TIOCOUTQ ioctl failed",
				 __func__);
		if ((rc = fd_get_socket_error(fd, &err)))
			log_flag(NET, "%s fd_get_socket_error failed with %s",
				 __func__, slurm_strerror(rc));
		else
			log_flag(NET, "%s: poll error with %d outstanding: %s",
				 __func__, value, slurm_strerror(err));

		(void) close(fd);
		return SLURM_ERROR;
	}

	(void) close(fd);

	return rc;
}

/*
 * Open a connection to the "address" specified in the slurm msg `req'
 * Then, immediately close the connection w/out waiting for a reply.
 * Ignore any errors. This should only be used when you do not care if
 * the message is ever received.
 */
void slurm_send_msg_maybe(slurm_msg_t *req)
{
	int fd = -1;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
			 __func__, &req->address);
		return;
	}

	(void) slurm_send_node_msg(fd, req);

	(void) close(fd);
}

/*
 *  Send a message to the nodelist specificed using fanout
 *    Then return List containing type (ret_data_info_t).
 * IN nodelist	  - list of nodes to send to.
 * IN msg	  - a slurm_msg struct to be sent by the function
 * IN timeout	  - how long to wait in milliseconds
 * RET List	  - List containing the responses of the children
 *		    (if any) we forwarded the message to. List
 *		    containing type (ret_data_info_t).
 */
List slurm_send_recv_msgs(const char *nodelist, slurm_msg_t *msg, int timeout)
{
	List ret_list = NULL;
	hostlist_t hl = NULL;

	if (!nodelist || !strlen(nodelist)) {
		error("slurm_send_recv_msgs: no nodelist given");
		return NULL;
	}

	hl = hostlist_create(nodelist);
	if (!hl) {
		error("slurm_send_recv_msgs: problem creating hostlist");
		return NULL;
	}

	ret_list = start_msg_tree(hl, msg, timeout);
	hostlist_destroy(hl);

	return ret_list;
}

/*
 *  Send a message to msg->address
 *    Then return List containing type (ret_data_info_t).
 * IN msg	  - a slurm_msg struct to be sent by the function
 * IN timeout	  - how long to wait in milliseconds
 * RET List	  - List containing the responses of the children
 *		    (if any) we forwarded the message to. List
 *		    containing type (ret_types_t).
 */
List slurm_send_addr_recv_msgs(slurm_msg_t *msg, char *name, int timeout)
{
	static pthread_mutex_t conn_lock = PTHREAD_MUTEX_INITIALIZER;
	static uint16_t conn_timeout = NO_VAL16, tcp_timeout = 2;
	List ret_list = NULL;
	int fd = -1;
	ret_data_info_t *ret_data_info = NULL;
	ListIterator itr;
	int i;

	slurm_mutex_lock(&conn_lock);

	if (conn_timeout == NO_VAL16) {
		conn_timeout = MIN(slurm_conf.msg_timeout, 10);
		tcp_timeout = MAX(0, slurm_conf.tcp_timeout - 1);
	}
	slurm_mutex_unlock(&conn_lock);

	/* This connect retry logic permits Slurm hierarchical communications
	 * to better survive slurmd restarts */
	for (i = 0; i <= conn_timeout; i++) {
		fd = slurm_open_msg_conn(&msg->address);
		if ((fd >= 0) || (errno != ECONNREFUSED && errno != ETIMEDOUT))
			break;
		if (errno == ETIMEDOUT) {
			if (i == 0)
				log_flag(NET, "Timed out connecting to %pA, retrying...",
					 &msg->address);
			i += tcp_timeout;
		} else {
			if (i == 0)
				log_flag(NET, "Connection refused by %pA, retrying...",
					 &msg->address);
			sleep(1);
		}
	}
	if (fd < 0) {
		log_flag(NET, "Failed to connect to %pA, %m", &msg->address);
		mark_as_failed_forward(&ret_list, name,
				       SLURM_COMMUNICATIONS_CONNECTION_ERROR);
		errno = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
		return ret_list;
	}

	msg->ret_list = NULL;
	msg->forward_struct = NULL;
	if (!(ret_list = _send_and_recv_msgs(fd, msg, timeout))) {
		mark_as_failed_forward(&ret_list, name, errno);
		errno = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
		return ret_list;
	} else {
		itr = list_iterator_create(ret_list);
		while ((ret_data_info = list_next(itr)))
			if (!ret_data_info->node_name) {
				ret_data_info->node_name = xstrdup(name);
			}
		list_iterator_destroy(itr);
	}
	return ret_list;
}

/*
 *  Open a connection to the "address" specified in the slurm msg "req".
 *    Then read back an "rc" message returning the "return_code" specified
 *    in the response in the "rc" parameter.
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT rc	- return code from the sent message
 * IN timeout	- how long to wait in milliseconds
 * RET int either 0 for success or -1 for failure.
 */
int slurm_send_recv_rc_msg_only_one(slurm_msg_t *req, int *rc, int timeout)
{
	int fd = -1;
	int ret_c = 0;
	slurm_msg_t resp;

	slurm_msg_t_init(&resp);

	/* Just in case the caller didn't initialize his slurm_msg_t, and
	 * since we KNOW that we are only sending to one node,
	 * we initialize some forwarding variables to disable forwarding.
	 */
	forward_init(&req->forward);
	req->ret_list = NULL;
	req->forward_struct = NULL;

	if ((fd = slurm_open_msg_conn(&req->address)) < 0) {
		log_flag(NET, "%s: slurm_open_msg_conn(%pA): %m",
			 __func__, &req->address);
		return -1;
	}
	if (!_send_and_recv_msg(fd, req, &resp, timeout)) {
		if (resp.auth_cred)
			auth_g_destroy(resp.auth_cred);
		*rc = slurm_get_return_code(resp.msg_type, resp.data);
		slurm_free_msg_data(resp.msg_type, resp.data);
		ret_c = 0;
	} else
		ret_c = -1;
	return ret_c;
}

/*
 * Send message to controller and get return code.
 * Make use of slurm_send_recv_controller_msg(), which handles
 * support for backup controller and retry during transistion.
 * IN req - request to send
 * OUT rc - return code
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET - 0 on success, -1 on failure
 */
extern int slurm_send_recv_controller_rc_msg(slurm_msg_t *req, int *rc,
					slurmdb_cluster_rec_t *comm_cluster_rec)
{
	int ret_c;
	slurm_msg_t resp;

	if (!slurm_send_recv_controller_msg(req, &resp, comm_cluster_rec)) {
		*rc = slurm_get_return_code(resp.msg_type, resp.data);
		slurm_free_msg_data(resp.msg_type, resp.data);
		ret_c = 0;
	} else {
		ret_c = -1;
	}

	return ret_c;
}

/* this is used to set how many nodes are going to be on each branch
 * of the tree.
 * IN total       - total number of nodes to send to
 * IN tree_width  - how wide the tree should be on each hop
 * RET int *	  - int array tree_width in length each space
 *		    containing the number of nodes to send to each hop
 *		    on the span.
 */
extern int *set_span(int total,  uint16_t tree_width)
{
	int *span = NULL;
	int left = total;
	int i = 0;

	if (tree_width == 0)
		tree_width = slurm_conf.tree_width;

	span = xcalloc(tree_width, sizeof(int));
	//info("span count = %d", tree_width);
	if (total <= tree_width) {
		return span;
	}

	while (left > 0) {
		for (i = 0; i < tree_width; i++) {
			if ((tree_width-i) >= left) {
				if (span[i] == 0) {
					left = 0;
					break;
				} else {
					span[i] += left;
					left = 0;
					break;
				}
			} else if (left <= tree_width) {
				if (span[i] == 0)
					left--;

				span[i] += left;
				left = 0;
				break;
			}

			if (span[i] == 0)
				left--;

			span[i] += tree_width;
			left -= tree_width;
		}
	}

	return span;
}

/*
 * Free a slurm message's memebers but not the message itself
 */
extern void slurm_free_msg_members(slurm_msg_t *msg)
{
	if (msg) {
		if (msg->auth_cred)
			(void) auth_g_destroy(msg->auth_cred);
		free_buf(msg->buffer);
		slurm_free_msg_data(msg->msg_type, msg->data);
		FREE_NULL_LIST(msg->ret_list);
	}
}

/*
 * Free a slurm message
 */
extern void slurm_free_msg(slurm_msg_t *msg)
{
	if (msg) {
		slurm_free_msg_members(msg);
		xfree(msg);
	}
}

extern void slurm_msg_set_r_uid(slurm_msg_t *msg, uid_t r_uid)
{
	msg->restrict_uid = r_uid;
	msg->restrict_uid_set = true;
}

extern char *nodelist_nth_host(const char *nodelist, int inx)
{
	hostlist_t hl = hostlist_create(nodelist);
	char *name = hostlist_nth(hl, inx);
	hostlist_destroy(hl);
	return name;
}

extern int nodelist_find(const char *nodelist, const char *name)
{
	hostlist_t hl = hostlist_create(nodelist);
	int id = hostlist_find(hl, name);
	hostlist_destroy(hl);
	return id;
}

/*
 * Convert number from one unit to another.
 * By default, Will convert num to largest divisible unit.
 * Appends unit type suffix -- if applicable.
 *
 * IN num: number to convert.
 * OUT buf: buffer to copy converted number into.
 * IN buf_size: size of buffer.
 * IN orig_type: The original type of num.
 * IN spec_type: Type to convert num to. If specified, num will be converted up
 * or down to this unit type.
 * IN divisor: size of type
 * IN flags: flags to control whether to convert exactly or not at all.
 */
extern void convert_num_unit2(double num, char *buf, int buf_size,
			      int orig_type, int spec_type, int divisor,
			      uint32_t flags)
{
	char *unit = "\0KMGTP?";
	uint64_t i;

	if ((int64_t)num == 0) {
		snprintf(buf, buf_size, "0");
		return;
	}

	if (spec_type != NO_VAL) {
		/* spec_type overrides all flags */
		if (spec_type < orig_type) {
			while (spec_type < orig_type) {
				num *= divisor;
				orig_type--;
			}
		} else if (spec_type > orig_type) {
			while (spec_type > orig_type) {
				num /= divisor;
				orig_type++;
			}
		}
	} else if (flags & CONVERT_NUM_UNIT_RAW) {
		orig_type = UNIT_NONE;
	} else if (flags & CONVERT_NUM_UNIT_NO) {
		/* no op */
	} else if (flags & CONVERT_NUM_UNIT_EXACT) {
		/* convert until we would loose precision */
		/* half values  (e.g., 2.5G) are still considered precise */

		while (num >= divisor
		       && ((uint64_t)num % (divisor / 2) == 0)) {
			num /= divisor;
			orig_type++;
		}
	} else {
		/* aggressively convert values */
		while (num >= divisor) {
			num /= divisor;
			orig_type++;
		}
	}

	if (orig_type < UNIT_NONE || orig_type > UNIT_PETA)
		orig_type = UNIT_UNKNOWN;
	i = (uint64_t)num;
	/* Here we are checking to see if these numbers are the same,
	 * meaning the float has not floating point.  If we do have
	 * floating point print as a float.
	*/
	if ((double)i == num)
		snprintf(buf, buf_size, "%"PRIu64"%c", i, unit[orig_type]);
	else
		snprintf(buf, buf_size, "%.2f%c", num, unit[orig_type]);
}

extern void convert_num_unit(double num, char *buf, int buf_size,
			     int orig_type, int spec_type, uint32_t flags)
{
	convert_num_unit2(num, buf, buf_size, orig_type, spec_type, 1024,
			  flags);
}

extern int revert_num_unit(const char *buf)
{
	char *unit = "\0KMGTP\0";
	int i = 1, j = 0, number = 0;

	if (!buf)
		return -1;
	j = strlen(buf) - 1;
	while (unit[i]) {
		if (toupper((int)buf[j]) == unit[i])
			break;
		i++;
	}

	number = atoi(buf);
	if (unit[i])
		number *= (i*1024);

	return number;
}

extern int get_convert_unit_val(int base_unit, char convert_to)
{
	int conv_unit = 0, conv_value = 0;

	if ((conv_unit = get_unit_type(convert_to)) == SLURM_ERROR)
		return SLURM_ERROR;

	while (base_unit++ < conv_unit) {
		if (!conv_value)
			conv_value = 1024;
		else
			conv_value *= 1024;
	}

	return conv_value;
}

extern int get_unit_type(char unit)
{
	char *units = "\0KMGTP";
	char *tmp_char = NULL;

	if (unit == '\0') {
		error("Invalid unit type '%c'. Possible options are '%s'",
		      unit, units + 1);
		return SLURM_ERROR;
	}

	tmp_char = strchr(units + 1, toupper(unit));
	if (!tmp_char) {
		error("Invalid unit type '%c'. Possible options are '%s'",
		      unit, units + 1);
		return SLURM_ERROR;
	}
	return tmp_char - units;
}

/*
 * slurm_forward_data - forward arbitrary data to unix domain sockets on nodes
 * IN/OUT nodelist: Nodes to forward data to (if failure this list is changed to
 *                  reflect the failed nodes).
 * IN address: address of unix domain socket
 * IN len: length of data
 * IN data: real data
 * RET: error code
 */
extern int slurm_forward_data(
	char **nodelist, char *address, uint32_t len, const char *data)
{
	List ret_list = NULL;
	int temp_rc = 0, rc = 0;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t msg;
	forward_data_msg_t req;
	hostlist_t hl = NULL;
	bool redo_nodelist = false;
	slurm_msg_t_init(&msg);

	log_flag(NET, "%s: nodelist=%s, address=%s, len=%u",
		 __func__, *nodelist, address, len);
	req.address = address;
	req.len = len;
	req.data = (char *)data;

	slurm_msg_set_r_uid(&msg, SLURM_AUTH_UID_ANY);
	msg.msg_type = REQUEST_FORWARD_DATA;
	msg.data = &req;

	if ((ret_list = slurm_send_recv_msgs(*nodelist, &msg, 0))) {
		if (list_count(ret_list) > 1)
			redo_nodelist = true;

		while ((ret_data_info = list_pop(ret_list))) {
			temp_rc = slurm_get_return_code(ret_data_info->type,
							ret_data_info->data);
			if (temp_rc != SLURM_SUCCESS) {
				rc = temp_rc;
				if (redo_nodelist) {
					if (!hl)
						hl = hostlist_create(
							ret_data_info->
							node_name);
					else
						hostlist_push_host(
							hl, ret_data_info->
							node_name);
				}
			}
			destroy_data_info(ret_data_info);
		}
	} else {
		error("slurm_forward_data: no list was returned");
		rc = SLURM_ERROR;
	}

	if (hl) {
		xfree(*nodelist);
		hostlist_sort(hl);
		*nodelist = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);
	}

	FREE_NULL_LIST(ret_list);

	return rc;
}

extern void slurm_setup_addr(slurm_addr_t *sin, uint16_t port)
{
	static slurm_addr_t s_addr = { 0 };

	memset(sin, 0, sizeof(*sin));

	if (slurm_addr_is_unspec(&s_addr)) {
		/* On systems with multiple interfaces we might not
		 * want to get just any address.  This is the case on
		 * a Cray system with RSIP.
		 */
		char *var;

		if (running_in_slurmctld())
			var = "NoCtldInAddrAny";
		else
			var = "NoInAddrAny";

		if (xstrcasestr(slurm_conf.comm_params, var)) {
			char host[HOST_NAME_MAX];

			if (!gethostname(host, HOST_NAME_MAX)) {
				slurm_set_addr(&s_addr, port, host);
			} else
				fatal("%s: Can't get hostname or addr: %m",
				      __func__);
		} else {
			slurm_set_addr(&s_addr, port, NULL);
		}
	}

	memcpy(sin, &s_addr, sizeof(*sin));
	slurm_set_port(sin, port);
	log_flag(NET, "%s: update address to %pA", __func__, sin);
}

/*
 * bind() and then listen() to any port in a given range of ports
 *
 * IN: s - socket
 * IN: port - port number to attempt to bind
 * IN: local - only bind to localhost if true
 * OUT: true/false if port was bound successfully
 */
extern int sock_bind_listen_range(int s, uint16_t *range, bool local)
{
	uint32_t count;
	uint32_t min;
	uint32_t max;
	uint32_t port;
	uint32_t num;

	min = range[0];
	max = range[1];

	srand(getpid());
	num = max - min + 1;
	port = min + (random() % num);
	count = num;

	do {
		if ((_is_port_ok(s, port, local)) &&
		    (!listen(s, SLURM_DEFAULT_LISTEN_BACKLOG)))
			return port;

		if (port == max)
			port = min;
		else
			++port;
		--count;
	} while (count > 0);

	close(s);
	error("%s: all ports in range (%u, %u) exhausted, cannot establish listening port",
	      __func__, min, max);

	return -1;
}

/*
 * Check if we can bind() the socket s to port port.
 *
 * IN: s - socket
 * IN: port - port number to attempt to bind
 * IN: local - only bind to localhost if true
 * OUT: true/false if port was bound successfully
 */
static bool _is_port_ok(int s, uint16_t port, bool local)
{
	slurm_addr_t addr;
	slurm_setup_addr(&addr, port);

	if (!local) {
		debug3("%s: requesting non-local port", __func__);
	} else if (addr.ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *) &addr;
		sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else if (addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *) &addr;
		sin->sin6_addr = in6addr_loopback;
	} else {
		error("%s: protocol family %u unsupported",
		      __func__, addr.ss_family);
		return false;
	}

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		log_flag(NET, "%s: bind() failed on port:%d fd:%d: %m",
			 __func__, port, s);
		return false;
	}

	return true;
}

extern int slurm_hex_to_char(int v)
{
	if (v >= 0 && v < 10)
		return '0' + v;
	else if (v >= 10 && v < 16)
		return ('a' - 10) + v;
	else
		return -1;
}

extern int slurm_char_to_hex(int c)
{
	int cl;

	cl = tolower(c);
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (cl >= 'a' && cl <= 'f')
		return cl + (10 - 'a');
	else
		return -1;
}
