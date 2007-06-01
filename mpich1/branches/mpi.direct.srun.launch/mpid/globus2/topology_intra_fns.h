
/*
 * CVS Id: $Id: topology_intra_fns.h,v 1.10 2002/09/13 23:24:44 lacour Exp $
 */

#ifndef TOPOLOGY_INTRA_FNS_H
#define TOPOLOGY_INTRA_FNS_H

#include "mpid.h"

/* experimental: for the symmetric collective operations, hypercube
 * algorithms are usually very efficient, but they can't be used in a
 * topology aware manner: the reason is the hypercube algorithms are
 * symmetric, while a topology aware scheme is Asymmetric (you need to
 * elect a local root in each cluster at every level: this local root
 * will be the representative of its cluster at a lower level).  An
 * idea (still to experiment) consists in re-ordering the processes in
 * a "hypercube-friendly" manner in function of the underlying network
 * topology.  "hypercube-friendly" means that a hypercube algorithm
 * will favor the low latency communications rather than the high
 * latency comms. */
#undef NEED_TOPOLOGY_ORDER

/* does the barrier use the virtual process numbers (i.e.: processes
 * sorted in function of the topology)? */
#define BARRIER_WITH_VIRTUAL_PROCESSES
#undef BARRIER_WITH_VIRTUAL_PROCESSES

#ifdef BARRIER_WITH_VIRTUAL_PROCESSES
#   define NEED_TOPOLOGY_ORDER
#endif   /* BARRIER_WITH_VIRTUAL_PROCESSES */


/* another experiment: need measurements / evaluation / comparison */
/* the initial implementation of the topology aware Gather uses
 * MPI_Pack and MPI_Unpack, so it performs memory copies... which is
 * bad (especially for long messages) from the viewpoint of
 * performance.  The experiment aims to remove those nasty memory
 * copies. */
#define GATHER_WITH_PACK_UNPACK
#undef GATHER_WITH_PACK_UNPACK

/* Same for Scatter */
#define SCATTER_WITH_PACK_UNPACK
#undef SCATTER_WITH_PACK_UNPACK


/*********************/
/* public prototypes */
/*********************/

extern int
MPID_FN_Bcast (void *, int, struct MPIR_DATATYPE *, int,
               struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Barrier (struct MPIR_COMMUNICATOR *);

extern int
MPID_FN_Gather (void *, int, struct MPIR_DATATYPE *, void *, int,
                struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Gatherv (void *, int, struct MPIR_DATATYPE *, void *, int *,
                 int *, struct MPIR_DATATYPE *, int,
                 struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Scatter (void *, int, struct MPIR_DATATYPE *, void *, int,
                 struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Scatterv (void *, int *, int *, struct MPIR_DATATYPE *, void *, int,
                  struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Allgather (void *, int, struct MPIR_DATATYPE *, void *, int,
                   struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Allgatherv (void *, int, struct MPIR_DATATYPE *, void *, int *, int *,
                    struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Alltoall (void *, int, struct MPIR_DATATYPE *, void *, int,
                  struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Alltoallv (void *, int *, int *, struct MPIR_DATATYPE *, void *, int *,
                   int *, struct MPIR_DATATYPE *, struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Reduce (void *, void *, int, struct MPIR_DATATYPE *, MPI_Op, int,
                struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Allreduce (void *, void *, int, struct MPIR_DATATYPE *, MPI_Op,
                   struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Reduce_scatter (void *, void *, int *, struct MPIR_DATATYPE *, MPI_Op,
                        struct MPIR_COMMUNICATOR *);
extern int
MPID_FN_Scan (void *, void *, int, struct MPIR_DATATYPE *, MPI_Op,
              struct MPIR_COMMUNICATOR *);

#endif   /* TOPOLOGY_INTRA_FNS_H */

