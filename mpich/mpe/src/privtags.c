/*
   This file provides a simple way to manage tags inside a private 
   communicator.  It uses the attribute to determine if a new communicator
   is needed.

   Notes on the implementation

   The tagvalues to use are stored in a two element array.  The first element
   is the first free tag value.  The second is used to indicate whether
   an attribute is being updated or deleted.  If the second value is 1,
   is is being deleted.  If it is two, it is either being inserted or
   updated.  This is used to keep the "delete" routine from being called
   when we do an update.
 */

#include "mpeconf.h"
#include "mpi.h"
#include "mpe.h"
/* For malloc */
#include <stdlib.h>

static int MPE_Tag_keyval = MPI_KEYVAL_INVALID;

/*
   Private routine to delete internal storage when a communicator is freed.
 */
int MPE_DelTag( comm, keyval, attr_val, extra_state )
MPI_Comm comm;
int      keyval;
void     *attr_val, *extra_state;
{
/* The attribute value is malloc'ed on creation; this prevents a
   storage leak */
free( attr_val );

return MPI_SUCCESS;
}

/*@
  MPE_GetTags - Returns tags that can be used in communication with a 
  communicator

  Input Parameters:
+ comm_in - Input communicator
- ntags   - Number of tags

  Output Parameters:
+ comm_out - Output communicator.  May be 'comm_in'.
- first_tag - First tag available

  Returns:
  MPI_SUCCESS on success, MPI error class on failure.

  Notes:
  This routine returns the requested number of tags, with the tags being
  'first_tag', 'first_tag+1', ..., 'first_tag+ntags-1'.

  These tags are guarenteed to be unique within 'comm_out'.  

.seealso: MPE_ReturnTags
  
@*/
int MPE_GetTags( comm_in, ntags, comm_out, first_tag )
MPI_Comm comm_in, *comm_out;
int      ntags, *first_tag;
{
int mpe_errno = MPI_SUCCESS;
int *tagvalp, *maxval, flag;

if (MPE_Tag_keyval == MPI_KEYVAL_INVALID) {
    MPI_Keyval_create( MPI_NULL_COPY_FN, MPE_DelTag, 
		       &MPE_Tag_keyval, (void *)0 );
    }

if ((mpe_errno = MPI_Attr_get( comm_in, MPE_Tag_keyval, &tagvalp, &flag )))
    return mpe_errno;

if (!flag) {
    /* This communicator is not yet known to this system, so we
       dup it and setup the first value */
    MPI_Comm_dup( comm_in, comm_out );
    comm_in = *comm_out;
    MPI_Attr_get( MPI_COMM_WORLD, MPI_TAG_UB, &maxval, &flag );
    tagvalp = (int *)malloc( 2 * sizeof(int) );
    if (!tagvalp) return MPI_ERR_OTHER;
    *tagvalp   = *maxval;
    *first_tag = *tagvalp - ntags;
    *tagvalp   = *first_tag;
    MPI_Attr_put( comm_in, MPE_Tag_keyval, tagvalp );
    return MPI_SUCCESS;
    }
*comm_out = comm_in;

if (*tagvalp < ntags) {
    /* Error, out of tags.  Another solution would be to 
       do an MPI_Comm_dup. */
    return MPI_ERR_INTERN;
    }
*first_tag = *tagvalp - ntags;
*tagvalp   = *first_tag;

return MPI_SUCCESS;
}

/* 
   This is a simple implementation that will lose track of tags that are
   not returned in a stack fashion.
 */

/*@
  MPE_ReturnTags - Returns tags allocated with MPE_GetTags.

  Input Parameters:
+ comm - Communicator to return tags to
. first_tag - First of the tags to return
- ntags - Number of tags to return.

.seealso: MPE_GetTags
  
@*/
int MPE_ReturnTags( comm, first_tag, ntags )
MPI_Comm comm;
int      first_tag, ntags;
{
int *tagvalp, flag, mpe_errno;

if ((mpe_errno = MPI_Attr_get( comm, MPE_Tag_keyval, &tagvalp, &flag )))
    return mpe_errno;

if (!flag) {
    /* Error, attribute does not exist in this communicator */
    return MPI_ERR_OTHER;
    }
if (*tagvalp == first_tag) {
    *tagvalp = first_tag + ntags;
    }

return MPI_SUCCESS;
}

/*@
  MPE_TagsEnd - Returns the private keyval.  

  Notes:
  This routine is provided to aid in cleaning up all of the allocated 
  storage in and MPI program.  Normally, this routine does `not` need
  to be called.  If it is, it should be called immediately before 
  'MPI_Finalize'.
@*/
int MPE_TagsEnd()
{
    MPI_Keyval_free( &MPE_Tag_keyval );
    MPE_Tag_keyval = MPI_KEYVAL_INVALID;
    return 0;
}

