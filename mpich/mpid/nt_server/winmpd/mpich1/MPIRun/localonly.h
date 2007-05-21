/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef LOCALONLY_H
#define LOCALONLY_H

#include "MPIRun.h"

void RunLocal(bool bDoSMP);
bool ReadMPDRegistry(char *name, char *value, DWORD *length = NULL);

#endif
