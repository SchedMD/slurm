#include "funcs.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "pmandel.h"

double drand48()
{
	return ((double)rand() / (double)RAND_MAX);
}

// fraction is a part of the rainbow (0.0 - 1.0) = (Red-Yellow-Green-Cyan-Blue-Magenta-Red)
// intensity (0.0 - 1.0) 0 = black, 1 = full color, 2 = white
MPE_Color getColor(double fraction, double intensity)
{
	double red, green, blue;

	double dtemp;
	fraction = fabs(modf(fraction, &dtemp));

	if (intensity > 2.0)
		intensity = 2.0;
	if (intensity < 0.0)
		intensity = 0.0;

	dtemp = 1.0/6.0;

	if (fraction < 1.0/6.0)
	{
		red = 1.0;
		green = fraction / dtemp;
		blue = 0.0;
	}
	else
	if (fraction < 1.0/3.0)
	{
		red = 1.0 - ((fraction - dtemp) / dtemp);
		green = 1.0;
		blue = 0.0;
	}
	else
	if (fraction < 0.5)
	{
		red = 0.0;
		green = 1.0;
		blue = (fraction - (dtemp*2.0)) / dtemp;
	}
	else
	if (fraction < 2.0/3.0)
	{
		red = 0.0;
		green = 1.0 - ((fraction - (dtemp*3.0)) / dtemp);
		blue = 1.0;
	}
	else
	if (fraction < 5.0/6.0)
	{
		red = (fraction - (dtemp*4.0)) / dtemp;
		green = 0.0;
		blue = 1.0;
	}
	else
	{
		red = 1.0;
		green = 0.0;
		blue = 1.0 - ((fraction - (dtemp*5.0)) / dtemp);
	}

	if (intensity > 1)
	{
		intensity = intensity - 1.0;
		red = red + ((1.0 - red) * intensity);
		green = green + ((1.0 - green) * intensity);
		blue = blue + ((1.0 - blue) * intensity);
	}
	else
	{
		red = red * intensity;
		green = green * intensity;
		blue = blue * intensity;
	}

	int r,g,b;
	
	r = (int)(red * 255.0);
	g = (int)(green * 255.0);
	b = (int)(blue * 255.0);

	return RGB(r,g,b);
}

int MPE_Make_color_array(MPE_XGraph &graph, int num_colors, MPE_Color colors[])
{
	double fraction, intensity;
	intensity = 1.0;
	for (int i=0; i<num_colors; i++)
	{
		fraction = double(i) / double(num_colors);
		colors[i] = getColor(fraction, intensity);
	}
	return 0;
}

int g_width=0, g_height=0;
HDC g_hDC=NULL;
HGDIOBJ g_hOldBitmap;
HANDLE g_hHDCMutex = CreateMutex(NULL, FALSE, NULL);
bool g_bNoStretch = true;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;
	RECT r, rOuter;
	DWORD flags;

	switch (message) 
	{
	case WM_ERASEBKGND:
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		GetClientRect(hWnd, &r);
		WaitForSingleObject(g_hHDCMutex, INFINITE);
		if (g_bNoStretch)
			BitBlt(hdc, 0, 0, g_width, g_height, g_hDC, 0, 0, SRCCOPY);
		else
			StretchBlt(hdc, 0, 0, r.right, r.bottom, g_hDC, 0, 0, g_width, g_height, SRCCOPY);
		ReleaseMutex(g_hHDCMutex);
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		MPI_Send(0, 0, MPI_INT, MASTER_PROC, WINDOW_CLOSED,	MPI_COMM_WORLD);
		SelectObject(g_hDC, g_hOldBitmap);
		CloseHandle(g_hHDCMutex);
		PostQuitMessage(0);
		break;
	case WM_LBUTTONDOWN:
		GetWindowRect(hWnd, &rOuter);
		GetClientRect(hWnd, &r);
		SetWindowPos(hWnd, HWND_TOP, 0, 0, 
			rOuter.right - rOuter.left - r.right + g_width,
			rOuter.bottom - rOuter.top - r.bottom + g_height, 
			SWP_NOMOVE);
		break;
	case WM_WINDOWPOSCHANGED:
		flags = ((WINDOWPOS*)lParam)->flags;
		//if ( ! (flags & 1) )
		if ( ! (flags & SWP_NOSIZE) )
		{
			//if ( flags & 528 )
			if ( flags & (SWP_NOREPOSITION | SWP_NOACTIVATE) )
				g_bNoStretch = false;
			else
			{
				//if ( flags & 4102)
				if ( flags & (SWP_NOMOVE | SWP_NOZORDER | 4096) )
					g_bNoStretch = true;
			}
		}
		PostMessage(hWnd, WM_PAINT, NULL, NULL);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

void MessageLoopThread(MPE_XGraph &graph)
{
	HWND hWnd;
	MSG msg;
	WNDCLASSEX wcex;
	TCHAR *szWindowClass = TEXT("MPI_MANDEL_WINDOW");
	
	wcex.cbSize = sizeof(WNDCLASSEX); 
	
	//wcex.style			= CS_NOCLOSE;
	//wcex.style			= 0; 
	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= NULL;
	wcex.hIcon			= NULL;
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_BACKGROUND);
	wcex.lpszMenuName	= NULL;
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(NULL, IDI_APPLICATION);
	
	if (!RegisterClassEx(&wcex))
	{
		int error = GetLastError();
		printf("RegisterClassEx failed: %d\n", error);
	}
	
	/*
	hWnd = graph.hWnd = CreateWindow(szWindowClass, TEXT("Mandel"), 
			//WS_OVERLAPPEDWINDOW,
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);
	//*/
	if (g_bNoStretch)
		hWnd = graph.hWnd = CreateWindowEx(
			WS_EX_TOOLWINDOW | WS_EX_APPWINDOW, 
			szWindowClass, TEXT("Mandel"), 
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);
	else
		hWnd = graph.hWnd = CreateWindowEx(
			WS_EX_TOOLWINDOW | WS_EX_APPWINDOW, 
			szWindowClass, TEXT("Mandel"), 
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);

	if (graph.hWnd == NULL)
	{
		int error = GetLastError();
		printf("CreateWindow failed: %d\n", error);
	}

	g_hDC = graph.hDC = CreateCompatibleDC(NULL);
	HBITMAP hBitmap = CreateBitmap(graph.width, graph.height, GetDeviceCaps(g_hDC, PLANES), GetDeviceCaps(g_hDC, BITSPIXEL), NULL);
	g_hOldBitmap = graph.hOldBitmap = SelectObject(graph.hDC, hBitmap);
	SetStretchBltMode(g_hDC, COLORONCOLOR);
	g_width = graph.width;
	g_height = graph.height;

	HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("booglesandboogles"));
	if (hEvent == NULL)
	{
	    printf("CreateEvent failed, error %d\n", GetLastError());fflush(stdout);
	}
	SetEvent(hEvent);
	CloseHandle(hEvent);

	while (GetMessage(&msg, hWnd, 0, 0)) 
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void BringUpWindow(MPE_XGraph &graph)
{
	HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("booglesandboogles"));
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MessageLoopThread, &graph, 0, NULL);
	CloseHandle(hThread);
	if (hEvent == NULL)
	{
	    printf("CreateEvent failed, error %d\n", GetLastError());fflush(stdout);
	}
	WaitForSingleObject(hEvent, INFINITE);
	CloseHandle(hEvent);
	
	ShowWindow(graph.hWnd, SW_SHOW);
	SetWindowPos(graph.hWnd, HWND_TOP, 0, 0, graph.width, graph.height, SWP_SHOWWINDOW | SWP_NOMOVE);
	RECT rOuter, r;
	GetWindowRect(graph.hWnd, &rOuter);
	GetClientRect(graph.hWnd, &r);
	SetWindowPos(graph.hWnd, HWND_TOP, 0, 0, 
		rOuter.right - rOuter.left - r.right + g_width,
		rOuter.bottom - rOuter.top - r.bottom + g_height, 
		SWP_NOMOVE);
	SendMessage(graph.hWnd, WM_PAINT, NULL, NULL);
}

int MPE_Open_graphics(MPE_XGraph *graph, MPI_Comm comm, char *display, int x, int y, int width, int height, int isVisible)
{
	graph->width = width;
	graph->height = height;
	graph->map = new MPE_Color[(width+1) * (height+1)];
	memset(graph->map, 0, (width+1) * (height+1) * sizeof(MPE_Color));
	if (isVisible)
	{
		graph->bVisible = true;
		BringUpWindow(*graph);
	}
	return 0;
}

int MPE_Close_graphics(MPE_XGraph *graph)
{
	if (graph->map)
		delete graph->map;
	graph->map = NULL;
	graph->width = 0;
	graph->height = 0;
	if (graph->bVisible)
	{
		if (IsWindow(graph->hWnd))
			PostMessage(graph->hWnd, WM_DESTROY, NULL, NULL);
	}
	return 0;
}

int MPE_Update(MPE_XGraph &graph)
{
	return 0;
}

int MPE_Draw_point(MPE_XGraph &graph, int x, int y, MPE_Color color)
{
	graph.map[x + y*graph.width] = color;
	return 0;
}

int MPE_Draw_points(MPE_XGraph &graph, MPE_Point *points, int num_points)
{
	for (int i=0; i<num_points; i++)
		graph.map[points[i].x + points[i].y*graph.width] = points[i].c;
	if (graph.bVisible)
	{
		WaitForSingleObject(g_hHDCMutex, INFINITE);
		for (int i=0; i<num_points; i++)
			SetPixelV(g_hDC, points[i].x, points[i].y, points[i].c);
		ReleaseMutex(g_hHDCMutex);
		InvalidateRect(graph.hWnd, 0, TRUE);
	}
	return 0;
}

int MPE_Fill_rectangle(MPE_XGraph &graph, int x, int y, int width, int height, MPE_Color color)
{
	// clip against zero
	if (x < 0)
	{
		width = width + x;
		x = 0;
	}
	if (y < 0)
	{
		height = height + y;
		y = 0;
	}
	// clip against graph.width and graph.height
	if (x+width >= graph.width)
		width = 2*width + x - graph.width - 1;
	if (y+height >= graph.height)
		height = 2*height + y - graph.height - 1;
	// draw the points to the window
	if (graph.bVisible)
	{
		RECT r;
		r.left = x;
		r.right = x + width;
		r.top = y;
		r.bottom = y + height;
		HBRUSH hBrush = CreateSolidBrush(color);
		HGDIOBJ hOldBrush = SelectObject(g_hDC, hBrush);
		WaitForSingleObject(g_hHDCMutex, INFINITE);
		FillRect(g_hDC, &r, hBrush);
		ReleaseMutex(g_hHDCMutex);
		SelectObject(g_hDC, hOldBrush);
		InvalidateRect(graph.hWnd, 0, TRUE);
	}
	else
	{
		// draw the points to the memory buffer
		for (int i=x; i<x+width; i++)
			for (int j=y; j<y+height; j++)
				graph.map[i + j*graph.width] = color;
	}
	return 0;
}
