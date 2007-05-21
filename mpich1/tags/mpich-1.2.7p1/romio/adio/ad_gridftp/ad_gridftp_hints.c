/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *   Copyright (C) 2003 University of Chicago, Ohio Supercomputer Center. 
 *   See COPYRIGHT notice in top-level directory.
 */

/* 

Valid hints for ftp:// and gsiftp:// URLs (aside from the std. ones):

  ftp_control_mode   extended|block|stream|compressed
                     (default extended for gsiftp:// URLs and stream for ftp:// URLs)

  parallelism        integer number of simultaneous threads connecting to
                     ftp server (default 1)

  striped_ftp        true|false or enable|disable; enables gsiftp striped data transfer

  tcp_buffer         integer size of tcp stream buffers in bytes

  transfer_type      ascii or binary (default binary)  

These *must* be specified at open time currently.
*/

#include "ad_gridftp.h"
#include "adioi.h"

void ADIOI_GRIDFTP_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    
    if (!(fd->info))
	{
	    if ( users_info==MPI_INFO_NULL )
		{
		    /* This must be part of the open call. */ 
		    MPI_Info_create(&(fd->info));
		}
	    else
		{
		    MPI_Info_dup(users_info,&(fd->info));
		}
	}
    else
	{
	    int i,nkeys,valuelen,flag;
	    char key[MPI_MAX_INFO_KEY], value[MPI_MAX_INFO_VAL];
	    
	    if ( users_info!=MPI_INFO_NULL )
		{
		    MPI_Info_get_nkeys(users_info,&nkeys);
		    for (i=0;i<nkeys;i++)
			{
			    MPI_Info_get_nthkey(users_info,i,key);
			    MPI_Info_get_valuelen(users_info,key,&valuelen,&flag);
			    if (flag)
				{
				    MPI_Info_get(users_info,key,valuelen,value,&flag);
				    if (flag) MPI_Info_set(fd->info,key,value);
				}
			}
		}
	}
    
    /* let the generic ROMIO and MPI-I/O stuff happen... */
    ADIOI_GEN_SetInfo(fd, users_info, error_code); 
}
