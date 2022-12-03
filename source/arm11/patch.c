/*
 *   This file is part of open_agb_firm
 *   Copyright (C) 2022 spitzeqc
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

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "oaf_error_codes.h"
#include "util.h"
#include "arm11/drivers/hid.h"
#include "drivers/lgy.h"
#include "arm11/fmt.h"
#include "fs.h"
#include "arm11/patch.h"
#include "arm11/power.h"
#include "drivers/sha.h"
#include "arm11/buffer.h"
#include "arm11/filebrowser.h"
#include "drivers/gfx.h"
#include "arm11/drivers/codec.h"

#define MAX_PATH_SIZE 512
#define MAX_BUFFER_SIZE 512
#define PATCH_PATH_BASE "sdmc:/3ds/open_agb_firm/patches"

#define endOffset(path, offset) path+strlen(path)-offset

/**
 * @brief Apply an IPS patch
 * 
 * @details Used for applying an IPS patch to a game. Rom MUST be loaded into memory before use. Returns RES_OK if no errors occurred or 
 *          RES_INVALID_PATCH if file is not an IPS patch (patch is not applied to game), returns a Result of the error otherwise
 * 
 * @param[in] patchHandle   An FHandle of the patch file
 * 
 * @return Result of operation
 */
static Result patchIPS(const FHandle patchHandle) {
	Result res = RES_OK;

	Buffer buff = createBuffer(MAX_BUFFER_SIZE);
	if(buff.buffer == NULL) return RES_OUT_OF_MEM;

	//verify patch is IPS patch (magic number "PATCH")
	bool isValidPatch = false;
	res = loadBuffer(patchHandle, &buff);
	if(res == RES_OK) {
		char temp[5];
		for (u8 i=0; i<5; ++i)
			temp[i] = readBuffer(patchHandle, &buff, &res);
		if(memcmp("PATCH", temp, 5) == 0) {
			isValidPatch = true;
		} else {
			res = RES_INVALID_PATCH;
		}
	}

	if(isValidPatch) {
		ee_puts("IPS patch found! Patching...");
		u32 offset = 0;
		u16 length = 0;
		char miniBuffer[3]; //scratch for reading offset, length, and RLE hunks

		while(res == RES_OK) {
			//Read offset
			for(u8 i=0; i<3; ++i) {
				miniBuffer[i] = readBuffer(patchHandle, &buff, &res);
				if(res != RES_OK) break;
			}
			if (res != RES_OK || memcmp("EOF", miniBuffer, 3)==0) break;
			offset = (miniBuffer[0]<<16) + (miniBuffer[1]<<8) + (miniBuffer[2]);

			//read length
			for(u8 i=0; i<2; ++i) {
				miniBuffer[i] = readBuffer(patchHandle, &buff, &res);
				if(res != RES_OK) break;
			}
			if(res != RES_OK) break;
			length = (miniBuffer[0]<<8) + (miniBuffer[1]);

			//RLE hunk
			if(length == 0) {
				for(u8 i=0; i<3; ++i) {
					miniBuffer[i] = readBuffer(patchHandle, &buff, &res);
					if(res != RES_OK) break;
				}
				if(res != RES_OK) break;

				u16 tempLen = (miniBuffer[0]<<8) + (miniBuffer[1]);
				memset((void*)(ROM_LOC + offset), miniBuffer[2], tempLen*sizeof(char));
			}

			//regular hunks
			else {
				for(u16 i=0; i<length; ++i) {
					*(char*)(ROM_LOC + offset + i) = readBuffer(patchHandle, &buff, &res);
					if(res != RES_OK) break;
				}
			}
		}
	}

	freeBuffer(&buff);

	return res;
}

/**
 * @brief Reads a variable width integer
 * 
 * @param[in]     patchFile   FHandle of the patch file
 * @param[in,out] res         Address of a Result
 * @param[in,out] buff        Address of Buffer for the patch file
 * 
 * @return uintmax_t of the read integer
 */
//based on code from http://fileformats.archiveteam.org/wiki/UPS_(binary_patch_format) (CC0, No copyright)
static uintmax_t read_vuint(const FHandle patchFile, Result *res, Buffer *buff) {
	uintmax_t result = 0, shift = 0;

	uint8_t octet = 0;
	while(1) {
		//*res = fRead(patchFile, &octet, 1, NULL);
        octet = readBuffer(patchFile, buff, res);
		if(*res != RES_OK) break;
		if(octet & 0x80) {
			result += (octet & 0x7f) << shift;
			break;
		}
		result += (octet | 0x80) << shift;
		shift += 7;
	}

	return result;
}

/**
 * @brief Apply an UPS patch
 * 
 * @details Used for applying a UPS patch to a game. Rom MUST be loaded into memory before use. If patched size is larger than the current rom size, romSize is updated.
 *          Returns RES_OK if no errors occurred or RES_INVALID_PATCH if file is not an UPS patch (patch is not applied to game), returns a Result of the error otherwise
 * 
 * @param[in]     patchHandle   An FHandle of the patch file
 * @param[in,out] romSize       Address of current size of the loaded rom
 * 
 * @return Result of operation
 */
static Result patchUPS(const FHandle patchHandle, u32 *romSize) {
	if (romSize == NULL) return RES_INVALID_ARG;

	Result res = RES_OK;

	Buffer buff = createBuffer(MAX_BUFFER_SIZE);
	if(buff.buffer == NULL) {
		return RES_OUT_OF_MEM;
	}

	//read data into buffer for first time
	res = loadBuffer(patchHandle, &buff);
	if(res != RES_OK) { freeBuffer(&buff); return res; }

	//verify patch is UPS (magic number is "UPS1")
	u8 magic[] = {0x00, 0x00, 0x00, 0x00}; 
	bool isValidPatch = false;
	for(u8 i=0; i<4; i++) {
		magic[i] = readBuffer(patchHandle, &buff, &res);
		if(res != RES_OK) break;
	}
	
	if(res == RES_OK) {
		if(memcmp(&magic, "UPS1", 4) == 0) {
			isValidPatch = true;
		} else {
			res = RES_INVALID_PATCH;
		}
	}

	if(isValidPatch) {
		ee_puts("UPS patch found! Patching...");

        //get rom size
		u32 baseRomSize = (u32)read_vuint(patchHandle, &res, &buff);
		if(res != RES_OK) { freeBuffer(&buff); return res; }
		//get patched rom size
		u32 patchedRomSize = (u32)read_vuint(patchHandle, &res, &buff);
		if(res != RES_OK) { freeBuffer(&buff); return res; }

        debug_printf("Base size:    0x%lx\nPatched size: 0x%lx\n", baseRomSize, patchedRomSize);

        if(patchedRomSize > baseRomSize) {
			//scale up rom
			*romSize = nextPow2(patchedRomSize);
			//check if upscaled rom is too big
			if(*romSize > MAX_ROM_SIZE) {
				ee_puts("Patched ROM exceeds 32MB! Skipping patching...");
				free(buff.buffer);
				return RES_INVALID_PATCH; 
			}

			memset((char*)(ROM_LOC + baseRomSize), 0xFFu, *romSize - baseRomSize); //fill out extra rom space
			memset((char*)(ROM_LOC + baseRomSize), 0x00u, patchedRomSize - baseRomSize); //fill new patch area with 0's
		}

        uintmax_t patchFileSize = fSize(patchHandle);

        uintmax_t offset = 0;
		u8 readByte = 0;
		u8 *romBytes = ((u8*)ROM_LOC);

        while(fTell(patchHandle) < (patchFileSize-12) && res==RES_OK) {
            offset += read_vuint(patchHandle, &res, &buff);
            if(res != RES_OK) break;

			while(offset<*romSize) {
				readByte = readBuffer(patchHandle, &buff, &res);
                if(res != RES_OK) break;

				if(readByte == 0x00) {
					offset++;
					break; 
				}
				romBytes[offset] ^= readByte;
				offset++;
			}
		}
        
    }

	freeBuffer(&buff);

	return res;
}

/**
 * @brief Applies provided patch to rom
 * 
 * @param[in]     patchPath   FHandle of patch file
 * @param[in,out] romSize     Size of currently loaded rom
 * 
 * @return Result of operation
 */
static Result applyPatch(FHandle patchFile, u32 *romSize) {
	Result res = RES_OK;
	res = fLseek(patchFile, 0);
	if (res != RES_OK) return res;

	if((res = patchIPS(patchFile)), res == RES_OK) {
		return res;
	}
	else if(res != RES_INVALID_PATCH) {
		ee_puts("An error has occurred while patching.\nContinuing is NOT recommended!\n\nPress Y+UP to proceed");
#ifndef NDEBUG
		ee_printf("Error Code: 0x%lX", res);
#endif
		while(1){
			hidScanInput();
			if(hidKeysHeld() == (KEY_Y | KEY_DUP) && hidKeysDown() != 0) break;
			if(hidGetExtraKeys(0) & (KEY_POWER_HELD | KEY_POWER)) power_off();
		}

		return res;
	}

	//reset file position
	res = fLseek(patchFile, 0);
	if (res != RES_OK) return res;

	if((res = patchUPS(patchFile, romSize)), res == RES_OK) {
		return res;
	}
	else if(res != RES_INVALID_PATCH) {
		ee_puts("An error has occurred while patching.\nContinuing is NOT recommended!\n\nPress Y+UP to proceed");
#ifndef NDEBUG
		ee_printf("Error Code: 0x%lX", res);
#endif
		while(1){
			hidScanInput();
			if(hidKeysHeld() == (KEY_Y | KEY_DUP) && hidKeysDown() != 0) break;
			if(hidGetExtraKeys(0) & (KEY_POWER_HELD | KEY_POWER)) power_off();
		}

		return res;
	}

	return res;
}

/**
 * @brief Scan for ONLY FILES in a given directory
 * 
 * @param[in]     path     Path to scan
 * @param[in,out] dList    DirList to store detected files
 * @param [in]    filter   File extension to search for
 * 
 * @return Result of operation
 */
Result scanFiles(const char *const path, DirList *const dList, const char *const filter)
{
	FILINFO *const fis = (FILINFO*)malloc(sizeof(FILINFO) * DIR_READ_BLOCKS);
	if(fis == NULL) return RES_OUT_OF_MEM;

	dList->num = 0;

	Result res;
	DHandle dh;
	if((res = fOpenDir(&dh, path)) == RES_OK)
	{
		u32 read;           // Number of entries read by fReadDir().
		u32 numEntries = 0; // Total number of processed entries.
		u32 entBufPos = 0;  // Entry buffer position/number of bytes used.
		const u32 filterLen = strlen(filter);
		do
		{
			if((res = fReadDir(dh, fis, DIR_READ_BLOCKS, &read)) != RES_OK) break;
			read = (read <= MAX_DIR_ENTRIES - numEntries ? read : MAX_DIR_ENTRIES - numEntries);

			for(u32 i = 0; i < read; i++)
			{
				if(fis[i].fattrib & AM_DIR) continue; //skip over any directory
				const char entType = ENT_TYPE_FILE;
				const u32 nameLen = strlen(fis[i].fname);
				if(nameLen <= filterLen || strcmp(filter, fis[i].fname + nameLen - filterLen) != 0)
					continue;
				

				// nameLen does not include the entry type and NULL termination.
				if(entBufPos + nameLen + 2 > MAX_ENT_BUF_SIZE) goto scanEnd;

				char *const entry = &dList->entBuf[entBufPos];
				*entry = entType;
				safeStrcpy(&entry[1], fis[i].fname, 256);
				dList->ptrs[numEntries++] = entry;
				entBufPos += nameLen + 2;
			}
		} while(read == DIR_READ_BLOCKS);

scanEnd:
		dList->num = numEntries;

		fCloseDir(dh);
	}

	free(fis);

	qsort(dList->ptrs, dList->num, sizeof(char*), dlistCompare);

	return res;
}

/**
 * @brief Run patching logic
 * 
 * @details Looks for a potential patch file to apply. If single patch and patch folder are present, preference is given to the single file.
 *          If single IPS and single UPS are present, preference is given to IPS file. Returns RES_OK if no errors, returns Result of error otherwise
 * 
 * @param[in]     gamePath   Path of the loaded rom
 * @param[in,out] romSize    Size of currently loaded rom
 * @param[in,out] savePath   Path fo the game save file
 * 
 * @return Result of operation
 */
Result patchRom(const char *const gamePath, u32 *romSize, char* savePath) {
	Result res = RES_OK;
	FHandle patchFile;

	//OPTIMIZE TO REMOVE patchPath
	char *patchPath = (char*)calloc(MAX_PATH_SIZE, 1);
	char *workingPath = (char*)calloc(MAX_PATH_SIZE, 1);

	if(patchPath != NULL && workingPath != NULL) {
		strcpy(workingPath, gamePath);
		memset(endOffset(workingPath, 3), '\0', 3); //replace 'gba' with '\0' characters

		// Check for single patch file
		if((res = fOpen(&patchFile, strncat(workingPath, "ips", MAX_PATH_SIZE-1), FA_OPEN_EXISTING | FA_READ)) == RES_OK) {
			res = applyPatch(patchFile, romSize);
			fClose(patchFile);
			goto cleanup;
		}
		else if(( *(endOffset(workingPath, 3)) = '\0', res = fOpen(&patchFile, strncat(workingPath, "ups", MAX_PATH_SIZE-1), FA_OPEN_EXISTING | FA_READ)) == RES_OK) {
			res = applyPatch(patchFile, romSize);
			fClose(patchFile);
			goto cleanup;
		}
		else if(( *(endOffset(workingPath, 3)) = '\0', res = fOpen(&patchFile, strncat(workingPath, "patch", MAX_PATH_SIZE-1), FA_OPEN_EXISTING | FA_READ)) == RES_OK) {
			res = applyPatch(patchFile, romSize);
			fClose(patchFile);
			goto cleanup;
		}


		// Check patch folder
		//get path of patch folder
		size_t breakPos = strlen(gamePath);
		if (gamePath[breakPos] == '/' && breakPos != 0) breakPos--; //if end of path is a '/', then we want to remove it
		for(; gamePath[breakPos] != '/' && breakPos != 0; --breakPos); //break pos *should* never reach 0 ("sdmc:" is part of path), but better safe than sorry

		//if breakPos *does* manage to reach 0, something has gone wrong
		if(breakPos == 0) {
			ee_puts("An unexpected error has occurred!");
			res = RES_FR_INT_ERR;
			goto cleanup;
		}

		//create new workingPath
		char *gameName = malloc( strlen(gamePath)-breakPos+1 );
		strncpy(gameName, gamePath+breakPos, strlen(gamePath)-breakPos+1);
		strncpy(workingPath, PATCH_PATH_BASE, MAX_PATH_SIZE-1);
		strncat(workingPath, gameName, MAX_PATH_SIZE-1);
		
		*(endOffset(workingPath, 4)) = '\0';

		DirList *const patchList = (DirList*)malloc(sizeof(DirList));
		if(patchList == NULL) {
			res = RES_OUT_OF_MEM;
			goto cleanup;
		}

		//check if patch folder exists
		DHandle tempDir;
		if((res = fOpenDir(&tempDir, workingPath)) != RES_OK) {
			ee_printf("Bad directory: %s\n", workingPath);
			free(patchList);
			if(res == RES_FR_NO_PATH) res = RES_OK;
			goto cleanup; 
		}
		fCloseDir(tempDir);

		//get all ".patch" files
		if((res = scanFiles(workingPath, patchList, ".patch")) != RES_OK) {
			free(patchList);
			goto cleanup;
		}
		
		//Open patch browser
		if((patchList->num) == 0) {
			free(patchList);
			goto cleanup;
		}

		//Pretty much all of this code is a copy of browseFiles(), may be able to remove it with slight changes to browseFiles()
		s32 cursorPos = 0;
		s32 oldCursorPos = 0;
		u32 windowPos = 0;
		showDirList(patchList, 0);

		u32 kDown = 0;
		while (1) {
			ee_printf("\x1b[%lu;H ", oldCursorPos - windowPos);      // Clear old cursor.
			ee_printf("\x1b[%lu;H\x1b[37m>", cursorPos - windowPos); // Draw cursor.

			do
			{
				GFX_waitForVBlank0();

				hidScanInput();
				if(hidGetExtraKeys(0) & (KEY_POWER_HELD | KEY_POWER)) {
					CODEC_deinit();
					GFX_deinit();
					fUnmount(FS_DRIVE_SDMC);

					power_off();
				}
				kDown = hidKeysDown();
			} while(kDown == 0);

			oldCursorPos = cursorPos;

			if(kDown & KEY_A) {
				ee_printf("\x1b[2J"); //clear screen
				//open file
				FHandle patch;
				strncpy(patchPath, strncat(workingPath, "/", MAX_PATH_SIZE-1), MAX_PATH_SIZE);

				strncat(patchPath, &patchList->ptrs[cursorPos][1], MAX_PATH_SIZE-1);
				if((res = fOpen(&patch, patchPath, FA_OPEN_EXISTING | FA_READ)) != RES_OK) break;
				
				res = applyPatch(patch, romSize);
				if (res != RES_OK && res != RES_INVALID_PATCH) {
					fClose(patch);
					break;
				}

				//adjust save path to prevent patched save conflicts
				strncpy(savePath, workingPath, MAX_PATH_SIZE-1);
				strncat(savePath, "saves/", MAX_PATH_SIZE-1);
				strncat(savePath, &patchList->ptrs[cursorPos][1], MAX_PATH_SIZE-1);
				*(endOffset(savePath, 5)) = '\0';
				strncat(savePath, "sav", MAX_PATH_SIZE-1);

				res = fClose(patch);
				break;
			}
			if(kDown & KEY_X) break;

			if(kDown & KEY_DRIGHT)
			{
				cursorPos += SCREEN_ROWS;
				if((u32)cursorPos > (patchList->num)) cursorPos = (patchList->num) - 1;
			}
			if(kDown & KEY_DLEFT)
			{
				cursorPos -= SCREEN_ROWS;
				if(cursorPos < -1) cursorPos = 0;
			}

			if(kDown & KEY_DDOWN) cursorPos++;
			if(kDown & KEY_DUP) cursorPos--;

			if(cursorPos < 0) cursorPos = (patchList->num) - 1; //wrap at beginning
			if((u32)cursorPos >= (patchList->num)) cursorPos = 0; //wrap at end

			if((u32)cursorPos < windowPos)
			{
				windowPos = cursorPos;
				showDirList(patchList, windowPos);
			}
			if((u32)cursorPos >= windowPos + SCREEN_ROWS)
			{
				windowPos = cursorPos - (SCREEN_ROWS - 1);
				showDirList(patchList, windowPos);
			}

		}

		free(patchList);
	}
	else res = RES_OUT_OF_MEM;

cleanup:
	free(patchPath);
	free(workingPath);

	if(res == RES_INVALID_PATCH) {
		ee_puts("No valid patch found! Skipping...\n");
	} 
#ifndef NDEBUG	
	else {
		u64 sha1[3];
		sha((u32*)ROM_LOC, *romSize, (u32*)sha1, SHA_IN_BIG | SHA_1_MODE, SHA_OUT_BIG);
		debug_printf("New hash: '%016" PRIX64 "'\n", __builtin_bswap64(sha1[0]));
	}
#endif

	return res;
}