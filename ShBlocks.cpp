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
#include "ShBlocks.h"








struct BlockHandler
{
    SYS_SEMAPHORE   SemID;
    SYS_SHMEM       ShmID;
    void           *pShmData;
};








static int      ShbValidateBlock(SharedBlock const & SHB);









static int      ShbValidateBlock(SharedBlock const & SHB)
{

    if (SHB.uSize == 0)
    {
        ErrSetErrorCode(ERR_INVALID_SHARED_BLOCK);
        return (ERR_INVALID_SHARED_BLOCK);
    }

    return (0);

}




int             ShbCreateBlock(SharedBlock & SHB, unsigned int uSize)
{

    ZeroData(SHB);
    SHB.ulOwnerID = SysGetCurrentThreadId();
    SHB.uSize = uSize;
    SHB.ShmName = SysCreateIPCName();
    SHB.SemName = SysCreateIPCName();

    if ((SHB.SemID = SysCreateSemaphore(1, SYS_DEFAULT_MAXCOUNT,
                            SHB.SemName)) == SYS_INVALID_SEMAPHORE)
    {
        ZeroData(SHB);
        return (ErrGetErrorCode());
    }


    if ((SHB.ShmID = SysCreateSharedMem(uSize, SHB.ShmName)) == SYS_INVALID_SHMEM)
    {
        ErrorPush();
        SysCloseSemaphore(SHB.SemID);
        SysKillSemaphore(SHB.SemID);
        ZeroData(SHB);
        return (ErrorPop());
    }

    void           *pShmData = SysMapSharedMem(SHB.ShmID);

    memset(pShmData, 0, uSize);

    SysUnmapSharedMem(SHB.ShmID, pShmData);

    return (0);

}



int             ShbDestroyBlock(SharedBlock & SHB)
{

    if (ShbValidateBlock(SHB) < 0)
        return (ErrGetErrorCode());


    SysCloseSemaphore(SHB.SemID);
    SysKillSemaphore(SHB.SemID);

    SysCloseSharedMem(SHB.ShmID);
    SysKillSharedMem(SHB.ShmID);

    ZeroData(SHB);

    return (0);

}




SHB_HANDLE      ShbConnectBlock(SharedBlock & SHB)
{

    if (ShbValidateBlock(SHB) < 0)
        return (SHB_INVALID_HANDLE);


    SYS_SHMEM       ShmID = SysConnectSharedMem(SHB.uSize, SHB.ShmName);

    if (ShmID == SYS_INVALID_SHMEM)
        return (SHB_INVALID_HANDLE);

    SYS_SEMAPHORE   SemID = SysConnectSemaphore(1, SYS_DEFAULT_MAXCOUNT,
            SHB.SemName);

    if (SemID == SYS_INVALID_SEMAPHORE)
    {
        SysCloseSharedMem(ShmID);
        return (SHB_INVALID_HANDLE);
    }


    BlockHandler   *pBH = (BlockHandler *) SysAlloc(sizeof(BlockHandler));

    if (pBH == NULL)
    {
        SysCloseSemaphore(SemID);
        SysCloseSharedMem(ShmID);
        return (SHB_INVALID_HANDLE);
    }

    pBH->ShmID = ShmID;
    pBH->SemID = SemID;
    pBH->pShmData = SysMapSharedMem(SHB.ShmID);

    return ((SHB_HANDLE) pBH);

}



void            ShbCloseBlock(SHB_HANDLE hBlock)
{

    BlockHandler   *pBH = (BlockHandler *) hBlock;

    SysCloseSemaphore(pBH->SemID);
    SysUnmapSharedMem(pBH->ShmID, pBH->pShmData);
    SysCloseSharedMem(pBH->ShmID);

    SysFree(pBH);

}



void           *ShbLock(SHB_HANDLE hBlock)
{

    BlockHandler   *pBH = (BlockHandler *) hBlock;

    if (SysWaitSemaphore(pBH->SemID, SYS_INFINITE_TIMEOUT) < 0)
        return (NULL);

    return (pBH->pShmData);

}



void            ShbUnlock(SHB_HANDLE hBlock)
{

    BlockHandler   *pBH = (BlockHandler *) hBlock;

    SysReleaseSemaphore(pBH->SemID, 1);

}
