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
#include <pthread.h>
#include <stdio.h>
#include <elan3/elan3.h>
#include <elan3/elanvp.h>
#include <rms/rmscall.h>

#include "bitstring.h"
#include "pack.h"
#include "qsw.h"

/*
 * Definitions local to this module.
 */

#define QSW_JOBINFO_MAGIC 	0xf00ff00e
#define QSW_LIBSTATE_MAGIC 	0xf00ff00f

/* we will allocate program descriptions in this range */
/* XXX note: do not start at zero as libelan shifts to get unique shm id */
#define QSW_PRG_START  		1
#define QSW_PRG_END    		INT_MAX
#define QSW_PRG_INVAL		(-1)

/* we allocate elan hardware context numbers in this range */
#define QSW_CTX_START		ELAN_USER_BASE_CONTEXT_NUM
#define QSW_CTX_END		ELAN_USER_TOP_CONTEXT_NUM
#define QSW_CTX_INVAL		(-1)

/* 
 * We are going to some trouble to keep these defs private so slurm
 * hackers not interested in the interconnect details can just pass around
 * the opaque types.  All use of the data structure internals is local to this
 * module.
 */
struct qsw_libstate {
	int ls_magic;
	int ls_prognum;
	int ls_hwcontext;
};

struct qsw_jobinfo {
	int             j_magic;
	int             j_prognum;
	ELAN_CAPABILITY j_cap;
	ELAN3_CTX      *j_ctx;
};

/* Copy library state */
#define _copy_libstate(dest, src) do { 			\
	assert((src)->ls_magic == QSW_LIBSTATE_MAGIC); 	\
	assert((dest)->ls_magic == QSW_LIBSTATE_MAGIC); 	\
	memcpy(dest, src, sizeof(struct qsw_libstate));	\
} while (0)

/* Lock on library state */
#define _lock_qsw() do {				\
	int err = pthread_mutex_lock(&qsw_lock);	\
	assert(err == 0);				\
} while (0)
#define _unlock_qsw() do {				\
	int err = pthread_mutex_unlock(&qsw_lock);	\
	assert(err == 0);				\
} while (0)

/*
 * Globals
 */
static qsw_libstate_t qsw_internal_state = NULL;
static pthread_mutex_t qsw_lock = PTHREAD_MUTEX_INITIALIZER;


/*
 * Allocate a qsw_libstate_t.
 *   lsp (IN)		store pointer to new instantiation here
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_alloc_libstate(qsw_libstate_t *lsp)
{
	qsw_libstate_t new;

	assert(lsp != NULL);
	new = (qsw_libstate_t)malloc(sizeof(struct qsw_libstate));
	if (!new) {
		errno = ENOMEM;
		return -1;
	}
	new->ls_magic = QSW_LIBSTATE_MAGIC;
	*lsp = new;
	return 0;
}

/*
 * Free a qsw_libstate_t.
 *   ls (IN)		qsw_libstate_t to free
 */
void
qsw_free_libstate(qsw_libstate_t ls)
{
	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	ls->ls_magic = 0;
	free(ls);
}

/*
 * Pack libstate structure in a format that can be shipped over the
 * network and unpacked on a different architecture.
 *   ls (IN)		libstate structure to be packed
 *   data (OUT)		where to store packed data
 *   len (IN)		max size of data 
 *   RETURN		#bytes unused in 'data'
 */
int
qsw_pack_libstate(qsw_libstate_t ls, void *data, int len)
{
	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);

	pack32(ls->ls_magic, &data, &len);
	pack32(ls->ls_prognum, &data, &len);
	pack32(ls->ls_hwcontext, &data, &len);

	return len;
}

/*
 * Unpack libstate packed by qsw_pack_libstate.
 *   ls (IN/OUT)	where to store libstate strucutre
 *   data (OUT)		where to store packed data
 *   len (IN)		max size of data 
 *   RETURN		#bytes unused or -1 on error (sets errno)
 */
int
qsw_unpack_libstate(qsw_libstate_t ls, void *data, int len)
{
	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);

	unpack32(&ls->ls_magic, &data, &len);
	unpack32(&ls->ls_prognum, &data, &len);
	unpack32(&ls->ls_hwcontext, &data, &len);

	if (ls->ls_magic != QSW_LIBSTATE_MAGIC) {
		errno = -EINVAL; 	/* bad data */
		return -1;
	}

	return len; 
}

/*
 * Seed the random number generator.  This can be called multiple times,
 * but srand48 will only be called once per program invocation.
 */
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
 * Initialize this library, optionally restoring a previously saved state.
 *   oldstate (IN)	old state retrieved from qsw_fini() or NULL
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_init(qsw_libstate_t oldstate)
{
	qsw_libstate_t new;

	assert(qsw_internal_state == NULL);
	_srand_if_needed();
	if (qsw_alloc_libstate(&new) < 0)
		return -1;
	if (oldstate)
		_copy_libstate(new, oldstate);
	else {
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
qsw_fini(qsw_libstate_t savestate)
{
	assert(qsw_internal_state != NULL);
	_lock_qsw();
	if (savestate)
		_copy_libstate(savestate, qsw_internal_state);
	qsw_free_libstate(qsw_internal_state);
	qsw_internal_state = NULL;
	_unlock_qsw();
}

/*
 * Allocate a qsw_jobinfo_t.
 *   jsp (IN)		store pointer to new instantiation here
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_alloc_jobinfo(qsw_jobinfo_t *jp)
{
	qsw_jobinfo_t new; 

	assert(jp != NULL);
	new = (qsw_jobinfo_t)malloc(sizeof(struct qsw_jobinfo));
	if (!new) {
		errno = ENOMEM;
		return -1;
	}
	new->j_magic = QSW_JOBINFO_MAGIC;
	new->j_ctx = NULL;
	*jp = new;
	return 0;
}

/*
 * Free a qsw_jobinfo_t.
 *   ls (IN)		qsw_jobinfo_t to free
 */
void
qsw_free_jobinfo(qsw_jobinfo_t j)
{
	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	assert(j->j_ctx == NULL);
	j->j_magic = 0;
	free(j);
}

/*
 * Pack jobinfo structure in a format that can be shipped over the
 * network and unpacked on a different architecture.
 *   j (IN)		jobinfo structure to be packed
 *   data (OUT)		where to store packed data
 *   len (IN)		max size of data 
 *   RETURN		#bytes unused in 'data' or -1 on error (sets errno)
 */
int
qsw_pack_jobinfo(qsw_jobinfo_t j, void *data, int len)
{
	int i;

	assert(j->j_magic == QSW_JOBINFO_MAGIC);

	pack32(j->j_magic, 		&data, &len);
	pack32(j->j_prognum, 		&data, &len);
	for (i = 0; i < 4; i++)
		pack32(j->j_cap.UserKey.Values[i], &data, &len);
	pack16(j->j_cap.Type, 		&data, &len);
	pack16(j->j_cap.Generation, 	&data, &len);
	pack32(j->j_cap.LowContext, 	&data, &len);
	pack32(j->j_cap.HighContext, 	&data, &len);
	pack32(j->j_cap.MyContext, 	&data, &len);
	pack32(j->j_cap.LowNode, 	&data, &len);
	pack32(j->j_cap.HighNode, 	&data, &len);
	pack32(j->j_cap.Entries, 	&data, &len);
	pack32(j->j_cap.RailMask, 	&data, &len);
	for (i = 0; i < ELAN_BITMAPSIZE; i++)
		pack32(j->j_cap.Bitmap[i], &data, &len);
	
	return len;
}

/*
 * Unpack jobinfo structure packed by qsw_pack_jobinfo.
 *   j (IN/OUT)		where to store libstate structure
 *   data (OUT)		where to load packed data
 *   len (IN)		max size of data 
 *   RETURN		#bytes unused in 'data' or -1 on error (sets errno)
 */
int
qsw_unpack_jobinfo(qsw_jobinfo_t j, void *data, int len)
{
	int i;

	assert(j->j_magic == QSW_JOBINFO_MAGIC);

	unpack32(&j->j_magic, 		&data, &len);
	unpack32(&j->j_prognum, 	&data, &len);
	for (i = 0; i < 4; i++)
		unpack32(&j->j_cap.UserKey.Values[i], &data, &len);
	unpack16(&j->j_cap.Type, 	&data, &len);
	unpack16(&j->j_cap.Generation, 	&data, &len); 
	unpack32(&j->j_cap.LowContext, 	&data, &len);
	unpack32(&j->j_cap.HighContext, &data, &len);
	unpack32(&j->j_cap.MyContext,	&data, &len);
	unpack32(&j->j_cap.LowNode, 	&data, &len);
	unpack32(&j->j_cap.HighNode, 	&data, &len);
	unpack32(&j->j_cap.Entries, 	&data, &len);
	unpack32(&j->j_cap.RailMask, 	&data, &len);
	for (i = 0; i < ELAN_BITMAPSIZE; i++)
		unpack32(&j->j_cap.Bitmap[i], &data, &len);
	
	if (j->j_magic != QSW_JOBINFO_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	return len;
}

/*
 * Allocate a program description number.  Program descriptions, which are the
 * key abstraction maintained by the rms.o kernel module, must not be used
 * more than once simultaneously on a single node.  We allocate one to each
 * parallel job which more than meets this requirement.  A program description
 * can be compared to a process group, except there is no way for a process to
 * disassociate itself or its children from the program description.  
 * If the library is initialized, we allocate these consecutively, otherwise 
 * we generate a random one, assuming we are being called by a transient 
 * program like pdsh.  Ref: rms_prgcreate(3).
 */
static int
_generate_prognum(void)
{
	int new;

	if (qsw_internal_state) {
		_lock_qsw();
		new = qsw_internal_state->ls_prognum;
		if (new == QSW_PRG_END)
			qsw_internal_state->ls_prognum = QSW_PRG_START;
		else
			qsw_internal_state->ls_prognum++;
		_unlock_qsw();
	} else {
		_srand_if_needed();
		new = lrand48() % (QSW_PRG_END - QSW_PRG_START + 1);
		new += QSW_PRG_START;
	}
	return new;
}

/*
 * Elan hardware context numbers are an adapter resource that must not be used
 * more than once on a single node.  One is allocated to each process on the
 * node that will be communication over Elan.  In order for processes on the 
 * same node to communicate with one another and with other nodes across QsNet,
 * they must use contexts in the hi-lo range of a common capability.
 * If the library is initialized, we allocate these consecutively, otherwise 
 * we generate a random one, assuming we are being called by a transient 
 * program like pdsh.  Ref: rms_setcap(3).
 */
static int
_generate_hwcontext(int num)
{
	int new;

	if (qsw_internal_state) {
		_lock_qsw();
		if (qsw_internal_state->ls_hwcontext + num - 1 > QSW_CTX_END)
			qsw_internal_state->ls_hwcontext = QSW_CTX_START;
		new = qsw_internal_state->ls_hwcontext;
		qsw_internal_state->ls_hwcontext += num;
		_unlock_qsw();
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
		cap->UserKey.Values[i] = lrand48();

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
	if (abs(cap->HighNode - cap->LowNode) == nnodes - 1)
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
 * Call this on the "client" process, e.g. pdsh, srun, slurctld, etc..
 */
int
qsw_create_jobinfo(qsw_jobinfo_t j, int nprocs, bitstr_t *nodeset, 
		int cyclic_alloc)
{
	int nnodes = bit_set_count(nodeset);

	assert(j != NULL);
	assert(j->j_magic == QSW_JOBINFO_MAGIC);

	/* sanity check on args */
	/* Note: ELAN_MAX_VPS is 512 on "old" Elan driver, 16384 on new. */
	if (nprocs <= 0 || nprocs > ELAN_MAX_VPS || nnodes <= 0 
			|| (nprocs % nnodes) != 0) {
		errno = EINVAL;
		return -1;
	}
      
	/* initialize jobinfo */
	j->j_prognum = _generate_prognum();
	j->j_ctx = NULL;
	_init_elan_capability(&j->j_cap, nprocs, nnodes, nodeset, cyclic_alloc);

	return 0;
}

int
qsw_prog_reap(struct qsw_jobinfo *jobinfo)
{
	if (rms_prgdestroy(jobinfo->j_prognum) < 0) {
		/* sets errno */
		return -1;
	}
	return 0;
}

void
qsw_prog_fini(struct qsw_jobinfo *jobinfo)
{
	if (jobinfo->j_ctx) {
		_elan3_fini(jobinfo->j_ctx);
		jobinfo->j_ctx = NULL;
	}
}

int
qsw_prog_init(struct qsw_jobinfo *jobinfo, uid_t uid)
{
	int err;

	/* obtain an Elan context (not the same as a hardware context num!) */
	if ((jobinfo->j_ctx = _elan3_init(0)) == NULL)
		goto fail;

	/* associate this process and its children with prgnum */
	if (rms_prgcreate(jobinfo->j_prognum, uid, 1) < 0)
		goto fail;

      	/* make cap known via rms_getcap/rms_ncaps to members of this prgnum */
	if (elan3_create(jobinfo->j_ctx, &jobinfo->j_cap) < 0)
		goto fail;
	if (rms_prgaddcap(jobinfo->j_prognum, 0, &jobinfo->j_cap) < 0)
		goto fail;

	/* note: _elan3_fini() destroys context and makes capability unavail */
	/* do it in qsw_prog_fini() after app terminates */
	return 0;
fail:
	err = errno; /* presrve errno in case _elan3_fini touches it */
	qsw_prog_fini(jobinfo);
	errno = err;
	return -1;
}

int
qsw_attach(struct qsw_jobinfo *jobinfo, int procnum)
{
	/*
	 * Assign elan hardware context to current process.
	 * - arg1 (0 below) is an index into the kernel's list of caps for this 
	 *   program desc (added by rms_prgaddcap).  There will be
	 *   one per rail.
	 * - arg2 indexes the hw ctxt range in the capability
	 *   [cap->LowContext, cap->HighContext]
	 */
	if (rms_setcap(0, procnum) < 0) /* sets errno */
		return -1;
	return 0;
}

#ifdef DEBUG_MODULE
#define TRUNC_BITMAP 1
static void
_dump_capbitmap(ELAN_CAPABILITY *cap)
{
	int bit_max = sizeof(cap->Bitmap)*8 - 1;
	int bit;
#if TRUNC_BITMAP
	bit_max = bit_max >= 64 ? 64 : bit_max;
#endif
	for (bit = bit_max; bit >= 0; bit--)
		printf("%c", BT_TEST(cap->Bitmap, bit) ? '1' : '0');
	printf("\n");
}

static void
_dump_jobinfo(struct qsw_jobinfo *jobinfo)
{
	ELAN_CAPABILITY *cap;

	assert(jobinfo->j_magic == QSW_JOBINFO_MAGIC);

	printf("__________________\n");
	printf("prognum=%d\n", jobinfo->j_prognum);

	cap = &jobinfo->j_cap;
	printf("cap.UserKey=%8.8x.%8.8x.%8.8x.%8.8x\n",
			cap->UserKey.Values[0], cap->UserKey.Values[1],
			cap->UserKey.Values[2], cap->UserKey.Values[3]);
	printf("cap.Version=%d\n", cap->Version);
	printf("cap.Type=0x%hx\n", cap->Type);
	printf("cap.Generation=%hd\n", cap->Generation);
	printf("cap.LowContext=%d\n", cap->LowContext);
	printf("cap.HighContext=%d\n", cap->HighContext);
	printf("cap.MyContext=%d\n", cap->MyContext);
	printf("cap.LowNode=%d\n", cap->LowNode);
	printf("cap.HighNode=%d\n", cap->HighNode);
	printf("cap.Entries=%d\n", cap->Entries);
	printf("cap.Railmask=0x%x\n", cap->RailMask);
	printf("cap.Bitmap=");
	_dump_capbitmap(cap);
	printf("------------------\n");
}

static void
_safe_mkjob(qsw_jobinfo_t jp, int nprocs, bitstr_t *nodeset, 
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
	qsw_libstate_t ls; 
	char ls_packed[QSW_LIBSTATE_PACK_MAX];
	char job_packed[QSW_JOBINFO_PACK_MAX];
	qsw_jobinfo_t job;
	bitstr_t *nodeset = bit_alloc(42);

	printf("bitmap size = %d\n", ELAN_BITMAPSIZE);

	/* allocate data structures */
	if (qsw_alloc_libstate(&ls) < 0)  {
		perror("qsw_alloc_libstate");
		exit(1);
	}
	if (qsw_alloc_jobinfo(&job) < 0) {
		perror("qsw_alloc_jobinfo");
		exit(1);
	}

	/* jobs will have 4 nodes to work with */
	bit_nset(nodeset, 4, 7);

	/* try one without initializing library */
	_safe_mkjob(job, 8, nodeset, 0);
	_dump_jobinfo(job);

	/* try another one without checkpoints */	
	qsw_init(NULL);

	_safe_mkjob(job, 4, nodeset, 0);
	_dump_jobinfo(job);

	qsw_fini(NULL);

	/* try another one with a checkpiont at the end */
	qsw_init(NULL);

	_safe_mkjob(job, 4, nodeset, 0);
	_dump_jobinfo(job);

	qsw_fini(ls);
	if (qsw_pack_libstate(ls, ls_packed, sizeof(ls_packed)) < 0) {
		perror("qsw_pack_libstate");
		exit(1);
	}

	if (qsw_unpack_libstate(ls, ls_packed, sizeof(ls_packed)) < 0) {
		perror("qsw_unpack_libstate");
		exit(1);
	}

	/* restore checkpoint and try another one */
	qsw_init(ls);

	_safe_mkjob(job, 4, nodeset, 0);
	if (qsw_pack_jobinfo(job, job_packed, sizeof(job_packed)) < 0) {
		perror("qsw_pack_jobinfo");
		exit(1);
	}
	_dump_jobinfo(job);

	_safe_mkjob(job, 12, nodeset, 1);
	_dump_jobinfo(job);

	_safe_mkjob(job, 512, nodeset, 1); /* will get EINVAL if > 512*/
	_dump_jobinfo(job);

	/* revive the packed job */
	if (qsw_unpack_jobinfo(job, job_packed, sizeof(job_packed)) < 0) {
		perror("qsw_pack_jobinfo");
		exit(1);
	}
	_dump_jobinfo(job);

	/* free data structures */
	qsw_free_jobinfo(job);
	qsw_free_libstate(ls);

	qsw_fini(NULL);

	exit(0);
}
#endif /* DEBUG_MODULE */
