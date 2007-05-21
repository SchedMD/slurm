/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPDUTIL_H
#define MPDUTIL_H

#include <winsock2.h>
#include <windows.h>

#define CREATE_THREAD_RETRIES       5
#define CREATE_THREAD_SLEEP_TIME  250

int easy_socket_init();
int easy_socket_finalize();
int easy_create(SOCKET *sock, int port=0, unsigned long addr=INADDR_ANY);
SOCKET easy_accept(SOCKET sock);
int easy_connect(SOCKET sock, char *host, int port);
int easy_connect_quick(SOCKET sock, char *host, int port);
int easy_connect_timeout(SOCKET sock, char *host, int port, int seconds);
int easy_closesocket(SOCKET sock);
int easy_get_sock_info(SOCKET sock, char *name, int *port);
int easy_get_sock_info_ip(SOCKET sock, char *ipstr, int *port);
int easy_get_ip_string(char *host, char *ipstr);
int easy_get_ip_string(char *ipstring);
int easy_get_ip(unsigned long *ip);
int easy_send(SOCKET sock, char *buffer, int length);
int easy_receive(SOCKET sock, char *buffer, int length);
int easy_receive_some(SOCKET sock, char *buffer, int len);
int easy_receive_timeout(SOCKET sock, char *buffer, int len, int timeout);
void MakeLoopAsync(SOCKET *pRead, SOCKET *pWrite);
bool ReadStringMax(SOCKET sock, char *str, int max);
bool ReadStringMaxTimeout(SOCKET sock, char *str, int max, int timeout);
bool ReadStringTimeout(SOCKET sock, char *str, int timeout);
bool ReadString(SOCKET sock, char *str);
int WriteString(SOCKET sock, char *str);

int ConnectToMPD(const char *host, int port, const char *phrase, SOCKET *psock);
int ConnectToMPDquick(const char *host, int port, const char *inphrase, SOCKET *psock);
int ConnectToMPDReport(const char *host, int port, const char *phrase, SOCKET *psock, char *err_msg);
int ConnectToMPDquickReport(const char *host, int port, const char *inphrase, SOCKET *psock, char *err_msg);
void MakeLoop(SOCKET *psockRead, SOCKET *psockWrite);

char * EncodePassword(char *pwd);
void DecodePassword(char *pwd);

#define TRANSFER_BUFFER_SIZE 20*1024
void GetFile(int sock, char *pszInputStr);
bool PutFile(int sock, char *pszInputStr);

bool TryCreateDir(char *pszFileName, char *pszError);

bool UpdateMPD(const char *pszHost, const char *pszAccount, const char *pszPassword, int nPort, const char *pszPhrase, const char *pszFileName, char *pszError, int nErrLen);
bool UpdateMPICH(const char *pszHost, const char *pszAccount, const char *pszPassword, int nPort, const char *pszPhrase, const char *pszFileName, const char *pszFileNamed, char *pszError, int nErrLen);

void dbg_printf(char *str, ...);
void dbg_printf_color(unsigned short color, char *str, ...);
void warning_printf(char *str, ...);
void err_printf(char *str, ...);
bool SetDbgRedirection(char *filename);
void CancelDbgRedirection();

unsigned int mpd_version_string_to_int(char *version_str);
void mpd_version_int_to_string(unsigned int n, char *str);

#if defined(__cplusplus)
#include "qvs.h"
#endif

#endif
