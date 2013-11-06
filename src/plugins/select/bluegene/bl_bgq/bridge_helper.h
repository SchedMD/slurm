/*****************************************************************************\
 *  bridge_helper.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _BRIDGE_HELPER_H_
#define _BRIDGE_HELPER_H_

extern "C" {
#include "../bg_enums.h"
#include "../bg_record_functions.h"
}

#ifdef HAVE_BG_FILES

#include <bgsched/DatabaseException.h>
#include <bgsched/InitializationException.h>
#include <bgsched/InputException.h>
#include <bgsched/InternalException.h>
#include <bgsched/RuntimeException.h>
#include <bgsched/Switch.h>
#include <bgsched/bgsched.h>
#include <bgsched/Block.h>
#include <bgsched/core/core.h>

#include <bgsched/realtime/ClientStateException.h>
#include <bgsched/realtime/ConnectionException.h>
#include <bgsched/realtime/InternalErrorException.h>
#include <bgsched/realtime/ConfigurationException.h>
#include <bgsched/realtime/FilterException.h>
#include <bgsched/realtime/ProtocolException.h>

#include <boost/foreach.hpp>

using namespace std;
using namespace bgsched;
using namespace bgsched::core;

/* core errors */
extern int bridge_handle_database_errors(
	const char *function, const uint32_t err);
extern int bridge_handle_init_errors(
	const char *function, const uint32_t err);
extern int bridge_handle_input_errors(const char *function, const uint32_t err,
				      bg_record_t *bg_record);
extern int bridge_handle_internal_errors(
	const char *function, const uint32_t err);
extern int bridge_handle_runtime_errors(const char *function,
					const uint32_t err,
					bg_record_t *bg_record);

/* realtime errors */
extern int bridge_handle_realtime_client_errors(const char *function,
						const uint32_t err);
extern int bridge_handle_realtime_configuration_errors(const char *function,
						       const uint32_t err);
extern int bridge_handle_realtime_connection_errors(const char *function,
						    const uint32_t err);
extern int bridge_handle_realtime_filter_errors(const char *function,
						const uint32_t err);
extern int bridge_handle_realtime_internal_errors(const char *function,
						  const uint32_t err);
extern int bridge_handle_realtime_protocol_errors(const char *function,
						  const uint32_t err);

/* traslate functions */
extern uint16_t bridge_translate_status(bgsched::Block::Status state_in);
#if defined HAVE_BG_GET_ACTION
extern uint16_t bridge_translate_action(
	bgsched::Block::Action::Value action_in);
#endif
extern uint16_t bridge_translate_switch_usage(bgsched::Switch::InUse usage_in);
extern const char *bridge_hardware_state_string(const int state);

/* helper functions */
extern Block::Ptrs bridge_get_blocks(BlockFilter filter);
extern Midplane::ConstPtr bridge_get_midplane(ComputeHardware::ConstPtr bgqsys,
					      ba_mp_t *ba_mp);
extern Node::ConstPtrs bridge_get_midplane_nodes(const std::string& loc);
extern NodeBoard::ConstPtr bridge_get_nodeboard(Midplane::ConstPtr mp_ptr,
						int nodeboard_num);
extern NodeBoard::ConstPtrs bridge_get_nodeboards(const std::string& mp_loc);
extern Switch::ConstPtr bridge_get_switch(Midplane::ConstPtr mp_ptr, int dim);
extern ComputeHardware::ConstPtr bridge_get_compute_hardware();

#endif

#endif
