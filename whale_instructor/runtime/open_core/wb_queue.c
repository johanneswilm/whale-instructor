/* wb_queue.c -- single-producer/consumer byte ring buffer.
 *
 * SPDX-FileCopyrightText: Johannes Wilm
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Whale Instructor open core (linked into user programs). Replaces the
 * proprietary user_queue.o member of the vendor archive; the struct
 * layout and function semantics are the queue contract the vendor core
 * depends on (see wb_core.h).
 */
#include "wb_core.h"

bool init_byte_queue(byte_queue_t *ptQueue, uint8_t *pchByte, uint16_t hwSize)
{
    if (ptQueue == NULL || pchByte == NULL || hwSize == 0) {
        return false;
    }
    ptQueue->pchBuffer = pchByte;
    ptQueue->hwSize = hwSize;
    ptQueue->hwHead = 0;
    ptQueue->hwTail = 0;
    ptQueue->hwLength = 0;
    ptQueue->hwPeek = ptQueue->hwTail;
    ptQueue->hwPeekCnt = 0;
    return true;
}

bool enqueue(byte_queue_t *ptQueue, uint8_t chByte)
{
    if (ptQueue == NULL || ptQueue->pchBuffer == NULL) {
        return false;
    }
    if (ptQueue->hwLength >= ptQueue->hwSize) {
        return false;
    }
    ptQueue->pchBuffer[ptQueue->hwTail++] = chByte;
    if (ptQueue->hwTail >= ptQueue->hwSize) {
        ptQueue->hwTail = 0;
    }
    ptQueue->hwLength++;
    return true;
}

bool dequeue(byte_queue_t *ptQueue, uint8_t *pchByte)
{
    if (ptQueue == NULL || pchByte == NULL || ptQueue->hwLength == 0) {
        return false;
    }
    *pchByte = ptQueue->pchBuffer[ptQueue->hwHead++];
    if (ptQueue->hwHead >= ptQueue->hwSize) {
        ptQueue->hwHead = 0;
    }
    ptQueue->hwLength--;
    reset_peek_byte(ptQueue);
    return true;
}

bool peek_byte_queue(byte_queue_t *ptQueue, uint8_t *pchByte)
{
    if (ptQueue == NULL || pchByte == NULL || ptQueue->hwPeekCnt >= ptQueue->hwLength) {
        return false;
    }
    *pchByte = ptQueue->pchBuffer[ptQueue->hwPeek++];
    if (ptQueue->hwPeek >= ptQueue->hwSize) {
        ptQueue->hwPeek = 0;
    }
    ptQueue->hwPeekCnt++;
    return true;
}

bool reset_peek_byte(byte_queue_t *ptQueue)
{
    if (ptQueue == NULL) {
        return false;
    }
    ptQueue->hwPeek = ptQueue->hwTail;
    ptQueue->hwPeekCnt = 0;
    return true;
}

bool get_all_peeked_byte(byte_queue_t *ptQueue)
{
    if (ptQueue == NULL || ptQueue->hwPeekCnt == 0) {
        return false;
    }
    /* Commit the peeked bytes: advance the dequeue position past them. */
    ptQueue->hwHead += ptQueue->hwPeekCnt;
    if (ptQueue->hwHead >= ptQueue->hwSize) {
        ptQueue->hwHead -= ptQueue->hwSize;
    }
    ptQueue->hwLength -= ptQueue->hwPeekCnt;
    ptQueue->hwPeek = ptQueue->hwTail;
    ptQueue->hwPeekCnt = 0;
    return true;
}
