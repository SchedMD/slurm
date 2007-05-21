/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   ad_panfs_open.c
 *
 *   Copyright (C) 2001 University of Chicago.
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_panfs.h"
#include <string.h>
#include <pan_fs_client_cw_mode.h>
#define TEMP_BUFFER_SIZE 64

void ADIOI_PANFS_Open(ADIO_File fd, int *error_code)
{
    char* value;
    int perm, old_mask, amode, flag;
    static char myname[] = "ADIOI_PANFS_OPEN";

    if (fd->perm == ADIO_PERM_NULL) {
        old_mask = umask(022);
        umask(old_mask);
        perm = ~old_mask & 0666;
    }
    else perm = fd->perm;

    amode = 0;
    if (fd->access_mode & ADIO_CREATE)
    {
        pan_fs_client_layout_agg_type_t layout_type = PAN_FS_CLIENT_LAYOUT_TYPE__DEFAULT;
        unsigned long int layout_stripe_unit = 0;
        unsigned long int layout_parity_stripe_width = 0;
        unsigned long int layout_parity_stripe_depth = 0; 
        unsigned long int layout_total_num_comps = 0;
        pan_fs_client_layout_visit_t layout_visit_policy  = PAN_FS_CLIENT_LAYOUT_VISIT__ROUND_ROBIN;

        *error_code = MPI_SUCCESS;
        value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));
        MPI_Info_get(fd->info, "panfs_layout_type", MPI_MAX_INFO_VAL, 
                 value, &flag);
        if (flag) {
            layout_type = strtoul(value,NULL,10);
        }
        MPI_Info_get(fd->info, "panfs_layout_stripe_unit", MPI_MAX_INFO_VAL, 
                 value, &flag);
        if (flag) {
            layout_stripe_unit = strtoul(value,NULL,10);
        }
        MPI_Info_get(fd->info, "panfs_layout_total_num_comps", MPI_MAX_INFO_VAL, 
                 value, &flag);
        if (flag) {
            layout_total_num_comps = strtoul(value,NULL,10);
        }
        MPI_Info_get(fd->info, "panfs_layout_parity_stripe_width", MPI_MAX_INFO_VAL, 
                 value, &flag);
        if (flag) {
            layout_parity_stripe_width = strtoul(value,NULL,10);
        }
        MPI_Info_get(fd->info, "panfs_layout_parity_stripe_depth", MPI_MAX_INFO_VAL, 
                 value, &flag);
        if (flag) {
            layout_parity_stripe_depth = strtoul(value,NULL,10);
        }
        MPI_Info_get(fd->info, "panfs_layout_visit_policy", MPI_MAX_INFO_VAL, 
                 value, &flag);
        if (flag) {
            layout_visit_policy = strtoul(value,NULL,10);
        }
        ADIOI_Free(value);

	    amode = amode | O_CREAT;
        /* Check for valid set of hints */
        if ((layout_type < PAN_FS_CLIENT_LAYOUT_TYPE__DEFAULT) ||
           (layout_type > PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE))
        {
            FPRINTF(stderr, "%s: panfs_layout_type is not a valid value: %u.\n", myname, layout_type);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        if ((layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID0) &&
           ((layout_stripe_unit == 0) || (layout_total_num_comps == 0)))
        {
            if(layout_stripe_unit == 0)
            {
                FPRINTF(stderr, "%s: MPI_Info does not contain the panfs_layout_stripe_unit hint which is necessary to specify a valid RAID0 layout to the PAN_FS_CLIENT_LAYOUT_CREATE_FILE ioctl.\n", myname);
            }
            if(layout_total_num_comps == 0)
            {
                FPRINTF(stderr, "%s: MPI_Info does not contain the panfs_layout_total_num_comps hint which is necessary to specify a valid RAID0 layout to the PAN_FS_CLIENT_LAYOUT_CREATE_FILE ioctl.\n", myname);
            }
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        if (layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE)
        {
            if ((layout_stripe_unit == 0) ||
               (layout_parity_stripe_width == 0) ||
               (layout_parity_stripe_depth == 0) ||
               (layout_total_num_comps == 0))
            {
                if(layout_stripe_unit == 0)
                {
                    FPRINTF(stderr, "%s: MPI_Info does not contain the panfs_layout_stripe_unit hint which is necessary to specify a valid RAID5 parity stripe layout to the PAN_FS_CLIENT_LAYOUT_CREATE_FILE ioctl.\n", myname);
                }
                if(layout_total_num_comps == 0)
                {
                    FPRINTF(stderr, "%s: MPI_Info does not contain the panfs_layout_total_num_comps hint which is necessary to specify a valid RAID5 parity stripe layout to the PAN_FS_CLIENT_LAYOUT_CREATE_FILE ioctl.\n", myname);
                }
                if(layout_parity_stripe_width == 0)
                {
                    FPRINTF(stderr, "%s: MPI_Info does not contain the panfs_layout_parity_stripe_width hint which is necessary to specify a valid RAID5 parity stripe layout to the PAN_FS_CLIENT_LAYOUT_CREATE_FILE ioctl.\n", myname);
                }
                if(layout_parity_stripe_depth == 0)
                {
                    FPRINTF(stderr, "%s: MPI_Info does not contain the panfs_layout_parity_stripe_depth hint which is necessary to specify a valid RAID5 parity stripe layout to the PAN_FS_CLIENT_LAYOUT_CREATE_FILE ioctl.\n", myname);
                }
                MPI_Abort(MPI_COMM_WORLD, 1);
           }
           if ((layout_visit_policy < PAN_FS_CLIENT_LAYOUT_VISIT__ROUND_ROBIN) ||
              (layout_visit_policy > PAN_FS_CLIENT_LAYOUT_VISIT__ROUND_ROBIN_WITH_HASHED_OFFSET))
           {
                FPRINTF(stderr, "%s: panfs_layout_visit_policy is not a valid value: %u.\n", myname, layout_visit_policy);
                MPI_Abort(MPI_COMM_WORLD, 1);
           }
        }
        if ((layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID0) ||
	    (layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE))
	{
            int myrank;

            MPI_Comm_rank(fd->comm, &myrank);
            if (myrank == 0) {
                pan_fs_client_layout_create_args_t file_create_args;    
                int fd_dir;
                char* slash;
                struct stat stat_buf;
                int err;
                char *value, *path, *file_name_ptr;

                /* Check that the file does not exist before
                 * trying to create it.  The ioctl itself should
                 * be able to handle this condition.  Currently,
                 * the ioctl will return successfully if the file
                 * has been previously created.  Filed bug 33862
                 * to track the problem.
                 */
                err = stat(fd->filename,&stat_buf);
                if((err == -1) && (errno != ENOENT))
                {
                    FPRINTF(stderr,"%s: Unexpected I/O Error calling stat() on PanFS file: %s.\n", myname, strerror(errno));
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
                else if (err == 0)
                {
                    FPRINTF(stderr,"%s: Cannot create PanFS file with ioctl when file already exists.\n", myname);
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
                else
                {
                    /* (err == -1) && (errno == ENOENT) */
                    /* File does not exist */
                    path = ADIOI_Strdup(fd->filename);
                    slash = strrchr(path, '/');
                    if (!slash)
                        ADIOI_Strncpy(path, ".", 2);
                    else {
                        if (slash == path) 
                            *(path + 1) = '\0';
                        else *slash = '\0';
                    }

                    /* create PanFS object */
                    bzero(&file_create_args,sizeof(pan_fs_client_layout_create_args_t)); 
                    /* open directory */
                    fd_dir = open(path, O_RDONLY);
                    if (fd_dir < 0) {
                        FPRINTF(stderr, "%s: I/O Error opening parent directory to create PanFS file using ioctl: %s.\n", myname, strerror(errno));
                        MPI_Abort(MPI_COMM_WORLD, 1);
                    }
                    else
                    {
                        char *file_name_ptr = fd->filename;
                        slash = strrchr(fd->filename, '/');
                        if (slash)
                        {
                            file_name_ptr = slash + 1;
                        }
                        /* create file in the directory */
                        file_create_args.mode = perm;
                        file_create_args.version = PAN_FS_CLIENT_LAYOUT_VERSION;
                        file_create_args.flags = PAN_FS_CLIENT_LAYOUT_CREATE_F__NONE;
                        ADIOI_Strncpy(file_create_args.filename, file_name_ptr, strlen(fd->filename)+1); 
                        file_create_args.layout.agg_type = layout_type;
                        file_create_args.layout.layout_is_valid = 1;
                        if(layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE)
                        {
                            file_create_args.layout.u.raid1_5_parity_stripe.total_num_comps = layout_total_num_comps;
                            file_create_args.layout.u.raid1_5_parity_stripe.parity_stripe_width   = layout_parity_stripe_width;
                            file_create_args.layout.u.raid1_5_parity_stripe.parity_stripe_depth   = layout_parity_stripe_depth;
                            file_create_args.layout.u.raid1_5_parity_stripe.stripe_unit     = layout_stripe_unit;
                            file_create_args.layout.u.raid1_5_parity_stripe.layout_visit_policy   = layout_visit_policy;
                        }
                        else if(layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID0)
                        {
                            file_create_args.layout.u.raid0.total_num_comps = layout_total_num_comps;
                            file_create_args.layout.u.raid0.stripe_unit     = layout_stripe_unit;
                        }
                        err = ioctl(fd_dir, PAN_FS_CLIENT_LAYOUT_CREATE_FILE, &file_create_args);
                        if (err < 0) {
                            FPRINTF(stderr, "%s: I/O Error doing ioctl on parent directory to create PanFS file using ioctl: %s.\n", myname, strerror(errno));
                            MPI_Abort(MPI_COMM_WORLD, 1);
                        }
                        err = close(fd_dir);
                    }
                    ADIOI_Free(path);
                }
            }
            MPI_Barrier(fd->comm);
        }
    }
    if (fd->access_mode & ADIO_RDONLY)
	amode = amode | O_RDONLY;
    if (fd->access_mode & ADIO_WRONLY)
	amode = amode | O_WRONLY;
    if (fd->access_mode & ADIO_RDWR)
	amode = amode | O_RDWR;
    if (fd->access_mode & ADIO_EXCL)
	amode = amode | O_EXCL;

	value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));
	MPI_Info_get(fd->info, "panfs_concurrent_write", MPI_MAX_INFO_VAL, 
		     value, &flag);
	if (flag) {
        unsigned long int concurrent_write = strtoul(value,NULL,10);
        if(concurrent_write == 1)
        {
            amode = amode | O_CONCURRENT_WRITE;
        }
	}
	ADIOI_Free(value);

    fd->fd_sys = open(fd->filename, amode, perm);
    fd->fd_direct = -1;

    if (fd->fd_sys != -1)
    {
        int rc;
        char temp_buffer[TEMP_BUFFER_SIZE];
        pan_fs_client_layout_query_args_t file_query_args;
        bzero(&file_query_args,sizeof(pan_fs_client_layout_query_args_t));
        file_query_args.version = PAN_FS_CLIENT_LAYOUT_VERSION;
        rc = ioctl(fd->fd_sys, PAN_FS_CLIENT_LAYOUT_QUERY_FILE, &file_query_args);
        if (rc < 0)
        {
            /* Error - set layout type to unknown */
	        MPI_Info_set(fd->info, "panfs_layout_type", "PAN_FS_CLIENT_LAYOUT_TYPE__INVALID");
        }
        else 
        {
            ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.agg_type);
            MPI_Info_set(fd->info, "panfs_layout_type", temp_buffer);
            if (file_query_args.layout.layout_is_valid == 1)
            {
                switch (file_query_args.layout.agg_type)
                {
                    case PAN_FS_CLIENT_LAYOUT_TYPE__RAID0:
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid0.stripe_unit);
                        MPI_Info_set(fd->info, "panfs_layout_stripe_unit", temp_buffer);
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid0.total_num_comps);
                        MPI_Info_set(fd->info, "panfs_layout_total_num_comps", temp_buffer);
                        break;
                    case PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE:
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid1_5_parity_stripe.stripe_unit);
                        MPI_Info_set(fd->info, "panfs_layout_stripe_unit", temp_buffer);
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid1_5_parity_stripe.parity_stripe_width);
                        MPI_Info_set(fd->info, "panfs_layout_parity_stripe_width", temp_buffer);
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid1_5_parity_stripe.parity_stripe_depth);
                        MPI_Info_set(fd->info, "panfs_layout_parity_stripe_depth", temp_buffer);
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid1_5_parity_stripe.total_num_comps);
                        MPI_Info_set(fd->info, "panfs_layout_total_num_comps", temp_buffer);
                        ADIOI_Snprintf(temp_buffer,TEMP_BUFFER_SIZE,"%u",file_query_args.layout.u.raid1_5_parity_stripe.layout_visit_policy);
                        MPI_Info_set(fd->info, "panfs_layout_visit_policy", temp_buffer);
                        break;
                }
            }
        }
    }

    if ((fd->fd_sys != -1) && (fd->access_mode & ADIO_APPEND))
	fd->fp_ind = fd->fp_sys_posn = lseek(fd->fd_sys, 0, SEEK_END);

    if (fd->fd_sys == -1) {
	if (errno == ENAMETOOLONG)
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_BAD_FILE,
					       "**filenamelong",
					       "**filenamelong %s %d",
					       fd->filename,
					       strlen(fd->filename));
	else if (errno == ENOENT)
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_NO_SUCH_FILE,
					       "**filenoexist",
					       "**filenoexist %s",
					       fd->filename);
	else if (errno == ENOTDIR || errno == ELOOP)
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE,
					       myname, __LINE__,
					       MPI_ERR_BAD_FILE,
					       "**filenamedir",
					       "**filenamedir %s",
					       fd->filename);
	else if (errno == EACCES) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_ACCESS,
					       "**fileaccess",
					       "**fileaccess %s", 
					       fd->filename );
	}
	else if (errno == EROFS) {
	    /* Read only file or file system and write access requested */
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_READ_ONLY,
					       "**ioneedrd", 0 );
	}
	else {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	}
    }
    else *error_code = MPI_SUCCESS;
}
