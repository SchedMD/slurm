/*****************************************************************************\
 *  bridge_helper.h
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
		rc = SLURM_SUCCESS;
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
		rc = SLURM_SUCCESS;
		error("%s: Unknown block %s!",
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
		error("%s: Error booting block %s.", function,
		      bg_record->bg_block_id);
		break;
	case bgsched::RuntimeErrors::BlockFreeError:
		/* not a real error */
		rc = SLURM_SUCCESS;
		error("%s: Error freeing block %s.", function,
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
		rc = SLURM_SUCCESS;
		error("%s: Error can't perform task with block %s in state %s"
		      "incorrect %s.", function, bg_record->bg_block_id,
		      bg_block_state_string(bg_record->state));
		break;
	case bgsched::RuntimeErrors::DimensionOutOfRange:
	 	error("%s: Dimension out of Range.", function);
	        break;
	case bgsched::RuntimeErrors::AuthorityError:
	 	error("%s: Authority Error.", function);
	        break;
	default:
		error("%s: Unexpected Runtime exception value %d.",
		      function, err);
	}
	return rc;
}

extern bg_block_status_t bridge_translate_status(
	bgsched::Block::Status state_in)
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
		return BG_BLOCK_ERROR;
		break;
	}
	error("unknown block state %d", state_in);
	return BG_BLOCK_NAV;
}

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
#endif
