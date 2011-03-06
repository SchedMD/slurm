/*
 * Lower-level BASIL/ALPS XML-RPC library functions.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#ifndef __BASIL_ALPS_H__
#define __BASIL_ALPS_H__

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "src/common/log.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include <ctype.h>
#include <string.h>

#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#ifdef HAVE_CRAY
#  include <expat.h>
#  include <mysql.h>
#endif

/*
 * Limits
 */
#define TAG_DEPTH_MAX		12	/* maximum XML nesting level */
#define BASIL_STRING_SHORT	16
#define BASIL_STRING_MEDIUM	32
#define BASIL_STRING_LONG	64
#define BASIL_ERROR_BUFFER_SIZE	256

/* Output parameters */
enum query_columns {
	/* integer data */
	COL_X,		/* X coordinate		*/
	COL_Y,		/* Y coordinate		*/
	COL_Z,		/* Z coordinate		*/
	COL_CAB,	/* cabinet position		*/
	COL_ROW,	/* row position			*/
	COL_CAGE,	/* cage number (0..2)		*/
	COL_SLOT,	/* slot number (0..7)		*/
	COL_CPU,	/* node number (0..3)		*/
	COL_CORES,	/* number of cores per node	*/
	COL_MEMORY,	/* rounded-down memory in MB	*/
	/* string data */
	COL_TYPE,	/* {service, compute }		*/
	COLUMN_COUNT	/* sentinel */
};

/*
 * Basil XML tags
 */
enum basil_version {
	BV_1_0 = 0,	/* Basil 1.0: earliest version and fallback */
	BV_1_1,		/* Basil 1.1 CLE variant (XT/SeaStar)       */
	BV_1_2,		/* Basil 1.1 CLE 2.x variant (XT/SeaStar)   */
	BV_3_1,		/* Basil 1.1 CLE 3.x (XE/Gemini support)    */
	BV_MAX
};

enum basil_method {
	BM_none = 0,
	BM_reserve,	/* RESERVE method          */
	BM_confirm,	/* CONFIRM method          */
	BM_release,	/* RELEASE method          */
	BM_engine,	/* QUERY of type ENGINE    */
	BM_inventory,	/* QUERY of type INVENTORY */
	BM_MAX,
	BM_UNKNOWN
};

/**
 * basil_element - XML tags appearing in BasilReponse
 * This is list is *sorted* according to the following Basil versions:
 * - Basil 1.0  (common denominator)
 * - Basil 1.1  (earliest 1.1 variant used on XT systems with CLE 2.x)
 * - Basil 3.1  (later 1.1 variant used on XE systems with CLE 3.x)
 * Remember to keep this order when making changes to this enum!
 */
enum basil_element {
	BT_MESSAGE = 0,
	BT_RESPONSE,
	BT_RESPDATA,

	BT_RESERVED,		/* RESERVE */
	BT_CONFIRMED,		/* CONFIRM */
	BT_RELEASED,		/* RELEASE */
	BT_ENGINE,		/* QUERY - ENGINE    */

	BT_INVENTORY,		/* QUERY - INVENTORY */
	BT_NODEARRAY,		/* Generic Inventory */
	BT_NODE,		/* Generic Inventory */
	BT_PROCARRAY,		/* Generic Inventory */
	BT_PROCESSOR,		/* Generic Inventory */
	BT_PROCALLOC,		/* Generic Inventory */
	BT_MEMARRAY,		/* Generic Inventory */
	BT_MEMORY,		/* Generic Inventory */
	BT_MEMALLOC,		/* Generic Inventory */
	BT_LABELARRAY,		/* Generic Inventory */
	BT_LABEL,		/* Generic Inventory */
	BT_RESARRAY,		/* Generic Inventory */
	BT_RESVN,		/* Generic Inventory */
#define BT_1_0_MAX		(BT_RESVN + 1)		/* End of Basil 1.0 */

	BT_SEGMARRAY,		/* Basil 1.1 Inventory/Node */
	BT_SEGMENT,		/* Basil 1.1 Inventory/Node */
	BT_APPARRAY,		/* Basil 1.1 Inventory/Reservation */
	BT_APPLICATION,		/* Basil 1.1 Inventory/Reservation */
	BT_CMDARRAY,		/* Basil 1.1 Inventory/Reservation */
	BT_COMMAND,		/* Basil 1.1 Inventory/Reservation */
#define BT_1_1_MAX		(BT_COMMAND + 1)	/* End of Basil 1.1 */

	BT_RESVDNODEARRAY,	/* Basil 3.1 RESERVE Response */
	BT_RESVDNODE,		/* Basil 3.1 RESERVE Response */
#define BT_3_1_MAX		(BT_RESVDNODE + 1)	/* End of Basil 3.1 */

	BT_MAX			/* End of Basil tags */
};

/* Error types */
enum basil_error {
	/* (a) up to and excluding BE_MAX, error kind information */
	BE_NONE = 0,
	BE_INTERNAL,
	BE_SYSTEM,
	BE_PARSER,
	BE_SYNTAX,
	BE_BACKEND,
	BE_UNKNOWN,
	BE_MAX,
	/* (b) bit masks for additional information */
	BE_ERROR_TYPE_MASK = 0x00FF,
	BE_TRANSIENT	   = 0x0100
};

/** Decode negative error code @rc into Basil error */
static inline enum basil_error decode_basil_error(int rc)
{
	int be = -rc & BE_ERROR_TYPE_MASK;

	return rc >= 0 ? BE_NONE : (be < BE_MAX ? be : BE_UNKNOWN);
}

/** Return true if the absolute value of @rc indicates transient error. */
static inline bool is_transient_error(int rc)
{
	return (rc < 0 ? -rc : rc) & BE_TRANSIENT;
}

extern const char *basil_strerror(int rc);

/*
 * INVENTORY/RESERVE data
 */
enum basil_node_arch {
	BNA_NONE = 0,
	BNA_X2,
	BNA_XT,
	BNA_UNKNOWN,
	BNA_MAX
};

enum basil_memory_type {
	BMT_NONE = 0,
	BMT_OS,
	BMT_HUGEPAGE,
	BMT_VIRTUAL,
	BMT_UNKNOWN,
	BMT_MAX
};

enum basil_label_type {
	BLT_NONE = 0,
	BLT_HARD,
	BLT_SOFT,
	BLT_UNKNOWN,
	BLT_MAX
};

enum basil_label_disp {
	BLD_NONE = 0,
	BLD_ATTRACT,
	BLD_REPEL,
	BLD_UNKNOWN,
	BLD_MAX
};

/*
 * INVENTORY-only data
 */
enum basil_node_state {
	BNS_NONE = 0,
	BNS_UP,
	BNS_DOWN,
	BNS_UNAVAIL,
	BNS_ROUTE,
	BNS_SUSPECT,
	BNS_ADMINDOWN,
	BNS_UNKNOWN,
	BNS_MAX
};

enum basil_node_role {
	BNR_NONE = 0,
	BNR_INTER,
	BNR_BATCH,
	BNR_UNKNOWN,
	BNR_MAX
};

enum basil_proc_type {
	BPT_NONE = 0,
	BPT_CRAY_X2,
	BPT_X86_64,
	BPT_UNKNOWN,
	BPT_MAX
};

enum basil_rsvn_mode {	/* Basil 3.1 */
	BRM_NONE = 0,
	BRM_EXCLUSIVE,
	BRM_SHARE,
	BRM_UNKNOWN,
	BRM_MAX
};

enum basil_gpc_mode {	/* Basil 3.1 */
	BGM_NONE = 0,
	BRM_PROCESSOR,
	BRM_LOCAL,
	BRM_GLOBAL,
	BGM_UNKNOWN,
	BGM_MAX
};

/*
 * Inventory structs
 */
struct basil_proc_alloc {
	uint32_t		rsvn_id;
	/* NB: processor is indivisible, i.e. at most 1 allocation */
};

struct basil_node_processor {
	uint32_t		ordinal;
	uint32_t		clock_mhz;
	enum basil_proc_type	arch;
	struct basil_proc_alloc	*allocation;

	struct basil_node_processor *next;
};

struct basil_mem_alloc {
	uint32_t		rsvn_id;
	uint32_t		page_count;

	struct basil_mem_alloc	*next;
};

struct basil_node_memory {
	enum basil_memory_type	type;
	uint32_t		page_size_kb;
	uint32_t		page_count;
	struct basil_mem_alloc	*a_head;

	struct basil_node_memory *next;
};

struct basil_label {
	enum basil_label_type	type;
	enum basil_label_disp	disp;
	char			name[BASIL_STRING_MEDIUM];

	struct basil_label *next;
};

struct basil_segment {
	uint8_t	ordinal;

	struct basil_node_processor	*proc_head;
	struct basil_node_memory	*mem_head;
	struct basil_label		*lbl_head;

	struct basil_segment *next;
};

struct basil_node {
	uint32_t node_id;
	uint32_t router_id;			/* Basil 3.1 */
	char	 name[BASIL_STRING_SHORT];

	enum basil_node_arch	arch;
	enum basil_node_role	role;
	enum basil_node_state	state;

	struct basil_segment	*seg_head;	/* Basil 1.1 */

	struct basil_node *next;
};
extern bool node_is_allocated(const struct basil_node *node);

struct basil_rsvn_app_cmd {
	uint32_t		width,
				depth,
				nppn,
				memory;
	enum basil_node_arch	arch;

	char			cmd[BASIL_STRING_MEDIUM];

	struct basil_rsvn_app_cmd *next;
};

struct basil_rsvn_app {
	uint64_t	apid;
	uint32_t	user_id;
	uint32_t	group_id;
	uint32_t	timestamp;

	struct basil_rsvn_app_cmd *cmd_head;

	struct basil_rsvn_app *next;
};

struct basil_rsvn {
	uint32_t	rsvn_id;
	uint32_t	timestamp;			/* Basil 1.1 */
	char		user_name[BASIL_STRING_MEDIUM];
	char		account_name[BASIL_STRING_MEDIUM];
	char		batch_id[BASIL_STRING_LONG];	/* Basil 1.1 */

	enum basil_rsvn_mode	rsvn_mode;		/* Basil 3.1 */
	enum basil_gpc_mode	gpc_mode;		/* Basil 3.1 */

	struct basil_rsvn_app	*app_head;		/* Basil 1.1 */

	struct basil_rsvn *next;
};

/*
 * Inventory parameters (OUT)
 */
struct basil_full_inventory {
	struct basil_node *node_head;
	struct basil_rsvn *rsvn_head;
};

/**
 * struct basil_inventory - basic inventory information
 * @mpp_host:    Basil 3.1 only
 * @timestamp:   Basil 3.1 only
 * @is_gemini:   true if XE/Gemini system, false if XT/SeaStar system
 * @batch_avail: number of compute nodes available for scheduling
 * @batch_total: total number of usable/used compute nodes
 * @nodes_total: total number of all compute nodes
 */
struct basil_inventory {
	char		mpp_host[BASIL_STRING_SHORT];
	uint32_t	timestamp;
	bool		is_gemini;
	uint32_t	batch_avail,
			batch_total,
			nodes_total;

	struct basil_full_inventory *f;
};

/*
 * Reservation parameters (IN)
 */
struct basil_memory_param {
	enum basil_memory_type	type;
	uint32_t		size_mb;

	struct basil_memory_param *next;
};

struct basil_rsvn_param {
	enum basil_node_arch	arch;		/* "architecture", XT or X2, -a  */
	long			width,		/* required mppwidth > 0,    -n  */
				/* The following MPP parameters are optional  */
				depth,		/* depth > 0,         -d  */
				nppn,		/* nppn > 0,          -N  */
				npps,		/* PEs per segment,   -S  */
				nspn;		/* segments per node, -sn */

	char				*nodes;		/* NodeParamArray   */
	struct basil_label		*labels;	/* LabelParamArray  */
	struct basil_memory_param	*memory;	/* MemoryParamArray */

	struct basil_rsvn_param		*next;
};

/**
 * struct basil_reservation  -  reservation parameters and data
 * @rsvn_id:      assigned by RESERVE method
 * @pagg_id:      used by CONFIRM method (session ID or CSA PAGG ID)
 * @rsvd_nodes:   assigned by Basil 3.1 RESERVE method
 * @user_name:    required by RESERVE method
 * @account_name: optional Basil 1.0 RESERVE parameter
 * @batch_id:     required Basil 1.1/3.1 RESERVE parameter
 * @params:	  parameter contents of the ReserveParamArray
 */
struct basil_reservation {
	/*
	 * Runtime (IN/OUT) parameters
	 */
	uint32_t	rsvn_id;
	uint64_t	pagg_id;
	struct nodespec *rsvd_nodes;
	/*
	 * Static (IN) parameters
	 */
	char		user_name[BASIL_STRING_MEDIUM],
			account_name[BASIL_STRING_MEDIUM],
			batch_id[BASIL_STRING_LONG];

	struct basil_rsvn_param *params;
};

/*
 * struct basil_parse_data  -  method-dependent data used during parsing
 *
 * @version:	which Basil version to use (IN)
 * @method:	the type of request issued (IN)
 *
 * @mdata:	method-dependent data (IN/OUT)
 * @inv:	containers for (full/counting) INVENTORY (OUT)
 * @res:	reservation parameters for RESERVE method (IN)
 * @raw:	typecast of mdata to check if parameters are present
 *
 * @msg:	method-dependent string on success, error string on failure (OUT)
 */
struct basil_parse_data {
	enum basil_version	version;
	enum basil_method	method;

	union {
		struct basil_inventory	 *inv;
		struct basil_reservation *res;
		uint8_t			 *raw;
	} mdata;

	char msg[BASIL_ERROR_BUFFER_SIZE];
};

/*
 * Mapping tables
 */
extern const char *bv_names[BV_MAX];
extern const char *bv_names_long[BV_MAX];
extern const char *bm_names[BM_MAX];
extern const char *be_names[BE_MAX];

extern const char *nam_arch[BNA_MAX];
extern const char *nam_memtype[BMT_MAX];
extern const char *nam_labeltype[BLT_MAX];
extern const char *nam_ldisp[BLD_MAX];

extern const char *nam_noderole[BNR_MAX];
extern const char *nam_nodestate[BNS_MAX];
extern const char *nam_proc[BPT_MAX];
extern const char *nam_rsvn_mode[BRM_MAX];
extern const char *nam_gpc_mode[BGM_MAX];

/**
 * struct nodespec  -  representation of node ranges
 * @start: start value of the range
 * @end:   end value of the range (may equal @start)
 * @next:  next element ns such that ns.start > this.end
 */
struct nodespec {
	uint32_t	start;
	uint32_t	end;

	struct nodespec *next;
};

extern int ns_add_node(struct nodespec **head, uint32_t node_id);
extern char *ns_to_string(const struct nodespec *head);
extern void free_nodespec(struct nodespec *head);

#ifdef HAVE_CRAY
/*
 *	Routines to interact with SDB database (uses prepared statements)
 */
/** Connect to the XTAdmin table on the SDB */
extern MYSQL *cray_connect_sdb(void);

/** Initialize and prepare statement */
extern MYSQL_STMT *prepare_stmt(MYSQL *handle, const char *query,
				MYSQL_BIND bind_parm[], unsigned long nparams,
				MYSQL_BIND bind_cols[], unsigned long ncols);

/** Execute and return the number of rows. */
extern int exec_stmt(MYSQL_STMT *stmt, const char *query,
		     MYSQL_BIND bind_col[], unsigned long ncols);

/**
 * Fetch the next row of data;
 */
int fetch_stmt(MYSQL_STMT *stmt);

/* Free memory associated with data retrieved by fetch_stmt() */
my_bool free_stmt_result(MYSQL_STMT *stmt);

/* Free memory associated with data generated by prepare_stmt() */
my_bool stmt_close(MYSQL_STMT *stmt);

/* Free memory associated with data generated by cray_connect_sdb() */
void cray_close_sdb(MYSQL *handle);

/** Find out interconnect chip: Gemini (XE) or SeaStar (XT) */
extern int cray_is_gemini_system(MYSQL *handle);
#endif  /* HAVE_CRAY */


/*
 *	Basil XML-RPC API prototypes
 */
extern enum basil_version get_basil_version(void);
extern int basil_request(struct basil_parse_data *bp);

extern struct basil_inventory *get_full_inventory(enum basil_version version);
extern void   free_inv(struct basil_inventory *inv);

extern long basil_reserve(const char *user, const char *batch_id,
			  uint32_t width, uint32_t depth, uint32_t nppn,
			  uint32_t mem_mb, struct nodespec *ns_head);
extern int basil_confirm(uint32_t rsvn_id, int job_id, uint64_t pagg_id);
extern int basil_release(uint32_t rsvn_id);

#endif /* __BASIL_ALPS_H__ */
