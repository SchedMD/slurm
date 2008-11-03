/* Wrapper for rm_api.h */

#ifndef ATTACH_BGL_H
#define ATTACH_BGL_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_BG_FILES
  /* Over-ride attach_bg.h inclusion due to problems in compiling it 
   * with gcc and missing externals in that file */

  /* MPI Debug support */

  typedef struct {
    const char * host_name;        /* Something we can pass to inet_addr */
    const char * executable_name;  /* The name of the image */
    int    pid;                    /* The pid of the process */
  } MPIR_PROCDESC;

#include "rm_api.h"

#else
  typedef char *   pm_partition_id_t;
  typedef int      rm_connection_type_t;
  typedef int      rm_partition_mode_t;
  typedef int      rm_partition_state_t;
  typedef uint16_t rm_partition_t;
  typedef char *   rm_BGL_t;
  typedef char *   rm_component_id_t;
  typedef rm_component_id_t rm_bp_id_t;
  typedef int      rm_BP_state_t;

  /* these are the typedefs that we will need to have 
   * if we want the states on the Front End Node of a BG system
   * make certain they match the rm_api.h values on the Service Node */
  enum rm_partition_state {RM_PARTITION_FREE,
			   RM_PARTITION_CONFIGURING,
			   RM_PARTITION_READY,
			   RM_PARTITION_BUSY,
			   RM_PARTITION_DEALLOCATING,
			   RM_PARTITION_ERROR,
			   RM_PARTITION_NAV};
  typedef enum status {STATUS_OK  = 0,
	  	       PARTITION_NOT_FOUND = -1,
		       JOB_NOT_FOUND = -2,
		       BP_NOT_FOUND = -3,
		       SWITCH_NOT_FOUND = -4,
		       JOB_ALREADY_DEFINED=-5,
		       CONNECTION_ERROR=-10,
		       INTERNAL_ERROR = -11,
		       INVALID_INPUT=-12,
		       INCOMPATIBLE_STATE=-13,
		       INCONSISTENT_DATA=-14
  }status_t;

#endif

#ifdef HAVE_BGP_FILES
typedef rm_BGP_t my_bluegene_t;
#else
typedef rm_BGL_t my_bluegene_t;
#endif

#endif
