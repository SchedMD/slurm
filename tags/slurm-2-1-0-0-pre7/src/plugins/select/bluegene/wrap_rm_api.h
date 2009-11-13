/* We can not include IBM's attach_bgl.h or attach_bg.h file due to problems
 * in compiling it with gcc and missing externals in that file, so we define
 * our own version of the header here and define critical variable. We also 
 * "#define ATTACH_BGL_H" and "define ATTACH_BG_H" to avoid having IBM's 
 * header files loaded for BGL and BGP systems respectively.*/

#ifndef ATTACH_BGL_H	/* Test for attach_bgl.h on BGL */
#ifndef ATTACH_BG_H	/* Test for attach_bg.h on BGP */
#define ATTACH_BGL_H	/* Replacement for attach_bgl.h on BGL */
#define ATTACH_BG_H	/* Replacement for attach_bg.h on BGP */

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_BG_FILES

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
  typedef char *   rm_BG_t;
  typedef char *   rm_component_id_t;
  typedef rm_component_id_t rm_bp_id_t;
  typedef int      rm_BP_state_t;
  typedef char *   rm_job_list_t;

  /* these are the typedefs that we will need to have 
   * if we want the states on the Front End Node of a BG system
   * make certain they match the rm_api.h values on the Service Node */
  enum rm_partition_state {RM_PARTITION_FREE,
			   RM_PARTITION_CONFIGURING,
			   RM_PARTITION_READY,
#ifdef HAVE_BGL
			   RM_PARTITION_BUSY,
#else
			   RM_PARTITION_REBOOTING,
#endif
			   RM_PARTITION_DEALLOCATING,
			   RM_PARTITION_ERROR,
			   RM_PARTITION_NAV};
  typedef enum status {STATUS_OK  = 0,
	  	       PARTITION_NOT_FOUND = -1,
		       JOB_NOT_FOUND = -2,
		       BP_NOT_FOUND = -3,
		       SWITCH_NOT_FOUND = -4,
		       JOB_ALREADY_DEFINED=-5,
#ifndef HAVE_BGL
		       PARTITION_ALREADY_DEFINED=-6,
#endif
		       CONNECTION_ERROR=-10,
		       INTERNAL_ERROR = -11,
		       INVALID_INPUT=-12,
		       INCOMPATIBLE_STATE=-13,
		       INCONSISTENT_DATA=-14
  }status_t;

#endif

#ifdef HAVE_BGL
typedef rm_BGL_t my_bluegene_t;
#else
typedef rm_BG_t my_bluegene_t;
#endif

#endif	/* #ifndef ATTACH_BG_H */
#endif	/* #ifndef ATTACH_BGL_H */
