/*
 * XML tag handlers specific to Basil 1.0.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "parser_internal.h"

const struct element_handler basil_1_0_elements[] = {
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
	[BT_PROCARRAY]	= {
			.tag	= "ProcessorArray",
			.depth	= 5,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_PROCESSOR]	= {
			.tag	= "Processor",
			.depth	= 6,
			.uniq	= false,
			.hnd	= eh_proc
	},
	[BT_PROCALLOC]	= {
			.tag	= "ProcessorAllocation",
			.depth	= 7,
			.uniq	= false,
			.hnd	= eh_proc_alloc
	},
	[BT_MEMARRAY]	= {
			.tag	= "MemoryArray",
			.depth	= 5,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_MEMORY]	= {
			.tag	= "Memory",
			.depth	= 6,
			.uniq	= false,
			.hnd	= eh_mem
	},
	[BT_MEMALLOC]	= {
			.tag	= "MemoryAllocation",
			.depth	= 7,
			.uniq	= false,
			.hnd	= eh_mem_alloc
	},
	[BT_LABELARRAY]	= {
			.tag	= "LabelArray",
			.depth	= 5,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_LABEL]	= {
			.tag	= "Label",
			.depth	= 6,
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
			.hnd	= eh_resv
	},
	[BT_1_0_MAX]	= {
			NULL, 0, 0, NULL
	}
};
