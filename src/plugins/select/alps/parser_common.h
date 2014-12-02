/*
 * Routines and data structures common to libalps and libemulate
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#ifndef __PARSER_COMMON_H__
#define __PARSER_COMMON_H__

#include "basil_alps.h"

/*
 * Global enum-to-string mapping tables
 */

/* Basil versions */
const char *bv_names[BV_MAX] = {	/* Basil Protocol version */
	[BV_1_0] = "1.0",
	[BV_1_1] = "1.1",
	[BV_1_2] = "1.1",
	[BV_3_1] = "1.1",
	[BV_4_0] = "1.2",
	[BV_4_1] = "1.2",
	[BV_5_0] = "1.2",
	[BV_5_1] = "1.3",
	[BV_5_2] = "1.3",
	[BV_5_2_3] = "1.3"
};

const char *bv_names_long[BV_MAX] = {	/* Actual version name */
	[BV_1_0] = "1.0",
	[BV_1_1] = "1.1",
	[BV_1_2] = "1.2",
	[BV_3_1] = "3.1",
	[BV_4_0] = "4.0",
	[BV_4_1] = "4.1",
	[BV_5_0] = "5.0",
	[BV_5_1] = "5.1",
	[BV_5_2] = "5.2",
	[BV_5_2_3] = "5.2"
};

/* Basil methods */
const char *bm_names[BM_MAX] = {
	[BM_none]	= "NONE",
	[BM_engine]	= "QUERY",
	[BM_inventory]	= "QUERY",
	[BM_reserve]	= "RESERVE",
	[BM_confirm]	= "CONFIRM",
	[BM_release]	= "RELEASE",
	[BM_switch]	= "SWITCH",
};

/* Error codes */
const char *be_names[BE_MAX] = {
	[BE_NONE]	= "",
	[BE_INTERNAL]	= "INTERNAL",
	[BE_SYSTEM]	= "SYSTEM",
	[BE_PARSER]	= "PARSER",
	[BE_SYNTAX]	= "SYNTAX",
	[BE_BACKEND]	= "BACKEND",
	[BE_NO_RESID]	= "BACKEND",	/* backend can not locate resId */
	[BE_UNKNOWN]	= "UNKNOWN"
};

const char *be_names_long[BE_MAX] = {
	[BE_NONE]	= "no ALPS error",
	[BE_INTERNAL]	= "internal error: unexpected condition encountered",
	[BE_SYSTEM]	= "system call failed",
	[BE_PARSER]	= "XML parser error",
	[BE_SYNTAX]	= "improper XML content or structure",
	[BE_BACKEND]	= "ALPS backend error",
	[BE_NO_RESID]	= "ALPS resId entry does not (or no longer) exist",
	[BE_UNKNOWN]	= "UNKNOWN ALPS ERROR"
};

/*
 * RESERVE/INVENTORY data
 */
const char *nam_arch[BNA_MAX] = {
	[BNA_NONE]	= "UNDEFINED",
	[BNA_X2]	= "X2",
	[BNA_XT]	= "XT",
	[BNA_UNKNOWN]	= "UNKNOWN"
};

const char *nam_memtype[BMT_MAX] = {
	[BMT_NONE]	= "UNDEFINED",
	[BMT_OS]	= "OS",
	[BMT_HUGEPAGE]	= "HUGEPAGE",
	[BMT_VIRTUAL]	= "VIRTUAL",
	[BMT_UNKNOWN]	= "UNKNOWN"
};

const char *nam_labeltype[BLT_MAX] = {
	[BLT_NONE]	= "UNDEFINED",
	[BLT_HARD]	= "HARD",
	[BLT_SOFT]	= "SOFT",
	[BLT_UNKNOWN]	= "UNKNOWN"
};

const char *nam_ldisp[BLD_MAX] = {
	[BLD_NONE]	= "UNDEFINED",
	[BLD_ATTRACT]	= "ATTRACT",
	[BLD_REPEL]	= "REPEL",
	[BLD_UNKNOWN]	= "UNKNOWN"
};

/*
 * INVENTORY-only data
 */
const char *nam_noderole[BNR_MAX] = {
	[BNR_NONE]	= "UNDEFINED",
	[BNR_INTER]	= "INTERACTIVE",
	[BNR_BATCH]	= "BATCH",
	[BNR_UNKNOWN]	= "UNKNOWN"
};

const char *nam_nodestate[BNS_MAX] = {
	[BNS_NONE]	= "UNDEFINED",
	[BNS_UP]	= "UP",
	[BNS_DOWN]	= "DOWN",
	[BNS_UNAVAIL]	= "UNAVAILABLE",
	[BNS_ROUTE]	= "ROUTING",
	[BNS_SUSPECT]	= "SUSPECT",
	[BNS_ADMINDOWN]	= "ADMIN",
	[BNS_UNKNOWN]	= "UNKNOWN"
};

const char *nam_proc[BPT_MAX] = {
	[BPT_NONE]	= "UNDEFINED",
	[BPT_CRAY_X2]	= "cray_x2",
	[BPT_X86_64]	= "x86_64",
	[BPT_UNKNOWN]	= "UNKNOWN"
};

/*
 * Enum-to-string mapping tables specific to Basil 3.1
 */
const char *nam_rsvn_mode[BRM_MAX] = {
	[BRM_NONE]      = "UNDEFINED",
	[BRM_EXCLUSIVE] = "EXCLUSIVE",
	[BRM_SHARE]     = "SHARED",
	[BRM_UNKNOWN]   = "UNKNOWN"
};

const char *nam_gpc_mode[BGM_MAX] = {
	[BGM_NONE]      = "NONE",
	[BRM_PROCESSOR] = "PROCESSOR",
	[BRM_LOCAL]     = "LOCAL",
	[BRM_GLOBAL]    = "GLOBAL",
	[BGM_UNKNOWN]   = "UNKNOWN"
};

/*
 * Enum-to-string mapping tables introduced in Alps 4.0
 */
const char *nam_acceltype[BA_MAX] = {
	[BA_NONE]	= "UNDEFINED",
	[BA_GPU]	= "GPU",
	[BA_UNKNOWN]	= "UNKNOWN"
};

const char *nam_accelstate[BAS_MAX] = {
	[BAS_NONE]	= "UNDEFINED",
	[BAS_UP]	= "UP",
	[BAS_DOWN]	= "DOWN",
	[BAS_UNKNOWN]	= "UNKNOWN"
};

#endif /* __PARSER_COMMON_H__ */
