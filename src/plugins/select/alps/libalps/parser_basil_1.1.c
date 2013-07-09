/*
 * XML tag handlers specific to Basil 1.1 (as used on XT systems up to CLE 2.x).
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "parser_internal.h"

/** Basil 1.1 'Reservation' element (more attributes than in Basil 1.0) */
void eh_resv_1_1(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "time_stamp", "batch_id" };

	eh_resv(ud, attrs);
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (ud->ud_inventory) {
		struct basil_rsvn *cur = ud->ud_inventory->rsvn_head;

		if (atotime_t(attribs[0], &cur->timestamp) < 0)
			fatal("illegal timestamp '%s'", attribs[0]);
		strncpy(cur->batch_id, attribs[1], sizeof(cur->batch_id));
	}
}

const struct element_handler basil_1_1_elements[] = {
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
	[BT_CONFIRMED]	= {
			.tag	= "Confirmed",
			.depth	= 2,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_RELEASED]	= {
			.tag	= "Released",
			.depth	= 2,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_ENGINE]	= {
			.tag	= "Engine",
			.depth	= 2,
			.uniq	= true,
			.hnd	= eh_engine
	},
	[BT_INVENTORY]	= {
			.tag	= "Inventory",
			.depth	= 2,
			.uniq	= true,
			.hnd	= NULL
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
			.hnd	= eh_node
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
			.hnd	= eh_resv_1_1
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
	[BT_1_1_MAX]	= {
			NULL, 0, 0, NULL
	}
};
