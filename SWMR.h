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


#ifndef _SWMR_H
#define _SWMR_H



int             SWMRCreateHandler(SharedBlock & SHB);
int             SWMRDestroyHandler(SharedBlock & SHB);
SHB_HANDLE      SWMRConnectHandler(SharedBlock & SHB);
void            SWMRCloseHandle(SHB_HANDLE hSWMR);
int             SWMRReadLock(SHB_HANDLE hSWMR);
int             SWMRReadUnlock(SHB_HANDLE hSWMR);
int             SWMRWriteLock(SHB_HANDLE hSWMR);
int             SWMRWriteUnlock(SHB_HANDLE hSWMR);
SHB_HANDLE      SWMRCreateReadLock(SharedBlock & SHB);
void            SWMRCloseReadUnlock(SHB_HANDLE hSWMR);
SHB_HANDLE      SWMRCreateWriteLock(SharedBlock & SHB);
void            SWMRCloseWriteUnlock(SHB_HANDLE hSWMR);



#endif
