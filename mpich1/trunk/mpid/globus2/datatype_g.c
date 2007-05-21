#include "chconfig.h"
#include "globdev.h"

#if defined(VMPI)

static int mpich_type_to_vmpi_type(
    MPI_Datatype			datatype);

#if (DEBUG_ENABLED)
static void MPID_Type_validate(
    struct MPIR_DATATYPE *		dtype_ptr);
#else
#define MPID_Type_validate(D)
#endif

/*
 * MPID_Type_permanent_setup()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_permanent_setup
int MPID_Type_permanent_setup(
    MPI_Datatype			datatype)
{
    int					rc;
    struct MPIR_DATATYPE *		dtype_ptr;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("datatype=%d\n", datatype));

    rc = MPI_SUCCESS;
    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
    MPID_Type_validate(dtype_ptr);
    
    if (!dtype_ptr->permanent)
    {
        MPID_Abort(NULL,
                   0,
                   "MPICH-G2 (internal error)",
		   "MPID_Type_permanent_setup() - MPICH didn't mark "
		   "this as a permanent type!");
    }

    rc = vmpi_error_to_mpich_error(
	mp_type_permanent_setup(dtype_ptr->vmpi_type,
				mpich_type_to_vmpi_type(datatype)));

    if (rc == MPI_SUCCESS)
    {
	dtype_ptr->vmpi_cookie = MPID_DATATYPE_COOKIE;
    }
    
  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_permanent_setup() */


/*
 * MPID_Type_commit()
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_commit
int MPID_Type_commit(
    MPI_Datatype			datatype)
{
    int					rc;
    struct MPIR_DATATYPE *		dtype_ptr;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("datatype=%d\n", datatype));

    rc = MPI_SUCCESS;
    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
    MPID_Type_validate(dtype_ptr);
    MPID_Type_validate_vmpi(dtype_ptr);

    /*
     * Do not commit basic/permanent types; these should already have been
     * committed by MPID_Type_permanent_setup()
     */
    if (!dtype_ptr->permanent)
    {
	MPID_Type_validate_vmpi(dtype_ptr);
	rc = vmpi_error_to_mpich_error(
	    mp_type_commit(dtype_ptr->vmpi_type));
    }

  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_commit() */


/*
 * MPID_Type_free()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_free
int MPID_Type_free(
    MPI_Datatype			datatype)
{
    int					rc;
    struct MPIR_DATATYPE *		dtype_ptr;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("datatype=%d\n", datatype));

    rc = MPI_SUCCESS;
    dtype_ptr = MPIR_GET_DTYPE_PTR(datatype);
    MPID_Type_validate(dtype_ptr);
    MPID_Type_validate_vmpi(dtype_ptr);
    
    MPID_Type_validate_vmpi(dtype_ptr);
    if (dtype_ptr->permanent)
    {
	rc = vmpi_error_to_mpich_error(
	    mp_type_permanent_free(dtype_ptr->vmpi_type,
				   mpich_type_to_vmpi_type(datatype)));
    }
    else
    {
	rc = vmpi_error_to_mpich_error(
	    mp_type_free(dtype_ptr->vmpi_type));
    }

    dtype_ptr->vmpi_cookie = 0;
    
  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_free() */


/*
 * MPID_Type_contiguous()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_contiguous
int MPID_Type_contiguous(
    int					count,
    MPI_Datatype			oldtype,
    MPI_Datatype			newtype)
{
    int					rc;
    struct MPIR_DATATYPE *		oldtype_ptr;
    struct MPIR_DATATYPE *		newtype_ptr;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("newtype=%d\n", newtype));

    rc = MPI_SUCCESS;

    oldtype_ptr = MPIR_GET_DTYPE_PTR(oldtype);
    MPID_Type_validate(oldtype_ptr);
    MPID_Type_validate_vmpi(oldtype_ptr);
    newtype_ptr = MPIR_GET_DTYPE_PTR(newtype);
    MPID_Type_validate(newtype_ptr);
    
    rc = vmpi_error_to_mpich_error(
	mp_type_contiguous(count,
			   oldtype_ptr->vmpi_type,
			   newtype_ptr->vmpi_type));
			   
    if (rc == MPI_SUCCESS)
    {
	newtype_ptr->vmpi_cookie = MPID_DATATYPE_COOKIE;
    }
    
  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_contiguous() */


/*
 * MPID_Type_hindexed()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_hindexed
int MPID_Type_hindexed(
    int					count,
    int					blocklens[],
    MPI_Aint				indices[],
    MPI_Datatype			oldtype,
    MPI_Datatype			newtype)
{
    int					rc;
    struct MPIR_DATATYPE *		oldtype_ptr;
    struct MPIR_DATATYPE *		newtype_ptr;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("newtype=%d\n", newtype));

    rc = MPI_SUCCESS;

    oldtype_ptr = MPIR_GET_DTYPE_PTR(oldtype);
    MPID_Type_validate(oldtype_ptr);
    MPID_Type_validate_vmpi(oldtype_ptr);
    newtype_ptr = MPIR_GET_DTYPE_PTR(newtype);
    MPID_Type_validate(newtype_ptr);
    
    rc = vmpi_error_to_mpich_error(
	mp_type_hindexed(count,
			 blocklens,
			 indices,
			 oldtype_ptr->vmpi_type,
			 newtype_ptr->vmpi_type));
			   
    if (rc == MPI_SUCCESS)
    {
	newtype_ptr->vmpi_cookie = MPID_DATATYPE_COOKIE;
    }
    
  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_hindexed() */


/*
 * MPID_Type_hvector()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_hvector
int MPID_Type_hvector(
    int					count,
    int					blocklen,
    MPI_Aint				stride,
    MPI_Datatype			oldtype,
    MPI_Datatype			newtype)
{
    int					rc;
    struct MPIR_DATATYPE *		oldtype_ptr;
    struct MPIR_DATATYPE *		newtype_ptr;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("newtype=%d\n", newtype));

    rc = MPI_SUCCESS;

    oldtype_ptr = MPIR_GET_DTYPE_PTR(oldtype);
    MPID_Type_validate(oldtype_ptr);
    MPID_Type_validate_vmpi(oldtype_ptr);
    newtype_ptr = MPIR_GET_DTYPE_PTR(newtype);
    MPID_Type_validate(newtype_ptr);

    rc = vmpi_error_to_mpich_error(
	mp_type_hvector(count,
			blocklen,
			stride,
			oldtype_ptr->vmpi_type,
			newtype_ptr->vmpi_type));
			   
    if (rc == MPI_SUCCESS)
    {
	newtype_ptr->vmpi_cookie = MPID_DATATYPE_COOKIE;
    }
    
  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_hvector() */


/*
 * MPID_Type_struct()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME MPID_Type_struct
int MPID_Type_struct(
    int					count,
    int					blocklens[],
    MPI_Aint				indices[],
    MPI_Datatype			oldtypes[],
    MPI_Datatype			newtype)
{
    int					rc;
    int					i;
    struct MPIR_DATATYPE *		newtype_ptr;
    globus_byte_t *			old_vmpi_types;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("newtype=%d\n", newtype));

    rc = MPI_SUCCESS;

    old_vmpi_types = (globus_byte_t *)
	globus_libc_malloc(count * VENDOR_MPI_DATATYPE_SIZE);
    if (old_vmpi_types == NULL)
    {
	rc = MPI_ERR_EXHAUSTED;
	goto fn_exit;
    }

    newtype_ptr = MPIR_GET_DTYPE_PTR(newtype);
    MPID_Type_validate(newtype_ptr);

    for (i = 0; i < count; i++)
    {
	struct MPIR_DATATYPE *		dtype_ptr;
	
	dtype_ptr = MPIR_GET_DTYPE_PTR(oldtypes[i]);
	MPID_Type_validate(dtype_ptr);
	MPID_Type_validate_vmpi(dtype_ptr);

	memcpy(old_vmpi_types + i * VENDOR_MPI_DATATYPE_SIZE,
	       dtype_ptr->vmpi_type,
	       VENDOR_MPI_DATATYPE_SIZE);
    }

    rc = vmpi_error_to_mpich_error(
	mp_type_struct(count,
		       blocklens,
		       indices,
		       old_vmpi_types,
		       newtype_ptr->vmpi_type));

    globus_libc_free(old_vmpi_types);
    
    if (rc == MPI_SUCCESS)
    {
	newtype_ptr->vmpi_cookie = MPID_DATATYPE_COOKIE;
    }
    
  fn_exit:
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* MPID_Type_struct() */


#if (DEBUG_ENABLED)

void MPID_Type_validate_vmpi(
    struct MPIR_DATATYPE *		dtype_ptr)
{
    if (dtype_ptr->vmpi_cookie != MPID_DATATYPE_COOKIE)
    {
        MPID_Abort(NULL,
                   0,
                   "MPICH-G2 (internal error)",
		   "MPID_Type_validate_vmpi() - failed VMPI datatype "
		   "cookie verification!");
    }
}
/* MPID_Type_validate_vmpi */


static void MPID_Type_validate(
    struct MPIR_DATATYPE *		dtype_ptr)
{
    if (dtype_ptr == NULL)
    {
        MPID_Abort(NULL,
                   0,
                   "MPICH-G2 (internal error)",
		   "MPID_Type_validate() - datatype point is NULL!");
    }

    if (dtype_ptr->cookie != MPIR_DATATYPE_COOKIE)
    {
        MPID_Abort(NULL,
                   0,
                   "MPICH-G2 (internal error)",
		   "MPID_Type_validate() - failed datatype cookie "
		   "verification!");
    }
}
/* MPID_Type_validate */

#endif /* DEBUG_ENABLED */

/*
 * mpich_type_to_vmpi_type()
 *
 */
#undef DEBUG_FN_NAME
#define DEBUG_FN_NAME mpich_type_to_vmpi_type
static int mpich_type_to_vmpi_type(
    MPI_Datatype			datatype)
{
    int					rc;
    
    DEBUG_FN_ENTRY(DEBUG_MODULE_TYPES);
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_ARGS,
		 ("datatype=%d\n", datatype));

    /*
     g* Note: MPICH maps MPI_CHARACTER to MPI_CHAR and MPI_LONG_LONG to
     * MPI_LONG_LONG_INT so we have no way to distiguish the types from the
     * ones upon which they are mapped.
     */
    switch(datatype)
    {
      case MPI_CHAR:			rc = VMPI_CHAR;			break;
      case MPI_UNSIGNED_CHAR:		rc = VMPI_UNSIGNED_CHAR;	break;
      case MPI_BYTE:			rc = VMPI_BYTE;			break;
      case MPI_SHORT:			rc = VMPI_SHORT;		break;
      case MPI_UNSIGNED_SHORT:		rc = VMPI_UNSIGNED_SHORT;	break;
      case MPI_INT:			rc = VMPI_INT;			break;
      case MPI_UNSIGNED:		rc = VMPI_UNSIGNED;		break;
      case MPI_LONG:			rc = VMPI_LONG;			break;
      case MPI_UNSIGNED_LONG:		rc = VMPI_UNSIGNED_LONG;	break;
      case MPI_FLOAT:			rc = VMPI_FLOAT;		break;
      case MPI_DOUBLE:			rc = VMPI_DOUBLE;		break;
      case MPI_LONG_DOUBLE:		rc = VMPI_LONG_DOUBLE;		break;
      case MPI_LONG_LONG_INT:		rc = VMPI_LONG_LONG_INT;	break;
      case MPI_PACKED:			rc = VMPI_PACKED;		break;
      case MPI_LB:			rc = VMPI_LB;			break;
      case MPI_UB:			rc = VMPI_UB;			break;
      case MPI_FLOAT_INT:		rc = VMPI_FLOAT_INT;		break;
      case MPI_DOUBLE_INT:		rc = VMPI_DOUBLE_INT;		break;
      case MPI_LONG_INT:		rc = VMPI_LONG_INT;		break;
      case MPI_SHORT_INT:		rc = VMPI_SHORT_INT;		break;
      case MPI_2INT:			rc = VMPI_2INT;			break;
      case MPI_LONG_DOUBLE_INT:		rc = VMPI_LONG_DOUBLE_INT;	break;
      case MPI_COMPLEX:			rc = VMPI_COMPLEX;		break;
      case MPI_DOUBLE_COMPLEX:		rc = VMPI_DOUBLE_COMPLEX;	break;
      case MPI_LOGICAL:			rc = VMPI_LOGICAL;		break;
      case MPI_REAL:			rc = VMPI_REAL;			break;
      case MPI_DOUBLE_PRECISION:	rc = VMPI_DOUBLE_PRECISION;	break;
      case MPI_INTEGER:			rc = VMPI_INTEGER;		break;
      case MPI_2INTEGER:		rc = VMPI_2INTEGER;		break;
      case MPI_2COMPLEX:		rc = VMPI_2COMPLEX;		break;
      case MPI_2DOUBLE_COMPLEX:		rc = VMPI_2DOUBLE_COMPLEX;	break;
      case MPI_2REAL:			rc = VMPI_2REAL;		break;
      case MPI_2DOUBLE_PRECISION:	rc = VMPI_2DOUBLE_PRECISION;	break;

      default:
      {
	 char err[1024];
	 globus_libc_sprintf(
	    err, 
	    "mpich_type_to_vmpi_type() -  encountered unrecognizable type %d", 
	    datatype);
	  MPID_Abort(NULL,
                   0,
                   "MPICH-G2 (internal error)",
		   err);
       }
         break;
     } /* end switch */

  /* fn_exit: */
    DEBUG_PRINTF(DEBUG_MODULE_TYPES, DEBUG_INFO_RC,
		 ("rc=%d\n", rc));
    DEBUG_FN_EXIT(DEBUG_MODULE_TYPES);
    return rc;
}
/* mpich_type_to_vmpi_type() */


#endif /* VMPI */
