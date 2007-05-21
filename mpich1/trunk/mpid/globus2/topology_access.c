
/*
 * CVS Id: $Id: topology_access.c,v 1.6 2004/05/11 18:11:12 karonis Exp $
 */

/* This module allows the user to access the underlying topology of
 * processes */

/* This implementation cannot resist to the following attacks by a
 * user:
 *  - freeing a key with MPI_Keyval_free();
 *  - freeing the memory allocated by MPICH-G2 for the information
 *    (depths and colors) passed to the user;
 *  - caching other data using the keys and MPI_Attr_put().
 *
 *  This could be partly solved making the attributes to retrieve the
 *  depths and colors permanent in MPICH core. */


#include "chconfig.h"
#include "mpiimpl.h"
#include "topology_access.h"
#include "mem.h"


/**********************************************************************/
/* GLOBAL VARIABLES                                                   */
/**********************************************************************/

/* initialization of the keys to invalid value */
int MPICHX_TOPOLOGY_DEPTHS = MPI_KEYVAL_INVALID;
int MPICHX_TOPOLOGY_COLORS = MPI_KEYVAL_INVALID;



/**********************************************************************/
/* PRIVATE VARIABLES                                                  */
/**********************************************************************/

/* copies of the key values, backup in case the user tries to mess
 * them up */
static int PRIVATE_TOPOLOGY_DEPTHS_KEY = MPI_KEYVAL_INVALID;
static int PRIVATE_TOPOLOGY_COLORS_KEY = MPI_KEYVAL_INVALID;



/**********************************************************************/
/* PRIVATE FUNCTIONS                                                  */
/**********************************************************************/


/**********************************************************************/
/* this function is called as a communicator is destroyed: free memory
 * used for the array of Topology_Depths; it is also called by
 * MPI_Attr_delete() and MPI_Attr_put(), but this should not happen. */
int
mpichx_topology_depths_destructor (MPI_Comm comm, int key, void *attr,
                                   void *extra)
{
   int mpi_errno = MPI_SUCCESS;
   int *Depths;

   MPICHX_TOPOLOGY_DEPTHS = PRIVATE_TOPOLOGY_DEPTHS_KEY;
   MPICHX_TOPOLOGY_COLORS = PRIVATE_TOPOLOGY_COLORS_KEY;

   Depths = (int *) attr;
   g_free(Depths);

   return mpi_errno;
}


/**********************************************************************/
/* this function is called as a communicator is destroyed: free memory
 * used for the 2D-array of Topology_Colors */
int
mpichx_topology_colors_destructor (MPI_Comm comm, int key, void *attr,
                                   void *extra)
{
   int mpi_errno = MPI_SUCCESS;
   int i, size;
   int **Colors;

   MPICHX_TOPOLOGY_DEPTHS = PRIVATE_TOPOLOGY_DEPTHS_KEY;
   MPICHX_TOPOLOGY_COLORS = PRIVATE_TOPOLOGY_COLORS_KEY;

   Colors = (int **) attr;
   (void) MPI_Comm_size(comm, &size);

   for (i = 0; i < size; i++)
      g_free(Colors[i]);
   g_free(Colors);

   return mpi_errno;
}


/**********************************************************************/
/* PUBLIC FUNCTIONS                                                   */
/**********************************************************************/

/**********************************************************************/
/* create the topology keys to access the Depths and Colors of the
 * processes (information cached in the communicators).  Also create a
 * copy to backup these keys, in case the user messes them up.  This
 * function is called at initialization time by MPID_Init(). */
extern void
create_topology_access_keys (void)
{
   /* The variables "PRIVATE_TOPOLOGY_XXX_KEY" are not visible by the
    * user: they are a backup in case the user modifies the values of
    * his keys "MPICHX_TOPOLOGY_XXX".  The copy function attached to
    * the keys is NULL because the attribute is put into a
    * communicator at creation time in function MPID_Comm_init(). */
   MPI_Keyval_create(MPI_NULL_COPY_FN, mpichx_topology_depths_destructor,
                     &PRIVATE_TOPOLOGY_DEPTHS_KEY, NULL);
   MPICHX_TOPOLOGY_DEPTHS = PRIVATE_TOPOLOGY_DEPTHS_KEY;

   MPI_Keyval_create(MPI_NULL_COPY_FN, mpichx_topology_colors_destructor,
                     &PRIVATE_TOPOLOGY_COLORS_KEY, NULL);
   MPICHX_TOPOLOGY_COLORS = PRIVATE_TOPOLOGY_COLORS_KEY;

   return;
}


/**********************************************************************/
/* free the topology keys; this function is called by MPID_End(). */
extern void
destroy_topology_access_keys (void)
{
   /* freeing keys to access the underlying topology (attribute caching) */
   MPI_Keyval_free(&PRIVATE_TOPOLOGY_DEPTHS_KEY);
   MPICHX_TOPOLOGY_DEPTHS = PRIVATE_TOPOLOGY_DEPTHS_KEY;

   MPI_Keyval_free(&PRIVATE_TOPOLOGY_COLORS_KEY);
   MPICHX_TOPOLOGY_COLORS = PRIVATE_TOPOLOGY_COLORS_KEY;

   return;
}


/**********************************************************************/
/* put the topology information (Depths & Colors) into the
 * communicator; this function is called by topology_initialization()
 * when a communicator is created/initilized in MPID_Comm_init(). */

int
cache_topology_information (struct MPIR_COMMUNICATOR * const comm)
{
   int mpi_errno = MPI_SUCCESS;
   int flag, size;
   int *Depths, **Colors;

   /* reset the publicly available topology access keys to the value
    * backed up in the private variables, in case the user may have
    * changed these values */
   MPICHX_TOPOLOGY_DEPTHS = PRIVATE_TOPOLOGY_DEPTHS_KEY;
   MPICHX_TOPOLOGY_COLORS = PRIVATE_TOPOLOGY_COLORS_KEY;

   (void) MPIR_Comm_size (comm, &size);

   mpi_errno = MPI_Attr_get(comm->self, PRIVATE_TOPOLOGY_DEPTHS_KEY, &Depths,
                            &flag);
   if ( mpi_errno ) return mpi_errno;

   /* The flag must be tested because a communicator and its attached
    * collective operations communicator share the same attributes. */
   if ( !flag )   /* cache the attribute (Depths array) */
   {
      const int * const Topology_Depths = comm->Topology_Depths;
      int p;

      /* copy the information available at the user level: the user must
       * not have a direct access to the pointer to the real data used by
       * the MPICH library */
      Depths = (int *) g_malloc_chk(sizeof(int) * size);

      for (p = 0; p < size; p++)
         Depths[p] = Topology_Depths[p];
      mpi_errno = MPI_Attr_put(comm->self, PRIVATE_TOPOLOGY_DEPTHS_KEY, Depths);
      if ( mpi_errno ) return mpi_errno;
   }

   mpi_errno = MPI_Attr_get(comm->self, PRIVATE_TOPOLOGY_COLORS_KEY, &Colors,
                            &flag);
   if ( mpi_errno ) return mpi_errno;

   /* The flag must be tested because a communicator and its attached
    * collective operations communicator share the same attributes. */
   if ( !flag )   /* cache the attribute (Colors array) */
   {
      int * const * const Topology_Colors = comm->Topology_Colors;
      int p;

      /* copy the information available at the user level: the user must
       * not have a direct access to the pointer to the real data used by
       * the MPICH library */
      Colors = (int **) g_malloc_chk(sizeof(int *) * size);

      for (p = 0; p < size; p++)
      {
         int lvl;
         const int depth = Depths[p];
         const int * const column = Topology_Colors[p];

         Colors[p] = (int *) g_malloc_chk(sizeof(int) * depth);
         for (lvl = 0; lvl < depth; lvl++)
            Colors[p][lvl] = column[lvl];
      }

      mpi_errno = MPI_Attr_put(comm->self, PRIVATE_TOPOLOGY_COLORS_KEY, Colors);
      if ( mpi_errno ) return mpi_errno;
   }

   return mpi_errno;
}

