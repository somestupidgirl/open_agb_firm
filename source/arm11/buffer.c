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
#include "arm11/buffer.h"
#include "fs.h"
#include "types.h"
#include "util.h"

/**
 * @brief Read a byte from a buffer
 * 
 * @details Used for reading a byte out of the provided buffer. The buffer should first be loaded using loadBuffer. When the end of the buffer, the buffer will be refilled using the 
 * 
 * @param[in]     fileHandle   An FHandle of the file in the buffer 
 * @param[in,out] buff         Address of the Buffer to read from
 * @param[in,out] res          Address of a Result
 * 
 * @return Next byte in the buffer
 * 
 * @see{loadBuffer}
 */
u8 readBuffer(const FHandle fileHandle, Buffer *buff, Result *res) {
    if(buff->bufferSize == 0) {
        *res = RES_OUT_OF_RANGE;
        return 0;
    }

    u8 result = (buff->buffer)[(buff->bufferOffset)++];
    if((buff->bufferOffset) >= (buff->bufferSize)) {
        (buff->bufferSize) = min((buff->maxBufferSize), ((fSize(fileHandle)) - fTell(fileHandle)));
        *res = fRead(fileHandle, (buff->buffer), (buff->bufferSize), NULL);
        (buff->bufferOffset) = 0;
    }

    return result;
}

/**
 * @brief Create an empty buffer
 * 
 * @param[in] maxBufferSize   Maximum number of bytes the buffer can hold
 * 
 * @return Buffer of size maxBufferSize
 * 
 * @see Buffer 
 */
Buffer createBuffer(u16 maxBufferSize) {
    Buffer ret = {
		(u8*)calloc(maxBufferSize, 1), //buffer
		0,                             //buffer size
		0,                             //buffer offset
		maxBufferSize                  //max buffer size
	};

    return ret;
}

/**
 * @brief Load a buffer for the first time
 * 
 * @param[in]     fileHandle   FHandle of the file to read from
 * @param[in,out] buff         Buffer to read into
 * 
 * @return Result, returns RES_OK if no errors occurred, otherwise a Result of the error is returned
 * 
 * @see Buffer
 */
Result loadBuffer(const FHandle fileHandle, Buffer *buff) {
    buff->bufferSize = min(buff->maxBufferSize, (fSize(fileHandle))-fTell(fileHandle));
	Result res = fRead(fileHandle, buff->buffer, buff->bufferSize, NULL);
	return res;
}

/**
 * @brief Free the memory of a buffer
 * 
 * @param[in,out] buff   Address of the Buffer to free
 * 
 * @see Buffer
 */
inline void freeBuffer(Buffer *buff) {
    free(buff->buffer);
}