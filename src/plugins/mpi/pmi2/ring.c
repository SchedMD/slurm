/*****************************************************************************\
 **  ring.c - Implements logic for PMIX_Ring
 *****************************************************************************
 * Copyright (c) 2015, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-670614
 * All rights reserved.
 *
 * This file is part of Slurm, a resource management program.
 * For details, see <https://slurm.schedmd.com/>.
 * Please also read the included file: DISCLAIMER.
 * 
 * LLNL Preamble Notice
 *
 * A. This notice is required to be provided under our contract with
 * the U.S. Department of Energy (DOE). This work was produced at the
 * Lawrence Livermore National Laboratory under Contract No.
 * DE-AC52-07NA27344 with the DOE.
 *
 * B. Neither the United States Government nor Lawrence Livermore
 * National Security, LLC nor any of their employees, makes any
 * warranty, express or implied, or assumes any liability or
 * responsibility for the accuracy, completeness, or usefulness of
 * any information, apparatus, product, or process disclosed, or
 * represents that its use would not infringe privately-owned rights.
 *
 * C. Also, reference herein to any specific commercial products,
 * process, or services by trade name, trademark, manufacturer or
 * otherwise does not necessarily constitute or imply its endorsement,
 * recommendation, or favoring by the United States Government or
 * Lawrence Livermore National Security, LLC. The views and opinions
 * of authors expressed herein do not necessarily state or reflect
 * those of the United States Government or Lawrence Livermore
 * National Security, LLC, and shall not be used for advertising or
 * product endorsement purposes.
\*****************************************************************************/

/*
 * -----------------------------------------------------------
 * PMIX_Ring - execute ring exchange over processes in group
 *
 * Input Parameters:
 * + value    - input string
 * - maxvalue - max size of input and output strings
 *
 *  Output Parameters:
 *  + rank  - returns caller's rank within ring
 *  - ranks - returns number of procs within ring
 *  - left  - buffer to receive value provided by (rank - 1) % ranks
 *  - right - buffer to receive value provided by (rank + 1) % ranks
 *
 *  Return values:
 *  Returns 'MPI_SUCCESS' on success and an MPI error code on failure.
 *
 *  Notes:
 *  This function is collective, but not necessarily synchronous,
 *  across all processes in the process group to which the calling
 *  process belongs.  All processes in the group must call this
 *  function, but a process may return before all processes have called
 *  the function.
 *
 * int PMIX_Ring(const char value[], int *rank, int *ranks, char left[], char right[], int maxvalue);
 * -----------------------------------------------------------
 *
 * For details on why this function is useful, see:
 *
 *   "PMI Extensions for Scalable MPI Startup",
 *   S. Chakrborty, H. Subramoni, J. Perkins, A. Moody,
 *   M. Arnold, and D. K. Panda, EuroMPI/ASIA 2014
 *
 * Here, PMIX_Ring is implemented as scan over the stepd tree.
 * Each application process sends a RING_IN message containing count,
 * left, and right values to its host stepd.  For this initial message,
 * count = 1 and left = right = input value provided by the app process.
 * After a stepd has received messages from all local tasks and all of
 * its stepd children (if any), it summarizes data received from all
 * procs and sends a RING_IN message up to its parent.
 *
 * When the root of the tree receives RING_IN messages from all
 * children, it computes and sends a custom RING_OUT message back to
 * each child.
 *
 * Upon receiving a RING_OUT message from its parent, a stepd computes
 * and sends a custom RING_OUT message to each of its children stepds
 * (if any) as well as responses to each application process.
 *
 * Each stepd process records the message received from each child
 * during the RING_IN phase, and it uses this data along with the
 * RING_OUT message from its parent to compute messages to send to its
 * children during the RING_OUT phase.
 *
 * With this algorithm, application processes on the same node are
 * assigned as consecutive ranks in the ring, and all processes within
 * a subtree are assigned as consecutive ranks within the ring.
 *
 * Going up the tree, the RING_IN message specifies the following:
 *   count - sum of app processes in subtree
 *   left  - left value from leftmost app process in subtree
 *   right - right value from rightmost app process in subtree
 *
 * Coming down the tree, the RING_OUT message species the following:
 *   count - rank to assign to leftmost app process in subtree
 *   left  - left value for leftmost app process in subtree
 *   right - right value for rightmost app process in subtree
 */

#include <stdlib.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#include "pmi.h"
#include "client.h"
#include "setup.h"
#include "tree.h"
#include "ring.h"

/* max number of times to retry sending to stepd before giving up */
#define MAX_RETRIES 5

/* tracks values received from child in pmix_ring_in message */
typedef struct {
    int count;   /* count received from child */
    char* left;  /* left value from child (strdup'd) */
    char* right; /* right value from child (strdup'd) */
} pmix_ring_msg;

/* we record one pmix_ring_msg structure for each child */
static pmix_ring_msg* pmix_ring_msgs = NULL;

/* tracks number of pmix_ring_in messages we've received,
 * we increment this count on each pmix_ring_in message,
 * and compose a message to our parent when it reaches
 * pmix_ring_children */
static int pmix_ring_count = 0;

/* tracks number of chilren we have for pmix_ring operation
 * (sum of application children and stepd children) */
static int pmix_ring_children = 0;

/* number of application processes */
static int pmix_app_children = 0;

/* our rank within stepd tree */
static int pmix_stepd_rank = -1;

/* number of procs in stepd tree */
static int pmix_stepd_ranks = 0;

/* degree k of k-ary stepd tree */
static int pmix_stepd_width = 16;

/* number of stepd children for this proc */
static int pmix_stepd_children = 0;

/* we allocate a hostlist in init and destroy it in finalize */
static hostlist_t pmix_stepd_hostlist = NULL;

/* return rank of our parent in stepd tree,
 * returns -1 if we're the root */
static int pmix_stepd_rank_parent()
{
	int rank = -1;
	if (pmix_stepd_rank > 0) {
        	rank = (pmix_stepd_rank - 1) / pmix_stepd_width;
        }
	return rank;
}

/* given a child index from 0..(pmix_stepd_children-1)
 * return rank of child in stepd tree */
static int pmix_stepd_rank_child(int i)
{
	int rank = pmix_stepd_rank * pmix_stepd_width + (i + 1);
	return rank;
}

/* given a global rank in stepd tree for message received
 * from one of our stepd children, compute its corresponding
 * ring_id, returns -1 if rank is not a child */
int pmix_ring_id_by_rank(int rank)
{
	/* compute the rank of our first child */
	int min_child = pmix_stepd_rank * pmix_stepd_width + 1;

	/* compute offset from this first child */
	int ring_id = rank - min_child;

	/* check that child is within range */
        if (rank >= min_child && ring_id < pmix_stepd_children) {
        	/* child is in range, add in local tasks */
        	ring_id += pmix_app_children;
        } else {
        	/* child is out of range */
        	ring_id = -1;
        }
	return ring_id;
}

/* send message defined by buf and size to given rank stepd */
static int pmix_stepd_send(const char* buf, uint32_t size, int rank)
{
	int rc = SLURM_SUCCESS;

	/* map rank to host name */
	char* host = hostlist_nth(pmix_stepd_hostlist, rank); /* strdup-ed */

	/* delay to sleep between retries in seconds,
	 * if there are multiple retires, we'll grow this delay
          * using exponential backoff, doubling it each time */
	unsigned int delay = 1;

	/* we'll try multiple times to send message to stepd,
	 * we retry in case stepd is just slow to get started */
	int retries = 0;
	while (1) {
		/* attempt to send message */
		rc = slurm_forward_data(&host, tree_sock_addr, size, buf);
		if (rc == SLURM_SUCCESS) {
			/* message sent successfully, we're done */
			break;
		}

		/* check whether we've exceeded our retry count */
		retries++;
		if (retries >= MAX_RETRIES) {
			/* cancel the step to avoid tasks hang */
			slurm_kill_job_step(job_info.jobid, job_info.stepid,
					    SIGKILL);
		}

		/* didn't succeeded, but we'll retry again,
		 * sleep for a bit first */
		sleep(delay);
		delay *= 2;
	}

	/* free host name */
	free(host); /* strdup-ed */

	return rc;
} 

/* allocate resources to track PMIX_Ring state */
int pmix_ring_init(const pmi2_job_info_t* job, char*** env)
{
	int i;
	int rc = SLURM_SUCCESS;

	/* this is called by each stepd process, and each stepd has
	 * at least one application process, so
	 * pmix_app_children > 0 and pmix_ring_children > 0 */

	/* allow user to override default tree width via variable */
	char* p = getenvp(*env, PMIX_RING_TREE_WIDTH_ENV);
	if (p) {
		int width = atoi(p);
		if (width >= 2) {
			pmix_stepd_width = width;
                } else {
			info("Invalid %s value detected (%d), using (%d).",
			     PMIX_RING_TREE_WIDTH_ENV, width, pmix_stepd_width);
		}
	}

	/* allocate hostlist so we can map a stepd rank to a hostname */
	pmix_stepd_hostlist = hostlist_create(job->step_nodelist);

	/* record our rank in the stepd tree */
	pmix_stepd_rank = job->nodeid;

        /* record number of ranks in stepd tree */
        pmix_stepd_ranks = job->nnodes;

        /* record number of application children we serve */
        pmix_app_children = job->ltasks;

	/* compute number of stepd children */
	int min_child = pmix_stepd_rank * pmix_stepd_width + 1;
	int max_child = pmix_stepd_rank * pmix_stepd_width + pmix_stepd_width;
	if (min_child >= pmix_stepd_ranks) {
		min_child = pmix_stepd_ranks;
	}
	if (max_child >= pmix_stepd_ranks) {
		max_child = pmix_stepd_ranks - 1;
	}
	pmix_stepd_children = max_child - min_child + 1;

	/* record number of children we have (includes app procs and stepds) */
	pmix_ring_children = pmix_app_children + pmix_stepd_children;

	/* allocate a structure to record ring_in message from each child */
	pmix_ring_msgs = (pmix_ring_msg*) xmalloc(pmix_ring_children * sizeof(pmix_ring_msg));

	/* initialize messages */
	for (i = 0; i < pmix_ring_children; i++) {
        	pmix_ring_msgs[i].count = 0;
        	pmix_ring_msgs[i].left  = NULL;
        	pmix_ring_msgs[i].right = NULL;
        }

	/* initialize count */
	pmix_ring_count = 0;

	return rc;
}

/* free resources allocated to track PMIX_Ring state */
int pmix_ring_finalize()
{
	int rc = SLURM_SUCCESS;

	/* clear the pmix_ring_in messages for next ring operation */
        if (pmix_ring_msgs != NULL) {
		int i;
		for (i = 0; i < pmix_ring_children; i++) {
			/* free any memory allocated for each message */
			pmix_ring_msg* msg = &pmix_ring_msgs[i];
			msg->count = 0;
			if (msg->left != NULL) {
				xfree(msg->left);
				msg->left = NULL;
			}
			if (msg->right != NULL) {
				xfree(msg->right);
				msg->right = NULL;
			}
		}

		/* free array of messages */
		xfree(pmix_ring_msgs);
		pmix_ring_msgs = NULL;
	}

	/* free host list */
	if (pmix_stepd_hostlist != NULL) {
		hostlist_destroy(pmix_stepd_hostlist);
        }

	return rc;
}

/* ring_out messages come in from our parent,
 * we process this and send ring_out messages to each of our children:
 *   count - starting rank for our leftmost application process
 *   left  - left value for leftmost application process in our subtree
 *   right - right value for rightmost application process in our subtree */
int pmix_ring_out(int count, char* left, char* right)
{
	int rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: in pmix_ring_out rank=%d count=%d left=%s right=%s",
		pmix_stepd_rank, count, left, right);

	/* our parent will send us a pmix_ring_out message, the count value
	 * contained in this message will be the rank of the first process
	 * in our subtree, the left value will be the left value for the
	 * first process in the subtree, and the right value will be the
	 * right value for the last process in our subtree */

	/* allocate a structure to compute values to send to each child */
	pmix_ring_msg* outmsgs = (pmix_ring_msg*) xmalloc(pmix_ring_children * sizeof(pmix_ring_msg));

        /* initialize messages to all children */
	int i;
	for (i = 0; i < pmix_ring_children; i++) {
		outmsgs[i].count = 0;
		outmsgs[i].left  = NULL;
		outmsgs[i].right = NULL;
	}

	/* iterate over all msgs and set count and left neighbor */
	for (i = 0; i < pmix_ring_children; i++) {
		/* store current count in output message */
		outmsgs[i].count = count;

		/* add count for this child to our running total */
		count += pmix_ring_msgs[i].count;

		/* set left value for this child */
		outmsgs[i].left = left;

		/* get right value from child, if it exists,
		 * it will be the left neighbor of the next child,
		 * otherwise, reuse the current left value */
		char* next = pmix_ring_msgs[i].right;
		if (next != NULL) {
			left = next;
		}
	}

	/* now set all right values (iterate backwards through children) */
	for (i = (pmix_ring_children - 1); i >= 0; i--) {
		/* set right value for this child */
		outmsgs[i].right = right;

		/* get left value from child, if it exists,
		 * it will be the right neighbor of the next child,
		 * otherwise, reuse the current right value */
		char* next = pmix_ring_msgs[i].left;
		if (next != NULL) {
			right = next;
		}
	}

	/* send messages to children in stepd tree,
	 * we do this first to get the message down the tree quickly */
	for (i = 0; i < pmix_stepd_children; i++) {
		/* get pointer to message data for this child */
		int ring_id = pmix_app_children + i;
		pmix_ring_msg* msg = &outmsgs[ring_id];

		/* TODO: do we need hton translation? */

		/* construct message */
		Buf buf = init_buf(1024);
		pack16(TREE_CMD_RING_RESP,    buf); /* specify message type (RING_OUT) */
		pack32((uint32_t) msg->count, buf); /* send count value */
		packstr(msg->left,            buf); /* send left value */
		packstr(msg->right,           buf); /* send right value */

		/* get global rank of our i-th child stepd */
		int rank = pmix_stepd_rank_child(i);

		debug3("mpi/pmi2: rank=%d sending RING_OUT to rank=%d count=%d left=%s right=%s",
			pmix_stepd_rank, rank, msg->count, msg->left, msg->right);

		/* send message to child */
		rc = pmix_stepd_send(get_buf_data(buf), (uint32_t) size_buf(buf), rank);

		/* TODO: use tmp_rc here to catch any failure */

		/* free message */
		free_buf(buf);
	}

	/* now send messages to children app procs,
	 * and set their state back to normal */
	for (i = 0; i < pmix_app_children; i++) {
		/* get pointer to message data for this child */
		pmix_ring_msg* msg = &outmsgs[i];

		/* TODO: want to catch send failure here? */

		/* construct message and send to client */
		client_resp_t *resp = client_resp_new();
		client_resp_append(resp, "%s=%s;%s=%d;%s=%d;%s=%s;%s=%s;",
			CMD_KEY, RINGRESP_CMD,
			RC_KEY, 0,
			RING_COUNT_KEY, msg->count,
			RING_LEFT_KEY,  msg->left,
			RING_RIGHT_KEY, msg->right);
		client_resp_send(resp, STEPD_PMI_SOCK(i));
		client_resp_free(resp);
	}

	/* delete messages, note that we don't need to free
         * left and right strings in each message since they
         * are pointers to strings allocated in pmix_ring_msgs */
	xfree(outmsgs);

	/* clear the pmix_ring_in messages for next ring operation */
	for (i = 0; i < pmix_ring_children; i++) {
		pmix_ring_msg* msg = &pmix_ring_msgs[i];
		msg->count = 0;
		if (msg->left != NULL) {
			xfree(msg->left);
			msg->left = NULL;
		}
		if (msg->right != NULL) {
			xfree(msg->right);
			msg->right = NULL;
		}
	}

	/* reset our ring count */
	pmix_ring_count = 0;

	debug3("mpi/pmi2: out pmix_ring_out");
	return rc;
}

/* we get a ring_in message from each child (stepd and application tasks),
 * once we've gotten a message from each child, we send a ring_in message
 * to our parent
 *   ring_id - index of child (all app procs first, followed by stepds)
 *   count   - count value from child
 *   left    - left value from child
 *   right   - right value from child
 *
 * upon receiving ring_in messages from all children, we send a ring_in
 * message to our parent consisting of:
 *   rank  = our rank in stepd tree (so parent knows which child msg is from)
 *   count = sum of counts from all children
 *   left  = left value from leftmost child
 *   right = right value from rightmost child */
int pmix_ring_in(int ring_id, int count, char* left, char* right)
{
	int i;
	int rc = SLURM_SUCCESS;

	debug3("mpi/pmi2: in pmix_ring_in rank=%d ring_id=%d count=%d left=%s right=%s",
		pmix_stepd_rank, ring_id, count, left, right);

	/* record values from child's ring_in message */
	pmix_ring_msg* msg = &pmix_ring_msgs[ring_id];
	msg->count = count;
	msg->left  = xstrdup(left);
	msg->right = xstrdup(right);

	/* update our running count of received ring_in messages */
	pmix_ring_count++;

	/* if we have received a ring_in message from each app process
         * and each stepd child, forward a ring_in message to our
         * parent in the stepd tree */
	if (pmix_ring_count == pmix_ring_children) {
		/* each stepd has at least one application process
		 * so each has at least one child */

		/* lookup leftmost value from all children,
		 * take left value from leftmost process */
		char* leftmost = pmix_ring_msgs[0].left;

		/* lookup rightmost value from all children,
		 * take right value from rightmost process */
		int right_id = pmix_ring_children - 1;
		char* rightmost = pmix_ring_msgs[right_id].right;

		/* total count values across all children */
		uint32_t sum = 0;
		for (i = 0; i < pmix_ring_children; i++) {
			sum += (uint32_t) pmix_ring_msgs[i].count;
		}

		/* send to parent if we have one, otherwise create ring output
		 * message and start the broadcast */
		if (pmix_stepd_rank > 0) {
			/* include our global rank in message so parent can
                         * determine which child we are */
			uint32_t my_rank = (uint32_t) pmix_stepd_rank;

			/* TODO: do we need hton translation? */

			/* construct message */
			Buf buf = init_buf(1024);
			pack16(TREE_CMD_RING, buf); /* specify message type (RING_IN) */
			pack32(my_rank,       buf); /* send our rank */
			pack32(sum,           buf); /* send count value */
			packstr(leftmost,     buf); /* send left value */
			packstr(rightmost,    buf); /* send right value */

			/* get global rank of our parent stepd */
			int rank = pmix_stepd_rank_parent();

			debug3("mpi/pmi2: rank=%d sending RING_IN to rank=%d count=%d left=%s right=%s",
				my_rank, rank, count, leftmost, rightmost);

			/* send message to parent */
                        rc = pmix_stepd_send(get_buf_data(buf), (uint32_t) size_buf(buf), rank);

			/* TODO: use tmp_rc here to catch any failure */

			/* free message */
			free_buf(buf);
		} else {
			/* we're the root of the tree, send values back down */

			/* at the top level, we wrap the ends to create a ring,
			 * setting the rightmost process to be the left neighbor
			 * of the leftmost process */

			/* we start the top of the tree at offset 0 */

			/* simulate reception of a ring output msg */
			pmix_ring_out(0, rightmost, leftmost);
		}
	}

	debug3("mpi/pmi2: out pmix_ring_in");
	return rc;
}
