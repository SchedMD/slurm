#include "GetStringOpt.h"
#include "mpdimpl.h"
#include "mpdutil.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include "Translate_Error.h"

struct HandleNode
{
    HANDLE handle;
    HandleNode *pNext;
};

void AddHandleToList(HandleNode *&pList, HANDLE h)
{
    HandleNode *p;

    p = new HandleNode;
    p->handle = h;
    p->pNext = pList;
    pList = p;
}

void CloseHandleList(HandleNode *pList)
{
    HandleNode *p;
    while (pList)
    {
	p = pList;
	pList = pList->pNext;
	CloseHandle(p->handle);
	delete p;
    }
}

void DebugWaitForProcess(bool &bAborted, char *pszError)
{
    DEBUG_EVENT DebugEv;                   // debugging event information 
    DWORD dwContinueStatus = DBG_CONTINUE; // exception continuation
    bool bExit = false;
    HandleNode *pList = NULL;

    bAborted = false;

    for(;;) 
    { 
	
	// Wait for a debugging event to occur. The second parameter indicates 
	// that the function does not return until a debugging event occurs. 
	
	WaitForDebugEvent(&DebugEv, INFINITE); 
	
	// Process the debugging event code. 
	
	switch (DebugEv.dwDebugEventCode)
	{
        case EXCEPTION_DEBUG_EVENT: 
	    // Process the exception code. When handling 
	    // exceptions, remember to set the continuation 
	    // status parameter (dwContinueStatus). This value 
	    // is used by the ContinueDebugEvent function.
	    //bAborted = (DebugEv.u.Exception.ExceptionRecord.ExceptionFlags == EXCEPTION_NONCONTINUABLE);
	    dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
	    // only abort on last chance exceptions to allow the user process to catch the exception
	    if (!DebugEv.u.Exception.dwFirstChance)
		bAborted = true;
            switch (DebugEv.u.Exception.ExceptionRecord.ExceptionCode)
            {
	    case EXCEPTION_ACCESS_VIOLATION: 
                // First chance: Pass this on to the system. 
                // Last chance: Display an appropriate error. 
		if (!DebugEv.u.Exception.dwFirstChance)
		{
		    if (DebugEv.u.Exception.ExceptionRecord.NumberParameters == 2)
		    {
			if (DebugEv.u.Exception.ExceptionRecord.ExceptionInformation[0] == 1)
			{
			    // write error
			    sprintf(pszError, "EXCEPTION_ACCESS_VIOLATION: instruction address: 0x%x, invalid write to 0x%x",
				DebugEv.u.Exception.ExceptionRecord.ExceptionAddress,
				DebugEv.u.Exception.ExceptionRecord.ExceptionInformation[1]);
			}
			else
			{
			    // read error
			    sprintf(pszError, "EXCEPTION_ACCESS_VIOLATION: instruction address: 0x%x, invalid read from 0x%x",
				DebugEv.u.Exception.ExceptionRecord.ExceptionAddress,
				DebugEv.u.Exception.ExceptionRecord.ExceptionInformation[1]);
			}
		    }
		    else
		    {
			sprintf(pszError, "EXCEPTION_ACCESS_VIOLATION: instruction address: 0x%x",
			    DebugEv.u.Exception.ExceptionRecord.ExceptionAddress);
		    }
		}
		break;
	    case EXCEPTION_BREAKPOINT: 
                // First chance: Display the current 
                // instruction and register values. 
		//sprintf(pszError, "EXCEPTION_BREAKPOINT: address: 0x%x", DebugEv.u.Exception.ExceptionRecord.ExceptionAddress);
		// I don't know what this exception is for but it is thrown for all processes good and bad, so just ignore it.
		bAborted = false;
		dwContinueStatus = DBG_CONTINUE;
		break;
	    case EXCEPTION_DATATYPE_MISALIGNMENT: 
                // First chance: Pass this on to the system. 
                // Last chance: Display an appropriate error. 
		sprintf(pszError, "EXCEPTION_DATATYPE_MISALIGNMENT");
		break;
	    case EXCEPTION_SINGLE_STEP: 
                // First chance: Update the display of the 
                // current instruction and register values. 
		sprintf(pszError, "EXCEPTION_SINGLE_STEP");
		break;
	    case DBG_CONTROL_C: 
                // First chance: Pass this on to the system. 
                // Last chance: Display an appropriate error. 
		sprintf(pszError, "DBG_CONTROL_C");
		break;
	    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		sprintf(pszError, "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.");
		break;
	    case EXCEPTION_FLT_DENORMAL_OPERAND:
		sprintf(pszError, "One of the operands in a floating-point operation is denormal. A denormal value is one that is too small to represent as a standard floating-point value.");
		break;
	    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		sprintf(pszError, "The thread tried to divide a floating-point value by a floating-point divisor of zero.");
		break;
	    case EXCEPTION_FLT_INEXACT_RESULT:
		sprintf(pszError, "The result of a floating-point operation cannot be represented exactly as a decimal fraction.");
		break;
	    case EXCEPTION_FLT_INVALID_OPERATION:
		sprintf(pszError, "This exception represents any floating-point exception not included in this list.");
		break;
	    case EXCEPTION_FLT_OVERFLOW:
		sprintf(pszError, "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.");
		break;
	    case EXCEPTION_FLT_STACK_CHECK:
		sprintf(pszError, "The stack overflowed or underflowed as the result of a floating-point operation.");
		break;
	    case EXCEPTION_FLT_UNDERFLOW:
		sprintf(pszError, "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.");
		break;
	    case EXCEPTION_ILLEGAL_INSTRUCTION:
		sprintf(pszError, "The thread tried to execute an invalid instruction.");
		break;
	    case EXCEPTION_IN_PAGE_ERROR:
		sprintf(pszError, "The thread tried to access a page that was not present, and the system was unable to load the page. For example, this exception might occur if a network connection is lost while running a program over the network.");
		break;
	    case EXCEPTION_INT_DIVIDE_BY_ZERO:
		sprintf(pszError, "The thread tried to divide an integer value by an integer divisor of zero.");
		break;
	    case EXCEPTION_INT_OVERFLOW:
		sprintf(pszError, "The result of an integer operation caused a carry out of the most significant bit of the result.");
		break;
	    case EXCEPTION_INVALID_DISPOSITION:
		sprintf(pszError, "An exception handler returned an invalid disposition to the exception dispatcher. Programmers using a high-level language such as C should never encounter this exception.");
		break;
	    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		sprintf(pszError, "The thread tried to continue execution after a noncontinuable exception occurred.");
		break;
	    case EXCEPTION_PRIV_INSTRUCTION:
		sprintf(pszError, "The thread tried to execute an instruction whose operation is not allowed in the current machine mode.");
		break;
	    case EXCEPTION_STACK_OVERFLOW:
		sprintf(pszError, "The thread used up its stack.");
		break;
	    default:
		sprintf(pszError, "EXCEPTION_DEBUG_EVENT %d", DebugEv.u.Exception.ExceptionRecord.ExceptionCode);
		bAborted = false;
		dwContinueStatus = DBG_CONTINUE;
		break;
            }
	    break;
	case CREATE_THREAD_DEBUG_EVENT: 
	    // As needed, examine or change the thread's registers 
	    // with the GetThreadContext and SetThreadContext functions; 
	    // and suspend and resume thread execution with the 
	    // SuspendThread and ResumeThread functions. 
	    //sprintf(pszError, "CREATE_THREAD_DEBUG_EVENT");
	    if (DebugEv.u.CreateThread.hThread != NULL)
		AddHandleToList(pList, DebugEv.u.CreateThread.hThread);
	    break;
	case CREATE_PROCESS_DEBUG_EVENT: 
	    // As needed, examine or change the registers of the 
	    // process's initial thread with the GetThreadContext and 
	    // SetThreadContext functions; read from and write to the 
	    // process's virtual memory with the ReadProcessMemory and 
	    // WriteProcessMemory functions; and suspend and resume 
	    // thread execution with the SuspendThread and ResumeThread 
	    // functions. 
	    //sprintf(pszError, "CREATE_PROCESS_DEBUG_EVENT");
	    if (DebugEv.u.CreateProcessInfo.hFile != NULL)
		AddHandleToList(pList, DebugEv.u.CreateProcessInfo.hFile);
	    if (DebugEv.u.CreateProcessInfo.hProcess != NULL)
		AddHandleToList(pList, DebugEv.u.CreateProcessInfo.hProcess);
	    if (DebugEv.u.CreateProcessInfo.hThread != NULL)
		AddHandleToList(pList, DebugEv.u.CreateProcessInfo.hThread);
	    break;
	case EXIT_THREAD_DEBUG_EVENT: 
	    // Display the thread's exit code. 
	    //sprintf(pszError, "EXIT_THREAD_DEBUG_EVENT: exit code: %d", DebugEv.u.ExitThread.dwExitCode);
	    break;
	case EXIT_PROCESS_DEBUG_EVENT: 
	    // Display the process's exit code. 
	    //sprintf(pszError, "EXIT_PROCESS_DEBUG_EVENT: exit code: %d", DebugEv.u.ExitProcess.dwExitCode);
	    bExit = true;
	    break;
	case LOAD_DLL_DEBUG_EVENT: 
	    // Read the debugging information included in the newly 
	    // loaded DLL. 
	    //sprintf(pszError, "LOAD_DLL_DEBUG_EVENT");
	    if (DebugEv.u.LoadDll.hFile != NULL)
		AddHandleToList(pList, DebugEv.u.LoadDll.hFile);
	    break;
	case UNLOAD_DLL_DEBUG_EVENT: 
	    // Display a message that the DLL has been unloaded. 
	    //sprintf(pszError, "UNLOAD_DLL_DEBUG_EVENT");
	    break;
	case OUTPUT_DEBUG_STRING_EVENT: 
	    // Display the output debugging string. 
	    //sprintf(pszError, "OUTPUT_DEBUG_STRING_EVENT");
	    break;
	default:
	    sprintf(pszError, "Unknown event %d", DebugEv.dwDebugEventCode);
	    break;
	}
	
	// Resume executing the thread that reported the debugging event. 
	
	ContinueDebugEvent(DebugEv.dwProcessId, 
	    DebugEv.dwThreadId, dwContinueStatus);

	if (bExit)
	    break;
    }

    CloseHandleList(pList);
}
