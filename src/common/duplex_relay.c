/*****************************************************************************\
 *  duplex_relay.c - Full-duplex relay mechanism using conmgr
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

#include "slurm/slurm_errno.h"

#include "src/common/duplex_relay.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_time.h"
#include "src/common/xmalloc.h"
#include "src/conmgr/conmgr.h"

#define MAGIC_DUPLEX_RELAY 0x4455504c

typedef struct {
	int magic; /* MAGIC_DUPLEX_RELAY */
	pthread_mutex_t mutex;
	conmgr_fd_ref_t *con1;
	conmgr_fd_ref_t *con2;
} duplex_relay_t;

static void _free(duplex_relay_t *drelay)
{
	xassert(drelay->magic == MAGIC_DUPLEX_RELAY);

	drelay->magic = ~MAGIC_DUPLEX_RELAY;
	conmgr_con_queue_close_free(&drelay->con1);
	conmgr_con_queue_close_free(&drelay->con2);
	slurm_mutex_destroy(&drelay->mutex);
	xfree(drelay);
}

static void _on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;
	duplex_relay_t *drelay = arg;
	bool free_drelay = false;

	slurm_mutex_lock(&drelay->mutex);

	xassert(drelay->magic == MAGIC_DUPLEX_RELAY);

	if (drelay->con1 && drelay->con2)
		log_flag(NET, "%s: [%s]<->[%s] closing relay",
			 __func__, conmgr_con_get_name(drelay->con1),
			 conmgr_con_get_name(drelay->con2));

	if (conmgr_con_is_equal(con, drelay->con1)) {
		CONMGR_CON_UNLINK(drelay->con1);
		conmgr_con_queue_close(drelay->con2);
	} else {
		xassert(conmgr_con_is_equal(con, drelay->con2));
		CONMGR_CON_UNLINK(drelay->con2);
		conmgr_con_queue_close(drelay->con1);
	}

	free_drelay = (!drelay->con1 && !drelay->con2);

	slurm_mutex_unlock(&drelay->mutex);

	if (free_drelay)
		_free(drelay);
}

static int _on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *src = conmgr_args.ref;
	conmgr_fd_ref_t *dst = NULL;
	duplex_relay_t *drelay = arg;
	int rc = EINVAL;
	buf_t *buffer = NULL;
	size_t bytes = 0;

	slurm_mutex_lock(&drelay->mutex);

	xassert(drelay->magic == MAGIC_DUPLEX_RELAY);

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) {
		conmgr_con_queue_close(drelay->con1);
		conmgr_con_queue_close(drelay->con2);

		log_flag(NET, "%s: [%s] cancelled",
				 __func__, conmgr_con_get_name(src));

		slurm_mutex_unlock(&drelay->mutex);
		return SLURM_SUCCESS;
	}

	if (conmgr_con_is_equal(src, drelay->con1)) {
		dst = drelay->con2;
	} else {
		xassert(conmgr_con_is_equal(src, drelay->con2));
		dst = drelay->con1;
	}

	if (!dst) {
		log_flag(NET, "%s: [%s] cancelled due to closing connection",
				 __func__, conmgr_con_get_name(src));
		slurm_mutex_unlock(&drelay->mutex);
		return SLURM_SUCCESS;
	}

	if ((rc = conmgr_con_xfer_in_buffer(src, &buffer))) {
		log_flag(NET, "%s: [%s]<->[%s] unable to get incoming data from [%s]",
			 __func__, conmgr_con_get_name(drelay->con1),
			 conmgr_con_get_name(drelay->con2),
			 conmgr_con_get_name(src));
	} else if (((bytes = get_buf_offset(buffer)) > 0) &&
		   (rc = conmgr_con_xfer_out_buffer(dst, buffer))) {
		log_flag(NET, "%s: [%s]<->[%s] unable to relay %zu bytes to [%s]",
			 __func__, conmgr_con_get_name(drelay->con1),
			 conmgr_con_get_name(drelay->con2), bytes,
			 conmgr_con_get_name(dst));
	} else {
		log_flag(NET, "%s: [%s]<->[%s] relayed %zu bytes to [%s]",
			 __func__, conmgr_con_get_name(drelay->con1),
			 conmgr_con_get_name(drelay->con2), bytes,
			 conmgr_con_get_name(dst));
		xassert(!get_buf_offset(buffer));
	}

	slurm_mutex_unlock(&drelay->mutex);

	FREE_NULL_BUFFER(buffer);
	return rc;
}

static int _assign(duplex_relay_t *drelay, conmgr_fd_ref_t *con1,
		   conmgr_fd_ref_t *con2)
{
	static const conmgr_events_t events = {
		.on_data = _on_data,
		.on_finish = _on_finish,
	};
	static conmgr_timeouts_t timeouts = {
		.read = TIMESPEC_INFINITE,
		.write = TIMESPEC_INFINITE,
		.connect = TIMESPEC_INFINITE,
		.quiesce = TIMESPEC_INFINITE,
		.write_complete = TIMESPEC_INFINITE,
	};
	int rc = EINVAL;

	if (timespec_is_infinite(timeouts.write) && slurm_conf.msg_timeout) {
		/* First run needs to set timeouts */
		timeouts.write = (timespec_t) {
			.tv_sec = slurm_conf.msg_timeout,
		};
		timeouts.connect = (timespec_t) {
			.tv_sec = slurm_conf.msg_timeout,
		};
		timeouts.quiesce = (timespec_t) {
			.tv_sec = (2 * slurm_conf.msg_timeout),
		};
		timeouts.write_complete = (timespec_t) {
			.tv_sec = slurm_conf.msg_timeout,
		};
	}

	/* Avoid connections doing anything while being reconfigured */
	if ((rc = conmgr_quiesce_con(con1)))
		return rc;
	if ((rc = conmgr_quiesce_con(con2)))
		return rc;

	if ((rc = conmgr_con_set_timeouts(con1, &timeouts, __func__)))
		return rc;
	if ((rc = conmgr_con_set_timeouts(con2, &timeouts, __func__)))
		return rc;

	if ((rc = conmgr_con_set_events(con1, &events, drelay, __func__)))
		fatal_abort("%s: [%s] Failed to set new events on connection",
			    __func__, conmgr_con_get_name(con1));
	if ((rc = conmgr_con_set_events(con2, &events, drelay, __func__)))
		fatal_abort("%s: [%s] Failed to set new events on connection",
			    __func__, conmgr_con_get_name(con2));

	if ((rc = conmgr_con_change_mode(con1, CON_TYPE_RAW)))
		fatal_abort("%s: [%s] Failed to set mode to RAW",
			    __func__, conmgr_con_get_name(con1));
	if ((rc = conmgr_con_change_mode(con2, CON_TYPE_RAW)))
		fatal_abort("%s: [%s] Failed to set mode to RAW",
			    __func__, conmgr_con_get_name(con2));

	if ((rc = conmgr_unquiesce_con(con1)))
		fatal_abort("%s: [%s] Failed unquiesce connection",
			    __func__, conmgr_con_get_name(con1));
	if ((rc = conmgr_unquiesce_con(con2)))
		fatal_abort("%s: [%s] Failed unquiesce connection",
			    __func__, conmgr_con_get_name(con2));

	log_flag(NET, "%s: [%s]<->[%s] duplex relay activated",
		 __func__, conmgr_con_get_name(con1),
		 conmgr_con_get_name(con2));

	return SLURM_SUCCESS;
}

extern int duplex_relay_assign(conmgr_fd_ref_t *con1, conmgr_fd_ref_t *con2)
{
	duplex_relay_t *drelay = NULL;
	int rc = EINVAL;

	if (!con1 || !con2)
		return EINVAL;

	drelay = xmalloc(sizeof(*drelay));
	*drelay = (duplex_relay_t) {
		.magic = MAGIC_DUPLEX_RELAY,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
	};

	CONMGR_CON_LINK(con1, drelay->con1);
	CONMGR_CON_LINK(con2, drelay->con2);

	if ((rc = _assign(drelay, con1, con2)))
		_free(drelay);

	return rc;
}
