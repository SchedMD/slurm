/*****************************************************************************\
 *  sluid.c - Slurm Lexicographically-sortable Unique ID
 *****************************************************************************
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

#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "src/common/macros.h"
#include "src/common/sluid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#ifdef __linux__
#define CLOCK_TYPE CLOCK_TAI
#else
#define CLOCK_TYPE CLOCK_REALTIME
#endif

static const char cb32map[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static pthread_mutex_t sluid_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t cluster_bits = 0;
static uint64_t last_ms = 0;
static uint64_t seq = 0;

extern void sluid_init(uint16_t cluster, time_t minimum)
{
	slurm_mutex_lock(&sluid_mutex);
	cluster_bits = (uint64_t) cluster << 52;
	last_ms = minimum * 1000;
	slurm_mutex_unlock(&sluid_mutex);
}

extern sluid_t generate_sluid(void)
{
	struct timespec ts;
	sluid_t sluid;
	uint64_t now_ms;

	if (clock_gettime(CLOCK_TYPE, &ts) < 0)
		fatal("clock_gettime(): %m");

	now_ms = ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);

	slurm_mutex_lock(&sluid_mutex);
	if (!cluster_bits)
		fatal("%s: cluster_bits unset", __func__);

	if (last_ms < now_ms) {
		last_ms = now_ms;
		seq = 0;
	} else {
		seq++;
		if (seq > 0x3ff) {
			last_ms++;
			seq = 0;
		}
		now_ms = last_ms;
	}
	sluid = seq;
	slurm_mutex_unlock(&sluid_mutex);

	sluid |= now_ms << 10;
	sluid |= cluster_bits;

	return sluid;
}

extern uint16_t generate_cluster_id(void)
{
	/* Cluster IDs must be between 2 and 4095. */
	static bool seeded = false;
	if (!seeded) {
		srandom(time(NULL) + getpid());
		seeded = true;
	}
	return (random() % (4096 - 2)) + 2;
}

extern char *sluid2str(const sluid_t sluid)
{
	char *str = xmalloc(15);
	str[0] = 's';

	for (int i = 0; i < 13; i++) {
		uint64_t shift = 5 * i;
		uint64_t mask = (uint64_t) 0x1f << shift;
		str[13 - i] = cb32map[(sluid & mask) >> shift];
	}

	return str;
}

extern sluid_t str2sluid(const char *string)
{
	sluid_t sluid = 0;

	if (strlen(string) != 14)
		return 0;

	if (string[0] != 's' && string[0] != 'S')
		return 0;

	string++;

	for (int i = 0; i < 13; i++) {
		char c = string[i];
		uint64_t shift = 5 * (12 - i);
		uint64_t value = 0;

		/* avoid toupper() and associated locale issues */
		if (c >= 'a')
			c -= 0x20;

		while (cb32map[value] && (cb32map[value] != c))
			value++;

		if (value > 31) {
			if (c == 'O')
				value = 0;
			else if (c == 'I' || c == 'L')
				value = 1;
			else
				return 0;
		}

		sluid |= value << shift;
	}

	return sluid;
}

static char *_sluid2uuid(const sluid_t sluid, uint32_t step_id, uint64_t padding)
{
	char *uuid = NULL;
	uint64_t unix_ts_ms = (sluid >> 10) & 0x3ffffffffff;
	uint64_t seq = sluid & 0xff;
	uint64_t cluster = sluid >> 56;
	uint64_t uuid_upper = (unix_ts_ms << 16) | 0x7000 | seq;
	uint64_t uuid_lower = 0x8000000000000000 | (cluster << 40) | (padding << 18) | step_id;

	xstrfmtcat(uuid, "%08"PRIx64"-%04"PRIx64"-%04"PRIx64"-%04"PRIx64"-%012"PRIx64,
		   (uuid_upper >> 32),
		   ((uuid_upper >> 16) & 0xffff),
		   (uuid_upper & 0xffff),
		   (uuid_lower >> 48),
		   (uuid_lower & 0xffffffffffff));

	return uuid;
}

extern char *sluid2uuid(const sluid_t sluid, uint64_t padding)
{
	return _sluid2uuid(sluid, padding, 0xffffffff);
}

extern char *stepid2uuid(const slurm_step_id_t step, const uint64_t padding)
{
	return _sluid2uuid(step.sluid, step.step_id, padding);
}
