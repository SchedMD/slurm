/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */


#ifndef ADIO_PROTO
#define ADIO_PROTO

#ifdef ROMIO_NFS
extern struct ADIOI_Fns_struct ADIO_NFS_operations;
/* prototypes are in adio/ad_nfs/ad_nfs.h */
#endif

#ifdef ROMIO_PANFS
extern struct ADIOI_Fns_struct ADIO_PANFS_operations;
/* prototypes are in adio/ad_panfs/ad_panfs.h */
#endif

#ifdef ROMIO_PFS
extern struct ADIOI_Fns_struct ADIO_PFS_operations;
/* prototypes are in adio/ad_pfs/ad_pfs.h */
#endif

#ifdef ROMIO_PIOFS
extern struct ADIOI_Fns_struct ADIO_PIOFS_operations;
/* prototypes are in adio/ad_piofs/ad_piofs.h */
#endif

#ifdef ROMIO_UFS
extern struct ADIOI_Fns_struct ADIO_UFS_operations;
/* prototypes are in adio/ad_ufs/ad_ufs.h */
#endif

#ifdef ROMIO_HFS
extern struct ADIOI_Fns_struct ADIO_HFS_operations;
/* prototypes are in adio/ad_hfs/ad_hfs.h */
#endif

#ifdef ROMIO_XFS
extern struct ADIOI_Fns_struct ADIO_XFS_operations;
/* prototypes are in adio/ad_xfs/ad_xfs.h */
#endif

#ifdef ROMIO_SFS
extern struct ADIOI_Fns_struct ADIO_SFS_operations;
/* prototypes are in adio/ad_sfs/ad_sfs.h */
#endif

#ifdef ROMIO_NTFS
extern struct ADIOI_Fns_struct ADIO_NTFS_operations;
/* prototypes are in adio/ad_ntfs/ad_ntfs.h */
#endif

#ifdef ROMIO_PVFS
extern struct ADIOI_Fns_struct ADIO_PVFS_operations;
/* prototypes are in adio/ad_pvfs/ad_pvfs.h */
#endif

#ifdef ROMIO_PVFS2
extern struct ADIOI_Fns_struct ADIO_PVFS2_operations;
/* prototypes are in adio/ad_pvfs2/ad_pvfs2.h */
#endif

#ifdef ROMIO_TESTFS
extern struct ADIOI_Fns_struct ADIO_TESTFS_operations;
/* prototypes are in adio/ad_testfs/ad_testfs.h */
#endif

#ifdef ROMIO_GRIDFTP
/* prototypes are in adio/ad_gridftp/ad_gridftp.h */
extern struct ADIOI_Fns_struct ADIO_GRIDFTP_operations;
#endif

#endif
