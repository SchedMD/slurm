/* 
 * $Id$
 *
 * Library routines for initiating jobs on QsNet.
 */

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <paths.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>	/* INT_MAX */
#include <stdio.h>
#include <elan3/elan3.h>
#include <elan3/elanvp.h>
#include <rms/rmscall.h>

#include "bitstring.h"
#include "qsw.h"

/*
 * Definitions local to this module.
 */

#define QSW_JOBINFO_MAGIC 	0xf00ff00e
#define QSW_CKPT_MAGIC 		0xf00ff00f

/* we will allocate program descriptions in this range */
/* XXX note: do not start at zero as libelan shifts to get unique shm id */
#define QSW_PRG_START  		1
#define QSW_PRG_END    		INT_MAX

/* we allocate elan hardware context numbers in this range */
#define QSW_CTX_START		ELAN_USER_BASE_CONTEXT_NUM
#define QSW_CTX_END		ELAN_USER_TOP_CONTEXT_NUM

/*
 * Macros
 */

/* Copy library state */
#define _copy_libstate(dest, src) do { 			\
	assert((src)->ls_magic == QSW_CKPT_MAGIC); 		\
	memcpy(dest, src, sizeof(struct qsw_libstate));	\
} while (0)

/*
 * Globals
 */

struct qsw_libstate *qsw_internal_state = NULL;

static void
_srand_if_needed(void)
{
	static int done = 0;

	if (!done) {
		srand48(getpid());
		done = 1;
	}
}

/*
 * Initialize this library.  If called, qsw_create_jobinfo() and 
 * qsw_destroy_jobinfo() will use consecutive integers for program 
 * descriptions.  If not called, those functions will use random numbers.
 * Internal state is initialized from 'oldstate' if non-null.
 */
int
qsw_init(struct qsw_libstate *oldstate)
{
	struct qsw_libstate *new;

	_srand_if_needed();

	assert(qsw_internal_state == NULL);

	new = (struct qsw_libstate *)malloc( sizeof(struct qsw_libstate));
	if (!new) {
		errno = ENOMEM;
		return -1;
	}
	if (oldstate)
		_copy_libstate(new, oldstate);
	else {
		new->ls_magic = QSW_CKPT_MAGIC;
		new->ls_prognum = QSW_PRG_START;
		new->ls_hwcontext = QSW_CTX_START;
	}
	qsw_internal_state = new;
	return 0;
}

/*
 * Finalize use of this library.  If 'savestate' is non-NULL, final
 * state is copied there before it is destroyed.
 */
void
qsw_fini(struct qsw_libstate *savestate)
{
	assert(qsw_internal_state != NULL);
	if (savestate)
		_copy_libstate(savestate, qsw_internal_state);
	qsw_internal_state->ls_magic = 0;
	free(qsw_internal_state);
	qsw_internal_state = NULL;
}

/*
 * Allocate a program description number.  Program descriptions, which are the
 * key abstraction maintained by the rms.o kernel module,  must be unique
 * per node.  It is like an inescapable process group.  If the library is 
 * initialized, we allocate these consecutively, otherwise we generate a 
 * random one, assuming we are being called by a transient program like pdsh.  
 * Ref: rms_prgcreate(3).
 */
static int
_generate_prognum(void)
{
	int new;

	if (qsw_internal_state) {
		new = qsw_internal_state->ls_prognum;
		if (new == QSW_PRG_END)
			qsw_internal_state->ls_prognum = QSW_PRG_START;
		else
			qsw_internal_state->ls_prognum++;
	} else {
		_srand_if_needed();
		new = lrand48() % (QSW_PRG_END - QSW_PRG_START + 1);
		new += QSW_PRG_START;
	}
	return new;
}

/*
 * Elan hardware context numbers must be unique per node.  One is allocated 
 * to each parallel process.  In order for processes on the same node to 
 * communicate, they must use contexts in the hi-lo range of a common 
 * capability.
 * If the library is initialized, we allocate these consecutively, otherwise 
 * we generate a random one, assuming we are being called by a transient 
 * program like pdsh.  Ref: rms_setcap(3).
 */
static int
_generate_hwcontext(int num)
{
	int new;

	if (qsw_internal_state) {
		if (qsw_internal_state->ls_hwcontext + num - 1 > QSW_CTX_END)
			qsw_internal_state->ls_hwcontext = QSW_CTX_START;
		new = qsw_internal_state->ls_hwcontext;
		qsw_internal_state->ls_hwcontext += num;
	} else {
		_srand_if_needed();
		new = lrand48() % (QSW_CTX_END - QSW_CTX_START + 1);
		new +=  QSW_CTX_START;
	}
	return new;
}


/*
 * Initialize the elan capability for this job.
 */
static void
_init_elan_capability(ELAN_CAPABILITY *cap, int nprocs, int nnodes,
		bitstr_t *nodeset, int cyclic_alloc)
{
	int i;
	int procs_per_node = nprocs / nnodes;

	_srand_if_needed();

	/* start with a clean slate */
	elan3_nullcap(cap);

	/* initialize for single rail and either block or cyclic allocation */
	if (cyclic_alloc)
		cap->Type = ELAN_CAP_TYPE_CYCLIC;
	else
		cap->Type = ELAN_CAP_TYPE_BLOCK;
	cap->Type |= ELAN_CAP_TYPE_MULTI_RAIL;
	cap->RailMask = 1;

	/* UserKey is 128 bits of randomness which should be kept private */
        for (i = 0; i < 4; i++)
		key->Values[i] = lrand48();

	/* set up hardware context range */
	cap->LowContext = _generate_hwcontext(procs_per_node);
	cap->HighContext = cap->LowContext + procs_per_node - 1;
	/* Note: not necessary to initialize cap->MyContext */

	/* set the range of nodes to be used and number of processes */
	cap->LowNode = bit_ffs(nodeset);
	assert(cap->LowNode != -1);
	cap->HighNode = bit_fls(nodeset);
	assert(cap->HighNode != -1);
	cap->Entries = nprocs;

	/* set the hw broadcast bit if consecutive nodes */
	if (abs(cap->HighNode - cap->LowNode) == nnodes)
		cap->Type |= ELAN_CAP_TYPE_BROADCASTABLE;

	/*
	 * Set up cap->Bitmap, which describes the mapping of processes to 
	 * the nodes in the range of cap->LowNode - cap->Highnode.
	 * There are (nprocs * nnodes) significant bits in the mask, each 
 	 * representing a process slot.  Bits are off for process slots 
	 * corresponding to unallocated nodes.  For example, if nodes 4 and 6 
	 * are running two processes per node, bits 0,1 (corresponding to the 
	 * two processes on node 4) and bits 4,5 (corresponding to the two 
	 * processes running on node 6) are set.  
	 */
	for (i = 0; i < bit_size(nodeset); i++) {
		if (bit_test(nodeset, i)) {
			int j, proc0;

			for (j = 0; j < procs_per_node; j++) {
				proc0 = (i - cap->LowNode) * procs_per_node;
				assert(proc0 + j < sizeof(cap->Bitmap)*8);
				BT_SET(cap->Bitmap, proc0 + j);
			}
		}
	}
}

/*
 * Create all the QsNet related information needed to set up a QsNet parallel
 * program and store it in the qsw_jobinfo struct.
 */
int
qsw_create_jobinfo(struct qsw_jobinfo **jp, int nprocs, bitstr_t *nodeset, 
		int cyclic_alloc)
{
	struct qsw_jobinfo *new;
	int nnodes = bit_set_count(nodeset);

	assert(jp != NULL);

	/* sanity check on args */
	if (nprocs <= 0 || nprocs > ELAN_MAX_VPS
			|| nnodes == 0 
			|| nprocs % nnodes != 0) {
		errno = EINVAL;
		return -1;
	}
      
        /* allocate space */	
	new = (struct qsw_jobinfo *)malloc(sizeof(struct qsw_jobinfo));
	if (!new) {
		errno = ENOMEM;
		return -1;
	}

	/* initialize jobinfo */
	new->j_magic = QSW_JOBINFO_MAGIC;
	new->j_prognum = _generate_prognum();
	_init_elan_capability(&new->j_cap, nprocs, nnodes, nodeset, 
			cyclic_alloc);

	/* success! */
	*jp = new;
	return 0;
}

/*
 * Destroy a jobinfo_t structure and free associated storage.
 */
void
qsw_destroy_jobinfo(struct qsw_jobinfo *jobinfo)
{
	assert(jobinfo->j_magic == QSW_JOBINFO_MAGIC);
	jobinfo->j_magic = 0;
	free(jobinfo);
}

int
qsw_create_prg(struct qsw_jobinfo *jobinfo)
{
	return 0;
}

int
qsw_destroy_prg(struct qsw_jobinfo *jobinfo)
{
	return 0;
}

int
qsw_attach(struct qsw_jobinfo *jobinfo, int procnum)
{
	return 0;
}

#ifdef DEBUG_MODULE
static void
_dump_jobinfo(struct qsw_jobinfo *jobinfo)
{
	assert(jobinfo->j_magic == QSW_JOBINFO_MAGIC);
	printf("__________________\n");
	printf("jobinfo.prognum=%d\n", jobinfo->j_prognum);
	printf("------------------\n");
}

static void
_safe_mkjob(struct qsw_jobinfo **jp, int nprocs, bitstr_t *nodeset, 
		int cyclic_alloc)
{
	if (qsw_create_jobinfo(jp, nprocs, nodeset, cyclic_alloc) < 0) {
		perror("qsw_create_jobinfo");
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	struct qsw_libstate libstate;
	struct qsw_jobinfo *job;
	bitstr_t *nodeset = bit_alloc(42);

	bit_nset(nodeset, 4, 7);

	_safe_mkjob(&job, 4, nodeset, 0);
	_dump_jobinfo(job);
	qsw_destroy_jobinfo(job);
	
	qsw_init(NULL);

	_safe_mkjob(&job, 4, nodeset, 0);
	_dump_jobinfo(job);
	qsw_destroy_jobinfo(job);

	qsw_fini(NULL);

	qsw_init(NULL);

	_safe_mkjob(&job, 4, nodeset, 0);
	_dump_jobinfo(job);
	qsw_destroy_jobinfo(job);

	qsw_fini(&libstate);

	qsw_init(&libstate);

	_safe_mkjob(&job, 4, nodeset, 0);
	_dump_jobinfo(job);
	qsw_destroy_jobinfo(job);

	qsw_fini(NULL);

	exit(0);
}
#endif /* DEBUG_MODULE */
