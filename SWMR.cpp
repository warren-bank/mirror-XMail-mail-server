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
#include "SWMR.h"






struct SWMRData
{
    SYS_IPCNAME     SemName;
    SYS_SEMAPHORE   SemID;
    int             iWriteLocks;
    int             iReadLocks;
    int             iWaitingProcesses;
};






int             SWMRCreateHandler(SharedBlock & SHB)
{

    if (ShbCreateBlock(SHB, sizeof(SWMRData)) < 0)
        return (ErrGetErrorCode());


    SHB_HANDLE      hSWMR = ShbConnectBlock(SHB);

    if (hSWMR == SHB_INVALID_HANDLE)
    {
        ErrorPush();
        ShbDestroyBlock(SHB);
        return (ErrorPop());
    }

    SYS_IPCNAME     SemName = SysCreateIPCName();
    SYS_SEMAPHORE   SemID = SysCreateSemaphore(0, SYS_DEFAULT_MAXCOUNT, SemName);

    if (SemID == SYS_INVALID_SEMAPHORE)
    {
        ErrorPush();
        ShbCloseBlock(hSWMR);
        ShbDestroyBlock(SHB);
        return (ErrorPop());
    }

    SWMRData       *pSWMRData = (SWMRData *) ShbLock(hSWMR);

    if (pSWMRData == NULL)
    {
        ErrorPush();
        SysCloseSemaphore(SemID);
        SysKillSemaphore(SemID);
        ShbCloseBlock(hSWMR);
        ShbDestroyBlock(SHB);
        return (ErrorPop());
    }

    pSWMRData->SemName = SemName;
    pSWMRData->SemID = SemID;
    pSWMRData->iWriteLocks = 0;
    pSWMRData->iReadLocks = 0;
    pSWMRData->iWaitingProcesses = 0;

    ShbUnlock(hSWMR);
    ShbCloseBlock(hSWMR);

    return (0);

}



int             SWMRDestroyHandler(SharedBlock & SHB)
{

    SHB_HANDLE      hSWMR = ShbConnectBlock(SHB);

    if (hSWMR == SHB_INVALID_HANDLE)
        return (ErrGetErrorCode());


    SWMRData       *pSWMRData = (SWMRData *) ShbLock(hSWMR);

    if (pSWMRData != NULL)
    {
        SysCloseSemaphore(pSWMRData->SemID);
        SysKillSemaphore(pSWMRData->SemID);
    }

    ShbUnlock(hSWMR);
    ShbCloseBlock(hSWMR);

    ShbDestroyBlock(SHB);

    return (0);

}



SHB_HANDLE      SWMRConnectHandler(SharedBlock & SHB)
{

    return (ShbConnectBlock(SHB));

}



void            SWMRCloseHandle(SHB_HANDLE hSWMR)
{

    ShbCloseBlock(hSWMR);

}



int             SWMRReadLock(SHB_HANDLE hSWMR)
{

    SYS_SEMAPHORE   SemID = SYS_INVALID_SEMAPHORE;

    for (;;)
    {
        SWMRData       *pSWMRData = (SWMRData *) ShbLock(hSWMR);

        if (pSWMRData == NULL)
            return (ErrGetErrorCode());

        if (pSWMRData->iWriteLocks > 0)
        {
            if ((SemID == SYS_INVALID_SEMAPHORE) &&
                    ((SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT,
                                            pSWMRData->SemName)) == SYS_INVALID_SEMAPHORE))
            {
                ErrorPush();
                ShbUnlock(hSWMR);
                return (ErrorPop());
            }

            ++pSWMRData->iWaitingProcesses;

            ShbUnlock(hSWMR);

            if (SysWaitSemaphore(SemID, SYS_INFINITE_TIMEOUT) < 0)
            {
                ErrorPush();
                SysCloseSemaphore(SemID);
                return (ErrorPop());
            }
        }
        else
        {
            ++pSWMRData->iReadLocks;

            ShbUnlock(hSWMR);

            break;
        }
    }

    if (SemID != SYS_INVALID_SEMAPHORE)
        SysCloseSemaphore(SemID);

    return (0);

}



int             SWMRReadUnlock(SHB_HANDLE hSWMR)
{

    SWMRData       *pSWMRData = (SWMRData *) ShbLock(hSWMR);

    if (pSWMRData == NULL)
        return (ErrGetErrorCode());

    --pSWMRData->iReadLocks;

    if ((pSWMRData->iWaitingProcesses > 0) && (pSWMRData->iReadLocks == 0))
    {
        SYS_SEMAPHORE   SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT, pSWMRData->SemName);

        if (SemID == SYS_INVALID_SEMAPHORE)
        {
            ErrorPush();
            ShbUnlock(hSWMR);
            return (ErrorPop());
        }

        SysReleaseSemaphore(SemID, pSWMRData->iWaitingProcesses);

        pSWMRData->iWaitingProcesses = 0;

        SysCloseSemaphore(SemID);
    }

    ShbUnlock(hSWMR);

    return (0);

}



int             SWMRWriteLock(SHB_HANDLE hSWMR)
{

    SYS_SEMAPHORE   SemID = SYS_INVALID_SEMAPHORE;

    for (;;)
    {
        SWMRData       *pSWMRData = (SWMRData *) ShbLock(hSWMR);

        if (pSWMRData == NULL)
            return (ErrGetErrorCode());

        if ((pSWMRData->iReadLocks > 0) || (pSWMRData->iWriteLocks > 0))
        {
            if ((SemID == SYS_INVALID_SEMAPHORE) &&
                    ((SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT, pSWMRData->SemName)) == SYS_INVALID_SEMAPHORE))
            {
                ErrorPush();
                ShbUnlock(hSWMR);
                return (ErrorPop());
            }

            ++pSWMRData->iWaitingProcesses;

            ShbUnlock(hSWMR);

            if (SysWaitSemaphore(SemID, SYS_INFINITE_TIMEOUT) < 0)
            {
                ErrorPush();
                SysCloseSemaphore(SemID);
                return (ErrorPop());
            }
        }
        else
        {
            ++pSWMRData->iWriteLocks;

            ShbUnlock(hSWMR);

            break;
        }
    }

    if (SemID != SYS_INVALID_SEMAPHORE)
        SysCloseSemaphore(SemID);

    return (0);

}



int             SWMRWriteUnlock(SHB_HANDLE hSWMR)
{

    SWMRData       *pSWMRData = (SWMRData *) ShbLock(hSWMR);

    if (pSWMRData == NULL)
        return (ErrGetErrorCode());

    --pSWMRData->iWriteLocks;

    if ((pSWMRData->iWaitingProcesses > 0) && (pSWMRData->iWriteLocks == 0))
    {
        SYS_SEMAPHORE   SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT,
                pSWMRData->SemName);

        if (SemID == SYS_INVALID_SEMAPHORE)
        {
            ErrorPush();
            ShbUnlock(hSWMR);
            return (ErrorPop());
        }

        SysReleaseSemaphore(SemID, pSWMRData->iWaitingProcesses);

        pSWMRData->iWaitingProcesses = 0;

        SysCloseSemaphore(SemID);
    }

    ShbUnlock(hSWMR);

    return (0);

}



SHB_HANDLE      SWMRCreateReadLock(SharedBlock & SHB)
{

    SHB_HANDLE      hSWMR = SWMRConnectHandler(SHB);

    if (hSWMR == SHB_INVALID_HANDLE)
        return (SHB_INVALID_HANDLE);

    if (SWMRReadLock(hSWMR) < 0)
    {
        SWMRCloseHandle(hSWMR);
        return (SHB_INVALID_HANDLE);
    }

    return (hSWMR);

}



void            SWMRCloseReadUnlock(SHB_HANDLE hSWMR)
{

    SWMRReadUnlock(hSWMR);

    SWMRCloseHandle(hSWMR);

}



SHB_HANDLE      SWMRCreateWriteLock(SharedBlock & SHB)
{

    SHB_HANDLE      hSWMR = SWMRConnectHandler(SHB);

    if (hSWMR == SHB_INVALID_HANDLE)
        return (SHB_INVALID_HANDLE);

    if (SWMRWriteLock(hSWMR) < 0)
    {
        SWMRCloseHandle(hSWMR);
        return (SHB_INVALID_HANDLE);
    }

    return (hSWMR);

}



void            SWMRCloseWriteUnlock(SHB_HANDLE hSWMR)
{

    SWMRWriteUnlock(hSWMR);

    SWMRCloseHandle(hSWMR);

}
