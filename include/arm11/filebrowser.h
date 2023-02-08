#pragma once

/*
 *   This file is part of open_agb_firm
 *   Copyright (C) 2021 derrek, profi200
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "error_codes.h"

// Notes on these settings:
// MAX_ENT_BUF_SIZE should be big enough to hold the average file/dir name length * MAX_DIR_ENTRIES.
#define MAX_ENT_BUF_SIZE  (1024u * 196) // 196 KiB.
#define MAX_DIR_ENTRIES   (1000u)
#define DIR_READ_BLOCKS   (10u)
#define SCREEN_COLS       (53u - 1) // - 1 because the console inserts a newline after the last line otherwise.
#define SCREEN_ROWS       (24u)

#define ENT_TYPE_FILE  (0)
#define ENT_TYPE_DIR   (1)

typedef struct
{
	u32 num;                       // Total number of entries.
	char entBuf[MAX_ENT_BUF_SIZE]; // Format: char entryType; char name[X]; // null terminated.
	char *ptrs[MAX_DIR_ENTRIES];   // For fast sorting.
} DirList;


Result browseFiles(const char *const basePath, char selected[512]);
void showDirList(const DirList *const dList, u32 start);
int dlistCompare(const void *a, const void *b);
