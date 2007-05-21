#include "chconfig.h"
#include <globdev.h>
#include "topology_clusters.h"

extern void *			VMPI_Internal_Comm;

/* START GRIDFTP */
extern int MPICHX_PARALLELSOCKETS_PARAMETERS; 

int enable_gridftp(struct MPIR_COMMUNICATOR *comm, void *attr_value); 
/* END GRIDFTP */

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Comm_init
int MPID_Comm_init(
    struct MPIR_COMMUNICATOR *		oldcomm,
    struct MPIR_COMMUNICATOR *		newcomm)
{
    int					rc;
#ifdef VMPI
    int					i;
    int					vlnp;
#endif   /* VMPI */

    DEBUG_FN_ENTRY(DEBUG_MODULE_COMM);

    rc = MPI_SUCCESS;

#ifdef VMPI
    if (newcomm != NULL)
    {
	newcomm->vmpi_comm = NULL;
	newcomm->lrank_to_vlrank = NULL;
	newcomm->vlrank_to_lrank = NULL;
	newcomm->vgrank_to_vlrank = NULL;
    }

    /*
     * If we can't use the vendor MPI to communicate then we are done
     */
    if (VMPI_MyWorldRank < 0
	|| (oldcomm != NULL && oldcomm->vmpi_comm == NULL))
    {
	goto fn_exit;
    }

    /*
     * If we aren't part of the new communicator, then we don't need to
     * initialze any of the data structures.  However, we still need to
     * participate in the operation.
     */
    if (newcomm == NULL)
    {
	vlnp = 0;
	goto skip_newcomm_init;
    }
    
    /*
     * Establish a mapping from global ranks in the vendor MPI_COMM_WORLD to
     * local ranks in the new MPICH communicator, and a mapping from local
     * ranks in the new MPICH communicator to local ranks in the vendor
     * communicator
     */
    newcomm->vgrank_to_vlrank = (int *)
	globus_libc_malloc(VMPI_MyWorldSize * sizeof(int));
    if (newcomm->vgrank_to_vlrank == NULL)
    {
	rc = MPI_ERR_EXHAUSTED;
	goto fn_abort;
    }
    for (i = 0; i < VMPI_MyWorldSize; i++)
    {
	newcomm->vgrank_to_vlrank[i] = -1;
    }

    newcomm->lrank_to_vlrank = (int *)
	globus_libc_malloc(newcomm->np * sizeof(int));
    if (newcomm->lrank_to_vlrank == NULL)
    {
	rc = MPI_ERR_EXHAUSTED;
	goto fn_abort;
    }

    vlnp = 0;
    for (i = 0; i < newcomm->np; i++)
    {
	int				vgrank;

	/* fixing assignment of vgrank to accommodate the fact that
	 * since the introduction of the MPI-2 stuff it is possible
	 * for lrank_to_grank (i.e., grank) to be larger that
	 * MPID_MyWorldSize.  this will happen when the new comm
	 * has procs that are outside the orig MPI_COMM_WORLD 
	 * NOTE: that it is invalid to index VMPI_GRank_to_VGRank
	 *       with a value >= MPID_MyWorldSize
	 */
	/* vgrank = VMPI_GRank_to_VGRank[newcomm->lrank_to_grank[i]]; */
	vgrank = (newcomm->lrank_to_grank[i] < MPID_MyWorldSize
		? VMPI_GRank_to_VGRank[newcomm->lrank_to_grank[i]]
		: -1);
	if (vgrank >= 0)
	{
	    if (vgrank >= VMPI_MyWorldSize)
	    {
		MPID_Abort(NULL,
			   0,
			   "MPICH-G2 (internal error)",
			   "MPID_CommInit() - vgrank >= VMPI_MyWorldSize");
	    } /* endif */
	    newcomm->vgrank_to_vlrank[vgrank] = vlnp;
	    newcomm->lrank_to_vlrank[i] = vlnp;
	    DEBUG_PRINTF(DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
			 ("newcomm->lrank_to_vlrank[%d]=%d\n",
			  i, vlnp));

	    vlnp++;
	}
	else
	{
	    DEBUG_PRINTF(DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
			 ("newcomm->vgrank_to_vlrank[%d]=-1\n", vlnp));
	    newcomm->lrank_to_vlrank[i] = -1;
	}
    }

#   if DEBUG_CHECK(DEBUG_MODULE_COMM, DEBUG_INFO_MISC)
    {
	for (i = 0; i < VMPI_MyWorldSize; i++)
	{
	    DEBUG_PRINTF(DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
			 ("newcomm->vgrank_to_vlrank[%d]=%d\n",
			  i, newcomm->vgrank_to_vlrank[i]));

	}
    }
#   endif
    
    DEBUG_PRINTF(DEBUG_MODULE_COMM, DEBUG_INFO_MISC,
		 ("newcomm->np=%d newcomm->vlnp=%d\n", newcomm->np, vlnp));

    if (vlnp == 0)
    {
	/*
	 * We are not part of the new communicator, so we should free up our
	 * data structures
	 */
	g_free(newcomm->vgrank_to_vlrank);
	g_free(newcomm->lrank_to_vlrank);

	newcomm->vgrank_to_vlrank = NULL;
	newcomm->lrank_to_vlrank = NULL;

	goto skip_newcomm_init;
    }

    /*
     * Establish a mapping from local ranks in the vendor communicator to
     * local ranks in the new MPICH communicator
     */
    newcomm->vlrank_to_lrank = (int *)
	globus_libc_malloc(vlnp * sizeof(int));
    if (newcomm->vlrank_to_lrank == NULL)
    {
	rc = MPI_ERR_EXHAUSTED;
	goto fn_abort;
    }

    for (i = 0; i < newcomm->np; i++)
    {
	int				vlrank;

	vlrank = newcomm->lrank_to_vlrank[i];
	if (vlrank >= 0)
	{
	    if (vlrank >= vlnp)
	    {
		MPID_Abort(NULL,
			   0,
			   "MPICH-G2 (internal error)",
			   "MPID_CommInit() - vlrank >= vlnp");
	    } /* endif */
	    newcomm->vlrank_to_lrank[vlrank] = i;
	}
    }
    
#   if DEBUG_CHECK(DEBUG_MODULE_COMM, DEBUG_INFO_MISC)
    {
	for (i = 0; i < vlnp; i++)
	{
	    DEBUG_PRINTF_NOCHECK(
		("newcomm->vlrank_to_lrank[%d]=%d\n",
		 i, newcomm->vlrank_to_lrank[i]));
	}
    }
#   endif
    
    newcomm->vmpi_comm = (int *)
	globus_libc_malloc(mp_comm_get_size());
    if (newcomm->vmpi_comm == NULL)
    {
	rc = MPI_ERR_EXHAUSTED;
	goto fn_abort;
    }

  skip_newcomm_init:

    if (oldcomm == NULL && newcomm == NULL)
    {
	MPID_Abort(NULL,
		   0,
		   "MPICH-G2 (internal error)",
		   "MPID_CommInit() - oldcomm = NULL && newcomm = NULL");
    }

    /*
     * If we are creating a new intra-communicator, then issue a vendor MPI
     * Comm_split().
     */
    if ((oldcomm == NULL || oldcomm->comm_type == MPIR_INTRA)
	&& (newcomm == NULL || newcomm->comm_type == MPIR_INTRA))
    {
	int				color;
	int				key;
	
	if (vlnp > 0)
	{
	    color = newcomm->lrank_to_grank[newcomm->vlrank_to_lrank[0]];
	    key = newcomm->lrank_to_vlrank[newcomm->local_rank];
	}
	else
	{
	    color = VMPI_UNDEFINED;
	    key = 0;
	}
	
	rc = vmpi_error_to_mpich_error(
	    mp_comm_split(oldcomm ? oldcomm->vmpi_comm : NULL,
			  color,
			  key,
			  newcomm ? newcomm->vmpi_comm : NULL));
	if (rc != MPI_SUCCESS)
	{
	    goto fn_abort;
	}

	goto fn_exit;
    }


    /*
     * Otherwise, we are creating, merging or duplicating an
     * inter-communicator.  We only need to pariticipate if we can use vMPI
     * to communicate with one or more of the processes in the remote group.
     */
    if (oldcomm == NULL)
    {
	MPID_Abort(NULL,
		   0,
		   "MPICH-G2 (internal error)",
		   "MPID_CommInit() - oldcomm = NULL");
    }
    
    if (newcomm == NULL)
    {
	MPID_Abort(NULL,
		   0,
		   "MPICH-G2 (internal error)",
		   "MPID_CommInit() - newcomm = NULL");
    }

    if (vlnp <= 0)
    {
	goto fn_exit;
    }
    
    if (oldcomm->comm_type == MPIR_INTRA && newcomm->comm_type == MPIR_INTER)
    {
	rc = vmpi_error_to_mpich_error(
	    mp_intercomm_create(
		oldcomm->vmpi_comm,
		0,
		VMPI_Internal_Comm,
		VMPI_GRank_to_VGRank[
		    newcomm->lrank_to_grank[newcomm->vlrank_to_lrank[0]]],
		0,
		newcomm->vmpi_comm));
    }
    else if (oldcomm->comm_type == MPIR_INTER
	     && newcomm->comm_type == MPIR_INTRA)
    {
	int				high;
	void *				intracomm;
	
	/*
	 * check if MPICH is creating a safe intra-communicator for performing
	 * internal collective operations
	 *
	 */
	if (oldcomm->comm_coll == newcomm)
	{
	    /*
	     * if MPICH is creating an intra-communicator of the local group,
	     * then we need to merge the communicator back together and then
	     * split it back apart.  If only we had the original, local
	     * communicator used when the inter-communicator was formed...
	     */
	    high = (oldcomm->local_group->lrank_to_grank[0] <
		    oldcomm->group->lrank_to_grank[0]) ? 0 : 1;
	    intracomm = (void *) globus_libc_malloc(mp_comm_get_size());
	}
	else
	{
	    /*
	     * ok, the user is actually performing a MPI_Intercomm_merge()
	     */
	    high = (oldcomm->local_rank == newcomm->local_rank) ? 0 : 1;
	    intracomm = newcomm->vmpi_comm;
	}
	
	rc = vmpi_error_to_mpich_error(
	    mp_intercomm_merge(oldcomm->vmpi_comm, high, intracomm));
	
    	if (oldcomm->comm_coll == newcomm)
	{
	    if (rc == MPI_SUCCESS)
	    {
		rc = vmpi_error_to_mpich_error(
		    mp_comm_split(intracomm,
				  high,
				  newcomm->local_rank,
				  newcomm->vmpi_comm));
		
		mp_comm_free(intracomm);
	    }
	    
	    globus_libc_free(intracomm);
	}
    }
    else if (oldcomm->comm_type == MPIR_INTER
	     && newcomm->comm_type == MPIR_INTER)
    {
	rc = vmpi_error_to_mpich_error(
	    mp_comm_dup(oldcomm->vmpi_comm, newcomm->vmpi_comm));
    }

    goto fn_exit;
    
  fn_abort:
    g_free(newcomm->vmpi_comm);
    g_free(newcomm->vlrank_to_lrank);
    g_free(newcomm->lrank_to_vlrank);
    g_free(newcomm->vgrank_to_vlrank);
    
    newcomm->vmpi_comm = NULL;
    newcomm->lrank_to_vlrank = NULL;
    newcomm->vlrank_to_lrank = NULL;
    newcomm->vgrank_to_vlrank = NULL;
#endif   /* VMPI */

#ifdef VMPI
  fn_exit:
#endif   /* VMPI */
    if ( rc == MPI_SUCCESS  &&  newcomm )
    {
        if ((rc = topology_initialization(newcomm)) == MPI_SUCCESS)
	{
	    int i;

	    for (newcomm->vmpi_only = GLOBUS_TRUE, i = 0;
		newcomm->vmpi_only && i < newcomm->np;
		    i ++)
			newcomm->vmpi_only = (get_proto(newcomm, i) == mpi);
	} /* endif */
    } /* endif */
    DEBUG_FN_EXIT(DEBUG_MODULE_COMM);
    return rc;
}

#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Comm_free
int MPID_Comm_free(
    struct MPIR_COMMUNICATOR *		comm)
{
    DEBUG_FN_ENTRY(DEBUG_MODULE_COMM);

#ifdef VMPI
    if (comm->vmpi_comm != NULL)
    {
	mp_comm_free(comm->vmpi_comm);
    }

    g_free(comm->vmpi_comm);
    g_free(comm->vlrank_to_lrank);
    g_free(comm->lrank_to_vlrank);
    g_free(comm->vgrank_to_vlrank);
    
    comm->vmpi_comm = NULL;
    comm->lrank_to_vlrank = NULL;
    comm->vlrank_to_lrank = NULL;
    comm->vgrank_to_vlrank = NULL;
#endif   /* VMPI */

  /* fn_exit: */
    topology_destruction(comm);
    DEBUG_FN_EXIT(DEBUG_MODULE_COMM);
    return MPI_SUCCESS;
}

/* START GRIDFTP */
int MPID_Attr_set(struct MPIR_COMMUNICATOR *comm, int keyval, void *attr_value)
{
    int rc;

    if (keyval == MPICHX_PARALLELSOCKETS_PARAMETERS)
    {
        rc = enable_gridftp(comm, attr_value); 
    }
    else
    {
        /* I don't care about any other attr */
        rc = MPI_SUCCESS; 
    } /* endif */

    return rc;

} /* end MPID_Attr_set() */
/* END GRIDFTP */
