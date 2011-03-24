/*****************************************************************************\
 *  runjob_plugin.cc - This plug is used to convey to runjob the
 *                     desires of slurm based on the allocation that
 *                     has surrounded it.  If runjob was ran outside
 *                     of SLURM this plugin will terminate the job at
 *                     that moment.
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <auble1@llnl.gov> et. al.
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
}

#ifdef HAVE_BG_FILES

#include <bgsched/runjob/Plugin.h>
//#include "ProcessTree.h"

#include <boost/thread/mutex.hpp>
#include <boost/foreach.hpp>

#include <iosfwd>

class Plugin : public bgsched::runjob::Plugin
{
public:
	Plugin();

	~Plugin();

	void execute(
		bgsched::runjob::Verify& data
		);

	void execute(
		const bgsched::runjob::Started& data
		);

	void execute(
		const bgsched::runjob::Terminated& data
		);

private:
	boost::mutex _mutex;
};

Plugin::Plugin() :
	bgsched::runjob::Plugin(),
	_mutex()
{
	std::cout << "Hello from sample runjob plugin ctor" << std::endl;
}

Plugin::~Plugin()
{
	std::cout << "Goodbye from sample runjob plugin dtor" << std::endl;
}

void Plugin::execute(bgsched::runjob::Verify& verify)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	char *bg_block_id = getenv("MPIRUN_PARTITION");

	if (!bg_block_id || (strlen(bg_block_id) < 3)) {
		printf("YOU ARE OUTSIDE OF SLURM!!!!\n");
		verify.denyJob(DenyJob::Yes);
		return;
	}

	verify.block(bg_block_id);

	std::cout << "starting job from pid " << verify.pid() << std::endl;
	std::cout << "executable: " << verify.exe() << std::endl;
	std::cout << "args      : ";
	std::copy(verify.args().begin(), verify.args().end(),
		  std::ostream_iterator<std::string>(std::cout, " "));
	std::cout << std::endl;
	std::cout << "envs      : ";
	std::copy(verify.envs().begin(), verify.envs().end(),
		  std::ostream_iterator<std::string>(std::cout, " "));
	std::cout << std::endl;
	std::cout << "block     : " << verify.block() << std::endl;
	if (!verify.corner().empty()) {
		std::cout << "corner:     " << verify.corner() << std::endl;
	}
	if (!verify.shape().empty()) {
		std::cout << "shape:      " << verify.shape() << std::endl;
	}

	// const ProcessTree tree( verify.pid() );
	// std::cout << tree << std::endl;

	return;
}

void Plugin::execute(const bgsched::runjob::Started& data)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	std::cout << "runjob " << data.pid()
		  << " started with ID " << data.job() << std::endl;
}

void Plugin::execute(const bgsched::runjob::Terminated& data)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	std::cout << "runjob " << data.pid() << " shadowing job "
		  << data.job() << " finished with status "
		  << data.status() << std::endl;

	// output failed nodes
	const bgsched::runjob::Terminated::Nodes& nodes = data.failed_nodes();
	if (!nodes.empty()) {
		std::cout << nodes.size() << " failed nodes" << std::endl;
		BOOST_FOREACH(const std::string& i, data.failed_nodes()) {
			std::cout << i << std::endl;
		}
	}
}

extern "C" bgsched::runjob::Plugin* create()
{
	return new Plugin();
}

extern "C" void destroy(bgsched::runjob::Plugin* p)
{
	delete p;
}

#endif
