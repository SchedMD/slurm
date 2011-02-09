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

#if defined HAVE_BG_FILES && defined HAVE_BG_L_P

  /* MPI Debug support */
  typedef struct {
    const char * host_name;        /* Something we can pass to inet_addr */
    const char * executable_name;  /* The name of the image */
    int    pid;                    /* The pid of the process */
  } MPIR_PROCDESC;

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
#endif

/* The below #defines are needed for cross cluster dealings */
#ifdef HAVE_BGL
typedef rm_BGL_t my_bluegene_t;
#define PARTITION_ALREADY_DEFINED -6
#define RM_PARTITION_REBOOTING 1000
#elif defined HAVE_BGP
typedef rm_BG_t my_bluegene_t;
#define RM_PARTITION_BUSY 1000
#else
typedef void my_bluegene_t;
#define RM_PARTITION_BUSY 1000
#endif

#endif	/* #ifndef ATTACH_BG_H */
#endif	/* #ifndef ATTACH_BGL_H */
