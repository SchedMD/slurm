/*****************************************************************************\
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
#include <check.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/util-net.h"
#include "src/common/xmalloc.h"

static void _test_addr_to_string(const slurm_addr_t *addr,
				 const socklen_t addrlen, const char *dst)
{
	char *str = sockaddr_to_string(addr, addrlen);

	if (!dst) {
		ck_assert(str == NULL);
		goto cleanup;
	}

	ck_assert(str != NULL);
	ck_assert_str_eq(str, dst);

cleanup:
	xfree(str);
}

#define taddr(addr, dst) \
	_test_addr_to_string((slurm_addr_t *) &addr, sizeof(addr), dst)
#define tunix(unix_path, dst)                     \
	do {                                      \
		const struct sockaddr_un addr = { \
			.sun_family = AF_UNIX,    \
			.sun_path = unix_path,    \
		};                                \
		taddr(addr, dst);                 \
	} while (false)
#define tip4(address, port, dst)                           \
	do {                                               \
		const struct in_addr inaddr = { address }; \
		const uint16_t nport = htons(port);        \
		const struct sockaddr_in addr = {          \
			.sin_family = AF_INET,             \
			.sin_port = nport,                 \
			.sin_addr = inaddr,                \
		};                                         \
		taddr(addr, dst);                          \
	} while (false)
#define tip6(address, port, dst)                        \
	do {                                            \
		const struct in6_addr inaddr = address; \
		const uint16_t nport = htons(port);     \
		const struct sockaddr_in6 addr = {      \
			.sin6_family = AF_INET6,        \
			.sin6_port = nport,             \
			.sin6_addr = inaddr,            \
		};                                      \
		taddr(addr, dst);                       \
	} while (false)

START_TEST(test_dns)
{
	{
		slurm_addr_t addr = { 0 };
		taddr(addr, NULL);
	}
	{
		slurm_addr_t addr = {
			.ss_family = AF_UNSPEC,
		};
		taddr(addr, NULL);
	}

	getnameinfo_cache_purge();

	tunix({ 0 }, "unix:");
	tunix("\0abstract", "unix:@abstract");
	tunix("/tmp/unix", "unix:/tmp/unix");

	tip4(INADDR_LOOPBACK, 10, "127.0.0.1:10");
	tip4(INADDR_ANY, 20, "0.0.0.0:20");
	tip4(0x0, 23, "0.0.0.0:23");
	tip4(INADDR_BROADCAST, 30, "255.255.255.255:30");
#ifdef INADDR_DUMMY
	tip4(INADDR_DUMMY, 40, "192.0.0.8:40");
#endif /* INADDR_DUMMY */
	tip4(0xee10eea1, 44, "161.238.16.238:44");
	tip4(0xee10eea1, 0, "161.238.16.238");

	tip6({ { { 0 } } }, 0, "[::]");
	tip6({ { { 0 } } }, 50, "[::]:50");
	tip6(IN6ADDR_LOOPBACK_INIT, 60, "[::1]:60");
	tip6(IN6ADDR_ANY_INIT, 70, "[::]:70");
	tip6(in6addr_any, 80, "[::]:80");
#define IP6_10102E                                           \
	{                                                    \
		{{0x10,0x10,0,0,0,0,0,0,0,0,0,0,0,0,0,0x2e}} \
	}
	tip6(IP6_10102E, 90, "[1010::2e]:90");
	tip6(IP6_10102E, 0, "[1010::2e]");
}

END_TEST
#undef taddr
#undef tunix
#undef tip4
#undef tip6

extern Suite *suite_dns(void)
{
	Suite *s = suite_create("dns");
	TCase *tc_core = tcase_create("dns");

	tcase_add_test(tc_core, test_dns);

	suite_add_tcase(s, tc_core);
	return s;
}

extern int main(void)
{
	enum print_output po = CK_ENV;
	enum fork_status fs = CK_FORK_GETENV;
	SRunner *sr = NULL;
	int number_failed = 0;
	const char *debug_env = getenv("SLURM_DEBUG");
	const char *debug_flags_env = getenv("SLURM_DEBUG_FLAGS");
	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	if (debug_env)
		log_opts.stderr_level = log_string2num(debug_env);
	if (debug_flags_env)
		debug_str2flags(debug_flags_env, &slurm_conf.debug_flags);

	log_init("dns-test", log_opts, 0, NULL);

	if (log_opts.stderr_level >= LOG_LEVEL_DEBUG) {
		/* automatically be gdb friendly when debug logging */
		po = CK_VERBOSE;
		fs = CK_NOFORK;
	}

	sr = srunner_create(suite_dns());
	srunner_set_fork_status(sr, fs);
	srunner_run_all(sr, po);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	log_fini();

	getnameinfo_cache_purge();

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
