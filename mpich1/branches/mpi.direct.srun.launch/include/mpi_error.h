#ifndef MPIR__ERROR

/* 
 * Macros to simplify error handling
 * MPIR_ERROR - used primarily in 
 *      return MPIR_ERROR( comm, err_code, routine )
 * MPIR_RETURN(comm,err_code,routine) 
 *  use instead of return mpi_errno (calls
 *  MPIR_ERROR if mpi_errno != MPI_SUCCESS).
 * MPIR_CALL(fcn,comm,msg) calls an MPI routine and returns if error (returning
 * the functions's return value)
 * 
 * The following are to allow MPI routines to call other MPI routines and
 * get the "correct" error behavior (i.e., return up to the outermost caller).
 * MPIR_ERROR_DECL - declaration (holds previous state)
 * MPIR_ERROR_PUSH(comm) - Change error handler on comm
 * MPIR_ERROR_POP(comm) - Change error handler on comm
 * MPIR_CALL_POP(fcn,comm,msg) - like MPIR_CALL, but also does MPIR_ERROR_POP.
 */

/* Generic error handling code.  This handles inserting the file and line
   number (in MPI) where the error occured.  In addition, it
   checks the error handler and calls the appropriate one.  Finally, 
   it returns the errorcode as its value.
 */
int MPIR_Error( struct MPIR_COMMUNICATOR *, int, char *, char *, int );

#define MPIR_ERROR(comm,code,string) \
    MPIR_Error( comm, code, string, __FILE__, __LINE__ )
#define MPIR_RETURN(comm,code,string) \
    return (code) ? MPIR_ERROR(comm,code,string) : code
#define MPIR_ERROR_DECL int mpi_comm_err_ret
#define MPIR_ERROR_PUSH(comm) {mpi_comm_err_ret = (comm)->use_return_handler;\
        (comm)->use_return_handler = 1;}
#define MPIR_ERROR_POP(comm) (comm)->use_return_handler = mpi_comm_err_ret
#define MPIR_RETURN_POP(comm,code,string) \
    {MPIR_ERROR_POP(comm);MPIR_RETURN(comm,code,string);}

/* 
 * This routine can be called to call an MPI function and call the
 * appropriate error handler.
 */
#define MPIR_CALL(fcn,comm,msg) {if ((mpi_errno = fcn) != 0) \
				 return MPIR_ERROR(comm,mpi_errno,msg);}
#define MPIR_CALL_POP(fcn,comm,msg) {if ((mpi_errno = fcn) != 0) {\
	MPIR_ERROR_POP(comm); return MPIR_ERROR(comm,mpi_errno,msg);}}

/*
 * These can be called to handle allocating storage that might fail, returning
 * a NULL value in that case.  Note that "internal" routines like trmalloc
 * and MPID_SBalloc should just return a null, so that the appropriate 
 * error handler can be invoked
 */
#define MPIR_ALLOC(ptr,fcn,comm,code,msg) \
   {if (!((ptr) = fcn)) {return MPIR_ERROR(comm,code,msg);}}
/* This is a version for macros that take the pointer as arg */
#define MPIR_ALLOCFN(ptr,fcn,comm,code,msg) \
   {fcn(ptr);if (!(ptr)) {return MPIR_ERROR(comm,code,msg);}}
/* MPIR_FALLOC is for Fortran */
#define MPIR_FALLOC(ptr,fcn,comm,code,msg) \
   {if (!((ptr) = fcn)) {*__ierr = MPIR_ERROR(comm,code,msg);return;}}
#define MPIR_ALLOC_POP(ptr,fcn,comm,code,msg) \
   {if (!((ptr) = fcn)) {MPIR_RETURN_POP(comm,code,msg);}}

#ifdef OLD_ERRMSGS
#define MPIR_MAX_ARGS 10
extern void *(MPIR_errargs[MPIR_MAX_ARGS]);
extern int    MPIR_errargcnt;

#define MPIR_ERROR_PUSH_ARG(ptr) MPIR_errargs[MPIR_errargcnt++] = (void*)(ptr)

#else
#define MPIR_ERROR_PUSH_ARG(ptr) ---- Error ----
#endif

/* Here is the new format:

   fields:   0 + <user?> + <ringid> + <kind> + <class>
   bits:     1     1         17          7       6

   This is for a 32 bit int; for a 64 bit int, zero extend.  <user?> is 
   0 for system message and 1 for user-defined error classes and codes.
   The <ringid> is a value used to extract a text message.
*/
/* Here we define some additional error information values.  These need to be
   or'ed into the appropriate MPI error class (from mpi_errno.h) 
 */
#define MPIR_ERR_CLASS_BITS 6
#define MPIR_ERR_CLASS_MASK 0x3f

/* 
   The various error codes are in the second 8 bits.  We reserve the 
   remaining 18 bits to indicate special error handling, for example,
   to indicate that runtime data for the message is available
 */
#define MPIR_ERR_CODE_BITS 13
#define MPIR_ERR_CODE_MASK 0x1fc0

/* To form a code from a class and kind, ALWAYS use the following */

#define MPIR_ERRCLASS_TO_CODE(class,kind) \
        ((class) | ((kind)<<MPIR_ERR_CLASS_BITS))

/* 
   These are all error CODES mapped onto some of the error CLASSES.

   These need to be reorganized.

   These are numbered from 1, not zero, simply to make matching against
   the message catalogs easier.

   We should reorder these for consistency
   1 Default message
   3 Null object
   5 Corrupt object
   7 Value out of range
   9 (more out of range choices)
 */

/* This should replace "0" in places where the default value is used. */
#define MPIR_ERR_DEFAULT 1

/* MPI_ERR_BUFFER */
#define MPIR_ERR_BUFFER_EXISTS 3
#define MPIR_ERR_USER_BUFFER_EXHAUSTED 5
                                    /* BSend with insufficent buffer space */
#define MPIR_ERR_BUFFER_ALIAS 7
                                    /* User has aliased an argument */
#define MPIR_ERR_BUFFER_SIZE 9

/* MPI_ERR_COUNT */
#define MPIR_ERR_COUNT_ARRAY_NEG 3

/* MPI_ERR_TYPE */
#define MPIR_ERR_UNCOMMITTED  3
                                    /* Uncommitted datatype */  
#define MPIR_ERR_TYPE_NULL    5
#define MPIR_ERR_TYPE_CORRUPT 7
#define MPIR_ERR_PERM_TYPE    9
                                    /* Can't free a perm type */
#define MPIR_ERR_BASIC_TYPE   11
                                    /* Can't get contents of a perm type */
#define MPIR_ERR_TYPE_ARRAY_NULL 13

/* MPI_ERR_TAG */

/* MPI_ERR_COMM */
#define MPIR_ERR_COMM_NULL    3
                                    /* NULL communicator argument 
				       passed to function */
#define MPIR_ERR_COMM_INTER   5
			            /* Intercommunicator is not allowed 
				       in function */
#define MPIR_ERR_COMM_INTRA   7
                                    /* Intracommunicator is not allowed 
				       in function */
#define MPIR_ERR_COMM_CORRUPT 9
#define MPIR_ERR_COMM_NAME    11
#define MPIR_ERR_PEER_COMM    13
#define MPIR_ERR_LOCAL_COMM   15

/* MPI_ERR_RANK */
#define MPIR_ERR_DUP_RANK     3
#define MPIR_ERR_RANK_ARRAY   5
#define MPIR_ERR_LOCAL_RANK   7
#define MPIR_ERR_REMOTE_RANK  9

/* MPI_ERR_ROOT */
#define MPIR_ERR_ROOT_TOOBIG 3

/* MPI_ERR_GROUP */
#define MPIR_ERR_GROUP_NULL   3
#define MPIR_ERR_GROUP_CORRUPT 5

/* MPI_ERR_OP */
#define MPIR_ERR_OP_NULL      3
#define MPIR_ERR_NOT_DEFINED  5
                                    /* Operation not defined for this 
				      datatype */

/* MPI_ERR_TOPOLOGY */
#define MPIR_ERR_TOPO_TOO_LARGE 3
#define MPIR_ERR_GRAPH_EDGE_ARRAY 5

/* MPI_ERR_DIMS */
/* MUST CHECK THESE IN USE */
#define MPIR_ERR_DIMS_SIZE 5
#define MPIR_ERR_DIMS_ARRAY 3
#define MPIR_ERR_DIMS_TOOLARGE 9
#define MPIR_ERR_DIMS_PARTITION 7

/* MPI_ERR_ARG */
#define MPIR_ERR_ERRORCODE    3
			            /* Invalid error code */
#define MPIR_ERR_NULL         5
                                    /* Null parameter */
#define MPIR_ERR_PERM_KEY     9
                                    /* Can't free a perm key */
#define MPIR_ERR_PERM_OP      13
                                    /* Can't free a permanent operator */
#define MPIR_ERR_FORTRAN_ADDRESS_RANGE 15
           /* Address of location given to MPI_ADDRESS does not fit in 
	      Fortran int */
#define MPIR_ERR_PERM_GROUP      17
				 /* Can't free a permanent group */   
#define MPIR_ERR_KEYVAL          19
#define MPIR_ERR_ERRHANDLER_NULL 21
#define MPIR_ERR_ERRHANDLER_CORRUPT  23
#define MPIR_ERR_STATUS_IGNORE      25
#define MPIR_ERR_ARG_STRIDE      27
#define MPIR_ERR_ARG_ZERO_STRIDE 29
#define MPIR_ERR_ARG_ARRAY_VAL   31
#define MPIR_ERR_ARG_NAMED       33
#define MPIR_ERR_NOKEY           35
#define MPIR_ERR_DARRAY_DIST_NONE 37
#define MPIR_ERR_DARRAY_DIST_UNKNOWN 39
#define MPIR_ERR_ARG_POSITION_NEG 41
#define MPIR_ERR_KEYVAL_NULL 43
#define MPIR_ERR_DARRAY_ARRAY_DIST_UNKNOWN 45
#define MPIR_ERR_ORDER 47
#define MPIR_ERR_DARRAY_INVALID_BLOCK 49
#define MPIR_ERR_DARRAY_INVALID_BLOCK2 51
#define MPIR_ERR_DARRAY_INVALID_BLOCK3 53
#define MPIR_ERR_INFO_VALLEN 55
#define MPIR_ERR_INFO_VALSIZE 57
#define MPIR_ERR_INFO_NKEY 59
#define MPIR_ERR_INFO_VAL_INVALID 61

/* MPI_ERR_OTHER */
#define MPIR_ERR_LIMIT        3
                                    /* limit reached */
#define MPIR_ERR_NOMATCH      5
                                    /* no recv posted for ready send */
#define MPIR_ERR_INIT         7
                                    /* MPI_INIT already called */
#define MPIR_ERR_PRE_INIT     9
                                    /* MPI_INIT has not been called */

#define MPIR_ERR_MPIRUN       11
#define MPIR_ERR_BAD_INDEX    13
#define MPIR_ERR_INDEX_EXHAUSTED 15
#define MPIR_ERR_INDEX_FREED  17
#define MPIR_ERR_BUFFER_TOO_SMALL 19
#define MPIR_ERR_MPIRUN_MACHINE    21
#define MPIR_ERR_ATTR_COPY    23
                                    /* User Copy routine returned error */
/* MPI_ERR_INTERN */
#define MPIR_ERR_EXHAUSTED    3
#define MPI_ERR_EXHAUSTED    MPIR_ERRCLASS_TO_CODE(MPI_ERR_INTERN,MPIR_ERR_EXHAUSTED)
                                    /* Memory exhausted */
#define MPIR_ERR_ONE_CHAR     5
#define MPIR_ERR_MSGREP_SENDER 7
#define MPIR_ERR_MSGREP_UNKNOWN 9
#define MPIR_ERR_ATTR_CORRUPT   11
#define MPIR_ERR_TOO_MANY_CONTEXTS 13
#define MPIR_ERR_BSEND_CORRUPT 15
#define MPIR_ERR_BSEND_DATA 17
#define MPIR_ERR_BSEND_PREPARE 19
#define MPIR_ERR_BSEND_PREPAREDATA 21
#define MPIR_ERR_FACTOR 23

/* MPI_ERR_REQUEST */
#define MPIR_ERR_REQUEST_NULL 3

/* MPI_ERR_ACCESS */
/* MPI_ERR_AMODE */
/* MPI_ERR_BAD_FILE */
/* MPI_ERR_CONVERSION */
/* MPI_ERR_DUP_DATAREP */
/* MPI_ERR_FILE_EXISTS */
/* MPI_ERR_FILE_IN_USE */
/* MPI_ERR_FILE        */
/* MPI_ERR_INFO        */

/* MPI_ERR_INFO_KEY    */
#define MPIR_ERR_KEY_TOOLONG 3
#define MPIR_ERR_KEY_EMPTY 5

/* MPI_ERR_INFO_VALUE  */
#define MPIR_ERR_INFO_VALUE_NULL 3
#define MPIR_ERR_INFO_VALUE_TOOLONG 5
/* MPI_ERR_INFO_NOKEY  */
/* MPI_ERR_IO          */
/* MPI_ERR_NAME        */
/* MPI_ERR_NOMEM       */
/* MPI_ERR_NOT_SAME    */
/* MPI_ERR_NO_SPACE    */
/* MPI_ERR_NO_SUCH_FILE */
/* MPI_ERR_PORT        */
/* MPI_ERR_QUOTA       */
/* MPI_ERR_READ_ONLY   */
/* MPI_ERR_SERVICE     */
/* MPI_ERR_SPAWN       */
/* MPI_ERR_UNSUPPORTED_DATAREP   */
/* MPI_ERR_UNSUPPORTED_OPERATION */
/* MPI_ERR_WIN         */

/* MPI_ERR_THATS_ALL */
/* The above is used to terminate the search for error kinds in the MakeMsgCat
   script */
/* 
   Standardized argument testing

   Many of the MPI routines take arguments of the same type.  These
   macros provide tests for these objects.

   It is intended that the tests for a valid opaque object such as 
   a communicator can check to insure that the object is both a communicator
   and that it is valid (hasn't been freed).  They can also test for
   null pointers.

   These are not used yet; we are still looking for the best ways to 
   define them.

   The intent is to use them in this manner:

   if (MPIR_TEST_...() || MPIR_TEST_... || ... ) 
        return MPIR_ERROR( comm, mpi_errno, "MPI_routine" );

   The hope is, that in the NO_ERROR_CHECKING case, the optimizer will
   be smart enough to remove the code.
 */
#ifdef MPIR_NO_ERROR_CHECKING
#define MPIR_TEST_SEND_TAG(comm,tag)      0
#define MPIR_TEST_RECV_TAG(comm,tag)      0
#define MPIR_TEST_SEND_RANK(comm,rank)    0
#define MPIR_TEST_RECV_RANK(comm,rank)    0
#define MPIR_TEST_COUNT(comm,count)       0
#define MPIR_TEST_OP(comm,op)             0
#define MPIR_TEST_GROUP(comm,group)       0
#define MPIR_TEST_COMM(comm,comm1)        0
#define MPIR_TEST_REQUEST(comm,request)   0
#define MPIR_TEST_IS_DATATYPE(comm,datatype) 0
#define MPIR_TEST_DATATYPE(comm,datatype) 0
#define MPIR_TEST_ERRHANDLER(comm,errhandler) 0
#define MPIR_TEST_ALIAS(b1,b2)            0
#define MPIR_TEST_ARG(arg)                0
#define MPIR_TEST_OUTSIZE(comm,count)    0
#define MPIR_TEST_OUT_LT_IN(comm,outcount,incount) 0
#define MPIR_TEST_OUTCOUNT(comm,outcount) 0

#else
#ifdef MPIR_HAS_COOKIES
#define MPIR_TEST_COOKIE(val,value) || ( ((val)->cookie != (value)) )
#define MPIR_CHECK_COOKIE(val,value) ( ((val)->cookie != (value)) )
#define MPIR_COOKIE_VAL(val) ((val)->cookie)
#else 
#define MPIR_TEST_COOKIE(val,value) 
#define MPIR_CHECK_COOKIE(val,value) 0
#define MPIR_COOKIE_VAL(val) 0
#endif

/*
 * Some compilers may complain about (a=b) tests.  They should be upgraded
 * to do what the GNU gcc compiler does: complain about a=b but not about
 * (a=b).  If you ABSOLUTELY must shut up a hostile compiler, change
 * (a=b) to ((a=b),1). 
 */

/*
 * Tag tests.  In order to detect "tag too large", we need to check the
 * tag value against the maximum tag value.  This is MPID_TAG_UB from
 * the device (which should normally be a compile time constant, but
 * could be a global variable if the device wants that option)
 */

#ifdef OLD_ERRMSGS
#define MPIR_TEST_SEND_TAG(comm,tag) \
    ((((tag) < 0 ) && (MPIR_ERROR_PUSH_ARG(&tag),mpi_errno = MPI_ERR_TAG )) ||\
     (((tag) > MPID_TAG_UB) && (MPIR_ERROR_PUSH_ARG(&tag),mpi_errno = MPI_ERR_TAG)))
    /* This requires MPI_ANY_TAG == -1 */
#define MPIR_TEST_RECV_TAG(comm,tag) \
    (( ((tag) < MPI_ANY_TAG) && \
         (MPIR_ERROR_PUSH_ARG(&tag),mpi_errno = MPI_ERR_TAG )) || \
    (((tag) > MPID_TAG_UB) && (MPIR_ERROR_PUSH_ARG(&tag),mpi_errno = MPI_ERR_TAG)))
    /* This exploits MPI_ANY_SOURCE==-2, MPI_PROC_NULL==-1 */
#define MPIR_TEST_SEND_RANK(comm,rank) \
    ( ((rank) < MPI_PROC_NULL || (rank) >= (comm)->np)\
           && (MPIR_ERROR_PUSH_ARG(&rank),mpi_errno = MPI_ERR_RANK))
    /* This requires min(MPI_PROC_NULL,MPI_ANY_SOURCE)=-2 */
#define MPIR_TEST_RECV_RANK(comm,rank) \
    (((rank) < -2 || (rank) >= (comm)->np) && \
     (MPIR_ERROR_PUSH_ARG(&rank),mpi_errno = MPI_ERR_RANK))
#define MPIR_TEST_COUNT(comm,count) ( ((count) < 0) && \
				     (mpi_errno = MPI_ERR_COUNT)) 
#else
#define MPIR_TEST_SEND_TAG(tag) \
if ((tag) < 0 || (tag) > MPID_TAG_UB) {\
  mpi_errno = MPIR_Err_setmsg( MPI_ERR_TAG, MPIR_ERR_DEFAULT, myname, (char*)0,(char *)0, tag );}

    /* This requires MPI_ANY_TAG == -1 */
#define MPIR_TEST_RECV_TAG(tag) \
if ((tag) < MPI_ANY_TAG || (tag)>MPID_TAG_UB) {\
   mpi_errno = MPIR_Err_setmsg( MPI_ERR_TAG, MPIR_ERR_DEFAULT, myname, (char*)0,(char *)0, tag );}
    /* This exploits MPI_ANY_SOURCE==-2, MPI_PROC_NULL==-1 */
#define MPIR_TEST_SEND_RANK(comm_ptr,rank) \
if ((rank) < MPI_PROC_NULL || (rank) >= (comm_ptr)->np) {\
    mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK,MPIR_ERR_DEFAULT,myname,(char *)0,(char *)0, rank );}
    /* This requires min(MPI_PROC_NULL,MPI_ANY_SOURCE)=-2 */
#define MPIR_TEST_RECV_RANK(comm_ptr,rank) \
if ((rank) < -2 || (rank) >= (comm_ptr)->np) {\
  mpi_errno = MPIR_Err_setmsg( MPI_ERR_RANK,MPIR_ERR_DEFAULT,myname,(char*)0,(char *)0, rank );}
#define MPIR_TEST_COUNT(count) \
if ((count)<0) { \
    mpi_errno = MPIR_Err_setmsg( MPI_ERR_COUNT,MPIR_ERR_DEFAULT,myname,(char *)0,(char *)0, count );}

#endif

/* New generic tests are needed for the following:

   The following take the name of the variable as one of the arguments.
   E.g., TEST_NULL(status,"status")
   TEST_NULL (for null pointer)
   TEST_NONNEGATIVE (non negative value)
   TEST_POSITIVE (positive value)
   TEST_<OBJ>_VALID, e.g., TEST_COMM_VALID (for valid cookie)
   
   For communication,
   TEST_COUNT
   TEST_SEND_TAG
   TEST_RECV_TAG
   TEST_RANK
   TEST_ROOT

   Also need topology, dimensions, array versions of many of these; e.g., 
   TEST_ARRAY_COUNT


 */

/*********************************************************************
 *** Debbie Swider put these in on 11/17/97 - for pack.c & unpack.c **/

/* These need to switch to the MPIR_Err_setmsg form */
#define MPIR_TEST_OUTSIZE(comm,count) ( ((count) < 0) && \
                                       (mpi_errno = MPI_ERR_ARG))
#define MPIR_TEST_OUT_LT_IN(comm,outcount,incount) ( (outcount < incount) \
                                       && (mpi_errno = MPI_ERR_COUNT) )
#define MPIR_TEST_OUTCOUNT(comm,outcount) ( ((outcount) < 0) && \
                                           (mpi_errno = MPI_ERR_COUNT))

/**********************************************************************
 **********************************************************************/
#ifdef NEW_POINTERS
#define MPIR_TEST_OP(comm,op) 'fixme'
#else
#define MPIR_TEST_OP(comm,op)       \
    ( (!(op) MPIR_TEST_COOKIE(op,MPIR_OP_COOKIE)) && (mpi_errno = MPI_ERR_OP ))
#endif
#ifdef NEW_POINTERS
#ifdef OLD_ERRMSGS
#define MPIR_TEST_GROUP(comm,group) 'fixme'
#else
#define MPIR_TEST_GROUP(group_ptr) \
if (!(group_ptr)) { mpi_errno = MPIR_ERRCLASS_TO_CODE(MPI_ERR_GROUP,MPIR_ERR_GROUP_NULL); } \
else if ((group_ptr)->cookie != MPIR_GROUP_COOKIE) {\
mpi_errno = MPIR_Err_setmsg( MPI_ERR_GROUP, MPIR_ERR_GROUP_CORRUPT, myname, (char *)0, (char*)0,(group_ptr)->cookie );}
#endif
#else
#define MPIR_TEST_GROUP(comm,group) \
    ( (!(group) MPIR_TEST_COOKIE(group,MPIR_GROUP_COOKIE)) && \
       (mpi_errno = MPI_ERR_GROUP ))
#endif
#ifdef NEW_POINTERS
#define MPIR_TEST_COMM(comm,comm1)  'fixme'
#else
#define MPIR_TEST_COMM(comm,comm1)  \
    ( (!(comm1) MPIR_TEST_COOKIE(comm1,MPIR_COMM_COOKIE)) \
     && (mpi_errno = MPI_ERR_COMM ))
#endif
#define MPIR_TEST_REQUEST(comm,request) \
 ( (!(request) MPIR_TEST_COOKIE(&((request)->chandle),MPIR_REQUEST_COOKIE)) \
     && (mpi_errno = MPI_ERR_REQUEST))

#ifdef MPIR_HAS_COOKIES
#define MPIR_TEST_IS_DATATYPE(comm,datatype) \
    ( (!(datatype) || \
       (!MPIR_TEST_PREDEF_DATATYPE(datatype) && \
	((datatype)->cookie!=MPIR_DATATYPE_COOKIE))) \
     && (mpi_errno = MPI_ERR_TYPE ))
#else
#define MPIR_TEST_IS_DATATYPE(comm,datatype) \
    ( (!(datatype) ) && (mpi_errno = MPI_ERR_TYPE ))
#endif
#define MPIR_TEST_DATATYPE(comm,datatype) 'fixme'
/*     (!(datatype) && (mpi_errno = MPI_ERR_TYPE)) */
#ifdef FOO
#define MPIR_TEST_DATATYPE(comm,datatype) \
    (MPIR_TEST_IS_DATATYPE(comm,datatype) || \
  (!MPIR_TEST_PREDEF_DATATYPE(datatype) && !(datatype)->committed && \
   (mpi_errno = (MPI_ERR_TYPE | MPIR_ERR_UNCOMMITTED))))
#endif

#ifdef NEW_POINTERS
#ifdef OLD_ERRMSGS
#define MPIR_TEST_ERRHANDLER(comm,errhandler) 'fixme'
#else
#define MPIR_TEST_ERRHANDLER(errhandler) \
  if (!(errhandler)) {\
 mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ERRHANDLER_NULL, myname, (char *)0,(char*)0 );}else{\
  if (MPIR_CHECK_COOKIE(errhandler,MPIR_ERRHANDLER_COOKIE)) {\
      mpi_errno = MPIR_Err_setmsg( MPI_ERR_ARG, MPIR_ERR_ERRHANDLER_CORRUPT, myname, (char*)0,(char *)0, MPIR_COOKIE_VAL(errhandler));}}

#endif
#else
#define MPIR_TEST_ERRHANDLER(comm,errhandler) \
    ( ( (!(errhandler) MPIR_TEST_COOKIE(errhandler,MPIR_ERRHANDLER_COOKIE)) \
       && (mpi_errno = MPI_ERR_ARG )))
#endif

#ifdef OLD_ERRMSGS
#define MPIR_TEST_HBT_NODE(comm,node) \
    ( ( !(node) MPIR_TEST_COOKIE(node,MPIR_HBT_NODE_COOKIE)) \
      && (mpi_errno = MPI_ERR_INTERN))
#define MPIR_TEST_HBT(comm,hbt) \
    ( ( !(hbt) MPIR_TEST_COOKIE(hbt,MPIR_HBT_COOKIE)) \
      && (mpi_errno = MPI_ERR_INTERN))

#define MPIR_TEST_ALIAS(b1,b2)      \
    ( ((b1)==(b2)) && (mpi_errno = (MPI_ERR_BUFFER | MPIR_ERR_BUFFER_ALIAS) ))
#define MPIR_TEST_ARG(arg)  (!(arg) && (mpi_errno = MPI_ERR_ARG) )
#else
/* Allow datatypes to be relative to MPI_BOTTOM */
#define MPIR_TEST_ALIAS(b1,b2) \
if ((b1) == (b2) && (b1) != MPI_BOTTOM) {\
mpi_errno = MPIR_Err_setmsg( MPI_ERR_BUFFER, MPIR_ERR_BUFFER_ALIAS, myname, (char*)0,(char*)0);}
#define MPIR_TEST_ARG(arg) \
if (!(arg)) {mpi_errno = MPI_ERR_ARG;}
#endif

#endif 

/* 
   Here are the definitions of the actual error messages; this is also needed
   by end-users (MPI error names are visible to all)
 */
#include "mpi_errno.h"

#endif


