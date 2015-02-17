/*****************************************************************************\
 *  runjob_plugin.cc - This plug is used to convey to runjob the
 *                     desires of slurm based on the allocation that
 *                     has surrounded it.  If runjob was ran outside
 *                     of SLURM this plugin will terminate the job at
 *                     that moment.
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com> et. al.
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/slurm_protocol_defs.h"
#include <slurm/slurm.h>

}

#ifdef HAVE_BG_FILES

#include <bgsched/runjob/Plugin.h>
#include <bgsched/Dimension.h>
//#include "ProcessTree.h"

#include <boost/thread/mutex.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include <log4cxx/logger.h>

#include <iosfwd>

static log4cxx::LoggerPtr slurm_ibm_logger(
	log4cxx::Logger::getLogger("ibm.runjob.mux.slurm"));

#define LOG_TRACE_MSG(message_expr) \
 LOG4CXX_TRACE(slurm_ibm_logger, message_expr)

#define LOG_DEBUG_MSG(message_expr) \
 LOG4CXX_DEBUG(slurm_ibm_logger, message_expr)

#define LOG_INFO_MSG(message_expr) \
 LOG4CXX_INFO(slurm_ibm_logger, message_expr)

#define LOG_WARN_MSG(message_expr) \
 LOG4CXX_WARN(slurm_ibm_logger, message_expr)

#define LOG_ERROR_MSG(message_expr) \
 LOG4CXX_ERROR(slurm_ibm_logger, message_expr)

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

typedef struct {
	char *bg_block_id;
	pid_t pid;             /* The only way we can track things
				  since we don't have a jobid from
				  mmcs in the verify state.
			       */
	uint32_t job_id;
	uint32_t step_id;
	char *total_cnodes;
} runjob_job_t;

static List runjob_list = NULL;
static pthread_mutex_t runjob_list_lock = PTHREAD_MUTEX_INITIALIZER;

static void _destroy_runjob_job(void *object)
{
	runjob_job_t *runjob_job = (runjob_job_t *)object;
	if (runjob_job) {
		xfree(runjob_job->bg_block_id);
		xfree(runjob_job->total_cnodes);
		xfree(runjob_job);
	}
}

static void _send_failed_cnodes(uint32_t job_id, uint32_t step_id, uint16_t sig)
{
	int rc;
	int count = 0;
	int max_tries = 30;

	while ((rc = slurm_kill_job_step(job_id, step_id, sig))) {
		rc = slurm_get_errno();

		if ((count > max_tries)
		    || rc == ESLURM_ALREADY_DONE || rc == ESLURM_INVALID_JOB_ID)
			break;
		LOG_WARN_MSG("Trying to fail cnodes, message from slurmctld: "
			     << slurm_strerror(rc));
		sleep (5);
		count++;
	}
}


Plugin::Plugin() :
	bgsched::runjob::Plugin(),
	_mutex()
{
	assert(HIGHEST_DIMENSIONS >= Dimension::NodeDims);

	runjob_list = list_create(_destroy_runjob_job);

	LOG_INFO_MSG("Slurm runjob plugin loaded version "
		     << SLURM_VERSION_STRING);
}

Plugin::~Plugin()
{
	LOG_INFO_MSG("Slurm runjob plugin finished");
	slurm_mutex_lock(&runjob_list_lock);
	list_destroy(runjob_list);
	runjob_list = NULL;
	slurm_mutex_unlock(&runjob_list_lock);
}

void Plugin::execute(bgsched::runjob::Verify& verify)
{
	LOG_TRACE_MSG("Verify - Start");
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
	runjob_job_t *runjob_job = NULL;
	char tmp_char[16], *tmp_str = NULL;
	std::string message = "Unknown failure";
	uint16_t dim;

	geo[0] = NO_VAL;
	start_coords[0] = NO_VAL;
	runjob_job = (runjob_job_t *)xmalloc(sizeof(runjob_job_t));
	runjob_job->job_id = NO_VAL;
	runjob_job->step_id = NO_VAL;

	/* Get the job/step id's from the environment and then go
	 * verify with the slurmctld where this step should be running.
	 */
	BOOST_FOREACH(const bgsched::runjob::Environment& env_var,
		      verify.envs()) {
		if (env_var.getKey() == "SLURM_JOB_ID") {
			runjob_job->job_id = atoi(env_var.getValue().c_str());
			found++;
		} else if (env_var.getKey() == "SLURM_STEP_ID") {
			runjob_job->step_id = atoi(env_var.getValue().c_str());
			found++;
		}

		if (found == looking_for)
			break;
	}

	if (found != looking_for) {
		message = "Couldn't find ENV VARS SLURM_JOB_ID and "
			"SLURM_STEP_ID.  Are you out of SLURM?  "
			"Use srun, not runjob.";
		goto deny_job;
	}

	LOG_TRACE_MSG("Getting info for step " << runjob_job->job_id
		      << "." << runjob_job->step_id);
	if (slurm_get_job_steps((time_t) 0, runjob_job->job_id,
				runjob_job->step_id,
				&step_resp, SHOW_ALL)) {
		message = "slurm_get_job_steps error";
		goto deny_job;
	}

	if (!step_resp->job_step_count) {
		message = "No steps match this id "
			+ boost::lexical_cast<std::string>(runjob_job->job_id)
			+ "."
			+ boost::lexical_cast<std::string>(runjob_job->step_id);
		goto deny_job;
	} else if (step_resp->job_step_count > 1) {
		uint32_t i;

		found = 0;
		for (i = 0, step_ptr = step_resp->job_steps;
		     i < step_resp->job_step_count; i++, step_ptr++) {
			if ((uint32_t)runjob_job->job_id == step_ptr->job_id) {
				found = 1;
				break;
			}
		}

		if (!found) {
			message = "Couldn't get job array task from response!";
			goto deny_job;
		}
	} else
		step_ptr = &step_resp->job_steps[0];

	if ((uint32_t)runjob_job->job_id != step_ptr->job_id) {
		message = "Step returned is for a different job "
			+ boost::lexical_cast<std::string>(step_ptr->job_id)
			+ "."
			+ boost::lexical_cast<std::string>(step_ptr->step_id)
			+ " != "
			+ boost::lexical_cast<std::string>(runjob_job->job_id)
			+ "."
			+ boost::lexical_cast<std::string>(runjob_job->step_id);
		goto deny_job;
	}

	/* A bit of verification to make sure this is the correct user
	   supposed to be running.
	*/
	if (verify.user().uid() != step_ptr->user_id) {
		message = "Jobstep "
			+ boost::lexical_cast<std::string>(runjob_job->job_id)
			+ "."
			+ boost::lexical_cast<std::string>(runjob_job->step_id)
			+ " should be ran by uid "
			+ boost::lexical_cast<std::string>(step_ptr->user_id)
			+ " but it is trying to be ran by "
			+ boost::lexical_cast<std::string>(verify.user().uid());
		goto deny_job;
	}

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_BLOCK_ID,
				     &runjob_job->bg_block_id)) {
		message = "Can't get the block id!";
		goto deny_job;
	}
	verify.block(runjob_job->bg_block_id);

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_IONODES,
				     &tmp_str)) {
		message = "Can't get the cnode string!";
		goto deny_job;
	}

	if (tmp_str) {
		runjob_job->total_cnodes =
			xstrdup_printf("%s[%s]", step_ptr->nodes, tmp_str);
		xfree(tmp_str);
	} else
		runjob_job->total_cnodes = xstrdup(step_ptr->nodes);

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_BLOCK_NODE_CNT,
				     &block_cnode_cnt)) {
		message = "Can't get the block node count!";
		goto deny_job;
	}

	if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
				     SELECT_JOBDATA_NODE_CNT,
				     &step_cnode_cnt)) {
		message = "Can't get the step node count!";
		goto deny_job;
	}

	if (!step_cnode_cnt || !block_cnode_cnt) {
		message = "We didn't get both the step cnode "
			"count and the block cnode cnt! step="
			+ boost::lexical_cast<std::string>(step_cnode_cnt)
			+ " block="
			+ boost::lexical_cast<std::string>(block_cnode_cnt);
		goto deny_job;
	} else if ((step_cnode_cnt < block_cnode_cnt)
		   && (step_cnode_cnt <= 512)) {
		uint16_t tmp_uint16[HIGHEST_DIMENSIONS];

		sub_block_job = 1;
		if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
					     SELECT_JOBDATA_GEOMETRY,
					     &tmp_uint16)) {
			message = "Can't figure out the geo "
				"given for sub-block job!";
			goto deny_job;
		}
		/* since geo is an unsigned (who really knows what
		   that is depending on the arch) we need to convert
		   our uint16_t to the unsigned array
		*/
		for (dim=0; dim<Dimension::NodeDims; dim++) {
			if (tmp_uint16[dim] == (uint16_t)NO_VAL)
				break;
			geo[dim] = tmp_uint16[dim];
		}
		/* Since IBM's stuff relies on a relative location we
		   have stored this information in the conn_type of
		   the select_jobinfo structure.  If you want the
		   absolute location use the SELECT_JOBDATA_START_LOC
		   variable.
		*/
		if (slurm_get_select_jobinfo(step_ptr->select_jobinfo,
					     SELECT_JOBDATA_CONN_TYPE,
					     &tmp_uint16)) {
			message = "Can't figure out the start loc "
				"for sub-block job!";
			goto deny_job;
		}
		for (dim=0; dim<Dimension::NodeDims; dim++) {
			if (tmp_uint16[dim] == (uint16_t)NO_VAL)
				break;
			start_coords[dim] = tmp_uint16[dim];
		}
	}

	if (sub_block_job && start_coords[0] != NO_VAL)
		verify.corner(bgsched::runjob::Corner(start_coords));
	else if (sub_block_job) {
		message = "No corner given for sub-block job!";
		goto deny_job;
	}

	if (sub_block_job && geo[0] != NO_VAL)
		verify.shape(bgsched::runjob::Shape(geo));
	else if (sub_block_job) {
		message = "No shape given for sub-block job!";
		goto deny_job;
	}

	if (verify.block().empty() || (verify.block().length() < 3)) {
		message = "YOU ARE OUTSIDE OF SLURM!!!!";
		goto deny_job;
	}

	if (sub_block_job) {
		char corner_str[Dimension::NodeDims];
		for (dim=0; dim<Dimension::NodeDims; dim++)
			corner_str[dim] = alpha_num[start_coords[dim]];
		LOG_DEBUG_MSG(runjob_job->job_id << "." << runjob_job->step_id
			      << " " << runjob_job->total_cnodes
			      << " relative " << corner_str);
	}

	/* set the scheduler_data to be the job id so we can filter on
	   it when we go to clean up the job in the slurmctld.
	*/
	snprintf(tmp_char, sizeof(tmp_char), "%u", runjob_job->job_id);
	verify.scheduler_data(tmp_char);

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

	runjob_job->pid = verify.pid();

	slurm_mutex_lock(&runjob_list_lock);
	if (runjob_list)
		list_append(runjob_list, runjob_job);
	slurm_mutex_unlock(&runjob_list_lock);

	slurm_free_job_step_info_response_msg(step_resp);
	LOG_TRACE_MSG("Verify - Done");
	return;

deny_job:
	_destroy_runjob_job(runjob_job);
	slurm_free_job_step_info_response_msg(step_resp);
	verify.deny_job(message);
	return;
}

void Plugin::execute(const bgsched::runjob::Started& data)
{
	LOG_TRACE_MSG("Started start");
	boost::lock_guard<boost::mutex> lock( _mutex );
	// ListIterator itr = NULL;
	// runjob_job_t *runjob_job = NULL;

	// slurm_mutex_lock(&runjob_list_lock);
	// if (runjob_list) {
	// 	itr = list_iterator_create(runjob_list);
	// 	while ((runjob_job = (runjob_job_t *)list_next(itr))) {
	// 		if (runjob_job->pid == data.pid()) {
	// 			std::cout << "Slurm step " << runjob_job->job_id
	// 				  << "." << runjob_job->step_id
	// 				  << " is IBM ID " << data.job()
	// 				  << std::endl;
	// 			break;
	// 		}
	// 	}
	// 	list_iterator_destroy(itr);
	// }
	// slurm_mutex_unlock(&runjob_list_lock);
	LOG_TRACE_MSG("Started - Done");
}

void Plugin::execute(const bgsched::runjob::Terminated& data)
{
	LOG_TRACE_MSG("Terminated - Start");
	ListIterator itr = NULL;
	runjob_job_t *runjob_job = NULL;
	uint16_t sig = 0;

	boost::lock_guard<boost::mutex> lock( _mutex );

	slurm_mutex_lock(&runjob_list_lock);
	if (runjob_list) {
		itr = list_iterator_create(runjob_list);
		while ((runjob_job = (runjob_job_t *)list_next(itr))) {
			if (runjob_job->pid == data.pid()) {
				// std::cout << "Slurm step " << runjob_job->job_id
				// 	  << "." << runjob_job->step_id
				// 	  << ", IBM ID " << data.job()
				// 	  << " finished with status "
				// 	  << data.status() << std::endl;
				list_remove(itr);
				break;
			}
		}
		list_iterator_destroy(itr);
	}
	slurm_mutex_unlock(&runjob_list_lock);

	if (!runjob_job) {
		if (runjob_list)
			LOG_ERROR_MSG("Couldn't find job running with pid, "
				      << data.pid() << " ID " << data.job());

	} else if (data.kill_timeout()) {
		LOG_ERROR_MSG(runjob_job->job_id << "." << runjob_job->step_id
			      << " had a kill_timeout()");
		/* In an older driver this wasn't always caught, so
		   send it.
		*/
		sig = SIG_NODE_FAIL;
	} else if (!data.message().empty()) {
		LOG_ERROR_MSG(runjob_job->job_id << "." << runjob_job->step_id
			      << " had a message of '" << data.message()
			      << "'. ("
			      << runjob_job->total_cnodes << ")");
	} // else if (data.status() == 9)
	  // 	sig = SIGKILL;

	if (sig)
		_send_failed_cnodes(
			runjob_job->job_id, runjob_job->step_id, sig);

	_destroy_runjob_job(runjob_job);
	LOG_TRACE_MSG("Terminated - Done");
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
