# Microsoft Developer Studio Project File - Name="mpdutil" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=mpdutil - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mpdutil.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mpdutil.mak" CFG="mpdutil - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mpdutil - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "mpdutil - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
RSC=rc.exe

!IF  "$(CFG)" == "mpdutil - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE F90 /compile_only /include:"Release/" /nologo /warn:nofileopt
# ADD F90 /compile_only /include:"Release/" /nologo /warn:nofileopt
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\bsocket" /I "..\mpd" /I "..\crypt" /D "NDEBUG" /D "WIN32" /D "_MBCS" /D "_LIB" /D "HAVE_WINBCONF_H" /D FD_SETSIZE=256 /D "NO_BSOCKETS" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "mpdutil - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE F90 /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD F90 /browser /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\bsocket" /I "..\mpd" /I "..\crypt" /D "_DEBUG" /D "WIN32" /D "_MBCS" /D "_LIB" /D "HAVE_WINBCONF_H" /D FD_SETSIZE=256 /D "NO_BSOCKETS" /FR /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "mpdutil - Win32 Release"
# Name "mpdutil - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Source File

SOURCE=.\dbg_printf.cpp
# End Source File
# Begin Source File

SOURCE=.\easy_sockets.cpp
# End Source File
# Begin Source File

SOURCE=.\getfile.cpp
# End Source File
# Begin Source File

SOURCE=.\GetOpt.cpp
# End Source File
# Begin Source File

SOURCE=.\GetStringOpt.cpp
# End Source File
# Begin Source File

SOURCE=.\mpdutil.cpp
# End Source File
# Begin Source File

SOURCE=.\mpdversion.cpp
# End Source File
# Begin Source File

SOURCE=.\putfile.cpp
# End Source File
# Begin Source File

SOURCE=.\qvs.cpp
# End Source File
# Begin Source File

SOURCE=.\read_write_string.cpp
# End Source File
# Begin Source File

SOURCE=.\strencode.cpp
# End Source File
# Begin Source File

SOURCE=.\Translate_Error.cpp
# End Source File
# Begin Source File

SOURCE=.\updatempd.cpp
# End Source File
# Begin Source File

SOURCE=.\updatempich.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=.\easy_sockets.h
# End Source File
# Begin Source File

SOURCE=.\GetOpt.h
# End Source File
# Begin Source File

SOURCE=.\GetStringOpt.h
# End Source File
# Begin Source File

SOURCE=.\mpdutil.h
# End Source File
# Begin Source File

SOURCE=.\qvs.h
# End Source File
# Begin Source File

SOURCE=.\Translate_Error.h
# End Source File
# End Group
# End Target
# End Project
