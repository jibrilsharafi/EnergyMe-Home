// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "ringbufferstream.h"
#include "customlog.h"

RingBufferStream::RingBufferStream()
    : buffer(nullptr),
      bufferSize(RING_BUFFER_SIZE),
      writePos(0),
      readPos(0),
      eofFlag(false),
      errorFlag(false),
      mutex(nullptr),
      dataAvailable(nullptr),
      spaceAvailable(nullptr)
{
    // Allocate ring buffer in PSRAM
    buffer = (uint8_t*)ps_malloc(bufferSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate %zu bytes in PSRAM for RingBufferStream", bufferSize);
        return;
    }

    // Create semaphores for synchronization
    mutex = xSemaphoreCreateMutex();
    dataAvailable = xSemaphoreCreateBinary();
    spaceAvailable = xSemaphoreCreateBinary();

    if (!mutex || !dataAvailable || !spaceAvailable) {
        LOG_ERROR("Failed to create semaphores for RingBufferStream");
        if (buffer) {
            free(buffer);
            buffer = nullptr;
        }
        if (mutex) vSemaphoreDelete(mutex);
        if (dataAvailable) vSemaphoreDelete(dataAvailable);
        if (spaceAvailable) vSemaphoreDelete(spaceAvailable);
        return;
    }

    // Initially there's space available but no data
    xSemaphoreGive(spaceAvailable);

    LOG_DEBUG("RingBufferStream created: %zu bytes in PSRAM", bufferSize);
}

RingBufferStream::~RingBufferStream() {
    if (buffer) {
        free(buffer);
    }
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
    if (dataAvailable) {
        vSemaphoreDelete(dataAvailable);
    }
    if (spaceAvailable) {
        vSemaphoreDelete(spaceAvailable);
    }
    LOG_DEBUG("RingBufferStream destroyed");
}

int RingBufferStream::available() {
    if (!buffer || errorFlag) {
        return 0;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        size_t avail = availableForRead();
        xSemaphoreGive(mutex);
        return static_cast<int>(avail);
    }

    return 0;
}

int RingBufferStream::read() {
    if (!buffer || errorFlag) {
        return -1;
    }

    // Wait for data to be available
    if (xSemaphoreTake(dataAvailable, pdMS_TO_TICKS(RING_BUFFER_TIMEOUT_MS)) != pdTRUE) {
        // Timeout - check if EOF
        if (eofFlag && available() == 0) {
            return -1; // EOF reached
        }
        LOG_WARNING("RingBufferStream read timeout");
        return -1;
    }

    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (availableForRead() == 0) {
            // No data available (shouldn't happen after dataAvailable signaled, but check anyway)
            xSemaphoreGive(mutex);
            return -1;
        }

        // Read one byte
        uint8_t byte = buffer[readPos];
        readPos = (readPos + 1) % bufferSize;

        // If there's still data, give dataAvailable back so next read doesn't block
        if (availableForRead() > 0) {
            xSemaphoreGive(dataAvailable);
        }

        xSemaphoreGive(mutex);

        // Signal producer that space is now available
        xSemaphoreGive(spaceAvailable);

        return byte;
    }

    return -1;
}

size_t RingBufferStream::readBytes(uint8_t* dest, size_t length) {
    if (!buffer || !dest || length == 0 || errorFlag) {
        return 0;
    }

    size_t totalRead = 0;

    while (totalRead < length) {
        // Check if EOF is set and buffer is empty (before semaphore wait to avoid blocking)
        if (eofFlag) {
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                size_t avail = availableForRead();
                xSemaphoreGive(mutex);
                if (avail == 0) {
                    break; // EOF reached and buffer empty, return what we've read so far
                }
            } else {
                // Couldn't acquire mutex quickly, try to continue
                break;
            }
        }

        // Wait for data to be available (with timeout)
        if (xSemaphoreTake(dataAvailable, pdMS_TO_TICKS(RING_BUFFER_TIMEOUT_MS)) != pdTRUE) {
            // Timeout - check if EOF and buffer empty (double-check)
            if (eofFlag) {
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (availableForRead() == 0) {
                        xSemaphoreGive(mutex);
                        break; // EOF reached, return what we've read so far
                    }
                    xSemaphoreGive(mutex);
                }
            }
            // Timeout without EOF or data still available
            if (totalRead > 0) {
                break; // Return partial data
            }
            LOG_WARNING("RingBufferStream readBytes timeout");
            break;
        }

        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            size_t avail = availableForRead();

            if (avail == 0) {
                // No data available (shouldn't happen, but handle gracefully)
                xSemaphoreGive(mutex);
                break;
            }

            // Read as much as possible in one go (up to contiguous block)
            size_t toRead = length - totalRead;
            if (toRead > avail) {
                toRead = avail;
            }

            // Handle ring buffer wrap-around
            size_t firstChunk = bufferSize - readPos;
            if (toRead <= firstChunk) {
                // Read contiguous block
                memcpy(dest + totalRead, buffer + readPos, toRead);
                readPos = (readPos + toRead) % bufferSize;
                totalRead += toRead;
            } else {
                // Read split into two chunks (wrap around)
                memcpy(dest + totalRead, buffer + readPos, firstChunk);
                size_t secondChunk = toRead - firstChunk;
                memcpy(dest + totalRead + firstChunk, buffer, secondChunk);
                readPos = secondChunk;
                totalRead += toRead;
            }

            // If there's still data available, give dataAvailable back
            if (availableForRead() > 0) {
                xSemaphoreGive(dataAvailable);
            }

            xSemaphoreGive(mutex);

            // Signal producer that space is now available
            xSemaphoreGive(spaceAvailable);
        }
    }

    return totalRead;
}

int RingBufferStream::peek() {
    if (!buffer || errorFlag) {
        return -1;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (availableForRead() == 0) {
            xSemaphoreGive(mutex);
            return -1;
        }

        uint8_t byte = buffer[readPos];
        xSemaphoreGive(mutex);
        return byte;
    }

    return -1;
}

size_t RingBufferStream::write(uint8_t byte) {
    if (!buffer) {
        return 0;
    }

    // Wait for space to be available
    if (xSemaphoreTake(spaceAvailable, pdMS_TO_TICKS(RING_BUFFER_TIMEOUT_MS)) != pdTRUE) {
        LOG_WARNING("RingBufferStream write timeout (buffer full)");
        return 0;
    }

    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        if (availableForWrite() == 0) {
            // No space (shouldn't happen after spaceAvailable signaled)
            xSemaphoreGive(mutex);
            return 0;
        }

        // Write one byte
        buffer[writePos] = byte;
        writePos = (writePos + 1) % bufferSize;

        // If there's still space, give spaceAvailable back
        if (availableForWrite() > 0) {
            xSemaphoreGive(spaceAvailable);
        }

        xSemaphoreGive(mutex);

        // Signal consumer that data is now available
        xSemaphoreGive(dataAvailable);

        return 1;
    }

    return 0;
}

size_t RingBufferStream::write(const uint8_t* data, size_t size) {
    if (!buffer || !data || size == 0) {
        return 0;
    }

    size_t totalWritten = 0;

    while (totalWritten < size) {
        // Wait for space to be available (with timeout)
        if (xSemaphoreTake(spaceAvailable, pdMS_TO_TICKS(RING_BUFFER_TIMEOUT_MS)) != pdTRUE) {
            LOG_WARNING("RingBufferStream write timeout: %zu/%zu bytes written", totalWritten, size);
            break; // Return partial write
        }

        if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            size_t space = availableForWrite();

            if (space == 0) {
                // No space (shouldn't happen, but handle gracefully)
                xSemaphoreGive(mutex);
                break;
            }

            // Write as much as possible in one go
            size_t toWrite = size - totalWritten;
            if (toWrite > space) {
                toWrite = space;
            }

            // Handle ring buffer wrap-around
            size_t firstChunk = bufferSize - writePos;
            if (toWrite <= firstChunk) {
                // Write contiguous block
                memcpy(buffer + writePos, data + totalWritten, toWrite);
                writePos = (writePos + toWrite) % bufferSize;
                totalWritten += toWrite;
            } else {
                // Write split into two chunks (wrap around)
                memcpy(buffer + writePos, data + totalWritten, firstChunk);
                size_t secondChunk = toWrite - firstChunk;
                memcpy(buffer, data + totalWritten + firstChunk, secondChunk);
                writePos = secondChunk;
                totalWritten += toWrite;
            }

            // If there's still space, give spaceAvailable back
            if (availableForWrite() > 0) {
                xSemaphoreGive(spaceAvailable);
            }

            xSemaphoreGive(mutex);

            // Signal consumer that data is now available
            xSemaphoreGive(dataAvailable);
        }
    }

    return totalWritten;
}

void RingBufferStream::setEOF() {
    eofFlag = true;
    // Signal consumer one last time in case they're waiting
    xSemaphoreGive(dataAvailable);
    LOG_DEBUG("RingBufferStream EOF set");
}

void RingBufferStream::setError() {
    errorFlag = true;
    // Signal both producer and consumer to unblock them
    xSemaphoreGive(dataAvailable);
    xSemaphoreGive(spaceAvailable);
    LOG_ERROR("RingBufferStream error set");
}
