
/*
 * CVS Id: $Id: topology_clusters.c,v 1.43 2004/05/11 18:11:12 karonis Exp $
 */

#include "chconfig.h"
#include <globdev.h>
#include "mpid.h"
#include "mpiimpl.h"
#include "protos.h"
#include "attr.h"
#include "mem.h"

#include "topology_intra_fns.h"
#include "topology_access.h"


/**********************************************************************/
/* PRIVATE FUNCTIONS                                                  */
/**********************************************************************/


/**********************************************************************/
/* dump the topology information attached to a given communicator */
static void
print_topology (struct MPIR_COMMUNICATOR * const comm)
{
   int max_depth = 0;
   int lvl, proc, rank, size;

   (void) MPIR_Comm_size(comm, &size);
   (void) MPIR_Comm_rank(comm, &rank);

   globus_libc_fprintf(stderr, "*** Start print topology from proc #%d/%d\n",
                       rank, size);
   globus_libc_fprintf(stderr, "Sizes of my clusters:\n");
   for (lvl = 0; lvl < comm->Topology_Depths[rank]; lvl++)
      globus_libc_fprintf(stderr, "Level %d: %d procs\n", lvl,
            comm->Topology_ClusterSizes[lvl][comm->Topology_Colors[rank][lvl]]);
   globus_libc_fprintf(stderr, "proc\t");
   for (proc = 0; proc < size; proc++)
      globus_libc_fprintf(stderr, "% 3d", proc);
   globus_libc_fprintf(stderr, "\ndepths\t");
   for (proc = 0; proc < size; proc++)
   {
      if ( max_depth < comm->Topology_Depths[proc] )
         max_depth = comm->Topology_Depths[proc];
      globus_libc_fprintf(stderr, "% 3d", comm->Topology_Depths[proc]);
   }
   globus_libc_fprintf(stderr, "\nCOLORS:");
   for (lvl = 0; lvl < max_depth; lvl++)
   {
      globus_libc_fprintf(stderr, "\nlvl %d\t", lvl);
      for (proc = 0; proc < size; proc++)
         if ( lvl < comm->Topology_Depths[proc] )
            globus_libc_fprintf(stderr, "% 3d",
                                comm->Topology_Colors[proc][lvl]);
         else
            globus_libc_fprintf(stderr, "   ");
   }
   globus_libc_fprintf(stderr, "\nPROCESS_RANKS:");
   for (lvl = 0; lvl < max_depth; lvl++)
   {
      globus_libc_fprintf(stderr, "\nlvl %d\t", lvl);
      for (proc = 0; proc < size; proc++)
         if ( lvl < comm->Topology_Depths[proc] )
            globus_libc_fprintf(stderr, "% 3d",
                                comm->Topology_Ranks[proc][lvl]);
         else
            globus_libc_fprintf(stderr, "   ");
   }
   globus_libc_fprintf(stderr, "\nCLUSTER_IDS:");
   for (lvl = 0; lvl < max_depth; lvl++)
   {
      globus_libc_fprintf(stderr, "\nlvl %d\t", lvl);
      for (proc = 0; proc < size; proc++)
         if ( lvl < comm->Topology_Depths[proc] )
            globus_libc_fprintf(stderr, "% 3d",
                                comm->Topology_ClusterIds[proc][lvl]);
         else
            globus_libc_fprintf(stderr, "   ");
   }
   globus_libc_fprintf(stderr, "\n");
   globus_libc_fprintf(stderr, "*** End print topology from proc #%d/%d\n",
                       rank, size);
   return;
}


/**********************************************************************/
/* return the number of protocol levels thru which a process can
 * communicate */
static int
num_protos_in_channel (struct channel_t * const cp)
{
   struct miproto_t *mp;
   int rc = 0;

   if (!cp)
   {
      globus_libc_fprintf(stderr,
               "\tERROR: num_protos_in_channel(): grank %d: passed NULL cp\n",
               MPID_MyWorldRank);
      MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2", "");
   } /* end if */

   for (mp = cp->proto_list; mp; mp = mp->next)
   {
      switch (mp->type)
      {
         /* TCP: 1 for localhost + 1 for LAN + 1 for WAN */
         case tcp: rc += 3; break;
         case mpi: rc ++; break;
         default: globus_libc_fprintf(stderr,
                     "\tERROR: num_protos_in_channel(): grank %d: encountered "
                     "unrecognized proto type %d", MPID_MyWorldRank, mp->type);
                  MPID_Abort( (struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2", "");
      } /* end switch */
   } /* end for */

   return rc;
} /* end num_protos_in_channel() */


/**********************************************************************/
/* return TRUE if the two processes can talk to each other at the
 * given level; FALSE otherwise */
static int
channels_proto_match (struct channel_t * const cp0,
                      struct channel_t * const cp1, const int level)
{
   struct miproto_t *mp0, *mp1;
   enum proto type = unknown; /* dummy initialization to keep compiler quiet */

   /* if level == MPICHX_WAN_LEVEL, then they always match */
   if ( level == MPICHX_WAN_LEVEL ) return GLOBUS_TRUE;

   switch ( level )
   {
      /* level != MPICHX_WAN_LEVEL */
      case MPICHX_LAN_LEVEL:
      case MPICHX_HOST_LEVEL: type = tcp; break;
      case MPICHX_VMPI_LEVEL: type = mpi; break;
      default:
         MPID_Abort((struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2 Internal",
                    "channels_proto_match(): unrecognized topology level");
   }

   /* finding the correct protos in each channel */
   for (mp0 = cp0->proto_list; mp0; mp0 = mp0->next)
      if ( mp0->type == type ) break;
   for (mp1 = cp1->proto_list; mp1; mp1 = mp1->next)
      if ( mp1->type == type ) break;

   if ( mp0  &&  mp1  &&  (mp0->type == mp1->type) )
   {
      /* now that i have correct proto for each, seeing if they match */
      switch ( level )
      {
         /* level != MPICHX_WAN_LEVEL */
         case MPICHX_LAN_LEVEL:   /* are the procs in the same LAN? */
         {
            if ( !strcmp(((struct tcp_miproto_t *)(mp0->info))->globus_lan_id,
                         ((struct tcp_miproto_t *)(mp1->info))->globus_lan_id) )
               return GLOBUS_TRUE;
            else
               return GLOBUS_FALSE;
         }
         case MPICHX_HOST_LEVEL:/* are the procs on the same localhost? */
         {
            if ( ((struct tcp_miproto_t *) (mp0->info))->localhost_id ==
                 ((struct tcp_miproto_t *) (mp1->info))->localhost_id )
               return GLOBUS_TRUE;
            else
               return GLOBUS_FALSE;
         }
         case MPICHX_VMPI_LEVEL:
         {
            if ( !strcmp( ((struct mpi_miproto_t *) 
                                        (mp0->info))->unique_session_string,
                          ((struct mpi_miproto_t *) 
                                        (mp1->info))->unique_session_string ) )
               return GLOBUS_TRUE;
            else
               return GLOBUS_FALSE;
         }
         default:
            MPID_Abort((struct MPIR_COMMUNICATOR *)0, 1, "MPICH-G2 Internal",
                  "channels_proto_match(): unrecognized topology level");
      } /* end switch */
   } /* end if */

   return GLOBUS_FALSE;
} /* end channels_proto_match() */



/**********************************************************************/
/* PUBLIC FUNCTIONS                                                   */
/**********************************************************************/

/**********************************************************************/
/* initialize Topology_Depths, Topology_Colors, Topology_ClusterIds,
 * Topology_Ranks, Topology_ClusterSizes for topology-aware collective
 * operations and topology reporting to MPI app */
int
topology_initialization (struct MPIR_COMMUNICATOR * const comm)
{
   int lvl, my_depth, max_depth, rank, size, p0;
   int mpi_errno = MPI_SUCCESS;
   int *Depths, **ClusterIds, **Colors, **ClusterSizes, **Ranks;
   comm_set_t *CommSets;

   /* don't do anything for intercommunicators */
   if (comm->comm_type == MPIR_INTER) return mpi_errno;

   (void) MPIR_Comm_rank(comm, &rank);
   (void) MPIR_Comm_size(comm, &size);

   /* allocate memory for the array of depths */
   Depths = (int *) g_malloc_chk(size * sizeof(int));
   comm->Topology_Depths = Depths;

   /* allocate memory for the 2D-array of colors */
   Colors = (int **) g_malloc_chk(size * sizeof(int *));
   comm->Topology_Colors = Colors;

   /* allocate memory for the 2D-array of cluster IDs */
   ClusterIds = (int **) g_malloc_chk(size * sizeof(int *));
   comm->Topology_ClusterIds = ClusterIds;

   /* allocate memory for the 2D-array of process ranks (inside a
    * cluster at a given level) */
   Ranks = (int **) g_malloc_chk(size * sizeof(int *));
   comm->Topology_Ranks = Ranks;


   /*********************************/
   /* Phase 1 of 3 - Finding Depths */
   /*********************************/

   max_depth = 0;
   for (p0 = 0; p0 < size; p0++)
   {
      struct channel_t *chanl;
      int depth;

      chanl = get_channel(comm->lrank_to_grank[p0]);
      if ( !chanl )
         MPID_Abort((struct MPIR_COMMUNICATOR *)0, 2, "MPICH-G2 Internal",
                    "topology_initialization() - NULL channel returned");
      Depths[p0] = depth = num_protos_in_channel(chanl);

      if (depth > max_depth) max_depth = depth;

      /* allocate memory for the colors and cluster-IDs */
      Colors[p0] = (int *) g_malloc_chk(depth * sizeof(int));
      ClusterIds[p0] = (int *) g_malloc_chk(depth * sizeof(int));
      Ranks[p0] = (int *) g_malloc_chk(depth * sizeof(int));

      /* initialize the colors and cluster IDs to invalid value -1 */
      for (lvl = 0; lvl < depth; lvl ++)
      {
         Colors[p0][lvl] = -1;
         ClusterIds[p0][lvl] = -1;
      }
   } /* endfor */
   my_depth = Depths[rank];

   /* allocate memory for the sets of communicating processes I will
    * be involved in */
   CommSets = (comm_set_t *) g_malloc_chk(my_depth * sizeof(comm_set_t));
   comm->Topology_CommSets = CommSets;

   /* allocate memory for the sizes of the clusters I belong to at
    * each communication level */
   ClusterSizes = (int **) g_malloc_chk(sizeof(int *) * max_depth);
   comm->Topology_ClusterSizes = ClusterSizes;


   /***************************/
   /* Phase 2 of 3 - Coloring */
   /***************************/

   for (lvl = 0; lvl < max_depth; lvl ++)
   {
      int next_color = 0;

      for (p0 = 0; p0 < size; p0++)
      {
         int rank, p1, color0;

         if ( lvl >= Depths[p0] ) continue;

         color0 = Colors[p0][lvl];
         if ( color0 == -1 )
         {
            /* this proc has not been colored at this level yet, i.e.,
             * it hasn't matched any of the procs to the left at this
             * level yet ... ok, start new color at this level. */

            struct channel_t *chanl0;

            chanl0 = get_channel(comm->lrank_to_grank[p0]);
            if ( !chanl0 )
               MPID_Abort((struct MPIR_COMMUNICATOR *)0, 2, "MPICH-G2 Internal",
                          "topology_initialization() - NULL channel returned");

            Colors[p0][lvl] = color0 = next_color ++;
            for (p1 = p0 + 1; p1 < size; p1++)
            {
               if (lvl < Depths[p1] && Colors[p1][lvl] == -1)
               {
                  struct channel_t *chanl1;;

                  chanl1 = get_channel(comm->lrank_to_grank[p1]);
                  if ( !chanl1 )
                     MPID_Abort((struct MPIR_COMMUNICATOR *)0, 2,
                                "MPICH-G2 Internal",
                                "topology_initialization() - NULL channel");

                  if (channels_proto_match(chanl0, chanl1, lvl))
                     Colors[p1][lvl] = color0;
               } /* endif (depths and colors) */
            } /* endfor (p1) */
         } /* endif (color) */

         /* determine the rank of this process inside its cluster */
         for (rank = 0, p1 = 0; p1 < p0; p1++)
         {
            if ( lvl < Depths[p1]  &&  Colors[p1][lvl] == color0 )
               rank++;
         } /* endfor (p1) */
         Ranks[p0][lvl] = rank;
      } /* endfor (p0) */

      /* determine the sizes of the clusters at this level */
      ClusterSizes[lvl] = (int *) g_malloc_chk(sizeof(int) * next_color);
      for (p0 = 0; p0 < next_color; p0++)
         ClusterSizes[lvl][p0] = 0;
      for (p0 = 0; p0 < size; p0++)
         if ( Depths[p0] > lvl )
            ClusterSizes[lvl][Colors[p0][lvl]]++;
   } /* endfor (lvl) */


   /************************************************/
   /* Phase 3 of 3 - Setting CID's based on colors */
   /************************************************/

   for (lvl = max_depth-1; lvl >= 0; lvl--)
   {
      for (p0 = 0; p0 < size; p0 ++)
      {
         if (lvl < Depths[p0] && ClusterIds[p0][lvl] == -1)
         {
            /* p0 has not been assigned a cid at this level yet, which
             * means all the procs at this level that have the same
             * color as p0 at this level have also not been assigned
             * cid's yet.
             *
             * find the rest of the procs at this level that have the
             * same color as p0 and assign them cids.  same color are
             * enumerated.  */

            int next_cid, p1;
            const int color0 = Colors[p0][lvl];

            ClusterIds[p0][lvl] = 0;
            next_cid = 1;

            for (p1 = p0 + 1; p1 < size; p1++)
            {
               if ( lvl < Depths[p1]  &&  color0 == Colors[p1][lvl] )
               {
                  /* p0 and p1 match colors at this level, which means
                   * p1 will now get its cid set at this level.  but
                   * to what value?  if p1 also matches color with any
                   * proc to its left at level lvl+1, then p1 copies
                   * that proc's cid at this level, otherwise p1 gets
                   * the next cid value at this level.  */
                  const int next_lvl = lvl + 1;

                  if (next_lvl < Depths[p1])
                  {
                     int p2;
                     const int color1 = Colors[p1][lvl];
                     const int next_color1 = Colors[p1][next_lvl];

                     for (p2 = 0; p2 < p1; p2 ++)
                     {
                        if ( next_lvl < Depths[p2] &&
                             color1 == Colors[p2][lvl] &&
                             next_color1 == Colors[p2][next_lvl])
                        {
                           ClusterIds[p1][lvl] = ClusterIds[p2][lvl];
                           break;
                        }
                     } /* endfor */
                     if (p2 == p1)
                        /* did not find one */
                        ClusterIds[p1][lvl] = next_cid ++;
                  }
                  else
                     /* p1 does not have a level lvl+1 */
                     ClusterIds[p1][lvl] = next_cid ++;
               } /* endif */
            } /* endfor */
         } /* endif */
      } /* endfor */
   } /* endfor */

   /* allocate enough memory for the sets of communicating processes
    * at each level */
   for (lvl = 0; lvl < my_depth; lvl++)
   {
      /* number of procs I may have to talk to at this level */
      int n_cid = 0;
      int my_color = Colors[rank][lvl];

      for (p0 = 0; p0 < size; p0++)
         if ( Depths[p0] > lvl  &&  my_color == Colors[p0][lvl]  &&
              ClusterIds[p0][lvl] > n_cid )
            n_cid = ClusterIds[p0][lvl];
      CommSets[lvl].set = (int *) g_malloc_chk(sizeof(int) * (n_cid + 1));
   }

   mpi_errno = cache_topology_information(comm);

/*
if ( size > 1  &&  rank == 1 ) print_topology(comm);
*/

   return mpi_errno;
}


/**********************************************************************/
/* free memory used for the topology information attached to the given
 * communicator */
void
topology_destruction (struct MPIR_COMMUNICATOR * const comm)
{
   int my_depth, i, rank, size;
   int max_depth = 0;

   (void) MPIR_Comm_rank(comm, &rank);
   (void) MPIR_Comm_size(comm, &size);

   /* don't do anything for intercommunicators */
   if (comm->comm_type == MPIR_INTER) return;

   for (i = 0; i < size; i ++)
   {
      g_free(comm->Topology_Colors[i]);
      g_free(comm->Topology_ClusterIds[i]);
      g_free(comm->Topology_Ranks[i]);
      if ( max_depth < comm->Topology_Depths[i] )
         max_depth = comm->Topology_Depths[i];
   }
   g_free(comm->Topology_Colors);
   comm->Topology_Colors = NULL;
   g_free(comm->Topology_ClusterIds);
   comm->Topology_ClusterIds = NULL;
   g_free(comm->Topology_Ranks);
   comm->Topology_Ranks = NULL;

   my_depth = comm->Topology_Depths[rank];
   g_free(comm->Topology_Depths);
   comm->Topology_Depths = NULL;

   for (i = 0; i < my_depth; i ++)
      g_free(comm->Topology_CommSets[i].set);
   g_free(comm->Topology_CommSets);
   comm->Topology_CommSets = NULL;

   for (i = 0; i < max_depth; i++)
      g_free(comm->Topology_ClusterSizes[i]);
   g_free(comm->Topology_ClusterSizes);
   comm->Topology_ClusterSizes = NULL;

   return;
}

