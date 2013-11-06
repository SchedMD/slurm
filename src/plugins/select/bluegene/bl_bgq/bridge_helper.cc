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

#include "bridge_helper.h"

#ifdef HAVE_BG_FILES
extern int bridge_handle_database_errors(
	const char *function, const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::DatabaseErrors::DatabaseError:
		error("%s: Can't access to the database!", function);
		break;
	case bgsched::DatabaseErrors::OperationFailed:
		error("%s: Database option Failed!", function);
		break;
	case bgsched::DatabaseErrors::InvalidKey:
		error("%s: Database Invalid Key.", function);
		break;
	case bgsched::DatabaseErrors::DataNotFound:
		error("%s: Data not found error.", function);
		break;
	case bgsched::DatabaseErrors::DuplicateEntry:
		error("%s: We got a duplicate entry?", function);
		break;
	case bgsched::DatabaseErrors::XmlError:
		error("%s: XML Error?", function);
		break;
	case bgsched::DatabaseErrors::ConnectionError:
		error("%s: Can't connect to the database!", function);
		break;
	case bgsched::DatabaseErrors::UnexpectedError:
		error("%s: UnexpectedError returned from the database!",
		      function);
		break;
	default:
		error("%s: Unexpected Database exception value %d",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_init_errors(
	const char *function, const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::InitializationErrors::DatabaseInitializationFailed:
		error("%s: Database Init failed.", function);
		break;
	case bgsched::InitializationErrors::MalformedPropertiesFile:
		error("%s: Malformated Properties File.", function);
		break;
	case bgsched::InitializationErrors::PropertiesNotFound:
		error("%s: Can't locate Properties File.", function);
		break;
	default:
		error("%s: Unexpected Initialization exception value %d",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_input_errors(const char *function, const uint32_t err,
				      bg_record_t *bg_record)
{
	int rc = SLURM_ERROR;

	/* Not real errors */
	switch (err) {
	case bgsched::InputErrors::InvalidMidplaneCoordinates:
		error("%s: Invalid midplane coodinates given.", function);
		break;
	case bgsched::InputErrors::InvalidLocationString:
		error("%s: Invalid location given.", function);
		break;
	case bgsched::InputErrors::InvalidBlockSize:
		error("%s: Invalid Block Size.", function);
		break;
	case bgsched::InputErrors::InvalidBlockName:
		/* Not real error */
		rc = BG_ERROR_BLOCK_NOT_FOUND;
		error("%s: Bad block name %s!",
		      function, bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidBlockDescription:
		error("%s: Invalid Block Description (%s).", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidBlockOptions:
		error("%s: Invalid Block Options (%s).", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidBlockBootOptions:
		error("%s: Invalid Block boot options (%s).", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidBlockMicroLoaderImage:
		error("%s: Invalid Block microloader image (%s).", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidBlockNodeConfiguration:
		error("%s: Invalid Block Node Configuration (%s).", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidBlockInfo:
		error("%s: Invalid Block Info (%s).", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::InvalidNodeBoards:
		error("%s: Invalid Node Boards.", function);
		break;
	case bgsched::InputErrors::InvalidDimension:
		error("%s: Invalid Dimensions.", function);
		break;
	case bgsched::InputErrors::InvalidNodeBoardCount:
		error("%s: Invalid NodeBoard count.", function);
		break;
	case bgsched::InputErrors::InvalidNodeBoardPosition:
		error("%s: Invalid NodeBoard position.", function);
		break;
	case bgsched::InputErrors::InvalidMidplanes:
		error("%s: Invalid midplanes given.", function);
		break;
	case bgsched::InputErrors::InvalidPassthroughMidplanes:
		error("%s: Invalid passthrough midplanes given.", function);
		break;
	case bgsched::InputErrors::InvalidConnectivity:
		error("%s: Invalid connectivity given.", function);
		break;
	case bgsched::InputErrors::BlockNotFound:
		/* Not real error */
		rc = BG_ERROR_BLOCK_NOT_FOUND;
		debug2("%s: Unknown block %s!",
		       function, bg_record->bg_block_id);
		break;
	case bgsched::InputErrors::BlockNotAdded:
		error("%s: For some reason the block was not added.", function);
		break;
	case bgsched::InputErrors::BlockNotCreated:
		error("%s: can not create block from input arguments",
		      function);
		break;
	case bgsched::InputErrors::InvalidUser:
		error("%s: Invalid User given.", function);
		break;
	default:
		error("%s: Unexpected Input exception value %d",
		      function, err);
		rc = SLURM_ERROR;
	}
	if (bg_record && (rc == SLURM_SUCCESS)) {
		/* Make sure we set this to free since if it isn't in
		   the system and we are waiting for it to be free, we
		   will be waiting around for a long time ;).
		*/
		bg_record->state = BG_BLOCK_FREE;
	}
	return rc;
}

extern int bridge_handle_internal_errors(
	const char *function, const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::InternalErrors::XMLParseError:
		error("%s: XML Parse Error.", function);
		break;
	case bgsched::InternalErrors::InconsistentDataError:
		error("%s: Inconsistent Data Error.", function);
		break;
	case bgsched::InternalErrors::UnexpectedError:
		error("%s: Unexpected Error returned.", function);
		break;
	default:
		error("%s: Unexpected Internal exception value %d",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_runtime_errors(const char *function,
					const uint32_t err,
					bg_record_t *bg_record)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::RuntimeErrors::BlockBootError:
	{
		BlockFilter filter;
		Block::Ptrs vec;

		rc = BG_ERROR_BOOT_ERROR;

		if ((bg_record->magic != BLOCK_MAGIC)
		    || !bg_record->bg_block_id) {
			error("%s: bad block given to booting.", function);
			break;
		}

		filter.setName(string(bg_record->bg_block_id));

		vec = bridge_get_blocks(filter);
		if (vec.empty()) {
			debug("%s: block %s not found, removing "
			      "from slurm", function, bg_record->bg_block_id);
			break;
		}
		const Block::Ptr &block_ptr = *(vec.begin());
		uint16_t state = bridge_translate_status(
			block_ptr->getStatus().toValue());
		if (state == BG_BLOCK_FREE) {
			error("%s: Block %s was free but we got an error "
			      "while trying to boot it. (system=%s) (us=%s)",
			      function, bg_record->bg_block_id,
			      bg_block_state_string(state),
			      bg_block_state_string(bg_record->state));
		} else {
			debug2("%s: tring to boot a block %s that wasn't "
			       "free (system=%s) (us=%s), no real error.",
			       function, bg_record->bg_block_id,
			       bg_block_state_string(state),
			       bg_block_state_string(bg_record->state));
			rc = SLURM_SUCCESS;
		}

		break;
	}
	case bgsched::RuntimeErrors::BlockFreeError:
		/* not a real error */
		rc = BG_ERROR_FREE;
		debug2("%s: Error freeing block %s.", function,
		       bg_record->bg_block_id);
		break;
	case bgsched::RuntimeErrors::BlockCreateError:
		error("%s: Error creating block %s.", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::RuntimeErrors::BlockAddError:
		error("%s: Error Setting block %s owner.", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::RuntimeErrors::InvalidBlockState:
		/* not a real error */
		rc = BG_ERROR_INVALID_STATE;
		error("%s: Error can't perform task with block %s in state %s",
		      function, bg_record->bg_block_id,
		      bg_block_state_string(bg_record->state));
		break;
	case bgsched::RuntimeErrors::DimensionOutOfRange:
	 	error("%s: Dimension out of Range.", function);
	        break;
	case bgsched::RuntimeErrors::AuthorityError:
	 	error("%s: Authority Error.", function);
	        break;
	case bgsched::RuntimeErrors::HardwareInUseError:
		error("%s: Hardware in use Error.", function);
	        break;
	default:
		error("%s: Unexpected Runtime exception value %d.",
		      function, err);
	}
	return rc;
}

/* RealTime errors */

extern int bridge_handle_realtime_client_errors(const char *function,
						const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::realtime::ClientStateErrors::MustBeConnected:
		error("%s: The real-time client must be connected before "
		      "this method is called, and apparently you are not",
		      function);
		break;
	default:
		error("%s: Unexpected Realtime client error: %d.",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_realtime_configuration_errors(const char *function,
						       const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::realtime::ConfigurationErrors::InvalidHost:
		error("%s: The host value given is not in the correct format",
		      function);
		break;
	case bgsched::realtime::ConfigurationErrors::MissingSecurityProperty:
		error("%s: A required security configuration property is "
		      "missing from the bg.properties file",
		      function);
		break;
	default:
		error("%s: Unexpected Realtime Configuration error: %d.",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_realtime_connection_errors(const char *function,
						    const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::realtime::ConnectionErrors::CannotResolve:
		error("%s: Cannot resolve the real-time server host or port",
		      function);
		break;
	case bgsched::realtime::ConnectionErrors::CannotConnect:
		error("%s: Cannot connect to the real-time server",
		      function);
		break;
	case bgsched::realtime::ConnectionErrors::LostConnection:
		error("%s: Unexpectedly lost the connection to the "
		      "real-time server",
		      function);
		break;
	default:
		error("%s: Unexpected Realtime Connection error: %d.",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_realtime_filter_errors(const char *function,
						const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::realtime::FilterErrors::PatternNotValid:
		error("%s: The pattern supplied to the filter option "
		      "is not valid", function);
		break;
	default:
		error("%s: Unexpected Realtime Filter error: %d.",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_realtime_internal_errors(const char *function,
						  const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::realtime::InternalErrors::ApiUnexpectedFailure:
		error("%s: An API called by the real-time client "
		      "failed in an unexpected way.", function);
		break;
	default:
		error("%s: Unexpected Realtime Internal error: %d.",
		      function, err);
	}
	return rc;
}

extern int bridge_handle_realtime_protocol_errors(const char *function,
						  const uint32_t err)
{
	int rc = SLURM_ERROR;

	switch (err) {
	case bgsched::realtime::ProtocolErrors::MessageTooLong:
		error("%s: A message received from the real-time server is "
		      "too long", function);
		break;
	case bgsched::realtime::ProtocolErrors::UnexpectedMessageType:
		error("%s: The type of message received from the real-time "
		      "server is not expected", function);
		break;
	case bgsched::realtime::ProtocolErrors::ErrorReadingMessage:
		error("%s: An error occurred parsing a message received "
		      "from the real-time server", function);
		break;
	case bgsched::realtime::ProtocolErrors::UnexpectedDbChangeType:
		error("%s: The type of DB change message received "
		      "from the real-time server is not expected", function);
		break;
	case bgsched::realtime::ProtocolErrors::MessageNotValid:
		error("%s: A message received from the real-time server "
		      "is not valid", function);
		break;
	default:
		error("%s: Unexpected Realtime Protocol error: %d.",
		      function, err);
	}
	return rc;
}

extern uint16_t bridge_translate_status(bgsched::Block::Status state_in)
{
	switch (state_in) {
	case Block::Allocated:
		return BG_BLOCK_ALLOCATED;
		break;
	case Block::Booting:
		return BG_BLOCK_BOOTING;
		break;
	case Block::Free:
		return BG_BLOCK_FREE;
		break;
	case Block::Initialized:
		return BG_BLOCK_INITED;
		break;
	case Block::Terminating:
		return BG_BLOCK_TERM;
		break;
	default:
		return BG_BLOCK_ERROR_FLAG;
		break;
	}
	error("unknown block state %d", state_in);
	return BG_BLOCK_NAV;
}

#if defined HAVE_BG_GET_ACTION
extern uint16_t bridge_translate_action(bgsched::Block::Action::Value action_in)
{
	switch (action_in) {
	case Block::Action::None:
		return BG_BLOCK_ACTION_NONE;
		break;
	case Block::Action::Boot:
		return BG_BLOCK_ACTION_BOOT;
		break;
	case Block::Action::Free:
		return BG_BLOCK_ACTION_FREE;
	default:
		error("unknown block action %d", action_in);
		return BG_BLOCK_ACTION_NAV;
		break;
	}
	error("unknown block action %d", action_in);
	return BG_BLOCK_ACTION_NAV;
}
#endif

extern uint16_t bridge_translate_switch_usage(bgsched::Switch::InUse usage_in)
{
	switch (usage_in) {
	case Switch::NotInUse:
		return BG_SWITCH_NONE;
		break;
	case Switch::IncludedBothPortsInUse:
		return BG_SWITCH_TORUS;
		break;
	case Switch::IncludedOutputPortInUse:
		return (BG_SWITCH_OUT | BG_SWITCH_OUT_PASS);
		break;
	case Switch::IncludedInputPortInUse:
		return (BG_SWITCH_IN | BG_SWITCH_IN_PASS);
		break;
	case Switch::Wrapped:
		return BG_SWITCH_WRAPPED;
		break;
	case Switch::Passthrough:
		return BG_SWITCH_PASS;
		break;
	case Switch::WrappedPassthrough:
		return BG_SWITCH_WRAPPED_PASS;
		break;
	default:
		error("unknown switch usage %d", usage_in);
		break;
	}

	return BG_SWITCH_NONE;
}

extern const char *bridge_hardware_state_string(const int state)
{
	switch(state) {
	case Hardware::Available:
		return "Available";
	case Hardware::Missing:
		return "Missing";
	case Hardware::Error:
		return "Error";
	case Hardware::Service:
		return "Service";
	case Hardware::SoftwareFailure:
		return "SoftwareFailure";
	default:
		return "Unknown";
	}
	return "Unknown";
}

/* helper functions */

extern Block::Ptrs bridge_get_blocks(BlockFilter filter)
{
	Block::Ptrs vec;

	try {
		vec = getBlocks(filter);
	} catch (const bgsched::DatabaseException& err) {
		bridge_handle_database_errors("getBlocks",
					      err.getError().toValue());
	} catch (const bgsched::InternalException& err) {
		bridge_handle_internal_errors("getBlocks",
					      err.getError().toValue());
	} catch (const bgsched::RuntimeException& err) {
		bridge_handle_runtime_errors("getBlocks",
					     err.getError().toValue(),
					     NULL);
	} catch (...) {
              error("Unknown error from getBlocks().");
	}

	return vec;
}

extern Midplane::ConstPtr bridge_get_midplane(ComputeHardware::ConstPtr bgqsys,
					      ba_mp_t *ba_mp)
{
	Midplane::ConstPtr mp_ptr;

	assert(ba_mp);

	try {
		Coordinates coords(
			ba_mp->coord[0], ba_mp->coord[1],
			ba_mp->coord[2], ba_mp->coord[3]);
		mp_ptr = bgqsys->getMidplane(coords);
	} catch (const bgsched::InputException& err) {
		bridge_handle_input_errors(
			"ComputeHardware::getMidplane",
			err.getError().toValue(), NULL);
	} catch (...) {
              error("Unknown error from ComputeHardware::getMidplane.");
	}
	return mp_ptr;
}

extern Node::ConstPtrs bridge_get_midplane_nodes(const std::string& loc)
{
	Node::ConstPtrs vec;

	try {
		vec = getMidplaneNodes(loc);
	} catch (const bgsched::DatabaseException& err) {
		bridge_handle_database_errors("getMidplaneNodes",
					      err.getError().toValue());
	} catch (const bgsched::InputException& err) {
		bridge_handle_input_errors("getMidplaneNodes",
					   err.getError().toValue(),
					   NULL);
	} catch (const bgsched::InternalException& err) {
		bridge_handle_internal_errors("getMidplaneNodes",
						   err.getError().toValue());
	} catch (...) {
                error("Unknown error from getMidplaneNodes.");
	}
	return vec;
}

extern NodeBoard::ConstPtr bridge_get_nodeboard(Midplane::ConstPtr mp_ptr,
						int nodeboard_num)
{
	NodeBoard::ConstPtr nb_ptr;

	try {
		nb_ptr = mp_ptr->getNodeBoard(nodeboard_num);
	} catch (const bgsched::InputException& err) {
		bridge_handle_input_errors("Midplane::getNodeBoard",
					   err.getError().toValue(),
					   NULL);
	} catch (...) {
                error("Unknown error from Midplane::getNodeBoard.");
	}
	return nb_ptr;
}

extern NodeBoard::ConstPtrs bridge_get_nodeboards(const std::string& mp_loc)
{
	NodeBoard::ConstPtrs nb_ptr;

	try {
		nb_ptr = getNodeBoards(mp_loc);
	} catch (const bgsched::InputException& err) {
		bridge_handle_input_errors("getNodeBoards",
					   err.getError().toValue(),
					   NULL);
	} catch (...) {
                error("Unknown error from getNodeBoards.");
	}
	return nb_ptr;
}

extern Switch::ConstPtr bridge_get_switch(Midplane::ConstPtr mp_ptr, int dim)
{
	Switch::ConstPtr switch_ptr;

	try {
		switch_ptr = mp_ptr->getSwitch(dim);
	} catch (const bgsched::InputException& err) {
		bridge_handle_input_errors("Midplane::getSwitch",
					   err.getError().toValue(),
					   NULL);
	} catch (...) {
                error("Unknown error from Midplane::getSwitch.");
	}
	return switch_ptr;
}

extern ComputeHardware::ConstPtr bridge_get_compute_hardware()
{
	ComputeHardware::ConstPtr bgqsys;

	try {
		bgqsys = getComputeHardware();
	} catch (const bgsched::InternalException& err) {
		bridge_handle_internal_errors("getComputeHardware",
					      err.getError().toValue());
	} catch (...) {
		error("Unknown error from getComputeHardware");
	}
	return bgqsys;
}
#endif
