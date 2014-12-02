/*
 * Shared routines to parse XML from different BASIL versions
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Licensed under the GPLv2.
 */
#ifndef __PARSER_INTERNAL_H__
#define __PARSER_INTERNAL_H__

#include "../basil_alps.h"
#include <errno.h>

/*
 * struct ud - user data passed to XML element handlers
 * @stack:	tag stack
 * @depth:	tag stack pointer
 * @counter:	tag counter (enforce tag uniqueness)
 *
 * @error:	%basil_error error information
 * @bp:		combined input/output data
 */
struct ud {
	uint8_t			depth;
	enum basil_element	stack[TAG_DEPTH_MAX];
	uint8_t			counter[BT_MAX];
	uint32_t		error;

	struct	{
		bool	available;	/* arch=XT && role=BATCH && state=UP */
		bool	reserved;	/* at least 1 reservation on this node */
	} current_node;

	struct basil_parse_data		*bp;
#define ud_inventory			bp->mdata.inv->f
};

/*
 * Tag handler lookup
 *
 * @tag:	NULL-terminated tag name
 * @depth:	depth at which this tag expected (not valid for all tags)
 * @uniq:	whether @tag should be unique within document
 * @hnd:	attribute-parsing function
 */
struct element_handler {
	char	*tag;
	uint8_t	depth;
	bool	uniq;
	void	(*hnd)(struct ud *, const XML_Char **);
};

/* MACROS */
/* Taken from linux/kernel.h 2.6.33 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* parser_basil_x.y.c */
extern const struct element_handler basil_1_0_elements[];
extern const struct element_handler basil_1_1_elements[];
extern const struct element_handler basil_3_1_elements[];
extern const struct element_handler basil_4_0_elements[];
extern const struct element_handler basil_5_1_elements[];
extern struct element_handler basil_5_2_elements[];
/* atoul.c */
extern int atou64(const char *str, uint64_t *value);
extern int atou32(const char *str, uint32_t *value);
extern int atotime_t(const char *str, time_t *value);

/* popen2.c */
extern pid_t popen2(const char *path, int *in, int *out, bool no_stderr);
extern unsigned char wait_for_child(pid_t pid);

/*
 * parser_common.c
 */
extern int parse_basil(struct basil_parse_data *bp, int fd);
extern void extract_attributes(const XML_Char **attrs, char **reqv, int reqc);

/* Basil 1.0/1.1 common handlers */
extern void eh_message(struct ud *ud, const XML_Char **attrs);
extern void eh_response(struct ud *ud, const XML_Char **attrs);
extern void eh_resp_data(struct ud *ud, const XML_Char **attrs);
extern void eh_reserved(struct ud *ud, const XML_Char **attrs);
extern void eh_engine(struct ud *ud, const XML_Char **attrs);
extern void eh_node(struct ud *ud, const XML_Char **attrs);
extern void eh_proc(struct ud *ud, const XML_Char **attrs);
extern void eh_proc_alloc(struct ud *ud, const XML_Char **attrs);
extern void eh_mem(struct ud *ud, const XML_Char **attrs);
extern void eh_mem_alloc(struct ud *ud, const XML_Char **attrs);
extern void eh_label(struct ud *ud, const XML_Char **attrs);
extern void eh_resv(struct ud *ud, const XML_Char **attrs);

/* Basil 1.1/3.1 common handlers */
extern void eh_segment(struct ud *ud, const XML_Char **attrs);
extern void eh_application(struct ud *ud, const XML_Char **attrs);
extern void eh_command(struct ud *ud, const XML_Char **attrs);
extern void eh_resv_1_1(struct ud *ud, const XML_Char **attrs);

/* Basil 3.1 and above common handlers */
extern void eh_resvd_node(struct ud *ud, const XML_Char **attrs);
extern void eh_confirmed(struct ud *ud, const XML_Char **attrs);
extern void eh_released_3_1(struct ud *ud, const XML_Char **attrs);
extern void eh_engine_3_1(struct ud *ud, const XML_Char **attrs);
extern void eh_inventory_3_1(struct ud *ud, const XML_Char **attrs);
extern void eh_node_3_1(struct ud *ud, const XML_Char **attrs);
extern void eh_resv_3_1(struct ud *ud, const XML_Char **attrs);

/* Basil 4.0 and above common handlers */
extern void eh_released_4_0(struct ud *ud, const XML_Char **attrs);
extern void eh_node_array_4_0(struct ud *ud, const XML_Char **attrs);
extern void eh_accel(struct ud *ud, const XML_Char **attrs);
extern void eh_accel_alloc(struct ud *ud, const XML_Char **attrs);
extern void eh_switch_res(struct ud *ud, const XML_Char **attrs);
/* st on think the name of the following was a mistake, should be ...resv
   hence I added the new line after */
extern void eh_switch_resv(struct ud *ud, const XML_Char **attrs);
extern void eh_switch_app(struct ud *ud, const XML_Char **attrs);

/* Basil 5.1 and above comon handlers */
extern void eh_node_array_5_1(struct ud *ud, const XML_Char **attrs);
extern void eh_socket_5_1(struct ud *ud, const XML_Char **attrs);
extern void eh_compute_5_1(struct ud *ud, const XML_Char **attrs);

#endif /*__PARSER_INTERNAL_H__ */
