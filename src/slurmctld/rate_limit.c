/*****************************************************************************\
 *  rate_limit.c
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
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

#include <stdbool.h>

#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"

/*
 * last_update is scaled by refill_period, and is not the direct unix time
 */
typedef struct {
	time_t last_update;
	uint32_t tokens;
	uid_t uid;
} user_bucket_t;

static int table_size = 8192;
static user_bucket_t *user_buckets = NULL;

static bool rate_limit_enabled = false;
static pthread_mutex_t rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 30 tokens max, bucket refills 2 tokens per 1 second */
static int bucket_size = 30;
static int refill_rate = 2;
static int refill_period = 1;

extern void rate_limit_init(void)
{
	char *tmp_ptr;

	if (!xstrcasestr(slurm_conf.slurmctld_params, "rl_enable"))
		return;

	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "rl_table_size=")))
		table_size = atoi(tmp_ptr + 14);
	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "rl_bucket_size=")))
		bucket_size = atoi(tmp_ptr + 15);
	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "rl_refill_rate=")))
		refill_rate = atoi(tmp_ptr + 15);
	if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
				   "rl_refill_period=")))
		refill_period = atoi(tmp_ptr + 17);

	rate_limit_enabled = true;
	user_buckets = xcalloc(table_size, sizeof(user_bucket_t));

	info("RPC rate limiting enabled");
	debug("%s: rl_table_size=%d,rl_bucket_size=%d,rl_refill_rate=%d,rl_refill_period=%d",
	      __func__, table_size, bucket_size, refill_rate, refill_period);
}

extern void rate_limit_shutdown(void)
{
	xfree(user_buckets);
}

/*
 * Return true if the limit's been exceeded.
 * False otherwise.
 */
extern bool rate_limit_exceeded(slurm_msg_t *msg)
{
	bool exceeded = false;
	int start_position = 0, position = 0;

	if (!rate_limit_enabled)
		return false;

	/*
	 * Exempt SlurmUser / root. Subjecting internal cluster traffic to
	 * the rate limit would break things really quickly. :)
	 * (We're assuming SlurmdUser is root here.)
	 */
	if (validate_slurm_user(msg->auth_uid))
		return false;

	slurm_mutex_lock(&rate_limit_mutex);

	/*
	 * Scan for position. Note that uid 0 indicates an unused slot,
	 * since root is never subjected to the rate limit.
	 * Naively hash the uid into the table. If that's not a match, keep
	 * scanning for the next vacant spot. Wrap around to the front if
	 * necessary once we hit the end.
	 */
	start_position = position = msg->auth_uid % table_size;
	while ((user_buckets[position].uid) &&
	       (user_buckets[position].uid != msg->auth_uid)) {
		position++;
		if (position == table_size)
			position = 0;
		if (position == start_position) {
			position = table_size;
			break;
		}
	}

	if (position == table_size) {
		/*
		 * Avoid the temptation to resize the table... you'd need to
		 * rehash all the contents which would be annoying and slow.
		 */
		error("RPC Rate Limiting: ran out of user table space. User will not be limited.");
	} else if (!user_buckets[position].uid) {
		user_buckets[position].uid = msg->auth_uid;
		user_buckets[position].last_update = time(NULL) / refill_period;
		user_buckets[position].last_update /= refill_period;
		user_buckets[position].tokens = bucket_size - 1;
		debug3("%s: new entry for uid %u", __func__, msg->auth_uid);
	} else {
		time_t now = time(NULL) / refill_period;
		time_t delta = now - user_buckets[position].last_update;
		user_buckets[position].last_update = now;

		/* add tokens */
		if (delta) {
			user_buckets[position].tokens += (delta * refill_rate);
			user_buckets[position].tokens =
				MIN(user_buckets[position].tokens, bucket_size);
		}

		if (user_buckets[position].tokens)
			user_buckets[position].tokens--;
		else
			exceeded = true;

		debug3("%s: found uid %u at position %d remaining tokens %d%s",
		       __func__, msg->auth_uid, position,
		       user_buckets[position].tokens,
		       (exceeded ? " rate limit exceeded" : ""));
	}
	slurm_mutex_unlock(&rate_limit_mutex);

	return exceeded;
}
