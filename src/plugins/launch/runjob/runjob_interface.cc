/*****************************************************************************\
 *  runjob_interface.cc
 *
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#include "runjob_interface.h"
}

#ifdef HAVE_BG_FILES

#include <bgsched/runjob/Client.h>

#include <iostream>

#include <sys/wait.h>
#include <unistd.h>

static bgsched::runjob::Client *rj_client_ptr = NULL;


extern int runjob_launch(int argc, char **argv,
			 int input, int output, int error)
{
	try {
		rj_client_ptr = new(bgsched::runjob::Client)(argc, argv);
		return rj_client_ptr->start(input, output, error);
	} catch (const std::exception& e) {
		std::cerr << "could not runjob: " << e.what() << std::endl;
		return -1;
	}
}

extern void runjob_signal(int signal)
{
	if (rj_client_ptr) {
		try {
			rj_client_ptr->kill(signal);
		}  catch (const std::exception& e) {
			std::cerr << "could send signal " << signal
				  << " to job: " << e.what() << std::endl;
		}
	}
}

#endif
