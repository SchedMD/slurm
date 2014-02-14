/*****************************************************************************\
 *  smd_ns.h - Library for fault tolerant application support
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *  Written by David Bigagli <david@schedmd.com>
 *  All rights reserved
\*****************************************************************************/

#ifndef _HAVE_SMD_NS_H
#define _HAVE_SMD_NS_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif
#if HAVE_STDINT_H
#  include <stdint.h>		/* for uint16_t, uint32_t definitions */
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>		/* for uint16_t, uint32_t definitions */
#endif
#if HAVE_STDBOOL_H
#  include <stdbool.h>
#else
typedef enum {false, true} bool;
#endif /* !HAVE_STDBOOL_H */

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/time.h>

/* Protocol version
 */
#define NS_VERSION 10
#define ns_version "1.0"

/* Some handy defines
 */
#define freeit(p) {if (p) {free(p); p = NULL;}}
#define closeit(s) {if (s > 0) {close(s); s = 1;}}

/* Faulty can be in state FAILED or FAILING
 * these flags tell the controller which one
 * the caller is interested in.
 */
#define FAILED_NODES   (1 << 1)
#define FAILING_NODES  (1 << 2)

/* Request for failed or failing nodes.
 */
typedef struct faulty_node_request {
	uint32_t job_id;	/* job_id */
	uint16_t options;	/* failing failed or both */
} faulty_node_request_t;

typedef struct faulty_node_reply {
	uint32_t job_id;	/* job_id */
	int num;		/* number of data structures */
	struct node_state *nodes; /* array of node states */
} faulty_node_reply_t;

/* This data structure describes the state of a node
 * either failing or failed.
 */
typedef struct node_state {
	char *node_name; /* the name of the node */
	int cpu_cnt;	 /* number of cpus per host */
	int state;		 /* state of node: failed or failing */
} node_state_t;

/* Request to drain a node given
 * a specific reason.
 */
typedef struct drain_node_request {
	uint32_t job_id;	/* job_id */
	char *node;		/* failed node */
	char *reason;		/* reason to drain it */
	time_t timeout;		/* network call timeout */
} drain_node_request_t;

/* Drop node request data structure.
 */
typedef struct drop_node_request {
	uint32_t job_id;	/* job_id */
	char *node;		/* node to drop from the job */
} drop_node_request_t;

/* New job running environment. These information are
 * sent by the controller upon node replacement or
 * node drop.
 */
typedef struct new_node_set {
	char *new_nodelist;	/* new node list or NULL if unchanged */
	int new_node_cnt;	/* new node count or 0 if unchanged */
	char *new_cpus_per_node;/* new CPU count per node, NULL if unchanged */
} new_node_set_t;

/* Drop node reply from the controller.
 */
typedef struct drop_node_reply {
	uint32_t job_id;	/* job_id */
	char *node;		/* node to drop from the job */
	struct new_node_set new_set;
} drop_node_reply_t;

/* Replacement request.
 */
typedef struct replace_node_request {
	uint32_t job_id;	/* job_id */
	char *node;		/* node to replace or NULL if any */
} replace_node_request_t;

/* This is the reply for the request of node replacement of a failed node.
 * The node can be replaced right away or the server can reply when a new
 * one will be available.
 */
typedef struct replace_node_reply {
	uint32_t job_id;	/* job_id */
	char *failed_node;	/* node to replace or NULL if any */
	char *replacement_node;	/* replacement or NULL if none */
	time_t when_available;	/* time of availability */
	struct new_node_set new_set;
} replace_node_reply_t;

typedef struct job_time_extend_request {
	uint32_t job_id;	/* job_id */
	uint32_t minutes;	/* extended time request */
} job_time_extend_request_t;

/* This data structure holds the configuration
 * values, from the config file, of the fault
 * tolerant computing. The config file is in
 * Bourne shell format.
 */
typedef struct config_var {
	char *key;		/* key */
	char *val;		/* value key=value */
} config_var_t;

/* Key value pair data structure.
 */
typedef struct key_val {
	char *key;
	uint32_t val;
} key_val_t;


/* This data structure is the library representation
 * of the configuration file. Some parts of the
 * config are used by the library only.
 */
typedef struct nonstop_config {
	/* Library configuration
	 */
	char *conf_fname;	            /* path to nonstop.conf */
	char *control_addr;	            /* controller address */
	in_addr_t control_saddr;        /* IPv4 controller addr */
	char *backup_addr;	            /* backup controller */
	in_addr_t backup_saddr;	        /* backup IPv4 controller */
	uint16_t debug;		            /* debug library message */
	uint16_t port;		            /* controller's port */
	uint32_t read_timeout;	        /* library read() timeout */
	uint32_t write_timeout;	        /* library write() timeout */
	/* Server configuration
	 */
	char *hot_spare_count;		    /* hot spare configuration */
	uint32_t max_spare_node_count;	/* max number of available spares */
	uint16_t time_limit_delay;	    /* seconds max extra time for the job */
	uint16_t time_limit_drop;	    /* seconds max extra time for if node is dropped */
	uint16_t time_limit_extend;	    /* minutes time extend for each replaced node */
	char *user_drain_allow;		    /* users allowed to drain nodes */
	char *user_drain_deny;		    /* users denied to drain nodes */
} nonstop_config_t;

/* Wrap the job_id for which information are being
 * requested in a data structure.
 */
typedef struct job_nonstop_info_request {
	uint32_t job_id;
} job_nonstop_info_request_t;

/* Data structure holding the information about a specific
 * jobs as seen by the controller.
 */
typedef struct job_nonstop_info_reply {
	uint32_t job_id;		/* job_id */
	uint32_t failed_node_cnt;	/* how many nodes have failed */
	struct  node_state  *failed_nodes; /* array of failed nodes */
	uint32_t pending_job_delay;	/* expected delay to start pending job, secs */
	uint32_t pending_job_id;	/* pending job with replacement resources */
	char *pending_node_name;	/* pending job to replace this node */
	uint32_t replace_node_cnt;	/* how many nodes were replaced */
	uint32_t time_extend_avail;	/* by how much the job time was extended */
} job_nonstop_info_reply_t;

/* Library errors and their messages. These are the errors from
 * the library to the caller.
 */
typedef enum {
	ENSTOP_OK = 0,		    /* All right */
	ENSTOP_CONFIG = 1000,	/* Invalid configuration */
	ENSTOP_NETIO,		    /* Network I/O error with controller */
	ENSTOP_INVALCMD,	    /* Invalid command sent to controller */
	ENSTOP_UPNODE,		    /* Update node failed */
	ENSTOP_JOBID,		    /* Invalid job ID */
	ENSTOP_PORT,		    /* Invalid port */
	ENSTOP_UID,		        /* Invalid user ID */
	ENSTOP_JOBNOTRUN,	    /* Job is not running */
	ENSTOP_NOHOST,		    /* Specified host is not found */
	ENSTOP_NODENOTFAILED,	/* Node has not failed */
	ENSTOP_NODENOTINJOB,	/* Node has no CPUs to replace */
	ENSTOP_REPLACELATER,	/* Node replacement is available later */
	ENSTOP_REPLACEPENDING,	/* A previous replace request is pending. */
	ENSTOP_MAXSPARECOUNT,	/* Job has reached MaxSpareNodeCount limit. */
	ENSTOP_NODEREPLACEFAILED, /* Failed to replace the node. */
	ENSTOP_NOINCREASETIMELIMIT, /* Not eligible for time limit increase. */
	ENSTOP_TIMEOVERLIMIT,	    /* Requested time exceeed the limit. */
	ENSTOP_JOBUPDATE,	        /* Failed to update the job */
	ENSTOP_DECODEHEADER,	    /* Failed decoding header from the controller.*/
	ENSTOP_UNKWNCTRLMSG,	    /* Unknown message from controller.*/
	ENSTOP_PROTOCOL,	        /* Protocol error with controller. */
	ENSTOP_NONODEFAIL,	        /* Job has no failed nodes at this time. */
	ENSTOP_LASTERR		        /* This must alwasy be the last nserrno */
} nonstop_errno_t;

/* These are the events sent from slurm to the client that
 * has registered for any of these events.
 * We use define as user can subscribe to more than one
 * events.
 */
#define	SMD_EVENT_NODE_FAILED  (1 << 1)	/* node has failed */
#define SMD_EVENT_NODE_FAILING (1 << 2)	/* node failing can be drained */
#define	SMD_EVENT_NODE_REPLACE (1 << 3)	/* replacement ready */

/* Each event returns a data structure carrying
 * the event type and a pointer to the event
 * data structure.
 */
typedef struct node_event {
	int event_type;		/* one of the SMD_EVENTS above */
	void *event;		/* event data structure */
} node_event_t;

/* SMD_EVENT_NODE_FAILED or SMD_EVENT_NODE_FAILING
 * The representation on job execution hosts that
 * have failed or are failing.
 */
typedef struct failed_nodes {
	char *node_name;	/* faulty node name */
} failed_nodes_t;

/* SMD_EVENT_NODE_REPLACE
 * The representation of the replacement node.
 * A replacement node always substitutes a give
 * node.
 */
typedef struct replace_node {
	char *faulty_node;	/* faulty node name */
	char *replacement;	/* replacement of the faulty node */
	char *hostlist;		/* the new execution hostlist */
} replace_node_t;

/* SMD_EVENT_NODE_REPLACE_WAIT
 * This is the wait for replacement event. The wait_time
 * indicates how long the caller has to wait before
 * the event SMD_EVENT_NODE_REPLACE is sent from
 * the controller to the library.
 */
typedef struct replace_node_wait {
	char *faulty_node;	/* faulty node name */
	time_t wait_time;	/* time to wait for a replacement */
} replace_node_wait_t;

/*
 * This API returns a list of nodes that are failed
 * or failing based on the option.
 * IN - struct faulty_node_request
 * IN - option FAILED_NODES or/and FAILING_NODES
 *
 * OUT - struct faulty_node_reply
 *
 */
extern int smd_get_job_faulty_nodes(struct faulty_node_request *,
                                    struct faulty_node_reply *);

/* API to free the reply data structure.
 */
extern void smd_free_job_faulty_nodes_reply(struct faulty_node_reply *);

/*
 * Advise Slurm to drain specific node.
 * IN node_names - Names of node to drain. Hostlist expression can be used.
 * IN reason - Description of why the nodes should be drained
 * RET 0 on success or -1 on failure and set errno.
 */
extern int smd_drain_job_node(struct drain_node_request *);

/*
 * Return latest message string.
 * User must free response if not NULL.
 */
extern char *smd_nonstop_errstr(int errnum);

/* Free any memory allocated by libsmdns
 * (optional, but useful to test for memory leaks) */
extern void smd_nonstop_fini(void);

/*
 * Replace a failed or down node for the specified job
 * IN  Replace node request structure.
 * OUT Replace reply node structure
 * RET 0 on success or -1 on failure and set errno
 * NOTE: Caller should free strings returned in struct replace_node_reply
 */
extern int smd_replace_job_node(struct replace_node_request *,
                                struct replace_node_reply *);

/* Free the members of the reply structure.
 */
extern void smd_free_replace_job_node_reply(struct replace_node_reply *);
/*
 * Increase the time limit of a job
 * IN job_id - The job to modify
 * IN minutes - The number of minutes to add to a job's time limit,
 *		If zero, then allocate the maximum possible time limit increment
 * RET 0 on success or -1 on failure and set errno
 */
extern int smd_extend_job_time(struct job_time_extend_request *);

/* smd_register4event()
 *
 * Call the controller and notify it which events is
 * the current job_id interested in. The caller of this
 * function will be notified only in the events of interest.
 *
 * IN job_id - The job interested in receiving node event
 *             notifications.
 * RET - 0 = Ok, != 0 Not Ok
 *
 * Registering for event establish a contract with the controller.
 * Everytime a node of the registered job_id failes or become
 * failing the controller notifies the client. The controller
 * also attempts to find a replacement node as soon as possible
 * and notify the client about it either the replacement is
 * available immediately or the client is notified to wait up
 * to the TimeLimitExtend value configured in the nonstop.conf file.
 * If the replacement is not going to be available in the
 * the max configurable time the wait_time value will
 * be set to INFINITE.
 *
 */
extern int smd_register4nodevent(uint32_t job_id);

/* smd_wait4nodevent()
 *
 * Monitor the communication with slurm for the registered
 * events.
 * IN - job_id
 * IN - timeout specifying in milliseconds how long should
 *      the call wait for events.
 *      If slurm does not replay in the
 *      specified timeval the call has to be repeated.
 * OUT - The structure representing the event from slurm.
 * RET - The return semantic is identical to poll.
 *       On  success a positive number is returned a value of 0
 *       indicates that the call timed out as there are no events.
 *       On error, -1 is returned, and nserrno is set appropriately.
 */
extern int smd_wait4nodevent(uint32_t job_id,
                             struct node_event *ev,
                             int timeout);

/* Get the nonstop configuration from the controller.
 */
extern int smd_get_nonstopconfig(struct nonstop_config *config);

/* Free the dynamic members of the nonstop_config data structure.
 */
extern void smd_free_nonstop_config(struct nonstop_config *config);

/* Drop a failed node, keep the computation with fewer nodes.
 * IN - struct drop node request
 * OUT - 0 if no error or -1 if error. In case of error
 * examine the errno.
 * NOTE: Caller should free strings returned in struct drop_node_reply
 */
extern int smd_drop_job_node(struct drop_node_request *,
                             struct drop_node_reply *);

/* Free the drop job node reply data structure.
 */
extern void smd_free_drop_job_node_reply(struct drop_node_reply *);

/* Request information about failed or failing nodes
 * of a specific job.
 */
extern int smd_nonstop_get_failed_jobinfo(struct job_nonstop_info_request *,
                                          struct job_nonstop_info_reply *);

/* Free the data structure which stores the information
 * about job's failed or failing nodes.
 */
extern void smd_nonstop_free_failed_jobinfo(struct job_nonstop_info_reply *);

/* Free the node state data structure.
 */
extern void smd_free_node_state(struct node_state *);

/* Logging function writes an application message to the specified
 * file descriptor.
 */
extern void smd_log(FILE *fp, const char *fmt, ...);

/* Logging function writes the user specified message
 * preceded by the time stamp and the threadID.
 */
extern void smd_log_time(FILE *, const char *fmt, ...);

/* Writes in the user supplied buffer the current time plus microsendonds
 * and the caller's threadID.
 */
extern char *smd_time(char *);

/* smd_match_key()
 *
 * Match given key in the input line.
 */
extern int smd_match_key(char *line, struct config_var *var);

/* smd_get_tokens()
 *
 * Tokenize a strings that is separated by spaces.
 */
extern char *smd_get_token(char **);

/* These are tools used by the library
 */
struct list_ {
	struct list_ *forw;
	struct list_ *back;
	int num;
	char *name;
};

struct liste {
	struct list_ *forw;
	struct list_ *back;
	void *data;
};

#define LIST_NUM_ENTS(L) ((L)->num)

extern struct list_ *listmake(const char *);
extern int  listinsert(struct list_ *,
                       struct list_ *,
                       struct list_ *);
extern int listpush(struct list_ *,
                    struct list_ *);
extern int listenque(struct list_ *,
                     struct list_ *);
extern struct list_ * listrm(struct list_ *,
                             struct list_ *);
struct list_ *listpop(struct list_ *);
extern struct list_ *listdeque(struct list_ *);
extern void listfree(struct list_ *, void (*f)());
extern void list_element_free(struct liste *);
extern int  millisleep_(int);

#endif	/* _HAVE_SMD_NS_H */
