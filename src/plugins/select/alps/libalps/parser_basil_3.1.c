/*
 * XML tag handlers specific to Basil 3.1 (Basil 1.1 variant on XE/Gemini).
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "parser_internal.h"

/** Basil 3.1 and above 'ReservedNode' element */
void eh_resvd_node(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "node_id" };
	uint32_t node_id;

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &node_id) < 0)
		fatal("illegal node_id = %s", attribs[0]);
	if (ns_add_node(&ud->bp->mdata.res->rsvd_nodes, node_id, true) < 0)
		fatal("could not add node %u", node_id);
}

/** Basil 3.1 and above 'Confirmed' element */
void eh_confirmed(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "reservation_id", "pagg_id" };
	uint32_t rsvn_id;
	uint64_t pagg_id;

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &rsvn_id) < 0)
		fatal("illegal rsvn_id = %s", attribs[0]);
	if (rsvn_id != ud->bp->mdata.res->rsvn_id)
		fatal("rsvn_id mismatch '%s'", attribs[0]);
	if (atou64(attribs[1], &pagg_id) < 0)
		fatal("illegal pagg_id = %s", attribs[1]);
	if (pagg_id != ud->bp->mdata.res->pagg_id)
		fatal("pagg_id mismatch '%s'", attribs[1]);
}

/** Basil 3.1 'Released' element */
void eh_released_3_1(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "reservation_id" };
	uint32_t rsvn_id;

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &rsvn_id) < 0)
		fatal("illegal rsvn_id = %s", attribs[0]);
	if (rsvn_id != ud->bp->mdata.res->rsvn_id)
		fatal("rsvn_id mismatch '%s'", attribs[0]);
}

/** Basil 3.1 and above 'Engine' element */
void eh_engine_3_1(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "basil_support" };

	eh_engine(ud, attrs);
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));
}

/** Basil 3.1 and above 'Inventory' element */
void eh_inventory_3_1(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "mpp_host", "timestamp" };
	struct basil_inventory *inv = ud->bp->mdata.inv;

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	strncpy(inv->mpp_host, attribs[0], sizeof(inv->mpp_host));
	if (atotime_t(attribs[1], &inv->timestamp) < 0)
		fatal("illegal timestamp = %s", attribs[1]);
}

/** Basil 3.1 and above 'Node' element */
void eh_node_3_1(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "router_id" };
	/*
	 * The 'router_id' element can be used to determine the interconnect:
	 * - on Gemini systems the 'Node' element has this attribute,
	 * - on SeaStar systems the 'Node' element does not have this attribute.
	 */
	ud->bp->mdata.inv->is_gemini = true;

	eh_node(ud, attrs);
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (ud->ud_inventory) {
		struct basil_node *current = ud->ud_inventory->node_head;

		if (atou32(attribs[0], &current->router_id) < 0)
			fatal("illegal router_id = %s", attribs[0]);
	}
}

/** Basil 3.1 and above 'Reservation' element */
void eh_resv_3_1(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "reservation_mode", "gpc_mode" };

	eh_resv_1_1(ud, attrs);
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (ud->ud_inventory) {
		struct basil_rsvn *cur = ud->ud_inventory->rsvn_head;

		for (cur->rsvn_mode = BRM_EXCLUSIVE;
		     cur->rsvn_mode < BRM_MAX; cur->rsvn_mode++)
			if (strcmp(attribs[0], nam_rsvn_mode[cur->rsvn_mode]) == 0)
				break;
		for (cur->gpc_mode = BGM_NONE;
		     cur->gpc_mode < BGM_MAX; cur->gpc_mode++)
			if (strcmp(attribs[1], nam_gpc_mode[cur->gpc_mode]) == 0)
				break;
	}
}

const struct element_handler basil_3_1_elements[] = {
	[BT_MESSAGE]	= {
			.tag	= "Message",
			.depth	= 0xff,	/* unused, can appear at any depth */
			.uniq	= false,
			.hnd	= eh_message
	},
	[BT_RESPONSE]	= {
			.tag	= "BasilResponse",
			.depth	= 0,
			.uniq	= true,
			.hnd	= eh_response
	},
	[BT_RESPDATA]	= {
			.tag	= "ResponseData",
			.depth	= 1,
			.uniq	= true,
			.hnd	= eh_resp_data
	},
	[BT_RESERVED]	= {
			.tag	= "Reserved",
			.depth	= 2,
			.uniq	= true,
			.hnd	= eh_reserved
	},
	[BT_RESVDNODEARRAY] = {
			.tag	= "ReservedNodeArray",
			.depth	= 3,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_RESVDNODE] = {
			.tag	= "ReservedNode",
			.depth	= 4,
			.uniq	= false,
			.hnd	= eh_resvd_node
	},
	[BT_CONFIRMED]	= {
			.tag	= "Confirmed",
			.depth	= 2,
			.uniq	= true,
			.hnd	= eh_confirmed
	},
	[BT_RELEASED]	= {
			.tag	= "Released",
			.depth	= 2,
			.uniq	= true,
			.hnd	= eh_released_3_1
	},
	[BT_ENGINE]	= {
			.tag	= "Engine",
			.depth	= 2,
			.uniq	= true,
			.hnd	= eh_engine_3_1
	},
	[BT_INVENTORY]	= {
			.tag	= "Inventory",
			.depth	= 2,
			.uniq	= true,
			.hnd	= eh_inventory_3_1
	},
	[BT_NODEARRAY]	= {
			.tag	= "NodeArray",
			.depth	= 3,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_NODE]	= {
			.tag	= "Node",
			.depth	= 4,
			.uniq	= false,
			.hnd	= eh_node_3_1
	},
	[BT_SEGMARRAY]	= {
			.tag	= "SegmentArray",
			.depth	= 5,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_SEGMENT]	= {
			.tag	= "Segment",
			.depth	= 6,
			.uniq	= false,
			.hnd	= eh_segment
	},
	[BT_PROCARRAY]	= {
			.tag	= "ProcessorArray",
			.depth	= 7,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_PROCESSOR]	= {
			.tag	= "Processor",
			.depth	= 8,
			.uniq	= false,
			.hnd	= eh_proc
	},
	[BT_PROCALLOC]	= {
			.tag	= "ProcessorAllocation",
			.depth	= 9,
			.uniq	= false,
			.hnd	= eh_proc_alloc
	},
	[BT_MEMARRAY]	= {
			.tag	= "MemoryArray",
			.depth	= 7,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_MEMORY]	= {
			.tag	= "Memory",
			.depth	= 8,
			.uniq	= false,
			.hnd	= eh_mem
	},
	[BT_MEMALLOC]	= {
			.tag	= "MemoryAllocation",
			.depth	= 9,
			.uniq	= false,
			.hnd	= eh_mem_alloc
	},
	[BT_LABELARRAY]	= {
			.tag	= "LabelArray",
			.depth	= 7,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_LABEL]	= {
			.tag	= "Label",
			.depth	= 8,
			.uniq	= false,
			.hnd	= eh_label
	},
	[BT_RESARRAY]	= {
			.tag	= "ReservationArray",
			.depth	= 3,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_RESVN]	= {
			.tag	= "Reservation",
			.depth	= 4,
			.uniq	= false,
			.hnd	= eh_resv_3_1
	},
	[BT_APPARRAY]	= {
			.tag	= "ApplicationArray",
			.depth	= 5,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_APPLICATION]	= {
			.tag	= "Application",
			.depth	= 6,
			.uniq	= false,
			.hnd	= eh_application
	},
	[BT_CMDARRAY]	= {
			.tag	= "CommandArray",
			.depth	= 7,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_COMMAND]	= {
			.tag	= "Command",
			.depth	= 8,
			.uniq	= false,
			.hnd	= eh_command
	},
	[BT_3_1_MAX]	= {
			NULL, 0, 0, NULL
	}
};
