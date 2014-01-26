/*
tapecopy.c - Copy data to and from a tape device

Copyright (C) 2000 Luis Carlos Castro Skertchly
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

Version 1.6a:
Tested with LTO 2 (using bsdcpio)
Compiled with MSVS 2013
Some small fixes, rest WIP

*/

#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "TapeLib.h"

#define EXTRACT		1
#define CREATE		2
#define APPEND		3

#define BUFFER_SIZE	655350

HANDLE hSource;
HANDLE hDestination;
HANDLE hCinta;

char devname[MAX_PATH];
char filename[MAX_PATH];
char *source;
char *target;

int action = EXTRACT;

int silent = FALSE;
int taperew = FALSE;
int autodetect = TRUE;

DWORD dflag;
DWORD error;
DWORD status;
DWORD block_size = 0;    /* Block size in tape */
DWORD segment_size = 0;  /* Transfer buffer size */
char  message[1024];
char *szBuffer;
DWORD bytes;
DWORD total;

DWORD dwInfosize;
TAPE_GET_MEDIA_PARAMETERS tapeInfo;
TAPE_SET_MEDIA_PARAMETERS tapeMedia;
TAPE_GET_DRIVE_PARAMETERS driveInfo;
TAPE_SET_DRIVE_PARAMETERS driveSet;

DWORD dwPartition;
DWORD dwOffsetLow;
DWORD dwOffsetHigh;
DWORD dwOffsetLowPrev;
DWORD dwOffsetHighPrev;

WORD wRetryCount;

SYSTEM_INFO sysinfo;

int init(void)
{
	action = 0;
	silent = TRUE;
	filename[0] = 0;
	source = NULL;
	target = NULL;
	strcpy(devname, "\\\\.\\TAPE0");

	return 0;
}

void usage(void)
{
	fprintf(stderr, "Usage: tapecopy [-v] [-b block] [-r] -x|c [-d devname] [-f filename]\n"
		"  -v : Verbose\n"
		"  -b : Specify tape block size in bytes (ex: 65536)\n"
		"  -r : Rewind tape before start\n"
		"  -x : Extract data from tape\n"
		"  -c : Copy data to tape\n"
		"  -d : Specify the tape device name (default = TAPE0)\n"
		"  -f : filename from/to where data will be copied\n"
		"\n"
		"Valid device names are:\n"
		"   TAPE0\n"
		"   TAPE1\n"
		);
	exit(1);
}

int parse_args(int argc, char **argv)
{
	int i;

	if (argc < 2) usage();
	for (i = 1; i < argc; i++)
	{
		if (!strncmp(argv[i], "-v", 2))
		{
			silent = FALSE;
		}
		else if (!strncmp(argv[i], "-b", 2))
		{
			if (!argv[i + 1]) usage();
			block_size = atoi(argv[++i]);
			if (!block_size)
			{
				fprintf(stderr, "The block size specified is invalid.\n");
				exit(1);
			}
			autodetect = FALSE;
			continue;
		}
		else if (!strcmp(argv[i], "-r"))
		{
			taperew = TRUE;
		}
		else if (!strncmp(argv[i], "-x", 2))
		{
			action = EXTRACT;
			dflag = OPEN_ALWAYS;
			source = devname;
			target = filename;
		}
		else if (!strncmp(argv[i], "-c", 2))
		{
			action = CREATE;
			dflag = OPEN_EXISTING;
			source = filename;
			target = devname;
			if (autodetect)
			{
				fprintf(stderr, "A block size must be specified before using -c\n");
				exit(1);
			}
		}
		else if (!strncmp(argv[i], "-d", 2))
		{
			if (!argv[i + 1]) usage();
			sprintf(devname, "\\\\.\\%s", argv[++i]);
			_strupr(devname);
			continue;
		}
		else if (!strncmp(argv[i], "-f", 2))
		{
			if (!argv[i + 1]) usage();
			strncpy(filename, argv[++i], sizeof(filename)-1);
			continue;
		}
		else
			usage();
	}
	if (!action) usage();
	return 0;
}


int print_drive_parameters(TAPE_GET_DRIVE_PARAMETERS *di)
{
	fprintf(stderr, " ECC = %s\n", di->ECC ? "TRUE" : "FALSE");
	fprintf(stderr, " Compression = %s\n", di->Compression ? "TRUE" : "FALSE");
	fprintf(stderr, " DataPadding = %s\n", di->DataPadding ? "TRUE" : "FALSE");
	fprintf(stderr, " ReportSetMarks = %s\n", di->ReportSetmarks ? "TRUE" : "FALSE");
	fprintf(stderr, " Default Block Size = %ld\n", di->DefaultBlockSize);
	fprintf(stderr, " Maximum Block Size = %ld\n", di->MaximumBlockSize);
	fprintf(stderr, " Minimum Block Size = %ld\n", di->MinimumBlockSize);
	fprintf(stderr, " Maximum Partition Count = %ld\n", di->MaximumPartitionCount);
	fprintf(stderr, " EOT Warning Zone Size = %ld\n", di->EOTWarningZoneSize);
	fprintf(stderr, "\n");
	return 0;
}


int print_tape_parameters(TAPE_GET_MEDIA_PARAMETERS *ti)
{
	fprintf(stderr, " Capacity: %lld\n", ti->Capacity);
	fprintf(stderr, " Remaining: %lld\n", ti->Remaining);
	fprintf(stderr, " Block Size: %d\n", ti->BlockSize);
	fprintf(stderr, " Partitions: %d\n", ti->PartitionCount);
	fprintf(stderr, " Write protected: %s\n", ti->WriteProtected ? "Yes" : "No");
	return 0;
}

int main(int argc, char **argv)
{
	init();
	fprintf(stderr, "tapecopy - Version 1.6\nCopyright (C) 2000-2008, Luis C. Castro Skertchly\nCopyright (C) 2014, deep (Catapult Archive Systems)\n");
	parse_args(argc, argv);

	if (!silent) fprintf(stderr, "\n(II) Opening source '%s'...\n", *source ? source : "<stdin>");
	if (*source)
	{
		hSource = CreateFile(source, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, NULL);
		if (hSource == INVALID_HANDLE_VALUE)
		{
			PutError("tapecopy - CreateFile(): ", NULL);
			exit(1);
		}
	}
	else {
		hSource = GetStdHandle(STD_INPUT_HANDLE);
	}
	
	// Got past opening source
	if (!silent) fprintf(stderr, "Ok\n");

	if (!silent) fprintf(stderr, "(II) Opening target '%s'...\n", *target ? target : "<stdout>");
	if (*target)
	{
		hDestination = CreateFile(target, GENERIC_WRITE | GENERIC_READ, 0, 0, dflag, 0, NULL);
		if (hDestination == INVALID_HANDLE_VALUE)
		{
			sprintf(message, "(EE) tapecopy - CreateFile('%s', GENERIC_WRITE)", target);
			PutError(message, NULL);
			exit(1);
		}
	}
	else
	{
		hDestination = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	SetLastError(0);
	
	// Got past opening dest
	if (!silent) fprintf(stderr, "Ok\n");

	if (action == EXTRACT)
		hCinta = hSource;
	else
		hCinta = hDestination;

	if (!silent) fprintf(stderr, "\nTape status:\n");
	error = GetTapeStatus(hCinta);
	if (error != NO_ERROR)
	{
		PutError("tapecopy - GetTapeStatus(): ", NULL);
		exit(1);
	}
	if (!silent) fprintf(stderr, " Tape Ready...\n");

	if (!silent) fprintf(stderr, "\nDrive information:\n");
	dwInfosize = sizeof(driveInfo);
	error = GetTapeParameters(hCinta, GET_TAPE_DRIVE_INFORMATION, &dwInfosize, &driveInfo);
	if (error != NO_ERROR)
	{
		PutError("tapecopy - GetTapeParameters(): ", NULL);
		exit(1);
	}
	if (!silent) print_drive_parameters(&driveInfo);


	if (!silent) fprintf(stderr, "Media information:\n");
	dwInfosize = sizeof(tapeInfo);
	error = GetTapeParameters(hCinta, GET_TAPE_MEDIA_INFORMATION, &dwInfosize, &tapeInfo);
	if (error != NO_ERROR)
	{
		PutError("tapecopy - GetTapeParameters(): ", NULL);
		exit(1);
	}
	if (!silent) print_tape_parameters(&tapeInfo);

	// If ECC/Compression/DataPadding are supported set them to TRUE in 'TAPE_SET_DRIVE_PARAMETERS driveSet'
	memset(&driveSet, 0, sizeof(driveSet));
	if (driveInfo.ECC) driveSet.ECC = TRUE;
	if (driveInfo.Compression) driveSet.Compression = TRUE;
	if (driveInfo.DataPadding) driveSet.DataPadding = TRUE;

	// TAPE_SET_MEDIA_PARAMETERS tapeMedia (set the blocksize given in args)
	tapeMedia.BlockSize = block_size;

	if (action == EXTRACT)
	{
		/* If block size arg is 0 use maximum blocksize (ex 256K LTO1) */
		if (autodetect)
			segment_size = driveInfo.MaximumBlockSize;
		else
		{
			if (!segment_size)
				segment_size = block_size;
		}
	}
	else
	{
		segment_size = block_size;
	}

	if (!silent) fprintf(stderr, "\n(II) Setting tape block size to %d bytes... ", tapeMedia.BlockSize);
	error = SetTapeParameters(hCinta, SET_TAPE_MEDIA_INFORMATION, &tapeMedia);
	if (error != NO_ERROR)
	{
		PutError("tapecopy - SetTapeParameters(): ", NULL);
		exit(1);
	}
	if (!silent) fprintf(stderr, "Ok\n");

	// if -r rewind tape to begin
	if (taperew)
	{
		fprintf(stderr, "(II) Rewinding... ");
		if (PrepareTape(hCinta, TAPE_LOAD, FALSE) != NO_ERROR)
			PutError("tapecopy - PrepareTape(): ", NULL);
		if (SetTapePosition(hCinta, TAPE_REWIND, 0, 0, 0, FALSE) != NO_ERROR)
			PutError("tapecopy - PrepareTape(): ", NULL);
		fprintf(stderr, "Ok\n");
	}

	if (!silent && *source)
	{
		fprintf(stderr, "\nPress ENTER to copy or 'x' and ENTER to exit...");
		if (getchar() == 'x') goto end;
	}

	/* Allocate the buffer, ensuring that it's page-aligned so we
	won't have problems doing i/o to/from a device such as a tape
	or diskette. */
	GetSystemInfo(&sysinfo);
	szBuffer = (char *)(((ULONG)_malloca(segment_size + sysinfo.dwPageSize) +
		sysinfo.dwPageSize) & ~(sysinfo.dwPageSize - 1));

	if (!szBuffer)
	{
		fprintf(stderr, "(EE) There is not enough memory for transfer buffer\n");
		exit(1);
	}

	if (!silent) fprintf(stderr, "(II)  Buffer size: %d bytes\n", segment_size);

	fprintf(stderr, "Copying '%s' to '%s'...\n", *source ? source : "<stdin>", *target ? target : "<stdout>");

	total = 0;
	error = GetTapePosition(hCinta, TAPE_ABSOLUTE_POSITION, &dwPartition, &dwOffsetLow, &dwOffsetHigh);
	if (error != NO_ERROR)
	{
		PutError("tapecopy - GetTapePosition(): ", NULL);
		exit(1);
	}
	
	//Main loop
	while (1)
	{
		bytes = 0;
		dwOffsetLowPrev = dwOffsetLow;
		dwOffsetHighPrev = dwOffsetHigh;
		GetTapePosition(hCinta, TAPE_ABSOLUTE_POSITION, &dwPartition, &dwOffsetLow, &dwOffsetHigh);
		status = ReadFile(hSource, szBuffer, segment_size, &bytes, NULL);
		if (!status)
		{
			if (GetLastError() == ERROR_NO_DATA_DETECTED)
			{
				if (!silent) fprintf(stderr, "\n(II) No more data is on tape.\n");
				break;
			}
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				if (!silent) fprintf(stderr, "\n(II) End of pipe.\n");
				break;
			}
			fprintf(stderr, "\nReadFile() = FALSE; bytes read: %d", bytes);
			if (GetLastError() == ERROR_NOT_READY && (hSource == hCinta))
			{
				LARGE_INTEGER a, b;

				a.HighPart = dwOffsetHighPrev;
				a.LowPart = dwOffsetLowPrev;
				b.HighPart = dwOffsetHigh;
				b.LowPart = dwOffsetLow;
				fprintf(stderr, "\n(EE) Device not ready reading blocks %I64d to %I64d. Retrying.\n", a.QuadPart, b.QuadPart);
				SetTapePosition(hSource, TAPE_ABSOLUTE_BLOCK, 0, dwOffsetLowPrev, dwOffsetHighPrev, FALSE);
				continue;
			}
			PutError("tapecopy - ReadFile(): ", NULL);
			break;
		}
		// no autodetect at this time
		if (!bytes)
		{
			fprintf(stderr, "\n(II) EOF reached\n");
			break;
		}
		if ((bytes < segment_size) && (hDestination == hCinta))
		{
			fprintf(stderr, "(WW): Source file size not an exact multiple of block size. Padding with zeros\n");
			memset(szBuffer + bytes, 0, segment_size - bytes);
			bytes = block_size;
		}
		wRetryCount = 0;
retry_write:
		status = WriteFile(hDestination, szBuffer, bytes, &bytes, NULL);
		if (!status)
		{
			if (GetLastError() == ERROR_NOT_READY && (hDestination == hCinta))
			{
				wRetryCount++;
				if (wRetryCount < 3)
				{
					fprintf(stderr, "\n(WW) Tape not ready while writing data. Retrying...\n");
					goto retry_write;
				}
			}
			fprintf(stderr, "\nWriteFile() = FALSE; bytes writen: %d\n", status, bytes);
			PutError("tapecopy - WriteFile(): ", NULL);
			break;
		}
		total += bytes;
		if (!silent) fprintf(stderr, "%8d KB copied...\r", total / 1024);
	}
end:
	CloseHandle(hSource);
	CloseHandle(hDestination);
	_freea(szBuffer);
	return 0;
}
