// Database.cpp: implementation of the Database class.
//
//////////////////////////////////////////////////////////////////////

#include "Database.h"
#include <stdio.h>

// Function name	: Database::Database
// Description	    : 
// Return type		: 
Database::Database()
{
	strcpy(m_pszServerHost, "127.0.0.1");
	m_nServerPort = 0;
	strcpy(m_pszID, "MPICH");
}

// Function name	: Database::~Database
// Description	    : 
// Return type		: 
Database::~Database()
{
}

// Function name	: Database::SetID
// Description	    : 
// Return type		: void 
// Argument         : char *pszID
void Database::SetID(char *pszID)
{
	strcpy(m_pszID, pszID);
}

// Function name	: Database::GetID
// Description	    : 
// Return type		: bool 
// Argument         : char *pszID
// Argument         : int *length
bool Database::GetID(char *pszID, int *length)
{
	int len = strlen(m_pszID);
	if (len >= *length)
	{
		*length = len;
		return false;
	}
	*length = len;
	strcpy(pszID, m_pszID);
	return true;
}

// Function name	: Database::Init
// Description	    : 
// Return type		: bool 
bool Database::Init()
{
	int len;
	char pszTemp[100];
	bool bFound = false;

	if (GetEnvironmentVariable("MPICH_DBS", pszTemp, 100))
	{
		char *token;
		token = strtok(pszTemp, ":");
		if (token != NULL)
			strcpy(m_pszServerHost, token);
		token = strtok(NULL, " \n");
		if (token != NULL)
			m_nServerPort = atoi(token);
		bFound = true;
	}
	else
	{
		if (GetEnvironmentVariable("MPICH_DBS_HOST", m_pszServerHost, 100) &&
			GetEnvironmentVariable("MPICH_DBS_PORT", pszTemp, 100))
		{
			m_nServerPort = atoi(pszTemp);
			bFound = true;
		}
	}

	if (bFound)
	{
		int ret_val;
		char ack, cmd;
		WSAEVENT sock_event;
		SOCKET sock;
		
		// create the event
		sock_event = WSACreateEvent();
		if (sock_event == WSA_INVALID_EVENT)
			return (dbs_error("WSACreateEvent failed in Database::Init()", WSAGetLastError()) != 0);

		// create the socket
		sock = socket(PF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
			return (dbs_error("socket failed in Database::Init()", WSAGetLastError()) != 0);
		
		// connect to server
		ret_val = NT_connect(sock, m_pszServerHost, m_nServerPort);
		if (ret_val)
			return (dbs_error("Database::Init: NT_connect failed", ret_val) != 0);
		
		if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
			return (dbs_error("Database::Init: WSAEventSelect failed", WSAGetLastError()) != 0);

		// send id
		len = strlen(m_pszID)+1;
		if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
			return (dbs_error("Database::Init: send len failed", WSAGetLastError(), sock, sock_event) != 0);
		if (SendBlocking(sock, m_pszID, len, 0) == SOCKET_ERROR)
			return (dbs_error("Database::Init: send pszID failed", WSAGetLastError(), sock, sock_event) != 0);

		// send command
		cmd = MPI_DBS_CMD_EXISTS;
		if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
			return (dbs_error("Database::Init: send cmd failed", WSAGetLastError(), sock, sock_event) != 0);
		
		// receive response
		ret_val = ReceiveBlocking(sock, sock_event, &ack, 1, 0);
		if (ret_val)
			return (dbs_error("Database::Init: recv ack failed", WSAGetLastError(), sock, sock_event) != 0);

		if (ack != MPI_DBS_SUCCESS)
			return (dbs_error("Unable to contact mpi database server\n", 1, sock, sock_event) != 0);

		// close socket
		NT_closesocket(sock, sock_event);
	}
	return true;
}

// Function name	: =
// Description	    : 
// Return type		: Database& Database::operator 
// Argument         : Database &db
Database& Database::operator =(Database &db)
{
	if (this != &db)
	{
		// No state is maintained locally so simply remember how to contact
		// the database server
		m_nServerPort = db.m_nServerPort;
		strcpy(m_pszID, db.m_pszID);
		strcpy(m_pszServerHost, db.m_pszServerHost);
	}
	return *this;
}

// Function name	: Database::Delete
// Description	    : 
// Return type		: int 
int Database::Delete()
{
	int ret_val, len;
	char cmd, ack;
	WSAEVENT sock_event;
	SOCKET sock;
	
	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		{dbs_error("WSACreateEvent failed in Database::Get()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		{dbs_error("socket failed in Database::Get()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// connect to server
	ret_val = NT_connect(sock, m_pszServerHost, m_nServerPort);
	if (ret_val)
		{dbs_error("Database::Delete: NT_connect failed", ret_val, sock, sock_event); return MPI_DBS_FAIL;}
	
	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		{dbs_error("Database::Delete: WSAEventSelect failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send id
	len = strlen(m_pszID)+1;
	if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Delete: send len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, m_pszID, len, 0) == SOCKET_ERROR)
		{dbs_error("Database::Delete: send pszID failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// send delete command
	cmd = MPI_DBS_CMD_DELETE;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		{dbs_error("Database::Delete: send cmd failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// receive ack
	ret_val = ReceiveBlocking(sock, sock_event, &ack, 1, 0);
	if (ret_val)
		{dbs_error("Database::Delete: recv ack failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// close socket
	NT_closesocket(sock, sock_event);

	return ack == MPI_DBS_SUCCESS ? MPI_DBS_SUCCESS : MPI_DBS_FAIL;
}

// Function name	: Database::Get
// Description	    : 
// Return type		: int 
// Argument         : char *pszKey
// Argument         : void *pValue
// Argument         : int *length
int Database::Get(char *pszKey, void *pValue, int *length)
{
	int ret_val, len;
	char cmd;
	WSAEVENT sock_event;
	SOCKET sock;
	
	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		{dbs_error("WSACreateEvent failed in Database::Get()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		{dbs_error("socket failed in Database::Get()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// connect to server
	ret_val = NT_connect(sock, m_pszServerHost, m_nServerPort);
	if (ret_val)
		{dbs_error("Database::Get: NT_connect failed", ret_val, sock, sock_event); return MPI_DBS_FAIL;}
	
	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		{dbs_error("Database::Get: WSAEventSelect failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send id
	len = strlen(m_pszID)+1;
	if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Get: send len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, m_pszID, len, 0) == SOCKET_ERROR)
		{dbs_error("Database::Get: send pszID failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// send get command
	cmd = MPI_DBS_CMD_GET;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
			{dbs_error("Database::Get: send cmd failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send key length, key, buffer length
	len = strlen(pszKey)+1;
	if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Get: send len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, pszKey, len, 0) == SOCKET_ERROR)
		{dbs_error("Database::Get: send pszKey failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, (char*)length, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Get: send length failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// receive length and buffer
	// the buffer is not sent if there isn't enough room
	ret_val = ReceiveBlocking(sock, sock_event, (char*)&len, sizeof(int), 0);
	if (ret_val)
		{dbs_error("Database::Get: recv len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (len <= *length)
	{
		ret_val = ReceiveBlocking(sock, sock_event, (char*)pValue, len, 0);
		if (ret_val)
			{dbs_error("Database::Get: recv pValue failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
		*length = len;
		NT_closesocket(sock, sock_event);
		return MPI_DBS_SUCCESS;
	}
	*length = len;
	return MPI_DBS_FAIL;
}

// Function name	: Database::Put
// Description	    : 
// Return type		: int 
// Argument         : char *pszKey
// Argument         : void *pValue
// Argument         : int length
// Argument         : bool bPersistent
int Database::Put(char *pszKey, void *pValue, int length, bool bPersistent)
{
	int ret_val, len;
	char cmd, ack;
	WSAEVENT sock_event;
	SOCKET sock;
	
	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		{dbs_error("WSACreateEvent failed in Database::Put()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		{dbs_error("socket failed in Database::Put()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// connect to server
	ret_val = NT_connect(sock, m_pszServerHost, m_nServerPort);
	if (ret_val)
		{dbs_error("Database::Put: NT_connect failed", ret_val, sock, sock_event); return MPI_DBS_FAIL;}
	
	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		{dbs_error("Database::Put: WSAEventSelect failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send id
	len = strlen(m_pszID)+1;
	if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, m_pszID, len, 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send pszID failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// send put command
	cmd = bPersistent ? MPI_DBS_CMD_PUT_PERSISTENT : MPI_DBS_CMD_PUT_CONSUMABLE;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send cmd failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send key length, key, buffer length, buffer
	len = strlen(pszKey)+1;
	if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, pszKey, len, 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send pszKey failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, (char*)&length, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send length failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, (char*)pValue, length, 0) == SOCKET_ERROR)
		{dbs_error("Database::Put: send pValue failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// receive ack
	ret_val = ReceiveBlocking(sock, sock_event, &ack, 1, 0);
	if (ret_val)
		{dbs_error("Database::Put: recv ack failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// close socket
	NT_closesocket(sock, sock_event);

	return ack == MPI_DBS_SUCCESS ? MPI_DBS_SUCCESS : MPI_DBS_FAIL;
}

// Function name	: Database::Print
// Description	    : 
// Return type		: int 
// Argument         : char *pBuffer
// Argument         : int *length
int Database::Print(char *pBuffer, int *length)
{
	int ret_val, len;
	char cmd;
	WSAEVENT sock_event;
	SOCKET sock;
	
	// create the event
	sock_event = WSACreateEvent();
	if (sock_event == WSA_INVALID_EVENT)
		{dbs_error("WSACreateEvent failed in Database::Get()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// create the socket
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		{dbs_error("socket failed in Database::Get()", WSAGetLastError()); return MPI_DBS_FAIL;}

	// connect to server
	ret_val = NT_connect(sock, m_pszServerHost, m_nServerPort);
	if (ret_val)
		{dbs_error("Database::Print: NT_connect failed", ret_val, sock, sock_event); return MPI_DBS_FAIL;}
	
	if (WSAEventSelect(sock, sock_event, FD_READ | FD_CLOSE) == SOCKET_ERROR)
		{dbs_error("Database::Print: WSAEventSelect failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send id
	len = strlen(m_pszID)+1;
	if (SendBlocking(sock, (char*)&len, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Print: send len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (SendBlocking(sock, m_pszID, len, 0) == SOCKET_ERROR)
		{dbs_error("Database::Print: send pszID failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// send get state command
	cmd = MPI_DBS_CMD_GETSTATE;
	if (SendBlocking(sock, &cmd, 1, 0) == SOCKET_ERROR)
		{dbs_error("Database::Print: send cmd failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	
	// send buffer length
	if (SendBlocking(sock, (char*)length, sizeof(int), 0) == SOCKET_ERROR)
		{dbs_error("Database::Print: send length failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}

	// receive length and buffer
	// the buffer is not sent if there isn't enough room
	ret_val = ReceiveBlocking(sock, sock_event, (char*)&len, sizeof(int), 0);
	if (ret_val)
		{dbs_error("Database::Print: recv len failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
	if (len <= *length)
	{
		ret_val = ReceiveBlocking(sock, sock_event, pBuffer, len, 0);
		if (ret_val)
			{dbs_error("Database::Print: recv pBuffer failed", WSAGetLastError(), sock, sock_event); return MPI_DBS_FAIL;}
		*length = len;
		NT_closesocket(sock, sock_event);
		return MPI_DBS_SUCCESS;
	}
	*length = len;
	return MPI_DBS_FAIL;
}

// Function name	: dbs_error
// Description	    : 
// Return type		: int 
// Argument         : char *string
// Argument         : int value
// Argument         : bool bExit
int dbs_error(char *string, int value, bool bExit)
{
	printf("Error %d\n   %s\n", value, string);fflush(stdout);
    
	if (bExit)
	{
		WSACleanup();
		ExitProcess(1);
	}
	return 0;
}

// Function name	: dbs_error
// Description	    : 
// Return type		: int 
// Argument         : char *string
// Argument         : int value
// Argument         : SOCKET sock
// Argument         : WSAEVENT sock_event
// Argument         : bool bExit
int dbs_error(char *string, int value, SOCKET sock, WSAEVENT sock_event, bool bExit)
{
	NT_closesocket(sock, sock_event);
	printf("Error %d\n   %s\n", value, string);fflush(stdout);
    
	if (bExit)
	{
		WSACleanup();
		ExitProcess(1);
	}
	return 0;
}
