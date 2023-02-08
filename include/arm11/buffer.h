#pragma once

#include "types.h"
#include "fs.h"


/**
 * @struct Buffer
 * @brief A structure for storing a buffer
 * 
 * @var buffer
 * An array of bytes currently loaded
 * @var bufferSize
 * The size of the current buffer
 * @var bufferOffset
 * Offset of the next byte for the buffer to read
 * @var maxBufferSize
 * The max number of bytes the buffer can store at one time
*/
typedef struct
{
    u8 *buffer;
    u16 bufferSize;
    u16 bufferOffset;
    u16 maxBufferSize;
} Buffer;

u8 readBuffer(const FHandle fileHandle, Buffer *buffer, Result *res);
Buffer createBuffer(u16 maxBufferSize);
Result loadBuffer(const FHandle fileHandle, Buffer *buff);
void freeBuffer(Buffer *buffer);