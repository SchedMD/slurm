/* $Id$ */

/* macros.h: some standard macros for slurm */

#ifndef _MACROS_H
#define _MACROS_H 	1

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef NULL
#  include <stddef.h>	/* for NULL */
#endif 

#if HAVE_STDBOOL_H
#  include <stdbool.h>
#else
typedef enum {false, true} bool;
#endif /* !HAVE_STDBOOL_H */

#ifndef FALSE
#define FALSE	false
#endif

#ifndef TRUE
#define TRUE	true
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))	
#endif

/* 
** define __CURRENT_FUNC__ macro for returning current function 
*/
#if defined (__GNUC__) && (__GNUC__ < 3)
#  define __CURRENT_FUNC__	__PRETTY_FUNCTION__
#else  /* !__GNUC__ */
#  ifdef _AIX 
#    define __CURRENT_FUNC__	__func__
#  else
#    define __CURRENT_FUNC__    ""
#  endif /* _AIX */
#endif /* __GNUC__ */

#ifndef __STRING
#  define __STRING(arg)		#arg
#endif

/* define macros for GCC function attributes if we're using gcc */

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 4)
#  define __PRINTF_ATTR( form_idx, arg_idx ) 	\
          __attribute__((__format__ (__printf__, form_idx, arg_idx)))
#  define __NORETURN_ATTR				\
          __attribute__((__noreturn__))
#else  /* !__GNUC__ */
#  define __PRINTF_ATTR( format_idx, arg_idx )	((void)0)
#  define __NORETURN_ATTR			((void)0)
#endif /* __GNUC__ */

/* the following is taken directly from glib 2.0, with minor changes */

/* Provide simple macro statement wrappers (adapted from Perl):
 *  _STMT_START { statements; } _STMT_END;
 *  can be used as a single statement, as in
 *  if (x) _STMT_START { ... } _STMT_END; else ...
 *
 *  For gcc we will wrap the statements within `({' and `})' braces.
 *  For SunOS they will be wrapped within `if (1)' and `else (void) 0',
 *  and otherwise within `do' and `while (0)'.
 */
#if !(defined (_STMT_START) && defined (_STMT_END))
#  if defined (__GNUC__) && !defined (__STRICT_ANSI__) && !defined (__cplusplus)
#    define _STMT_START        ((void)
#    define _STMT_END          )
#  else
#    if (defined (sun) || defined (__sun__))
#      define _STMT_START      if (1)
#      define _STMT_END        else (void)0
#    else
#      define _STMT_START      do
#      define _STMT_END        while (0)
#    endif
#  endif
#endif

#ifdef WITH_PTHREADS

#  define slurm_mutex_init(mutex)                                              \
     do {                                                                      \
         if ((errno = pthread_mutex_init(mutex, NULL)) != 0)                   \
             error("%s:%d %s: pthread_mutex_init(): %m", 		       \
	           __FILE__, __LINE__, __CURRENT_FUNC__);            	       \
     } while (0)

#  define slurm_mutex_destroy(mutex)                                           \
     do {                                                                      \
         if ((errno = pthread_mutex_destroy(mutex)) != 0)                      \
             error("%s:%d %s: pthread_mutex_destroy(): %m", 		       \
	           __FILE__, __LINE__, __CURRENT_FUNC__);            	       \
     } while (0)

#  define slurm_mutex_lock(mutex)                                              \
     do {                                                                      \
         if ((errno = pthread_mutex_lock(mutex)) != 0)	                       \
             error("%s:%d %s: pthread_mutex_lock(): %m", 		       \
	           __FILE__, __LINE__, __CURRENT_FUNC__);            	       \
     } while (0)

#  define slurm_mutex_unlock(mutex)                                            \
     do {                                                                      \
         if ((errno = pthread_mutex_unlock(mutex)) != 0)                       \
             error("%s:%d %s: pthread_mutex_unlock(): %m", 		       \
	           __FILE__, __LINE__, __CURRENT_FUNC__);            	       \
     } while (0)

#else /* !WITH_PTHREADS */

#  define slurm_mutex_init(mutex)
#  define slurm_mutex_destroy(mutex)
#  define slurm_mutex_lock(mutex)
#  define slurm_mutex_unlock(mutex)

#endif /* WITH_PTHREADS */


#endif /* !_MACROS_H */
