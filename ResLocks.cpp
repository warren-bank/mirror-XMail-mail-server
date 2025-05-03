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
#include "ShBlocks.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "MailConfig.h"
#include "MailSvr.h"







#define NUM_ACCOUNTS_X_GATE         32
#define MIN_LOCK_MEMORY             (64 * 1024)
#define MAX_LOCK_MEMORY             (1024 * 1024)

#define MIN_WAIT_GATES              101
#define MAX_WAIT_GATES              5381
#define MAX_WAIT_GATE_SEMAPHORES    64

#define END_OF_LIST                 ((SYS_PTRUINT) -1)








struct GatesAllocParams
{
    int             iNumGates;
    int             iNumSemaphores;
    int             iLockMemorySize;
};

struct WaitGateSemaphore
{
    SYS_IPCNAME     SemName;
    SYS_SEMAPHORE   SemID;
};

struct ResWaitGate
{
    SYS_IPCNAME     SemName;
    int             iWaitingProcesses;
    SYS_PTRUINT     uResList;
};
/* INDENT OFF */

struct ResLockEntry
{
    SYS_PTRUINT    uNext;
    SYS_UINT32      ShLocks:31;
    SYS_UINT32      ExLocks:1;
    char            szName[1];
};

/* INDENT ON */

struct ResLockArena
{
    MemHeapData     MHD;
    int             iNumSemaphores;
    SYS_PTRUINT     uSemaphoresPtr;
    int             iNumGates;
    SYS_PTRUINT     uGatesPtr;
};

struct ResArenaConnection
{
    SHB_HANDLE      hResLocks;
    ResLockArena   *pRLA;
    unsigned char  *pHeapBase;
    WaitGateSemaphore *pWGS;
    ResWaitGate    *pRWG;
};








static int      RLckSetupAllocParams(int iNumAccounts, GatesAllocParams & GAP);
static int      RLckConnectArena(ResArenaConnection & RAC);
static int      RLckDisconnectArena(ResArenaConnection & RAC);
static int      RLckLockArena(ResArenaConnection & RAC);
static int      RLckUnlockArena(ResArenaConnection & RAC);
static char    *RLckGetResourceName(unsigned int uUserID, unsigned int uResID,
                        char *pszResourceName);
static int      RLckGetWaitGate(char const * pszResourceName, int iNumGates);
static ResLockEntry *RLckGetEntry(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName);
static int      RLckRemoveEntry(ResArenaConnection & RAC, int iWaitGate,
                        ResLockEntry * pRLE);
static ResLockEntry *RLckAllocEntry(ResArenaConnection & RAC, char const * pszResourceName);
static int      RLckTryLockEX(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName);
static int      RLckDoUnlockEX(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName);
static int      RLckTryLockSH(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName);
static int      RLckDoUnlockSH(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName);
static RLCK_HANDLE RLckLock(char const * pszResourceName,
                        int (*pLockProc) (ResArenaConnection &, int, char const *));
static int      RLckUnlock(RLCK_HANDLE hLock,
                        int (*pUnlockProc) (ResArenaConnection &, int, char const *));













static SharedBlock SHB_ResLocks;












static int      RLckSetupAllocParams(int iNumAccounts, GatesAllocParams & GAP)
{

    ZeroData(GAP);

///////////////////////////////////////////////////////////////////////////////
//  Calculate the number of gates
///////////////////////////////////////////////////////////////////////////////
    int             iGates = iNumAccounts / NUM_ACCOUNTS_X_GATE;

    while (!IsPrimeNumber(iGates))
        ++iGates;

    GAP.iNumGates = max(MIN_WAIT_GATES, min(iGates, MAX_WAIT_GATES));

///////////////////////////////////////////////////////////////////////////////
//  Calculate the number of semaphores
///////////////////////////////////////////////////////////////////////////////
    GAP.iNumSemaphores = min(GAP.iNumGates / 80 + 1, MAX_WAIT_GATE_SEMAPHORES);

///////////////////////////////////////////////////////////////////////////////
//  Calculate locking memory heap size
///////////////////////////////////////////////////////////////////////////////
    int             iLockMemory = (iNumAccounts / 8) * 80;

    GAP.iLockMemorySize = min(MAX_LOCK_MEMORY, max(iLockMemory, MIN_LOCK_MEMORY));


    return (0);

}



int             RLckInitLockers(int iNumAccounts)
{
///////////////////////////////////////////////////////////////////////////////
//  Calculate allocation params
///////////////////////////////////////////////////////////////////////////////
    GatesAllocParams GAP;

    RLckSetupAllocParams(iNumAccounts, GAP);

///////////////////////////////////////////////////////////////////////////////
//  Create resource locking shared block
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uAllocSize = (sizeof(ResLockArena) + GAP.iLockMemorySize +
            GAP.iNumGates * sizeof(ResWaitGate) + GAP.iNumSemaphores * sizeof(WaitGateSemaphore));

    if (ShbCreateBlock(SHB_ResLocks, uAllocSize) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Connect to shared block
///////////////////////////////////////////////////////////////////////////////
    SHB_HANDLE      hResLocks = ShbConnectBlock(SHB_ResLocks);

    if (hResLocks == SHB_INVALID_HANDLE)
    {
        ErrorPush();
        ShbDestroyBlock(SHB_ResLocks);
        return (ErrorPop());
    }

    ResLockArena   *pRLA = (ResLockArena *) ShbLock(hResLocks);

    if (pRLA == NULL)
    {
        ErrorPush();
        ShbCloseBlock(hResLocks);
        ShbDestroyBlock(SHB_ResLocks);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup resource entries heap
///////////////////////////////////////////////////////////////////////////////
    unsigned char  *pHeapBase = (unsigned char *) pRLA + sizeof(ResLockArena);

    if (HeapSetup(pRLA->MHD, pHeapBase, uAllocSize - sizeof(ResLockArena)) < 0)
    {
        ErrorPush();
        ShbUnlock(hResLocks);
        ShbCloseBlock(hResLocks);
        ShbDestroyBlock(SHB_ResLocks);
        return (ErrorPop());
    }

    pRLA->iNumSemaphores = GAP.iNumSemaphores;
    pRLA->iNumGates = GAP.iNumGates;

///////////////////////////////////////////////////////////////////////////////
//  Allocates gates table and semaphore table
///////////////////////////////////////////////////////////////////////////////
    if (((pRLA->uSemaphoresPtr = HeapAlloc(pRLA->MHD, pHeapBase,
                                    GAP.iNumSemaphores * sizeof(WaitGateSemaphore))) == 0) ||
            ((pRLA->uGatesPtr = HeapAlloc(pRLA->MHD, pHeapBase,
                                    GAP.iNumGates * sizeof(ResWaitGate))) == 0))
    {
        ErrorPush();
        ShbUnlock(hResLocks);
        ShbCloseBlock(hResLocks);
        ShbDestroyBlock(SHB_ResLocks);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup gates semaphores
///////////////////////////////////////////////////////////////////////////////
    WaitGateSemaphore *pWGS = (WaitGateSemaphore *) HEAP_ADDRESS(pHeapBase, pRLA->uSemaphoresPtr);

    for (int ii = 0; ii < GAP.iNumSemaphores; ii++)
    {
        pWGS[ii].SemName = SysCreateIPCName();

        if ((pWGS[ii].SemID = SysCreateSemaphore(0, SYS_DEFAULT_MAXCOUNT,
                                pWGS[ii].SemName)) == SYS_INVALID_SEMAPHORE)
        {
            ErrorPush();

            for (--ii; ii >= 0; ii--)
                SysCloseSemaphore(pWGS[ii].SemID), SysKillSemaphore(pWGS[ii].SemID);

            ShbUnlock(hResLocks);
            ShbCloseBlock(hResLocks);
            ShbDestroyBlock(SHB_ResLocks);
            return (ErrorPop());
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup gates table
///////////////////////////////////////////////////////////////////////////////
    ResWaitGate    *pRWG = (ResWaitGate *) HEAP_ADDRESS(pHeapBase, pRLA->uGatesPtr);

    for (int ww = 0, ss = 0; ww < GAP.iNumGates; ww++, ss = INext(ss, GAP.iNumSemaphores))
    {
        pRWG[ww].SemName = pWGS[ss].SemName;
        pRWG[ww].iWaitingProcesses = 0;
        pRWG[ww].uResList = END_OF_LIST;
    }

    ShbUnlock(hResLocks);

    ShbCloseBlock(hResLocks);

    return (0);

}



int             RLckCleanupLockers(void)
{

    ResArenaConnection RAC;

    if (RLckConnectArena(RAC) < 0)
    {
        ErrorPush();
        ShbDestroyBlock(SHB_ResLocks);
        return (ErrorPop());
    }

    if (RLckLockArena(RAC) < 0)
    {
        ErrorPush();
        RLckDisconnectArena(RAC);
        ShbDestroyBlock(SHB_ResLocks);
        return (ErrorPop());
    }


    for (int ii = 0; ii < RAC.pRLA->iNumSemaphores; ii++)
        SysCloseSemaphore(RAC.pWGS[ii].SemID), SysKillSemaphore(RAC.pWGS[ii].SemID);


    RLckUnlockArena(RAC);

    RLckDisconnectArena(RAC);

    ShbDestroyBlock(SHB_ResLocks);

    return (0);

}




static int      RLckConnectArena(ResArenaConnection & RAC)
{

    ZeroData(RAC);

    if ((RAC.hResLocks = ShbConnectBlock(SHB_ResLocks)) == SHB_INVALID_HANDLE)
        return (ErrGetErrorCode());

    RAC.pRLA = NULL;
    RAC.pHeapBase = NULL;
    RAC.pWGS = NULL;
    RAC.pRWG = NULL;

    return (0);

}



static int      RLckDisconnectArena(ResArenaConnection & RAC)
{

    ShbCloseBlock(RAC.hResLocks);

    ZeroData(RAC);

    return (0);

}



static int      RLckLockArena(ResArenaConnection & RAC)
{

    if ((RAC.pRLA = (ResLockArena *) ShbLock(RAC.hResLocks)) == NULL)
        return (ErrGetErrorCode());

    RAC.pHeapBase = (unsigned char *) RAC.pRLA + sizeof(ResLockArena);

    RAC.pWGS = (WaitGateSemaphore *) HEAP_ADDRESS(RAC.pHeapBase, RAC.pRLA->uSemaphoresPtr);

    RAC.pRWG = (ResWaitGate *) HEAP_ADDRESS(RAC.pHeapBase, RAC.pRLA->uGatesPtr);

    return (0);

}



static int      RLckUnlockArena(ResArenaConnection & RAC)
{

    ShbUnlock(RAC.hResLocks);

    RAC.pRLA = NULL;
    RAC.pHeapBase = NULL;
    RAC.pWGS = NULL;
    RAC.pRWG = NULL;

    return (0);

}



static char    *RLckGetResourceName(unsigned int uUserID, unsigned int uResID,
                        char *pszResourceName)
{

    sprintf(pszResourceName, "$>usr%u.res%u", uUserID, uResID);

    return (pszResourceName);

}



static int      RLckGetWaitGate(char const * pszResourceName, int iNumGates)
{

    SYS_UINT32      uHashValue = MscHashString(pszResourceName, strlen(pszResourceName));

    return ((int) (uHashValue % iNumGates));

}



static ResLockEntry *RLckGetEntry(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName)
{

    SYS_PTRUINT     uListPtr = RAC.pRWG[iWaitGate].uResList;

    while (uListPtr != END_OF_LIST)
    {
        ResLockEntry   *pRLE = (ResLockEntry *) HEAP_ADDRESS(RAC.pHeapBase, uListPtr);

        if (stricmp(pszResourceName, pRLE->szName) == 0)
            return (pRLE);

        uListPtr = pRLE->uNext;
    }

    return (NULL);

}



static int      RLckRemoveEntry(ResArenaConnection & RAC, int iWaitGate,
                        ResLockEntry * pRLE)
{

    SYS_PTRUINT     uPrevPtr = END_OF_LIST,
                    uListPtr = RAC.pRWG[iWaitGate].uResList;

    while (uListPtr != END_OF_LIST)
    {
        ResLockEntry   *pCurrRLE = (ResLockEntry *) HEAP_ADDRESS(RAC.pHeapBase, uListPtr);

        if (pCurrRLE == pRLE)
            break;

        uPrevPtr = uListPtr;
        uListPtr = pCurrRLE->uNext;
    }

    if (uListPtr == END_OF_LIST)
    {
        ErrSetErrorCode(ERR_LOCK_ENTRY_NOT_FOUND);
        return (ERR_LOCK_ENTRY_NOT_FOUND);
    }

///////////////////////////////////////////////////////////////////////////////
//  Delete entry from list
///////////////////////////////////////////////////////////////////////////////
    if (uPrevPtr == END_OF_LIST)
        RAC.pRWG[iWaitGate].uResList = pRLE->uNext;
    else
    {
        ResLockEntry   *pPrevRLE = (ResLockEntry *) HEAP_ADDRESS(RAC.pHeapBase, uPrevPtr);

        pPrevRLE->uNext = pRLE->uNext;
    }

///////////////////////////////////////////////////////////////////////////////
//  Free heap memory
///////////////////////////////////////////////////////////////////////////////
    if (HeapFree(RAC.pRLA->MHD, RAC.pHeapBase, HEAP_OFFSET(pRLE, RAC.pHeapBase)) < 0)
        return (ErrGetErrorCode());


    return (0);

}



static ResLockEntry *RLckAllocEntry(ResArenaConnection & RAC, char const * pszResourceName)
{

    SYS_PTRUINT     uEntryPtr = HeapAlloc(RAC.pRLA->MHD, RAC.pHeapBase,
            sizeof(ResLockEntry) + strlen(pszResourceName));

    if (uEntryPtr == 0)
        return (NULL);

    ResLockEntry   *pRLE = (ResLockEntry *) HEAP_ADDRESS(RAC.pHeapBase, uEntryPtr);

    pRLE->uNext = END_OF_LIST;
    pRLE->ShLocks = 0;
    pRLE->ExLocks = 0;
    strcpy(pRLE->szName, pszResourceName);

    return (pRLE);

}



static int      RLckTryLockEX(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(RAC, iWaitGate, pszResourceName);

    if (pRLE == NULL)
    {
        if ((pRLE = RLckAllocEntry(RAC, pszResourceName)) == NULL)
            return (ErrGetErrorCode());

        pRLE->ExLocks = 1;

///////////////////////////////////////////////////////////////////////////////
//  Insert new entry in resource list
///////////////////////////////////////////////////////////////////////////////
        pRLE->uNext = RAC.pRWG[iWaitGate].uResList;

        RAC.pRWG[iWaitGate].uResList = HEAP_OFFSET(pRLE, RAC.pHeapBase);
    }
    else
    {
        if (pRLE->ExLocks || (pRLE->ShLocks != 0))
        {
            ErrSetErrorCode(ERR_LOCKED_RESOURCE);
            return (ERR_LOCKED_RESOURCE);
        }

        pRLE->ExLocks = 1;
    }

    return (0);

}



static int      RLckDoUnlockEX(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(RAC, iWaitGate, pszResourceName);

    if ((pRLE == NULL) || !pRLE->ExLocks)
    {
        ErrSetErrorCode(ERR_RESOURCE_NOT_LOCKED);
        return (ERR_RESOURCE_NOT_LOCKED);
    }

    pRLE->ExLocks = 0;

///////////////////////////////////////////////////////////////////////////////
//  Remove entry from list and delete entry heap memory
///////////////////////////////////////////////////////////////////////////////
    if (RLckRemoveEntry(RAC, iWaitGate, pRLE) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Release waiting processes
///////////////////////////////////////////////////////////////////////////////
    if (RAC.pRWG[iWaitGate].iWaitingProcesses > 0)
    {
        SYS_SEMAPHORE   SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT,
                RAC.pRWG[iWaitGate].SemName);

        if (SemID == SYS_INVALID_SEMAPHORE)
            return (ErrGetErrorCode());


        SysReleaseSemaphore(SemID, RAC.pRWG[iWaitGate].iWaitingProcesses);

        RAC.pRWG[iWaitGate].iWaitingProcesses = 0;

        SysCloseSemaphore(SemID);
    }

    return (0);

}



static int      RLckTryLockSH(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(RAC, iWaitGate, pszResourceName);

    if (pRLE == NULL)
    {
        if ((pRLE = RLckAllocEntry(RAC, pszResourceName)) == NULL)
            return (ErrGetErrorCode());

        pRLE->ShLocks = 1;

///////////////////////////////////////////////////////////////////////////////
//  Insert new entry in resource list
///////////////////////////////////////////////////////////////////////////////
        pRLE->uNext = RAC.pRWG[iWaitGate].uResList;

        RAC.pRWG[iWaitGate].uResList = HEAP_OFFSET(pRLE, RAC.pHeapBase);
    }
    else
    {
        if (pRLE->ExLocks)
        {
            ErrSetErrorCode(ERR_LOCKED_RESOURCE);
            return (ERR_LOCKED_RESOURCE);
        }

        ++pRLE->ShLocks;
    }

    return (0);

}



static int      RLckDoUnlockSH(ResArenaConnection & RAC, int iWaitGate,
                        char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(RAC, iWaitGate, pszResourceName);

    if ((pRLE == NULL) || (pRLE->ShLocks == 0))
    {
        ErrSetErrorCode(ERR_RESOURCE_NOT_LOCKED);
        return (ERR_RESOURCE_NOT_LOCKED);
    }

    if (--pRLE->ShLocks == 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  Remove entry from list and delete entry heap memory
///////////////////////////////////////////////////////////////////////////////
        if (RLckRemoveEntry(RAC, iWaitGate, pRLE) < 0)
            return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Release waiting processes
///////////////////////////////////////////////////////////////////////////////
        if (RAC.pRWG[iWaitGate].iWaitingProcesses > 0)
        {
            SYS_SEMAPHORE   SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT,
                    RAC.pRWG[iWaitGate].SemName);

            if (SemID == SYS_INVALID_SEMAPHORE)
                return (ErrGetErrorCode());


            SysReleaseSemaphore(SemID, RAC.pRWG[iWaitGate].iWaitingProcesses);

            RAC.pRWG[iWaitGate].iWaitingProcesses = 0;

            SysCloseSemaphore(SemID);
        }
    }

    return (0);

}



static RLCK_HANDLE RLckLock(char const * pszResourceName,
                        int (*pLockProc) (ResArenaConnection &, int, char const *))
{

    ResArenaConnection RAC;

    if (RLckConnectArena(RAC) < 0)
        return (INVALID_RLCK_HANDLE);


    int             iWaitGate = -1;
    SYS_SEMAPHORE   SemID = SYS_INVALID_SEMAPHORE;

    for (;;)
    {
        if (RLckLockArena(RAC) < 0)
        {
            RLckDisconnectArena(RAC);
            return (INVALID_RLCK_HANDLE);
        }

        if (iWaitGate < 0)
            iWaitGate = RLckGetWaitGate(pszResourceName, RAC.pRLA->iNumGates);


        int             iLockResult = pLockProc(RAC, iWaitGate, pszResourceName);


        if (iLockResult == ERR_LOCKED_RESOURCE)
        {
            if ((SemID == SYS_INVALID_SEMAPHORE) &&
                    ((SemID = SysConnectSemaphore(0, SYS_DEFAULT_MAXCOUNT,
                                            RAC.pRWG[iWaitGate].SemName)) == SYS_INVALID_SEMAPHORE))
            {
                RLckUnlockArena(RAC);
                RLckDisconnectArena(RAC);
                return (INVALID_RLCK_HANDLE);
            }

            ++RAC.pRWG[iWaitGate].iWaitingProcesses;

            RLckUnlockArena(RAC);

            if (SysWaitSemaphore(SemID, SYS_INFINITE_TIMEOUT) < 0)
            {
                SysCloseSemaphore(SemID);
                RLckDisconnectArena(RAC);
                return (INVALID_RLCK_HANDLE);
            }
        }
        else if (iLockResult == 0)
        {
            RLckUnlockArena(RAC);

            break;
        }
        else
        {
            if (SemID != SYS_INVALID_SEMAPHORE)
                SysCloseSemaphore(SemID);
            RLckUnlockArena(RAC);
            RLckDisconnectArena(RAC);
            return (INVALID_RLCK_HANDLE);
        }
    }

    if (SemID != SYS_INVALID_SEMAPHORE)
        SysCloseSemaphore(SemID);

    RLckDisconnectArena(RAC);

    return ((RLCK_HANDLE) SysStrDup(pszResourceName));

}



static int      RLckUnlock(RLCK_HANDLE hLock,
                        int (*pUnlockProc) (ResArenaConnection &, int, char const *))
{

    char           *pszResourceName = (char *) hLock;
    ResArenaConnection RAC;

    if (RLckConnectArena(RAC) < 0)
    {
        SysFree(pszResourceName);
        return (ErrGetErrorCode());
    }

    if (RLckLockArena(RAC) < 0)
    {
        ErrorPush();
        RLckDisconnectArena(RAC);
        SysFree(pszResourceName);
        return (ErrorPop());
    }

    int             iWaitGate = RLckGetWaitGate(pszResourceName, RAC.pRLA->iNumGates);

    if (pUnlockProc(RAC, iWaitGate, pszResourceName) < 0)
    {
        ErrorPush();
        RLckUnlockArena(RAC);
        RLckDisconnectArena(RAC);
        SysFree(pszResourceName);
        return (ErrorPop());
    }

    RLckUnlockArena(RAC);

    RLckDisconnectArena(RAC);

    SysFree(pszResourceName);

    return (0);

}



RLCK_HANDLE     RLckLockEX(char const * pszResourceName)
{

    return (RLckLock(pszResourceName, RLckTryLockEX));

}



RLCK_HANDLE     RLckLockEX(unsigned int uUserID, unsigned int uResID)
{

    char            szResourceName[256] = "";

    RLckGetResourceName(uUserID, uResID, szResourceName);

    return (RLckLockEX(szResourceName));

}



int             RLckUnlockEX(RLCK_HANDLE hLock)
{

    return (RLckUnlock(hLock, RLckDoUnlockEX));

}



RLCK_HANDLE     RLckLockSH(char const * pszResourceName)
{

    return (RLckLock(pszResourceName, RLckTryLockSH));

}



RLCK_HANDLE     RLckLockSH(unsigned int uUserID, unsigned int uResID)
{

    char            szResourceName[256] = "";

    RLckGetResourceName(uUserID, uResID, szResourceName);

    return (RLckLockSH(szResourceName));

}



int             RLckUnlockSH(RLCK_HANDLE hLock)
{

    return (RLckUnlock(hLock, RLckDoUnlockSH));

}
