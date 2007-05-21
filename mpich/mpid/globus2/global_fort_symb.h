
/*
 * CVS Id: $Id: global_fort_symb.h,v 1.4 2002/09/20 18:19:54 lacour Exp $
 */

/* This header file is included by "global_c_symb.h".  This is needed
 * to rename the Fortran functions called in initf.c, initf77.c,
 * initfutil.c (in directory src/fortran/src).  This file should be
 * read only when MPICH-G2 is built with vendor MPI. */

#ifndef GLOBAL_FORT_SYMB_H
#define GLOBAL_FORT_SYMB_H

#if defined(F77_NAME_UPPER)
#   define MPIR_INIT_FCM MPQR_INIT_FCM
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_init_fcm__ mpqr_init_fcm__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_init_fcm mpqr_init_fcm
#else
#   define mpir_init_fcm_ mpqr_init_fcm_
#endif

#if defined(F77_NAME_UPPER)
#   define MPIR_INIT_FLOG MPQR_INIT_FLOG
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_init_flog__ mpqr_init_flog__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_init_flog mpqr_init_flog
#else
#   define mpir_init_flog_ mpqr_init_flog_
#endif

#if defined(F77_NAME_UPPER)
#   define MPIR_GETARG MPQR_GETARG
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_getarg__ mpqr_getarg__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_getarg mpqr_getarg
#else
#   define mpir_getarg_ mpqr_getarg_
#endif

#if defined(F77_NAME_UPPER)
#   define MPIR_IARGC MPQR_IARGC
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_iargc__ mpqr_iargc__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_iargc mpqr_iargc
#else
#   define mpir_iargc_ mpqr_iargc_
#endif

#if defined(F77_NAME_UPPER)
#   define MPIR_GET_FSIZE MPQR_GET_FSIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_get_fsize__ mpqr_get_fsize__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_get_fsize mpqr_get_fsize
#else
#   define mpir_get_fsize_ mpqr_get_fsize_
#endif

#if defined(F77_NAME_UPPER)
#   define MPIR_INIT_FSIZE MPQR_INIT_FSIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_init_fsize__ mpqr_init_fsize__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_init_fsize mpqr_init_fsize
#else
#   define mpir_init_fsize_ mpqr_init_fsize_
#endif

#if defined(F77_NAME_UPPER)
#   define MPIR_INIT_BOTTOM MPQR_INIT_BOTTOM
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpir_init_bottom__ mpqr_init_bottom__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpir_init_bottom mpqr_init_bottom
#else
#   define mpir_init_bottom_ mpqr_init_bottom_
#endif

#endif   /* GLOBAL_FORT_SYMB_H */

