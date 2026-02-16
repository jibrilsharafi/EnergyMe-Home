// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <Stream.h>

#define RING_BUFFER_SIZE (64 * 1024) // 64KB ring buffer in PSRAM
#define RING_BUFFER_TIMEOUT_MS 5000   // 5 second timeout for semaphore waits

/**
 * RingBufferStream - Thread-safe ring buffer implementing Arduino Stream interface
 *
 * This class provides a PSRAM-based ring buffer that decouples a producer
 * (writing via Stream::write) from a consumer (reading via Stream::read).
 *
 * Designed for streaming TAR data from TarPacker (producer task) to HTTP
 * chunked response (consumer callback) without using LittleFS temp files.
 *
 * Thread Safety:
 * - Producer and consumer can run on different cores/tasks
 * - Semaphores coordinate access and signal data availability
 * - Blocks producer when buffer full, blocks consumer when buffer empty
 */
class RingBufferStream : public Stream {
private:
    uint8_t* buffer;           // PSRAM-allocated ring buffer
    size_t bufferSize;         // Size of buffer (64KB)
    volatile size_t writePos;  // Producer write position
    volatile size_t readPos;   // Consumer read position
    volatile bool eofFlag;     // Producer finished writing
    volatile bool errorFlag;   // Error occurred during production

    SemaphoreHandle_t mutex;          // Protects buffer position access
    SemaphoreHandle_t dataAvailable;  // Signals consumer: data ready to read
    SemaphoreHandle_t spaceAvailable; // Signals producer: space ready to write

    // Helper: bytes available to read (producer ahead of consumer)
    // Must be called with mutex held
    size_t availableForRead() const {
        if (writePos >= readPos) {
            return writePos - readPos;
        } else {
            // Wrapped around
            return bufferSize - readPos + writePos;
        }
    }

    // Helper: bytes available to write (free space in buffer)
    // Must be called with mutex held
    // Reserve 1 byte to distinguish full from empty (writePos == readPos means empty)
    size_t availableForWrite() const {
        size_t used = availableForRead();
        return bufferSize - used - 1;
    }

public:
    /**
     * Constructor - Allocates PSRAM ring buffer and creates semaphores
     *
     * @note If allocation fails, buffer will be nullptr (check before use)
     */
    RingBufferStream();

    /**
     * Destructor - Frees buffer and deletes semaphores
     */
    ~RingBufferStream();

    // ========== Stream interface - Reading (used by HTTP callback) ==========

    /**
     * Returns number of bytes available to read immediately
     *
     * @return Number of bytes that can be read without blocking
     */
    int available() override;

    /**
     * Read single byte from buffer
     *
     * @return Byte value (0-255), or -1 if EOF or error
     */
    int read() override;

    /**
     * Read multiple bytes from buffer into provided buffer
     *
     * Blocks if buffer empty (until data available or EOF/error).
     * Uses timeout to prevent infinite blocking.
     *
     * @param buffer Destination buffer
     * @param length Maximum bytes to read
     * @return Number of bytes actually read (0 on EOF/error/timeout)
     */
    size_t readBytes(uint8_t* buffer, size_t length) override;

    /**
     * Peek at next byte without consuming it
     *
     * @return Next byte value (0-255), or -1 if no data available
     */
    int peek() override;

    // ========== Stream interface - Writing (used by TarPacker) ==========

    /**
     * Write single byte to buffer
     *
     * Blocks if buffer full (until space available).
     *
     * @param byte Byte to write
     * @return 1 on success, 0 on failure
     */
    size_t write(uint8_t byte) override;

    /**
     * Write multiple bytes to buffer
     *
     * Blocks if buffer full (until space available).
     * May write partial data if timeout occurs.
     *
     * @param data Source buffer
     * @param size Number of bytes to write
     * @return Number of bytes actually written
     */
    size_t write(const uint8_t* data, size_t size) override;

    /**
     * Flush - No-op for ring buffer (data is immediately available to consumer)
     */
    void flush() override {}

    // ========== EOF and Error Signaling ==========

    /**
     * Signal end-of-file (called by producer task when done)
     *
     * After EOF is set, read operations will return 0 when buffer is empty
     */
    void setEOF();

    /**
     * Signal error (called by producer task on failure)
     *
     * After error is set, read operations will return 0 immediately
     */
    void setError();

    /**
     * Check if error flag is set
     *
     * @return true if producer reported an error
     */
    bool hasError() const { return errorFlag; }
};
