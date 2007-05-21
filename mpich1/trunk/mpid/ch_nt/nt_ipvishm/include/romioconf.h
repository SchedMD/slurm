/* Define as __inline if that's what the C compiler calls it.  */
//#undef inline

/* Define if `sys_siglist' is declared by <signal.h>.  */
#undef SYS_SIGLIST_DECLARED

#undef AIO_HANDLE_IN_AIOCB
#undef AIO_PRIORITY_DEFAULT
#undef AIO_SIGNOTIFY_NONE
#undef AIO_SUN
#undef FREEBSD
#undef HAVE_LONG_LONG_64
#undef HAVE_MOUNT_NFS
#undef HAVE_MPI_COMBINERS
#undef HAVE_MPI_DARRAY_SUBARRAY
#define HAVE_MPI_INFO
#undef HAVE_MPI_LONG_LONG_INT
#undef HAVE_PRAGMA_CRI_DUP
#undef HAVE_PRAGMA_HP_SEC_DEF
#undef HAVE_PRAGMA_WEAK
#undef HAVE_PREAD64
#define HAVE_STATUS_SET_BYTES
#define HAVE_STRERROR
#undef HAVE_SYSERRLIST
#undef HAVE_WEAK_SYMBOLS
#undef HFS
#undef HPUX
#undef INT_LT_POINTER
#undef IRIX
#undef LINUX
#define MPICH
#undef MPIHP
#undef MPILAM
#undef MPISGI
#undef MPI_OFFSET_IS_INT
#undef NEEDS_MPI_TEST
#undef NFS
#undef NO_AIO
#undef NO_FD_IN_AIOCB
#undef NO_MPI_SGI_type_is_contig
#undef PARAGON
#undef PFS
#undef PIOFS
#undef PRINT_ERR_MSG
#undef ROMIO_PVFS
#undef ROMIO_TESTFS
#undef SFS
#undef SOLARIS
#undef SPPUX
#undef SX4
#undef UFS
#undef XFS

#ifndef ROMIO_NTFS
#define ROMIO_NTFS
#endif
#ifndef HAVE_INT64
#define HAVE_INT64 1
#endif

#ifndef HAS_MPIR_ERR_SETMSG
#define HAS_MPIR_ERR_SETMSG
#endif
#ifndef MPICH
#define MPICH
#endif
#ifndef HAVE_STATUS_SET_BYTES
#define HAVE_STATUS_SET_BYTES
#endif
#ifndef USE_MPI_VERSIONS
#define USE_MPI_VERSIONS
#endif

#ifndef FORTRANDOUBLEUNDERSCORE
#define FORTRANCAPS
#endif
