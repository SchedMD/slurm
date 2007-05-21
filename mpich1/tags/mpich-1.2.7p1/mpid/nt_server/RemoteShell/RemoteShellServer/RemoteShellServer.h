
#pragma warning( disable: 4049 )  /* more than 64k source lines */

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 6.00.0347 */
/* at Mon Nov 12 14:21:16 2001
 */
/* Compiler settings for C:\Mpich\MPID\nt_server\RemoteShell\RemoteShellServer\RemoteShellServer.idl:
    Oicf, W1, Zp8, env=Win32 (32b run)
    protocol : dce , ms_ext, c_ext
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
//@@MIDL_FILE_HEADING(  )


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 440
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __RemoteShellServer_h__
#define __RemoteShellServer_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IRemoteShell_FWD_DEFINED__
#define __IRemoteShell_FWD_DEFINED__
typedef interface IRemoteShell IRemoteShell;
#endif 	/* __IRemoteShell_FWD_DEFINED__ */


#ifndef __RemoteShell_FWD_DEFINED__
#define __RemoteShell_FWD_DEFINED__

#ifdef __cplusplus
typedef class RemoteShell RemoteShell;
#else
typedef struct RemoteShell RemoteShell;
#endif /* __cplusplus */

#endif 	/* __RemoteShell_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 

void * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void * ); 

#ifndef __IRemoteShell_INTERFACE_DEFINED__
#define __IRemoteShell_INTERFACE_DEFINED__

/* interface IRemoteShell */
/* [unique][helpstring][dual][uuid][object] */ 


EXTERN_C const IID IID_IRemoteShell;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("56657461-CDE5-4C12-B379-9FE844195E00")
    IRemoteShell : public IDispatch
    {
    public:
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE LaunchProcess( 
            BSTR bCmdLine,
            BSTR bEnv,
            BSTR bDir,
            BSTR bAccount,
            BSTR bPassword,
            long *nPid,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetProcessOutput( 
            VARIANT *vOutput,
            long *nState,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE PutProcessInput( 
            VARIANT vInput,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE Abort( 
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE SendBreak( 
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE CreateTempFile( 
            BSTR *bFileName,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetPortFromFile( 
            BSTR bFileName,
            long *nPort,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GrantAccessToDesktop( 
            BSTR bAccount,
            BSTR bPassword,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE CreateFileMapping( 
            BSTR bName,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
        virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetPortFromMapping( 
            long *nPort,
            long *nError,
            BSTR *bErrorMsg) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IRemoteShellVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IRemoteShell * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IRemoteShell * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IRemoteShell * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfoCount )( 
            IRemoteShell * This,
            /* [out] */ UINT *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetTypeInfo )( 
            IRemoteShell * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ ITypeInfo **ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE *GetIDsOfNames )( 
            IRemoteShell * This,
            /* [in] */ REFIID riid,
            /* [size_is][in] */ LPOLESTR *rgszNames,
            /* [in] */ UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ DISPID *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE *Invoke )( 
            IRemoteShell * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS *pDispParams,
            /* [out] */ VARIANT *pVarResult,
            /* [out] */ EXCEPINFO *pExcepInfo,
            /* [out] */ UINT *puArgErr);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *LaunchProcess )( 
            IRemoteShell * This,
            BSTR bCmdLine,
            BSTR bEnv,
            BSTR bDir,
            BSTR bAccount,
            BSTR bPassword,
            long *nPid,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetProcessOutput )( 
            IRemoteShell * This,
            VARIANT *vOutput,
            long *nState,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *PutProcessInput )( 
            IRemoteShell * This,
            VARIANT vInput,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *Abort )( 
            IRemoteShell * This,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *SendBreak )( 
            IRemoteShell * This,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *CreateTempFile )( 
            IRemoteShell * This,
            BSTR *bFileName,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetPortFromFile )( 
            IRemoteShell * This,
            BSTR bFileName,
            long *nPort,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GrantAccessToDesktop )( 
            IRemoteShell * This,
            BSTR bAccount,
            BSTR bPassword,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *CreateFileMapping )( 
            IRemoteShell * This,
            BSTR bName,
            long *nError,
            BSTR *bErrorMsg);
        
        /* [helpstring][id] */ HRESULT ( STDMETHODCALLTYPE *GetPortFromMapping )( 
            IRemoteShell * This,
            long *nPort,
            long *nError,
            BSTR *bErrorMsg);
        
        END_INTERFACE
    } IRemoteShellVtbl;

    interface IRemoteShell
    {
        CONST_VTBL struct IRemoteShellVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IRemoteShell_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IRemoteShell_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IRemoteShell_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IRemoteShell_GetTypeInfoCount(This,pctinfo)	\
    (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo)

#define IRemoteShell_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo)

#define IRemoteShell_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)

#define IRemoteShell_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)


#define IRemoteShell_LaunchProcess(This,bCmdLine,bEnv,bDir,bAccount,bPassword,nPid,nError,bErrorMsg)	\
    (This)->lpVtbl -> LaunchProcess(This,bCmdLine,bEnv,bDir,bAccount,bPassword,nPid,nError,bErrorMsg)

#define IRemoteShell_GetProcessOutput(This,vOutput,nState,nError,bErrorMsg)	\
    (This)->lpVtbl -> GetProcessOutput(This,vOutput,nState,nError,bErrorMsg)

#define IRemoteShell_PutProcessInput(This,vInput,nError,bErrorMsg)	\
    (This)->lpVtbl -> PutProcessInput(This,vInput,nError,bErrorMsg)

#define IRemoteShell_Abort(This,nError,bErrorMsg)	\
    (This)->lpVtbl -> Abort(This,nError,bErrorMsg)

#define IRemoteShell_SendBreak(This,nError,bErrorMsg)	\
    (This)->lpVtbl -> SendBreak(This,nError,bErrorMsg)

#define IRemoteShell_CreateTempFile(This,bFileName,nError,bErrorMsg)	\
    (This)->lpVtbl -> CreateTempFile(This,bFileName,nError,bErrorMsg)

#define IRemoteShell_GetPortFromFile(This,bFileName,nPort,nError,bErrorMsg)	\
    (This)->lpVtbl -> GetPortFromFile(This,bFileName,nPort,nError,bErrorMsg)

#define IRemoteShell_GrantAccessToDesktop(This,bAccount,bPassword,nError,bErrorMsg)	\
    (This)->lpVtbl -> GrantAccessToDesktop(This,bAccount,bPassword,nError,bErrorMsg)

#define IRemoteShell_CreateFileMapping(This,bName,nError,bErrorMsg)	\
    (This)->lpVtbl -> CreateFileMapping(This,bName,nError,bErrorMsg)

#define IRemoteShell_GetPortFromMapping(This,nPort,nError,bErrorMsg)	\
    (This)->lpVtbl -> GetPortFromMapping(This,nPort,nError,bErrorMsg)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_LaunchProcess_Proxy( 
    IRemoteShell * This,
    BSTR bCmdLine,
    BSTR bEnv,
    BSTR bDir,
    BSTR bAccount,
    BSTR bPassword,
    long *nPid,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_LaunchProcess_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_GetProcessOutput_Proxy( 
    IRemoteShell * This,
    VARIANT *vOutput,
    long *nState,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_GetProcessOutput_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_PutProcessInput_Proxy( 
    IRemoteShell * This,
    VARIANT vInput,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_PutProcessInput_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_Abort_Proxy( 
    IRemoteShell * This,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_Abort_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_SendBreak_Proxy( 
    IRemoteShell * This,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_SendBreak_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_CreateTempFile_Proxy( 
    IRemoteShell * This,
    BSTR *bFileName,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_CreateTempFile_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_GetPortFromFile_Proxy( 
    IRemoteShell * This,
    BSTR bFileName,
    long *nPort,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_GetPortFromFile_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_GrantAccessToDesktop_Proxy( 
    IRemoteShell * This,
    BSTR bAccount,
    BSTR bPassword,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_GrantAccessToDesktop_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_CreateFileMapping_Proxy( 
    IRemoteShell * This,
    BSTR bName,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_CreateFileMapping_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id] */ HRESULT STDMETHODCALLTYPE IRemoteShell_GetPortFromMapping_Proxy( 
    IRemoteShell * This,
    long *nPort,
    long *nError,
    BSTR *bErrorMsg);


void __RPC_STUB IRemoteShell_GetPortFromMapping_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IRemoteShell_INTERFACE_DEFINED__ */



#ifndef __REMOTESHELLSERVERLib_LIBRARY_DEFINED__
#define __REMOTESHELLSERVERLib_LIBRARY_DEFINED__

/* library REMOTESHELLSERVERLib */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_REMOTESHELLSERVERLib;

EXTERN_C const CLSID CLSID_RemoteShell;

#ifdef __cplusplus

class DECLSPEC_UUID("43DC2E30-38F9-464B-84E0-1B1BEA64B6DC")
RemoteShell;
#endif
#endif /* __REMOTESHELLSERVERLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

unsigned long             __RPC_USER  BSTR_UserSize(     unsigned long *, unsigned long            , BSTR * ); 
unsigned char * __RPC_USER  BSTR_UserMarshal(  unsigned long *, unsigned char *, BSTR * ); 
unsigned char * __RPC_USER  BSTR_UserUnmarshal(unsigned long *, unsigned char *, BSTR * ); 
void                      __RPC_USER  BSTR_UserFree(     unsigned long *, BSTR * ); 

unsigned long             __RPC_USER  VARIANT_UserSize(     unsigned long *, unsigned long            , VARIANT * ); 
unsigned char * __RPC_USER  VARIANT_UserMarshal(  unsigned long *, unsigned char *, VARIANT * ); 
unsigned char * __RPC_USER  VARIANT_UserUnmarshal(unsigned long *, unsigned char *, VARIANT * ); 
void                      __RPC_USER  VARIANT_UserFree(     unsigned long *, VARIANT * ); 

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


