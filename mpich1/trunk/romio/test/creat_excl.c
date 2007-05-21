/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* 
 * the new defered open code made some changes to the way we manage CREAT|EXCL,
 * so test out that code path */

struct options {
	char *fname;
	int verbose;
	int do_aggregation;
};
typedef struct options options;


void handle_error(int errcode, char *str) 
{
	char msg[MPI_MAX_ERROR_STRING];
	int resultlen;
	MPI_Error_string(errcode, msg, &resultlen);
	fprintf(stderr, "%s: %s\n", str, msg);
	MPI_Abort(MPI_COMM_WORLD, 1);
}

void parse_args(int argc, char ** argv, int rank, options *opts) 
{
	int i, len=0;
	if (rank == 0) {
		i = 1;
		while (i < argc) {
			if (strcmp("-fname", argv[i]) == 0) {
				len = strlen(argv[i+1]);
				opts->fname = (char *) malloc(len + 1);
				strcpy(opts->fname, argv[i+1]);
				i+=2;/* option and argument */
				continue;
			}
			if (strcmp("-aggregate", argv[i]) == 0) {
				opts->do_aggregation = 1;
				i++;
				continue;
			}
			if (strcmp("-verbose", argv[i]) == 0) {
				opts->verbose = 1;
				i++;
				continue;
			}
		}
		if (opts->fname == NULL) { /* didn't get a filename */
			fprintf(stderr, "Usage: %s -fname filename [-aggregate] [-verbose]\n", argv[0]);
			MPI_Abort(MPI_COMM_WORLD, 1);
		}
		MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Bcast(opts->fname, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
		MPI_Bcast(&(opts->do_aggregation), 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Bcast(&(opts->verbose), 1, MPI_INT, 0, MPI_COMM_WORLD);
	} else {
		MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
		opts->fname = (char *) malloc(len + 1);
		MPI_Bcast(opts->fname, len+1, MPI_CHAR, 0, MPI_COMM_WORLD);
		MPI_Bcast(&(opts->do_aggregation), 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Bcast(&(opts->verbose), 1, MPI_INT, 0, MPI_COMM_WORLD);
	}

}

int main(int argc, char ** argv) {
	int nprocs, mynod, errcode;
	options my_options = {NULL, 0, 0};
	MPI_File fh;
	MPI_Status status;
	MPI_Info  info;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &mynod);

	parse_args(argc, argv, mynod, &my_options);

	if (my_options.do_aggregation) {
		MPI_Info_create(&info);
		MPI_Info_set(info, "romio_no_indep_rw", "true");
		MPI_Info_set(info, "cb_config_list", "leela.mcs.anl.gov:1");
	} else {
		info = MPI_INFO_NULL;
	}

	/* create the file w/o EXCL: this must not fail */
	errcode = MPI_File_open(MPI_COMM_WORLD, my_options.fname,
			MPI_MODE_CREATE|MPI_MODE_RDWR, info, &fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_open");
	}

	errcode = MPI_File_close(&fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_close");
	}

	/* now try to open w/ CREAT|EXCL: this must fail */
	errcode = MPI_File_open(MPI_COMM_WORLD, my_options.fname,
			MPI_MODE_CREATE|MPI_MODE_EXCL|MPI_MODE_RDWR, info, &fh);
	if (errcode == MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_open: expected an error: got");
	}

	/* ignore the error: File_delete is not aggregator-aware */
	MPI_File_delete(my_options.fname, info);

	/* this must succeed: the file no longer exists */
	errcode = MPI_File_open(MPI_COMM_WORLD, my_options.fname,
			MPI_MODE_CREATE|MPI_MODE_EXCL|MPI_MODE_RDWR, info, &fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_open");
	}

	errcode = MPI_File_close(&fh);
	if (errcode != MPI_SUCCESS) {
		handle_error(errcode, "MPI_File_close");
	}

	if (mynod == 0) {
		printf(" No Errors\n");
	}

	MPI_Finalize();
	return 0;
}
