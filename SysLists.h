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
 *  Davide Libenzi <davide_libenzi@mycio.com>
 *
 */


#ifndef _SYSLISTS_H
#define _SYSLISTS_H



#define SYS_LIST_HEAD_INIT(name)        { &(name), &(name) }

#define SYS_LIST_HEAD(name)             struct SysListHead name = SYS_LIST_HEAD_INIT(name)

#define SYS_INIT_LIST_HEAD(ptr) \
do { \
    (ptr)->pNext = (ptr); (ptr)->pPrev = (ptr); \
} while (0)

#define SYS_LIST_ADD(new, prev, next) \
do { \
    (next)->pPrev = new; \
	(new)->pNext = next; \
	(new)->pPrev = prev; \
	(prev)->pNext = new; \
} while (0)

#define SYS_LIST_ADDH(new, head)        SYS_LIST_ADD(new, head, (head)->pNext)

#define SYS_LIST_ADDT(new, head)        SYS_LIST_ADD(new, (head)->pPrev, head)

#define SYS_LIST_UNLINK(prev, next) \
do { \
    (next)->pPrev = prev; \
    (prev)->pNext = next; \
} while (0)

#define SYS_LIST_DEL(entry)             SYS_LIST_UNLINK((entry)->pPrev, (entry)->pNext)

#define SYS_LIST_EMTPY(head)            ((head)->pNext == head)

#define SYS_LIST_SPLICE(list, head) \
do { \
    struct SysListHead *    first = (list)->pNext; \
    if (first != list) { \
        struct SysListHead *    last = (list)->pPrev; \
        struct SysListHead *    at = (head)->pNext; \
        (first)->pPrev = head; \
        (head)->pNext = first; \
        (last)->pNext = at; \
        (at)->pPrev = last; \
    } \
} while (0)

#define SYS_LIST_ENTRY(ptr, type, member)   ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define SYS_LIST_FOR_EACH(pos, head)        for (pos = (head)->pNext; pos != (head); pos = (pos)->pNext)





struct SysListHead
{
    struct SysListHead *    pNext;
    struct SysListHead *    pPrev;
};





#endif
