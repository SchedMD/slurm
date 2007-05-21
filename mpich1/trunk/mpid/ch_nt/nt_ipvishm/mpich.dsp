# Microsoft Developer Studio Project File - Name="mpich" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=mpich - Win32 DebugNoFortran
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mpich.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mpich.mak" CFG="mpich - Win32 DebugNoFortran"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mpich - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mpich - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mpich - Win32 DebugCDECLStrLenEnd" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mpich - Win32 ReleaseCDECLStrLenEnd" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mpich - Win32 DebugNoFortran" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mpich - Win32 ReleaseNoFortran" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "../../../lib"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE F90 /include:"Release/"
# ADD F90 /browser /compile_only /iface:nomixed_str_len_arg /iface:cref /include:"Release/" /threads
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_USRDLL" /D "NT_TCP_DLL_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /I "..\..\nt_server\winmpd\mpdutil" /I "..\..\nt_server\winmpd\bsocket" /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL" /D "NDEBUG" /D "HAVE_FORTRAN_API" /D "FORTRAN_EXPORTS" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "USE_MPI_INTERNALLY" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409
# ADD RSC /l 0x409 /i "\mpich\include" /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /machine:I386
# ADD LINK32 crypt.lib mpdutil.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /map /machine:I386 /libpath:"..\..\nt_server\winmpd\crypt\release" /libpath:"../../../lib" /libpath:"..\..\nt_server\winmpd\mpdutil\release"
# SUBTRACT LINK32 /force
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Copying mpich.dll to system32 directory
PostBuild_Cmds=copy ..\..\..\lib\mpich.dll %SystemRoot%\system32\mpich.dll	copy ..\nt_common\mpicherr.dll %SystemRoot%\system32\mpicherr.dll	copy ..\nt_common\mpicherr.dll ..\..\..\lib\mpicherr.dll
# End Special Build Tool

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "../../../lib"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE F90 /include:"Debug/"
# ADD F90 /browser /compile_only /dbglibs /iface:nomixed_str_len_arg /iface:cref /include:"Debug/" /threads
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "_WINDOWS" /D "_DEBUG" /D "_MBCS" /D "_USRDLL" /D "NT_TCP_DLL_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /I "..\..\nt_server\winmpd\mpdutil" /I "..\..\nt_server\winmpd\bsocket" /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL" /D "_DEBUG" /D "HAVE_FORTRAN_API" /D "FORTRAN_EXPORTS" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "USE_MPI_INTERNALLY" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 crypt.lib mpdutil.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /out:"../../../lib/mpichd.dll" /pdbtype:sept /libpath:"../../../lib" /libpath:"..\..\nt_server\winmpd\mpdutil\debug" /libpath:"..\..\nt_server\winmpd\crypt\debug"
# SUBTRACT LINK32 /pdb:none /force
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Copying mpichd.dll to system32 directory
PostBuild_Cmds=copy ..\..\..\lib\mpichd.dll %SystemRoot%\system32\mpichd.dll
# End Special Build Tool

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "DebugCDECLStrLenEnd"
# PROP BASE Intermediate_Dir "DebugCDECLStrLenEnd"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "DebugCDECLStrLenEnd"
# PROP Intermediate_Dir "DebugCDECLStrLenEnd"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE F90 /browser /compile_only /dbglibs /iface:nomixed_str_len_arg /iface:cref /include:"DebugCDECLStrLenEnd/" /threads
# ADD F90 /browser /compile_only /dbglibs /iface:nomixed_str_len_arg /iface:cref /include:"DebugCDECLStrLenEnd/" /threads
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\nt_common" /I ".\\" /I "..\..\include" /I "..\..\src\fortran\include" /I "..\ch2" /I "..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /D "USE_FORT_STDCALL" /D "_DEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /FR /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /I "..\..\nt_server\winmpd\mpdutil" /I "..\..\nt_server\winmpd\bsocket2" /D "_DEBUG" /D "HAVE_FORTRAN_API" /D "FORTRAN_EXPORTS" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "USE_MPI_INTERNALLY" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /out:"../../lib/mpichd.dll" /pdbtype:sept /libpath:"../../lib"
# SUBTRACT BASE LINK32 /pdb:none /force
# ADD LINK32 crypt.lib mpdutil.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /out:"DebugCDECLStrLenEnd/mpichdcn.dll" /pdbtype:sept /libpath:"../../../lib" /libpath:"..\..\nt_server\winmpd\mpdutil\debug" /libpath:"..\..\nt_server\winmpd\crypt\debug"
# SUBTRACT LINK32 /pdb:none /force

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "ReleaseCDECLStrLenEnd"
# PROP BASE Intermediate_Dir "ReleaseCDECLStrLenEnd"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "ReleaseCDECLStrLenEnd"
# PROP Intermediate_Dir "ReleaseCDECLStrLenEnd"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE F90 /browser /compile_only /iface:nomixed_str_len_arg /iface:cref /include:"ReleaseCDECLStrLenEnd/" /threads
# ADD F90 /browser /compile_only /iface:nomixed_str_len_arg /iface:cref /include:"ReleaseCDECLStrLenEnd/" /threads
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /I "..\nt_common" /I ".\\" /I "..\..\include" /I "..\..\src\fortran\include" /I "..\ch2" /I "..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /FR /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /I "..\..\nt_server\winmpd\mpdutil" /I "..\..\nt_server\winmpd\bsocket2" /D "NDEBUG" /D "HAVE_FORTRAN_API" /D "FORTRAN_EXPORTS" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "USE_MPI_INTERNALLY" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /i "\mpich\include" /d "NDEBUG"
# ADD RSC /l 0x409 /i "\mpich\include" /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /map /machine:I386 /libpath:"../../lib"
# SUBTRACT BASE LINK32 /force
# ADD LINK32 crypt.lib mpdutil.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /map /machine:I386 /out:"ReleaseCDECLStrLenEnd/mpichcn.dll" /libpath:"../../../lib" /libpath:"..\..\nt_server\winmpd\mpdutil\release"
# SUBTRACT LINK32 /force

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "mpich___Win32_DebugNoFortran"
# PROP BASE Intermediate_Dir "mpich___Win32_DebugNoFortran"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "../../../lib"
# PROP Intermediate_Dir "DebugNoFortran"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE F90 /browser /compile_only /dbglibs /iface:nomixed_str_len_arg /iface:cref /include:"mpich___Win32_DebugNoFortran/" /include:"Debug/" /threads
# ADD F90 /browser /compile_only /dbglibs /iface:nomixed_str_len_arg /iface:cref /include:"DebugNoFortran/" /include:"Debug/" /threads
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL" /D "_DEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "HAVE_FORTRAN_API" /D "FORTRAN_EXPORTS" /FR /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /I "..\..\nt_server\winmpd\mpdutil" /I "..\..\nt_server\winmpd\bsocket2" /D "_DEBUG" /D "MPID_NO_FORTRAN" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "USE_MPI_INTERNALLY" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /out:"../../../lib/mpichd.dll" /pdbtype:sept /libpath:"../../../lib"
# SUBTRACT BASE LINK32 /pdb:none /force
# ADD LINK32 crypt.lib mpdutil.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /out:"../../../lib/mpichd.dll" /pdbtype:sept /libpath:"../../../lib" /libpath:"..\..\nt_server\winmpd\mpdutil\debug" /libpath:"..\..\nt_server\winmpd\crypt\debug"
# SUBTRACT LINK32 /pdb:none /force
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Copying mpichd.dll to system32 directory
PostBuild_Cmds=copy ..\..\..\lib\mpichd.dll %SystemRoot%\system32\mpichd.dll
# End Special Build Tool

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "mpich___Win32_ReleaseNoFortran"
# PROP BASE Intermediate_Dir "mpich___Win32_ReleaseNoFortran"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "../../../lib"
# PROP Intermediate_Dir "ReleaseNoFortran"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
LIB32=link.exe
# ADD BASE F90 /browser /compile_only /iface:nomixed_str_len_arg /iface:cref /include:"mpich___Win32_ReleaseNoFortran/" /include:"Release/" /threads
# ADD F90 /browser /compile_only /iface:nomixed_str_len_arg /iface:cref /include:"ReleaseNoFortran/" /include:"Release/" /threads
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "HAVE_FORTRAN_API" /D "FORTRAN_EXPORTS" /FR /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "include" /I "..\nt_common" /I ".\\" /I "..\..\..\include" /I "..\..\..\src\fortran\include" /I "..\..\ch2" /I "..\..\util" /I "..\..\..\romio\include" /I "..\..\..\romio\adio\include" /I "..\..\nt_server\winmpd\mpdutil" /I "..\..\nt_server\winmpd\bsocket2" /D "NDEBUG" /D "MPID_NO_FORTRAN" /D "_WINDOWS" /D "_USRDLL" /D "_MBCS" /D "_WIN32_DCOM" /D "HAVE_STDLIB_H" /D "BUILDING_IN_MPICH" /D "HAVE_MPICHCONF_H" /D "USE_MPI_VERSIONS" /D "USE_MPI_INTERNALLY" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /i "\mpich\include" /d "NDEBUG"
# ADD RSC /l 0x409 /i "\mpich\include" /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /map /machine:I386 /libpath:"../../../lib"
# SUBTRACT BASE LINK32 /force
# ADD LINK32 crypt.lib mpdutil.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /dll /map /machine:I386 /libpath:"../../../lib" /libpath:"..\..\nt_server\winmpd\mpdutil\release"
# SUBTRACT LINK32 /force
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Copying mpich.dll to system32 directory
PostBuild_Cmds=copy ..\..\..\lib\mpich.dll %SystemRoot%\system32\mpich.dll
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "mpich - Win32 Release"
# Name "mpich - Win32 Debug"
# Name "mpich - Win32 DebugCDECLStrLenEnd"
# Name "mpich - Win32 ReleaseCDECLStrLenEnd"
# Name "mpich - Win32 DebugNoFortran"
# Name "mpich - Win32 ReleaseNoFortran"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "fortran"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\abortf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\addressf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\allgatherf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\allgathervf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\allreducef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\alltoallf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\alltoallvf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\attr_delvalf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\attr_getvalf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\attr_putvalf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\barrierf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\bcastf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\bsend_initf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\bsendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\bufattachf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\buffreef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cancelf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_coordsf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_createf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_getf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_mapf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_rankf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_shiftf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cart_subf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\cartdim_getf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_createf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_dupf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_freef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_groupf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_namegetf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_nameputf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_rankf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_rgroupf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_rsizef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_sizef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_splitf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\comm_testicf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\commcomparef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\commreqfreef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\create_recvf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\create_sendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\darrayf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\dims_createf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\dup_fnf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\errclassf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\errcreatef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\errfreef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\errgetf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\errorstringf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\errsetf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\finalizedf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\finalizef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\fstrutils.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\gatherf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\gathervf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\getcountf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\getelementsf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\getpnamef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\getversionf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\graph_getf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\graph_mapf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\graph_nbrf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\graphcreatef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\graphdimsgtf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\graphnbrcntf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_difff.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_exclf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_freef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_inclf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_interf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_rankf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_rexclf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_rinclf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_sizef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\group_unionf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\groupcomparf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\grouptranksf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\ibsendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\ic_createf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\ic_mergef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_createf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_deletef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_dupf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_freef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_getf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_getnksf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_getnthf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_getvlnf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\info_setf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\initf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\initf77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\initfutil.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\initializef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\iprobef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\irecvf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\irsendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\isendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\issendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\keyval_freef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\keyvalcreatf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\null_copyfnf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\null_del_fnf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\opcreatef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\opfreef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\pack_sizef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\packf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\pcontrolf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\probef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\recvf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\red_scatf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\reducef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\rsend_initf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\rsendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\scanf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\scatterf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\scattervf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\sendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\sendrecvf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\sendrecvrepf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\ssend_initf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\ssendf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\startallf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\startf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\statuscancelf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\statuselmf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\statusf2c.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\subarrayf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\testallf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\testanyf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\testcancelf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\testf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\testsomef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\topo_testf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_blkindf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_commitf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_contigf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_extentf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_freef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_get_envf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_getcontf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_hindf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_hvecf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_indf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_lbf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_sizef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_structf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_ubf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\type_vecf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\unpackf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\waitallf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\waitanyf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\waitf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\waitsomef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\wtickf.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\wtimef.c

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Group
# Begin Group "src"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\src\env\abort.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\address.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\allgather.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\allgatherv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\allreduce.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\alltoall.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\alltoallv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\attr_delval.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\attr_getval.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\attr_putval.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\attr_util.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\barrier.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\bcast.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\bsend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\bsend_init.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\util\bsendutil2.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\bufattach.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\buffree.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\cancel.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_coords.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_create.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_get.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_map.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_rank.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_shift.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cart_sub.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\cartdim_get.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_create.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_dup.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_free.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_group.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_name_get.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_name_put.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_rank.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_rgroup.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_rsize.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_size.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_split.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_testic.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\comm_util.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\commcompare.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\commreq_free.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\context_util.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\create_recv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\create_send.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\darray.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\debugutil.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\dims_create.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\dmpi\dmpipk.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\dup_fn.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\errclass.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\errcreate.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\errfree.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\errget.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\errorstring.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\errset.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\finalize.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\finalized.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\gather.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\gatherv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\getcount.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\getelements.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\getpname.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\getversion.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\global_ops.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\graph_get.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\graph_map.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\graph_nbr.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\graphcreate.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\graphdimsget.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\graphnbrcnt.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_diff.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_excl.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_free.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_incl.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_inter.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_rank.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_rexcl.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_rincl.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_size.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_tranks.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_union.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\group_util.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\groupcompare.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\ibsend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\ic_create.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\ic_merge.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_c2f.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_create.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_delete.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_dup.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_f2c.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_free.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_get.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_getnks.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_getnth.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_getvln.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\info_set.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\init.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\initdte.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\initialize.c
# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\ENV\initthread.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\initutil.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\inter_fns.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\intra_fns.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\intra_fns_new.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\intra_scan.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\iprobe.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\irecv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\irsend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\isend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\issend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\keyval_free.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\keyvalcreate.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\mperror.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\util\mpirutil.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\msgqdllloc.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\nerrmsg.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\null_copyfn.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\null_del_fn.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\opcreate.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\opfree.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\oputil.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\pack.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\pack_size.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\profile\pcontrol.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\dmpi\pkutil.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\probe.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\util\ptrcvt.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\recv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\red_scat.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\reduce.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\requestc2f.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\rsend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\rsend_init.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\scan.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\scatter.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\scatterv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\send.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\sendrecv.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\sendrecv_rep.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\sendutil.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\ssend.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\ssend_init.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\start.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\startall.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\statusc2f.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\external\statuscancel.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\external\statuselm.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\subarray.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\test.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\testall.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\testany.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\testcancel.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\testsome.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\topo_test.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\topo_util.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\misc2\type_blkind.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_commit.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_contig.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_extent.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_free.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\external\type_get_cont.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\external\type_get_env.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_hind.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_hvec.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_ind.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_lb.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_size.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_struct.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_ub.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_util.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\type_vec.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\unpack.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\util\util_hbt.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\wait.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\waitall.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\waitany.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\pt2pt\waitsome.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\wtick.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\wtime.c
# End Source File
# End Group
# Begin Group "device"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\ch2\adi2cancel.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2hrecv.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2hsend.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2hssend.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2init.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2mpack.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2pack.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2probe.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2recv.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2req.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2send.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\adi2ssend.c
# End Source File
# Begin Source File

SOURCE=.\bnrfunctions.cpp
# End Source File
# Begin Source File

SOURCE=..\..\ch2\bswap2.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\calltrace.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chbeager.c
# End Source File
# Begin Source File

SOURCE=..\..\CH2\chcancel.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chchkdev.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chdebug.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chflow.c
# End Source File
# Begin Source File

SOURCE=.\chinit.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chnodename.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chpackflow.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chshort.c
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chtick.c
# End Source File
# Begin Source File

SOURCE=..\..\util\cmnargs.c
# End Source File
# Begin Source File

SOURCE=..\nt_common\Database.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\DatabaseClientThread.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\DatabaseServer.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\DatabaseServerThread.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\MessageQueue.cpp
# End Source File
# Begin Source File

SOURCE=.\nt_ipvishm_comport.cpp
# End Source File
# Begin Source File

SOURCE=.\nt_ipvishm_control_loop.cpp
# End Source File
# Begin Source File

SOURCE=.\nt_ipvishm_nrndv.c
# End Source File
# Begin Source File

SOURCE=.\nt_ipvishm_priv.cpp
# End Source File
# Begin Source File

SOURCE=.\nt_ipvishm_rndv.c
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_lock.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_log.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_smp.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_tcp_recv_blocking.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_tcp_send_blocking.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_tcp_sockets.cpp
# End Source File
# Begin Source File

SOURCE=.\nt_vi.cpp
# End Source File
# Begin Source File

SOURCE=..\..\ch2\objtrace.c
# End Source File
# Begin Source File

SOURCE=..\nt_common\parsecliques.cpp
# End Source File
# Begin Source File

SOURCE=..\..\util\queue.c
# End Source File
# Begin Source File

SOURCE=..\..\util\sbcnst2.c
# End Source File
# Begin Source File

SOURCE=..\nt_common\ShmemLockedQueue.cpp
# End Source File
# Begin Source File

SOURCE=..\nt_common\syslog.cpp
# End Source File
# Begin Source File

SOURCE=..\..\util\tr2.c
# End Source File
# End Group
# Begin Group "romio"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_aggregate.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_close.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_delete.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_end.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_fstype.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_get_sh_fp.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_hints.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_init.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_iopen.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_close.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_done.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_fcntl.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_flush.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_hints.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_iread.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_iwrite.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_open.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_rdcoll.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_read.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_resize.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_seek.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_wait.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_wrcoll.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs_write.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_open.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_read_coll.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_read_str.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_read_str_naive.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_set_sh_fp.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_set_view.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_write_coll.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\ad_write_str.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\async_list.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\byte_offset.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\cb_config_list.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\close.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\delete.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\eof_offset.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\error.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\file_c2f.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\file_f2c.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\flatten.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fsync.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\gencheck.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_amode.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_atom.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_bytoff.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_errh.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_extent.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\get_fp_posn.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_group.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_info.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_posn.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_posn_sh.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_size.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\get_view.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\ioreq_c2f.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\ioreq_f2c.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iotest.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iowait.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iread.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iread_at.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iread_sh.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\iscontig.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iwrite.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iwrite_at.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\iwrite_sh.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\lock.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Intermediate_Dir "Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Intermediate_Dir "Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP BASE Intermediate_Dir "Release"
# PROP Intermediate_Dir "Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP BASE Intermediate_Dir "Release"
# PROP Intermediate_Dir "Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Intermediate_Dir "Release"
# PROP Intermediate_Dir "Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Intermediate_Dir "Release"
# PROP Intermediate_Dir "Release"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\malloc.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\open.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\prealloc.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\rd_atallb.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\rd_atalle.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_all.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_allb.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_alle.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_at.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_atall.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_ord.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_ordb.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_orde.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\read_sh.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\req_malloc.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\seek.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\seek_sh.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\set_atom.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\set_errh.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\set_info.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\set_size.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\set_view.c"
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\setfn.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\shfp_fname.c
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\common\status_setb.c
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\wr_atallb.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\wr_atalle.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_all.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_allb.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_alle.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_at.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_atall.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_ord.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_ordb.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_orde.c"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\write_sh.c"
# End Source File
# End Group
# Begin Group "fortran_romio"

# PROP Default_Filter ""
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\closef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\deletef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\fsyncf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_amodef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_atomf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_bytofff.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_errhf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_extentf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_groupf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_infof.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_posn_shf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_posnf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_sizef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\get_viewf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iotestf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iowaitf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iread_atf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iread_shf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\ireadf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iwrite_atf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iwrite_shf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\iwritef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\openf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\preallocf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\rd_atallbf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\rd_atallef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_allbf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_allef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_allf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_atallf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_atf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_ordbf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_ordef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_ordf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\read_shf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\readf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\seek_shf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\seekf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\set_atomf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\set_errhf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\set_infof.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\set_sizef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\set_viewf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\wr_atallbf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\wr_atallef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_allbf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_allef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_allf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_atallf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_atf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_ordbf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_ordef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_ordf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\write_shf.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\writef.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Group
# Begin Group "g77"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\abortf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\addressf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\allgatherf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\allgathervf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\allreducef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\alltoallf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\alltoallvf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\attr_delvalf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\attr_getvalf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\attr_putvalf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\barrierf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\bcastf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\bsend_initf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\bsendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\bufattachf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\buffreef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cancelf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_coordsf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_createf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_getf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_mapf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_rankf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_shiftf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cart_subf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\cartdim_getf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\closef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_createf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_dupf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_freef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_groupf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_namegetf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_nameputf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_rankf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_rgroupf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_rsizef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_sizef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_splitf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\comm_testicf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\commcomparef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\commreqfreef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\create_recvf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\create_sendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\darrayf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\deletef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\dims_createf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\dup_fnf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\errclassf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\errcreatef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\errfreef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\errgetf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\errorstringf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\errsetf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\finalizedf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\finalizef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\fstrutils.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\fsyncf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\gatherf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\gathervf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_amodef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_atomf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_bytofff.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_errhf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_extentf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_groupf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_infof.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_posn_shf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_posnf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_sizef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\get_viewf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\getcountf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\getelementsf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\getpnamef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\getversionf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\graph_getf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\graph_mapf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\graph_nbrf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\graphcreatef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\graphdimsgtf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\graphnbrcntf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_difff.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_exclf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_freef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_inclf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_interf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_rankf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_rexclf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_rinclf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_sizef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\group_unionf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\groupcomparf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\grouptranksf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\ibsendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\ic_createf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\ic_mergef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_createf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_deletef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_dupf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_freef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_getf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_getnksf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_getnthf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_getvlnf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\info_setf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\initf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE" /D "USE_FARG_UPPER"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE" /D "USE_FARG_UPPER"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE" /D "USE_FARG_UPPER"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE" /D "USE_FARG_UPPER"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE" /D "USE_FARG_UPPER"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE" /D "USE_FARG_UPPER"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\initf77.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\initfutil.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\initializef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iotestf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iowaitf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\iprobef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iread_atf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iread_shf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\ireadf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\irecvf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\irsendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\isendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\issendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iwrite_atf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iwrite_shf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\iwritef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\keyval_freef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\keyvalcreatf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\null_copyfnf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\null_del_fnf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\opcreatef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\openf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\opfreef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\pack_sizef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\packf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\pcontrolf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\preallocf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\probef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\rd_atallbf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\rd_atallef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_allbf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_allef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_allf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_atallf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_atf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_ordbf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_ordef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_ordf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\read_shf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\readf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\recvf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\red_scatf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\reducef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\rsend_initf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\rsendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\scanf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\scatterf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\scattervf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\seek_shf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\seekf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\sendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\sendrecvf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\sendrecvrepf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\set_atomf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\set_errhf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\set_infof.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\set_sizef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\set_viewf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\ssend_initf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\ssendf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\startallf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\startf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\statuscancelf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\statuselmf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\statusf2c.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\subarrayf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\testallf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\testanyf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\testcancelf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\testf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\testsomef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\topo_testf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_blkindf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_commitf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_contigf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_extentf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_freef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_get_envf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_getcontf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_hindf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_hvecf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_indf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_lbf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_sizef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_structf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_ubf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\type_vecf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\unpackf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\waitallf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\waitanyf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\waitf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\waitsomef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\wr_atallbf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\wr_atallef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_allbf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_allef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_allf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_atallf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_atf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_ordbf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_ordef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_ordf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\write_shf.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\g77\writef.g77.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\wtickf.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\g77\wtimef.g77.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1
# ADD BASE CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"
# ADD CPP /D "FORTRANDOUBLEUNDERSCORE" /D "F77_NAME_LOWER_2USCORE"

!ENDIF 

# End Source File
# End Group
# Begin Group "intel"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\abortf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\addressf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\allgatherf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\allgathervf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\allreducef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\alltoallf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\alltoallvf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\attr_delvalf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\attr_getvalf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\attr_putvalf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\barrierf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\bcastf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\bsend_initf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\bsendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\bufattachf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\buffreef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cancelf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_coordsf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_createf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_getf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_mapf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_rankf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_shiftf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cart_subf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\cartdim_getf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\closef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_createf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_dupf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_freef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_groupf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_namegetf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_nameputf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_rankf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_rgroupf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_rsizef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_sizef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_splitf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\comm_testicf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\commcomparef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\commreqfreef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\create_recvf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\create_sendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\darrayf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\deletef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\dims_createf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\dup_fnf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\errclassf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\errcreatef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\errfreef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\errgetf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\errorstringf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\errsetf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\finalizedf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\finalizef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\fstrutils.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\fsyncf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\gatherf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\gathervf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_amodef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_atomf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_bytofff.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_errhf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_extentf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_groupf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_infof.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_posn_shf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_posnf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_sizef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\get_viewf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\getcountf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\getelementsf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\getpnamef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\getversionf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\graph_getf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\graph_mapf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\graph_nbrf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\graphcreatef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\graphdimsgtf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\graphnbrcntf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_difff.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_exclf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_freef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_inclf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_interf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_rankf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_rexclf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_rinclf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_sizef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\group_unionf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\groupcomparf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\grouptranksf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\ibsendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\ic_createf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\ic_mergef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_createf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_deletef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_dupf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_freef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_getf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_getnksf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_getnthf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_getvlnf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\info_setf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\initf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\initf77.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\initfutil.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\initializef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iotestf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iowaitf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\iprobef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iread_atf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iread_shf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\ireadf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\irecvf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\irsendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\isendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\issendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iwrite_atf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iwrite_shf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\iwritef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\keyval_freef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\keyvalcreatf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\null_copyfnf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\null_del_fnf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\opcreatef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\openf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\opfreef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\pack_sizef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\packf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\pcontrolf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\preallocf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\probef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\rd_atallbf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\rd_atallef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_allbf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_allef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_allf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_atallf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_atf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_ordbf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_ordef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_ordf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\read_shf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\readf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\recvf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\red_scatf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\reducef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\rsend_initf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\rsendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\scanf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\scatterf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\scattervf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\seek_shf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\seekf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\sendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\sendrecvf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\sendrecvrepf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\set_atomf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\set_errhf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\set_infof.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\set_sizef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\set_viewf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\ssend_initf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\ssendf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\startallf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\startf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\statuscancelf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\statuselmf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\statusf2c.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\subarrayf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\testallf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\testanyf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\testcancelf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\testf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\testsomef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\topo_testf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_blkindf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_commitf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_contigf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_extentf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_freef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_get_envf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_getcontf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_hindf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_hvecf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_indf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_lbf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_sizef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_structf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_ubf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\type_vecf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\unpackf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\waitallf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\waitanyf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\waitf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\waitsomef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\wr_atallbf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\wr_atallef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_allbf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_allef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_allf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_atallf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_atf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_ordbf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_ordef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_ordf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\write_shf.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\fortran\intel\writef.intel.c"

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\wtickf.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\src\fortran\src\intel\wtimef.intel.c

!IF  "$(CFG)" == "mpich - Win32 Release"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# SUBTRACT CPP /D "USE_FORT_MIXED_STR_LEN" /D "USE_FORT_STDCALL"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Group
# Begin Source File

SOURCE=.\mpich.def

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\mpich_nofortran.def

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\mpichsingle.def

!IF  "$(CFG)" == "mpich - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\..\romio\adio\ad_ntfs\ad_ntfs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\adio.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\adio_cb_config_list.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\adio_extern.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\adioi.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\adioi_error.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\adioi_fs_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\attach.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\attr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\binding.h
# End Source File
# Begin Source File

SOURCE=.\include\bnr.h
# End Source File
# Begin Source File

SOURCE=.\bnrfunctions.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\calltrace.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\channel.h
# End Source File
# Begin Source File

SOURCE=.\chconfig.h
# End Source File
# Begin Source File

SOURCE=.\chdef.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\chhetero.h
# End Source File
# Begin Source File

SOURCE=..\..\CH2\chpackflow.h
# End Source File
# Begin Source File

SOURCE=..\..\util\cmnargs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\coll\coll.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\comm.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\cookie.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\Database.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\datatype.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\dev.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\flow.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\context\ic.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\lock.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\MessageQueue.h
# End Source File
# Begin Source File

SOURCE=".\mpi++.h"
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpi.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpi_errno.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpi_error.h
# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\include\mpi_fort.h
# End Source File
# Begin Source File

SOURCE=.\include\mpi_fortconf.h
# End Source File
# Begin Source File

SOURCE=.\include\mpi_fortdefs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\include\mpi_fortimpl.h
# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\include\mpi_fortnames.h
# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\include\mpi_fortran.h
# End Source File
# Begin Source File

SOURCE=.\include\mpichconf.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpicoll.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\mpid.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\mpid_bind.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\mpid_debug.h
# End Source File
# Begin Source File

SOURCE=.\mpid_time.h
# End Source File
# Begin Source File

SOURCE=.\mpiddev.h
# End Source File
# Begin Source File

SOURCE=.\include\mpidefs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpidmpi.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\env\mpierrstrings.h
# End Source File
# Begin Source File

SOURCE=.\include\mpif.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpifort.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpiimpl.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\mpimem.h
# End Source File
# Begin Source File

SOURCE=.\include\mpio.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\mpio_error.h
# End Source File
# Begin Source File

SOURCE=.\include\mpiof.h
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\mpioimpl.h"
# End Source File
# Begin Source File

SOURCE="..\..\..\romio\mpi-io\mpioprof.h"
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpiops.h
# End Source File
# Begin Source File

SOURCE=..\..\..\romio\adio\include\mpipr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpiprof.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\mpipt2pt.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\topol\mpitopo.h
# End Source File
# Begin Source File

SOURCE=.\netdb.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_common.h
# End Source File
# Begin Source File

SOURCE=.\nt_global.h
# End Source File
# Begin Source File

SOURCE=.\nt_global_cpp.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_log.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\nt_tcp_sockets.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\objtrace.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\packets.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\parsecliques.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\patchlevel.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\pmpi2mpi.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\ptrcvt.h
# End Source File
# Begin Source File

SOURCE=..\..\util\queue.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\req.h
# End Source File
# Begin Source File

SOURCE=..\..\ch2\reqalloc.h
# End Source File
# Begin Source File

SOURCE=.\include\romioconf.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\sbcnst.h
# End Source File
# Begin Source File

SOURCE=..\..\util\sbcnst2.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\sendq.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\ShmemLockedQueue.h
# End Source File
# Begin Source File

SOURCE=..\nt_common\syslog.h
# End Source File
# Begin Source File

SOURCE=..\..\util\tr2.h
# End Source File
# Begin Source File

SOURCE=.\vipl.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\nt_fortran\farg.f

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\nt_fortran\initfcmn.f

!IF  "$(CFG)" == "mpich - Win32 Release"

!ELSEIF  "$(CFG)" == "mpich - Win32 Debug"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseCDECLStrLenEnd"

!ELSEIF  "$(CFG)" == "mpich - Win32 DebugNoFortran"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "mpich - Win32 ReleaseNoFortran"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\SRC\fortran\src\initfdte.f
# PROP Exclude_From_Build 1
# End Source File
# End Target
# End Project
