/* for pmi2_job_info_t definition */
#include "setup.h"

/* allocate resources to track PMIX_Ring state */
int pmix_ring_init(const pmi2_job_info_t* job, char*** env);

/* free resources allocated to track PMIX_Ring state */
int pmix_ring_finalize();

/* given a global rank in stepd/srun tree for message received
 * from one of our stepd children, compute corresponding child index */
int pmix_ring_id_by_rank(int rank);

/* ring_out messages come in from our parent nodes,
 * we process this and send ring_out messages to each of our children:
 *   count - starting rank for our leftmost application process
 *   left  - left value for leftmost application process in our subtree
 *   right - right value for rightmost application process in our subtree */
int pmix_ring_out(int count, char* left, char* right);

/* we get a ring_in message from each child (stepd and application tasks),
 * once we've gotten a message from each child, we send a ring_in message
 * to our parent
 *   ring_id - index of child (all app procs first, followed by stepds)
 *   count   - count value from child
 *   left    - left value from child
 *   right   - right value from child
 *
 * upon receiving ring_in messages from all children, sends message to
 * parent consisting of:
 *   count = sum of counts from all children
 *   left  = left value from leftmost child that specified a left value
 *   right = right value from rightmost child that specified a right value */
int pmix_ring_in(int ring_id, int count, char* left, char* right);
