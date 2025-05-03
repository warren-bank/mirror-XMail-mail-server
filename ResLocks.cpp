/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,2000,2001  Davide Libenzi
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
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */


#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "ShBlocks.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "MessQueue.h"
#include "MailConfig.h"
#include "MailSvr.h"







#define STD_WAIT_GATES              37









struct ResWaitGate
{
    SYS_SEMAPHORE   hSemaphore;
    int             iWaitingProcesses;
    SysListHead     ResList;
};

struct ResLockEntry
{
    SysListHead     LLink;
    int             iShLocks;
    int             iExLocks;
    char            szName[1];
};










static char    *RLckGetResourceName(unsigned int uUserID, unsigned int uResID,
                        char *pszResourceName);
static int      RLckGetWaitGate(char const * pszResourceName);
static ResLockEntry *RLckGetEntry(int iWaitGate, char const * pszResourceName);
static int      RLckRemoveEntry(int iWaitGate, ResLockEntry * pRLE);
static ResLockEntry *RLckAllocEntry(char const * pszResourceName);
static int      RLckFreeEntry(ResLockEntry * pRLE);
static int      RLckTryLockEX(int iWaitGate, char const * pszResourceName);
static int      RLckDoUnlockEX(int iWaitGate, char const * pszResourceName);
static int      RLckTryLockSH(int iWaitGate, char const * pszResourceName);
static int      RLckDoUnlockSH(int iWaitGate, char const * pszResourceName);
static RLCK_HANDLE RLckLock(char const * pszResourceName, int (*pLockProc) (int, char const *));
static int      RLckUnlock(RLCK_HANDLE hLock, int (*pUnlockProc) (int, char const *));













static SYS_MUTEX hRLMutex = SYS_INVALID_MUTEX;
static ResWaitGate RLGates[STD_WAIT_GATES];













int             RLckInitLockers(void)
{
///////////////////////////////////////////////////////////////////////////////
//  Create resource locking mutex
///////////////////////////////////////////////////////////////////////////////
    if ((hRLMutex = SysCreateMutex()) == SYS_INVALID_MUTEX)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Initialize wait gates
///////////////////////////////////////////////////////////////////////////////
    for (int ii = 0; ii < STD_WAIT_GATES; ii++)
    {
        if ((RLGates[ii].hSemaphore = SysCreateSemaphore(0,
                                SYS_DEFAULT_MAXCOUNT)) == SYS_INVALID_SEMAPHORE)
        {
            ErrorPush();

            for (--ii; ii >= 0; ii--)
                SysCloseSemaphore(RLGates[ii].hSemaphore);

            SysCloseMutex(hRLMutex);
            return (ErrorPop());
        }

        RLGates[ii].iWaitingProcesses = 0;

        SYS_INIT_LIST_HEAD(&RLGates[ii].ResList);
    }

    return (0);

}



int             RLckCleanupLockers(void)
{

    SysLockMutex(hRLMutex, SYS_INFINITE_TIMEOUT);

    for (int ii = 0; ii < STD_WAIT_GATES; ii++)
    {
        SysListHead    *pLLink;

        while ((pLLink = SYS_LIST_FIRST(&RLGates[ii].ResList)) != NULL)
        {
            ResLockEntry   *pRLE = SYS_LIST_ENTRY(pLLink, ResLockEntry, LLink);

            SYS_LIST_DEL(&pRLE->LLink);

            RLckFreeEntry(pRLE);
        }


        SysCloseSemaphore(RLGates[ii].hSemaphore);
    }

    SysUnlockMutex(hRLMutex);

    SysCloseMutex(hRLMutex);

    return (0);

}



static char    *RLckGetResourceName(unsigned int uUserID, unsigned int uResID,
                        char *pszResourceName)
{

    sprintf(pszResourceName, "$>usr%u.res%u", uUserID, uResID);

    return (pszResourceName);

}



static int      RLckGetWaitGate(char const * pszResourceName)
{

    SYS_UINT32      uHashValue = MscHashString(pszResourceName, strlen(pszResourceName));

    return ((int) (uHashValue % STD_WAIT_GATES));

}



static ResLockEntry *RLckGetEntry(int iWaitGate, char const * pszResourceName)
{

    SysListHead    *pLLink;

    SYS_LIST_FOR_EACH(pLLink, &RLGates[iWaitGate].ResList)
    {
        ResLockEntry   *pRLE = SYS_LIST_ENTRY(pLLink, ResLockEntry, LLink);

        if (stricmp(pszResourceName, pRLE->szName) == 0)
            return (pRLE);

    }

    ErrSetErrorCode(ERR_LOCK_ENTRY_NOT_FOUND);

    return (NULL);

}



static int      RLckRemoveEntry(int iWaitGate, ResLockEntry * pRLE)
{

    SysListHead    *pLLink;

    SYS_LIST_FOR_EACH(pLLink, &RLGates[iWaitGate].ResList)
    {
        ResLockEntry   *pCurrRLE = SYS_LIST_ENTRY(pLLink, ResLockEntry, LLink);

        if (pCurrRLE == pRLE)
        {
            SYS_LIST_DEL(&pRLE->LLink);

            RLckFreeEntry(pRLE);

            return (0);
        }
    }

    ErrSetErrorCode(ERR_LOCK_ENTRY_NOT_FOUND);
    return (ERR_LOCK_ENTRY_NOT_FOUND);

}



static ResLockEntry *RLckAllocEntry(char const * pszResourceName)
{

    ResLockEntry   *pRLE = (ResLockEntry *) SysAlloc(sizeof(ResLockEntry) +
            strlen(pszResourceName));

    if (pRLE == NULL)
        return (NULL);

    SYS_INIT_LIST_HEAD(&pRLE->LLink);
    pRLE->iShLocks = 0;
    pRLE->iExLocks = 0;
    strcpy(pRLE->szName, pszResourceName);

    return (pRLE);

}



static int      RLckFreeEntry(ResLockEntry * pRLE)
{

    SysFree(pRLE);

    return (0);

}



static int      RLckTryLockEX(int iWaitGate, char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(iWaitGate, pszResourceName);

    if (pRLE == NULL)
    {
        if ((pRLE = RLckAllocEntry(pszResourceName)) == NULL)
            return (ErrGetErrorCode());

        pRLE->iExLocks = 1;

///////////////////////////////////////////////////////////////////////////////
//  Insert new entry in resource list
///////////////////////////////////////////////////////////////////////////////
        SYS_LIST_ADDH(&pRLE->LLink, &RLGates[iWaitGate].ResList);

    }
    else
    {
        if ((pRLE->iExLocks > 0) || (pRLE->iShLocks > 0))
        {
            ErrSetErrorCode(ERR_LOCKED_RESOURCE);
            return (ERR_LOCKED_RESOURCE);
        }

        pRLE->iExLocks = 1;
    }

    return (0);

}



static int      RLckDoUnlockEX(int iWaitGate, char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(iWaitGate, pszResourceName);

    if ((pRLE == NULL) || (pRLE->iExLocks == 0))
    {
        ErrSetErrorCode(ERR_RESOURCE_NOT_LOCKED);
        return (ERR_RESOURCE_NOT_LOCKED);
    }

    pRLE->iExLocks = 0;

///////////////////////////////////////////////////////////////////////////////
//  Remove entry from list and delete entry memory
///////////////////////////////////////////////////////////////////////////////
    if (RLckRemoveEntry(iWaitGate, pRLE) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Release waiting processes
///////////////////////////////////////////////////////////////////////////////
    if (RLGates[iWaitGate].iWaitingProcesses > 0)
    {
        SysReleaseSemaphore(RLGates[iWaitGate].hSemaphore, RLGates[iWaitGate].iWaitingProcesses);

        RLGates[iWaitGate].iWaitingProcesses = 0;
    }

    return (0);

}



static int      RLckTryLockSH(int iWaitGate, char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(iWaitGate, pszResourceName);

    if (pRLE == NULL)
    {
        if ((pRLE = RLckAllocEntry(pszResourceName)) == NULL)
            return (ErrGetErrorCode());

        pRLE->iShLocks = 1;

///////////////////////////////////////////////////////////////////////////////
//  Insert new entry in resource list
///////////////////////////////////////////////////////////////////////////////
        SYS_LIST_ADDH(&pRLE->LLink, &RLGates[iWaitGate].ResList);

    }
    else
    {
        if (pRLE->iExLocks > 0)
        {
            ErrSetErrorCode(ERR_LOCKED_RESOURCE);
            return (ERR_LOCKED_RESOURCE);
        }

        ++pRLE->iShLocks;
    }

    return (0);

}



static int      RLckDoUnlockSH(int iWaitGate, char const * pszResourceName)
{

    ResLockEntry   *pRLE = RLckGetEntry(iWaitGate, pszResourceName);

    if ((pRLE == NULL) || (pRLE->iShLocks == 0))
    {
        ErrSetErrorCode(ERR_RESOURCE_NOT_LOCKED);
        return (ERR_RESOURCE_NOT_LOCKED);
    }

    if (--pRLE->iShLocks == 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  Remove entry from list and delete entry heap memory
///////////////////////////////////////////////////////////////////////////////
        if (RLckRemoveEntry(iWaitGate, pRLE) < 0)
            return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Release waiting processes
///////////////////////////////////////////////////////////////////////////////
        if (RLGates[iWaitGate].iWaitingProcesses > 0)
        {
            SysReleaseSemaphore(RLGates[iWaitGate].hSemaphore, RLGates[iWaitGate].iWaitingProcesses);

            RLGates[iWaitGate].iWaitingProcesses = 0;
        }
    }

    return (0);

}



static RLCK_HANDLE RLckLock(char const * pszResourceName, int (*pLockProc) (int, char const *))
{

    int             iWaitGate = RLckGetWaitGate(pszResourceName);

    for (;;)
    {
///////////////////////////////////////////////////////////////////////////////
//  Lock resources list access
///////////////////////////////////////////////////////////////////////////////
        if (SysLockMutex(hRLMutex, SYS_INFINITE_TIMEOUT) < 0)
            return (INVALID_RLCK_HANDLE);


        int             iLockResult = pLockProc(iWaitGate, pszResourceName);


        if (iLockResult == ERR_LOCKED_RESOURCE)
        {
            SYS_SEMAPHORE   SemID = RLGates[iWaitGate].hSemaphore;

            ++RLGates[iWaitGate].iWaitingProcesses;

            SysUnlockMutex(hRLMutex);

            if (SysWaitSemaphore(SemID, SYS_INFINITE_TIMEOUT) < 0)
                return (INVALID_RLCK_HANDLE);
        }
        else if (iLockResult == 0)
        {
            SysUnlockMutex(hRLMutex);

            break;
        }
        else
        {
            SysUnlockMutex(hRLMutex);

            return (INVALID_RLCK_HANDLE);
        }
    }

    return ((RLCK_HANDLE) SysStrDup(pszResourceName));

}



static int      RLckUnlock(RLCK_HANDLE hLock, int (*pUnlockProc) (int, char const *))
{

    char           *pszResourceName = (char *) hLock;
    int             iWaitGate = RLckGetWaitGate(pszResourceName);

///////////////////////////////////////////////////////////////////////////////
//  Lock resources list access
///////////////////////////////////////////////////////////////////////////////
    if (SysLockMutex(hRLMutex, SYS_INFINITE_TIMEOUT) < 0)
        return (ErrGetErrorCode());


    if (pUnlockProc(iWaitGate, pszResourceName) < 0)
    {
        ErrorPush();
        SysUnlockMutex(hRLMutex);
        SysFree(pszResourceName);
        return (ErrorPop());
    }


    SysUnlockMutex(hRLMutex);

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
