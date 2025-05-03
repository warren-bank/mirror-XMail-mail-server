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


#ifndef _MEMHEAP_H
#define _MEMHEAP_H





#define HEAP_OFFSET(p, b)           (SYS_PTRUINT) ((unsigned char *) (p) - (unsigned char *) (b))
#define HEAP_ADDRESS(b, o)          ((unsigned char *) (b) + (o))






struct MemHeapData
{
    unsigned int    uSize;
    SYS_PTRUINT     uFreePtr;
    SYS_PTRUINT     uAllocPtr;
    unsigned int    uAllocSize;
    unsigned int    uFreeBlocks;
    unsigned int    uAllocBlocks;
};






int             HeapSetup(MemHeapData & MHD, unsigned char *pBase, unsigned int uSize);
SYS_PTRUINT     HeapAlloc(MemHeapData & MHD, unsigned char *pBase, unsigned int uSize);
int             HeapFree(MemHeapData & MHD, unsigned char *pBase, SYS_PTRUINT uBlock);





#endif
