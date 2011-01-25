/*****************************************************************************\
 *  real_time.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

extern "C" {
#include "bridge_linker.h"
#include "../block_allocator/block_allocator.h"
}

#if defined HAVE_BG_FILES && defined HAVE_BGQ

#include <bgsched/bgsched.h>
#include <bgsched/Block.h>
#include <bgsched/core/core.h>
#include <boost/foreach.hpp>
#include <bgsched/realtime/Client.h>
#include <bgsched/realtime/ClientConfiguration.h>
#include <bgsched/realtime/Filter.h>

#include <iostream>

using namespace std;
using namespace bgsched;
using namespace bgsched::core;

#endif

static bool real_time_inited = false;

#if defined HAVE_BG_FILES && defined HAVE_BGQ

static int _real_time_connect(bgsched::realtime::Client c)
{
	int rc = SLURM_ERROR;
	int count = 0;
	while (real_time_inited && (rc != SLURM_SUCCESS)) {
		try {
			c.connect();
			rc = SLURM_SUCCESS;
		} catch (...) {
			rc = SLURM_ERROR;
			error("couldn't connect to the real_time server, "
			      "trying for %d seconds.", count * 5);
			sleep(5);
		}
	}

	return rc;
}

static void *_real_time(void *no_data)
{
	const bgsched::realtime::ClientConfiguration client_configuration;
	FilterHolder filter_holder;

	bool failed = false;

   	info("Creating real-time client...");

	filter_holder.get().setBlocks(true);
 	filter_holder.get().setBlockDeleted(true);
	// filter_holder.get().setMidplanes(true);
 	// filter_holder.get().setNodeBoards(true);
 	// filter_holder.get().setSwitches(true);
 	// filter_holder.get().setCables(true);

 	bgsched::realtime::Client c(client_configuration);

	info("Connecting real-time client..." );

	_real_time_connect(c);

	while (real_time_inited && !failed) {
		info("Requesting updates on the real-time client...");

		c.requestUpdates(NULL);

		info("Receiving messages on the real-time client...");

		c.receiveMessages(NULL, NULL, &failed);

		if (real_time_inited && failed) {
			info("Disconnected from real-time events. "
			     "Will try to reconnect.");
			_real_time_connect(c);
			failed = false;
		}
	}

	c.disconnect();
}

#endif

extern int real_time_init(void)
{
	real_time_inited = true;

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	pthread_attr_t thread_attr;
	slurm_attr_init(&thread_attr);
	if (pthread_create(&real_time_thread, &thread_attr,
			   _real_time, NULL))
		fatal("pthread_create error %m");
#endif
	return SLURM_SUCCESS;
}

extern int real_time_fini(void)
{
	real_time_inited = false;
#if defined HAVE_BG_FILES && defined HAVE_BGQ
	pthread_join(real_time_thread, NULL);
#endif

	return SLURM_SUCCESS;
}
