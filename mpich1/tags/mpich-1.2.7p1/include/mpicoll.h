/*
 * Collective operations (this allows choosing either an implementation
 * in terms of point-to-point, or a special version exploiting special
 * facilities, in a communicator by communicator fashion).
 */
#ifndef MPIR_COLLOPS_DEF
#define MPIR_COLLOPS_DEF
struct _MPIR_COLLOPS {
    int (*Barrier) (struct MPIR_COMMUNICATOR *);
    int (*Bcast) (void*, int, struct MPIR_DATATYPE *, int, 
			    struct MPIR_COMMUNICATOR * );
    int (*Gather) (void*, int, struct MPIR_DATATYPE *, 
		  void*, int, struct MPIR_DATATYPE *, int, 
			     struct MPIR_COMMUNICATOR *); 
    int (*Gatherv) (void*, int, struct MPIR_DATATYPE *, 
			      void*, int *, int *, struct MPIR_DATATYPE *, 
			      int, struct MPIR_COMMUNICATOR *); 
    int (*Scatter) (void*, int, struct MPIR_DATATYPE *, 
		   void*, int, struct MPIR_DATATYPE *, int, 
			      struct MPIR_COMMUNICATOR *);
    int (*Scatterv) (void*, int *, int *, struct MPIR_DATATYPE *, 
			       void*, int, 
		    struct MPIR_DATATYPE *, int, struct MPIR_COMMUNICATOR *);
    int (*Allgather) (void*, int, struct MPIR_DATATYPE *, 
		     void*, int, struct MPIR_DATATYPE *, 
				struct MPIR_COMMUNICATOR *);
    int (*Allgatherv) (void*, int, struct MPIR_DATATYPE *, 
		      void*, int *, int *, struct MPIR_DATATYPE *,
				 struct MPIR_COMMUNICATOR *);
    int (*Alltoall) (void*, int, struct MPIR_DATATYPE *, 
		    void*, int, struct MPIR_DATATYPE *, 
			       struct MPIR_COMMUNICATOR *);
    int (*Alltoallv) (void*, int *, int *, 
		     struct MPIR_DATATYPE *, void*, int *, 
		     int *, struct MPIR_DATATYPE *, 
				struct MPIR_COMMUNICATOR *);
    int (*Alltoallw) (void*, int *, int *, 
		     struct MPIR_DATATYPE *, void*, int *, 
		     int *, struct MPIR_DATATYPE *, 
				struct MPIR_COMMUNICATOR *);
    int (*Reduce) (void*, void*, int, 
		  struct MPIR_DATATYPE *, MPI_Op, int, 
			     struct MPIR_COMMUNICATOR *);
    int (*Allreduce) (void*, void*, int, 
		     struct MPIR_DATATYPE *, MPI_Op, 
				struct MPIR_COMMUNICATOR *);
    int (*Reduce_scatter) (void*, void*, int *, 
			  struct MPIR_DATATYPE *, MPI_Op, 
				     struct MPIR_COMMUNICATOR *);
    int (*Scan) (void*, void*, int, struct MPIR_DATATYPE *, MPI_Op, 
			   struct MPIR_COMMUNICATOR * );
    int ref_count;     /* So we can share it */
};

/* Predefined function tables for collective routines, the device
 * can also use its own, but these are the defaults.
 */
extern MPIR_COLLOPS MPIR_inter_collops;   /* Simply raises appropriate error */
extern MPIR_COLLOPS MPIR_intra_collops;   /* Do the business using pt2pt     */


#endif
