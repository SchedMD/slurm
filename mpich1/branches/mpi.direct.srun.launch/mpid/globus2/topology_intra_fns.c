
/*
 * CVS Id: $Id: topology_intra_fns.c,v 1.118 2004/07/21 15:54:53 lacour Exp $
 */

#include "chconfig.h"
#include "mpid.h"
#include "mpiimpl.h"
#include "mem.h"
#include "coll.h"
#include "mpiops.h"

#include "topology_intra_fns.h"


/* useful prototypes: */
extern void
MPIR_Type_get_limits (struct MPIR_DATATYPE *, MPI_Aint *, MPI_Aint *);


/**********************************************************************/
/* the library routines should call the profiling versions of the
 * functions (PMPI_...) to ensure that only any user code to catch an
 * MPI function only uses the PMPI functions.  Here is a partial
 * solution.  In the weak symbol case, we simply change all of the
 * routines to their PMPI versions. */
/**********************************************************************/

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"



/**********************************************************************/
/* PRIVATE FUNCTIONS                                                  */
/**********************************************************************/

/**********************************************************************/
/* print the contents of a single communication set */
static void
print_set (const comm_set_t set)
{
   const int sz = set.size;
   int i;

   globus_libc_fprintf(stderr, "size=%d, root_index=%d, my_rank_index=%d, " \
                       "set={", sz, sz==0 ? -1 : set.root_index,
                       sz==0 ? -1 : set.my_rank_index);
   for (i = 0; i < sz; i++)
   {
      if ( i ) globus_libc_fprintf(stderr, ", ");
      globus_libc_fprintf(stderr, "%d", set.set[i]);
   }
   globus_libc_fprintf(stderr, "}\n");

   return;
}


/**********************************************************************/
/* print the contents of the sets of communicating processes attached
 * to the given communicator */
static void
print_comm_set (struct MPIR_COMMUNICATOR * const comm)
{
   int lvl, my_depth, my_rank, size;

   (void) MPIR_Comm_size(comm, &size);
   (void) MPIR_Comm_rank(comm, &my_rank);
   my_depth = comm->Topology_Depths[my_rank];

   globus_libc_fprintf(stderr, "*** Start print comm_set from proc #%d/%d\n",
                       my_rank, size);

   for (lvl = 0; lvl < my_depth; lvl++)
   {
      globus_libc_fprintf(stderr, "lvl=%d: ", lvl);
      print_set(comm->Topology_CommSets[lvl]);
   }

   globus_libc_fprintf(stderr, "*** End print comm_set from proc #%d/%d\n",
                       my_rank, size);
   return;
}   /* print_comm_set */


/**********************************************************************/
/* buffer copy; copy unit is "stride" bytes */
static void
copy_buf (const void * const from_buf, const int from, void * const to_buf,
          const int to, const int stride)
{
   memcpy((char *)to_buf + to*stride, (char *)from_buf + from*stride, stride);
   return;
}   /* copy_buf */


/**********************************************************************/
/* A process is a master process at the given level iff its cluster.
 * ids are 0 from the given level + 1 up to its depth: a master proc
 * is the representative of a cluster at level n+1 in a cluster at
 * level n. */
static int
is_a_master (const int level, const int * const ClusterIds, const int depth)
{
   int lvl;

   for (lvl = level+1; lvl < depth; lvl++)
      if ( ClusterIds[lvl] != 0 )
         return GLOBUS_FALSE;

   return GLOBUS_TRUE;
}   /* is_a_master */


/**********************************************************************/
/* create a single set of communicating processes at a given level and
 * for a given color */
static void
make_set (const int lvl, const int color, int * const * const ClusterIds,
          int * const * const Colors, const int * const Depths,
          comm_set_t * const CommSet, const int comm_size, const int rank)
{
   int proc, index;

   CommSet->my_rank_index = -1;

   /* a set is made of all the master processes at the given level, ie: all
    * the root processes at level+1 */
   for (proc = 0, index = 0; proc < comm_size; proc++)
   {
      const int current_Depth = Depths[proc];
      const int * const current_ClusterIds = ClusterIds[proc];

      if ( lvl < current_Depth  &&  color == Colors[proc][lvl]  &&
           is_a_master(lvl, current_ClusterIds, current_Depth) )
      {
         /* know the index of my process in the set */
         if ( proc == rank )
            CommSet->my_rank_index = index;

         /* know the index of the root in this set */
         if ( current_ClusterIds[lvl] == 0 )
            CommSet->root_index = index;

         CommSet->set[index++] = proc;
      }   /* endif */
   }   /* endfor */
   CommSet->size = index;

   return;
}   /* make_set */


/**********************************************************************/
/* create the sets of processes in which I will be involved for
 * communication */
static void
update_comm_sets (const int my_rank, const int comm_size,
                  const int * const Depths, int * const * const ClusterIds,
                  int * const * const Colors, comm_set_t * const CommSets,
                  int * const * const ClusterSizes)
{
   int first_lvl, lvl;
   const int my_depth = Depths[my_rank];
   const int * const my_ClusterIds = ClusterIds[my_rank];
   const int * const my_Colors = Colors[my_rank];

   /* from which level will I be involved in a communication */
   for (first_lvl = 0, lvl = my_depth-1; lvl >= 0; lvl--)
      if ( my_ClusterIds[lvl] != 0 )
      {
         first_lvl = lvl;
         break;
      }   /* endif */

   for (lvl = 0; lvl < first_lvl; lvl++)
      (CommSets[lvl]).size = 0;

   for (lvl = first_lvl; lvl < my_depth; lvl++)
   {
      const int my_color = my_Colors[lvl];

      if ( ClusterSizes[lvl][my_color] < 2 )
         (CommSets[lvl]).size = 0;
      else
         make_set(lvl, my_color, ClusterIds, Colors, Depths,
                  CommSets + lvl, comm_size, my_rank);
   }

   return;
}   /* update_comm_sets */


/**********************************************************************/
/* 'rename' the clusters at each level so that the root process has
 * only zeros as cluster ids (at each level) */
static void
update_cluster_ids (const int root, struct MPIR_COMMUNICATOR * const comm)
{
   int lvl, size;
   const int * const Depths = comm->Topology_Depths;
   int * const * const ClusterIds = comm->Topology_ClusterIds;
   int * const * const Colors = comm->Topology_Colors;
   const int * const root_Colors = Colors[root];
   const int root_depth = Depths[root];
   const int * const root_ClusterIds = ClusterIds[root];

   (void) MPIR_Comm_size (comm, &size);

   for (lvl = 0; lvl < root_depth; lvl++)
   {
      int shift = root_ClusterIds[lvl];

      if ( shift )
      {
         /* at the current level, the root process has a non-zero
          * cluster id: we shift (rotate) the cids at this level for
          * all the processes which can communicate directly with the
          * root process at this level (ie: all the procs in the same
          * cluster as the root). */

         int proc;
         int n_cid = 0;
         const int root_color = root_Colors[lvl];

         /* find the number of cluster-ids that need to be rotated at
          * this level: this value could be cached in a 2D-array in
          * the communicator... */
         for (proc = 0; proc < size; proc++)
            if ( Depths[proc] > lvl  &&  root_color == Colors[proc][lvl]  &&
                 ClusterIds[proc][lvl] > n_cid )
               n_cid = ClusterIds[proc][lvl];

         shift = ++n_cid - shift;

         for (proc = 0; proc < size; proc++)
            if ( Depths[proc] > lvl  &&  root_color == Colors[proc][lvl] )
               ClusterIds[proc][lvl] = (ClusterIds[proc][lvl] + shift) % n_cid;
      }   /* endif */
   }   /* endfor */

   return;
}   /* update_cluster_ids */


/**********************************************************************/
/* create a new MPI datatype which contains all the data elements
 * process 'rank' is responsible for at level 'lvl', and so that those
 * data elements be placed to their right displacements in my local
 * buffer (which was allocated at level 'init_lvl').  Process 'rank'
 * is NOT the local root of its cluster at level 'lvl'.  A flat tree
 * algorithm is assumed. */
static MPI_Datatype
flat_create_datatype (const MPI_Datatype oldtype, const int rank, const int lvl,
                      const int * const Depths, int * const * const Colors,
                      int * const * const Ranks,
                      int * const * const ClusterSizes, const int init_lvl)
{
   /* process 'rank' is responsible for all the processes which have
    * the same color as 'rank' at level 'lvl+1' if this level exists;
    * otherwise, process 'rank' is responsible for itself only */
   const int next_lvl = lvl + 1;
   MPI_Datatype newtype = MPI_DATATYPE_NULL;

   if ( Depths[rank] > next_lvl )
   {
      const int color = Colors[rank][next_lvl];
      const int count = ClusterSizes[next_lvl][color];
      int i, p;
      int *blocklengths, *displs;

      blocklengths = (int *) g_malloc_chk(sizeof(int) * count);
      for (i = 0; i < count; i++)
         blocklengths[i] = 1;

      displs = (int *) g_malloc_chk(sizeof(int) * count);
      for (i = 0, p = 0; i < count; p++)
         if ( Depths[p] > next_lvl  &&  Colors[p][next_lvl] == color )
            displs[i++] = Ranks[p][init_lvl];

      MPI_Type_indexed(count, blocklengths, displs, oldtype, &newtype);

      g_free(displs);
      g_free(blocklengths);
   }
   else   /* proc 'rank' is responsible for itself only */
   {
      int blocklengths[1] = { 1 };
      int displs[1];
      const int count = 1;

      displs[0] = Ranks[rank][init_lvl];
      MPI_Type_indexed(count, blocklengths, displs, oldtype, &newtype);
   }

   return newtype;
}   /* flat_create_datatype */


/**********************************************************************/
/* create a new MPI datatype which contains all the data elements
 * process 'rank' is responsible for at level 'lvl', and so that those
 * data elements be placed to their right displacements in my local
 * buffer (which was allocated at level 'init_lvl').  Process 'rank'
 * is the local root of its cluster at level 'lvl'. */
#if 0
static MPI_Datatype
flat_create_datatype_root (const MPI_Datatype oldtype, const int rank,
                           const int lvl, const int * const Depths,
                           int * const * const Colors, int * const * const Ranks,
                           int * const * const ClusterSizes, const int init_lvl)
{
   /* process 'rank' is responsible for all the processes which
    * have the same color as 'rank' at level 'lvl' */
   const int color = Colors[rank][lvl];
   const int count = ClusterSizes[lvl][color];
   int i, p;
   int *blocklengths, *displs;
   MPI_Datatype newtype = MPI_DATATYPE_NULL;

   g_malloc_chk(blocklengths, int *, sizeof(int) * count);
   for (i = 0; i < count; i++)
      blocklengths[i] = 1;

   g_malloc_chk(displs, int *, sizeof(int) * count);
   for (i = 0, p = 0; i < count; p++)
      if ( Depths[p] > lvl  &&  Colors[p][lvl] == color )
         displs[i++] = Ranks[p][init_lvl];

   MPI_Type_indexed(count, blocklengths, displs, oldtype, &newtype);

   g_free(displs);
   g_free(blocklengths);

   return newtype;
}
#endif


#if defined(GATHER_WITH_PACK_UNPACK)  ||  defined(SCATTER_WITH_PACK_UNPACK)
/**********************************************************************/
/* pack the data elements (in 'from_buf', which is a buffer allocated
 * at level 'init_lvl') process 'rank' is responsible for at level
 * 'lvl', copying them into 'to_buf' at position 'to_position'.  Also
 * update 'to_pos' for the next packs. */
static void
pack_dependencies (void * const from_buf, const int count,
                   const MPI_Datatype datatype, const MPI_Aint stride,
                   const int init_lvl, const int rank, const int lvl,
                   void * const to_buf, const int to_size, int *to_position,
                   const MPI_Comm comm, const int comm_size,
                   const int * const Depths, int * const * const Colors,
                   int * const * const Ranks)
{
   const int next_lvl = lvl + 1;

   if ( Depths[rank] > next_lvl )
   {
      int prc;
      const int color = Colors[rank][next_lvl];

      for (prc = 0; prc < comm_size; prc++)
         /* the process 'prc' belongs to the "dependencies" of process
          * 'rank' iff they both have the same color at level 'next_lvl' */
         if ( Depths[prc] > next_lvl  &&  Colors[prc][next_lvl] == color )
            MPI_Pack(((char *)from_buf) + stride * Ranks[prc][init_lvl],
                     count, datatype, to_buf, to_size, to_position, comm);
   }
   else   /* process 'rank' is responsible for itself only */
      MPI_Pack(((char*)from_buf) + stride * Ranks[rank][init_lvl],
               count, datatype, to_buf, to_size, to_position, comm);

   return;
}   /* pack_dependencies */
#endif   /* GATHER_WITH_PACK_UNPACK  ||  SCATTER_WITH_PACK_UNPACK */


/**********************************************************************/
/* create a new MPI datatype which contains all the data elements
 * process 'rank' is responsible for at level 'lvl', and so that those
 * data elements be placed to their right displacements in my local
 * buffer (which was allocated at level 'init_lvl').  Process 'rank'
 * is NOT the local root of its cluster at level 'lvl'.  A binomial
 * tree algorithm is assumed. */
static MPI_Datatype
binomial_create_datatype (const MPI_Datatype oldtype, const int mask,
                          const int relative_rank_idx, const int lvl,
                          const comm_set_t set, const int * const Depths,
                          int * const * const Colors, int * const * const Ranks,
                          int * const * const ClusterSizes, const int init_lvl)
{
   /* process 'rank' is responsible for all the processes which will
    * have their data elements relayed through process 'rank' as well
    * as those they are responsible for at level 'lvl+1' if this level
    * exists. */
   MPI_Datatype newtype = MPI_DATATYPE_NULL;
   const int next_lvl = lvl + 1;
   const int set_size = set.size;
   const int root_idx = set.root_index;
   const int rank_idx = (relative_rank_idx + root_idx) % set_size;
   const int rank = set.set[rank_idx];
   const int count = ClusterSizes[lvl][Colors[rank][lvl]];
   int *blocklengths, *displs;
   int index = 0;
   int i;

   /* allocate enough memory for the blocklengths and displacements */
   displs = (int *) g_malloc_chk(sizeof(int) * count);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * count);

   for (i = 0; i < mask; i++)
   {
      int fellow = relative_rank_idx + i;

      if ( fellow >= set_size ) break;
      fellow = set.set[(fellow + root_idx) % set_size];
      /* add the data elements process 'fellow' is responsible for at
       * level 'lvl'. */
      if ( Depths[fellow] > next_lvl )
      {
         int idx, p;
         const int clr = Colors[fellow][next_lvl];
         const int cnt = ClusterSizes[next_lvl][clr];

         for (idx = 0, p = 0; idx < cnt; p++)
            if ( Depths[p] > next_lvl  &&  Colors[p][next_lvl] == clr )
            {
               blocklengths[index] = 1;
               displs[index++] = Ranks[p][init_lvl];
               idx++;
            }
      }
      else   /* process 'fellow' is responsible for itself only */
      {
         blocklengths[index] = 1;
         displs[index++] = Ranks[fellow][init_lvl];
      }
   }

   MPI_Type_indexed(index, blocklengths, displs, oldtype, &newtype);

   g_free(displs);
   g_free(blocklengths);

   return newtype;
}   /* binomial_create_datatype */


#if defined(GATHER_WITH_PACK_UNPACK)  ||  defined(SCATTER_WITH_PACK_UNPACK)
/**********************************************************************/
/* unpack the data elements process 'rank' is responsible for (at
 * level 'lvl') from the buffer 'from_buf' (at position
 * 'from_position') copying them into 'to_buf' (which is a buffer
 * allocated at level 'init_lvl').  Also update 'from_position' for
 * the next data to unpack */
static void
unpack_dependencies (void * const from_buf, const int from_size,
                     int *from_position, const int rank, const int lvl,
                     void *to_buf, const int count, const MPI_Datatype datatype,
                     const MPI_Aint stride, const int init_lvl,
                     const MPI_Comm comm, const int comm_size,
                     const int * const Depths, int * const * const Colors,
                     int * const * const Ranks)
{
   int next_lvl = lvl + 1;

   if ( Depths[rank] > next_lvl )
   {
      int prc;
      const int color = Colors[rank][next_lvl];

      for (prc = 0; prc < comm_size; prc++)
         /* the process 'prc' belongs to the "dependencies" of process
          * 'rank' iff they both have the same color at level 'next_lvl' */
         if ( Depths[prc] > next_lvl  &&  Colors[prc][next_lvl] == color )
            MPI_Unpack(from_buf, from_size, from_position,
                       ((char *)to_buf) + stride * Ranks[prc][init_lvl],
                       count, datatype, comm);
   }
   else
      MPI_Unpack(from_buf, from_size, from_position, ((char*)to_buf) + stride *
                                                        Ranks[rank][init_lvl],
                 count, datatype, comm);

   return;
}   /* unpack_dependencies */
#endif   /* GATHER_WITH_PACK_UNPACK  ||  SCATTER_WITH_PACK_UNPACK */


/**********************************************************************/
/* perform an MPI_Waitall on the given number of MPI_Request's */
static int
wait_for_all_reqs (MPI_Request * const req, const int n_req)
{
   MPI_Status *statuses;
   int mpi_errno = MPI_SUCCESS;

   if ( !n_req ) return MPI_SUCCESS;

   statuses = (MPI_Status *) g_malloc_chk(sizeof(MPI_Status) * n_req);
   mpi_errno = MPI_Waitall(n_req, req, statuses);
   /* set mpi_errno in function of statuses if mpi_errno != MPI_SUCCESS */

   g_free(statuses);

   return mpi_errno;
}   /* wait_for_all_reqs */


#ifdef MPID_Barrier
#ifdef BARRIER_WITH_VIRTUAL_PROCESSES

/**********************************************************************/
/* combine like algorithm, using the virtual process ranks, sorted in
 * a "hypercube-friendly fashion". */
static int
hypercube_barrier (struct MPIR_COMMUNICATOR * const comm, const int real_rank)
{
   int mpi_errno = MPI_SUCCESS;

fprintf(stderr, "SEBASTIEN - %d entering hypercube_barrier: not implemented\n",
        real_rank);

   return mpi_errno;
}   /* hypercube_barrier */

#else   /* BARRIER_WITH_VIRTUAL_PROCESSES */

/**********************************************************************/
static int
flat_tree_enter_barrier (const comm_set_t comm_set, const MPI_Comm comm)
{
   int mpi_errno = MPI_SUCCESS;
   const int set_size = comm_set.size;
   const int my_rank_idx = comm_set.my_rank_index;
   const int root_idx = comm_set.root_index;
   const int * const set = comm_set.set;

   if ( my_rank_idx == root_idx )   /* I'm the root of the set */
   {
      int i;
      MPI_Request *req;
      int n_req = 0;

      req = (MPI_Request *) g_malloc_chk(sizeof(MPI_Request) * (set_size-1));
      /* wait for all the notifications from the procs */
      for (i = 0; i < set_size; i++)
         if ( i != my_rank_idx )
         {
            mpi_errno = MPI_Irecv(NULL, 0, MPI_INT, set[i], MPIR_BARRIER_TAG,
                                  comm, req + n_req++);
            if ( mpi_errno ) return mpi_errno;
         }   /* endif */
      mpi_errno = wait_for_all_reqs(req, n_req);
      g_free(req);
   }
   else   /* I'm not the root of this set */
   {
      /* notify the root I reached the barrier */
      mpi_errno = MPI_Send(NULL, 0, MPI_INT, set[root_idx],
                           MPIR_BARRIER_TAG, comm);
   }   /* endif */

   return mpi_errno;
}   /* flat_tree_enter_barrier */


/**********************************************************************/
static int
flat_tree_exit_barrier (const comm_set_t comm_set, const MPI_Comm comm)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_rank_idx = comm_set.my_rank_index;
   const int root_idx = comm_set.root_index;
   const int set_size = comm_set.size;
   const int * const set = comm_set.set;

   if ( my_rank_idx == root_idx )   /* I'm the root of the set */
   {
      int i;
      MPI_Request *req;
      int n_req = 0;

      req = (MPI_Request *) g_malloc_chk(sizeof(MPI_Request) * (set_size-1));
      /* send a GO signal the all the processes in my set */
      for (i = 0; i < set_size; i++)
         if ( i != my_rank_idx )
         {
            mpi_errno = MPI_Isend(NULL, 0, MPI_INT, set[i], MPIR_BARRIER_TAG,
                                  comm, req + n_req++);
            if ( mpi_errno ) return mpi_errno;
         }   /* endif */
      mpi_errno = wait_for_all_reqs(req, n_req);
      g_free(req);
   }
   else   /* I'm not the root of this set */
   {
      MPI_Status status;

      /* wait for root's GO signal */
      mpi_errno = MPI_Recv(NULL, 0, MPI_INT, set[root_idx],
                           MPIR_BARRIER_TAG, comm, &status);
   }   /* endif */

   return mpi_errno;
}   /* flat_tree_exit_barrier */
#endif   /* BARRIER_WITH_VIRTUAL_PROCESSES */
#endif   /* MPID_Barrier */


/**********************************************************************/
#if defined(MPID_Bcast) || defined(MPID_Allgather) || \
    defined(MPID_Allgatherv) || defined(MPID_Alltoall)
static int
binomial_bcast (void * const buffer, const int count,
                const MPI_Datatype datatype, const MPI_Comm comm,
                const comm_set_t set)
{
   int mpi_errno = MPI_SUCCESS;
   MPI_Request *req;
   int n_req = 0;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;
   const int relative_rnk_idx = (my_rank_idx - root_idx + set_size) % set_size;
   int mask = 0x1;

   while ( mask < set_size )
   {
      if ( relative_rnk_idx & mask )
      {
         MPI_Status st;
         const int src_index = (my_rank_idx - mask + set_size) % set_size;

         mpi_errno = MPI_Recv(buffer, count, datatype, set.set[src_index],
                              MPIR_BCAST_TAG, comm, &st);
         if (mpi_errno) return mpi_errno;
         break;
      }   /* endif */
      mask <<= 1;
   }   /* end while */

   req = (MPI_Request *) g_malloc_chk(sizeof(MPI_Request) * set_size);

   /* using the binomial tree algorithm, I may have to relay the message */
   mask >>= 1;
   while (mask > 0)
   {
      if (relative_rnk_idx + mask < set_size)
      {
         const int dst_index = (my_rank_idx + mask) % set_size;

         mpi_errno = MPI_Isend(buffer, count, datatype, set.set[dst_index],
                               MPIR_BCAST_TAG, comm, req + n_req++);
         if ( mpi_errno ) return mpi_errno;
      }
      mask >>= 1;
   }   /* end while */

   mpi_errno = wait_for_all_reqs(req, n_req);
   g_free(req);

   return mpi_errno;
}   /* binomial_bcast */
#endif   /* MPID_Bcast || MPID_Allgather || MPID_Allgatherv || MPID_Alltoall */


/**********************************************************************/
/* flat tree BroadCast: the root process sends the msg to the each of
 * the processes in its set */
#ifdef MPID_Bcast
static int
flat_tree_bcast (void * const buffer, const int count,
                 const MPI_Datatype datatype, const MPI_Comm comm,
                 const comm_set_t set)
{
   int mpi_errno = MPI_SUCCESS;
   const int root_idx = set.root_index;
   const int my_rank_idx = set.my_rank_index;
   const int set_size = set.size;

   if ( root_idx == my_rank_idx )   /* I'm root, i send */
   {
      int i;

      for (i = 0; i < set_size; i++)
         if ( i != my_rank_idx )
         {
            mpi_errno = MPI_Send(buffer, count, datatype, set.set[i],
                                 MPIR_BCAST_TAG, comm);
            if (mpi_errno) return mpi_errno;
         }   /* endif */
   }
   else   /* I'm not the root proc in this set, I recv */
   {
      MPI_Status status;

      mpi_errno = MPI_Recv(buffer, count, datatype, set.set[root_idx],
                           MPIR_BCAST_TAG, comm, &status);
   }   /* endif */

   return mpi_errno;
}   /* flat_tree_bcast */
#endif   /* MPID_Bcast */


#if defined(MPID_Gather)  ||  defined(MPID_Reduce_scatter)
#ifdef GATHER_WITH_PACK_UNPACK
/**********************************************************************/
/* flat algorithm for Gather with memory copies (Pack/Unpack) */
static int
flat_tree_gather (void * const my_buf, const int count,
                  const MPI_Datatype datatype, const MPI_Comm comm,
                  const int comm_size, void *tmp_buf, const int buf_size,
                  const MPI_Aint stride, const int init_lvl,
                  const comm_set_t set, const int * const Depths,
                  int * const * const Colors, int * const * const Ranks,
                  const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;

   if ( root_idx == my_rank_idx )   /* I'm the root of this set: I recv */
   {
      int i;

      for (i = 0; i < set_size; i++)   /* I recv lots */
         if ( i != root_idx )
         {
            const int src = set.set[i];
            int pos = 0;
            MPI_Status status;

            mpi_errno = MPI_Recv(tmp_buf, buf_size, MPI_PACKED, src,
                                 MPIR_GATHER_TAG, comm, &status);
            if ( mpi_errno ) return mpi_errno;

            /* copy the elements recv'd in tmp_buf to their correct position
             * in my_buf */
            unpack_dependencies(tmp_buf, buf_size, &pos, src, lvl, my_buf,
                                count, datatype, stride, init_lvl, comm,
                                comm_size, Depths, Colors, Ranks);
         }   /* endif */
   }
   else   /* I'm not the root process: I send my buffer to the local root */
   {
      int pos = 0;

      pack_dependencies(my_buf, count, datatype, stride, init_lvl,
                        set.set[my_rank_idx], lvl, tmp_buf, buf_size, &pos,
                        comm, comm_size, Depths, Colors, Ranks);
      mpi_errno = MPI_Send(tmp_buf, pos, MPI_PACKED, set.set[root_idx],
                           MPIR_GATHER_TAG, comm);
   }

   return mpi_errno;
}   /* flat_tree_gather */

#else   /* GATHER_WITH_PACK_UNPACK */

/**********************************************************************/
/* flat algorithm for Gather withOUT Pack/Unpack */
static int
flat_tree_gather (void * const my_buf, const MPI_Datatype datatype,
                  const MPI_Comm comm, const int init_lvl, const comm_set_t set,
                  const int * const Depths, int * const * const Colors,
                  int * const * const Ranks, int * const * const ClusterSizes,
                  const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;

   if ( root_idx == my_rank_idx )   /* I'm the root of this set: I recv */
   {
      int i;

      for (i = 0; i < set_size; i++)   /* I recv lots */
         if ( i != root_idx )
         {
            const int src = set.set[i];
            MPI_Datatype type;
            MPI_Status status;

            type = flat_create_datatype(datatype, src, lvl, Depths, Colors,
                                        Ranks, ClusterSizes, init_lvl);
            mpi_errno = MPI_Type_commit(&type);
            if ( mpi_errno ) return mpi_errno;
            mpi_errno = MPI_Recv(my_buf, 1, type, src, MPIR_GATHER_TAG,
                                 comm, &status);
            MPI_Type_free(&type);
            if ( mpi_errno ) return mpi_errno;
         }   /* endif */
   }
   else   /* I'm not the root process: I send my buffer to the local root */
   {
      MPI_Datatype type;

      type = flat_create_datatype(datatype, set.set[my_rank_idx], lvl, Depths,
                                  Colors, Ranks, ClusterSizes, init_lvl);
      mpi_errno = MPI_Type_commit(&type);
      if ( mpi_errno ) return mpi_errno;
      mpi_errno = MPI_Send(my_buf, 1, type, set.set[root_idx],
                           MPIR_GATHER_TAG, comm);
      MPI_Type_free(&type);
   }

   return mpi_errno;
}   /* flat_tree_gather */
#endif   /* GATHER_WITH_PACK_UNPACK */
#endif   /* MPID_Gather  ||  MPID_Reduce_scatter */


#if defined(MPID_Gather)  ||  defined(MPID_Reduce_scatter)
#ifdef GATHER_WITH_PACK_UNPACK
/**********************************************************************/
/* binomial algorithm for Gather with memory copies (Pack/Unpack) */
static int
binomial_gather (void * const my_buf, const int count,
                 const MPI_Datatype datatype, const MPI_Comm comm,
                 const int comm_size, void *tmp_buf, const int buf_size,
                 const MPI_Aint stride, const int init_lvl,
                 const comm_set_t set, const int * const Depths,
                 int * const * const Colors, int * const * const Ranks,
                 const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   int relative_rnk_idx, mask;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;

   relative_rnk_idx = (my_rank_idx - root_idx + set_size) % set_size;
   mask = 0x1;
   /* receive some chunks of data and copy them into my own buffer */
   while ( mask < set_size )
   {
      const int rel_src_idx = relative_rnk_idx + mask;

      if ( relative_rnk_idx & mask ) break;

      if ( rel_src_idx < set_size )
      {
         MPI_Status status;
         int i, pos;
         const int source = set.set[(rel_src_idx + root_idx) % set_size];

         mpi_errno = MPI_Recv(tmp_buf, buf_size, MPI_PACKED, source,
                              MPIR_GATHER_TAG, comm, &status);
         if ( mpi_errno ) return mpi_errno;
         /* copy the recv'd elements from tmp_buf to my_buf */
         pos = 0;
         for (i = 0; i < mask; i++)
         {
            int src;

            src = rel_src_idx + i;
            if ( src >= set_size ) break;
            src = set.set[(src + root_idx) % set_size];
            unpack_dependencies(tmp_buf, buf_size, &pos, src, lvl, my_buf,
                                count, datatype, stride, init_lvl, comm,
                                comm_size, Depths, Colors, Ranks);
         }   /* endfor */
      }   /* endif */
      mask <<= 1;
   }   /* end while */

   /* send all the data elements I collected to my local root */
   if ( my_rank_idx != root_idx )
   {
      int i, pos = 0;
      const int dst = (my_rank_idx - mask + set_size) % set_size;

      for (i = 0; i < mask; i++)
      {
         int prc = relative_rnk_idx + i;

         if ( prc >= set_size ) break;
         prc = set.set[(prc + root_idx) % set_size];
         pack_dependencies(my_buf, count, datatype, stride, init_lvl, prc,
                           lvl, tmp_buf, buf_size, &pos, comm, comm_size,
                           Depths, Colors, Ranks);
      }   /* endfor */
      mpi_errno = MPI_Send(tmp_buf, pos, MPI_PACKED, set.set[dst],
                           MPIR_GATHER_TAG, comm);
   }   /* endif */

   return mpi_errno;
}   /* binomial_gather */

#else   /* GATHER_WITH_PACK_UNPACK */

/**********************************************************************/
/* binomial algorithm for Gather withOUT Pack/Unpack */
static int
binomial_gather (void * const my_buf, const MPI_Datatype datatype,
                 const MPI_Comm comm, const int init_lvl, const comm_set_t set,
                 const int * const Depths, int * const * const Colors,
                 int * const * const Ranks, int * const * const ClusterSizes,
                 const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   int mask;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;
   const int relative_rnk_idx = (my_rank_idx - root_idx + set_size) % set_size;

   mask = 0x1;
   /* receive some chunks of data and copy them into my own buffer */
   while ( mask < set_size )
   {
      const int rel_src_idx = relative_rnk_idx + mask;

      if ( relative_rnk_idx & mask ) break;

      if ( rel_src_idx < set_size )
      {
         MPI_Status status;
         MPI_Datatype type;
         const int source = set.set[(rel_src_idx + root_idx) % set_size];

         type = binomial_create_datatype(datatype, mask, rel_src_idx, lvl, set,
                                         Depths, Colors, Ranks, ClusterSizes,
                                         init_lvl);
         mpi_errno = MPI_Type_commit(&type);
         if ( mpi_errno ) return mpi_errno;
         mpi_errno = MPI_Recv(my_buf, 1, type, source,
                              MPIR_GATHER_TAG, comm, &status);
         MPI_Type_free(&type);
         if ( mpi_errno ) return mpi_errno;
      }   /* endif */
      mask <<= 1;
   }   /* end while */

   /* send all the data elements I collected to my local root */
   if ( my_rank_idx != root_idx )
   {
      const int dst = (my_rank_idx - mask + set_size) % set_size;
      MPI_Datatype type;

      type = binomial_create_datatype(datatype, mask, relative_rnk_idx, lvl,
                                      set, Depths, Colors, Ranks, ClusterSizes,
                                      init_lvl);
      mpi_errno = MPI_Type_commit(&type);
      if ( mpi_errno ) return mpi_errno;
      mpi_errno = MPI_Send(my_buf, 1, type, set.set[dst],
                           MPIR_GATHER_TAG, comm);
      MPI_Type_free(&type);
   }   /* endif */

   return mpi_errno;
}   /* binomial_gather */
#endif   /* GATHER_WITH_PACK_UNPACK */
#endif   /* MPID_Gather  ||  MPID_Reduce_scatter */


/**********************************************************************/
/* unpack recursively the data contained in tmp_buf and which
 * processes 'rank' is responsible for.  At unpacking, the data is
 * placed directly in recvbuf at the right place. */
#ifdef MPID_Gatherv
static void
unpack_gatherv (void ** const tmp_buf, const int lvl, const int rank,
                const int * const displs, const int * const recvcnts,
                void * recvbuf, struct MPIR_COMMUNICATOR * const comm,
                const MPI_Datatype recvtype, const int buf_size)
{
   const int next_lvl = lvl + 1;
   const int * const Depths = comm->Topology_Depths;
   const int rank_depth = Depths[rank];

   if ( next_lvl == rank_depth )
   {
      /* process 'rank' is responsible for itself only at level 'lvl' */
      int position = 0;
      MPI_Aint extent;

      MPI_Type_extent(recvtype, &extent);
      MPI_Unpack(*tmp_buf, buf_size, &position,
                 ((char*)recvbuf) + displs[rank] * extent, recvcnts[rank],
                 recvtype, comm->self);
      *tmp_buf = ((char*)(*tmp_buf)) + position;
      return;
   }
   else
   {
      int * const * const Colors = comm->Topology_Colors;
      int * const * const ClusterIds = comm->Topology_ClusterIds;
      const int rank_color = Colors[rank][next_lvl];
      int i, comm_size, set_size;
      comm_set_t set;

      (void) MPIR_Comm_size(comm, &comm_size);

      /* allocate enough memory to hold the processes in rank's
       * cluster at level 'next_lvl' */
      set.set = (int *) g_malloc_chk(sizeof(int) *
                           comm->Topology_ClusterSizes[next_lvl][rank_color]);

      make_set(next_lvl, rank_color, ClusterIds, Colors, Depths, &set,
               comm_size, rank);

      set_size = set.size;
      for (i = 0; i < set_size; i++)
         unpack_gatherv(tmp_buf, next_lvl, set.set[i], displs, recvcnts,
                        recvbuf, comm, recvtype, buf_size);

      g_free(set.set);
   }

   return;
}   /* unpack_gatherv */
#endif   /* MPID_Gatherv */


/**********************************************************************/
/* perform an MPI_Gatherv operation, knowing the various recv_counts */
#ifdef MPID_Gatherv
static int
flat_tree_gatherv_root (void * const sendbuf, const int sendcnt,
                        struct MPIR_DATATYPE * const sendtype,
                        void * const recvbuf, const int * const recvcnts,
                        const int * const displs,
                        struct MPIR_DATATYPE * const recvtype,
                        struct MPIR_COMMUNICATOR * const comm)
{
   int mpi_errno = MPI_SUCCESS;
   void *tmp_buf;
   int lvl, my_depth, i, comm_size, tmp_buf_size, my_rank;
   const comm_set_t * const CommSets = comm->Topology_CommSets;
   MPI_Status status;
   MPI_Aint extent;

   (void) MPIR_Comm_rank(comm, &my_rank);   /* my_rank == global_root */
   mpi_errno = MPI_Type_extent(recvtype->self, &extent);
   if ( mpi_errno ) return mpi_errno;

   /* copy my send_buffer into the right place of recvbuf */
   mpi_errno = MPI_Sendrecv(sendbuf, sendcnt, sendtype->self, my_rank,
                            MPIR_GATHERV_TAG,
                            ((char*)recvbuf) + displs[my_rank] * extent,
                            recvcnts[my_rank], recvtype->self, my_rank,
                            MPIR_GATHERV_TAG, comm->self, &status);
   if ( mpi_errno ) return mpi_errno;

   (void) MPIR_Comm_size(comm, &comm_size);
   /* allocate a temporary buffer for the packed data I'll recv */
   for (tmp_buf_size = 0, i = 0; i < comm_size; i++)
   {
      int sz;

      MPI_Pack_size(recvcnts[i], recvtype->self, comm->self, &sz);
      tmp_buf_size += sz;
   }
   tmp_buf = (void *) g_malloc_chk(tmp_buf_size);

   my_depth = comm->Topology_Depths[my_rank];
   for (lvl = my_depth-1; lvl >= 0; lvl--)
   {
      const comm_set_t set = CommSets[lvl];
      const int my_rank_idx = set.my_rank_index;
      const int set_size = set.size;

      /* any one to talk to? */
      if ( set_size < 2 ) continue;

      for (i = 0; i < set_size; i++)
      {
         MPI_Status status;
         void * const save_tmp_buf = tmp_buf;
         const int src = set.set[i];

         if ( i == my_rank_idx )
            /* my sendbuf has already been copied into the right place */
            continue;

         /* recv packed data into temp_buffer */
         /* these MPI_Recv's might be made non-blocking (MPI_Irecv) */
         mpi_errno = MPI_Recv(tmp_buf, tmp_buf_size, MPI_PACKED, src,
                              MPIR_GATHERV_TAG, comm->self, &status);
         if ( mpi_errno ) goto clean_exit;

         /* unpack the data into its right place in recvbuf */
         unpack_gatherv(&tmp_buf, lvl, src, displs, recvcnts, recvbuf, comm,
                        recvtype->self, tmp_buf_size);
         tmp_buf = save_tmp_buf;
      }
   }

clean_exit:
   g_free(tmp_buf);

   return mpi_errno;
}   /* flat_tree_gatherv_root */
#endif   /* MPID_Gatherv */


/**********************************************************************/
/* perform a topology aware MPI_Gatherv WITHOUT knowing the various
 * recv_counts: so we need to recv the size of the packed data before
 * actually receiving it. */
#ifdef MPID_Gatherv
static int
flat_tree_gatherv_non_root (void * const sendbuf, const int sendcnt,
                            struct MPIR_DATATYPE * const sendtype,
                            const int global_root,
                            struct MPIR_COMMUNICATOR * const comm)
{
   int mpi_errno = MPI_SUCCESS;
   int buf_size = 0;
   int lvl, my_depth, my_rank, sz;
   const comm_set_t * const CommSets = comm->Topology_CommSets;
   void *my_buf;

   (void) MPIR_Comm_rank(comm, &my_rank);
   my_depth = comm->Topology_Depths[my_rank];

   /* pack my send_buffer into my_buf */
   mpi_errno = MPI_Pack_size(sendcnt, sendtype->self, comm->self, &sz);
   if ( mpi_errno ) return mpi_errno;
   my_buf = (void *) g_malloc_chk(sz);
   mpi_errno = MPI_Pack(sendbuf, sendcnt, sendtype->self,
                        my_buf, sz, &buf_size, comm->self);
   if ( mpi_errno ) goto clean_exit_0;

   for (lvl = my_depth-1; lvl >= 0; lvl--)
   {
      const comm_set_t set = CommSets[lvl];
      const int my_rank_idx = set.my_rank_index;
      const int root_idx = set.root_index;
      const int set_size = set.size;

      /* any one to talk to? */
      if ( set_size < 2 ) continue;

      if ( my_rank_idx == root_idx )   /* I'm the local root of the cluster */
      {
         int *recv_sizes;
         void *new_buf, *recvbuf;
         void *old_buf = my_buf;
         int i, new_buf_size;

         /* receive the sizes of the packed data from all the
          * processes I'm responsible for */
         recv_sizes = (int *) g_malloc_chk(sizeof(int) * set_size);
         new_buf_size = recv_sizes[my_rank_idx] = buf_size;
         for (i = 0; i < set_size; i++)
            if ( i != my_rank_idx )
            {
               MPI_Status status;

               mpi_errno = MPI_Recv(recv_sizes + i, 1, MPI_INT, set.set[i],
                                    MPIR_GATHERV_TAG, comm->self, &status);
               if ( mpi_errno ) goto clean_exit_1;
               new_buf_size += recv_sizes[i];
            }

         /* allocate a new buffer for all the data I'm responsible for */
         new_buf = (void *) g_malloc_chk(new_buf_size);
         recvbuf = new_buf;
         new_buf_size = 0;

         /* recv packed data from the processes of my communication set */
         for (i = 0; i < set_size; i++)
         {
            if ( i == my_rank_idx )   /* simply copy my_buf into new_buf */
            {
               memcpy(recvbuf, my_buf, buf_size);
               recvbuf = ((char*)recvbuf) + buf_size;
               new_buf_size += buf_size;
            }
            else   /* actually recv the data from another proc */
            {
               MPI_Status status;
               const int cnt = recv_sizes[i];

               mpi_errno = MPI_Recv(recvbuf, cnt, MPI_PACKED, set.set[i],
                                    MPIR_GATHERV_TAG, comm->self, &status);
               if ( mpi_errno ) break;
               new_buf_size += cnt;
               recvbuf = ((char*)recvbuf) + cnt;
            }
         }

         /* switch to new_buffer */
         my_buf = new_buf;
         buf_size = new_buf_size;
         g_free(old_buf);

clean_exit_1:
         g_free(recv_sizes);
         if ( mpi_errno ) break;
      }
      else   /* send my current buffer of packed data to the local root */
      {
         /* I send my packed buffer 'my_buf' to my local root.  If my
          * local root is NOT the global root, I must first send the
          * size of my local buffer */
         if ( set.set[root_idx] != global_root )
         {
            mpi_errno = MPI_Send(&buf_size, 1, MPI_INT, set.set[root_idx],
                                 MPIR_GATHERV_TAG, comm->self);
            if ( mpi_errno ) break;
         }
         mpi_errno = MPI_Send(my_buf, buf_size, MPI_PACKED, set.set[root_idx],
                              MPIR_GATHERV_TAG, comm->self);
         if ( mpi_errno ) break;
      }
   }

clean_exit_0:
   g_free(my_buf);

   return mpi_errno;
}   /* flat_tree_gatherv_non_root */
#endif   /* MPID_Gatherv */


#ifdef MPID_Scatter
#ifdef SCATTER_WITH_PACK_UNPACK
/**********************************************************************/
/* flat algorithm for Scatter with memory copies (Pack/Unpack) */
static int
flat_tree_scatter (void * const my_buf, const int count,
                   const MPI_Datatype datatype, const MPI_Comm comm,
                   const int comm_size, void *tmp_buf, const int buf_size,
                   const MPI_Aint stride, const int init_lvl,
                   const comm_set_t set, const int * const Depths,
                   int * const * const Colors, int * const * const Ranks,
                   const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;

   if ( root_idx == my_rank_idx )   /* send chunks to the procs of this set */
   {
      int i;

      for (i = 0; i < set_size; i++)
         if ( i != my_rank_idx )
         {
            const int dst = set.set[i];
            int pos = 0;

            /* copy the right chunks of my_buf into tmp_buf */
            pack_dependencies(my_buf, count, datatype, stride, init_lvl, dst,
                              lvl, tmp_buf, buf_size, &pos, comm, comm_size,
                              Depths, Colors, Ranks);

            /* send my data */
            mpi_errno = MPI_Send(tmp_buf, pos, MPI_PACKED, dst,
                                 MPIR_SCATTER_TAG, comm);
            if ( mpi_errno ) return mpi_errno;
         }   /* endif */
   }
   else   /* I recv from the root of this set of processes */
   {
      MPI_Status status;
      int pos = 0;

      mpi_errno = MPI_Recv(tmp_buf, buf_size, MPI_PACKED, set.set[root_idx],
                           MPIR_SCATTER_TAG, comm, &status);
      if ( mpi_errno ) return mpi_errno;
      unpack_dependencies(tmp_buf, buf_size, &pos, set.set[my_rank_idx], lvl,
                          my_buf, count, datatype, stride, init_lvl, comm,
                          comm_size, Depths, Colors, Ranks);
   }   /* endif */

   return mpi_errno;
}   /* flat_tree_scatter */

#else   /* SCATTER_WITH_PACK_UNPACK */

/**********************************************************************/
/* flat algorithm for Scatter withOUT Pack/Unpack */
static int
flat_tree_scatter (void * const my_buf, const MPI_Datatype datatype,
                   const MPI_Comm comm, const int init_lvl,
                   const comm_set_t set, const int * const Depths,
                   int * const * const Colors, int * const * const Ranks,
                   int * const * const ClusterSizes, const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;

   if ( root_idx == my_rank_idx )   /* send chunks to the procs of this set */
   {
      int i;

      for (i = 0; i < set_size; i++)
         if ( i != my_rank_idx )
         {
            const int dst = set.set[i];
            MPI_Datatype type;

            type = flat_create_datatype(datatype, dst, lvl, Depths, Colors,
                                        Ranks, ClusterSizes, init_lvl);
            mpi_errno = MPI_Type_commit(&type);
            if ( mpi_errno ) return mpi_errno;
            /* send my data */
            mpi_errno = MPI_Send(my_buf, 1, type, dst, MPIR_SCATTER_TAG, comm);
            MPI_Type_free(&type);
            if ( mpi_errno ) return mpi_errno;
         }   /* endif */
   }
   else   /* I recv from the root of this set of processes */
   {
      MPI_Status status;
      MPI_Datatype type;

      type = flat_create_datatype(datatype, set.set[my_rank_idx], lvl, Depths,
                                  Colors, Ranks, ClusterSizes, init_lvl);
      mpi_errno = MPI_Type_commit(&type);
      if ( mpi_errno ) return mpi_errno;
      mpi_errno = MPI_Recv(my_buf, 1, type, set.set[root_idx],
                           MPIR_SCATTER_TAG, comm, &status);
      MPI_Type_free(&type);
   }   /* endif */

   return mpi_errno;
}   /* flat_tree_scatter */
#endif   /* SCATTER_WITH_PACK_UNPACK */
#endif   /* MPID_Scatter */


#ifdef MPID_Scatter
#ifdef SCATTER_WITH_PACK_UNPACK
/**********************************************************************/
/* binomial algorithm for Scatter with memory copies (Pack/Unpack) */
static int
binomial_scatter (void * const my_buf, const int count,
                  const MPI_Datatype datatype, const MPI_Comm comm,
                  const int comm_size, void *tmp_buf, const int buf_size,
                  const MPI_Aint stride, const int init_lvl,
                  const comm_set_t set, const int * const Depths,
                  int * const * const Colors, int * const * const Ranks,
                  const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   int mask, relative_rnk_idx;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;

   relative_rnk_idx = (my_rank_idx - root_idx + set_size) % set_size;
   mask = 0x1;

   /* find the guy which is going to send me the data */
   while ( mask < set_size )
   {
      if ( relative_rnk_idx & mask ) break;
      mask <<= 1;
   }

   /* receive some chunks of data from my local root */
   if ( my_rank_idx != root_idx )
   {
      MPI_Status status;
      int i;
      const int source = set.set[(my_rank_idx - mask + set_size) % set_size];
      int pos = 0;

      mpi_errno = MPI_Recv(tmp_buf, buf_size, MPI_PACKED, source,
                           MPIR_SCATTER_TAG, comm, &status);
      if ( mpi_errno ) return mpi_errno;
      for (i = 0; i < mask; i++)
      {
         int prc = relative_rnk_idx + i;

         if ( prc >= set_size ) break;
         prc = set.set[(prc + root_idx) % set_size];
         unpack_dependencies(tmp_buf, buf_size, &pos, prc, lvl, my_buf,
                             count, datatype, stride, init_lvl, comm,
                             comm_size, Depths, Colors, Ranks);
      }
   }   /* endif */

   mask >>= 1;
   /* dispatch data elements to my fellows (binomial-tree algorithm) */
   while ( mask > 0 )
   {
      int dst = relative_rnk_idx + mask;

      if ( dst < set_size )
      {
          int i, pos = 0;

         /* copy the right data elements from my_buf into tmp_buf */
         for (i = 0; i < mask; i++)
         {
            int sub_dest = dst + i;

            if ( sub_dest >= set_size ) break;
            sub_dest = set.set[(sub_dest + root_idx) % set_size];
            pack_dependencies(my_buf, count, datatype, stride, init_lvl,
                              sub_dest, lvl, tmp_buf, buf_size, &pos, comm,
                              comm_size, Depths, Colors, Ranks);
         }   /* endfor */

         dst = set.set[(dst + root_idx) % set_size];
         /* send the tmp_buf */
         mpi_errno = MPI_Send(tmp_buf, pos, MPI_PACKED, dst, MPIR_SCATTER_TAG,
                              comm);
         if ( mpi_errno ) return mpi_errno;
      }
      mask >>= 1;
   }   /* end while */

   return mpi_errno;
}   /* binomial_scatter */

#else   /* SCATTER_WITH_PACK_UNPACK */

/**********************************************************************/
/* binomial algorithm for Scatter withOUT Pack/Unpack */
static int
binomial_scatter (void * const my_buf, const MPI_Datatype datatype,
                  const MPI_Comm comm, const int init_lvl, const comm_set_t set,
                  const int * const Depths, int * const * const Colors,
                  int * const * const Ranks, int * const * const ClusterSizes,
                  const int lvl)
{
   int mpi_errno = MPI_SUCCESS;
   int mask;
   const int my_rank_idx = set.my_rank_index;
   const int root_idx = set.root_index;
   const int set_size = set.size;
   const int relative_rnk_idx = (my_rank_idx - root_idx + set_size) % set_size;

   mask = 0x1;
   /* find the guy which is going to send me the data */
   while ( mask < set_size )
   {
      if ( relative_rnk_idx & mask ) break;
      mask <<= 1;
   }

   /* receive some chunks of data from my local root */
   if ( my_rank_idx != root_idx )
   {
      MPI_Status status;
      MPI_Datatype type;
      const int source = set.set[(my_rank_idx - mask + set_size) % set_size];

      type = binomial_create_datatype(datatype, mask, relative_rnk_idx, lvl,
                                      set, Depths, Colors, Ranks, ClusterSizes,
                                      init_lvl);
      mpi_errno = MPI_Type_commit(&type);
      if ( mpi_errno ) return mpi_errno;
      mpi_errno = MPI_Recv(my_buf, 1, type, source, MPIR_SCATTER_TAG,
                           comm, &status);
      MPI_Type_free(&type);
      if ( mpi_errno ) return mpi_errno;
   }   /* endif */

   mask >>= 1;
   /* dispatch data elements to my fellows (binomial-tree algorithm) */
   while ( mask > 0 )
   {
      int dst = relative_rnk_idx + mask;

      if ( dst < set_size )
      {
         MPI_Datatype type;

         type = binomial_create_datatype(datatype, mask, dst, lvl, set, Depths,
                                         Colors, Ranks, ClusterSizes, init_lvl);
         mpi_errno = MPI_Type_commit(&type);
         if ( mpi_errno ) return mpi_errno;

         dst = set.set[(dst + root_idx) % set_size];
         mpi_errno = MPI_Send(my_buf, 1, type, dst, MPIR_SCATTER_TAG, comm);
         MPI_Type_free(&type);
         if ( mpi_errno ) return mpi_errno;
      }
      mask >>= 1;
   }   /* end while */

   return mpi_errno;
}   /* binomial_scatter */
#endif   /* SCATTER_WITH_PACK_UNPACK */
#endif   /* MPID_Scatter */


/**********************************************************************/
/* create a new datatype containing all the data elements process
 * 'rank' contains at level 'lvl' and at step 'mask'; the data
 * elements the processes are responsible for are also included. */
#ifdef MPID_Allgather
static MPI_Datatype
recurs_dbl_create_datatype (const int rank_idx, const int lvl, const int mask,
                            const MPI_Datatype oldtype, const comm_set_t set,
                            int * const * const ClusterSizes,
                            int * const * const Colors,
                            const int * const Depths)
{
   MPI_Datatype newtype = MPI_DATATYPE_NULL;
   int *blocklengths, *displs;
   int i;
   int index = 0;
   const int set_size = set.size;
   const int next_lvl = lvl + 1;
   const int rank = set.set[rank_idx];
   const int count = ClusterSizes[lvl][Colors[rank][lvl]];
   /* max_elements is useful in case of non-power of 2 set sizes, to
    * avoid sending data which the receiver has already */
   int max_elements = set_size - mask;

   /* allocate enough memory for the blocklengths and displacements */
   displs = (int *) g_malloc_chk(sizeof(int) * count);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * count);

   for (i = 0; i < mask; i++)
   {
      const int fellow = set.set[(rank_idx - i + set_size) % set_size];

      if ( max_elements == 0 ) break;

      /* insert all the data elements process 'fellow' is responsible
       * for at level 'lvl' (i.e.: all the processes which have the
       * same color as 'fellow' at level 'lvl+1'). */
      if ( Depths[fellow] > next_lvl )
      {
         int idx, p;
         const int clr = Colors[fellow][next_lvl];
         const int cnt = ClusterSizes[next_lvl][clr];

         max_elements--;
         for (idx = 0, p = 0; idx < cnt; p++)
            if ( Depths[p] > next_lvl  &&  Colors[p][next_lvl] == clr )
            {
               blocklengths[index] = 1;
               displs[index++] = p;
               idx++;
            }
      }
      else   /* process 'fellow' is responsible for itself only */
      {
         max_elements--;
         blocklengths[index] = 1;
         displs[index++] = fellow;
      }
   }

   MPI_Type_indexed(index, blocklengths, displs, oldtype, &newtype);

   g_free(displs);
   g_free(blocklengths);

   return newtype;
}   /* recurs_dbl_create_datatype */
#endif   /* MPID_Allgather */


/**********************************************************************/
/* create a new datatype containing all the data elements process
 * 'rank' contains at level 'lvl' and at step 'mask'; the data
 * elements the processes are responsible for are also included. */
#ifdef MPID_Allgatherv
static MPI_Datatype
recurs_dbl_create_datatypev (const int rank_idx, const int lvl, const int mask,
                             const MPI_Datatype oldtype,
                             const int * const counts, const int * const displs,
                             const comm_set_t set,
                             int * const * const ClusterSizes,
                             int * const * const Colors,
                             const int * const Depths)
{
   MPI_Datatype newtype = MPI_DATATYPE_NULL;
   int *blocklengths, *dspl;
   int i;
   int index = 0;
   const int set_size = set.size;
   const int next_lvl = lvl + 1;
   const int rank = set.set[rank_idx];
   const int count = ClusterSizes[lvl][Colors[rank][lvl]];
   /* max_elements is useful in case of non-power of 2 set sizes, to
    * avoid sending data which the receiver already has */
   int max_elements = set_size - mask;

   /* allocate enough memory for the blocklengths and displacements */
   dspl = (int *) g_malloc_chk(sizeof(int) * count);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * count);

   for (i = 0; i < mask; i++)
   {
      const int fellow = set.set[(rank_idx - i + set_size) % set_size];

      if ( max_elements == 0 ) break;

      /* insert all the data elements process 'fellow' is responsible
       * for at level 'lvl' (i.e.: all the processes which have the
       * same color as 'fellow' at level 'lvl+1'). */
      if ( Depths[fellow] > next_lvl )
      {
         int idx, p;
         const int clr = Colors[fellow][next_lvl];
         const int cnt = ClusterSizes[next_lvl][clr];

         max_elements--;
         for (idx = 0, p = 0; idx < cnt; p++)
            if ( Depths[p] > next_lvl  &&  Colors[p][next_lvl] == clr )
            {
               blocklengths[index] = counts[p];
               dspl[index++] = displs[p];
               idx++;
            }
      }
      else   /* process 'fellow' is responsible for itself only */
      {
         max_elements--;
         blocklengths[index] = counts[fellow];
         dspl[index++] = displs[fellow];
      }
   }

   MPI_Type_indexed(index, blocklengths, dspl, oldtype, &newtype);

   g_free(dspl);
   g_free(blocklengths);

   return newtype;
}   /* recurs_dbl_create_datatypev */
#endif   /* MPID_Allgatherv */


/**********************************************************************/
/* recursive doubling algorithm for Allgather: allgather upwards to
 * the local root */
#ifdef MPID_Allgather
static int
binomial_allgather_up (const comm_set_t set, void * const buffer,
                       const MPI_Datatype datatype, const MPI_Comm comm,
                       const int lvl, int * const * const Colors,
                       int * const * const ClusterSizes,
                       const int * const Depths)
{
   int mpi_errno = MPI_SUCCESS;
   int mask = 0x1;
   const int set_size = set.size;
   const int my_rank_idx = set.my_rank_index;

   while ( mask < set_size )
   {
      int receiver = (my_rank_idx + mask) % set_size;
      int sender = (my_rank_idx - mask + set_size) % set_size;
      MPI_Status status;
      MPI_Datatype sendtype;
      MPI_Datatype recvtype;

      recvtype = recurs_dbl_create_datatype(sender, lvl, mask, datatype, set,
                                            ClusterSizes, Colors, Depths);
      mpi_errno = MPI_Type_commit(&recvtype);
      if ( mpi_errno ) return mpi_errno;
      sender = set.set[sender];

      sendtype = recurs_dbl_create_datatype(my_rank_idx, lvl, mask, datatype,
                                            set, ClusterSizes, Colors, Depths);
      mpi_errno = MPI_Type_commit(&sendtype);
      if ( mpi_errno )
      {
         MPI_Type_free(&recvtype);
         return mpi_errno;
      }
      receiver = set.set[receiver];

      mpi_errno = MPI_Sendrecv(buffer, 1, sendtype, receiver,
                               MPIR_ALLGATHER_TAG, buffer, 1, recvtype, sender,
                               MPIR_ALLGATHER_TAG, comm, &status);
      MPI_Type_free(&recvtype);
      MPI_Type_free(&sendtype);
      if ( mpi_errno ) break;
      mask <<= 1;
   }

   return mpi_errno;
}   /* binomial_allgather_up */
#endif   /* MPID_Allgather */


/**********************************************************************/
/* 2nd phase of topology aware allgather: broadcast downwards to the
 * slaves the data elements they miss (using a binomial tree algo). */
#ifdef MPID_Allgather
static int
binomial_allgather_down (const comm_set_t set, void * const buffer,
                         const MPI_Datatype datatype, const MPI_Comm comm,
                         const int lvl, const int comm_size,
                         const int * const Depths, int * const * const Colors)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_color = Colors[ set.set[set.root_index] ][ lvl ];
   int *displs, *blocklengths;
   int index, p;
   MPI_Datatype type;

   /* create the datatype including all the data elements that are
    * broadcast (i.e.: those coming from processes which don't have
    * the same color as we at level 'lvl'). */
   displs = (int *) g_malloc_chk(sizeof(int) * comm_size);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * comm_size);
   for (index = 0, p = 0; p < comm_size; p++)
      if ( Depths[p] <= lvl  ||  Colors[p][lvl] != my_color )
      {
         blocklengths[index] = 1;
         displs[index++] = p;
      }
   MPI_Type_indexed(index, blocklengths, displs, datatype, &type);
   g_free(displs);
   g_free(blocklengths);
   mpi_errno = MPI_Type_commit(&type);
   if ( mpi_errno ) return mpi_errno;

   /* perform a binomial broadcast in the set of communicating
    * processes 'set' for datatype 'type' and count == 1 */
   mpi_errno = binomial_bcast(buffer, 1, type, comm, set);

   MPI_Type_free(&type);
   return mpi_errno;
}   /* binomial_allgather_down */
#endif   /* MPID_Allgather */


/**********************************************************************/
/* recursive doubling algorithm for AllgatherV: allgather upwards to
 * the local root */
#ifdef MPID_Allgatherv
static int
binomial_allgatherv_up (const comm_set_t set, void * const buffer,
                        const MPI_Datatype datatype, const int * const counts,
                        const int * const displs, const MPI_Comm comm,
                        const int lvl, int * const * const Colors,
                        int * const * const ClusterSizes,
                        const int * const Depths)
{
   int mpi_errno = MPI_SUCCESS;
   int mask = 0x1;
   const int set_size = set.size;
   const int my_rank_idx = set.my_rank_index;

   while ( mask < set_size )
   {
      int receiver = (my_rank_idx + mask) % set_size;
      int sender = (my_rank_idx - mask + set_size) % set_size;
      MPI_Status status;
      MPI_Datatype sendtype = MPI_DATATYPE_NULL;
      MPI_Datatype recvtype = MPI_DATATYPE_NULL;

      recvtype = recurs_dbl_create_datatypev(sender, lvl, mask, datatype,
                                             counts, displs, set, ClusterSizes,
                                             Colors, Depths);
      mpi_errno = MPI_Type_commit(&recvtype);
      if ( mpi_errno ) return mpi_errno;
      sender = set.set[sender];

      sendtype = recurs_dbl_create_datatypev(my_rank_idx, lvl, mask, datatype,
                                             counts, displs, set, ClusterSizes,
                                             Colors, Depths);
      mpi_errno = MPI_Type_commit(&sendtype);
      if ( mpi_errno ) return mpi_errno;
      receiver = set.set[receiver];

      mpi_errno = MPI_Sendrecv(buffer, 1, sendtype, receiver,
                               MPIR_ALLGATHERV_TAG, buffer, 1, recvtype, sender,
                               MPIR_ALLGATHERV_TAG, comm, &status);
      MPI_Type_free(&recvtype);
      MPI_Type_free(&sendtype);
      if ( mpi_errno ) break;
      mask <<= 1;
   }

   return mpi_errno;
}   /* binomial_allgatherv_up */
#endif   /* MPID_Allgatherv */


/**********************************************************************/
/* 2nd phase of topology aware allgatherv: broadcast downwards to the
 * slaves the data elements they miss (using a binomial tree algo). */
#ifdef MPID_Allgatherv
static int
binomial_allgatherv_down (const comm_set_t set, void * const buffer,
                          const MPI_Datatype datatype, const int * const counts,
                          const int * const displs, const MPI_Comm comm,
                          const int lvl, const int comm_size,
                          const int * const Depths, int * const * const Colors)
{
   int mpi_errno = MPI_SUCCESS;
   const int my_color = Colors[ set.set[set.root_index] ][ lvl ];
   int *dspl, *blocklengths;
   int index, p;
   MPI_Datatype type;

   /* create the datatype including all the data elements that are
    * broadcast (i.e.: those coming from processes which don't have
    * the same color as we at level 'lvl'). */
   dspl = (int *) g_malloc_chk(sizeof(int) * comm_size);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * comm_size);
   for (index = 0, p = 0; p < comm_size; p++)
      if ( Depths[p] <= lvl  ||  Colors[p][lvl] != my_color )
      {
         blocklengths[index] = counts[p];
         dspl[index++] = displs[p];
      }
   MPI_Type_indexed(index, blocklengths, dspl, datatype, &type);
   g_free(dspl);
   g_free(blocklengths);
   mpi_errno = MPI_Type_commit(&type);
   if ( mpi_errno ) return mpi_errno;

   /* perform a binomial broadcast in the set of communicating
    * processes 'set' for datatype 'type' and count == 1 */
   mpi_errno = binomial_bcast(buffer, 1, type, comm, set);

   MPI_Type_free(&type);
   return mpi_errno;
}   /* binomial_allgatherv_down */
#endif   /* MPID_Allgatherv */


/**********************************************************************/
/* recursive doubling algorithm for Alltoall: alltoall upwards to the
 * local roots */
#ifdef MPID_Alltoall
static MPI_Datatype
alltoall_create_datatype (const MPI_Datatype oldtype, const int oldcount,
                          const comm_set_t set, const int mask,
                          const int rank_idx, const int lvl,
                          const int comm_size, const int * const Depths,
                          int * const * const ClusterSizes,
                          int * const * const Colors)
{
   MPI_Datatype newtype = MPI_DATATYPE_NULL;
   int i;
   const int set_size = set.size;
   const int next_lvl = lvl + 1;
   const int rank = set.set[rank_idx];
   const int count = ClusterSizes[lvl][Colors[rank][lvl]];
   const int blocks = oldcount * comm_size;
   int *blocklengths, *displs;
   /* max_elements is useful in case of non-power of 2 set sizes, to
    * avoid sending data which the receiver has already */
   int max_elements = set_size - mask;
   int index = 0;

   /* allocate enough memory for the blocklengths and displacements */
   displs = (int *) g_malloc_chk(sizeof(int) * count);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * count);

   for (i = 0; i < mask; i++)
   {
      const int fellow = set.set[(rank_idx - i + set_size) % set_size];

      if ( max_elements == 0 ) break;

      /* insert all the data elements process 'fellow' is responsible
       * for at level 'lvl' (i.e.: all the processes which have the
       * same color as 'fellow' at level 'lvl+1'). */
      if ( Depths[fellow] > next_lvl )
      {
         int idx, p;
         const int clr = Colors[fellow][next_lvl];
         const int cnt = ClusterSizes[next_lvl][clr];

         max_elements--;
         for (idx = 0, p = 0; idx < cnt; p++)
            if ( Depths[p] > next_lvl  &&  Colors[p][next_lvl] == clr )
            {
               blocklengths[index] = blocks;
               displs[index++] = p * blocks;
               idx++;
            }
      }
      else   /* process 'fellow' is responsible for itself only */
      {
         max_elements--;
         blocklengths[index] = blocks;
         displs[index++] = fellow * blocks;
      }
   }

   MPI_Type_indexed(index, blocklengths, displs, oldtype, &newtype);

   g_free(displs);
   g_free(blocklengths);

   return newtype;
}   /* alltoall_create_datatype */
#endif   /* MPID_Alltoall */


/**********************************************************************/
/* 2nd phase of topology aware alltoall: broadcast downwards to the
 * slaves the data elements they miss (using a binomial tree algo). */
#ifdef MPID_Alltoall
static int
binomial_alltoall_down (const comm_set_t set, void * const buffer,
                        const MPI_Datatype datatype, const int count,
                        const MPI_Comm comm, const int lvl, const int comm_size,
                        const int * const Depths, int * const * const Colors)
{
   int mpi_errno = MPI_SUCCESS;
   int *displs, *blocklengths;
   int index, p;
   MPI_Datatype type;
   const int blocks = count * comm_size;
   const int my_color = Colors[ set.set[set.root_index] ][ lvl ];

   /* create the datatype including all the data elements that are
    * broadcast (i.e.: those coming from processes which don't have
    * the same color as we at level 'lvl'). */
   displs = (int *) g_malloc_chk(sizeof(int) * comm_size);
   blocklengths = (int *) g_malloc_chk(sizeof(int) * comm_size);
   for (index = 0, p = 0; p < comm_size; p++)
      if ( Depths[p] <= lvl  ||  Colors[p][lvl] != my_color )
      {
         blocklengths[index] = blocks;
         displs[index++] = p * blocks;
      }
   MPI_Type_indexed(index, blocklengths, displs, datatype, &type);
   g_free(displs);
   g_free(blocklengths);
   mpi_errno = MPI_Type_commit(&type);
   if ( mpi_errno ) return mpi_errno;

   /* perform a binomial broadcast in the set of communicating
    * processes 'set' for datatype 'type' and count == 1 */
   mpi_errno = binomial_bcast(buffer, 1, type, comm, set);

   MPI_Type_free(&type);
   return mpi_errno;
}   /* binomial_alltoall_down */
#endif   /* MPID_Alltoall */


/**********************************************************************/
#ifdef MPID_Alltoall
static int
binomial_alltoall_up (void * const tmp_buf, const comm_set_t set,
                      const MPI_Comm comm, const int comm_size, const int lvl,
                      const MPI_Datatype datatype, const int count,
                      const int * const Depths, int * const * const Colors,
                      int * const * const ClusterSizes)
{
   int mpi_errno = MPI_SUCCESS;
   const int set_size = set.size;
   const int my_rank_idx = set.my_rank_index;
   int mask = 0x1;

   while ( mask < set_size )
   {
      int receiver = (my_rank_idx + mask) % set_size;
      int sender = (my_rank_idx - mask + set_size) % set_size;
      MPI_Status status;
      MPI_Datatype sendtype;
      MPI_Datatype recvtype;

      recvtype = alltoall_create_datatype(datatype, count, set, mask, sender,
                                          lvl, comm_size, Depths, ClusterSizes,
                                          Colors);
      mpi_errno = MPI_Type_commit(&recvtype);
      if ( mpi_errno ) return mpi_errno;
      sender = set.set[sender];

      sendtype = alltoall_create_datatype(datatype, count, set, mask,
                                          my_rank_idx, lvl, comm_size, Depths,
                                          ClusterSizes, Colors);
      mpi_errno = MPI_Type_commit(&sendtype);
      if ( mpi_errno ) return mpi_errno;
      receiver = set.set[receiver];

      mpi_errno = MPI_Sendrecv(tmp_buf, 1, sendtype, receiver,
                               MPIR_ALLTOALL_TAG, tmp_buf, 1, recvtype, sender,
                               MPIR_ALLTOALL_TAG, comm, &status);
      MPI_Type_free(&recvtype);
      MPI_Type_free(&sendtype);
      if ( mpi_errno ) break;
      mask <<= 1;
   }

   return mpi_errno;
}   /* binomial_alltoall_up */
#endif   /* MPID_Alltoall */


/**********************************************************************/
#ifdef MPID_Reduce
static int
flat_tree_reduce (const comm_set_t set, void * const my_buf, int count,
                  MPI_Datatype datatype, const MPI_Comm comm,
                  MPI_User_function * const uop, void * const tmp_buf)
{
   /* here the operation is assumed to be commutative and associative */
   int mpi_errno = MPI_SUCCESS;
   const int root_idx = set.root_index;
   const int my_rank_idx = set.my_rank_index;
   const int set_size = set.size;

   /* I recv and compute only if I'm the root of this set */
   if ( root_idx == my_rank_idx )
   {
      int i;

      /* receive all the elements and compute */
      for (i = 0; i < set_size; i++)
         if ( i != root_idx )
         {
            MPI_Status status;

            mpi_errno = MPI_Recv((char *) tmp_buf, count, datatype, set.set[i],
                                 MPIR_REDUCE_TAG, comm, &status);
            if ( mpi_errno ) return mpi_errno;
            /* compute (order does not matter) */
            (*uop)(tmp_buf, my_buf, &count, &datatype);
         }   /* endif */
   }
   else   /* I'm not root: I send my buffer */
      mpi_errno = MPI_Send(my_buf, count, datatype, set.set[root_idx],
                           MPIR_REDUCE_TAG, comm);

   return mpi_errno;
}   /* flat_tree_reduce */
#endif   /* MPID_Reduce */


/**********************************************************************/
#ifdef MPID_Reduce
static int
hypercube_reduce (const comm_set_t set, void *my_buf, int count,
                  MPI_Datatype datatype, const MPI_Comm comm,
                  MPI_User_function * const uop, void * const tmp_buf)
{
   /* here the operation is assumed to be commutative and associative */
   int mpi_errno = MPI_SUCCESS;
   const int root_idx = set.root_index;
   const int my_rank_idx = set.my_rank_index;
   const int set_size = set.size;
   const int relative_rnk_idx = (my_rank_idx - root_idx + set_size) % set_size;
   int mask = 0x1;

   while ( mask < set_size )
   {
      if ( mask & relative_rnk_idx )   /* send my (intermediate) result */
      {
         const int dst = set.set[(my_rank_idx - mask + set_size) % set_size];

         mpi_errno = MPI_Send(my_buf, count, datatype, dst, MPIR_REDUCE_TAG,
                              comm);
         break;
      }
      else   /* recv and compute */
      {
         int source = relative_rnk_idx | mask;

         if ( source < set_size )
         {
            MPI_Status status;

            source = set.set[(source + root_idx) % set_size];
            mpi_errno = MPI_Recv(tmp_buf, count, datatype, source,
                                 MPIR_REDUCE_TAG, comm, &status);
            if ( mpi_errno ) break;
            /* compute (order does not matter) */
            (*uop)(tmp_buf, my_buf, &count, &datatype);
         }   /* endif */
      }
      mask <<= 1;
   }   /* end while */

   return mpi_errno;
}   /* hypercube_reduce */
#endif   /* MPID_Reduce */



/**********************************************************************/
/* PUBLIC FUNCTIONS                                                   */
/**********************************************************************/


/**********************************************************************/
/* The barrier: inside each cluster (at each level), the processes of
 * the cluster synchronize w.r.t. the local root of their cluster;
 * then, all the local roots synchronize w.r.t. the "master" root of
 * the communicator.  This barrier scheme is in 2 phases:
 *  - 1st: I notify my local root I reached the barrier, and my local
 *  root notifies its own root that its cluster has reached the
 *  barrier;
 *  - 2nd: the "master" root of the communicator notifies the local
 *  roots of the various cluster that everybody has reached the
 *  barrier, sending a GO signal; then the local roots forward this GO
 *  signal to the processes of the cluster they are responsible for.
 *
 *  This scheme is not satisfactory, because it is not symmetric,
 *  while the barrier is a symmetric operation (we need to elect
 *  roots).
 *
 *  Furthermore, it may be less efficient than the combine-like
 *  algorithm used to implement the default MPICH barrier: less
 *  latencies are required using the latter.  But the combine-like
 *  algorithm canNOT be used in conjunction with the clusterization
 *  induced by the topology.
 *
 *  Possible improvement/solution: keep the combine-like algorithm of
 *  the default MPICH barrier AFTER SORTING the processes in function
 *  of the topology.  Grouping together the processes belonging the
 *  the same cluster, starting with the largest clusters...  This
 *  needs work again, and experiment. */
#ifdef MPID_Barrier
int
MPID_FN_Barrier (struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int size, my_rank;
#ifndef BARRIER_WITH_VIRTUAL_PROCESSES
   int lvl, my_depth;
   const int * const Depths = comm->Topology_Depths;
   comm_set_t *CommSets;
#endif

   /* Intialize communicator size */
   (void) MPIR_Comm_size (comm, &size);

   /* If there's only one member, this is trivial */
   if ( size == 1 ) return MPI_SUCCESS;

   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;
   (void) MPIR_Comm_rank(comm, &my_rank);

#ifndef BARRIER_WITH_VIRTUAL_PROCESSES
   my_depth = Depths[my_rank];
   CommSets = comm->Topology_CommSets;
   update_comm_sets(my_rank, size, Depths, comm->Topology_ClusterIds,
                    comm->Topology_Colors, CommSets,
                    comm->Topology_ClusterSizes);
#endif   /* BARRIER_WITH_VIRTUAL_PROCESSES */

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

#ifdef BARRIER_WITH_VIRTUAL_PROCESSES
   hypercube_barrier(comm, my_rank);
#else   /* BARRIER_WITH_VIRTUAL_PROCESSES */
   /* enter the barrier (tell the people I reached the barrier) */
   for (lvl = my_depth-1; lvl >= 0; lvl--)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      mpi_errno = flat_tree_enter_barrier(CommSets[lvl], comm->self);
      if ( mpi_errno ) goto clean_exit;
   }

   /* exit the barrier (tell the people they can go on working) */
   for (lvl = 0; lvl < my_depth; lvl++)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      mpi_errno = flat_tree_exit_barrier(CommSets[lvl], comm->self);
      if ( mpi_errno ) goto clean_exit;
   }   /* endfor */
#endif   /* BARRIER_WITH_VIRTUAL_PROCESSES */

clean_exit:
   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   return mpi_errno;
}   /* MPID_FN_Barrier */
#endif   /* MPID_Barrier */


/**********************************************************************/
#ifdef MPID_Bcast
int
MPID_FN_Bcast (void *buffer, int count, struct MPIR_DATATYPE *datatype,
               int root, struct MPIR_COMMUNICATOR *comm)
{
   int lvl, my_depth, size, my_rank;
   int mpi_errno = MPI_SUCCESS;
   static char myname[] = "MPI_BCAST";
   comm_set_t *CommSets;
   const int * const Depths = comm->Topology_Depths;

   /* Is root within the comm and more than 1 processes involved? */
   (void) MPIR_Comm_size (comm, &size);

#ifndef MPIR_NO_ERROR_CHECKING
   if (root >= size) 
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, 
                                   myname, (char *)0, (char *)0, root, size);
   else if (root < 0)
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
                                   (char *)0, (char*)0, root);
   if (mpi_errno)
      return MPIR_ERROR(comm, mpi_errno, myname);
#endif   /* MPIR_NO_ERROR_CHECKING */

   /* If there is only one process or nothing to broadcast... */
   if ( size == 1  ||  count == 0 ) return MPI_SUCCESS;

   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;
   (void) MPIR_Comm_rank (comm, &my_rank);

   /* Algorithm:
    * using the cluster IDs, we guess to which processes we need to
    * send messages, or from which proc we need to recv a msg.  We
    * start communicating thru the slowest links (WAN-TCP), ending up
    * with the fastest communication level (vMPI or localhost-TCP).
    * At each communication level, a local root process broadcasts the
    * msg to the representatives of the other cluster at the current
    * level, using either a flat tree algo or a binomial tree algo.
    * For high-latency networks (WAN TCP), a flat tree algo is better.
    * Otherwise, we use a binomial tree algorithm. */

   /* first we 'rename' the clusters at each level so that the root
    * process have only zeros as cluster IDs (at each level). */
   update_cluster_ids(root, comm);
   CommSets = comm->Topology_CommSets;
   update_comm_sets(my_rank, size, Depths, comm->Topology_ClusterIds,
                    comm->Topology_Colors, CommSets,
                    comm->Topology_ClusterSizes);
   my_depth = Depths[my_rank];

   /* Lock for collective operation */
   MPID_THREAD_LOCK (comm->ADIctx,comm);

   for (lvl = 0; lvl < my_depth; lvl++)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      if ( lvl == MPICHX_WAN_LEVEL )   /* flat-tree algorithm */
         mpi_errno = flat_tree_bcast(buffer, count, datatype->self, comm->self,
                                     CommSets[lvl]);
      else   /* binomial tree algorithm */
         mpi_errno = binomial_bcast(buffer, count, datatype->self, comm->self,
                                    CommSets[lvl]);
      if (mpi_errno) return MPIR_ERROR(comm, mpi_errno, myname);
   }   /* endfor */

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   return MPI_SUCCESS;
}   /* MPID_FN_Bcast */
#endif   /* MPID_Bcast */


/**********************************************************************/
#ifdef MPID_Gather
int
MPID_FN_Gather (void *sendbuf, int sendcnt, struct MPIR_DATATYPE *sendtype,
                void *recvbuf, int recvcnt, struct MPIR_DATATYPE *recvtype,
                int root, struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
#ifdef GATHER_WITH_PACK_UNPACK
   void *tmp_buf;
   MPI_Aint buf_size;
#else
   MPI_Datatype datatype;
#endif   /* GATHER_WITH_PACK_UNPACK */
   int lvl, init_lvl, my_depth, size, my_rank;
   const int * const Depths = comm->Topology_Depths;
   static char myname[] = "MPI_GATHER";
   comm_set_t *CommSets;
   int * const * const Colors = comm->Topology_Colors;
   int * const * const Ranks = comm->Topology_Ranks;
   int * const * const ClusterSizes = comm->Topology_ClusterSizes;
   MPI_Status status;
   MPI_Aint recvtype_stride, recvtype_extent, lb, ub;

   if ( sendcnt == 0 ) return MPI_SUCCESS;

   /* Is root within the communicator? */
   (void) MPIR_Comm_size (comm, &size);

#ifndef MPIR_NO_ERROR_CHECKING
   if ( root >= size )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, myname,
                                   (char *)0, (char *)0, root, size);
   else if ( root < 0 )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
                                   (char *)0, (char *)0, root);
   if ( mpi_errno )
      return MPIR_ERROR(comm, mpi_errno, myname);
#endif   /* MPIR_NO_ERROR_CHECKING */

   /* Get my rank and switch communicators to the hidden collective */
   (void) MPIR_Comm_rank (comm, &my_rank);
   comm = comm->comm_coll;

   /* first we 'rename' the clusters at each level so that the root
    * process has only zeros as cluster IDs (at each level). */
   update_cluster_ids(root, comm);
   CommSets = comm->Topology_CommSets;
   update_comm_sets(my_rank, size, Depths, comm->Topology_ClusterIds, Colors,
                    CommSets, ClusterSizes);
   my_depth = Depths[my_rank];

   /* Find the 1st level (init_lvl) of the communication sets I will
    * be involved in */
   for (init_lvl = 0; init_lvl < my_depth; init_lvl++)
      if ( CommSets[init_lvl].size != 0 ) break;

   if ( my_rank != root )
   {
      recvcnt = sendcnt;
      recvtype = sendtype;
   }
   MPIR_Type_get_limits(recvtype, &lb, &ub);
   recvtype_extent = ub - lb;
   recvtype_stride = recvcnt * recvtype_extent;

   /* MPI standard: recvbuf, recvcnt, recvtype are not significant for
    * procs != root.  If I'm not root, allocate memory to hold data I
    * may have to relay from processes I'm responsible for */
   if ( my_rank != root )
   {
      recvbuf = (void *) g_malloc_chk(recvtype_stride *
                           ClusterSizes[init_lvl][Colors[my_rank][init_lvl]]);
      recvbuf = (void *) ((char *)recvbuf - lb);
   }

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

   /* put the data I currently have to relay into recvbuf */
   /* MPI_Sendrecv should be optimized in case source == destination */
   mpi_errno = MPI_Sendrecv(sendbuf, sendcnt, sendtype->self, my_rank,
                            MPIR_GATHER_TAG, ((char*)recvbuf)+recvtype_stride*
                                                     Ranks[my_rank][init_lvl],
                            recvcnt, recvtype->self, my_rank, MPIR_GATHER_TAG,
                            comm->self, &status);
   if ( mpi_errno ) goto clean_exit;

#ifdef GATHER_WITH_PACK_UNPACK
   /* allocate a temporary buffer to recv packed data and where to
    * pack data before sending */
   MPI_Pack_size(recvcnt, recvtype->self, comm->self, &buf_size);
   buf_size *= ClusterSizes[init_lvl][Colors[my_rank][init_lvl]];
   tmp_buf = (void *) g_malloc_chk(buf_size);
#else
   /* create a contiguous datatype holding recvcnt * recvtype */
   MPI_Type_contiguous(recvcnt, recvtype->self, &datatype);
   mpi_errno = MPI_Type_commit(&datatype);
   if ( mpi_errno ) goto clean_exit;
#endif   /* GATHER_WITH_PACK_UNPACK */

   /* p: # of procs; o: overhead to send/recv; l: latency
    * flat tree: time = o*p + l
    * binomial tree: time = (l+2*o) * ceil(log p)
    * If l>>o: for large values of p, binomial tree is more efficient;
    *          for small values of p, flat tree is faster;
    * If l~=o: idem, but the threshold for 'p' is lower. */

   /* depending on the msg size (sendcnt * extent * size), it might be
    * more efficient:
    *  - to take advantage of the protocol levels in case of small msg
    *    size (because there are memory copies),
    *  - or to ignore the protocol levels in case of large msg size
    *    (when the memory copies would take more time than latencies). */

   for (lvl = my_depth-1; lvl >= init_lvl; lvl--)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      if ( lvl == MPICHX_WAN_LEVEL )   /* WAN TCP: flat tree */
#ifdef GATHER_WITH_PACK_UNPACK
         mpi_errno = flat_tree_gather(recvbuf, recvcnt, recvtype->self,
                                      comm->self, size, tmp_buf, buf_size,
                                      recvtype_stride, init_lvl, CommSets[lvl],
                                      Depths, Colors, Ranks, lvl);
#else
         mpi_errno = flat_tree_gather(recvbuf, datatype, comm->self, init_lvl,
                                      CommSets[lvl], Depths, Colors, Ranks,
                                      ClusterSizes, lvl);
#endif   /* GATHER_WITH_PACK_UNPACK */
      else   /* binomial tree algorithm */
#ifdef GATHER_WITH_PACK_UNPACK
         mpi_errno = binomial_gather(recvbuf, recvcnt, recvtype->self,
                                     comm->self, size, tmp_buf, buf_size,
                                     recvtype_stride, init_lvl, CommSets[lvl],
                                     Depths, Colors, Ranks, lvl);
#else
         mpi_errno = binomial_gather(recvbuf, datatype, comm->self, init_lvl,
                                     CommSets[lvl], Depths, Colors, Ranks,
                                     ClusterSizes, lvl);
#endif   /* GATHER_WITH_PACK_UNPACK */
      if ( mpi_errno ) break;
   }   /* endfor */

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

#ifdef GATHER_WITH_PACK_UNPACK
   g_free(tmp_buf);
#else
   MPI_Type_free(&datatype);
#endif   /* GATHER_WITH_PACK_UNPACK */

clean_exit:
   if ( my_rank != root )
      g_free((char*)recvbuf + lb);

   return mpi_errno;
}   /* MPID_FN_Gather */
#endif   /* MPID_Gather */


/**********************************************************************/
/* Since the array of recvcounts is valid only on the root, we cannot
 * do a tree algorithm without first communicating the recvcounts and
 * the recvtype to other processes.
 *
 * The performance of the algorithm implemented here needs to be
 * measured and compared to the default MPICH algorithm (linear
 * algorithm where each proc sends its data to the root directly).
 *
 * Algo: inside each cluster, a process sends first its sendcount to
 * its local root, then it sends the data itself.  The root collects
 * the sendcounts, allocate enough memory, and receive the data.  I'm
 * far from being sure this is an interesting scheme.  This scheme has
 * two (important) drawbacks:
 *  - when a process wants to send data to another "slave" process, it
 *    really sends 2 msgs.
 *  - as I cannot transmit an MPI_Datatype, I need to pack/unpack the
 *    data, hence memory copies...  */
#ifdef MPID_Gatherv
int
MPID_FN_Gatherv (void *sendbuf, int sendcnt, struct MPIR_DATATYPE *sendtype,
                 void *recvbuf, int *recvcnts, int *displs,
                 struct MPIR_DATATYPE *recvtype, int root,
                 struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int size, my_rank;
   static char myname[] = "MPI_GATHERV";

   /* Is root within the communicator? */
   (void) MPIR_Comm_size (comm, &size);

#ifndef MPIR_NO_ERROR_CHECKING
   if ( root >= size )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, myname,
                                   (char *)0, (char *)0, root, size);
   else if ( root < 0 )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
                                   (char *)0, (char *)0, root);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);
#endif   /* MPIR_NO_ERROR_CHECKING */

   /* switch communicators to the hidden collective */
   comm = comm->comm_coll;
   (void) MPIR_Comm_rank(comm, &my_rank);

   /* first we 'rename' the clusters at each level so that the root
    * process has only zeros as cluster IDs (at each level). */
   update_cluster_ids(root, comm);
   update_comm_sets(my_rank, size, comm->Topology_Depths,
                    comm->Topology_ClusterIds, comm->Topology_Colors,
                    comm->Topology_CommSets, comm->Topology_ClusterSizes);

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

   if ( my_rank == root )
      mpi_errno = flat_tree_gatherv_root(sendbuf, sendcnt, sendtype, recvbuf,
                                         recvcnts, displs, recvtype, comm);
   else
      mpi_errno = flat_tree_gatherv_non_root(sendbuf, sendcnt, sendtype, root,
                                             comm);

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   return mpi_errno;
}   /* MPID_FN_Gatherv */
#endif   /* MPID_Gatherv */


/**********************************************************************/
#ifdef MPID_Scatter
int
MPID_FN_Scatter (void *sendbuf, int sendcnt, struct MPIR_DATATYPE *sendtype,
                 void *recvbuf, int recvcnt, struct MPIR_DATATYPE *recvtype,
                 int root, struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
#ifdef SCATTER_WITH_PACK_UNPACK
   void *tmp_buf;
   MPI_Aint buf_size;
#else
   MPI_Datatype datatype;
#endif   /* SCATTER_WITH_PACK_UNPACK */
   int init_lvl, lvl, my_depth, size, my_rank;
   const int * const Depths = comm->Topology_Depths;
   static char myname[] = "MPI_SCATTER";
   comm_set_t *CommSets;
   int * const * const Colors = comm->Topology_Colors;
   int * const * const Ranks = comm->Topology_Ranks;
   int * const * const ClusterSizes = comm->Topology_ClusterSizes;
   MPI_Status status;
   MPI_Aint sendtype_stride, sendtype_extent, ub, lb;

   if ( recvcnt == 0 ) return MPI_SUCCESS;

   /* Is root within the communicator? */
   (void) MPIR_Comm_size (comm, &size);

#ifndef MPIR_NO_ERROR_CHECKING
   if ( root >= size )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, myname,
                                   (char *)0, (char *)0, root, size);
   else if ( root < 0 )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
                                   (char *)0, (char *)0, root);
   if ( mpi_errno )
      return MPIR_ERROR(comm, mpi_errno, myname);
#endif   /* MPIR_NO_ERROR_CHECKING */

   /* Get my rank and switch communicators to the hidden collective */
   (void) MPIR_Comm_rank (comm, &my_rank);
   comm = comm->comm_coll;

   /* first we 'rename' the clusters at each level so that the root
    * process has only zeros as cluster IDs (at each level). */
   update_cluster_ids(root, comm);
   CommSets = comm->Topology_CommSets;
   update_comm_sets(my_rank, size, Depths, comm->Topology_ClusterIds, Colors,
                    CommSets, ClusterSizes);
   my_depth = Depths[my_rank];

   /* Find the 1st level (init_lvl) of the communication sets I will
    * be involved in */
   for (init_lvl = 0; init_lvl < my_depth; init_lvl++)
      if ( CommSets[init_lvl].size != 0 ) break;

   if ( my_rank != root )
   {
      sendtype = recvtype;
      sendcnt = recvcnt;
   }
   MPIR_Type_get_limits(sendtype, &lb, &ub);
   sendtype_extent = ub - lb;
   sendtype_stride = sendcnt * sendtype_extent;

   /* MPI Standard: 'sendbuf', 'sendcnt', 'sendtype' are significant only
    * at root.  If I'm not root, allocate memory to hold data I may
    * have to relay from root to the processes I'm responsible for */
   if ( my_rank != root )
   {
      sendbuf = (void *) g_malloc_chk(sendtype_stride *
                           ClusterSizes[init_lvl][Colors[my_rank][init_lvl]]);
      sendbuf = (void*) ((char*) sendbuf - lb);
   }

#ifdef SCATTER_WITH_PACK_UNPACK
   /* allocate a temporary buffer to recv packed data and where to
    * pack data before sending */
   MPI_Pack_size(sendcnt, sendtype->self, comm->self, &buf_size);
   buf_size *= ClusterSizes[init_lvl][Colors[my_rank][init_lvl]];
   tmp_buf = (void *) g_malloc_chk(buf_size);
#else
   /* create a contiguous datatype holding sendcnt * sendtype */
   MPI_Type_contiguous(sendcnt, sendtype->self, &datatype);
   mpi_errno = MPI_Type_commit(&datatype);
   if ( mpi_errno ) goto clean_exit;
#endif   /* SCATTER_WITH_PACK_UNPACK */

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

   for (lvl = init_lvl; lvl < my_depth; lvl++)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      if ( lvl == MPICHX_WAN_LEVEL )   /* flat-tree algorithm */
#ifdef SCATTER_WITH_PACK_UNPACK
         mpi_errno = flat_tree_scatter(sendbuf, sendcnt, sendtype->self,
                                       comm->self, size, tmp_buf, buf_size,
                                       sendtype_stride, init_lvl, CommSets[lvl],
                                       Depths, Colors, Ranks, lvl);
#else
         mpi_errno = flat_tree_scatter(sendbuf, datatype, comm->self, init_lvl,
                                       CommSets[lvl], Depths, Colors, Ranks,
                                       ClusterSizes, lvl);
#endif   /* SCATTER_WITH_PACK_UNPACK */
      else   /* low latency ==> binomial-tree algorithm */
#ifdef SCATTER_WITH_PACK_UNPACK
         mpi_errno = binomial_scatter(sendbuf, sendcnt, sendtype->self,
                                      comm->self, size, tmp_buf, buf_size,
                                      sendtype_stride, init_lvl, CommSets[lvl],
                                      Depths, Colors, Ranks, lvl);
#else
         mpi_errno = binomial_scatter(sendbuf, datatype, comm->self, init_lvl,
                                      CommSets[lvl], Depths, Colors, Ranks,
                                      ClusterSizes, lvl);
#endif   /* SCATTER_WITH_PACK_UNPACK */
      if ( mpi_errno ) break;
   }   /* endfor */

   /* MPI_Sendrecv should be optimized in case source == destination */
   mpi_errno = MPI_Sendrecv(((char *) sendbuf) + sendtype_stride *
                                                     Ranks[my_rank][init_lvl],
                            sendcnt, sendtype->self, my_rank, MPIR_SCATTER_TAG,
                            recvbuf, recvcnt, recvtype->self, my_rank,
                            MPIR_SCATTER_TAG, comm->self, &status);

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

#ifdef SCATTER_WITH_PACK_UNPACK
   g_free(tmp_buf);
#else
   MPI_Type_free(&datatype);
#endif   /* SCATTER_WITH_PACK_UNPACK */

clean_exit:
   if ( my_rank != root )
      g_free((char*)sendbuf + lb);

   return mpi_errno;
}   /* MPID_FN_Scatter */
#endif   /* MPID_Scatter */


/**********************************************************************/
#ifdef MPID_Scatterv
int
MPID_FN_Scatterv (void *sendbuf, int *sendcnts, int *displs,
                  struct MPIR_DATATYPE *sendtype, void *recvbuf,
                  int recvcnt, struct MPIR_DATATYPE *recvtype, int root,
                  struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int size, rank;
   static char myname[] = "MPI_SCATTERV";

   /* Is root within the communicator? */
   (void) MPIR_Comm_size (comm, &size);

#ifndef MPIR_NO_ERROR_CHECKING
   if ( root >= size )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, myname,
                                   (char *)0, (char *)0, root, size);
   else if ( root < 0 )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
                                   (char *)0, (char *)0, root);
   if ( mpi_errno )
      return MPIR_ERROR(comm, mpi_errno, myname);
#endif   /* MPIR_NO_ERROR_CHECKING */

   /* Get my rank and switch communicators to the hidden collective */
   (void) MPIR_Comm_rank (comm, &rank);
   comm = comm->comm_coll;

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

/* do something here! */

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   return mpi_errno;
}   /* MPID_FN_Scatterv */
#endif   /* MPID_Scatterv */


/**********************************************************************/
/* Topology aware MPI_Allgather function.  Algorithm used:
 *  - in each cluster, the processes send their data element to their
 *    local root (using a flat tree or binomial tree algorithm),
 *  - all the local roots send the data elements of the processes they
 *    are responsible for to the master root (flat or binomial tree),
 *  - the master root broadcasts to the local roots of the cluster all
 *    all the data elements they miss,
 *  - the local roots forward these data elements.
 *
 *  Question: for Wide-Area Networks (high latency), is it more
 *  efficient to use a recursive doubling algorithm (log p, symmetric)
 *  or to elect a master root which gathers all the data and then
 *  broadcast it to all the local roots?
 *
 *  Question: is it efficient to take the underlying topology into
 *  account, using an Asymmetric scheme, while the Allgather operation
 *  is Symmetric?  */
#ifdef MPID_Allgather
int
MPID_FN_Allgather (void *sendbuf, int sendcnt,
                   struct MPIR_DATATYPE *sendtype, void *recvbuf,
                   int recvcnt, struct MPIR_DATATYPE *recvtype,
                   struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int lvl, my_depth, size, my_rank;
   static char myname[] = "MPI_ALLGATHER";
   MPI_Status status;
   MPI_Datatype datatype;
   MPI_Aint stride;
   comm_set_t *CommSets;
   const int * const Depths = comm->Topology_Depths;
   int * const * const Colors = comm->Topology_Colors;
   int * const * const ClusterSizes = comm->Topology_ClusterSizes;

   if ( sendcnt == 0 || recvcnt == 0 ) return MPI_SUCCESS;

   /* Get my rank and the size of the communicator */
   (void) MPIR_Comm_size (comm, &size);
   (void) MPIR_Comm_rank (comm, &my_rank);
   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;
   CommSets = comm->Topology_CommSets;
   update_comm_sets(my_rank, size, Depths, comm->Topology_ClusterIds, Colors,
                    CommSets, ClusterSizes);
   my_depth = comm->Topology_Depths[my_rank];

   /* create a contiguous datatype and find the extent (= stride) of
    * the new datatype */
   MPI_Type_contiguous(recvcnt, recvtype->self, &datatype);
   mpi_errno = MPI_Type_commit(&datatype);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);
   MPI_Type_extent(datatype, &stride);

   /* put my data elements in their final position in recvbuf */
   mpi_errno = MPI_Sendrecv(sendbuf, sendcnt, sendtype->self, my_rank,
                            MPIR_ALLGATHER_TAG, ((char*)recvbuf)+stride*my_rank,
                            recvcnt, recvtype->self, my_rank,
                            MPIR_ALLGATHER_TAG, comm->self, &status);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);

   /* first phase: upwards allgather to the local roots */
   for (lvl = my_depth-1; lvl >= 0; lvl--)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      /* recursive doubling algorithm at each communication level */
      mpi_errno = binomial_allgather_up(CommSets[lvl], recvbuf, datatype,
                                        comm->self, lvl, Colors, ClusterSizes,
                                        Depths);
      if ( mpi_errno ) goto clean_exit;
   }

   /* second phase: downwards broadcast to the slaves.  We can start
    * at level 1 because all the processes in the communication set at
    * level 0 have all the data. */
   for (lvl = 1; lvl < my_depth; lvl++)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      /* here, it may be more efficient to use a binomial tree
       * algorithm (we're never at WAN-TCP level, because we start at
       * lvl == 1) */
      mpi_errno = binomial_allgather_down(CommSets[lvl], recvbuf, datatype,
                                          comm->self, lvl, size, Depths,
                                          Colors);
      if ( mpi_errno ) break;
   }

clean_exit:
   MPI_Type_free(&datatype);

   return mpi_errno;
}   /* MPID_FN_Allgather */
#endif   /* MPID_Allgather */


/**********************************************************************/
/* same comments as for MPI_Allgather. */
#ifdef MPID_Allgatherv
int
MPID_FN_Allgatherv (void *sendbuf, int sendcnt,
                    struct MPIR_DATATYPE *sendtype, void *recvbuf,
                    int *recvcnts, int *displs,
                    struct MPIR_DATATYPE *recvtype,
                    struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int i, lvl, size, rank, my_depth;
   static char myname[] = "MPI_ALLGATHERV";
   MPI_Aint recvtype_extent;
   comm_set_t *CommSets;
   MPI_Status status;
   const int * const Depths = comm->Topology_Depths;
   int * const * const Colors = comm->Topology_Colors;
   int * const * const ClusterSizes = comm->Topology_ClusterSizes;
   int total_recvcnts = 0;

   /* Get my rank and the size of the communicator */
   (void) MPIR_Comm_size (comm, &size);
   (void) MPIR_Comm_rank (comm, &rank);

   for (i = 0; i < size; i++)
      total_recvcnts += recvcnts[i];
   if ( total_recvcnts == 0 ) return MPI_SUCCESS;

   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;
   CommSets = comm->Topology_CommSets;
   update_comm_sets(rank, size, Depths, comm->Topology_ClusterIds, Colors,
                    CommSets, ClusterSizes);
   my_depth = Depths[rank];

   MPI_Type_extent(recvtype->self, &recvtype_extent);

   /* put my data elements in their final position in recvbuf */
   mpi_errno = MPI_Sendrecv(sendbuf, sendcnt, sendtype->self, rank,
                            MPIR_ALLGATHERV_TAG,
                            ((char*)recvbuf) + displs[rank] * recvtype_extent,
                            recvcnts[rank], recvtype->self, rank,
                            MPIR_ALLGATHERV_TAG, comm->self, &status);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);

   /* first phase: upwards allgather to the local roots */
   for (lvl = my_depth-1; lvl >= 0; lvl--)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      /* recursive doubling algorithm at each communication level */
      mpi_errno = binomial_allgatherv_up(CommSets[lvl], recvbuf, recvtype->self,
                                         recvcnts, displs, comm->self, lvl,
                                         Colors, ClusterSizes, Depths);
      if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);
   }

   /* second phase: downwards broadcast to the slaves.  We can start
    * at level 1 because all the processes in the communication set at
    * level 0 have all the data. */
   for (lvl = 1; lvl < my_depth; lvl++)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      /* here, it may be more efficient to use a binomial tree
       * algorithm (we're never at WAN-TCP level, because we start at
       * lvl == 1) */
      mpi_errno = binomial_allgatherv_down(CommSets[lvl], recvbuf,
                                           recvtype->self, recvcnts, displs,
                                           comm->self, lvl, size, Depths,
                                           Colors);
      if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);
   }

   return MPI_SUCCESS;
}   /* MPID_FN_Allgatherv */
#endif   /* MPID_Allgatherv */


/**********************************************************************/
#ifdef MPID_Alltoall
int
MPID_FN_Alltoall (void *sendbuf, int sendcnt,
                  struct MPIR_DATATYPE *sendtype, void *recvbuf,
                  int recvcnt, struct MPIR_DATATYPE *recvtype,
                  struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int p, rank, size, lvl, my_depth;
   MPI_Status status;
   void *tmp_buf, *offset_buf;
   const int * const Depths = comm->Topology_Depths;
   int * const * const ClusterSizes = comm->Topology_ClusterSizes;
   int * const * const Colors = comm->Topology_Colors;
   comm_set_t *CommSets;
   MPI_Aint lb, tmp_buf_stride, send_stride, recv_stride;

   if ( sendcnt == 0 || recvcnt == 0 ) return MPI_SUCCESS;

   /* Get my rank and the size of the communicator */
   (void) MPIR_Comm_rank (comm, &rank);
   (void) MPIR_Comm_size (comm, &size);

   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;
   CommSets = comm->Topology_CommSets;
   update_comm_sets(rank, size, Depths, comm->Topology_ClusterIds, Colors,
                    CommSets, ClusterSizes);
   my_depth = Depths[rank];

   /* get the extent of the send type, and the stride of the data
    * elements to send */
   MPI_Type_extent(sendtype->self, &send_stride);
   send_stride *= sendcnt;
   tmp_buf_stride = size * send_stride;
   MPI_Type_extent(recvtype->self, &recv_stride);
   recv_stride *= recvcnt;

   /* allocate memory for tmp_buf to hold data I may need to relay */
   tmp_buf = (void *) g_malloc_chk(size * tmp_buf_stride);
   MPI_Type_lb(sendtype->self, &lb);
   tmp_buf = ((char *)tmp_buf) - lb;

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx, comm);

   /* copy local sendbuf into tmp_buf at location indexed by rank */
   mpi_errno = MPI_Sendrecv(sendbuf, sendcnt * size, sendtype->self, rank,
                            MPIR_ALLTOALL_TAG,
                            ((char *)tmp_buf) + rank * tmp_buf_stride,
                            sendcnt * size, sendtype->self, rank,
                            MPIR_ALLTOALL_TAG, comm->self, &status);
   if (mpi_errno) goto clean_exit;

   /* 1st phase: upwards alltoall, so the local roots get all the data
    * they'll need */
   for (lvl = my_depth-1; lvl >= 0; lvl--)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      mpi_errno = binomial_alltoall_up(tmp_buf, CommSets[lvl], comm->self, size,
                                       lvl, sendtype->self, sendcnt, Depths,
                                       Colors, ClusterSizes);
      if (mpi_errno) goto clean_exit;
   }

   /* 2nd phase: downwards broadcast to the slaves.  We can start at
    * level 1 because all the processes in the communication set at
    * level 0 have all the data. */
   for (lvl = 1; lvl < my_depth; lvl++)
   {
      /* any one to talk to? */
      if ( CommSets[lvl].size < 2 ) continue;

      /* here, it may be more efficient to use a binomial tree
       * algorithm (we're never at WAN-TCP level, because we start at
       * lvl == 1) */

      mpi_errno = binomial_alltoall_down(CommSets[lvl], tmp_buf, sendtype->self,
                                         sendcnt, comm->self, lvl, size, Depths,
                                         Colors);
      if ( mpi_errno ) goto clean_exit;
   }

   /* everyone's contribution from tmp_buf to recvbuf */
   offset_buf = ((char *)tmp_buf) + send_stride * rank;
   for (p = 0; p < size; p++)
   {
      mpi_errno = MPI_Sendrecv(((char *)offset_buf) + p * tmp_buf_stride,
                               sendcnt, sendtype->self, rank, MPIR_ALLTOALL_TAG,
                               ((char *)recvbuf) + p * recv_stride, recvcnt,
                               recvtype->self, rank, MPIR_ALLTOALL_TAG,
                               comm->self, &status);
      if (mpi_errno) break;
   }

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

clean_exit:
   g_free(((char *)tmp_buf) + lb);

   return mpi_errno;
}   /* MPID_FN_Alltoall */
#endif   /* MPID_Alltoall */


/**********************************************************************/
#ifdef MPID_Alltoallv
int
MPID_FN_Alltoallv (void *sendbuf, int *sendcnts, int *sdispls,
                   struct MPIR_DATATYPE *sendtype, void *recvbuf,
                   int *recvcnts, int *rdispls,
                   struct MPIR_DATATYPE *recvtype,
                   struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int size, rank;

   /* Get my rank and the size of the communicator */
   (void) MPIR_Comm_size (comm, &size);
   (void) MPIR_Comm_rank (comm, &rank);
   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx, comm);

/* do something here! */

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   return mpi_errno;
}   /* MPID_FN_Alltoallv */
#endif   /* MPID_Alltoallv */


/**********************************************************************/
#ifdef MPID_Reduce
int
MPID_FN_Reduce (void *sendbuf, void *recvbuf, int count,
                struct MPIR_DATATYPE *datatype, MPI_Op op, int root,
                struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int size, my_rank;
   struct MPIR_OP *op_ptr;
   MPI_User_function *uop;
   static char myname[] = "MPI_REDUCE";
   MPI_Aint lb, ub, extent, stride;

   /* Is root within the communicator? */
   (void) MPIR_Comm_size (comm, &size);

#ifndef MPIR_NO_ERROR_CHECKING
   if ( root >= size )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_ROOT_TOOBIG, myname,
                                   (char *)0, (char *)0, root, size);
   else if ( root < 0 )
      mpi_errno = MPIR_Err_setmsg (MPI_ERR_ROOT, MPIR_ERR_DEFAULT, myname,
                                   (char *)0, (char *)0, root);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);
#endif   /* MPIR_NO_ERROR_CHECKING */

   /* See the overview in Collection Operations for why this is ok */
   if ( count == 0 ) return MPI_SUCCESS;

   /* Get my rank and switch communicators to the hidden collective */
   (void) MPIR_Comm_rank (comm, &my_rank);
   comm = comm->comm_coll;
   op_ptr = MPIR_GET_OP_PTR(op);
   MPIR_TEST_MPI_OP(op, op_ptr, comm, myname);
   uop = op_ptr->op;
   MPIR_Type_get_limits(datatype, &lb, &ub);
   extent = ub - lb;
   stride = extent * count;

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

   /* MPI complete reference -- volume 1, 2nd edition (Snir, Otto,
    * Dongarra...), p228: advice to implementors not respected.  The
    * same result might NOT be obtained whenever the function is
    * applied on the same arguments appearing in the same order: to
    * take advantage of the physical location of the processors, the
    * results might vary with the root process (depending on the
    * architectures) */

   /* If the operation is NOT commutative, then it might be faster to
    * MPI_Gather all the data elements to the root proc (using a
    * topology aware algorithm) and let it compute everything.  That
    * also depends on the message size (size * count * extent).  If
    * the message size is very very large, it might be better ignore
    * the protocol levels and resort to an "hypercube algorithm":  for
    * 8 procs:
    *  - phase 1: 1 sends {1} to 0 and 0 computes {0} * {1}
    *             5 sends {5} to 4 and 4 computes {4} * {5}
    *             3 sends {3} to 2 and 2 computes {2} * {3}
    *             7 sends {7} to 6 and 6 computes {6} * {7}
    *  - phase 2: 2 sends {2*3} to 0 and 0 computes {0*1} * {2*3}
    *             6 sends {6*7} to 4 and 4 computes {4*5} * {6*7}
    *  - phase 3: 4 sends {4*5*6*7} to 0 and 0 computes {0*1*2*3} * {4*5*6*7}.
    * But that could incur several WAN-TCP latencies in sequence...  That
    * decision must be made comparing the computation time (msg size) and
    * the latency.
    *
    * In case of non-commutative operation, the MPI_Gather scheme
    * should also be compared with an "hypercube" reduction to
    * process 0, followed by a send to the root of the Reduce. */
   if ( !op_ptr->commute )
   {
      /* If the reduction operation takes too long to compute (long
       * msg, slow CPU, ...), then we should prefer a hypercube
       * reduction algorithm without taking the underlying topology
       * into account.  But what threshold?  depending on the msg
       * size, CPU performance, reduction operation itself (which may
       * be user-defined), ratio between time to compute and time to
       * transfer a msg over the network (including latency and
       * bandwidth)... */
      void *tmp_buf = NULL;  /* dummy initialization to keep compiler quiet */
      int i;

      if ( my_rank == root )
      {
         tmp_buf = (void *) g_malloc_chk(size * stride);
         tmp_buf = (void *) ((char *)tmp_buf - lb);
      }   /* endif */
      mpi_errno = MPI_Gather(sendbuf, count, datatype->self, tmp_buf, count,
                             datatype->self, root, comm->self);
      if ( mpi_errno == MPI_SUCCESS  &&  my_rank == root )
      {
         /* MPI Standard: the operation is always assumed to be associative */
         /* copy the last data element into recvbuf */
         copy_buf((char *) tmp_buf + lb, size - 1,
                  (char *) recvbuf + lb, 0, stride);
         for (i = size-2; i >= 0; i--)
            (*uop)((char *) tmp_buf + i * stride, recvbuf, &count,
                   &(datatype->self));
      }   /* endif */
      if ( my_rank == root )
         g_free((char*)tmp_buf + lb);
   }
   else   /* commutative operation (and always assumed associative!) */
   {
      int lvl;
      comm_set_t *CommSets;
      const int * const Depths = comm->Topology_Depths;
      const int my_depth = Depths[my_rank];
      void *tmp_buf;

      /* first we 'rename' the clusters at each level so that the root
       * process has only zeros as cluster IDs (at each level). */
      update_cluster_ids(root, comm);

      /* find the sets of procs among which I will send/recv msgs */
      CommSets = comm->Topology_CommSets;
      update_comm_sets(my_rank, size, Depths, comm->Topology_ClusterIds,
                       comm->Topology_Colors, CommSets,
                       comm->Topology_ClusterSizes);

      /* if I'm not the global root proc then I need to allocate a temporary
       * buffer to hold the intermediate value in the computation (MPI
       * Standard: recvbuf may NOT be valid if i'm not the root). */
      if ( my_rank != root )
      {
         recvbuf = (void *) g_malloc_chk(stride);
         recvbuf = (void *) ((char *) recvbuf - lb);
      }   /* endif */
      /* copy my element into the recv buffer */
      copy_buf((char *)sendbuf + lb, 0, (char *) recvbuf + lb, 0,
               stride);

      /* allocate a temporary recv buffer */
      tmp_buf = (void *) g_malloc_chk(stride);
      tmp_buf = (void *) ((char *) tmp_buf - lb);

      for (lvl = my_depth-1; lvl >= 0; lvl--)
      {
         /* any one to talk to? */
         if ( CommSets[lvl].size < 2 ) continue;

         /* for high latencies (WAN-TCP) it's more efficient to Gather
          * the data to the local root process and let it compute.
          * For very large msg sizes or small latencies, we should use
          * a binomial-tree algorithm, but what is the threshold
          * between small and large msgs? */

         if ( lvl == MPICHX_WAN_LEVEL )   /* flat-tree algorithm */
            mpi_errno = flat_tree_reduce(CommSets[lvl], recvbuf, count,
                                         datatype->self, comm->self,
                                         uop, tmp_buf);
         else   /* binomial-tree algo */
            mpi_errno = hypercube_reduce(CommSets[lvl], recvbuf, count,
                                         datatype->self, comm->self,
                                         uop, tmp_buf);
         if ( mpi_errno ) break;
      }   /* endfor */

      g_free((char*)tmp_buf + lb);
      if ( my_rank != root )
         g_free((char*)recvbuf + lb);
   }   /* endif: end of commutative case */

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   return mpi_errno;
}   /* MPID_FN_Reduce */
#endif   /* MPID_Reduce */


/**********************************************************************/
/* MPI Standard 1.1: section 4.9.5: "MPI requires that all processes
 * participating in these operations receive identical results."
 * Thus, we cannot implement Allreduce as Allgather() followed by
 * independant computations on each node, because the Globus2 device
 * is heterogeneous.
 * This implementation is Reduce() to root == 0, followed by Bcast
 * from root == 0.  It may be interesting to know if all the machines
 * are really heterogeneous: in case they would be homogeneous, it may
 * be interesting to compare the performance of the current
 * implementation with Allgather() followed by independant
 * computations on each node. */
#ifdef MPID_Allreduce
int
MPID_FN_Allreduce (void *sendbuf, void *recvbuf, int count,
                   struct MPIR_DATATYPE *datatype, MPI_Op op,
                   struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   const int root = 0;
   static char myname[] = "MPI_ALLREDUCE";

   mpi_errno = MPI_Reduce(sendbuf, recvbuf, count, datatype->self, op, root,
                          comm->self);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);
   mpi_errno = MPI_Bcast(recvbuf, count, datatype->self, root, comm->self);
   if ( mpi_errno ) return MPIR_ERROR(comm, mpi_errno, myname);

   return MPI_SUCCESS;
}   /* MPID_FN_Allreduce */
#endif   /* MPID_Allreduce */


/**********************************************************************/
#ifdef MPID_Reduce_scatter
int
MPID_FN_Reduce_scatter (void *sendbuf, void *recvbuf, int *recvcnts,
                        struct MPIR_DATATYPE *datatype, MPI_Op op,
                        struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   int max_cnt, lvl, size, rank, proc, my_depth;
   MPI_Aint extent, ub, lb;
   static char myname[] = "MPI_REDUCE_SCATTER";
   comm_set_t **comm_sets;
   const int * const Depths = comm->Topology_Depths;
   int * const * const Colors = comm->Topology_Colors;
   int * const * const ClusterSizes = comm->Topology_ClusterSizes;
   int *my_Colors, *my_ClusterSizes, **ClusterIds, *cumul_cnts;
   struct MPIR_OP *op_ptr;
   MPI_User_function *uop;

   /* Get my rank and the size of the communicator */
   (void) MPIR_Comm_size (comm, &size);
   (void) MPIR_Comm_rank (comm, &rank);
   /* Switch communicators to the hidden collective */
   comm = comm->comm_coll;
   MPIR_Type_get_limits(datatype, &lb, &ub);
   extent = ub - lb;
   op_ptr = MPIR_GET_OP_PTR(op);
   MPIR_TEST_MPI_OP(op, op_ptr, comm, myname);
   uop = op_ptr->op;
   ClusterIds = comm->Topology_ClusterIds;
   comm_sets = (comm_set_t **) g_malloc_chk(sizeof(comm_set_t *) * size);
   cumul_cnts = (int *) g_malloc_chk(sizeof(int) * (1 + size));
   cumul_cnts[0] = 0;
   my_depth = Depths[rank];
   my_Colors = Colors[rank];
   my_ClusterSizes = (int *) g_malloc_chk(sizeof(int) * my_depth);
   for (lvl = 0; lvl < my_depth; lvl++)
      my_ClusterSizes[lvl] = ClusterSizes[lvl][my_Colors[lvl]];
   for (max_cnt = 0, proc = 0; proc < size; proc++)
   {
      const int cnt = recvcnts[proc];

      if ( cnt > max_cnt ) max_cnt = cnt;
      cumul_cnts[proc+1] = cumul_cnts[proc] + cnt;
      comm_sets[proc] = (comm_set_t *) g_malloc_chk(sizeof(comm_set_t) *
                                                                     my_depth);
      for (lvl = 0; lvl < my_depth; lvl++)
         comm_sets[proc][lvl].set = (int *) g_malloc_chk(sizeof(int) *
                                                         my_ClusterSizes[lvl]);
      update_cluster_ids(proc, comm);
      update_comm_sets(rank, size, Depths, ClusterIds, Colors, comm_sets[proc],
                       ClusterSizes);
   }
   g_free(my_ClusterSizes);

   /* Lock for collective operation */
   MPID_THREAD_LOCK(comm->ADIctx,comm);

   if ( op_ptr->commute )
   {
      /* commutative operation: reduce to the roots determined by the
       * destination of the scatter */
      MPI_Status status;
      void *tmp_buf;

      /* copy the data elements I initially hold into my recv buffer */
      mpi_errno = MPI_Sendrecv((char*) sendbuf + cumul_cnts[rank] * extent,
                               recvcnts[rank], datatype->self, rank,
                               MPIR_REDUCE_SCATTER_TAG, recvbuf, recvcnts[rank],
                               datatype->self, rank, MPIR_REDUCE_SCATTER_TAG,
                               comm->self, &status);

      tmp_buf = (void *) g_malloc_chk(max_cnt * extent);
      tmp_buf = (void *) ((char *) tmp_buf - lb);

      for (lvl = my_depth-1; lvl >= 0; lvl--)
      {
         for (proc = 0; proc < size; proc++)
         {
            const comm_set_t cs = comm_sets[proc][lvl];
            void * const buffer = ( proc == rank ) ? recvbuf :
                                    (char*) sendbuf + cumul_cnts[proc] * extent;

            /* any one to talk to? */
            if ( cs.size < 2  ||  recvcnts[proc] == 0 ) continue;

            if ( lvl == MPICHX_WAN_LEVEL )   /* flat-tree algorithm */
               mpi_errno = flat_tree_reduce(cs, buffer, recvcnts[proc],
                                            datatype->self, comm->self, uop,
                                            tmp_buf);
            else
               mpi_errno = hypercube_reduce(cs, buffer, recvcnts[proc],
                                            datatype->self, comm->self, uop,
                                            tmp_buf);
            if ( mpi_errno ) break;
         }   /* endfor */
      }   /* endfor */
      g_free((char*)tmp_buf + lb);
   }
   else
   {
      /* non-commutative operation: gather to the roots determined by
       * the destination of the scatter + compute all */
      int * const * const Ranks = comm->Topology_Ranks;
      void *buffer;
      MPI_Datatype *types;
      const MPI_Aint stride = extent * size;

      buffer = (void *) g_malloc_chk(stride * cumul_cnts[size]);
      buffer = (char *)buffer - lb;
      for (proc = 0; proc < size; proc++)
      {
         const int cumul = cumul_cnts[proc];
         const MPI_Aint strd = extent * recvcnts[proc];

         copy_buf((char *)sendbuf + lb + extent * cumul, 0,
                  (char *)buffer + lb + stride * cumul + rank * strd, 0,
                  strd);
      }

      types = (MPI_Datatype *) g_malloc_chk(sizeof(MPI_Datatype) * size);
      /* create contiguous datatypes holding all roots' data elements */
      for (proc = 0; proc < size; proc++)
      {
         MPI_Type_contiguous(recvcnts[proc], datatype->self, types + proc);
         mpi_errno = MPI_Type_commit(types + proc);
         if ( mpi_errno ) goto clean_exit;
      }

      /* Gather data elements to the proper roots */
      for (lvl = my_depth-1; lvl >= 0; lvl--)
      {
         for (proc = 0; proc < size; proc++)
         {
            const comm_set_t cs = comm_sets[proc][lvl];

            /* any one to talk to? */
            if ( cs.size < 2  ||  recvcnts[proc] == 0 ) continue;

            if ( lvl == MPICHX_WAN_LEVEL )   /* WAN TCP: flat tree */
               mpi_errno =
                       flat_tree_gather((char*)buffer + stride*cumul_cnts[proc],
                                        types[proc], comm->self, 0, cs, Depths,
                                        Colors, Ranks, ClusterSizes, lvl);
            else   /* binomial tree algorithm */
               mpi_errno =
                        binomial_gather((char*)buffer + stride*cumul_cnts[proc],
                                        types[proc], comm->self, 0, cs, Depths,
                                        Colors, Ranks, ClusterSizes, lvl);
            if ( mpi_errno ) goto clean_exit;
         }   /* endfor */
      }   /* endfor */

      /* compute all */
      if ( recvcnts[rank] > 0 )
      {
         int my_cnt = recvcnts[rank];
         void * const my_buf = (char *)buffer + stride * cumul_cnts[rank];

         copy_buf((char *)my_buf + lb, size-1,
                  (char *)recvbuf + lb, 0, my_cnt * extent);
         for (proc = size-2; proc >= 0; proc--)
            (*uop)((char *)my_buf + proc * extent * my_cnt,
                   recvbuf, &my_cnt, &(datatype->self));
      }   /* endif */

clean_exit:
      for (proc = 0; proc < size; proc++)
         MPI_Type_free(types + proc);
      g_free(types);
      g_free((char*)buffer + lb);
   }

   /* Unlock for collective operation */
   MPID_THREAD_UNLOCK(comm->ADIctx,comm);

   for (proc = 0; proc < size; proc++)
   {
      for (lvl = 0; lvl < my_depth; lvl++)
         g_free(comm_sets[proc][lvl].set);
      g_free(comm_sets[proc]);
   }
   g_free(comm_sets);
   g_free(cumul_cnts);

   return mpi_errno;
}   /* MPID_FN_Reduce_scatter */
#endif   /* MPID_Reduce_scatter */


/**********************************************************************/
#ifdef MPID_Scan
int
MPID_FN_Scan (void *sendbuf, void *recvbuf, int count,
              struct MPIR_DATATYPE *datatype, MPI_Op op,
              struct MPIR_COMMUNICATOR *comm)
{
   int mpi_errno = MPI_SUCCESS;
   static char myname[] = "MPI_SCAN";
   int i, size, rank;
   MPI_Aint stride, lb, ub;
   void *buffer;
   MPI_Status status;
   struct MPIR_OP *op_ptr;
   MPI_User_function *uop;

   /* See the overview in Collection Operations for why this is ok */
   if (count == 0) return MPI_SUCCESS;

   /* Get the size of the communicator */
   (void) MPIR_Comm_size(comm, &size);
   (void) MPIR_Comm_rank(comm, &rank);

   op_ptr = MPIR_GET_OP_PTR(op);
   MPIR_TEST_MPI_OP(op, op_ptr, comm, myname);
   uop = op_ptr->op;

   /* get the extent of the data type */
   MPIR_Type_get_limits(datatype, &lb, &ub);
   stride = (ub - lb) * count;

   /* allocate buffer to hold all data elements to gather */
   buffer = (void *) g_malloc_chk(stride * size);
   buffer = (char *)buffer - lb;

   /* (all)gather all the data elements on all processes */
   mpi_errno = MPI_Allgather(sendbuf, count, datatype->self, buffer, count,
                             datatype->self, comm->self);
   if ( mpi_errno ) goto clean_exit;

   mpi_errno = MPI_Sendrecv(sendbuf, count, datatype->self, rank,
                            MPIR_SCAN_TAG, recvbuf, count, datatype->self,
                            rank, MPIR_SCAN_TAG, comm->self, &status);
   if ( mpi_errno ) goto clean_exit;

   /* compute */
   for (i = rank-1; i >= 0; i--)
      (*uop)((char *)buffer + i * stride, recvbuf, &count, &(datatype->self));

clean_exit:
   g_free((char*)buffer + lb);
   return mpi_errno;
}   /* MPID_FN_Scan */
#endif   /* MPID_Scan */

