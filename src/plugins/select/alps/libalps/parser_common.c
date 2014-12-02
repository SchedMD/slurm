/*
 * Routines and data structures common to all BASIL versions
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#include "parser_internal.h"
#include "../parser_common.h"

const char *bv_names[BV_MAX];
const char *bv_names_long[BV_MAX];
const char *bm_names[BM_MAX];
const char *be_names[BE_MAX];

const char *nam_arch[BNA_MAX];
const char *nam_memtype[BMT_MAX];
const char *nam_labeltype[BLT_MAX];
const char *nam_ldisp[BLD_MAX];

const char *nam_noderole[BNR_MAX];
const char *nam_nodestate[BNS_MAX];
const char *nam_proc[BPT_MAX];
const char *nam_rsvn_mode[BRM_MAX];
const char *nam_gpc_mode[BGM_MAX];

const char *nam_gpc_mode[BGM_MAX];
const char *nam_rsvn_mode[BRM_MAX];

const char *nam_acceltype[BA_MAX];
const char *nam_accelstate[BAS_MAX];

bool node_rank_inv = 0;

/*
 *	General-purpose routines
 */
/** Decode (negative) error code following a Basil response. */
const char *basil_strerror(int rc)
{
	return be_names_long[decode_basil_error(rc)];
}

/*
 * Overwrite @reqc attribute keys supplied in @reqv with corresponding
 * attribute value from @attr_list.
 */
void extract_attributes(const XML_Char **attr_list, char **reqv, int reqc)
{
	const XML_Char **attr, *val = NULL;

	while (--reqc >= 0) {
		for (attr = attr_list, val = NULL; *attr; attr += 2) {
			if (strcmp(reqv[reqc], *attr) == 0) {
				if (val != NULL)
					fatal("multiple '%s' occurrences",
					      *attr);
				val = attr[1];
			}
		}
		if (val == NULL)
			fatal("unspecified '%s' attribute", reqv[reqc]);
		reqv[reqc] = (XML_Char *)val;
		val = NULL;
	}
}

/*
 *	XML Handlers
 */

/** Generic 'Message' element */
void eh_message(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "severity" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	/* Message appears within ResponseData, which may set ud->error */
	if (ud->error == BE_NONE)
		snprintf(ud->bp->msg, sizeof(ud->bp->msg), "%s: ", attribs[0]);
}

/** Generic 'BasilResponse' element */
void eh_response(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "protocol" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));
	/*
	 * When the method call failed (ResponseData with status="FAILURE"),
	 * it can happen that ALPS sets the 'protocol' to the empty string ("").
	 */
	if (*attribs[0] && strcmp(attribs[0], bv_names[ud->bp->version]) != 0)
		fatal("Version mismatch: expected %s, but got %s",
		      bv_names[ud->bp->version], attribs[0]);
}

/** Generic 'ResponseData' element */
void eh_resp_data(struct ud *ud, const XML_Char **attrs)
{
	char *attr_std[] = { "method", "status" };

	extract_attributes(attrs, attr_std, ARRAY_SIZE(attr_std));

	if (strcmp(attr_std[1], "SUCCESS") == 0) {
		ud->error = BE_NONE;
		/*
		 * When the method call failed, ALPS in some cases sets the
		 * 'method' to "UNDEFINED", hence verify this on success only.
		 */
		if (strcmp(attr_std[0], bm_names[ud->bp->method]) != 0)
			fatal("method mismatch in=%s, out=%s",
			      bm_names[ud->bp->method], attr_std[0]);
	} else {
		char *attr_err[] = { "error_source", "error_class" };

		extract_attributes(attrs, attr_err, ARRAY_SIZE(attr_err));

		for (ud->error = BE_INTERNAL;
		     ud->error < BE_UNKNOWN; ud->error++) {
			if (strcmp(attr_err[0], be_names[ud->error]) == 0)
				break;
		}
		snprintf(ud->bp->msg, sizeof(ud->bp->msg), "%s ALPS %s error: ",
			 attr_err[1], be_names[ud->error]);

		if (strcmp(attr_err[1], "TRANSIENT") == 0)
			ud->error |= BE_TRANSIENT;
	}
}

/** Basil 1.0/1.1/3.1 'Reserved' element */
void eh_reserved(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "reservation_id" };
	/*
	 * The Catamount 'admin_cookie' and 'alloc_cookie' attributes
	 * have been deprecated starting from Basil 1.1.
	 */
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &ud->bp->mdata.res->rsvn_id) < 0)
		fatal("illegal reservation_id = %s", attribs[0]);

	ud->counter[BT_RESVDNODEARRAY] = 0;	/* Basil 3.1 */
}

/** Basil 1.0/1.1 'Engine' element */
void eh_engine(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "name", "version" };
	/*
	 * Basil 3.1 has an additional attribute 'basil_support' which
	 * contains a comma-separated list of supported Basil versions.
	 */
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (strcmp(attribs[0], "ALPS") != 0)
		fatal("unknown engine name '%s'", attribs[0]);
	strncpy(ud->bp->msg, attribs[1], sizeof(ud->bp->msg));
}

/** Basil 1.0/1.1 'Node' element  */
void eh_node(struct ud *ud, const XML_Char **attrs)
{
	struct basil_node node = {0};
	char *attribs[] = { "node_id", "name", "architecture",
			    "role", "state" };
	/*
	 * Basil 3.1 in addition has a 'router_id' attribute.
	 */
	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &node.node_id) < 0)
		fatal("illegal node_id = %s", attribs[0]);

	strncpy(node.name, attribs[1], sizeof(node.name));

	for (node.arch = BNA_X2; node.arch < BNA_MAX; node.arch++) {
		if (strcmp(attribs[2], nam_arch[node.arch]) == 0)
			break;
	}
	for (node.role = BNR_INTER; node.role < BNR_MAX; node.role++) {
		if (strcmp(attribs[3], nam_noderole[node.role]) == 0)
			break;
	}
	for (node.state = BNS_UP; node.state < BNS_MAX; node.state++) {
		if (strcmp(attribs[4], nam_nodestate[node.state]) == 0)
			break;
	}
	ud->current_node.available = node.arch == BNA_XT &&
				     node.role == BNR_BATCH &&
				     node.state == BNS_UP;
	ud->current_node.reserved  = false;

	if (ud->ud_inventory) {
		struct basil_node *new = xmalloc(sizeof(*new));

		*new = node;
		if (ud->ud_inventory->node_head)
			new->next = ud->ud_inventory->node_head;
		ud->ud_inventory->node_head = new;
	}

	if ( ud->bp->version < BV_5_1 )
		ud->counter[BT_SEGMARRAY]  = 0;
	else
		ud->counter[BT_SOCKARRAY]  = 0;

	ud->counter[BT_ACCELARRAY] = 0;

	/* Cover up Basil version differences by faking a segment. */
	if (ud->bp->version < BV_1_1)
		eh_segment(ud, NULL);
}

/** Basil 1.1/3.1 'Segment' element */
void eh_segment(struct ud *ud, const XML_Char **attrs)
{
	uint32_t ordinal = 0;
	char *attribs[] = { "ordinal" };

	if (attrs) {
		extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));
		if (atou32(attribs[0], &ordinal) < 0)
			fatal("illegal segment ordinal = %s", attribs[0]);
	}

	if (ud->ud_inventory) {
		struct basil_segment *new = xmalloc(sizeof(*new));

		new->ordinal = ordinal;
		xassert(ud->ud_inventory->node_head);

		if (ud->ud_inventory->node_head->seg_head)
			new->next = ud->ud_inventory->node_head->seg_head;
		ud->ud_inventory->node_head->seg_head = new;
	}

	if ( ud->bp->version < BV_5_1 )
		ud->counter[BT_PROCARRAY]  = 0;
	else
		ud->counter[BT_COMUARRAY]  = 0;

	ud->counter[BT_MEMARRAY]   = 0;
	ud->counter[BT_LABELARRAY] = 0;
}

/** Generic 'Processor' element */
void eh_proc(struct ud *ud, const XML_Char **attrs)
{
	struct basil_node_processor proc = {0};
	char *attribs[] = { "ordinal", "architecture", "clock_mhz" };

	if ( ud->bp->version < BV_5_1 )
		extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));
	else
		extract_attributes(attrs, attribs, 1);

	if (atou32(attribs[0], &proc.ordinal) < 0)
		fatal("illegal ordinal = %s", attribs[0]);

	if ( ud->bp->version < BV_5_1 ) {
		for (proc.arch = BPT_X86_64; proc.arch < BPT_MAX; proc.arch++)
			if (strcmp(attribs[1], nam_proc[proc.arch]) == 0)
				break;

		if (atou32(attribs[2], &proc.clock_mhz) < 0)
			fatal("illegal clock_mhz = %s", attribs[2]);
	}

	if (ud->ud_inventory) {
		struct basil_node_processor *new = xmalloc(sizeof(*new));

		*new = proc;
		xassert(ud->ud_inventory->node_head);
		xassert(ud->ud_inventory->node_head->seg_head);

		if (node_rank_inv)
			ud->ud_inventory->node_head->cpu_count++;

		if (ud->ud_inventory->node_head->seg_head->proc_head)
			new->next = ud->ud_inventory->node_head->
				seg_head->proc_head;
		ud->ud_inventory->node_head->seg_head->proc_head = new;
	}
}

/** Generic 'ProcessorAllocation' element */
void eh_proc_alloc(struct ud *ud, const XML_Char **attrs)
{
	uint32_t rsvn_id;
	char *attribs[] = { "reservation_id" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &rsvn_id) < 0)
		fatal("illegal reservation_id = %s", attribs[0]);

	/* A node is "reserved" if at has at least one allocation */
	ud->current_node.reserved = true;

	if (ud->ud_inventory) {
		xassert(ud->ud_inventory->node_head);
		xassert(ud->ud_inventory->node_head->seg_head);
		xassert(ud->ud_inventory->node_head->seg_head->proc_head);

		ud->ud_inventory->node_head->seg_head->proc_head->rsvn_id =
			rsvn_id;
	}
}

/** Generic 'Memory' element */
void eh_mem(struct ud *ud, const XML_Char **attrs)
{
	struct basil_node_memory memory = {0};
	char *attribs[] = { "type", "page_size_kb", "page_count" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	for (memory.type = BMT_OS; memory.type < BMT_MAX; memory.type++) {
		if (strcmp(attribs[0], nam_memtype[memory.type]) == 0)
			break;
	}
	if (atou32(attribs[1], &memory.page_size_kb) < 0 ||
	    memory.page_size_kb < 1)
		fatal("illegal page_size_kb = %s", attribs[1]);

	if (atou32(attribs[2], &memory.page_count) < 0 ||
	    memory.page_count < 1)
		fatal("illegal page_count = %s", attribs[2]);

	if (ud->ud_inventory) {
		struct basil_node_memory *new = xmalloc(sizeof(*new));

		*new = memory;
		xassert(ud->ud_inventory->node_head);
		xassert(ud->ud_inventory->node_head->seg_head);

		if (node_rank_inv)
			ud->ud_inventory->node_head->mem_size +=
				(memory.page_size_kb * memory.page_count)
				/ 1024;

		if (ud->ud_inventory->node_head->seg_head->mem_head)
			new->next = ud->ud_inventory->node_head->
				seg_head->mem_head;
		ud->ud_inventory->node_head->seg_head->mem_head = new;
	}
}

/** Generic 'MemoryAllocation' element */
void eh_mem_alloc(struct ud *ud, const XML_Char **attrs)
{
	struct basil_mem_alloc memalloc = {0};
	char *attribs[] = { "reservation_id", "page_count" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &memalloc.rsvn_id) < 0)
		fatal("illegal reservation_id = %s", attribs[0]);

	if (atou32(attribs[1], &memalloc.page_count) < 0)
		fatal("illegal page_count = %s", attribs[1]);

	ud->current_node.reserved = true;

	if (ud->ud_inventory) {
		struct basil_mem_alloc *new = xmalloc(sizeof(*new));

		*new = memalloc;
		xassert(ud->ud_inventory->node_head);
		xassert(ud->ud_inventory->node_head->seg_head);
		xassert(ud->ud_inventory->node_head->seg_head->mem_head);

		if (ud->ud_inventory->node_head->seg_head->mem_head->a_head)
			new->next = ud->ud_inventory->node_head->
				seg_head->mem_head->a_head;
		ud->ud_inventory->node_head->seg_head->mem_head->a_head = new;
	}
}

/** Generic 'Label' element */
void eh_label(struct ud *ud, const XML_Char **attrs)
{
	struct basil_label label = {0};
	char *attribs[] = { "name", "type", "disposition" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	strncpy(label.name, attribs[0], sizeof(label.name));

	for (label.type = BLT_HARD; label.type < BLT_MAX; label.type++) {
		if (strcmp(attribs[1], nam_labeltype[label.type]) == 0)
			break;
	}
	for (label.disp = BLD_ATTRACT; label.disp < BLD_MAX; label.disp++) {
		if (strcmp(attribs[2], nam_ldisp[label.disp]) == 0)
			break;
	}

	if (ud->ud_inventory) {
		struct basil_label *new = xmalloc(sizeof(*new));

		*new = label;
		xassert(ud->ud_inventory->node_head);
		xassert(ud->ud_inventory->node_head->seg_head);

		if (ud->ud_inventory->node_head->seg_head->lbl_head)
			new->next = ud->ud_inventory->node_head->
				seg_head->lbl_head;
		ud->ud_inventory->node_head->seg_head->lbl_head = new;
	}
}

/** Basil 1.0 'Reservation' element (1.1 and 3.1 have additional attributes). */
void eh_resv(struct ud *ud, const XML_Char **attrs)
{
	uint32_t rsvn_id;
	char *attribs[] = { "reservation_id", "user_name", "account_name" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (atou32(attribs[0], &rsvn_id) < 0)
		fatal("illegal reservation_id '%s'", attribs[0]);

	if (ud->ud_inventory) {
		struct basil_rsvn *new = xmalloc(sizeof(*new));

		new->rsvn_id = rsvn_id;
		strncpy(new->user_name, attribs[1], sizeof(new->user_name));
		strncpy(new->account_name, attribs[2],
			sizeof(new->account_name));

		if (ud->ud_inventory->rsvn_head)
			new->next = ud->ud_inventory->rsvn_head;
		ud->ud_inventory->rsvn_head = new;
	}

	ud->counter[BT_APPARRAY] = 0; /* Basil 3.1 */
}

/** Basil 1.1/3.1 'Application' element */
void eh_application(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "application_id", "user_id", "group_id",
			    "time_stamp" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (ud->ud_inventory) {
		struct basil_rsvn_app *new = xmalloc(sizeof(*new));

		if (atou64(attribs[0], &new->apid) < 0)
			fatal("invalid application_id '%s'", attribs[0]);
		else if (atou32(attribs[1], &new->user_id) < 0)
			fatal("invalid user_id '%s'", attribs[1]);
		else if (atou32(attribs[2], &new->group_id) < 0)
			fatal("invalid group_id '%s'", attribs[2]);
		else if (atotime_t(attribs[3], &new->timestamp) < 0)
			fatal("invalid time_stamp '%s'", attribs[3]);

		xassert(ud->ud_inventory->rsvn_head);
		if (ud->ud_inventory->rsvn_head->app_head)
			new->next = ud->ud_inventory->rsvn_head->app_head;
		ud->ud_inventory->rsvn_head->app_head = new;
	}

	ud->counter[BT_CMDARRAY] = 0;
}

/** Basil 1.1/3.1 'Command' element */
void eh_command(struct ud *ud, const XML_Char **attrs)
{
	char *attribs[] = { "width", "depth", "nppn", "memory",
			    "architecture", "cmd" };

	extract_attributes(attrs, attribs, ARRAY_SIZE(attribs));

	if (ud->ud_inventory) {
		struct basil_rsvn_app_cmd *new = xmalloc(sizeof(*new));

		xassert(ud->ud_inventory->rsvn_head);
		xassert(ud->ud_inventory->rsvn_head->app_head);

		if (atou32(attribs[0], &new->width) < 0)
			fatal("invalid width '%s'", attribs[0]);
		else if (atou32(attribs[1], &new->depth) < 0)
			fatal("invalid depth '%s'", attribs[1]);
		else if (atou32(attribs[2], &new->nppn) < 0)
			fatal("invalid nppn '%s'", attribs[2]);
		else if (atou32(attribs[3], &new->memory) < 0)
			fatal("invalid memory '%s'", attribs[3]);
		for (new->arch = BNA_X2; new->arch < BNA_MAX; new->arch += 1)
			if (strcmp(attribs[4], nam_arch[new->arch]) == 0)
				break;
		strncpy(new->cmd, attribs[5], sizeof(new->cmd));

		if (ud->ud_inventory->rsvn_head->app_head->cmd_head)
			new->next = ud->ud_inventory->rsvn_head->
				app_head->cmd_head;
		ud->ud_inventory->rsvn_head->app_head->cmd_head = new;
	}
}

/*
 *	Top-Level Handlers
 */
static const struct element_handler *basil_tables[BV_MAX] = {
	[BV_1_0] = basil_1_0_elements,
	[BV_1_1] = basil_1_1_elements,
	[BV_1_2] = basil_1_1_elements,		/* Basil 1.2 behaves like 1.1 */
	[BV_3_1] = basil_3_1_elements,
	[BV_4_0] = basil_4_0_elements,
	[BV_4_1] = basil_4_0_elements,
	[BV_5_0] = basil_4_0_elements,
	[BV_5_1] = basil_5_1_elements,
	[BV_5_2] = basil_5_2_elements,
	[BV_5_2_3] = basil_5_2_elements
};

/**
 * tag_to_method - Look up Basil method by tag.
 * NOTE: This must be kept in synch with the order in %basil_element!
 */
static enum basil_method _tag_to_method(const enum basil_element tag)
{
	switch (tag) {
	case BT_MESSAGE ... BT_RESPDATA:	/* generic, no method */
		return BM_none;
	case BT_RESVDNODEARRAY ... BT_RESVDNODE:/* RESERVE, Basil >= 3.1 */
	case BT_RESERVED:			/* RESERVE, Basil >= 1.0 */
		return BM_reserve;
	case BT_CONFIRMED:
		return BM_confirm;
	case BT_RELEASED:
		return BM_release;
	case BT_ENGINE:
		return BM_engine;
	case BT_ACCELARRAY ... BT_ACCELALLOC:	/* INVENTORY, Basil >= 4.0 */
	case BT_SEGMARRAY ... BT_COMMAND:	/* INVENTORY, Basil >= 1.1 */
	case BT_INVENTORY ... BT_RESVN:		/* INVENTORY, Basil >= 1.0 */
		return BM_inventory;
	case BT_SWITCH ... BT_SWITCHAPPARRAY:
		return BM_switch;
	case BT_SOCKARRAY:			/* INVENTORY, Basil >= 5.1.0/1.3 */
	case BT_COMUARRAY:			/* INVENTORY, Basil >= 5.1.0/1.3 */
		return BM_none;
	case BT_SOCKET:				/* INVENTORY, Basil >= 5.1.0/1.3 */
	case BT_COMPUNIT:			/* INVENTORY, Basil >= 5.1.0/1.3 */
		return BM_inventory;
	default:
		return BM_UNKNOWN;
	}
}

static void _start_handler(void *user_data,
			   const XML_Char *el, const XML_Char **attrs)
{
	struct ud *ud = user_data;
	const struct element_handler *table = basil_tables[ud->bp->version];
	enum basil_method method;
	enum basil_element tag;

	for (tag = BT_MESSAGE; tag < BT_MAX; tag++) {
		if (table[tag].tag) {
			if (strcmp(table[tag].tag, el) == 0) {
				/* since BM_inventory is returned for Arrays
				   if the method is switch we need to "switch"
				   it up here.
				*/
				if (ud->bp->method == BM_switch) {
					if (!strcmp(table[tag].tag,
						    "ReservationArray"))
						tag = BT_SWITCHRESARRAY;
					else if (!strcmp(table[tag].tag,
							 "Reservation"))
						tag = BT_SWITCHRES;
					else if (!strcmp(table[tag].tag,
							 "ApplicationArray"))
						tag = BT_SWITCHAPPARRAY;
					else if (!strcmp(table[tag].tag,
							 "Application"))
						tag = BT_SWITCHAPP;
				}
				break;
			}
		}
	}
	if (table[tag].tag == NULL)
		fatal("Unrecognized XML start tag '%s'", el);

	method = _tag_to_method(tag);
	if (method == BM_UNKNOWN)
		fatal("Unsupported XML start tag '%s'", el);

	if (method != BM_none && method != ud->bp->method)
		fatal("Unexpected '%s' start tag within %u response, "
		      "expected %u", el, method, ud->bp->method);

	if (tag != BT_MESSAGE) {
		if (ud->depth != table[tag].depth)
			fatal("Tag '%s' appeared at depth %d instead of %d",
			      el, ud->depth, table[tag].depth);
		if (ud->counter[tag] && table[tag].uniq)
			fatal("Multiple occurrences of %s in document", el);
	}

	if (ud->depth == TAG_DEPTH_MAX)
		fatal("BUG: maximum tag depth reached");
	ud->stack[ud->depth] = tag;
	ud->counter[tag]++;

	if (table[tag].hnd == NULL && *attrs != NULL)
		fatal("Unexpected attribute '%s' in %s", *attrs, el);
	else if (table[tag].hnd != NULL && *attrs == NULL)
		fatal("Tag %s without expected attributes", el);
	else if (table[tag].hnd != NULL)
		table[tag].hnd(ud, attrs);
	ud->depth++;
}

static void _end_handler(void *user_data, const XML_Char *el)
{
	struct ud *ud = user_data;
	const struct element_handler *table = basil_tables[ud->bp->version];
	enum basil_element end_tag;

	--ud->depth;

	for (end_tag = BT_MESSAGE; end_tag < BT_MAX; end_tag++) {
		if (table[end_tag].tag) {
			if (strcmp(table[end_tag].tag, el) == 0) {
				/* since BM_inventory is returned for Arrays
				   if the method is switch we need to "switch"
				   it up here.
				*/
				if (ud->bp->method == BM_switch) {
					if (!strcmp(table[end_tag].tag,
						    "ReservationArray"))
						end_tag = BT_SWITCHRESARRAY;
					else if (!strcmp(table[end_tag].tag,
							 "Reservation"))
						end_tag = BT_SWITCHRES;
					else if (!strcmp(table[end_tag].tag,
							 "ApplicationArray"))
						end_tag = BT_SWITCHAPPARRAY;
					else if (!strcmp(table[end_tag].tag,
							 "Application"))
						end_tag = BT_SWITCHAPP;
				}
				break;
			}
		}
	}
	if (table[end_tag].tag == NULL) {
		fatal("Unknown end tag '%s'", el);
	} else if (end_tag != ud->stack[ud->depth]) {
		fatal("Non-matching end element '%s'", el);
	} else if (end_tag == BT_NODE) {
		if (ud->current_node.reserved) {
			ud->bp->mdata.inv->batch_total++;
		} else if (ud->current_node.available) {
			ud->bp->mdata.inv->batch_avail++;
			ud->bp->mdata.inv->batch_total++;
		}
		ud->bp->mdata.inv->nodes_total++;
	} else if (end_tag == BT_RESPDATA && ud->error) {
		/*
		 * Re-classify errors. The error message has been added by the
		 * cdata handler nested inside the ResponseData tags.
		 *
		 * Match substrings that are common to all Basil versions:
		 * - the ' No entry for resId ' string is returned when calling
		 *   the RELEASE method multiple times;
		 * - the ' cannot find resId ' string is returned when trying to
		 *   confirm a reservation which does not or no longer exist.
		 */
		if (strstr(ud->bp->msg, " No entry for resId ") ||
		    strstr(ud->bp->msg, " cannot find resId "))
			ud->error = BE_NO_RESID;
	}
}

static void _cdata_handler(void *user_data, const XML_Char *s, int len)
{
	struct ud *ud = user_data;
	size_t mrest;

	if (!ud->depth || ud->stack[ud->depth - 1] != BT_MESSAGE)
		return;

	while (isspace(*s))
		++s, --len;

	mrest = sizeof(ud->bp->msg) - (strlen(ud->bp->msg) + 1);
	if (mrest > 0)
		strncat(ud->bp->msg, s, len > mrest ? mrest : len);
}

/*
 * parse_basil  -  parse the response to a Basil query (version-independent)
 *
 * @bp:	information passed in to guide the parsing process
 * @fd: file descriptor connected to the output of apbasil
 * Returns 0 if ok, negative %basil_error otherwise.
 */
int parse_basil(struct basil_parse_data *bp, int fd)
{
	char xmlbuf[65536];
	struct ud ud = {0};
	XML_Parser parser;
	int len;

	/* Almost all methods require method-specific data in mdata */
	xassert(bp->method == BM_engine || bp->mdata.raw != NULL);
	ud.bp = bp;

	parser = XML_ParserCreate("US-ASCII");
	if (parser == NULL)
		fatal("can not allocate memory for parser");

	XML_SetUserData(parser, &ud);
	XML_SetElementHandler(parser, _start_handler, _end_handler);
	XML_SetCharacterDataHandler(parser, _cdata_handler);
	do {
		len = read(fd, xmlbuf, sizeof(xmlbuf));
		if (len == -1)
			fatal("read error on stream: len=%d", len);

		switch (XML_Parse(parser, xmlbuf, len, len == 0)) {
		case XML_STATUS_ERROR:
			xmlbuf[len] = '\0';
			snprintf(ud.bp->msg, sizeof(ud.bp->msg),
				 "Basil %s %s response parse error: %s "
				 "at line %lu: '%s'",
				 bv_names_long[bp->version],
				 bm_names[bp->method],
				 XML_ErrorString(XML_GetErrorCode(parser)),
				 XML_GetCurrentLineNumber(parser), xmlbuf);
			/* fall through */
		case XML_STATUS_SUSPENDED:
			ud.error = BE_PARSER;
			/* fall through */
		case XML_STATUS_OK:
			break;
		}
	} while (len && ud.error == BE_NONE);

	close(fd);
	XML_ParserFree(parser);

	switch (ud.error) {
	case BE_NO_RESID:	/* resId no longer exists */
	case BE_NONE:		/* no error: bp->msg is empty */
		break;
	default:
		if (is_transient_error(ud.error))
			debug("%s", bp->msg);
		else
			error("%s", bp->msg);
	}
	return -ud.error;
}
