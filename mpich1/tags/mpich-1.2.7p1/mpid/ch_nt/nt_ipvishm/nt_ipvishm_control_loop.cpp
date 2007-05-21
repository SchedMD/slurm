#include "nt_global_cpp.h"
#include <stdio.h>

int g_nNumInDone = 0;
long g_nNumConnected = 0;
HANDLE g_hAllInDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE g_hOkToPassThroughDone = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE g_hNumInDoneMutex = CreateMutex(NULL, FALSE, NULL);

HANDLE g_hControlLoopThread = NULL;
HANDLE g_hStopControlLoopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE g_hEveryoneConnectedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

bool SendAllDoneMsg(char *host, int port);

// Function name	: ControlLoopClientThread
// Description	    : 
// Return type		: void 
// Argument         : ControlLoopClientArg *arg
void ControlLoopClientThread(ControlLoopClientArg *arg)
{
	char cCmd, ack=1;
	DWORD ret_val = 0;
	int remote_iproc, query_n, i;
	SOCKET sock;
	WSAEVENT sock_event;
	char temp_host[NT_HOSTNAME_LEN];

	sock = arg->sock;
	sock_event = arg->sock_event;

	delete arg;

	// save and compare the result of ReceiveBlocking
	if ( ret_val = ReceiveBlocking(sock, sock_event, &cCmd, 1, 0) )
	{
		NT_Tcp_closesocket(sock, sock_event);
		nt_error_socket("Failure to read command from ControlLoopClient connection.\n", ret_val);
	}

	switch (cCmd)
	{
	case NT_TCP_CTRL_CMD_INIT_DATA_TO_ROOT:
		// Receive iproc, listen port, control port, hostname, exename, and pid
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&remote_iproc, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: recv remote_iproc failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[remote_iproc].listen_port, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: recv listen port failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[remote_iproc].control_port, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: recv control port failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, g_pProcTable[remote_iproc].host, NT_HOSTNAME_LEN, 0))
			nt_error_socket("ControlLoopClientThread: recv remote_host failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, g_pProcTable[remote_iproc].exename, NT_EXENAME_LEN, 0))
			nt_error_socket("ControlLoopClientThread: recv remote_exename failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[remote_iproc].pid, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: recv remote_pid failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[remote_iproc].num_nics, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: recv remote_num_nics failed.", ret_val);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[remote_iproc].nic_ip, sizeof(int)*MAX_NUM_NICS, 0))
			nt_error_socket("ControlLoopClientThread: recv remote_nic_ip[4] failed.", ret_val);
		g_pProcTable[remote_iproc].multinic = (g_pProcTable[remote_iproc].num_nics > 1) ? TRUE : FALSE;
		if (!SetEvent(g_pProcTable[remote_iproc].hValidDataEvent))
			MakeErrMsg(GetLastError(), "ControlLoopClientThread: SetEvent(hValidDataEvent[%d]) failed", remote_iproc);
		//printf("iproc: %d, listen: %d, control: %d, host: %s, exe: %s, pid: %d\n", 
		//	remote_iproc, g_pProcTable[remote_iproc].listen_port, g_pProcTable[remote_iproc].control_port,
		//	g_pProcTable[remote_iproc].host, g_pProcTable[remote_iproc].exename, g_pProcTable[remote_iproc].pid);fflush(stdout);
		InterlockedIncrement(&g_nNumConnected);
		if (g_nNumConnected == g_nNproc)
			SetEvent(g_hEveryoneConnectedEvent);
		else
		    WaitForSingleObject(g_hEveryoneConnectedEvent, INFINITE);
		// Send acknowledgement
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			nt_error_socket("ControlLoopClientThread: send ack failed.", WSAGetLastError());
		//printf("Init data to root message processed for %d\n", remote_iproc);fflush(stdout);
		break;
	case NT_TCP_CTRL_CMD_PROCESS_CONNECT_INFO:
		// Receive the rank of the process information is requested of
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&query_n, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: ReceiveBlocking query_n failed", ret_val);

		// What do I do if this information is not available yet?
		if (g_pProcTable[query_n].listen_port == 0)
		{
			if (WaitForSingleObject(g_pProcTable[query_n].hValidDataEvent, 2000*g_nNproc) != WAIT_OBJECT_0)
				LogMsg("Sending invalid information for process %d\n", query_n);
		}

		if (g_bMultinic)
		{
		    bool bSent = false;
		    for (i=0; i<g_pProcTable[query_n].num_nics; i++)
		    {
			if ((g_pProcTable[query_n].nic_ip[i] & g_nNicMask) == g_nNicNet)
			{
			    unsigned int a, b, c, d;
			    a = ((unsigned char *)(&g_pProcTable[query_n].nic_ip[i]))[0];
			    b = ((unsigned char *)(&g_pProcTable[query_n].nic_ip[i]))[1];
			    c = ((unsigned char *)(&g_pProcTable[query_n].nic_ip[i]))[2];
			    d = ((unsigned char *)(&g_pProcTable[query_n].nic_ip[i]))[3];
			    sprintf(temp_host, "%u.%u.%u.%u", a, b, c, d);
			    //printf("sending %s\n", temp_host);fflush(stdout);
			    // Send the host name for the requested process
			    if (SendBlocking(sock, temp_host, NT_HOSTNAME_LEN, 0) == SOCKET_ERROR)
				MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send temp_host %d failed", query_n);
			    bSent = true;
			    break;
			}
		    }
		    if (!bSent)
		    {
			//printf("sending default host: %s\n", g_pProcTable[query_n].host);fflush(stdout);
			if (SendBlocking(sock, g_pProcTable[query_n].host, NT_HOSTNAME_LEN, 0) == SOCKET_ERROR)
			    MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send host %d failed", query_n);
		    }
		}
		else
		{
		    //printf("sending %s\n", g_pProcTable[query_n].host);fflush(stdout);
		    // Send the host name for the requested process
		    if (SendBlocking(sock, g_pProcTable[query_n].host, NT_HOSTNAME_LEN, 0) == SOCKET_ERROR)
			MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send host %d failed", query_n);
		}
		// Send the port for the requested process
		if (SendBlocking(sock, (char*)&g_pProcTable[query_n].listen_port, sizeof(int), 0) == SOCKET_ERROR)
			MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send listen_port[%d] %d failed", query_n, g_pProcTable[query_n].listen_port);
		//printf("process connect info processed for %d\n", query_n);fflush(stdout);
		break;
	case NT_TCP_CTRL_CMD_PROCESS_INFO:
		// Receive the rank of the process information is requested of
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&query_n, sizeof(int), 0))
			nt_error_socket("ControlLoopClientThread: ReceiveBlocking query_n failed", ret_val);
		// Send the host name, executable name, and process id for the requested process
		if (SendBlocking(sock, g_pProcTable[query_n].host, NT_HOSTNAME_LEN, 0) == SOCKET_ERROR)
			MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send host %d failed", query_n);
		if (SendBlocking(sock, g_pProcTable[query_n].exename, NT_EXENAME_LEN, 0) == SOCKET_ERROR)
			MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send exename %d failed", query_n);
		if (SendBlocking(sock, (char*)&g_pProcTable[query_n].pid, sizeof(int), 0) == SOCKET_ERROR)
			MakeErrMsg(WSAGetLastError(), "ControlLoopClientThread: send process %d id %d failed", query_n, g_pProcTable[query_n].pid);
		//printf("process info processed for %d\n", query_n);fflush(stdout);
		break;
	case NT_TCP_CTRL_CMD_POST_IN_DONE:
		// Send acknowledgement
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			nt_error_socket("ControlLoopClientThread: send post_in_done ack failed.", WSAGetLastError());
		if (WaitForSingleObject(g_hNumInDoneMutex, INFINITE) != WAIT_OBJECT_0)
			nt_error_socket("ControlLoopClientThread:POST_IN_DONE: WaitForSingleObject(g_hNumInDoneMutex) failed", GetLastError());
		g_nNumInDone++;
		if (g_nNumInDone == g_nNproc)
		{
			// Send 'all in done' messages
			for (i=g_nNproc-1; i>=0; i--)
			{
				//printf("About to call SendAllDoneMsg for %d on %s at %d\n", 
				//	i, g_pProcTable[i].host, g_pProcTable[i].control_port);fflush(stdout);
				SendAllDoneMsg(g_pProcTable[i].host, g_pProcTable[i].control_port);
			}
			if (!ReleaseMutex(g_hNumInDoneMutex))
				nt_error_socket("ControlLoopClientThread:POST_IN_DONE: ReleaseMutex(g_hNumInDoneMutex) failed", GetLastError());
			if (!CloseHandle(g_hNumInDoneMutex))
				nt_error_socket("ControlLoopClientThread:POST_IN_DON: CloseHandle(g_hNumInDoneMutex) failed", GetLastError());
			NT_Tcp_closesocket(sock, sock_event);
			//printf("post in done processed\n");fflush(stdout);
			if (!SetEvent(g_hAllInDoneEvent))
				nt_error_socket("ControlLoopClientThread:POST_IN_DONE: SetEvent(g_hAllInDoneEvent) failed", GetLastError());
			return;
		}
		else
		{
			if (!ReleaseMutex(g_hNumInDoneMutex))
				nt_error_socket("ControlLoopClientThread:POST_IN_DONE: ReleaseMutex(g_hNumInDoneMutex) failed", GetLastError());
		}
		//printf("post in done processed\n");fflush(stdout);
		break;
	case NT_TCP_CTRL_CMD_ALL_IN_DONE:
		// Send acknowledgement
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			nt_error_socket("ControlLoopClientThread: send all_in_done ack failed.", WSAGetLastError());
		NT_Tcp_closesocket(sock, sock_event);
		//printf("all in done processed\n");fflush(stdout);
		if (!SetEvent(g_hOkToPassThroughDone))
			nt_error_socket("ControlLoopClientThread:ALL_IN_DONE: SetEvent(g_hOkToPassThroughDone) failed", GetLastError());
		return;
		break;
	case NT_TCP_CTRL_CMD_ABORT:
		nt_error("request to abort received", 1);
		break;
	default:
		nt_error("Invalid command received from ControlLoopClient connection.\n", cCmd);
	}

	NT_Tcp_closesocket(sock, sock_event);
}

// Function name	: ControlLoopThread
// Description	    : 
// Return type		: void 
// Argument         : HANDLE hReadyEvent
void ControlLoopThread(HANDLE hReadyEvent)
{
	SOCKET sock;
	WSAEVENT sock_event, aEvents[2];
	int error = 0;
	char host[NT_HOSTNAME_LEN];
	DWORD result;
	SOCKET temp_socket;
	WSAEVENT temp_event;
	HANDLE hThread;
	DWORD dwThreadID;

	// create a listening socket
	// The control_port field of the ProcTable is initialized to zero. Therefore the system will pick any available port when creating the socket.
	// But if the user selects to use a static port, then control_port will be set to this port number.
	error = NT_Tcp_create_bind_socket(&sock, &sock_event, g_pProcTable[g_nIproc].control_port);
	if (error)
		nt_error("ControlLoopThread: NT_Tcp_create_bind_socket failed", 1);

	// associate sock_event with sock
	if (WSAEventSelect(sock, sock_event, FD_ACCEPT) == SOCKET_ERROR)
		nt_error_socket("ControlLoopThread: WSAEventSelect(FD_ACCEPT) failed for the control socket", WSAGetLastError());

	if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
		nt_error_socket("ControlLoopThread: listen failed", WSAGetLastError());

	// get the port and local hostname for the listening socket
	error = NT_Tcp_get_sock_info(sock, host, &g_pProcTable[g_nIproc].control_port);
	if (error)
		nt_error_socket("ControlLoopThread: Unable to get host and port of listening socket", error);

	// Signal that the control port is valid
	if (!SetEvent(hReadyEvent))
		nt_error_socket("ControlLoopThread: SetEvent(hReadyEvent) failed", GetLastError());

	aEvents[0] = sock_event;
	aEvents[1] = g_hStopControlLoopEvent;
	// Loop indefinitely, waiting for remote connections or a stop signal
	while (true)
	{
		result = WSAWaitForMultipleEvents(2, aEvents, FALSE, INFINITE, FALSE);
		if ((result != WSA_WAIT_EVENT_0) && (result != WSA_WAIT_EVENT_0+1))
			nt_error("ControlLoopThread: Wait for a connect event failed", result);
		
		if (result == WSA_WAIT_EVENT_0+1)
		{
			closesocket(sock);
			CloseHandle(g_hStopControlLoopEvent);
			return;
		}

		temp_socket = accept(sock, NULL, NULL);
		if (temp_socket != INVALID_SOCKET)
		{
			ControlLoopClientArg *cArg = new ControlLoopClientArg;
			if ((temp_event = WSACreateEvent()) == WSA_INVALID_EVENT)
				nt_error_socket("ControlLoopThread: WSACreateEvent failed", WSAGetLastError());
			if (WSAEventSelect(temp_socket, temp_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
				nt_error_socket("ControlLoopThread: WSAEventSelect failed", WSAGetLastError());
			cArg->sock = temp_socket;
			cArg->sock_event = temp_event;
			for (int i=0; i<NT_CREATE_THREAD_RETRIES; i++)
			{
			    hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ControlLoopClientThread, cArg, NT_THREAD_STACK_SIZE, &dwThreadID);
			    if (hThread != NULL)
				break;
			    Sleep(NT_CREATE_THREAD_SLEEP_TIME);
			}
			if (hThread == NULL)
			{
				delete cArg;
				NT_Tcp_closesocket(temp_socket, temp_event);
				nt_error_socket("CreateThread failed in ControlLoopThread.", GetLastError());
			}
			CloseHandle(hThread);
			continue;
		}
		result = GetLastError();
		if (result == WSAEWOULDBLOCK)
		{
			WSAResetEvent(sock_event);
			WSAEventSelect(sock, sock_event, FD_ACCEPT);
		}
		else
			nt_error_socket("ControlLoopThread: accept failed", result);
	}
}

// Function name	: SendInitDataToRoot
// Description	    : 
// Return type		: bool 
bool SendInitDataToRoot()
{
	int ret_val;
	char ack, cmd;
	WSAEVENT sock_event;
	SOCKET sock;

	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		nt_error_socket("WSACreateEvent failed in SendInitDataToRoot", WSAGetLastError());
	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		nt_error_socket("socket failed in SendInitDataToRoot", WSAGetLastError());

	//printf("Connecting to root: %s %d\n", g_pszRootHostName, g_nRootPort);fflush(stdout);
	ret_val = NT_Tcp_connect(sock, g_pszRootHostName, g_nRootPort);
	if (ret_val)
		nt_error_socket("SendInitDataToRoot: NT_Tcp_connect failed", ret_val);

	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: WSAEventSelect failed", WSAGetLastError());

	cmd = NT_TCP_CTRL_CMD_INIT_DATA_TO_ROOT;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send cmd failed", WSAGetLastError());

	// Send iproc, listen_port, hostname, exename, and pid
	if (SendBlocking(sock, (char*)&g_nIproc, sizeof(int), 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send iproc failed", WSAGetLastError());
	if (SendBlocking(sock, (char*)&g_pProcTable[g_nIproc].listen_port, sizeof(int), 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send listen port failed", WSAGetLastError());
	if (SendBlocking(sock, (char*)&g_pProcTable[g_nIproc].control_port, sizeof(int), 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send control port failed", WSAGetLastError());
	if (SendBlocking(sock, g_pszHostName, NT_HOSTNAME_LEN, 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send host name failed", WSAGetLastError());
	if (SendBlocking(sock, g_pProcTable[g_nIproc].exename, NT_EXENAME_LEN, 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send exe name failed", WSAGetLastError());
	if (SendBlocking(sock, (char*)&g_pProcTable[g_nIproc].pid, sizeof(int), 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send pid failed", WSAGetLastError());
	if (SendBlocking(sock, (char*)&g_pProcTable[g_nIproc].num_nics, sizeof(int), 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send num_nics failed", WSAGetLastError());
	if (SendBlocking(sock, (char*)&g_pProcTable[g_nIproc].nic_ip, sizeof(int)*MAX_NUM_NICS, 0) == SOCKET_ERROR)
		nt_error_socket("SendInitDataToRoot: send nic_ip[4] failed", WSAGetLastError());

	// Wait for an ack to ensure the data was received
	ret_val = ReceiveBlocking(sock, sock_event, &ack, 1, 0);
	if (ret_val)
		nt_error_socket("SendInitDataToRoot: recv ack failed", WSAGetLastError());

	//printf("SendInitDataToRoot called.\n");fflush(stdout);

	NT_Tcp_closesocket(sock, sock_event);
	return true;
}

// Function name	: GetProcessConnectInfo
// Description	    : 
// Return type		: bool 
// Argument         : int iproc
bool GetProcessConnectInfo(int iproc)
{
	int ret_val;
	char cmd;
	WSAEVENT sock_event;
	SOCKET sock;

	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		nt_error_socket("WSACreateEvent failed in GetProcessConnectInfo", WSAGetLastError());
	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		nt_error_socket("socket failed in GetProcessConnectInfo", WSAGetLastError());

	//printf("Connecting to root: %s %d\n", host, port);fflush(stdout);
	ret_val = NT_Tcp_connect(sock, g_pszRootHostName, g_nRootPort);
	if (ret_val)
		nt_error_socket("GetProcessConnectInfo: NT_Tcp_connect failed", ret_val);

	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		nt_error_socket("GetProcessConnectInfo: WSAEventSelect failed", WSAGetLastError());

	cmd = NT_TCP_CTRL_CMD_PROCESS_CONNECT_INFO;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		nt_error_socket("GetProcessConnectInfo: send cmd failed", WSAGetLastError());

	// Send the rank of the process information is requested of
	if (SendBlocking(sock, (char*)&iproc, sizeof(int), 0) == SOCKET_ERROR)
		MakeErrMsg(WSAGetLastError(), "GetProcessConnectInfo: send iproc(%d) to root failed", iproc);
	// Receive the host name and port for the requested process
	if (ret_val = ReceiveBlocking(sock, sock_event, g_pProcTable[iproc].host, NT_HOSTNAME_LEN, 0))
		MakeErrMsg(ret_val, "GetProcessConnectInfo: receive host name %d failed", iproc);
	if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[iproc].listen_port, sizeof(int), 0))
		MakeErrMsg(ret_val, "GetProcessConnectInfo: receive listen_port %d failed", iproc);

	//printf("GetProcessConnectInfo called\n");fflush(stdout);
	NT_Tcp_closesocket(sock, sock_event);

	if (g_pProcTable[iproc].listen_port < 1)
		return false;
	return true;
}

// Function name	: GetProcessInfo
// Description	    : 
// Return type		: bool 
// Argument         : int iproc
bool GetProcessInfo(int iproc)
{
	int ret_val;
	char cmd;
	WSAEVENT sock_event;
	SOCKET sock;

	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		nt_error_socket("WSACreateEvent failed in GetProcessInfo", WSAGetLastError());
	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		nt_error_socket("socket failed in GetProcessInfo", WSAGetLastError());

	//printf("Connecting to root: %s %d\n", host, port);fflush(stdout);
	ret_val = NT_Tcp_connect(sock, g_pszRootHostName, g_nRootPort);
	if (ret_val)
		nt_error_socket("GetProcessInfo: NT_Tcp_connect failed", ret_val);

	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		nt_error_socket("GetProcessInfo: WSAEventSelect failed", WSAGetLastError());

	cmd = NT_TCP_CTRL_CMD_PROCESS_INFO;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		nt_error_socket("GetProcessInfo: send cmd failed", WSAGetLastError());

	// Send the rank of the process information is requested of
	if (SendBlocking(sock, (char*)&iproc, sizeof(int), 0) == SOCKET_ERROR)
		MakeErrMsg(WSAGetLastError(), "GetProcessInfo: SendBlocking iproc(%d) failed", iproc);
	// Receive the host name, executable name, and process id for the requested process
	if (ret_val = ReceiveBlocking(sock, sock_event, g_pProcTable[iproc].host, NT_HOSTNAME_LEN, 0))
		MakeErrMsg(ret_val, "GetProcessInfo: receive host %d failed", iproc);
	if (ret_val = ReceiveBlocking(sock, sock_event, g_pProcTable[iproc].exename, NT_EXENAME_LEN, 0))
		MakeErrMsg(ret_val, "GetProcessInfo: receive exename %d failed", iproc);
	if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&g_pProcTable[iproc].pid, sizeof(int), 0))
		MakeErrMsg(ret_val, "GetProcessInfo: receive process %d pid failed", iproc);

	//printf("GetProcessInfo called\n");fflush(stdout);
	NT_Tcp_closesocket(sock, sock_event);
	return true;
}

// Function name	: SendInDoneMsg
// Description	    : 
// Return type		: bool 
bool SendInDoneMsg()
{
	int ret_val;
	char ack, cmd;
	WSAEVENT sock_event;
	SOCKET sock;

	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		nt_error_socket("WSACreateEvent failed in SendInDoneMsg", WSAGetLastError());
	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		nt_error_socket("socket failed in SendInDoneMsg", WSAGetLastError());

	ret_val = NT_Tcp_connect(sock, g_pszRootHostName, g_nRootPort);
	if (ret_val)
		nt_error_socket("SendInDoneMsg: NT_Tcp_connect failed", ret_val);

	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		nt_error_socket("SendInDoneMsg: WSAEventSelect failed", WSAGetLastError());

	cmd = NT_TCP_CTRL_CMD_POST_IN_DONE;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		nt_error_socket("SendInDoneMsg: send cmd failed", WSAGetLastError());

	ret_val = ReceiveBlocking(sock, sock_event, &ack, 1, 0);
	if (ret_val)
		nt_error_socket("SendInDoneMsg: receive ack failed", ret_val);

	NT_Tcp_closesocket(sock, sock_event);
	return true;
}

// Function name	: SendAllDoneMsg
// Description	    : 
// Return type		: bool 
// Argument         : char *host
// Argument         : int port
bool SendAllDoneMsg(char *host, int port)
{
	int ret_val;
	char ack, cmd;
	WSAEVENT sock_event;
	SOCKET sock;

	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		nt_error_socket("WSACreateEvent failed in SendAllDoneMsg", WSAGetLastError());
	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		nt_error_socket("socket failed in SendAllDoneMsg", WSAGetLastError());

	ret_val = NT_Tcp_connect(sock, host, port);
	if (ret_val)
		nt_error_socket("SendAllDoneMsg: NT_Tcp_connect failed", ret_val);

	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		nt_error_socket("SendAllDoneMsg: WSAEventSelect failed", WSAGetLastError());

	cmd = NT_TCP_CTRL_CMD_ALL_IN_DONE;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		nt_error_socket("SendAllDoneMsg: send cmd failed", WSAGetLastError());

	ret_val = ReceiveBlocking(sock, sock_event, &ack, 1, 0);
	if (ret_val)
		nt_error_socket("SendAllDoneMsg: receive ack failed", ret_val);

	NT_Tcp_closesocket(sock, sock_event);
	return true;
}
