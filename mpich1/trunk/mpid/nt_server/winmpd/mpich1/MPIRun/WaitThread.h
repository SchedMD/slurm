/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef WAIT_THREAD_H
#define WAIT_THREAD_H

#ifdef WSOCK2_BEFORE_WINDOWS
#include <winsock2.h>
#endif
#include <windows.h>

#ifndef CREATE_THREAD_RETRIES
#define CREATE_THREAD_RETRIES            5
#endif
#ifndef CREATE_THREAD_SLEEP_TIME
#define CREATE_THREAD_SLEEP_TIME       250
#endif

void WaitForLotsOfObjects(int nHandles, HANDLE *pHandle);

#endif
