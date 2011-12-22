/*****************************************************************************\
 *  load_leveler.c - Provide an srun command line interface over LoadLeveler.
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utmp.h>

#include "src/common/fd.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/mpi.h"
#include "src/common/net.h"
#include "src/common/plugstack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/switch.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/srun/opt.h"

#ifdef USE_LOADLEVELER

extern char *build_poe_command(void)
{
	int i;
	char *cmd_line = NULL, *tmp_str;

	xstrcat(cmd_line, "poe");
	for (i = 0; i < opt.argc; i++)
		xstrfmtcat(cmd_line, " %s", opt.argv[i]);

	if (opt.network) {
		if (strstr(opt.network, "dedicated"))
			xstrfmtcat(cmd_line, " -adapter-use=dedicated");
		else if (strstr(opt.network, "shared"))
			xstrfmtcat(cmd_line, " -adapter-use=shared");
	}
	if (opt.cpu_bind_type) {
		if ((opt.cpu_bind_type & CPU_BIND_TO_THREADS) ||
		    (opt.cpu_bind_type & CPU_BIND_TO_CORES)) {
			xstrfmtcat(cmd_line, " -bindprocs=yes");
		}
	}
	if (opt.shared != (uint16_t) NO_VAL) {
		if (opt.shared)
			xstrfmtcat(cmd_line, " -cpu_use=unique");
		else
			xstrfmtcat(cmd_line, " -cpu_use=multiple");
	}
	if (opt.network) {
		if (strstr(opt.network, "ib"))
			xstrfmtcat(cmd_line, " -devtype=ip");
	}
	if (opt.network) {
		if (strstr(opt.network, "sn_all"))
			xstrfmtcat(cmd_line, " -euidevice=sn_all");
		else if (strstr(opt.network, "sn_single"))
			xstrfmtcat(cmd_line, " -euidevice=sn_single");
		else if ((tmp_str = strstr(opt.network, "eth"))) {
			char buf[5];
			strncpy(buf, tmp_str, 5);
			buf[4] = '\0';
			xstrfmtcat(cmd_line, " -euidevice=%s", buf);
		}
	}
	if (opt.network) {
		if (strstr(opt.network, "ib"))
			xstrfmtcat(cmd_line, " -euilib=ip");
		else if (strstr(opt.network, "us"))
			xstrfmtcat(cmd_line, " -euilib=us");
	}
	if (opt.nodelist) {
/* FIXME: Need to generate hostlist file on compute node,
 * presumably using environment variables to set up */
		char *fname = NULL, *host_name, *host_line;
		pid_t pid = getpid();
		hostlist_t hl;
		int fd, len, offset, wrote;
		hl = hostlist_create(opt.nodelist);
		if (!hl)
			fatal("Invalid nodelist: %s", opt.nodelist);
		xstrfmtcat(fname, "slurm_hostlist.%u", (uint32_t) pid);
		if ((fd = creat(fname, 0600)) < 0)
			fatal("creat(%s): %m", fname);
		while ((host_name = hostlist_shift(hl))) {
			host_line = NULL;
			xstrfmtcat(host_line, "%s\n", host_name);
			free(host_name);
			len = strlen(host_line) + 1;
			offset = 0;
			while (len > offset) {
				wrote = write(fd, host_line + offset,
					      len - offset);
				if (wrote < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					fatal("write(%s): %m", fname);
				}
				offset += wrote;
			}
			xfree(host_line);
		}
		hostlist_destroy(hl);
		info("wrote hostlist file at %s", fname);
		xfree(fname);
		close(fd);
	}
	if (opt.msg_timeout) {
/* FIXME: Set MP_TIMEOUT env var */
	}
	if (opt.immediate)
		xstrfmtcat(cmd_line, " -retry=0");
	if (_verbose) {
		int info_level = MIN((_verbose + 1), 6);
		xstrfmtcat(cmd_line, " -infolevel=%d", info_level);
	}
	if (opt.labelio)
		xstrfmtcat(cmd_line, " -labelio");
	if (opt.min_nodes != NO_VAL)
		xstrfmtcat(cmd_line, " -nodes=%u", opt.min_nodes);
	if (opt.ntasks)
		xstrfmtcat(cmd_line, " -procs=%u", opt.ntasks);
	if (opt.cpu_bind_type) {
		if (opt.cpu_bind_type & CPU_BIND_TO_THREADS)
			xstrfmtcat(cmd_line, " -task_affinity=cpu");
		else if (opt.cpu_bind_type & CPU_BIND_TO_CORES)
			xstrfmtcat(cmd_line, " -task_affinity=core");
		else if (opt.cpus_per_task) {
			xstrfmtcat(cmd_line, " -task_affinity=cpu:%d",
				   opt.cpus_per_task);
		}
	}
	if (opt.ntasks_per_node != NO_VAL) {
		xstrfmtcat(cmd_line, " -tasks_per_node=%u",
			   opt.ntasks_per_node);
	}
	if (opt.unbuffered) {
		xstrfmtcat(cmd_line, " -stderrmode unordered");
		xstrfmtcat(cmd_line, " -stdoutmode unordered");
	}

info("%s", cmd_line);
exit(0);
	return cmd_line;
}

#endif
