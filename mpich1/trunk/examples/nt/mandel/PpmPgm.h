// PpmPgm.h: interface for the PpmPgm class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PPMPGM_H__719AADD4_879E_11D1_BDD3_90EC07C10000__INCLUDED_)
#define AFX_PPMPGM_H__719AADD4_879E_11D1_BDD3_90EC07C10000__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include <afx.h>

class PpmPgm  
{
public:
	int max_value;
	int height, width;
	unsigned int **pixel, **red, **green, **blue, **gray;
	enum FILE_TYPE {PPMASCII, PPMRAW, PGMASCII, PGMRAW};
	FILE_TYPE type;

	PpmPgm &operator=(PpmPgm &p2);
	void ClearData();
	void Fill(unsigned int color = 0);
	bool Write(CFile &fout, FILE_TYPE t = PPMRAW);
	bool Read(CFile &fin);
	void Convert_To_Color();
	void Convert_To_Gray();
	PpmPgm();
	PpmPgm(PpmPgm &p2);
	PpmPgm(int w, int h, FILE_TYPE t = PPMRAW);
	virtual ~PpmPgm();
	void SetPixel(int i, int j, unsigned int color);

private:
	char SkipComment(CFile &f);
	void ClearColor();
	void ClearGray();
};

#endif // !defined(AFX_PPMPGM_H__719AADD4_879E_11D1_BDD3_90EC07C10000__INCLUDED_)
