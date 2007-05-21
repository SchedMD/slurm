/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef CONNECT_TO_HOST_H
#define CONNECT_TO_HOST_H

#include <winsock2.h>
#include <windows.h>

bool ConnectToHost(const char *host, int port, char *pwd, SOCKET *psock, bool fast = false);

#endif
