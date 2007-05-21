
// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the MPICHBNR_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// MPICHBNR_API functions as being imported from a DLL, wheras this DLL sees symbols
// defined with this macro as being exported.
#ifdef MPICHBNR_EXPORTS
#define MPICHBNR_API __declspec(dllexport)
#else
#define MPICHBNR_API __declspec(dllimport)
#endif

// This class is exported from the mpichbnr.dll
class MPICHBNR_API CMpichbnr {
public:
	CMpichbnr(void);
	// TODO: add your methods here.
};

extern MPICHBNR_API int nMpichbnr;

MPICHBNR_API int fnMpichbnr(void);

