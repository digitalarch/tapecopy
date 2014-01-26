/*

luislib.c -  Rutinas diversas
TapeLib.c - Common routines

Copyright (C) 1999-2002 Luis C. Castro Skertchly
Copyright (C) 2014 darkdata (Catapult Archive Systems)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

The original source was obtained from: http://www.tecno-notas.com/winnt.htm

*/

#define _CRT_SECURE_NO_DEPRECATE
#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <stdlib.h>

DWORD PutError(char *name, char *extra)
{
	DWORD ErrorCode;
	LPVOID lpMsgBuf;
	ErrorCode = GetLastError();

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK | FORMAT_MESSAGE_ARGUMENT_ARRAY,
		NULL,
		ErrorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0,
		NULL);

	_tprintf("%s - %s\n", name, (LPCTSTR)lpMsgBuf);

	LocalFree(lpMsgBuf);
	
	return ErrorCode;
}