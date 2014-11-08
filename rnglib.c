/*
This library provides routines for creating and using ring buffers, which
are first-in-first-out circular buffers.  The routines simply manipulate
the ring buffer data structure; no kernel functions are invoked.  In
particular, ring buffers by themselves provide no task synchronization or
mutual exclusion.

However, the ring buffer pointers are manipulated in such a way that a
reader task (invoking rngBufGet()) and a writer task (invoking
rngBufPut()) can access a ring simultaneously without requiring mutual
exclusion.  This is because readers only affect a \f2read\f1 pointer and
writers only affect a \f2write\f1 pointer in a ring buffer data structure.
However, access by multiple readers or writers \f2must\fP be interlocked
through a mutual exclusion mechanism (i.e., a mutual-exclusion semaphore
guarding a ring buffer).


This library also supplies two macros, RNG_ELEM_PUT and RNG_ELEM_GET,
for putting and getting single bytes from a ring buffer.  They are defined
in rngLib.h.
.CS
    int RNG_ELEM_GET (ringId, pch, fromP)
    int RNG_ELEM_PUT (ringId, ch, toP)
.CE
Both macros require a temporary variable <fromP> or <toP>, which
should be declared as `register int' for maximum efficiency.  RNG_ELEM_GET
returns 1 if there was a character available in the buffer; it returns 0
otherwise.  RNG_ELEM_PUT returns 1 if there was room in the buffer; it returns
0 otherwise.  These are somewhat faster than rngBufPut() and rngBufGet(),
which can put and get multi-byte buffers.

INCLUDE FILES: rngLib.h
*/

/* LINTLIBRARY */
#if 0
#include <windows.h>
#include <pcap.h>
#endif

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include "rnglib.h"

#define min(x,y) ((x) < (y) ? (x) : (y))

/*******************************************************************************
*
* rngCreate - create an empty ring buffer
*
* This routine creates a ring buffer of size <nbytes>, and initializes
* it.  Memory for the buffer is allocated from the system memory partition.
*
* RETURNS
* The ID of the ring buffer, or NULL if memory cannot be allocated.
*/

RING_ID rngCreate
    (
    int nbytes          /* number of bytes in ring buffer */
    )
    {
    char *buffer;
    RING_ID ringId = (RING_ID) malloc (sizeof (RING));

    if (ringId == NULL)
	return (NULL);

    /* bump number of bytes requested because ring buffer algorithm
     * always leaves at least one empty byte in buffer */

    buffer = (char *) malloc ((unsigned) ++nbytes);

    if (buffer == NULL)
	{
	free ((char *)ringId);
	return (NULL);
	}

    ringId->bufSize = nbytes;
    ringId->buf	    = buffer;

    rngFlush (ringId);

    return (ringId);
    }
/*******************************************************************************
*
* rngDelete - delete a ring buffer
*
* This routine deletes a specified ring buffer.
* Any data currently in the buffer will be lost.
*
* RETURNS: N/A
*/

void rngDelete
    (
    RING_ID ringId         /* ring buffer to delete */
    )
    {
    free (ringId->buf);
    free ((char *)ringId);
    }
/*******************************************************************************
*
* rngFlush - make a ring buffer empty
*
* This routine initializes a specified ring buffer to be empty.
* Any data currently in the buffer will be lost.
*
* RETURNS: N/A
*/

void rngFlush
    (
    RING_ID ringId         /* ring buffer to initialize */
    )
    {
    ringId->pToBuf   = 0;
    ringId->pFromBuf = 0;
    }
/*******************************************************************************
*
* rngBufGet - get characters from a ring buffer
*
* This routine copies bytes from the ring buffer <rngId> into <buffer>.
* It copies as many bytes as are available in the ring, up to <maxbytes>.
* The bytes copied will be removed from the ring.
*
* RETURNS:
* The number of bytes actually received from the ring buffer;
* it may be zero if the ring buffer is empty at the time of the call.
*/

int rngBufGet
    (
    RING_ID rngId,         /* ring buffer to get data from      */
    char *buffer,               /* pointer to buffer to receive data */
    int maxbytes                /* maximum number of bytes to get    */
    )
    {
    int bytesgot = 0;
    int pToBuf = rngId->pToBuf;
    int bytes2;
    int pRngTmp = 0;

    if (pToBuf >= rngId->pFromBuf)
	{
	/* pToBuf has not wrapped around */

	bytesgot = min (maxbytes, pToBuf - rngId->pFromBuf);
	memcpy (buffer, &rngId->buf [rngId->pFromBuf], bytesgot);
	rngId->pFromBuf += bytesgot;
	}
    else
	{
	/* pToBuf has wrapped around.  Grab chars up to the end of the
	 * buffer, then wrap around if we need to. */

	bytesgot = min (maxbytes, rngId->bufSize - rngId->pFromBuf);
	memcpy (buffer, &rngId->buf [rngId->pFromBuf], bytesgot);
	pRngTmp = rngId->pFromBuf + bytesgot;

	/* If pFromBuf is equal to bufSize, we've read the entire buffer,
	 * and need to wrap now.  If bytesgot < maxbytes, copy some more chars
	 * in now. */

	if (pRngTmp == rngId->bufSize)
	    {
	    bytes2 = min (maxbytes - bytesgot, pToBuf);
	    memcpy (buffer + bytesgot, rngId->buf, bytes2);
	    rngId->pFromBuf = bytes2;
	    bytesgot += bytes2;
	    }
	else
	    rngId->pFromBuf = pRngTmp;
	}
    return (bytesgot);
    }
/*******************************************************************************
*
* rngBufPut - put bytes into a ring buffer
*
* This routine puts bytes from <buffer> into ring buffer <ringId>.  The
* specified number of bytes will be put into the ring, up to the number of
* bytes available in the ring.
*
* INTERNAL
* Always leaves at least one byte empty between pToBuf and pFromBuf, to
* eliminate ambiguities which could otherwise occur when the two pointers
* are equal.
*
* RETURNS:
* The number of bytes actually put into the ring buffer;
* it may be less than number requested, even zero,
* if there is insufficient room in the ring buffer at the time of the call.
*/

int rngBufPut
    (
    RING_ID rngId,         /* ring buffer to put data into  */
    char *buffer,               /* buffer to get data from       */
    int nbytes                  /* number of bytes to try to put */
    )
    {
    int bytesput = 0;
    int pFromBuf = rngId->pFromBuf;
    int bytes2;
    int pRngTmp = 0;

    if (pFromBuf > rngId->pToBuf)
	{
	/* pFromBuf is ahead of pToBuf.  We can fill up to two bytes
	 * before it */

	bytesput = min (nbytes, pFromBuf - rngId->pToBuf - 1);
	memcpy (&rngId->buf [rngId->pToBuf], buffer, bytesput);
	rngId->pToBuf += bytesput;
	}
    else if (pFromBuf == 0)
	{
	/* pFromBuf is at the beginning of the buffer.  We can fill till
	 * the next-to-last element */

	bytesput = min (nbytes, rngId->bufSize - rngId->pToBuf - 1);
	memcpy (&rngId->buf [rngId->pToBuf], buffer, bytesput);
	rngId->pToBuf += bytesput;
	}
    else
	{
	/* pFromBuf has wrapped around, and its not 0, so we can fill
	 * at least to the end of the ring buffer.  Do so, then see if
	 * we need to wrap and put more at the beginning of the buffer. */

	bytesput = min (nbytes, rngId->bufSize - rngId->pToBuf);
	memcpy (&rngId->buf [rngId->pToBuf], buffer, bytesput);
	pRngTmp = rngId->pToBuf + bytesput;

	if (pRngTmp == rngId->bufSize)
	    {
	    /* We need to wrap, and perhaps put some more chars */

	    bytes2 = min (nbytes - bytesput, pFromBuf - 1);
	    memcpy (rngId->buf, buffer + bytesput, bytes2);
	    rngId->pToBuf = bytes2;
	    bytesput += bytes2;
	    }
	else
	    rngId->pToBuf = pRngTmp;
	}
    return (bytesput);
    }
/*******************************************************************************
*
* rngIsEmpty - test if a ring buffer is empty
*
* This routine determines if a specified ring buffer is empty.
*
* RETURNS:
* TRUE if empty, FALSE if not.
*/

BOOL rngIsEmpty
    (
    RING_ID ringId      /* ring buffer to test */
    )
    {
    return (ringId->pToBuf == ringId->pFromBuf);
    }
/*******************************************************************************
*
* rngIsFull - test if a ring buffer is full (no more room)
*
* This routine determines if a specified ring buffer is completely full.
*
* RETURNS:
* TRUE if full, FALSE if not.
*/

BOOL rngIsFull
    (
    RING_ID ringId         /* ring buffer to test */
    )
    {
    int n = ringId->pToBuf - ringId->pFromBuf + 1;

    return ((n == 0) || (n == ringId->bufSize));
    }
/*******************************************************************************
*
* rngFreeBytes - determine the number of free bytes in a ring buffer
*
* This routine determines the number of bytes currently unused in a specified
* ring buffer.
*
* RETURNS: The number of unused bytes in the ring buffer.
*/

int rngFreeBytes
    (
    RING_ID ringId         /* ring buffer to examine */
    )
    {
    int n = ringId->pFromBuf - ringId->pToBuf - 1;

    if (n < 0)
	n += ringId->bufSize;

    return (n);
    }
/*******************************************************************************
*
* rngNBytes - determine the number of bytes in a ring buffer
*
* This routine determines the number of bytes currently in a specified
* ring buffer.
*
* RETURNS: The number of bytes filled in the ring buffer.
*/

int rngNBytes
    (
    RING_ID ringId         /* ring buffer to be enumerated */
    )
    {
    int n = ringId->pToBuf - ringId->pFromBuf;

    if (n < 0)
	n += ringId->bufSize;

    return (n);
    }
/*******************************************************************************
*
* rngPutAhead - put a byte ahead in a ring buffer without moving ring pointers
*
* This routine writes a byte into the ring, but does not move the ring buffer
* pointers.  Thus the byte will not yet be available to rngBufGet() calls.
* The byte is written <offset> bytes ahead of the next input location in the
* ring.  Thus, an offset of 0 puts the byte in the same position as would
* RNG_ELEM_PUT would put a byte, except that the input pointer is not updated.
*
* Bytes written ahead in the ring buffer with this routine can be made available
* all at once by subsequently moving the ring buffer pointers with the routine
* rngMoveAhead().
*
* Before calling rngPutAhead(), the caller must verify that at least
* <offset> + 1 bytes are available in the ring buffer.
*
* RETURNS: N/A
*/

void rngPutAhead
    (
    RING_ID ringId,   /* ring buffer to put byte in    */
    char byte,             /* byte to be put in ring        */
    int offset             /* offset beyond next input byte where to put byte */
    )
    {
    int n = ringId->pToBuf + offset;

    if (n >= ringId->bufSize)
	n -= ringId->bufSize;

    *(ringId->buf + n) = byte;
    }
/*******************************************************************************
*
* rngMoveAhead - advance a ring pointer by <n> bytes
*
* This routine advances the ring buffer input pointer by <n> bytes.  This makes
* <n> bytes available in the ring buffer, after having been written ahead in
* the ring buffer with rngPutAhead().
*
* RETURNS: N/A
*/

void rngMoveAhead
    (
    RING_ID ringId,  /* ring buffer to be advanced                  */
    int n            /* number of bytes ahead to move input pointer */
    )
    {
    n += ringId->pToBuf;

    if (n >= ringId->bufSize)
	n -= ringId->bufSize;

    ringId->pToBuf = n;
    }

int rngSearch(
              RING_ID rngId,
              char chr,
              int *retPos)
{
    int nChars = rngNBytes(rngId);
    char *endPos = rngId->buf + rngId->bufSize;
    char *bufPtr;
    int retStat = -1;

    if (retPos != 0)
    {
        *retPos = 0;
    }

    for(bufPtr = &rngId->buf[rngId->pFromBuf];
        nChars > 0;
        bufPtr++, nChars--)
    {
      if(bufPtr >= endPos)
      {
          bufPtr = rngId->buf;
      }

      if (*bufPtr == chr)
      {
          retStat = 0;
        if (retPos != 0)
        {
            *retPos =  (rngNBytes(rngId) - nChars) + 1;
        }
          break;
      }
    }


    return retStat;
}
