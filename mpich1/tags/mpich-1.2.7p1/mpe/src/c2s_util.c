/******************** c2s_util.c ****************************/
/*
  This program converts a clog file generated using MPE Logging calls into an 
  slog file.
*/

/*
  a clog file format:
  divided into chunks of 1024 bytes containing a CLOG block.
  the block contains several records of different types.
  a record consists of a header which contains the timestamp, record type and
  process id.
  the headers are the same for all record types but the records themselves are
  very much different. this converter only pays attention to the following 
  record types: 
  CLOG_STATEDEF,
  CLOG_RAWEVENT,
  CLOG_COMMEVENT.
  the rest are ignored. for more information on other record types, please look
  at clog.h in the MPE source.
*/


#include "mpeconf.h"
#include <stdio.h>
#include <fcntl.h> 
#include <string.h>
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
#if defined( HAVE_UNISTD_H )
#include <unistd.h> 
#endif
#include "clog2slog.h"
#include "slog.h"
#include "mpi.h"

#if defined( C2S_BYTESWAP )
#undef C2S_BYTESWAP
#endif

/*
   the preprocessor variable STANDALONE should be defined when
   the file is compiled for standalone clog2slog converter.
   Because byteswapping is needed to be done only in the converter.
           C2S_BYTESWAP = ! WORDS_BIGENDIAN
*/

#if defined( STANDALONE )
#    if ! defined( WORDS_BIGENDIAN )
#        define C2S_BYTESWAP
#    endif
#endif

static int proc_num    = 0;                
static long num_events  = 0;
static int state_id = 1;                /* state id for clog2slog independent of
					   the clog state id's.*/

static struct state_info *first,        /* pointers to the beginning and end */
                         *last;         /* of the list of state defs.*/

static struct list_elemnt *list_first,  /* pointers to the beginning and end */
                          *list_last;   /* of the list of start events.      */

static struct list_elemnt 
                       *msg_list_first, /* pointers to the beginning and end */
                       *msg_list_last;  /* of the list of start events.      */

static long list_messages = 0;
static long events = 0;

static SLOG slog;                       /* a handle to the slog format.      */

/* Forward refs */
static int init_SLOG_TTAB (void);
static int init_SLOG_PROF_RECDEF(void);

static int logEvent( CLOG_HEADER *, CLOG_RAW * );
static int handle_extra_state_defs(CLOG_STATE *);
static int writeSLOGInterval(CLOG_HEADER *, CLOG_RAW *, struct list_elemnt);
static int handleStartEvent(int, CLOG_HEADER *, CLOG_RAW *);
static int addState(int, int, int, CLOG_CNAME, CLOG_DESC);
static int replace_state_in_list(int, int, CLOG_CNAME, CLOG_DESC);
static int findState_strtEvnt(int);
static int findState_endEvnt(int);
static int addToList(int, int, int, double);
static int addToMsgList(int, int, int, int, double);
static int find_elemnt(int, int, int, struct list_elemnt *);
static int find_msg_elemnt(int, int, int, int, struct list_elemnt *);
static void freeList(void);
static void freeMsgList(void);
#ifdef DEBUG_PRINT
static void printEventList(void);
static void printMsgEventList(void);
static void printStateInfo(void);
#endif
static int get_new_state_id(void);



/****
     initialize clog2slog data structures.
****/
int C2S1_init_clog2slog(char *clog_file, char **slog_file) {

  first                = NULL;
  last                 = NULL;
  list_first           = NULL;
  list_last            = NULL;
  msg_list_first       = NULL;
  msg_list_last        = NULL;

  /*
    slog_file has the same name as the clog file - the .clog extension is
    changed to .slog extension. the file is created in the same directory as 
    clog file.
  */

  *slog_file = (char*) MALLOC (strlen(clog_file)+1);
  if(*slog_file == NULL) {
    fprintf(stderr,__FILE__":%d: Not enough memory to write"
	    " the slog file name.\n", __LINE__);
    return C2S_ERROR;
  }
  strcpy(*slog_file, clog_file);
  *(strrchr(*slog_file,'c')) = 's'; 
  return C2S_SUCCESS;
}  


/****
     returns memory resources used by clog2slog
****/
void C2S1_free_resources() {
  C2S1_free_state_info();
  freeList();
  freeMsgList();
  SLOG_CloseOutputStream( slog );
}
 
  
/****
     the memory buffer read in from the clog file is passed to this function
     the state definitions list initialized. 
     the return value of this function represents the end of a clog block
     or the end of the log itself.
     this function is only interested in CLOG_STATEDEF and CLOG_COMMEVENT.
     the others are ignored.
     CLOG_COMMEVENT helps to initialize the proc_num global variable which is
     used in the thread initialization.
     WARNING: to be used when a first pass is made for initializing
     state definitions.
****/
int C2S1_init_state_defs(double *membuff) {

  int rec_type;
  CLOG_HEADER* headr;         /* pointer to a clog header.          */
  CLOG_STATE* state;          /* pointer to a clog state definition.*/ 
  double *data_ptr = membuff; /* pointer to the memory buffer.      */

  rec_type = CLOG_UNDEF;      

  while((rec_type != CLOG_ENDBLOCK) && (rec_type != CLOG_ENDLOG)) {
    headr    = (CLOG_HEADER *)data_ptr;
#if defined( C2S_BYTESWAP )
    adjust_CLOG_HEADER(headr);
#endif
    rec_type = headr->rectype;
    data_ptr = headr->rest;
    switch(rec_type) {
      
    case CLOG_MSGEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_MSG((CLOG_MSG *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_MSG *) data_ptr)->end);
      break;
      
    case CLOG_COLLEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_COLL ((CLOG_COLL *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_COLL *) data_ptr)->end);
      break;
      
    case CLOG_RAWEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_RAW ((CLOG_RAW *)data_ptr);
#endif
      num_events++;
      data_ptr = (double *) (((CLOG_RAW *) data_ptr)->end);
      break;
      
    case CLOG_SRCLOC:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_SRC ((CLOG_SRC *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_SRC *) data_ptr)->end);
      break;

    case CLOG_COMMEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_COMM ((CLOG_COMM *)data_ptr);
#endif
      if(((CLOG_HEADER *)headr)->procid > proc_num)
	proc_num = ((CLOG_HEADER *)headr)->procid;
      
      data_ptr = (double *) (((CLOG_COMM *) data_ptr)->end);
      break;
      
    case CLOG_STATEDEF:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_STATE ((CLOG_STATE *)data_ptr);
#endif
      state = (CLOG_STATE *)data_ptr;
      if( (findState_strtEvnt(state->endetype) == C2S_ERROR) ||
	  (findState_endEvnt(state->startetype) == C2S_ERROR) ) {
	if(addState(state->stateid, state->startetype, state->endetype,
		    state->color, state->description) == C2S_ERROR)
	  return C2S_ERROR;
      }
      else {
	fprintf(stderr,__FILE__":%d: event ids defined for state %s already "
		"exist. Use MPE_Log_get_event_number() to define new event ids.\n",
		__LINE__,state->description);
	return C2S_ERROR;
      }
      data_ptr = (double *) (((CLOG_STATE *) data_ptr)->end);
      break;
      
    case CLOG_EVENTDEF:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_EVENT ((CLOG_EVENT *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_EVENT *) data_ptr)->end);
      break;

    case CLOG_ENDBLOCK:
      break;

    case CLOG_ENDLOG:
      if(addState(MSG_STATE,LOG_MESG_SEND,LOG_MESG_RECV,"White","Message") ==
	 C2S_ERROR)
	return C2S_ERROR;      
      break;
    }
  }
  return rec_type;
}

/****
     all the mpi calls are initialized in the state_info linked list.
     this allows us to ignore the need for knowing state definitions
     before starting slog logging. 
     WARNING: if used, this function should be called
     before any state definitions are initialized since it assumes that 
     it provides the very first state definitions in the linked list of 
     state definitions.
****/
int C2S1_init_all_mpi_state_defs ( ) {
  int event_id = 1;  /* identical to the state initializations in
		        "log_wrap.c" in the mpe direcory under
			MPI_Init's definition.
		     */
  addState( 0, event_id, event_id+1, "white:vlines", "ALLGATHER" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ALLGATHERV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "purple:vlines3", "ALLREDUCE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ALLTOALL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ALLTOALLV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "yellow:dimple3", "BARRIER" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "cyan:boxes", "BCAST" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GATHER" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GATHERV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "OP_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "OP_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "REDUCE_SCATTER" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "purple:2x2", "REDUCE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "SCAN" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "SCATTER" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "SCATTERV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ATTR_DELETE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ATTR_GET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ATTR_PUT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_COMPARE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_DUP" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_GROUP" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_RANK" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_REMOTE_GROUP" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_REMOTE_SIZE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_SIZE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_SPLIT" ); 
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "COMM_TEST_INTER" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_COMPARE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_DIFFERENCE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_EXCL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_INCL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_INTERSECTION" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_RANK" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_RANGE_EXCL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_RANGE_INCL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_SIZE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_TRANSLATE_RANKS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GROUP_UNION" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "INTERCOMM_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "INTERCOMM_MERGE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "KEYVAL_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "KEYVAL_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ABORT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ERROR_CLASS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ERRHANDLER_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ERRHANDLER_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ERRHANDLER_GET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ERROR_STRING" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ERRHANDLER_SET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GET_PROCESSOR_NAME" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "INITIALIZED" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "WTICK" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "WTIME" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "ADDRESS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "blue:gray3", "BSEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "BSEND_INIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "BUFFER_ATTACH" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "BUFFER_DETACH" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CANCEL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "REQUEST_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "RECV_INIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "SEND_INIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GET_ELEMENTS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GET_COUNT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "IBSEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "seagreen:gray", "IPROBE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "springgreen:gray", "IRECV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "IRSEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "skyblue:gray", "ISEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "seagreen:gray", "ISSEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "PACK" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "PACK_SIZE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "seagreen:gray", "PROBE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "green:light_gray", "RECV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "RSEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "RSEND_INIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "blue:gray3", "SEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "seagreen:gray", "SENDRECV" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "seagreen:gray", "SENDRECV_REPLACE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "deepskyblue:gray", "SSEND" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "SSEND_INIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "START" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "STARTALL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "orange:gray",  "TEST" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "orange:gray",  "TESTALL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "orange:gray", "TESTANY" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TEST_CANCELLED" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "orange:gray", "TESTSOME" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_COMMIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_CONTIGUOUS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_EXTENT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_FREE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_HINDEXED" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_HVECTOR" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_INDEXED" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_LB" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_SIZE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_STRUCT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_UB" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TYPE_VECTOR" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "UNPACK" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "red:black", "WAIT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "OrangeRed:gray", "WAITALL" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "coral:gray", "WAITANY" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "red:black", "WAITSOME" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_COORDS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_GET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_MAP" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_SHIFT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CARTDIM_GET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "DIMS_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GRAPH_CREATE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GRAPH_GET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GRAPH_MAP" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GRAPH_NEIGHBORS" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GRAPH_NEIGHBORS_COUNT" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "GRAPHDIMS_GET" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "TOPO_TEST" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "RECV_IDLE" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_RANK" );
  event_id+=2;
  addState( 0, event_id, event_id+1, "white:vlines", "CART_SUB" );
  event_id+=2;

  /* initialize message state in the state_info linked list */
  
  if(addState(MSG_STATE,LOG_MESG_SEND,LOG_MESG_RECV,"White","Message") ==
     C2S_ERROR)
    return C2S_ERROR;

  return C2S_SUCCESS;
}  

/****
     initialization of slog when all state definitions and the number of 
     processes and number of events are known.
****/
int C2S1_init_SLOG (long num_frames, long frame_size, char *slog_file ) {
  
  long fixed_record_size,
       kilo_byte  = 1024,
       error = C2S_SUCCESS;

  slog = SLOG_OpenOutputStream( slog_file );
  free(slog_file);

  if(slog == NULL) {
    fprintf(stderr, __FILE__":%d: SLOG_OpenOutputStream returns null - "
	    "check SLOG documentation for more information.\n",__LINE__);
    C2S1_free_state_info();
    return C2S_ERROR;
  }
  
  fixed_record_size = SLOG_typesz[ min_IntvlRec ] + SLOG_typesz[ taskID_t ];

  /*
    calculating the number of frames that would be required to convert
    the clog file. it is not possible to estimate this value for small
    frame byte sizes because the number of pseudo records in the slog file
    maybe much larger than the number of individual records.
  */
  if(num_frames == 0) {
      num_frames = (num_events * fixed_record_size) /
	  ((frame_size*kilo_byte)-SLOG_typesz[FrameHdr]);
      num_frames++;
  }
  SLOG_SetMaxNumOfFramesPerDir(slog, num_frames);
  SLOG_SetFrameByteSize(slog, frame_size*kilo_byte );
  SLOG_SetFrameReservedSpace(slog, 0);
  SLOG_SetIncreasingEndtimeOrder(slog);

  /*
    this is of no use and should be taken out whenever the
    dependency on the file "SLOG_Preview.txt"  gets removed
  */
#ifndef HAVE_WINDOWS_H
  SLOG_SetPreviewName(slog,SLOG_PREVIEW_NAME);
#endif

  /*
    initializing slog tread table, profiling and record definition table.
  */
  error = init_SLOG_TTAB();
  if(error == C2S_ERROR)
    return error;
  error = init_SLOG_PROF_RECDEF();
  return error;
}

/****
     initialize number of events and number of processes
****/
void C2S1_init_essential_values(long event_count, int process_count) {
  num_events = event_count;
  proc_num   = process_count;
}

/****
     this is the function which does all the logging in the second pass.
     it looks for CLOG_RAWEVENT types and then passes it on to the logEvent
     function where all the details are handled.
****/
int C2S1_make_SLOG(double *membuff) {

  int  rec_type;
  CLOG_HEADER* headr;
  CLOG_STATE* state;          /* pointer to a clog state definition.*/ 
  double *data_ptr = membuff;
  
  rec_type = CLOG_UNDEF;

  while((rec_type != CLOG_ENDBLOCK) && (rec_type != CLOG_ENDLOG)) {
    headr    = (CLOG_HEADER *)data_ptr;
#if defined( C2S_BYTESWAP )
    adjust_CLOG_HEADER(headr);
#endif
    rec_type = headr->rectype;
    data_ptr = headr->rest;
    switch(rec_type) {
      
    case CLOG_MSGEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_MSG ((CLOG_MSG *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_MSG *) data_ptr)->end);
      break;
      
    case CLOG_COLLEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_COLL ((CLOG_COLL *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_COLL *) data_ptr)->end);
      break;
      
    case CLOG_RAWEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_RAW ((CLOG_RAW *)data_ptr);
#endif
      if(logEvent( headr, (CLOG_RAW *)data_ptr) == C2S_ERROR)
	return C2S_ERROR;
      data_ptr = (double *) (((CLOG_RAW *) data_ptr)->end);
      break;
	     
    case CLOG_SRCLOC:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_SRC ((CLOG_SRC *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_SRC *) data_ptr)->end);
      break;
      
    case CLOG_COMMEVENT:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_COMM ((CLOG_COMM *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_COMM *) data_ptr)->end);
      break;
      
    case CLOG_STATEDEF:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_STATE ((CLOG_STATE *)data_ptr);
#endif
      state = (CLOG_STATE *)data_ptr;
      if( handle_extra_state_defs(state) == C2S_ERROR )
	return C2S_ERROR;
      data_ptr = (double *) (((CLOG_STATE *) data_ptr)->end);
      break;
      
    case CLOG_EVENTDEF:
#if defined( C2S_BYTESWAP )
      adjust_CLOG_EVENT ((CLOG_EVENT *)data_ptr);
#endif
      data_ptr = (double *) (((CLOG_EVENT *) data_ptr)->end);
      break;
      
    case CLOG_ENDBLOCK:
      break;
     
    case CLOG_ENDLOG:
      break;
    }
  }
  return rec_type;
}

/****
     the most important function of all.
     this function takes the decision whether to add an event log into the
     start event list and reserve space in the slog file OR to match it with a 
     matching start event in the list and log the interval.
     the arguments to the function include a pointer to the header of the 
     event log as well as a pointer to the event log itself.
****/
static int logEvent( CLOG_HEADER *headr, CLOG_RAW *event_ptr )
{
  /*
  static int N_mpi_proc_null = 0;
  */

  int stat_id;
  int error = C2S_SUCCESS;
  struct list_elemnt one; /* a structure that will contain information
			     gathered from the start event list if there is
			     a start event corresponding to the incoming
			     event.
			  */

  if ( (stat_id = findState_strtEvnt(event_ptr->etype)) != C2S_ERROR ) {
      /* 
         if the above condition is true then we have run into a start event.
         now you know why that state definition list is so important. how
         else would one know if an event is a start event or an end event or 
         neither.
      */
      if ( stat_id == MSG_STATE ) {
          if ( event_ptr->data != MPI_PROC_NULL ) {
              if ( find_msg_elemnt( stat_id, event_ptr->data, headr->procid,
                                    event_ptr->etype, &one ) == C2S_ERROR )
                  error = handleStartEvent( stat_id, headr, event_ptr );
              else 
                  error = writeSLOGInterval( headr, event_ptr, one );
          }
	  /*
          else {
	      N_mpi_proc_null += 1;
	      fprintf( stderr, "logEvent(BEGIN) : N_mpi_proc_null = %d\n",
			       N_mpi_proc_null );
          }
	  */
      }
      else
          error = handleStartEvent( stat_id, headr, event_ptr );
  }
  else if( (stat_id = findState_endEvnt(event_ptr->etype)) != C2S_ERROR ) {
      /*
        if the above condition is true we have run into an end event.
        so now we find the corresponding start event in the start event list
        and log an slog interval. a lot of memory allocation and deallocation
        happening around this point.
      */
    
      if (    find_elemnt(stat_id, event_ptr->data, headr->procid, &one)
           == C2S_ERROR ) {
        /*
	  if the above condition is true then we haven't found a matching start 
	  event in the list of start events. hence there has been an error in
	  the logging. an end event without a start event - definitely something
	  wrong - either in this code or in the logging itself.
	  Of course, if it is the other way around then it is ignored. 
	  but that case arises only when the list of start events is not empty 
	  at the end of the second pass. i havent taken it into account.
        */
        /*
	  new documentation - such a case may also arise because of a message
	  event. hence we log a recieve event as if it were a normal
	  start event. Though any other clog-state having the same problem is 
	  unacceptable and hence the error message at the end.
        */
          if ( stat_id == MSG_STATE ) {
              if ( event_ptr->data != MPI_PROC_NULL ) {
                  if ( find_msg_elemnt( stat_id, event_ptr->data, headr->procid,
                                        event_ptr->etype, &one ) == C2S_ERROR )
	              error = handleStartEvent( stat_id, headr, event_ptr );
	          else
	              error = writeSLOGInterval( headr, event_ptr, one );
	      }
	      /*
	      else {
	           N_mpi_proc_null += 1;
	           fprintf( stderr, "logEvent(END) : N_mpi_proc_null = %d\n",
	                            N_mpi_proc_null );
              }
	      */
          }
          else {
	      fprintf( stderr, __FILE__":%d: couldnt find matching start event"
	                       ",state=%d,processid=%d,data=%d,timestamp=%f\n",
		 	       __LINE__, stat_id, headr->procid,
			       event_ptr->data, headr->timestamp );
#ifdef DEBUG_PRINT
	      /*
	      printEventList();
	      printStateInfo();
	      free(one);
	      C2S1_free_resources();
	      */
#endif
	      return C2S_ERROR;
          }
      }
      else {
          error = writeSLOGInterval( headr, event_ptr, one );
      }
  }

  return error;
}

/****
     handles state definitions during parsing of log file when slog conversion
     begins.
****/
static int handle_extra_state_defs(CLOG_STATE *state) {

  SLOG_intvltype_t intvltype;
  SLOG_bebit_t     bebit_0 = 1;
  SLOG_bebit_t     bebit_1 = 1;
  SLOG_N_assocs_t  Nassocs = 0;
  SLOG_N_args_t    Nargs   = 0;

  int ierr;
  
  /*
    checking to see if state has not already been defined 
    and also to see if the start or end event id or both
    are not already defined. CLOG2SLOG requires unique event ids'
    to distinguish between CLOG records.
  */
  if((findState_strtEvnt(state->endetype) != C2S_ERROR) ||
     (findState_endEvnt(state->startetype) != C2S_ERROR)) {
    fprintf(stderr, __FILE__":%d: event ids defined for state %s already "
	    "exist. Use MPE_Log_get_event_number() to define new event ids.\n",
	    __LINE__,state->description); 
    return C2S_ERROR;
  }
  else if((findState_strtEvnt(state->startetype) != C2S_ERROR) &&
	  (findState_endEvnt(state->endetype) != C2S_ERROR)) 
    return C2S_SUCCESS;

  if( addState(state->stateid, state->startetype, state->endetype,
	       state->color, state->description) == C2S_ERROR )
    return C2S_ERROR;
  
  intvltype = (unsigned int)(state_id - 1);
  
  ierr = SLOG_RDEF_AddExtraRecDef( slog, intvltype, bebit_0, bebit_1,
                                   Nassocs, Nargs );
  if( ierr != SLOG_SUCCESS ) {
    fprintf( stderr, __FILE__":%d: SLOG Record Definition initialization"
	     " failed. \n", __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }

  ierr = SLOG_PROF_AddExtraIntvlInfo( slog, intvltype, bebit_0, bebit_1,
                                      CLASS_TYPE,
				      state->description, state->color );
  if( ierr != SLOG_SUCCESS ) {
    fprintf( stderr, __FILE__":%d: SLOG_PROF_AddExtraIntvlInfo failed - "
	     "check SLOG documentation for more information. \n",
	     __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }
  return C2S_SUCCESS;
}
       	     
/****
     wirtes an SLOG interval
****/
static int writeSLOGInterval(CLOG_HEADER *headr, CLOG_RAW *event_ptr,
		       struct list_elemnt one) {
  /*
    SLOG STUFF:
    we are not interested in bebits, cpu_id, thread_id, irec_iaddr
    and the rec_type is always a constant, NON_MSG_RECORD, a fixed
    interval record as opposed to a variable interval record.
  */
  SLOG_Irec irec;
  SLOG_rectype_t   irec_rectype;
  SLOG_intvltype_t irec_intvltype;
  SLOG_starttime_t irec_starttime;
  SLOG_duration_t  irec_duration;
  SLOG_bebit_t     irec_bebit_0 = 1;
  SLOG_bebit_t     irec_bebit_1 = 1;
  SLOG_nodeID_t    irec_node_id;
  SLOG_nodeID_t    irec_destination_id;
  SLOG_cpuID_t     irec_cpu_id    = 0;
  SLOG_threadID_t  irec_thread_id = 0;
  SLOG_iaddr_t     irec_iaddr     = 0;

  int error = SLOG_SUCCESS;

  /*
    finally an slog fixed interval record is created and logged.
  */
  irec_node_id   = headr->procid;
  irec_intvltype = one.state_id;
  irec_starttime = one.start_time;
  irec_duration  = headr->timestamp - one.start_time;
  irec_rectype = NON_MSG_RECORD;
  
  /*free(one);*/
  
#if defined( NOARROW )
  /*
     avoid logging arrows/messages into slogfile
  */
  if (one.state_id == MSG_STATE )
      return C2S_SUCCESS;
#endif
  
  irec = SLOG_Irec_Create();
  
  if(irec == NULL) {
    fprintf(stderr, __FILE__":%d: SLOG_Irec_Create returned null - "
	    "system might be low on memory.\n",__LINE__);
    return C2S_ERROR;
  }
  
  if(one.state_id != MSG_STATE)
    error = SLOG_Irec_SetMinRec( irec, irec_rectype, irec_intvltype,
				 irec_bebit_0, irec_bebit_1, 
				 irec_starttime, irec_duration,
				 irec_node_id, irec_cpu_id, irec_thread_id,
				 irec_iaddr );
  else {
    irec_rectype = MSG_RECORD;
    if(event_ptr->etype == LOG_MESG_RECV)
      irec_intvltype = FORWARD_MSG;
    else
      irec_intvltype = BACKWARD_MSG;

    irec_node_id = one.process_id;
    irec_destination_id = one.data;
    error = SLOG_Irec_SetMinRec( irec, irec_rectype, irec_intvltype, 
				 irec_bebit_0, irec_bebit_1,
				 irec_starttime, irec_duration,
				 irec_node_id, irec_cpu_id,
				 irec_thread_id, irec_iaddr,
				 irec_destination_id, irec_cpu_id,
				 irec_thread_id );
  }      
  if(error == SLOG_FAIL) {
    SLOG_Irec_Free(irec);
    fprintf(stderr, __FILE__":%d: SLOG_Irec_SetMinRec returns failure - "
	    "check SLOG documentation for more information.\n",__LINE__);
    return C2S_ERROR;
  }
  
  error = SLOG_Irec_ToOutputStream( slog, irec );
  SLOG_Irec_Free(irec);
  if(error == SLOG_FAIL) {
    fprintf(stderr, __FILE__":%d: SLOG_Irec_ToOutputStream returns failure - "
	    "check SLOG documentation for more information.\n",
	     __LINE__);
    return C2S_ERROR;
  }
  return C2S_SUCCESS;
}

/****
     add incoming event to list of start events. aka "list_first/list_last"
     list. 
****/
static int handleStartEvent(int stat_id, CLOG_HEADER *headr, 
		      CLOG_RAW *event_ptr) {
  /*
    SLOG STUFF:
    we are not interested in bebits, cpu_id, thread_id, irec_iaddr
    and the rec_type is always a constant, NON_MSG_RECORD, a fixed
    interval record as opposed to a variable interval record.
  */
  SLOG_rectype_t   irec_rectype;
  SLOG_intvltype_t irec_intvltype;
  SLOG_starttime_t irec_starttime;
  SLOG_bebit_t     irec_bebit_0 = 1;
  SLOG_bebit_t     irec_bebit_1 = 1;

  int data,
      proc_id,
      rec_type,
      error   = SLOG_SUCCESS;
  double start_time;
  
  data       = event_ptr->data;
  start_time = headr->timestamp;
  proc_id    = headr->procid;
  rec_type   = event_ptr->etype;
  irec_rectype = NON_MSG_RECORD;
  
#if defined( NOARROW )
  /*
     avoid logging arrows/messages into slogfile   
  */
  if ( stat_id == MSG_STATE )
      return C2S_SUCCESS;
#endif

  if(stat_id != MSG_STATE) {
    if(addToList( stat_id, data, proc_id, start_time) == C2S_ERROR)
      return C2S_ERROR;
  }
  else {
    if(addToMsgList( stat_id, data, proc_id, rec_type, start_time) == C2S_ERROR)
      return C2S_ERROR;
  }
  
  irec_intvltype = stat_id; 
  irec_starttime = start_time;
  if(stat_id != MSG_STATE)
    error = SLOG_Irec_ReserveSpace( slog, irec_rectype, irec_intvltype, 
				    irec_bebit_0, irec_bebit_1,
				    irec_starttime );
  else {
    irec_rectype = MSG_RECORD;    
    
    if(event_ptr->etype == LOG_MESG_SEND) {
      irec_intvltype = FORWARD_MSG;
      error = SLOG_Irec_ReserveSpace( slog, irec_rectype, irec_intvltype, 
				      irec_bebit_0, irec_bebit_1,
				      irec_starttime );
    }
    else {
      irec_intvltype = BACKWARD_MSG;
      error = SLOG_Irec_ReserveSpace( slog, irec_rectype, irec_intvltype, 
				      irec_bebit_0, irec_bebit_1,
				      irec_starttime );
    }
  }

  if(error == SLOG_FAIL) {
    fprintf(stderr, __FILE__":%d: SLOG_Irec_ReserveSpace returns FAILURE"
	    " - system might have run out of memory. Check SLOG "
	    "documentation for more information.\n", __LINE__);
    return C2S_ERROR;
  }

  return C2S_SUCCESS;
}

/****
     initialzes the thread table.
****/
int init_SLOG_TTAB() {
  int ierr, ii;

  SLOG_nodeID_t      node_id;
  SLOG_threadID_t    thread_id    = 0;
  SLOG_OSprocessID_t OSprocess_id = 0;
  SLOG_OSthreadID_t  OSthread_id  = 0;
  SLOG_appID_t       app_id;

  if( SLOG_TTAB_Open( slog ) != SLOG_SUCCESS ) {
    fprintf(stderr, __FILE__":%d: SLOG_TTAB_Open() fails! \n",__LINE__ );
    C2S1_free_resources();
    return C2S_ERROR;
  }

  for( ii=0 ; ii <= proc_num ; ii++) {
    node_id = app_id = ii;
    ierr = SLOG_TTAB_AddThreadInfo( slog, node_id, thread_id,
				    OSprocess_id, OSthread_id, app_id);
	
    if( ierr != SLOG_SUCCESS ) {
      fprintf( stderr,__FILE__":%d: SLOG Thread Table initialization "
	       "failed.\n",__LINE__);
      C2S1_free_resources();
      return C2S_ERROR;
    }
  }

  if((ierr = SLOG_TTAB_Close( slog )) != SLOG_SUCCESS ) {
    fprintf(stderr, __FILE__":%d: SLOG_TTAB_Close() fails! \n", __LINE__ );
    C2S1_free_resources();
    return C2S_ERROR;
  }
  return C2S_SUCCESS;
}

/****
     initializes the profiling as well as the record definition tables.
     for details look at the slog api documentation.
     clog counterparts:
     state definitions  =  record definition + profiling
****/
int init_SLOG_PROF_RECDEF() {
  int ierr;
  struct state_info *one  = NULL,
                    *two  = NULL;
  SLOG_intvltype_t intvltype;
  SLOG_bebit_t     bebit_0 = 1;
  SLOG_bebit_t     bebit_1 = 1;
  SLOG_N_assocs_t  Nassocs = 0;
  SLOG_N_args_t    Nargs   = 0;
  
  if( SLOG_PROF_Open( slog ) != SLOG_SUCCESS ) {
    fprintf(stderr, __FILE__":%d: SLOG_PROF_Open() fails! \n", __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }

  /* initialization of the interval table */

  for( one = first ; one != NULL ; one = two ) {
    two = one->next;
    intvltype = (unsigned int)(one->state_id);
    if((one->start_event_num != LOG_MESG_SEND) ||
       (one->end_event_num != LOG_MESG_RECV))
      ierr = SLOG_PROF_AddIntvlInfo( slog, intvltype, bebit_0, bebit_1,
		                     CLASS_TYPE, one->description, one->color,
				     Nargs );
    else {
      ierr = SLOG_PROF_AddIntvlInfo( slog, FORWARD_MSG, bebit_0, bebit_1,
                                     FORWARD_MSG_CLASSTYPE,
                                     FORWARD_MSG_LABEL,
                                     FORWARD_MSG_COLOR,
				     Nargs );
      if( ierr != SLOG_SUCCESS ) {
	fprintf( stderr,__FILE__":%d: SLOG Profile initialization failed.\n",
		  __LINE__);
	C2S1_free_resources();
	return C2S_ERROR;
      }
      ierr = SLOG_PROF_AddIntvlInfo( slog, BACKWARD_MSG, bebit_0, bebit_1,
                                     BACKWARD_MSG_CLASSTYPE,
                                     BACKWARD_MSG_LABEL,
                                     BACKWARD_MSG_COLOR,
				     Nargs );
      }
    if( ierr != SLOG_SUCCESS ) {
      fprintf( stderr,__FILE__":%d: SLOG Profile initialization failed. \n",
	        __LINE__);
      C2S1_free_resources();
      return C2S_ERROR;
    }
      
  }

  if(    SLOG_PROF_SetExtraNumOfIntvlInfos( slog, EXTRA_STATES )
      != SLOG_SUCCESS ) {
    fprintf(stderr, __FILE__
                    ":%d: SLOG_PROF_SetExtraNumOfIntvlInfos() fails! \n", 
	            __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }
  if( SLOG_RDEF_Open( slog ) != SLOG_SUCCESS ) {
    fprintf(stderr, __FILE__":%d: SLOG_RDEF_Open() fails! \n", __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }

  for( one = first ; one != NULL ; one = two ) {
    two = one->next;
    intvltype = (unsigned int)(one->state_id);

    if((one->start_event_num != LOG_MESG_SEND) ||
       (one->end_event_num != LOG_MESG_RECV))
      ierr = SLOG_RDEF_AddRecDef( slog, intvltype,
                                  bebit_0, bebit_1,
                                  Nassocs, Nargs );
    else {
      ierr = SLOG_RDEF_AddRecDef( slog, FORWARD_MSG,
				  bebit_0, bebit_1, 
				  Nassocs, Nargs );
      if( ierr != SLOG_SUCCESS ) {
	fprintf( stderr,__FILE__":%d: SLOG Record Definition initialization"
		 " failed. \n",__LINE__);
	C2S1_free_resources();
	return C2S_ERROR;
      }
      ierr = SLOG_RDEF_AddRecDef( slog, BACKWARD_MSG,
				  bebit_0, bebit_1, 
				  Nassocs, Nargs );
    }
    if( ierr != SLOG_SUCCESS ) {
      fprintf( stderr,__FILE__":%d: SLOG Record Definition initialization"
	       " failed. \n", __LINE__);
      C2S1_free_resources();
      return C2S_ERROR;
    }
  }

  if( SLOG_RDEF_SetExtraNumOfRecDefs( slog,EXTRA_STATES ) != SLOG_SUCCESS ) {
    fprintf(stderr, __FILE__":%d: SLOG_RDEF_SetExtraNumOfRecDefs() fails! \n",
	    __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }
  return C2S_SUCCESS;
}
	 

/****
     names says it all. 
     if big endian then initialize to BIGENDIAN to 1.
****/

/*
static void checkForBigEndian() {
    union {
	long l;
	char ch[sizeof(long)];
    }u;
    
    u.l=1;
    if(u.ch[sizeof(long) - 1] == 1)
        BIGENDIAN = 1;
    else
        BIGENDIAN = 0;

}
*/
	
	    
/****
     add a new state definition to the end of the state definition list.
****/
static int addState(int stat_id, int strt_id, int end_id,
	      CLOG_CNAME colr, CLOG_DESC desc) {

  struct state_info *temp_ptr;
  int s_id, e_id;
  
  s_id = findState_strtEvnt(strt_id);
  e_id = findState_endEvnt(end_id);
  if((s_id != C2S_ERROR) &&
     (e_id != C2S_ERROR)) {
    if(s_id == e_id) {
      replace_state_in_list(strt_id, end_id, colr, desc);
      return C2S_SUCCESS;
    }
    else {
      fprintf(stderr,__FILE__":%d: event ids defined for state %s already "
	      "exist. Use MPE_Log_get_event_number() to define new event ids.",
	      __LINE__,desc);
      return C2S_ERROR;
    }
  }
  else if((s_id != C2S_ERROR) ||
	  (e_id != C2S_ERROR)) {
    fprintf(stderr,__FILE__":%d: event ids defined for state %s already "
	    "exist. Use MPE_Log_get_event_number() to define new event ids.",
	    __LINE__,desc); 
    return C2S_ERROR;
  }
  
  temp_ptr  =  ( struct state_info *) MALLOC( sizeof(struct state_info) );

  if(temp_ptr == NULL) {
    fprintf(stderr,__FILE__":%d: not enough memory for start event list!\n",
	    __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }
  if(stat_id != MSG_STATE)
    temp_ptr->state_id           = get_new_state_id();
  else
    temp_ptr->state_id           = MSG_STATE;
  temp_ptr->start_event_num    = strt_id;
  temp_ptr->end_event_num      = end_id;
  /*temp_ptr->color = (char *)(MALLOC(strlen(colr)+1));      */
  /*temp_ptr->description = (char *)(MALLOC(strlen(desc)+1));*/
  strncpy(temp_ptr->color,colr,sizeof(CLOG_CNAME)-1);
  temp_ptr->color[sizeof(CLOG_CNAME)-1] = '\0';
  strncpy(temp_ptr->description,desc,sizeof(CLOG_DESC)-1);
  temp_ptr->description[sizeof(CLOG_DESC)-1] = '\0';
  temp_ptr->next               = NULL;
  
  if(first == NULL) {
    first = temp_ptr;
    last  = temp_ptr;
  }
  else {
    last->next = temp_ptr;
    last       = temp_ptr;
  }
  return C2S_SUCCESS;
	
}

static int replace_state_in_list(int start_id, int end_id, 
			  CLOG_CNAME colr, CLOG_DESC desc) {
  struct state_info *one = NULL,
                    *two = NULL;
  for(one = first; one != NULL; one = two){
    two = one->next;
    if(one->start_event_num == start_id) {
      one->start_event_num    = start_id;
      one->end_event_num      = end_id;
      strncpy(one->color,colr,sizeof(CLOG_CNAME)-1);
      one->color[sizeof(CLOG_CNAME)-1] = '\0';
      strncpy(one->description,desc,sizeof(CLOG_DESC)-1);
      one->description[sizeof(CLOG_DESC)-1] = '\0';
      return C2S_SUCCESS;
    }
  }
  return C2S_ERROR;
} 

/****
     finds a state id for the given start event id from the state def list.
****/
static int findState_strtEvnt(int strt_id) {
  struct state_info *one = NULL,
                    *two = NULL;
  for(one = first; one != NULL; one = two){
    two = one->next;
    if(one->start_event_num == strt_id)
      return one->state_id;
  }
  return C2S_ERROR;
}

/****
     finds a state id for the given end event id from the state def list.
****/
static int findState_endEvnt(int end_id){

  struct state_info *one = NULL,
                    *two = NULL;
  for(one = first; one != NULL; one = two){
    two = one->next;
    if(one->end_event_num == end_id)
      return one->state_id;
  }
  return C2S_ERROR;
}

/****
     frees up memory malloced for state definitions in the list of state defs.
****/
void C2S1_free_state_info(void) {
  struct state_info *one = NULL,
                    *two = NULL;
  for(one = first; one != NULL; one = two) {
    two = one->next;
    free(one);
  }
}

/****
     print all the elements in the state definition list. helps a lot in
     debugging if the need be.
****/
#ifdef DEBUG_PRINT
void printStateInfo(void) {
  struct state_info *one = NULL,
                    *two = NULL;
  for(one = first; one != NULL; one = two) {
    two = one->next;
    printf("%d  %d  %d %s %s\n",one->state_id,
	   one->start_event_num,one->end_event_num,one->description,
	   one->color);
    fflush(stdout);
  }
}
#endif
/****
     adds a start event to the start event list. the only relevant info 
     needed for slogging are state id, the data from CLOG_RAWEVENT and the 
     start time. the start time is needed to calculate the interval duration
     when the end event is found. the new element is added to the front of the
     list - why??? - well, just in case there are nested events(threads) or 
     non-blocking MPI calls. hasnt been tested on those two conditions yet.
****/
static int addToList(int stat_id, int data, int proc_id, double strt_time) {

  struct list_elemnt *temp_ptr =
    (struct list_elemnt *)MALLOC
    (sizeof(struct list_elemnt));

  if(temp_ptr == NULL) {
    fprintf(stderr,__FILE__":%d: not enough memory for start event list!\n",
	    __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }

  temp_ptr->state_id        = stat_id;
  temp_ptr->data            = data;
  temp_ptr->process_id      = proc_id;
  temp_ptr->start_time      = strt_time;
  temp_ptr->next            = NULL;
  
  
  if(list_first == NULL) {
    list_first = temp_ptr;
    list_last  = temp_ptr;
  }
  else {
    temp_ptr->next = list_first;
    list_first     = temp_ptr;
    /*
    list_last->next = temp_ptr;
    list_last       = temp_ptr;
    */
  }
  events++;
  return C2S_SUCCESS;
}

/****
     adds a start event to the start event list. the only relevant info 
     needed for slogging are state id, the data from CLOG_RAWEVENT and the 
     start time. the start time is needed to calculate the interval duration
     when the end event is found. the new element is added to the front of the
     list - why??? - well, just in case there are nested events(threads) or 
     non-blocking MPI calls. hasnt been tested on those two conditions yet.
****/
static int addToMsgList(int stat_id, int data, int proc_id,
                 int rec_type, double strt_time) {

  struct list_elemnt *temp_ptr =
    (struct list_elemnt *)MALLOC(sizeof(struct list_elemnt));

  if(temp_ptr == NULL) {
    fprintf(stderr,__FILE__":%d: not enough memory for message list!\n",
	    __LINE__);
    C2S1_free_resources();
    return C2S_ERROR;
  }

  temp_ptr->state_id        = stat_id;
  temp_ptr->data            = data;
  temp_ptr->process_id      = proc_id;
  temp_ptr->rectype         = rec_type;
  temp_ptr->start_time      = strt_time;
  temp_ptr->next            = NULL;
  
  
  if(msg_list_first == NULL) {
    msg_list_first = temp_ptr;
    msg_list_last  = temp_ptr;
  }
  else {
    /*
    temp_ptr->next = msg_list_first;
    msg_list_first     = temp_ptr;
    */
    msg_list_last->next = temp_ptr;
    msg_list_last       = temp_ptr;
  }
  return C2S_SUCCESS;
}

/****
     find a start element for the corresponding end event info passed in the 
     arguments from the start event list. 
     that start event is then removed from the list and passed by reference to
     the calling function through the variable "element".
****/
static int find_elemnt(int stat_id, int data,int procid, struct list_elemnt *element){
  struct list_elemnt *one   = NULL,
                     *two   = NULL,
                     *three = NULL;
  for(one = list_first, three = list_first; one != NULL; one = two){
    two = one->next;
    if((one->state_id != MSG_STATE) && (one->state_id == stat_id) && 
       (one->process_id == procid)) {
      element->state_id   = one->state_id;
      element->data       = one->data;
      element->process_id = one->process_id;
      element->start_time = one->start_time;
      element->next = NULL;
      if(one == list_first) {
	if(one->next == NULL){
	  list_first = NULL;
	  list_last  = NULL;
	}
	else
	  list_first = one->next;
      }
      else {
	three->next = one->next;
	if(one == list_last)
	  list_last = three;
      }
      free(one);
      events--;
      return C2S_SUCCESS;
    }
    if(one != list_first)
      three = three->next;
    
  }
  return C2S_ERROR;
}

static int find_msg_elemnt(int stat_id, int data, int procid, int record_type,
                    struct list_elemnt *element){
  struct list_elemnt *one   = NULL,
                     *two   = NULL,
                     *three = NULL;
  for(one = msg_list_first, three = msg_list_first; one != NULL; one = two){
    two = one->next;
    if((one->state_id == MSG_STATE) && (one->state_id == stat_id) && 
       (one->process_id == data) && (one->data == procid) &&
       (one->rectype != record_type)) {
      element->state_id   = one->state_id;
      element->data       = one->data;
      element->process_id = one->process_id;
      element->rectype    = one->rectype;
      element->start_time = one->start_time;
      element->next = NULL;
      if(one == msg_list_first) {
	if(one->next == NULL){
	  msg_list_first = NULL;
	  msg_list_last  = NULL;
	}
	else
	  msg_list_first = one->next;
      }
      else {
	three->next = one->next;
	if(one == msg_list_last)
	  msg_list_last = three;
      }
      free(one);
      list_messages--;
      return C2S_SUCCESS;
    }
    if(one != msg_list_first)
      three = three->next;
    
  }
  return C2S_ERROR;
}

/****
     frees memory malloced to the start event list.
     in theory there shouldnt be any elements left in this list at the end
     of the second pass but wierd things happen when you dont know how the
     clog file got generated.
****/
static void freeList(void){
  struct list_elemnt *one = NULL,
                     *two = NULL;
  for(one = list_first; one != NULL; one = two) {
    two = one->next;
    free(one);
  }
  list_first = NULL;
  list_last = NULL;
}
/****
     frees memory malloced to the message event list.
****/
static void freeMsgList(void){
  struct list_elemnt *one = NULL,
                     *two = NULL;
  for(one = msg_list_first; one != NULL; one = two) {
    two = one->next;
    free(one);
  }
  msg_list_first = NULL;
  msg_list_last = NULL;
}

#ifdef DEBUG_PRINT
/****
     print all the elements in the start event list. helps a lot in
     debugging if the need be.
****/
static void printEventList(void) {
  struct list_elemnt *one = NULL,
	             *two = NULL;
  for(one = list_first; one != NULL; one = two) {
    printf("%d,%d,%d\n",one->state_id,one->process_id,one->data);
    fflush(stdout);
    two = one->next;
  }
}
static void printMsgEventList(void) {
  struct list_elemnt *one = NULL,
	             *two = NULL;
  for(one = msg_list_first; one != NULL; one = two) {
    printf("%d,%d,%d\n",one->state_id,one->process_id,one->data);
    fflush(stdout);
    two = one->next;
  }
}
#endif

/****
     prints all the options available with this program.
****/
void C2S1_print_help(void) {
    fprintf(stdout,"Usage : clog2slog [ -d=FrameNum ] [ -f=FrameSize ]"
	    " [ -h ] file.clog\n"
	    "        where file.clog is a clog file\n"
	    "Options:\n"
	    "\td : \"FrameNum\" specifies the number of frames per"
	    " directory\n"
	    "\tf : \"FrameSize\" specifies the size of a frame in Kilobytes\n"
	    "\th : help menu\n\n"
            "Due to the limitations of the current implementation of SLOG-API\n"
            "If the default or supplied frame size is too small, it may cause\n"
            "problems in generation of the SLOG file.  If one encounters\n"
            "some strange errors in using clog2slog, like complaints about\n"
            "frame has been filled up or the maximin allowable number of\n"
            "frames has been reached, try to set the frame size bigger.\n"
            "e.g.  clog2slog -f=NewFrameSizeInKiloByte filename.clog\n"
            "If this does NOT work when your frame size reaches 4MB,\n"
            "try set the maximum number of frames to a bigger number than the\n"
            "guess shown in the error message from the previous run of\n" 
            "clog2slog.  e.g. clog2slog -d=NewFrameNumber filename.clog\n"
            "The default frame size is 64 KB.\n");
    fflush(stdout);
}
    
static int get_new_state_id(void) {
  return state_id++;
}






