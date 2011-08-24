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

extern "C" {
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include "src/common/xmalloc.h"
#include <slurm/slurm.h>

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

Plugin::Plugin() :
	bgsched::runjob::Plugin(),
	_mutex()
{
	assert(HIGHEST_DIMENSIONS >= Dimension::NodeDims);

	std::cout << "Slurm runjob plugin loaded" << std::endl;
}

Plugin::~Plugin()
{
	std::cout << "Slurm runjob plugin finished" << std::endl;
}

void Plugin::execute(bgsched::runjob::Verify& verify)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	unsigned geo[Dimension::NodeDims];
	unsigned start_coords[Dimension::NodeDims];
	int found = 0;
	int looking_for = 2;
	int block_cnode_cnt = 0;
	int step_cnode_cnt = 0;
	bool sub_block_job = 0;
	job_step_info_response_msg_t * step_resp = NULL;
	job_step_info_t *step_ptr = NULL;
	uint32_t job_id = NO_VAL, step_id = NO_VAL;
	char *bg_block_id = NULL;

	geo[0] = NO_VAL;
	start_coords[0] = NO_VAL;

	/* Get the job/step id's from the environment and then go
	 * verify with the slurmctld where this step should be running.
	 */
	BOOST_FOREACH(const bgsched::runjob::Environment& env_var,
		      verify.envs()) {
		if (env_var.getKey() == "SLURM_JOB_ID") {
			job_id = atoi(env_var.getValue().c_str());
			found++;
		} else if (env_var.getKey() == "SLURM_STEP_ID") {
			step_id = atoi(env_var.getValue().c_str());
			found++;
		}

		if (found == looking_for)
			break;
	}

	if (found != looking_for)
		goto deny_job;

	if (slurm_get_job_steps((time_t) 0, job_id, step_id,
				&step_resp, SHOW_ALL)) {
		slurm_perror((char *)"slurm_get_job_steps error");
		goto deny_job;
	}

	if (!step_resp->job_step_count) {
		std::cerr << "No steps match this id "
			  << job_id << "." << step_id << std::endl;
		goto deny_job;
	}

	step_ptr = &step_resp->job_steps[0];

	/* A bit of verification to make sure this is the correct user
	   supposed to be running.
	*/
	if (verify.user().uid() != step_ptr->user_id) {
		std::cerr << "Jobstep " << job_id << "." << step_id
			  << " should be ran by uid " << step_ptr->user_id
			  << " but it is trying to be ran by "
			  << verify.user().uid() << std::endl;
		goto deny_job;
	}

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_BLOCK_ID,
				     &bg_block_id)) {
		std::cerr << "Can't get the block id!" << std::endl;
		goto deny_job;
	}
	verify.block(bg_block_id);
	xfree(bg_block_id);

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_BLOCK_NODE_CNT,
				     &block_cnode_cnt)) {
		std::cerr << "Can't get the block node count!" << std::endl;
		goto deny_job;
	}

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_NODE_CNT,
				     &step_cnode_cnt)) {
		std::cerr << "Can't get the step node count!" << std::endl;
		goto deny_job;
	}

	if (!step_cnode_cnt || !block_cnode_cnt) {
		std::cerr << "We didn't get both the step cnode "
			  << "count and the block cnode cnt! step="
			  << step_cnode_cnt << " block="
			  << block_cnode_cnt << std::endl;
		goto deny_job;
	} else if (step_cnode_cnt < block_cnode_cnt) {
		uint16_t dim;
		uint16_t tmp_uint16[HIGHEST_DIMENSIONS];

		sub_block_job = 1;
		if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
					     SELECT_JOBDATA_GEOMETRY,
					     &tmp_uint16)) {
			std::cerr << "Can't figure out the geo "
				  << "given for sub-block job!" << std::endl;
			goto deny_job;
		}
		/* since geo is an unsigned (who really knows what
		   that is depending on the arch) we need to convert
		   our uint16_t to the unsigned array
		*/
		for (dim=0; dim<Dimension::NodeDims; dim++)
			geo[dim] = tmp_uint16[dim];

		/* Since IBM's stuff relies on a relative location we
		   have stored this information in the conn_type of
		   the select_jobinfo structure.  If you want the
		   absolute location use the SELECT_JOBDATA_START_LOC
		   variable.
		*/
		if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
					     SELECT_JOBDATA_CONN_TYPE,
					     &tmp_uint16)) {
			std::cerr << "Can't figure out the start loc "
				  << "for sub-block job!" << std::endl;
			goto deny_job;
		}
		for (dim=0; dim<Dimension::NodeDims; dim++)
			start_coords[dim] = tmp_uint16[dim];
	}

	if (sub_block_job && start_coords[0] != NO_VAL)
		verify.corner(bgsched::runjob::Corner(start_coords));
	else if (sub_block_job) {
		std::cerr << "No corner given for sub-block job!" << std::endl;
		goto deny_job;
	}

	if (sub_block_job && geo[0] != NO_VAL)
		verify.shape(bgsched::runjob::Shape(geo));
	else if (sub_block_job) {
		std::cerr << "No shape given for sub-block job!" << std::endl;
		goto deny_job;
	}

	if (verify.block().empty() || (verify.block().length() < 3)) {
		std::cerr << "YOU ARE OUTSIDE OF SLURM!!!!" << std::endl;
		goto deny_job;
	}

	// std::cout << "executable: " << verify.exe() << std::endl;
	// std::cout << "args      : ";
	// std::copy(verify.args().begin(), verify.args().end(),
	// 	  std::ostream_iterator<std::string>(std::cout, " "));
	// std::cout << std::endl;
	// std::cout << "envs      : ";
	// std::copy(verify.envs().begin(), verify.envs().end(),
	// 	  std::ostream_iterator<std::string>(std::cout, " "));
	// std::cout << std::endl;
	// std::cout << "block     : " << verify.block() << std::endl;
	// if (!verify.corner().location().empty()) {
	// 	std::cout << "corner:     " <<
	// 		verify.corner().location() << std::endl;
	// }
	// if (!verify.shape().value().empty()) {
	// 	std::cout << "shape:      " << verify.shape().value()
	// 		  << std::endl;
	// }

	// const ProcessTree tree( verify.pid() );
	// std::cout << tree << std::endl;

	slurm_free_job_step_info_response_msg(step_resp);
	return;

deny_job:
	slurm_free_job_step_info_response_msg(step_resp);
	verify.deny_job(bgsched::runjob::Verify::DenyJob::Yes);
	return;
}

void Plugin::execute(const bgsched::runjob::Started& data)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	// std::cout << "runjob " << data.pid()
	// 	  << " started with ID " << data.job() << std::endl;
}

void Plugin::execute(const bgsched::runjob::Terminated& data)
{
	boost::lock_guard<boost::mutex> lock( _mutex );
	// std::cout << "runjob " << data.pid() << " shadowing job "
	// 	  << data.job() << " finished with status "
	// 	  << data.status() << std::endl;

	// output failed nodes
	const bgsched::runjob::Terminated::Nodes& nodes =
		data.software_error_nodes();
	if (!nodes.empty()) {
		/* FIXME: We sould tell the slurmctld about this
		   instead of just printing it out.
		*/
		std::cerr << nodes.size() << " failed nodes" << std::endl;
		BOOST_FOREACH(const bgsched::runjob::Node& i, nodes) {
			std::cerr << i.location() << ": "
				  << i.coordinates() << std::endl;
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
