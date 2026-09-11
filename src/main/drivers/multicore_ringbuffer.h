/*
 * This file is part of Wingflight.
 *
 * Wingflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Wingflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

// Generic lock-free single-producer/single-consumer byte ring buffer, for
// the "work buffers" transport in docs/RP2350-Porting-Plan.md's "Core-1
// task consumer design" (one per producer/consumer pair - e.g. a future
// USB-MSC block-I/O queue or blackbox flush-chunk queue - as distinct from
// the occasional-one-shot RPC queue_t pair in platform/multicore.h).
//
// Safe for exactly one producer core and one consumer core (or a producer
// ISR + consumer core) operating concurrently without locks: head is only
// ever written by the producer, tail only by the consumer, and both are
// read by the other side with a single aligned 16-bit load, which is
// atomic on Cortex-M33. Atomicity alone is not enough across cores though:
// Armv8-M normal memory is weakly ordered, so each index publish carries a
// release fence (data written before the other core can observe the new
// index) and each consume side an acquire pairing. Capacity must be a
// power of two.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct multicoreRingBuffer_s {
    uint8_t *buffer;
    uint16_t capacity;  // power of two
    uint16_t mask;      // capacity - 1
    volatile uint16_t head; // next write index (producer-owned)
    volatile uint16_t tail; // next read index (consumer-owned)
} multicoreRingBuffer_t;

#define MULTICORE_RINGBUFFER_INIT(buf, cap) { \
    .buffer = (buf), \
    .capacity = (cap), \
    .mask = (cap) - 1, \
    .head = 0, \
    .tail = 0, \
}

static inline uint16_t multicoreRingBufferBytesUsed(const multicoreRingBuffer_t *rb)
{
    return (uint16_t)(rb->head - rb->tail);
}

static inline bool multicoreRingBufferPush(multicoreRingBuffer_t *rb, uint8_t byte)
{
    if (multicoreRingBufferBytesUsed(rb) >= rb->capacity) {
        return false; // full - never block the producer, drop instead
    }
    rb->buffer[rb->head & rb->mask] = byte;
    // Release: the byte store must be visible to the consumer core before
    // the new head is (volatile alone does not order it on Armv8-M).
    __atomic_thread_fence(__ATOMIC_RELEASE);
    rb->head++;
    return true;
}

static inline bool multicoreRingBufferPop(multicoreRingBuffer_t *rb, uint8_t *out)
{
    if (multicoreRingBufferBytesUsed(rb) == 0) {
        return false; // empty
    }
    // Acquire: pairs with the producer's release so the byte read below is
    // the one published with the observed head.
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *out = rb->buffer[rb->tail & rb->mask];
    // Release: the byte must be fully read before the slot is handed back
    // to the producer via the new tail, or it can be overwritten mid-read.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    rb->tail++;
    return true;
}

static inline uint16_t multicoreRingBufferBytesFree(const multicoreRingBuffer_t *rb)
{
    return (uint16_t)(rb->capacity - multicoreRingBufferBytesUsed(rb));
}

// Bulk variants of the two calls above. Byte-at-a-time works for a trickle of
// characters but not for a stream (a CLI dump, an MSP reply): each byte would
// otherwise cost its own fence pair, and the consumer could not hand a
// contiguous block to whatever it is feeding. These move one contiguous run -
// up to where the index wraps - so a caller drains a full buffer in at most
// two calls, with exactly one index publish each.

// Copies as much of data[] as fits, and returns how many bytes were taken.
static inline uint16_t multicoreRingBufferPushBuf(multicoreRingBuffer_t *rb, const uint8_t *data, uint16_t count)
{
    const uint16_t free = multicoreRingBufferBytesFree(rb);
    if (count > free) {
        count = free;
    }

    const uint16_t head = rb->head & rb->mask;
    const uint16_t firstRun = (count > (uint16_t)(rb->capacity - head)) ? (uint16_t)(rb->capacity - head) : count;

    for (uint16_t i = 0; i < firstRun; i++) {
        rb->buffer[head + i] = data[i];
    }
    for (uint16_t i = firstRun; i < count; i++) {
        rb->buffer[i - firstRun] = data[i];
    }

    // Release: as in multicoreRingBufferPush(), the bytes must be visible to
    // the consumer core before the head that publishes them.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    rb->head += count;
    return count;
}

// Hands back a pointer into the buffer covering the next contiguous run of
// readable bytes (never wrapping), without consuming it - so the consumer can
// pass it straight to a write call and only commit what was accepted.
// Returns 0 when empty.
static inline uint16_t multicoreRingBufferPeekRun(const multicoreRingBuffer_t *rb, const uint8_t **out)
{
    const uint16_t used = multicoreRingBufferBytesUsed(rb);
    if (used == 0) {
        return 0;
    }
    // Acquire: pairs with the producer's release, so the bytes behind the
    // observed head are the ones it published.
    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    const uint16_t tail = rb->tail & rb->mask;
    const uint16_t run = (used > (uint16_t)(rb->capacity - tail)) ? (uint16_t)(rb->capacity - tail) : used;
    *out = &rb->buffer[tail];
    return run;
}

// Releases count bytes previously returned by multicoreRingBufferPeekRun().
static inline void multicoreRingBufferConsume(multicoreRingBuffer_t *rb, uint16_t count)
{
    // Release: the bytes must be fully read before the slots are handed back
    // to the producer via the new tail.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    rb->tail += count;
}
