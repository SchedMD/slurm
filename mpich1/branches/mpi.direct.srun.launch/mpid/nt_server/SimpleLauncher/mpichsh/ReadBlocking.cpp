#include "stdafx.h"
#include "ReadBlocking.h"

bool ReadBlocking(HANDLE hObject, LPVOID pBuffer, DWORD length)
{
	DWORD num_read;

	while (length)
	{
		if (!ReadFile(hObject, pBuffer, length, &num_read, NULL))
			return false;

		length -= num_read;
		pBuffer = (LPBYTE)pBuffer + num_read;
	}

	return true;
}
