/* 
 * $Id$
 *
 * Copyright (C) 2001-2002 Regents of the University of California
 */

#ifndef _QSW_INCLUDED
#define _QSW_INCLUDED

/* opaque data structures - no peeking! */
typedef struct qsw_libstate 	*qsw_libstate_t;
typedef struct qsw_jobinfo 	*qsw_jobinfo_t;

#define QSW_LIBSTATE_PACK_MAX	12
#define QSW_JOBINFO_PACK_MAX	120
#define QSW_MAX_PROCS		1024

/**
 ** Allocation and set up / tear down of jobinfo.
 **/

/*
 * Init/fini the library (optional).  If called, qsw_init() sets up
 * internal library state which allows QsNet program description numbers
 * and hardware context numbers to be allocated sequentially.  If not called,
 * these values are assigned randomly.  If qsw_init() is called, qsw_fini()
 * must be called to clean up storage allocated internally.  The internal
 * state only affects the qsw_create_jobinfo() and qsw_destroy_jobinfo()
 * functions.
 *
 * The library state is checkpointable.  If qsw_fini() is passed qsw_libstate_t
 * (previously instantiated by qsw_alloc_libstate) internal library state will 
 * copied to this location before it is destroyed internally.  This permits 
 * the state to be written to a file or passed to a failover server.  If 
 * qsw_init() is passed a qsw_libstate_t, it copies its initial state from 
 * this instead of starting from scratch.
 *
 * qsw_libstate_t's are externally allocated and freed with the alloc/free
 * functions below.
 *
 * qsw_libstate_t's can be converted to/from a packed format which is suitable
 * for transferring over the network using the pack/unpack functions.
 */
int		qsw_alloc_libstate(qsw_libstate_t *lsp);
void		qsw_free_libstate(qsw_libstate_t ls);

int		qsw_pack_libstate(qsw_libstate_t ls, void *data, int len);
int		qsw_unpack_libstate(qsw_libstate_t ls, void *data, int len);

int 		qsw_init(qsw_libstate_t restorestate);
void 		qsw_fini(qsw_libstate_t savestate);

/*
 * Create all the interconnect information needed to start a parallel job,
 * encpasulated in a qsw_jobinfo_t.  This includes the Elan capability and 
 * program description number.  The qsw_jobinfo_t should be passed (possibly 
 * over the network) to the parents of parallel jobs.  A few parameters are 
 * needed to direct the setup of the elan capability:  
 *   nprocs - the total number of processes in the parallel program
 *   nodeset - a bitmap representing the set of nodes which will run the
 *             parallel program (bit position == elan Id)
 *   cyclic_alloc - 0 if using "block" allocation, 1 if "cyclic" allocation
 * Note: the number of processes per node is expected to be 'nprocs' 
 * divided by the number of bits set in 'nodeset'.
 *
 * qsw_jobinfo_t's can be converted to/from a packed format which is suitable
 * for transferring over the network using the pack/unpack functions.
 */

int		qsw_alloc_jobinfo(qsw_jobinfo_t *jp);
void		qsw_free_jobinfo(qsw_jobinfo_t j);

int		qsw_pack_jobinfo(qsw_jobinfo_t j, void *data, int len);
int		qsw_unpack_jobinfo(qsw_jobinfo_t j, void *data, int len);

int 		qsw_setup_jobinfo(qsw_jobinfo_t j, int nprocs, 
			bitstr_t *nodeset, int cyclic_alloc);

/**
 ** Setup of execution environment for elan-enabled processes.
 **/

/*
 * 1:			2:			3: (N instances)	4:
 * unpack jobinfo 	qsw_prog_init		qsw_attach		setuid
 * get uid from slurm	fork N processes	set environment		exec
 * fork			wait N processes	fork
 * waitpid		qsw_prog_fini		wait
 * qsw_prog_reap	exit			exit
 * exit
 */

int 		qsw_prog_reap(qsw_jobinfo_t jobinfo);

int 		qsw_prog_init(qsw_jobinfo_t jobinfo, uid_t uid);
void 		qsw_prog_fini(qsw_jobinfo_t jobinfo);

int 		qsw_attach(qsw_jobinfo_t jobinfo, int procnum);

#endif /* _QSW_INCLUDED */
