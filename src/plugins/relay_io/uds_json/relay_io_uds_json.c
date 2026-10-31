/*****************************************************************************\
 *  relay_io_uds_json.c - uds_json relay_io plugin.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/relay_io.h"
#include "src/interfaces/serializer.h"

#include "../common/relay_io_common.h"
#include "uds_writer.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "uds_json relay_io plugin";
const char plugin_type[] = "relay_io/uds_json";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* name of the socket the site's scraper listens on */
#define DEFAULT_SOCKET_NAME "relay-io.sock"

/*
 * The ring is the only buffer between a producer and the socket: the kernel
 * queues just net.unix.max_dgram_qlen datagrams, which is small and cannot be
 * raised from here. These are caps rather than allocations, so a ring only
 * costs memory while the far end is actually behind.
 */
#define DEFAULT_RING_MBYTES 2

/* Default max user log data to emit in one message */
#define DEFAULT_MAX_LOG_KBYTES 4

/* how long teardown waits for the queue to drain */
#define FLUSH_TIMEOUT ((timespec_t) { .tv_sec = 5 })

/* slurmd and each slurmstepd write their own records to the scraper */
static uds_writer_t *writer = NULL;

/* most task output one record may carry, for relay_io_p_log_limit() */
static size_t max_log_bytes = 0;

static char *_param_str(const char *key)
{
	char *arg = NULL, *val = NULL;

	xstrfmtcat(arg, "%s=", key);
	val = conf_get_opt_str(slurm_conf.relay_io_params, arg);
	xfree(arg);

	return val;
}

/* RET value of key, or def if unset or unparsable */
static uint64_t _param_num(const char *key, uint64_t def)
{
	char *val = _param_str(key);
	uint64_t num = def;

	if (val && (parse_uint64(val, &num) || !num)) {
		error("ignoring unusable RelayIOParameters %s=%s", key, val);
		num = def;
	}

	xfree(val);

	return num;
}

/* RET socket path the scraper reads from (must xfree()) */
static char *_socket_path(const char *spooldir)
{
	char *path = _param_str("socket");

	if (path)
		return path;

	return xstrdup_printf("%s/%s", spooldir, DEFAULT_SOCKET_NAME);
}

extern int init(void)
{
	int rc = SLURM_SUCCESS;

	/*
	 * slurmstepd does not initialize the serializer plugins, so load them
	 * here rather than assume something else already did. In slurmd this
	 * is a no-op, it loads them at startup. This plugin requires json
	 * serializer, but deliberately didn't use serializer_required() since
	 * that fatals if the required mime_type isn't available.
	 */
	if (!(rc = serializer_g_init())) {
		const char **mime_types = NULL;
		int i = 0;
		mime_types = get_mime_type_array(false);
		while (mime_types[i] && xstrcmp(mime_types[i], MIME_TYPE_JSON))
			i++;
		if (!mime_types[i])
			rc = SLURM_ERROR;
	}

	if (rc) {
		error("%s: no serializer for %s, not relaying: %s",
		      plugin_type, MIME_TYPE_JSON, slurm_strerror(rc));
	}

	return rc;
}

extern void fini(void)
{
	/* nothing to do here, relay_io_p_fini() has already run */
}

extern int relay_io_p_init(const char *spooldir)
{
	char *socket_path = NULL;
	size_t ring_bytes;
	int rc = SLURM_SUCCESS;

	if (!spooldir || !spooldir[0]) {
		error("no spool directory given");
		return SLURM_ERROR;
	}

	socket_path = _socket_path(spooldir);

	/*
	 * Every daemon on the node is a plain client of the scraper's socket
	 * and writes its own records, so the role only decides how much
	 * buffering to pay for. slurmd relays the node's job events for as
	 * long as it runs; a stepd relays one step and then exits.
	 */
	if (running_in_slurmd())
		ring_bytes =
			(_param_num("slurmd_ring_mbytes", DEFAULT_RING_MBYTES) *
			 (1024 * 1024));
	else
		ring_bytes =
			(_param_num("stepd_ring_mbytes", DEFAULT_RING_MBYTES) *
			 (1024 * 1024));
	max_log_bytes =
		(_param_num("max_log_kbytes", DEFAULT_MAX_LOG_KBYTES) * 1024);

	if (!(writer = uds_writer_create(socket_path, ring_bytes)))
		rc = SLURM_ERROR;
	else if (running_in_slurmd())
		info("%s: relaying job output to %s", plugin_type, socket_path);

	xfree(socket_path);

	return rc;
}

extern int relay_io_p_fini(void)
{
	uint64_t dropped = 0;

	/* after the writer has stopped, so late losses are in the total */
	if (writer)
		uds_writer_destroy(&writer, &dropped);

	/* final tally, to close out whatever was reported while running */
	if (dropped)
		info("dropped %"PRIu64" record%s in total, the relay did not drain fast enough",
		     dropped, ((dropped == 1) ? "" : "s"));

	return SLURM_SUCCESS;
}

extern int relay_io_p_record(const relay_io_record_t *record,
			     size_t *consumed_ptr)
{
	int rc = SLURM_SUCCESS;
	data_t *data;
	char *json = NULL;
	size_t len = 0;

	if (!writer)
		return SLURM_SUCCESS;

	data = data_new();

	relay_io_record_pack_data(record, data, max_log_bytes, consumed_ptr);
	rc = serialize_g_data_to_string(&json, &len, data, MIME_TYPE_JSON,
					SER_FLAGS_COMPACT);
	FREE_NULL_DATA(data);

	if (rc || !json) {
		xfree(json);
		rc = (rc ? rc : SLURM_ERROR);
		/*
		 * The caller moves on regardless, so this record is
		 * gone. Count it where every other loss is counted
		 * rather than dropping it silently.
		 */
		uds_writer_discard(writer);
		if (consumed_ptr)
			*consumed_ptr = record->body_len;
	} else {
		uds_writer_submit(writer, json, len);
	}

	return rc;
}

extern size_t relay_io_p_log_limit(void)
{
	return max_log_bytes;
}

extern int relay_io_p_flush(void)
{
	if (!writer)
		return SLURM_SUCCESS;

	return uds_writer_flush(writer, FLUSH_TIMEOUT);
}
