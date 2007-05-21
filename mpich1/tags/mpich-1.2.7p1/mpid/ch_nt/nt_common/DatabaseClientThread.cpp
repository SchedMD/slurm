#include "Database.h"

// Function name	: DatabaseClientThread
// Description	    : 
// Return type		: int 
// Argument         : DBSClientArg *arg
int DatabaseClientThread(DBSClientArg *arg)
{
	char cCmd, ack=1;
	DWORD ret_val = 0;
	SOCKET sock;
	WSAEVENT sock_event;
	DatabaseServer *pServer;
	char pszID[DBSIDLEN], *pValue;
	void *pData;
	int length, datalen;

	sock = arg->sock;
	sock_event = arg->sock_event;
	pServer = arg->pServer;

	delete arg;

	// receive the id
	if ( ret_val = ReceiveBlocking(sock, sock_event, (char*)&length, sizeof(int), 0) )
		return dbs_error("Failure to read id length from DatabaseClient connection.\n", ret_val, sock, sock_event);
	if (length > DBSIDLEN)
		return dbs_error("length id too long", 0, sock, sock_event);
	if ( ret_val = ReceiveBlocking(sock, sock_event, pszID, length, 0) )
		return dbs_error("Failure to read pszID from DatabaseClient connection.\n", ret_val, sock, sock_event);
	// receive the command
	if ( ret_val = ReceiveBlocking(sock, sock_event, &cCmd, 1, 0) )
		return dbs_error("Failure to read command from ControlLoopClient connection.\n", ret_val, sock, sock_event);

	switch (cCmd)
	{
	case MPI_DBS_CMD_EXISTS:
		ack = MPI_DBS_SUCCESS;
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			return dbs_error("DatabaseClientThread: send ack failed.", WSAGetLastError(), sock, sock_event);
		break;
	case MPI_DBS_CMD_PUT_PERSISTENT:
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&length, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv key length failed.", ret_val, sock, sock_event);
		if (length <= 0)
			return dbs_error("DatabaseClientThread: Invalid length received for key.", 0, sock, sock_event);
		pValue = new char[length];
		if (ret_val = ReceiveBlocking(sock, sock_event, pValue, length, 0))
			return dbs_error("DatabaseClientThread: recv key failed.", ret_val, sock, sock_event);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&length, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv data length failed.", ret_val, sock, sock_event);
		if (length <= 0)
			return dbs_error("DatabaseClientThread: Invalid length received for data.", 0, sock, sock_event);
		pData = new char[length];
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)pData, length, 0))
			return dbs_error("DatabaseClientThread: recv data failed.", ret_val, sock, sock_event);
		ack = pServer->Put(pszID, pValue, pData, length, true);
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			return dbs_error("DatabaseClientThread: send put persistent ack failed.", WSAGetLastError(), sock, sock_event);
		break;
	case MPI_DBS_CMD_PUT_CONSUMABLE:
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&length, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv key length failed.", ret_val, sock, sock_event);
		if (length <= 0)
			return dbs_error("DatabaseClientThread: Invalid length received for key.", 0, sock, sock_event);
		pValue = new char[length];
		if (ret_val = ReceiveBlocking(sock, sock_event, pValue, length, 0))
			return dbs_error("DatabaseClientThread: recv key failed.", ret_val, sock, sock_event);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&length, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv data length failed.", ret_val, sock, sock_event);
		if (length <= 0)
			return dbs_error("DatabaseClientThread: Invalid length received for data.", 0, sock, sock_event);
		pData = new char[length];
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)pData, length, 0))
			return dbs_error("DatabaseClientThread: recv data failed.", ret_val, sock, sock_event);
		ack = pServer->Put(pszID, pValue, pData, length, false);
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			return dbs_error("DatabaseClientThread: send put consumable ack failed.", WSAGetLastError(), sock, sock_event);
		break;
	case MPI_DBS_CMD_GET:
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&length, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv key length failed.", ret_val, sock, sock_event);
		if (length <= 0)
			return dbs_error("DatabaseClientThread: Invalid length received for key.", 0, sock, sock_event);
		pValue = new char[length];
		if (ret_val = ReceiveBlocking(sock, sock_event, pValue, length, 0))
			return dbs_error("DatabaseClientThread: recv key failed.", ret_val, sock, sock_event);
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&datalen, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv data length failed.", ret_val, sock, sock_event);
		length = datalen;
		ack = pServer->Get(pszID, pValue, pData, &length);
		if (ack == MPI_DBS_FAIL)
			length = 0;
		if (SendBlocking(sock, (char*)&length, sizeof(int), 0) == SOCKET_ERROR)
			return dbs_error("DatabaseClientThread: send get data length failed.", WSAGetLastError(), sock, sock_event);
		if (length <= datalen)
		{
			if (ack == MPI_DBS_SUCCESS)
			{
				if (SendBlocking(sock, (char*)pData, length, 0) == SOCKET_ERROR)
					return dbs_error("DatabaseClientThread: send get data failed.", WSAGetLastError(), sock, sock_event);
				delete pData;
			}
		}
		delete pValue;
		break;
	case MPI_DBS_CMD_DELETE:
		ack = pServer->Delete(pszID);
		if (SendBlocking(sock, &ack, 1, 0) == SOCKET_ERROR)
			return dbs_error("DatabaseClientThread: send delete ack failed.", WSAGetLastError(), sock, sock_event);
		break;
	case MPI_DBS_CMD_GETSTATE:
		if (ret_val = ReceiveBlocking(sock, sock_event, (char*)&datalen, sizeof(int), 0))
			return dbs_error("DatabaseClientThread: recv data length failed.", ret_val, sock, sock_event);
		pValue = new char[datalen];
		length = datalen;
		ack = pServer->GetState(pValue, &length);
		if (ack == MPI_DBS_FAIL)
			length = 0;
		if (SendBlocking(sock, (char*)&length, sizeof(int), 0) == SOCKET_ERROR)
			return dbs_error("DatabaseClientThread: send get data length failed.", WSAGetLastError(), sock, sock_event);
		if (length <= datalen)
		{
			if (ack == MPI_DBS_SUCCESS)
			{
				if (SendBlocking(sock, (char*)pValue, length, 0) == SOCKET_ERROR)
					return dbs_error("DatabaseClientThread: send GetState data failed.", WSAGetLastError(), sock, sock_event);
			}
		}
		delete pValue;
		break;
	default:
		//dbs_error("Invalid command received from DatabaseClient connection.\n", cCmd, sock, sock_event);
		printf("Invalid command received: %d\n", cCmd);
	}

	NT_Tcp_closesocket(sock, sock_event);
	return 0;
}
