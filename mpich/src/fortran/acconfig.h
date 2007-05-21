/* acconfig_cc.h */
/* Define if union semun is defined in sys/sem.h */
#undef HAVE_UNION_SEMUN

/* Define if semctl requires a union semun argument in the 4th position */
#undef SEMCTL_NEEDS_SEMUN

/* Define as empty if C does not support volatile */
#undef volatile

/* Define if #pragma weak supported */
#undef HAVE_PRAGMA_WEAK

/* Define is #pragma _HP_SECONDARY_DEF supported */
#undef HAVE_PRAGMA_HP_SEC_DEF

/* Define is #pragma _CRI duplicate x as y supported */
#undef HAVE_PRAGMA_CRI_DUP

/* Define if crypt requires a prototype */
#undef NEED_CRYPT_PROTOTYPE

/* Define as empty if C does not support any form of restrict or define 
   as the appropriate extension */
#undef restrict

/* Define if the XOPEN versions of routines and header files are needed */
#undef _XOPEN_SOURCE

/* Define as empty if C does not support inline */
#undef inline
/* acconfig_f77.h */
/* Define if Fortran uses lowercase name mangling */
#undef F77_NAME_LOWER

/* Define if Fortran use lowercase followed by an underscore */
#undef F77_NAME_LOWER_USCORE

/* Define if Fortran uses uppercase */
#undef F77_NAME_UPPER

/* Define if Fortran uses two underscores for names with an underscore 
   (and one for names without an underscore) */
#undef F77_NAME_LOWER_2USCORE

/* Define if Fortran leaves case unchanged */
#undef F77_NAME_MIXED

/* Define if Fortran leaves case unchanged, followed by an underscore */
#undef F77_NAME_MIXED_USCORE

/* Sizeof standard Fortran types */
#undef SIZEOF_F77_DOUBLE_PRECISION
#undef SIZEOF_F77_INTEGER
#undef SIZEOF_F77_INTEGER_4
#undef SIZEOF_F77_REAL
/* Define if MPI_xxx_f2c and c2f routines defined */
#undef HAVE_MPI_F2C
/* Define if Fortran functions pointers are pointers to pointers */
#undef FORTRAN_SPECIAL_FUNCTION_PTR

/* Define if Fortran character variable support requires this (Cray) */
#undef _TWO_WORD_FCD

#undef HAS_MPIR_ERR_SETMSG

#undef BUILDING_IN_MPICH

#undef F77_TRUE_VALUE_SET
#undef F77_TRUE_VALUE
#undef F77_FALSE_VALUE

#undef SIZEOF_F77_INTEGER_1
#undef SIZEOF_F77_INTEGER_2
#undef SIZEOF_F77_INTEGER_4
#undef SIZEOF_F77_INTEGER_8
#undef SIZEOF_F77_INTEGER_16

#undef SIZEOF_F77_REAL_4
#undef SIZEOF_F77_REAL_8
#undef SIZEOF_F77_REAL_16

#undef HAVE_WEAK_SYMBOLS

#undef USE_STDARG

#undef USE_OLDSTYLE_STDARG

/* Define if MPI_Fint is the same as a C int */
#undef FINT_IS_INT

#undef USE_POINTER_FOR_BOTTOM
