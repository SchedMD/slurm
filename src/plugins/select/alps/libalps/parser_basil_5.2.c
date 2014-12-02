/*
 * XML tag handlers specific to Basil 5.2
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "parser_internal.h"

struct element_handler basil_5_2_elements[] = {
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
			.hnd	= eh_released_4_0
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
			.hnd	= eh_node_array_5_1
	},
	[BT_NODE]	= {
			.tag	= "Node",
			.depth	= 4,
			.uniq	= false,
			.hnd	= eh_node
	},
	[BT_SOCKARRAY]  = {
			.tag    = "SocketArray",
			.depth  = 5,
			.uniq	= true,
			.hnd    = NULL
	},
	[BT_SOCKET]     = {
			.tag    = "Socket",
			.depth  = 6,
			.uniq	= false,
			.hnd    = eh_socket_5_1
	},
	[BT_SEGMARRAY]	= {
			.tag	= "SegmentArray",
			.depth	= 7,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_SEGMENT]	= {
			.tag	= "Segment",
			.depth	= 8,
			.uniq	= false,
			.hnd	= eh_segment
	},
	[BT_COMUARRAY]  = {
			.tag    = "ComputeUnitArray",
			.depth  = 9,
			.uniq   = true,
			.hnd    = NULL
	},
	[BT_COMPUNIT]  = {
			.tag    = "ComputeUnit",
			.depth  = 10,
			.uniq   = false,
			.hnd    = eh_compute_5_1
	},
	[BT_PROCARRAY]	= {
			.tag	= "ProcessorArray",
			.depth	= 11,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_PROCESSOR]	= {
			.tag	= "Processor",
			.depth	= 12,
			.uniq	= false,
			.hnd	= eh_proc
	},
	[BT_PROCALLOC]	= {
			.tag	= "ProcessorAllocation",
			.depth	= 13,
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
			.depth	= 9,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_LABEL]	= {
			.tag	= "Label",
			.depth	= 10,
			.uniq	= false,
			.hnd	= eh_label
	},
	[BT_ACCELARRAY]	= {
			.tag	= "AcceleratorArray",
			.depth	= 5,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_ACCEL]	= {
			.tag	= "Accelerator",
			.depth	= 6,
			.uniq	= false,
			.hnd	= eh_accel
	},
	[BT_ACCELALLOC]	= {
			.tag	= "AcceleratorAllocation",
			.depth	= 7,
			.uniq	= false,
			.hnd	= eh_accel_alloc
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
	[BT_SWITCHRES]	= {
			.tag	= "Reservation",
			.depth	= 3,
			.uniq	= false,
			.hnd	= eh_switch_resv
	},
	[BT_SWITCHAPP]	= {
			.tag	= "Application",
			.depth	= 3,
			.uniq	= false,
			.hnd	= eh_switch_app
	},
	[BT_SWITCHRESARRAY]	= {
			.tag	= "ReservationArray",
			.depth	= 2,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_SWITCHAPPARRAY]	= {
			.tag	= "ApplicationArray",
			.depth	= 2,
			.uniq	= true,
			.hnd	= NULL
	},
	[BT_5_1_MAX]	= {
			NULL, 0, 0, NULL
	}
};
