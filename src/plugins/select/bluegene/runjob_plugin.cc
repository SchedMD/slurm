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
#include <bgsched/Dimension.h>
//#include "ProcessTree.h"

#include <boost/thread/mutex.hpp>
#include <boost/foreach.hpp>

#include <iosfwd>

using namespace bgsched;

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

static int _char2coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return ((coord - 'A') + 10);
	return -1;
}

static bool _set_coords(const std::string& var, int *coords)
{
	if (var.length() < Dimension::NodeDims) {
		std::cerr << "Coord variable '"<< var << "' has "
			  << var.length()
			  << " characters in it, but it needs to be at least "
			  << Dimension::NodeDims << std::endl;
		return 0;
	}

	for (uint32_t dim = 0; dim<Dimension::NodeDims; dim++) {
		if ((coords[dim] = _char2coord(var[dim]) == -1)) {
			std::cerr << "Coord in the " << dim <<
				" dimension is out of bounds with a value of "
				  << var[dim] << std::endl;
			return 0;
		}
		// std::cout << "Got " << dim << " = "
		// 	  << coords[dim] << std::endl;
	}

	return 1;
}


Plugin::Plugin() :
	bgsched::runjob::Plugin(),
	_mutex()
{
	std::cout << "Slurm runjob plugin loaded" << std::endl;
}

Plugin::~Plugin()
{
	std::cout << "Slurm runjob plugin finished" << std::endl;
}

void Plugin::execute(bgsched::runjob::Verify& verify)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	int geo[Dimension::NodeDims] = { -1 };
	int start_coords[Dimension::NodeDims] = { -1 };
	int found = 0;
	int looking_for = 3;
	int block_cnode_cnt = 0;
	int step_cnode_cnt = 0;
	bool sub_block_job = 0;

	/* This should probably be changed to read this from the
	   slurmctld for security reasons.  But for now this is what
	   we have to work with.
	*/
	BOOST_FOREACH(const bgsched::runjob::Environment& env_var,
		      verify.envs()) {
		if (env_var.getKey() == "MPIRUN_PARTITION") {
			verify.block(env_var.getValue());
			found++;
		} else if (env_var.getKey() == "SLURM_BLOCK_NUM_NODES") {
			block_cnode_cnt = atoi(env_var.getValue().c_str());
			found++;
		} else if (env_var.getKey() == "SLURM_STEP_NUM_NODES") {
			step_cnode_cnt = atoi(env_var.getValue().c_str());
			found++;
		} else if (env_var.getKey() == "SLURM_STEP_START_LOC") {
			if (!_set_coords(env_var.getValue(), start_coords))
				goto deny_job;
			found++;
		} else if (env_var.getKey() == "SLURM_STEP_GEO") {
			if (!_set_coords(env_var.getValue(), geo))
				goto deny_job;
			found++;
		}

		if (!step_cnode_cnt || !block_cnode_cnt)
			continue;

		if (step_cnode_cnt < block_cnode_cnt) {
			looking_for += 2;
			sub_block_job = 1;
		}

		if (found == looking_for)
			break;
	}

	if (start_coords[0] != -1)
		verify.corner(bgsched::runjob::Corner(
				      (unsigned *)start_coords));
	else if (sub_block_job) {
		std::cerr << "No corner given for sub-block job!" << std::endl;
		goto deny_job;
	}

	if (geo[0] != -1)
		verify.shape(bgsched::runjob::Shape((unsigned *)geo));
	else if (sub_block_job) {
		std::cerr << "No shape given for sub-block job!" << std::endl;
		goto deny_job;
	}

	if (verify.block().empty() || (verify.block().length() < 3)) {
		std::cerr << "YOU ARE OUTSIDE OF SLURM!!!!" << std::endl;
		goto deny_job;
	}

	std::cout << "executable: " << verify.exe() << std::endl;
	std::cout << "args      : ";
	std::copy(verify.args().begin(), verify.args().end(),
		  std::ostream_iterator<std::string>(std::cout, " "));
	std::cout << std::endl;
	// std::cout << "envs      : ";
	// std::copy(verify.envs().begin(), verify.envs().end(),
	// 	  std::ostream_iterator<std::string>(std::cout, " "));
	// std::cout << std::endl;
	std::cout << "block     : " << verify.block() << std::endl;
	if (!verify.corner().location().empty()) {
		std::cout << "corner:     " <<
			verify.corner().location() << std::endl;
	}
	if (!verify.shape().value().empty()) {
		std::cout << "shape:      " << verify.shape().value()
			  << std::endl;
	}

	// const ProcessTree tree( verify.pid() );
	// std::cout << tree << std::endl;

	return;

deny_job:
	verify.denyJob(bgsched::runjob::Verify::DenyJob::Yes);
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
