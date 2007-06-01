// PpmPgm.cpp: implementation of the PpmPgm class.
//
//////////////////////////////////////////////////////////////////////

//#include "stdafx.h"
//#include "p1.h"
#include "PpmPgm.h"
#include <ctype.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

PpmPgm::PpmPgm()
{
	pixel = red = green = blue = gray = NULL;
	width = height = 0;
	max_value = 255;
	type = PPMRAW;
}

PpmPgm::PpmPgm(PpmPgm &p2)
{
	pixel = red = green = blue = gray = NULL;
	*this = p2;
}

PpmPgm::PpmPgm(int w, int h, FILE_TYPE t)
{
	pixel = red = green = blue = gray = NULL;
	width = w;
	height = h;
	type = t;
	max_value = 255;
	if ((type == PPMRAW) || (type == PPMASCII))
	{
		red		= new unsigned int *[height];
		green	= new unsigned int *[height];
		blue	= new unsigned int *[height];
		pixel	= new unsigned int *[height];
		for (int i=0; i<height; i++)
		{
			red[i]		= new unsigned int[width];
			green[i]	= new unsigned int[width];
			blue[i]		= new unsigned int[width];
			pixel[i]	= new unsigned int[width];
		}
	}
	else
	{
		gray  = new unsigned int *[height];
		pixel = new unsigned int *[height];
		for (int i=0; i<height; i++)
		{
			gray[i]  = new unsigned int[width];
			pixel[i] = new unsigned int[width];
		}
	}
	Fill(0);
}

PpmPgm::~PpmPgm()
{
	ClearData();
}

bool PpmPgm::Read(CFile &fin)
{
	char buffer[100];
	int i;
	unsigned char *line;

	// Read the magic number
	fin.Read(buffer, 2);
	if (buffer[0] != 'P')
		return false;
	switch (buffer[1])
	{
	case '2':
		type = PGMASCII;
		break;
	case '3':
		type = PPMASCII;
		break;
	case '5':
		type = PGMRAW;
		break;
	case '6':
		type = PPMRAW;
		break;
	default:
		return false;
	}

	// Read the width
	fin.Read(buffer, 1);
	do
	{
		if (buffer[0] == '#')
			buffer[0] = SkipComment(fin);
		while (isspace(buffer[0]))
			fin.Read(buffer, 1);
	} while (buffer[0] == '#');
	char *temp = buffer;
	while (!isspace(*temp))
	{
		temp++;
		fin.Read(temp, 1);
	}
	width = atoi(buffer);

	// Read the height
	buffer[0] = *temp;
	do
	{
		if (buffer[0] == '#')
			buffer[0] = SkipComment(fin);
		while (isspace(buffer[0]))
			fin.Read(buffer, 1);
	} while (buffer[0] == '#');
	temp = buffer;
	while(!isspace(*temp))
	{
		temp++;
		fin.Read(temp, 1);
	}
	height = atoi(buffer);

	// Read the maximum value
	buffer[0] = *temp;
	do
	{
		if (buffer[0] == '#')
			buffer[0] = SkipComment(fin);
		while (isspace(buffer[0]))
			fin.Read(buffer, 1);
	} while (buffer[0] == '#');
	temp = buffer;
	while(!isspace(*temp))
	{
		temp++;
		fin.Read(temp, 1);
	}
	max_value = atoi(buffer);

	// Allocate memory for the pixel values
	pixel = new unsigned int *[height];
	for (i=0; i<height; i++)
		pixel[i] = new unsigned int[width];
	if ((type == PPMASCII) || (type == PPMRAW))
	{
		red = new unsigned int *[height];
		green = new unsigned int *[height];
		blue = new unsigned int *[height];
		for (i=0; i<height; i++)
		{
			red[i] = new unsigned int[width];
			green[i] = new unsigned int[width];
			blue[i] = new unsigned int[width];
		}
	}
	else
	{
		gray = new unsigned int *[height];
		for (i=0; i<height; i++)
			gray[i] = new unsigned int[width];
	}
		
	// Read in the pixel values
	switch (type)
	{
	case PPMASCII:
		for (i=0; i<height; i++)
		{
			for (int j=0; j<width; j++)
			{
				// Read red value
				buffer[0] = *temp;
				while (isspace(buffer[0]))
					fin.Read(buffer, 1);
				temp = buffer;
				while (!isspace(*temp))
				{
					temp++;
					fin.Read(temp, 1);
				}
				red[i][j] = atoi(buffer);

				// Read green value
				buffer[0] = *temp;
				while (isspace(buffer[0]))
					fin.Read(buffer, 1);
				temp = buffer;
				while (!isspace(*temp))
				{
					temp++;
					fin.Read(temp, 1);
				}
				green[i][j] = atoi(buffer);

				// Read blue value
				buffer[0] = *temp;
				while (isspace(buffer[0]))
					fin.Read(buffer, 1);
				temp = buffer;
				while (!isspace(*temp))
				{
					temp++;
					fin.Read(temp, 1);
				}
				blue[i][j] = atoi(buffer);

				// set the pixel value
				pixel[i][j] = 
					((unsigned int)(((double)blue[i][j]/(double)max_value)*255.0)<<16) + 
					((unsigned int)(((double)green[i][j]/(double)max_value)*255.0)<<8) + 
					((unsigned int)(((double)red[i][j]/(double)max_value)*255.0));
			}
		}
		break;
	case PPMRAW:
		// allocate memory for a complete scan line
		line = new unsigned char[width*3];
		for (i=0; i<height; i++)
		{
			// read in a horizontal line of the image
			fin.Read(line, width*3);

			// pick out the r,g,b values from the line
			for (int j=0; j<width; j++)
			{
				red[i][j] = (unsigned int)line[j*3];
				green[i][j] = (unsigned int)line[j*3+1];
				blue[i][j] = (unsigned int)line[j*3+2];
				pixel[i][j] = 
					((unsigned int)line[j*3+2]<<16) + 
					((unsigned int)line[j*3+1]<<8) + 
					((unsigned int)line[j*3]);
			}
		}
		delete line;
		break;
	case PGMASCII:
		for (i=0; i<height; i++)
		{
			for (int j=0; j<width; j++)
			{
				// read the gray value
				buffer[0] = *temp;
				while (isspace(buffer[0]))
					fin.Read(buffer, 1);
				temp = buffer;
				while (!isspace(*temp))
				{
					temp++;
					fin.Read(temp, 1);
				}
				gray[i][j] = atoi(buffer);

				// set the pixel value
				pixel[i][j] = 
					((unsigned int)(((double)gray[i][j]/(double)max_value)*255.0)<<16) + 
					((unsigned int)(((double)gray[i][j]/(double)max_value)*255.0)<<8) + 
					((unsigned int)(((double)gray[i][j]/(double)max_value)*255.0));
			}
		}
		break;
	case PGMRAW:
		// allocate memory for a complete scan line
		line = new unsigned char[width*3];
		for (i=0; i<height; i++)
		{
			// read in a horizontal line of the image
			fin.Read(line, width);

			// pick out the gray values for each pixel along the line
			for (int j=0; j<width; j++)
			{
				gray[i][j] = (unsigned int)line[j];
				pixel[i][j] = (line[j]<<16) + (line[j]<<8) + (line[j]);
			}
		}
		delete line;
		break;
	}
	return true;
}

bool PpmPgm::Write(CFile &fout, FILE_TYPE t)
{
	char buffer[100];
	int i;
	unsigned char *line;

	// convert the image to type t
	switch (type)
    {
    case PPMASCII:
		switch(t)
		{
		case PPMASCII:
			break;
		case PPMRAW:
			type = PPMRAW;
			break;
		case PGMASCII:
			Convert_To_Gray();
			type = PGMASCII;
		break;
		case PGMRAW:
			Convert_To_Gray();
			type = PGMRAW;
		break;
		}
		break;
    case PPMRAW:
		switch(t)
		{
		case PPMASCII:
			type = PPMASCII;
			break;
		case PPMRAW:
			break;
		case PGMASCII:
			Convert_To_Gray();
			type = PGMASCII;
			break;
		case PGMRAW:
			Convert_To_Gray();
			type = PGMRAW;
			break;
		}
		break;
	case PGMASCII:
		switch(t)
		{
		case PPMASCII:
			Convert_To_Color();
			type = PPMASCII;
			break;
		case PPMRAW:
			Convert_To_Color();
			type = PPMRAW;
			break;
		case PGMASCII:
			break;
		case PGMRAW:
			type = PGMRAW;
			break;
		}
		break;
    case PGMRAW:
		switch(t)
		{
		case PPMASCII:
			Convert_To_Color();
			type = PPMASCII;
			break;
		case PPMRAW:
			Convert_To_Color();
			type = PPMRAW;
			break;
		case PGMASCII:
			type = PGMASCII;
			break;
		case PGMRAW:
			break;
		}
		break;
	}

	// Write the magic number
	buffer[0] = 'P';
	fout.Write(buffer, 1);
	switch (type)
	{
	case PGMASCII:
		buffer[0] = '2';
		break;
	case PPMASCII:
		buffer[0] = '3';
		break;
	case PGMRAW:
		buffer[0] = '5';
		break;
	case PPMRAW:
		buffer[0] = '6';
		break;
	default:
		return false;
	}
	buffer[1] = '\n';
	fout.Write(buffer, 2);

	// Write the width
	itoa(width, buffer, 10);
	i = strlen(buffer);
	buffer[i] = ' ';
	fout.Write(buffer, i+1);

	// Write the height
	itoa(height, buffer, 10);
	i = strlen(buffer);
	buffer[i] = '\n';
	fout.Write(buffer, i+1);

	// Write the maximum value
	itoa(max_value, buffer, 10);
	i = strlen(buffer);
	buffer[i] = '\n';
	fout.Write(buffer, i+1);

	// Write the pixel values
	switch (type)
	{
	case PPMASCII:
		for (i=0; i<height; i++)
		{
			for (int j=0; j<width; j++)
			{
				itoa(red[i][j], buffer, 10);
				fout.Write(buffer, strlen(buffer));
				fout.Write(" ", 1);
				itoa(green[i][j], buffer, 10);
				fout.Write(buffer, strlen(buffer));
				fout.Write(" ", 1);
				itoa(blue[i][j], buffer, 10);
				fout.Write(buffer, strlen(buffer));
				fout.Write(" ", 1);
			}
			fout.Write("\n", 1);
		}
		break;
	case PPMRAW:
		// allocate memory for a horizontal scan line
		line = new unsigned char[width*3];
		for (i=0; i<height; i++)
		{
			// load the line with r,g,b values from the image
			for (int j=0; j<width; j++)
			{
				line[j*3] = (unsigned char)(pixel[i][j] & 0xFF);
				line[j*3+1] = (unsigned char)((pixel[i][j] >> 8) & 0xFF);
				line[j*3+2] = (unsigned char)((pixel[i][j] >> 16) & 0xFF);
			}

			// write the scan line to the file
			fout.Write(line, width*3);
		}
		delete line;
		break;
	case PGMASCII:
		for (i=0; i<height; i++)
		{
			for (int j=0; j<width; j++)
			{
				itoa(gray[i][j], buffer, 10);
				fout.Write(buffer, strlen(buffer));
				fout.Write(" ", 1);
			}
			fout.Write("\n", 1);
		}
		break;
	case PGMRAW:
		// allocate memory for a horizontal scan line
		line = new unsigned char[width];
		for (i=0; i<height; i++)
		{
			// load the line with gray values from the image
			for (int j=0; j<width; j++)
				line[j] = (unsigned char)(pixel[i][j] & 0xFF);

			// write the scan line to the file
			fout.Write(line, width);
		}
		delete line;
		break;
	}
	return true;
}

void PpmPgm::ClearData()
{
	if (pixel != NULL)
    {
		for (int i=0; i<height; i++)
			delete pixel[i];
		delete pixel;
	}
	pixel = NULL;
	ClearColor();
	ClearGray();
}

void PpmPgm::ClearColor()
{
	if (red != NULL)
    {
		for (int i=0; i<height; i++)
			delete red[i];
		delete red;
	}
	if (green != NULL)
    {
		for (int i=0; i<height; i++)
			delete green[i];
		delete green;
	}
	if (blue != NULL)
    {
		for (int i=0; i<height; i++)
			delete blue[i];
		delete blue;
	}
	red = green = blue = NULL;
}

void PpmPgm::ClearGray()
{
	if (gray != NULL)
    {
		for (int i=0; i<height; i++)
			delete gray[i];
		delete gray;
	}
	gray = NULL;
}

void PpmPgm::Fill(unsigned int color)
{
	// do nothing if the image has no pixels 
	if (pixel == NULL)
		return;

	int i;
	unsigned int *redline, *greenline, *blueline, *grayline;
	// line holds one scan line of pixel values
	unsigned int *line = new unsigned int[width];
	// if the image is grayscale then set the r,g,b components of color to be the same
	if ((type == PGMRAW) || (type == PGMASCII))
		color = (color&0xFF) + ((color&0xFF)<<8) + ((color&0xFF)<<16);
	// set the scan line to be all one color
	for (i=0; i<width; i++)
		line[i] = color;

	switch(type)
	{
	case PPMRAW:
	case PPMASCII:
		redline = new unsigned int[width];
		greenline = new unsigned int[width];
		blueline = new unsigned int[width];

		// fill single scan lines for red, green, and blue
		for (i=0; i<width; i++)
		{
			redline[i] = color & 0xFF;
			greenline[i] = (color >> 8) & 0xFF;
			blueline[i] = (color >> 16) & 0xFF;
		}

		// copy the scan line into each row of the image
		for (i=0; i<height; i++)
		{
			memcpy(pixel[i], line, width*sizeof(unsigned int));
			memcpy(red[i], redline, width*sizeof(unsigned int));
			memcpy(green[i], greenline, width*sizeof(unsigned int));
			memcpy(blue[i], blueline, width*sizeof(unsigned int));
		}
		delete redline;
		delete greenline;
		delete blueline;
		break;
	case PGMRAW:
	case PGMASCII:
		grayline = new unsigned int[width];

		// fill the gray scan line
		for (i=0; i<width; i++)
			grayline[i] = color & 0xFF;

		// copy the scan line into each row of the image
		for (i=0; i<height; i++)
		{
			memcpy(pixel[i], line, width*sizeof(unsigned int));
			memcpy(gray[i], grayline, width*sizeof(unsigned int));
		}
		delete grayline;
		break;
	}
	delete line;
}

char PpmPgm::SkipComment(CFile &f)
// this eats characters until a '\n' is encountered
// it is platform dependent
// PC:   '\n' = CR LF
// UNIX: '\n' = CR
// MAC:  '\n' = LF
{
	char ch;
	f.Read(&ch, 1);
	while ((ch != 10) && (ch != 13))
		f.Read(&ch, 1);
	return ch;
}

void PpmPgm::Convert_To_Color()
{
	// If the image is already color, return
	if ((type == PPMRAW) || (type == PPMASCII))
		return;

	// If the gray array doesn't contain any data, return
	if (gray == NULL)
		return;

	ClearColor();
	red = new unsigned int *[height];
	green = new unsigned int *[height];
	blue = new unsigned int *[height];
	for (int i=0; i<height; i++)
    {
		red[i] = new unsigned int[width];
		green[i] = new unsigned int[width];
		blue[i] = new unsigned int[width];
		}
	for (i=0; i<height; i++)
		for (int j=0; j<width; j++)
		{
			red[i][j] = gray[i][j];
			green[i][j] = gray[i][j];
			blue[i][j] = gray[i][j];
			pixel[i][j] = gray[i][j] + (gray[i][j]<<8) + (gray[i][j]<<16);
		}	
	ClearGray();
	type = PPMRAW;
}

void PpmPgm::Convert_To_Gray()
{
	// If the image is already gray, return
	if ((type == PGMRAW) || (type == PGMASCII))
		return;

	// If the color arrays don't contain any data, return
	if ((red == NULL) || (green == NULL) || (blue == NULL))
		return;

	ClearGray();
	gray = new unsigned int *[height];
	for (int i=0; i<height; i++)
		gray[i] = new unsigned int[width];
	for (i=0; i<height; i++)
		for (int j=0; j<width; j++)
		{
			gray[i][j] = (unsigned int)(
				(double)red[i][j]*0.299 +
				(double)green[i][j]*0.587 +
				(double)blue[i][j]*0.114);
			pixel[i][j] = gray[i][j] + (gray[i][j]<<8) + (gray[i][j]<<16);
		}
	ClearColor();
	type = PGMRAW;
}

PpmPgm& PpmPgm::operator=(PpmPgm &p2)
{
	if (this != &p2)
	{
		ClearData();
		width = p2.width;
		height = p2.height;
		max_value = p2.max_value;
		type = p2.type;
		int i;
		switch(type)
		{
		case PPMRAW:
		case PPMASCII:
			red		= new unsigned int *[height];
			green	= new unsigned int *[height];
			blue	= new unsigned int *[height];
			pixel	= new unsigned int *[height];
			for (i=0; i<height; i++)
			{
				red[i]		= new unsigned int[width];
				green[i]	= new unsigned int[width];
				blue[i]		= new unsigned int[width];
				pixel[i]	= new unsigned int[width];
			}
			for (i=0; i<height; i++)
				for (int j=0; j<width; j++)
				{
					red[i][j]	= p2.red[i][j];
					green[i][j] = p2.green[i][j];
					blue[i][j]	= p2.blue[i][j];
					pixel[i][j] = p2.pixel[i][j];
				}
			break;
		case PGMRAW:
		case PGMASCII:
			gray  = new unsigned int *[height];
			pixel = new unsigned int *[height];
			for (i=0; i<height; i++)
			{
				gray[i]  = new unsigned int[width];
				pixel[i] = new unsigned int[width];
			}
			for (i=0; i<height; i++)
				for (int j=0; j<width; j++)
				{
					gray[i][j] = p2.gray[i][j];
					pixel[i][j] = p2.pixel[i][j];
				}
			break;
		}
	}
	return *this;
}

void PpmPgm::SetPixel(int i, int j, unsigned int color)
{
	if ((type == PPMRAW) || (type == PPMASCII))
	{
		pixel[i][j] = color;
		red[i][j] = color & 0xFF;
		green[i][j] = (color >> 8) & 0xFF;
		blue[i][j] = (color >> 16) & 0xFF;
	}
	else
	{
		pixel[i][j] = (color & 0xFF) + ((color & 0xFF) << 8) + ((color & 0xFF) << 16);
		gray[i][j] = color & 0xFF;
	}
}
