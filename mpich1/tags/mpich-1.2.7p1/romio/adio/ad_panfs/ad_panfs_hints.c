/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   ad_panfs_hints.c
 *
 *   Copyright (C) 2001 University of Chicago.
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_panfs.h"
#include <pan_fs_client_cw_mode.h>

void ADIOI_PANFS_SetInfo(ADIO_File fd, MPI_Info users_info, int *error_code)
{
    static char myname[] = "ADIOI_PANFS_SETINFO";
    char* value;
    int flag, tmp_val = -1;
    unsigned long int concurrent_write = 0; 
    pan_fs_client_layout_agg_type_t layout_type = PAN_FS_CLIENT_LAYOUT_TYPE__DEFAULT;
    unsigned long int layout_stripe_unit = 0;
    unsigned long int layout_parity_stripe_width = 0;
    unsigned long int layout_parity_stripe_depth = 0; 
    unsigned long int layout_total_num_comps = 0;
    pan_fs_client_layout_visit_t layout_visit_policy  = PAN_FS_CLIENT_LAYOUT_VISIT__ROUND_ROBIN;
    int gen_error_code;

    *error_code = MPI_SUCCESS;

    if (fd->info == MPI_INFO_NULL) {
	    /* This must be part of the open call. can set striping parameters 
         * if necessary. 
         */ 
	    MPI_Info_create(&(fd->info));

        /* has user specified striping parameters 
               and do they have the same value on all processes? */
        if (users_info != MPI_INFO_NULL) {
	        value = (char *) ADIOI_Malloc((MPI_MAX_INFO_VAL+1)*sizeof(char));

            MPI_Info_get(users_info, "panfs_concurrent_write", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag) {
                concurrent_write = strtoul(value,NULL,10);
                tmp_val = concurrent_write;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != concurrent_write) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_concurrent_write\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_concurrent_write", value); 
            }

            MPI_Info_get(users_info, "panfs_layout_type", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag) {
                layout_type = strtoul(value,NULL,10);
                tmp_val = layout_type;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != layout_type) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_layout_type\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_layout_type", value); 
            }

            MPI_Info_get(users_info, "panfs_layout_stripe_unit", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag) {
                layout_stripe_unit = strtoul(value,NULL,10);
                tmp_val = layout_stripe_unit;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != layout_stripe_unit) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_layout_stripe_unit\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_layout_stripe_unit", value); 
            }

            MPI_Info_get(users_info, "panfs_layout_parity_stripe_width", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag && (layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE)) {
                layout_parity_stripe_width = strtoul(value,NULL,10);
                tmp_val = layout_parity_stripe_width;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != layout_parity_stripe_width) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_layout_parity_stripe_width\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_layout_parity_stripe_width", value); 
            }

            MPI_Info_get(users_info, "panfs_layout_parity_stripe_depth", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag && (layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE)) {
                layout_parity_stripe_depth = strtoul(value,NULL,10);
                tmp_val = layout_parity_stripe_depth;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != layout_parity_stripe_depth) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_layout_parity_stripe_depth\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_layout_parity_stripe_depth", value); 
            }

            MPI_Info_get(users_info, "panfs_layout_total_num_comps", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag) {
                layout_total_num_comps = strtoul(value,NULL,10);
                tmp_val = layout_total_num_comps;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != layout_total_num_comps) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_layout_total_num_comps\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_layout_total_num_comps", value); 
            }

            MPI_Info_get(users_info, "panfs_layout_visit_policy", MPI_MAX_INFO_VAL, 
                 value, &flag);
            if (flag && (layout_type == PAN_FS_CLIENT_LAYOUT_TYPE__RAID1_5_PARITY_STRIPE)) {
                layout_visit_policy = strtoul(value,NULL,10);
                tmp_val = layout_visit_policy;
                MPI_Bcast(&tmp_val, 1, MPI_INT, 0, fd->comm);
                if (tmp_val != layout_visit_policy) {
                    FPRINTF(stderr, "ADIOI_PANFS_SetInfo: the value for key \"panfs_layout_visit_policy\" must be the same on all processes\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
	            MPI_Info_set(fd->info, "panfs_layout_visit_policy", value); 
            }

	        ADIOI_Free(value);

        }
    }

    ADIOI_GEN_SetInfo(fd, users_info, &gen_error_code); 
    /* If this function is successful, use the error code returned from ADIOI_GEN_SetInfo
     * otherwise use the error_code generated by this function
     */
    if(*error_code == MPI_SUCCESS)
    {
        *error_code = gen_error_code;
    }
}
