/*
 *  MailSvr by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@maticad.it>
 *
 */


#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "MemHeap.h"







#define HEAP_GRANULARITY            4
#define HEAP_MIN_WASTE              (HEAP_GRANULARITY + sizeof(MemHeapNode))
#define HEAP_END                    ((SYS_PTRUINT) -1)

#define HEAP_FIRST(b, o)            (MemHeapNode *) (((o) == HEAP_END) ? NULL: (unsigned char *) (b) + (o))
#define HEAP_NEXT(b, p)             (MemHeapNode *) (((p)->uNext == HEAP_END) ? NULL: (unsigned char *) (b) + (p)->uNext)
#define HEAP_NEAR(p, c)             (((unsigned char *) (p) + sizeof(MemHeapNode) + (p)->uSize) == (unsigned char *) (c))








struct MemHeapNode
{
    SYS_PTRUINT     uNext;
    SYS_PTRUINT     uSize;
};

struct MemAllocData
{
    unsigned int    uBlockSize;
    MemHeapNode    *pBestPrev;
    MemHeapNode    *pBest;
    MemHeapNode    *pPrevPrev;
    MemHeapNode    *pPrev;
    MemHeapNode    *pCurr;
};








static int      HeapInitAllocData(MemAllocData & MAD, unsigned char *pBase,
                        MemHeapData & MHD, unsigned int uSize);









int             HeapSetup(MemHeapData & MHD, unsigned char *pBase, unsigned int uSize)
{

    ZeroData(MHD);
    MHD.uSize = uSize;
    MHD.uFreePtr = 0;
    MHD.uAllocPtr = HEAP_END;
    MHD.uAllocSize = 0;
    MHD.uFreeBlocks = 1;
    MHD.uAllocBlocks = 0;

///////////////////////////////////////////////////////////////////////////////
//  Setup initial free block
///////////////////////////////////////////////////////////////////////////////
    MemHeapNode    *pBlock = (MemHeapNode *) pBase;

    pBlock->uNext = HEAP_END;
    pBlock->uSize = uSize - sizeof(MemHeapNode);


    return (0);

}



static int      HeapInitAllocData(MemAllocData & MAD, unsigned char *pBase,
                        MemHeapData & MHD, unsigned int uSize)
{

    ZeroData(MAD);
    MAD.uBlockSize = ((uSize + HEAP_GRANULARITY - 1) / HEAP_GRANULARITY) * HEAP_GRANULARITY;
    MAD.pBestPrev = NULL;
    MAD.pBest = NULL;
    MAD.pPrevPrev = NULL;
    MAD.pPrev = NULL;
    MAD.pCurr = HEAP_FIRST(pBase, MHD.uFreePtr);

    return (0);

}




SYS_PTRUINT     HeapAlloc(MemHeapData & MHD, unsigned char *pBase, unsigned int uSize)
{

    MemAllocData    MAD;

    if (HeapInitAllocData(MAD, pBase, MHD, uSize) < 0)
        return (0);

    for (; MAD.pCurr != NULL; MAD.pCurr = HEAP_NEXT(pBase, MAD.pCurr))
    {
///////////////////////////////////////////////////////////////////////////////
//  Try to merge nearest free blocks
///////////////////////////////////////////////////////////////////////////////
        if ((MAD.pPrev != NULL) && HEAP_NEAR(MAD.pPrev, MAD.pCurr))
        {
            MAD.pPrev->uSize += MAD.pCurr->uSize + sizeof(MemHeapNode);
            MAD.pPrev->uNext = MAD.pCurr->uNext;

            --MHD.uFreeBlocks;

            MAD.pCurr = MAD.pPrev;
            MAD.pPrev = MAD.pPrevPrev;
        }

///////////////////////////////////////////////////////////////////////////////
//  Best fit strategy
///////////////////////////////////////////////////////////////////////////////
        if ((MAD.pCurr->uSize >= MAD.uBlockSize) &&
                ((MAD.pBest == NULL) || (MAD.pCurr->uSize < MAD.pBest->uSize)))
        {
            MAD.pBest = MAD.pCurr;
            MAD.pBestPrev = MAD.pPrev;

            if (MAD.pBest->uSize == MAD.uBlockSize)
                break;
        }

        MAD.pPrevPrev = MAD.pPrev;
        MAD.pPrev = MAD.pCurr;
    }

///////////////////////////////////////////////////////////////////////////////
//  Not enough contiguous memory
///////////////////////////////////////////////////////////////////////////////
    if (MAD.pBest == NULL)
    {
        ErrSetErrorCode(ERR_HEAP_ALLOC);
        return (0);
    }

    MHD.uAllocSize += MAD.uBlockSize + sizeof(MemHeapNode);

    ++MHD.uAllocBlocks;

///////////////////////////////////////////////////////////////////////////////
//  If waste size is less then HEAP_MIN_WASTE assign the entire block
///////////////////////////////////////////////////////////////////////////////
    if ((MAD.pBest->uSize - MAD.uBlockSize) <= HEAP_MIN_WASTE)
    {
        if (MAD.pBestPrev == NULL)
            MHD.uFreePtr = MAD.pBest->uNext;
        else
            MAD.pBestPrev->uNext = MAD.pBest->uNext;

        --MHD.uFreeBlocks;
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Assign the ending part of the allocated block
///////////////////////////////////////////////////////////////////////////////
        MAD.pBest->uSize -= MAD.uBlockSize + sizeof(MemHeapNode);


        MAD.pBest = (MemHeapNode *) ((unsigned char *) MAD.pBest +
                sizeof(MemHeapNode) + MAD.pBest->uSize);

        MAD.pBest->uSize = MAD.uBlockSize;
    }

///////////////////////////////////////////////////////////////////////////////
//  Link the new allocated block into the head of the allocation list
///////////////////////////////////////////////////////////////////////////////
    MAD.pBest->uNext = MHD.uAllocPtr;

    MHD.uAllocPtr = HEAP_OFFSET(MAD.pBest, pBase);


    return (HEAP_OFFSET(MAD.pBest, pBase) + sizeof(MemHeapNode));

}



int             HeapFree(MemHeapData & MHD, unsigned char *pBase, SYS_PTRUINT uBlock)
{

    MemHeapNode    *pBlock = (MemHeapNode *) (pBase + (uBlock - sizeof(MemHeapNode))),
                   *pPrev = NULL,
                   *pCurr = HEAP_FIRST(pBase, MHD.uAllocPtr);

    for (; (pCurr != pBlock) && (pCurr != NULL); pPrev = pCurr, pCurr = HEAP_NEXT(pBase, pCurr));

///////////////////////////////////////////////////////////////////////////////
//  Block not found
///////////////////////////////////////////////////////////////////////////////
    if (pCurr == NULL)
    {
        ErrSetErrorCode(ERR_HEAP_FREE);
        return (ERR_HEAP_FREE);
    }

    MHD.uAllocSize -= pBlock->uSize + sizeof(MemHeapNode);

    --MHD.uAllocBlocks;

///////////////////////////////////////////////////////////////////////////////
//  Remove block from allocation list
///////////////////////////////////////////////////////////////////////////////
    if (pPrev == NULL)
        MHD.uAllocPtr = pBlock->uNext;
    else
        pPrev->uNext = pBlock->uNext;


///////////////////////////////////////////////////////////////////////////////
//  Insert block in free list in ascending order
///////////////////////////////////////////////////////////////////////////////
    for (pPrev = NULL, pCurr = HEAP_FIRST(pBase, MHD.uFreePtr);
            (pCurr != NULL) && (pBlock > pCurr);
            pPrev = pCurr, pCurr = HEAP_NEXT(pBase, pCurr));

    if (pPrev == NULL)
    {
        pBlock->uNext = MHD.uFreePtr;

        MHD.uFreePtr = HEAP_OFFSET(pBlock, pBase);

        ++MHD.uFreeBlocks;
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Merge if it's contiguous
///////////////////////////////////////////////////////////////////////////////
        if (HEAP_NEAR(pPrev, pBlock))
            pPrev->uSize += pBlock->uSize + sizeof(MemHeapNode);
        else
        {
            pBlock->uNext = pPrev->uNext;

            pPrev->uNext = HEAP_OFFSET(pBlock, pBase);

            ++MHD.uFreeBlocks;
        }
    }

    return (0);

}
