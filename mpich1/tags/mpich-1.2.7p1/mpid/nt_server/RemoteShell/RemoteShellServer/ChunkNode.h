#ifndef CHUNKNODE_H
#define CHUNKNODE_H

struct ChunkNode
{
	ChunkNode():pData(NULL), pNext(NULL), dwSize(0), bStdError(false) {};

	bool bStdError;
	DWORD dwSize, dwExitCode;
	char *pData;
	ChunkNode *pNext;
};

#endif
