/* mpeconf.h.  Generated automatically by configure.  */
/* mpeconf.h.in.  Generated automatically from configure.in by autoheader.  */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if your processor stores words with the most significant
   byte first (like Motorola and SPARC, unlike Intel and VAX).  */
/* #undef WORDS_BIGENDIAN */

/* Define if void * is 8 bytes */
/* #undef POINTER_64_BITS */

/* Define if uname function available */
//#define HAVE_UNAME 1

/* Define in gethostbyname function available */
//#define HAVE_GETHOSTBYNAME 1

/* define is stdarg.h is available */
#define HAVE_STDARG_H 1

/* Define if stdarg can be used */
#define USE_STDARG 1

/* Define if C supports prototypes (but isn't ANSI C) */
#define HAVE_PROTOTYPES 1

/* Define if Fortran uses lowercase name mangling */
/* #undef F77_NAME_LOWER */

/* Define if Fortran use lowercase followed by an underscore */
/* #undef F77_NAME_LOWER_USCORE */

/* Define if Fortran uses uppercase */
/* #undef F77_NAME_UPPER */

/* Define if Fortran uses two underscores for names with an underscore 
   (and one for names without an underscore) */
#define F77_NAME_LOWER_2USCORE 1

/* Define if Fortran leaves case unchanged */
/* #undef F77_NAME_MIXED */

/* Define if Fortran leaves case unchanged, followed by an underscore */
/* #undef F77_NAME_MIXED_USCORE */

/* Define Fortran logical values used in MPI C program  */
/* #undef MPE_F77_TRUE_VALUE */
/* #undef MPE_F77_FALSE_VALUE */

/* Define if MPI_Wtime is there  */
/* #undef HAVE_MPI_WTIME */

/* Define if MPI_Fint if necessary */
/* #undef MPI_Fint */

/* Define MPI_STATUS_SIZE */
/* #undef MPI_STATUS_SIZE */

/* Define if MPI_COMM_f2c and c2f routines defined */
#define HAVE_MPI_COMM_F2C 1

/* Define if MPI_TYPE_f2c and c2f routines defined */
/* #undef HAVE_MPI_TYPE_F2C */

/* Define if MPI_GROUP_f2c and c2f routines defined */
/* #undef HAVE_MPI_GROUP_F2C */

/* Define if MPI_REQUEST_f2c and c2f routines defined */
/* #undef HAVE_MPI_REQUEST_F2C */

/* Define if MPI_OP_f2c and c2f routines defined */
/* #undef HAVE_MPI_OP_F2C */

/* Define if MPI_ERRHANDLER_f2c and c2f routines defined */
/* #undef HAVE_MPI_ERRHANDLER_F2C */

/* Define if MPI_Status_f2c and c2f rotines defined */
/* #undef HAVE_MPI_STATUS_F2C */

/* Define in MPI_RECV etc. does not set status on MPI_PROC_NULL */
/* #undef HAVE_BROKEN_STATUS_ON_PROC_NULL */

/* Define if sighandler_t defined in signal.h */
/* #undef HAVE_SIGHANDLER_T */

/* Define if we want to include the MPI IO routines */
#define HAVE_MPI_IO 1

/* Define if you have the sysinfo function.  */
//#define HAVE_SYSINFO 1

/* Define if you have the system function.  */
#define HAVE_SYSTEM 1

/* Define if you have the <netdb.h> header file.  */
//#define HAVE_NETDB_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/systeminfo.h> header file.  */
/* #undef HAVE_SYS_SYSTEMINFO_H */

/* Define if you have the <unistd.h> header file.  */
//#define HAVE_UNISTD_H 1

#ifndef HAVE_WINDOWS_H
#define HAVE_WINDOWS_H
#endif

#define OPEN( a , b , c ) open( a , b | O_BINARY , c )

#ifdef HAVE_MPI_IO
#define ROMIO_NTFS
#define HAVE_INT64
#endif
