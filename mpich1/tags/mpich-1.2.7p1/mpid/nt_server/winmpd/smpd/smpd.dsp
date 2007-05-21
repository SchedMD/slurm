# Microsoft Developer Studio Project File - Name="smpd" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=smpd - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "smpd.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "smpd.mak" CFG="smpd - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "smpd - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "smpd - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
RSC=rc.exe

!IF  "$(CFG)" == "smpd - Win32 Release"

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
# ADD BASE F90 /compile_only /include:"Release/" /nologo /warn:nofileopt
# ADD F90 /browser /compile_only /include:"Release/" /nologo /warn:nofileopt
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\privileges" /I "..\bsocket" /I "..\dbs" /I "..\mpdutil" /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "HAVE_WINBCONF_H" /D FD_SETSIZE=256 /D "NO_BSOCKETS" /FR /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 mpdutil.lib mpr.lib privileges.lib dbs.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386 /out:"Release/mpd.exe" /libpath:"..\dbs\release" /libpath:"..\privileges\release" /libpath:"..\mpdutil\release"

!ELSEIF  "$(CFG)" == "smpd - Win32 Debug"

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
# ADD BASE F90 /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD F90 /browser /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\privileges" /I "..\bsocket" /I "..\dbs" /I "..\mpdutil" /D "_DEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "HAVE_WINBCONF_H" /D FD_SETSIZE=256 /D "NO_BSOCKETS" /FR /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 mpdutil.lib mpr.lib privileges.lib dbs.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /out:"Debug/mpd.exe" /pdbtype:sept /libpath:"..\dbs\debug" /libpath:"..\privileges\debug" /libpath:"..\mpdutil\debug"

!ENDIF 

# Begin Target

# Name "smpd - Win32 Release"
# Name "smpd - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Source File

SOURCE=.\authenticate.cpp
# End Source File
# Begin Source File

SOURCE=.\barrier.cpp
# End Source File
# Begin Source File

SOURCE=.\connect_and_redirect.cpp
# End Source File
# Begin Source File

SOURCE=.\connect_and_restart.cpp
# End Source File
# Begin Source File

SOURCE=.\crypt.c
# End Source File
# Begin Source File

SOURCE=.\doconsole.cpp
# End Source File
# Begin Source File

SOURCE=.\forwarder.cpp
# End Source File
# Begin Source File

SOURCE=.\getdircontents.cpp
# End Source File
# Begin Source File

SOURCE=.\GetOpt.cpp
# End Source File
# Begin Source File

SOURCE=.\GetStringOpt.cpp
# End Source File
# Begin Source File

SOURCE=.\launch.cpp
# End Source File
# Begin Source File

SOURCE=.\launchdbg.cpp
# End Source File
# Begin Source File

SOURCE=.\launchprocess.cpp
# End Source File
# Begin Source File

SOURCE=.\mapdrive.cpp
# End Source File
# Begin Source File

SOURCE=.\mpd.cpp
# End Source File
# Begin Source File

SOURCE=.\mpd_context.cpp
# End Source File
# Begin Source File

SOURCE=.\mpd_start.cpp
# End Source File
# Begin Source File

SOURCE=.\mpd_stop.cpp
# End Source File
# Begin Source File

SOURCE=.\mpdconsole.cpp
# End Source File
# Begin Source File

SOURCE=.\mpdregistry.cpp
# End Source File
# Begin Source File

SOURCE=.\mpdstat.cpp
# End Source File
# Begin Source File

SOURCE=.\mpdtmp.cpp
# End Source File
# Begin Source File

SOURCE=.\mpduser.cpp
# End Source File
# Begin Source File

SOURCE=.\parse_command_line.cpp
# End Source File
# Begin Source File

SOURCE=.\read_write_string.cpp
# End Source File
# Begin Source File

SOURCE=.\redirect.cpp
# End Source File
# Begin Source File

SOURCE=.\redirectovl.cpp
# End Source File
# Begin Source File

SOURCE=.\run.cpp
# End Source File
# Begin Source File

SOURCE=.\safe_terminate_process.cpp
# End Source File
# Begin Source File

SOURCE=.\Service.cpp
# End Source File
# Begin Source File

SOURCE=.\Translate_Error.cpp
# End Source File
# Begin Source File

SOURCE=.\updatempdinternal.cpp
# End Source File
# Begin Source File

SOURCE=.\updatempich.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=.\GetOpt.h
# End Source File
# Begin Source File

SOURCE=.\GetStringOpt.h
# End Source File
# Begin Source File

SOURCE=.\mpd.h
# End Source File
# Begin Source File

SOURCE=.\mpdimpl.h
# End Source File
# Begin Source File

SOURCE=.\safe_terminate_process.h
# End Source File
# Begin Source File

SOURCE=.\Service.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
