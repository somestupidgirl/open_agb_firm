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
	ee_puts("IPS patch found! Patching...");

	Buffer buff = createBuffer(512);
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
	Result res = RES_OK;

	Buffer buff = createBuffer(512);
	if(buff.buffer == NULL) {
		return RES_OUT_OF_MEM;
	}

	//read data into buffer for first time
	res = loadBuffer(patchHandle, &buff);
	if(res != RES_OK) { freeBuffer(&buff); return res; }

	ee_puts("UPS patch found! Patching...");

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
 * @brief Applies a patch file to a rom
 * 
 * @details Looks for a potential patch file to apply. If both an IPS and UPS patch are found, preference is given to the IPS patch.
 *          Returns RES_OK if no errors, returns Result of error otherwise
 * 
 * @param[in]     gamePath   String of the loaded rom
 * @param[in,out] romSize    Size of currently loaded rom
 * 
 * @return Result of operation
 */
Result patchRom(const char *const gamePath, u32 *romSize) {
	Result res = RES_OK;

	//get base path for game with 'gba' extension removed
	int gamePathLength = strlen(gamePath) + 1; //add 1 for '\0' character
	const int extensionOffset = gamePathLength-4;
	char *patchPathBase = (char*)calloc(gamePathLength, 1);

	char *patchPath = (char*)calloc(512, 1);

	if(patchPathBase != NULL && patchPath != NULL) {
		strcpy(patchPathBase, gamePath);
		memset(patchPathBase+extensionOffset, '\0', 3); //replace 'gba' with '\0' characters
		
		FHandle f;
		//check if patch file is present. If so, call appropriate patching function
		if((res = fOpen(&f, strcat(patchPathBase, "ips"), FA_OPEN_EXISTING | FA_READ)) == RES_OK)
		{
			res = patchIPS(f);

			if(res != RES_OK && res != RES_INVALID_PATCH) {
				ee_puts("An error has occurred while patching.\nContinuing is NOT recommended!\n\nPress Y+UP to proceed");
				while(1){
					hidScanInput();
					if(hidKeysHeld() == (KEY_Y | KEY_DUP) && hidKeysDown() != 0) break;
					if(hidGetExtraKeys(0) & (KEY_POWER_HELD | KEY_POWER)) power_off();
				}
			}

			fClose(f);
			goto cleanup;
		}
		//reset patchPathBase
		memset(patchPathBase+extensionOffset, '\0', 3);
		
		if ((res = fOpen(&f, strcat(patchPathBase, "ups"), FA_OPEN_EXISTING | FA_READ)) == RES_OK) 
		{
			res = patchUPS(f, romSize);

			if(res != RES_OK && res != RES_INVALID_PATCH) {
				ee_puts("An error has occurred while patching.\nContinuing is NOT recommended!\n\nPress Y+UP to proceed");
				while(1){
					hidScanInput();
					if(hidKeysHeld() == (KEY_Y | KEY_DUP) && hidKeysDown() != 0) break;
					if(hidGetExtraKeys(0) & (KEY_POWER_HELD | KEY_POWER)) power_off();
				}
			}

			fClose(f);
			goto cleanup;
		}

	} else {
		res = RES_OUT_OF_MEM;
	}

cleanup:
	//cleanup our resources
	free(patchPath);
	free(patchPathBase);

	if(res == RES_INVALID_PATCH) {
		ee_puts("Patch is not valid! Skipping...\n");
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
