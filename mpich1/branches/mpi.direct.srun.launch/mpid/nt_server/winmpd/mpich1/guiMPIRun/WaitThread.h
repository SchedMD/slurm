/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef WAIT_THREAD_H
#define WAIT_THREAD_H

#include <winsock2.h>
#include <windows.h>

void WaitForLotsOfObjects(int nHandles, HANDLE *pHandle);

#endif
