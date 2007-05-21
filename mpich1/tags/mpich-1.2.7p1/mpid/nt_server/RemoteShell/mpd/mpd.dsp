# Microsoft Developer Studio Project File - Name="mpd" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=mpd - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mpd.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mpd.mak" CFG="mpd - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mpd - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "mpd - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
RSC=rc.exe

!IF  "$(CFG)" == "mpd - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE F90 /compile_only /nologo /warn:nofileopt
# ADD F90 /browser /compile_only /nologo /warn:nofileopt
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\common" /D "NDEBUG" /D "_WIN32_DCOM" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /FR /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 pdh.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /subsystem:console /machine:I386
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Saving copy to bin and system32 directory
PostBuild_Cmds=copy Release\mpd.exe %SystemRoot%\system32\mpd.exe	copy Release\mpd.exe ..\..\..\..\bin\mpd.exe
# End Special Build Tool

!ELSEIF  "$(CFG)" == "mpd - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE F90 /check:bounds /compile_only /debug:full /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD F90 /browser /check:bounds /compile_only /debug:full /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\common" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_WIN32_DCOM" /FR /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 pdh.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "mpd - Win32 Release"
# Name "mpd - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Source File

SOURCE=.\Command.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\Database.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\DatabaseClientThread.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\DatabaseServer.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\DatabaseServerThread.cpp
# End Source File
# Begin Source File

SOURCE=.\GetCPUsage.cpp
# End Source File
# Begin Source File

SOURCE=.\GetHosts.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\GetOpt.cpp
# End Source File
# Begin Source File

SOURCE=.\GetReturnThread.cpp
# End Source File
# Begin Source File

SOURCE=.\global.cpp
# End Source File
# Begin Source File

SOURCE=.\LaunchMPDProcess.cpp
# End Source File
# Begin Source File

SOURCE=.\LaunchMPDs.cpp
# End Source File
# Begin Source File

SOURCE=.\LaunchNode.cpp
# End Source File
# Begin Source File

SOURCE=.\LeftThread.cpp
# End Source File
# Begin Source File

SOURCE=.\ManageProcess.cpp
# End Source File
# Begin Source File

SOURCE=.\mpd.cpp
# End Source File
# Begin Source File

SOURCE=.\MPDList.cpp
# End Source File
# Begin Source File

SOURCE=.\PipeThread.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\recv_blocking.cpp
# End Source File
# Begin Source File

SOURCE=.\RightThread.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\send_blocking.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\sockets.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\StringOpt.cpp
# End Source File
# Begin Source File

SOURCE=.\TerminalClientThread.cpp
# End Source File
# Begin Source File

SOURCE=..\Common\Translate_Error.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=.\Command.h
# End Source File
# Begin Source File

SOURCE=..\Common\Database.h
# End Source File
# Begin Source File

SOURCE=.\GetCPUsage.h
# End Source File
# Begin Source File

SOURCE=.\GetHosts.h
# End Source File
# Begin Source File

SOURCE=..\Common\GetOpt.h
# End Source File
# Begin Source File

SOURCE=.\GetReturnThread.h
# End Source File
# Begin Source File

SOURCE=.\global.h
# End Source File
# Begin Source File

SOURCE=.\LaunchMPDProcess.h
# End Source File
# Begin Source File

SOURCE=.\LaunchMPDs.h
# End Source File
# Begin Source File

SOURCE=.\LaunchNode.h
# End Source File
# Begin Source File

SOURCE=.\LeftThread.h
# End Source File
# Begin Source File

SOURCE=.\ManageProcess.h
# End Source File
# Begin Source File

SOURCE=.\MPDList.h
# End Source File
# Begin Source File

SOURCE=..\Common\MPIJobDefs.h
# End Source File
# Begin Source File

SOURCE=.\PipeThread.h
# End Source File
# Begin Source File

SOURCE=.\RightThread.h
# End Source File
# Begin Source File

SOURCE=..\Common\sockets.h
# End Source File
# Begin Source File

SOURCE=..\Common\StringOpt.h
# End Source File
# Begin Source File

SOURCE=.\TerminalClientThread.h
# End Source File
# Begin Source File

SOURCE=..\Common\Translate_Error.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
