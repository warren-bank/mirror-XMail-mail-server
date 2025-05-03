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
#include "SList.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "MailConfig.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "POP3Svr.h"
#include "POP3Utils.h"
#include "MessQueue.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "POP3GwLink.h"
#include "PSYNCSvr.h"






#define PSYNC_TRIGGER_FILE          ".psync-trigger"
#define PSYNC_WAIT_SLEEP            2
#define MAX_CLIENTS_WAIT            300
#define PSYNC_WAKEUP_TIME           2
#define PSYNC_SERVER_NAME           "[" APP_NAME_VERSION_OS_STR " PSYNC Server]"








static bool     PSYNCNeedSync(void);
static PSYNCConfig *PSYNCGetConfigCopy(SHB_HANDLE hShbPSYNC);
static int      PSYNCThreadCountAdd(long lCount, SHB_HANDLE hShbPSYNC,
                        PSYNCConfig * pPSYNCCfg = NULL);
static int      PSYNCTimeToStop(SHB_HANDLE hShbPSYNC);
static int      PSYNCStartTransfer(SHB_HANDLE hShbPSYNC, PSYNCConfig * pPSYNCCfg);
unsigned int    PSYNCThreadSyncProc(void *pThreadData);
static int      PSYNCThreadNotifyExit(void);








static bool     PSYNCNeedSync(void)
{

    char            szTriggerPath[SYS_MAX_PATH] = "";

    CfgGetRootPath(szTriggerPath);

    strcat(szTriggerPath, PSYNC_TRIGGER_FILE);

///////////////////////////////////////////////////////////////////////////////
//  Check for the presence of the trigger file
///////////////////////////////////////////////////////////////////////////////
    if (!SysExistFile(szTriggerPath))
        return (false);


    SysRemove(szTriggerPath);

    return (true);

}



static PSYNCConfig *PSYNCGetConfigCopy(SHB_HANDLE hShbPSYNC)
{

    PSYNCConfig    *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

    if (pPSYNCCfg == NULL)
        return (NULL);

    PSYNCConfig    *pNewPSYNCCfg = (PSYNCConfig *) SysAlloc(sizeof(PSYNCConfig));

    if (pNewPSYNCCfg != NULL)
        memcpy(pNewPSYNCCfg, pPSYNCCfg, sizeof(PSYNCConfig));

    ShbUnlock(hShbPSYNC);

    return (pNewPSYNCCfg);

}



static int      PSYNCThreadCountAdd(long lCount, SHB_HANDLE hShbPSYNC,
                        PSYNCConfig * pPSYNCCfg)
{

    int             iDoUnlock = 0;

    if (pPSYNCCfg == NULL)
    {
        if ((pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC)) == NULL)
            return (ErrGetErrorCode());

        ++iDoUnlock;
    }

    pPSYNCCfg->lThreadCount += lCount;

    if (iDoUnlock)
        ShbUnlock(hShbPSYNC);

    return (0);

}



static int      PSYNCTimeToStop(SHB_HANDLE hShbPSYNC)
{

    PSYNCConfig    *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

    if (pPSYNCCfg == NULL)
        return (1);


    int             iTimeToStop = (pPSYNCCfg->ulFlags & PSYNCF_STOP_SERVER) ? 1 : 0;


    ShbUnlock(hShbPSYNC);

    return (iTimeToStop);


}



unsigned int    PSYNCThreadProc(void *pThreadData)
{

    SysLogMessage(LOG_LEV_MESSAGE, "%s started\n", PSYNC_SERVER_NAME);


    int             iElapsedTime = 0;

    for (;;)
    {
        SysSleep(PSYNC_WAKEUP_TIME);

        iElapsedTime += PSYNC_WAKEUP_TIME;


        PSYNCConfig    *pPSYNCCfg = PSYNCGetConfigCopy(hShbPSYNC);

        if (pPSYNCCfg == NULL)
            break;

        if (pPSYNCCfg->ulFlags & PSYNCF_STOP_SERVER)
        {
            SysFree(pPSYNCCfg);
            break;
        }

        if ((pPSYNCCfg->iSyncInterval == 0) ||
                ((iElapsedTime < pPSYNCCfg->iSyncInterval) && !PSYNCNeedSync()))
        {
            SysFree(pPSYNCCfg);
            continue;
        }

        iElapsedTime = 0;


        PSYNCStartTransfer(hShbPSYNC, pPSYNCCfg);


        SysFree(pPSYNCCfg);
    }

///////////////////////////////////////////////////////////////////////////////
//  Wait for client completion
///////////////////////////////////////////////////////////////////////////////
    for (int iTotalWait = 0; (iTotalWait < MAX_CLIENTS_WAIT); iTotalWait += PSYNC_WAIT_SLEEP)
    {
        PSYNCConfig    *pPSYNCCfg = (PSYNCConfig *) ShbLock(hShbPSYNC);

        if (pPSYNCCfg == NULL)
            break;

        long            lThreadCount = pPSYNCCfg->lThreadCount;

        ShbUnlock(hShbPSYNC);

        if (lThreadCount == 0)
            break;

        SysSleep(PSYNC_WAIT_SLEEP);
    }


    SysLogMessage(LOG_LEV_MESSAGE, "%s stopped\n", PSYNC_SERVER_NAME);

    return (0);

}



static int      PSYNCStartTransfer(SHB_HANDLE hShbPSYNC, PSYNCConfig * pPSYNCCfg)
{

    GWLKF_HANDLE    hLinksDB = GwLkOpenDB();

    if (hLinksDB == INVALID_GWLKF_HANDLE)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString(ErrorFetch()));
        return (ErrorPop());
    }


    POP3Link       *pPopLnk = GwLkGetFirstUser(hLinksDB);

    for (; pPopLnk != NULL; pPopLnk = GwLkGetNextUser(hLinksDB))
    {
///////////////////////////////////////////////////////////////////////////////
//  Check if link is enabled
///////////////////////////////////////////////////////////////////////////////
        if (GwLkCheckEnabled(pPopLnk) < 0)
            continue;


        if (SysWaitSemaphore(hSyncSem, SYS_INFINITE_TIMEOUT) < 0)
            break;

        if (PSYNCTimeToStop(hShbPSYNC))
            break;


        SYS_THREAD      hClientThread = SysCreateThread(PSYNCThreadSyncProc, pPopLnk);

        if (hClientThread != SYS_INVALID_THREAD)
            SysCloseThread(hClientThread, 0);
        else
        {
            GwLkFreePOP3Link(pPopLnk);

            SysReleaseSemaphore(hSyncSem, 1);
        }
    }

    GwLkCloseDB(hLinksDB);

    return (0);

}




static int      PSYNCThreadNotifyExit(void)
{

    SysReleaseSemaphore(hSyncSem, 1);

    return (0);

}



unsigned int    PSYNCThreadSyncProc(void *pThreadData)
{

    POP3Link       *pPopLnk = (POP3Link *) pThreadData;

    SysLogMessage(LOG_LEV_MESSAGE, "[PSYNC] entry\n");

///////////////////////////////////////////////////////////////////////////////
//  Get configuration handle
///////////////////////////////////////////////////////////////////////////////
    SVRCFG_HANDLE   hSvrConfig = SvrGetConfigHandle();

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_MESSAGE, "%s\n", ErrGetErrorString(ErrorFetch()));

        GwLkFreePOP3Link(pPopLnk);
///////////////////////////////////////////////////////////////////////////////
//  Notify thread exit semaphore
///////////////////////////////////////////////////////////////////////////////
        PSYNCThreadNotifyExit();
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Get the error account for email that the server is not able to deliver coz
//  it does not find informations about where it has to deliver
///////////////////////////////////////////////////////////////////////////////
    char            szErrorAccount[MAX_ADDR_NAME] = "";

    SvrConfigVar("Pop3SyncErrorAccount", szErrorAccount, sizeof(szErrorAccount) - 1,
            hSvrConfig, "");

    char const     *pszErrorAccount = (IsEmptyString(szErrorAccount)) ? NULL: szErrorAccount;

///////////////////////////////////////////////////////////////////////////////
//  Lock the link
///////////////////////////////////////////////////////////////////////////////
    if (GwLkLinkLock(pPopLnk) < 0)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_MESSAGE, "%s\n", ErrGetErrorString(ErrorFetch()));

        SvrReleaseConfigHandle(hSvrConfig);
        GwLkFreePOP3Link(pPopLnk);
///////////////////////////////////////////////////////////////////////////////
//  Notify thread exit semaphore
///////////////////////////////////////////////////////////////////////////////
        PSYNCThreadNotifyExit();
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Increase threads count
///////////////////////////////////////////////////////////////////////////////
    PSYNCThreadCountAdd(+1, hShbPSYNC);

///////////////////////////////////////////////////////////////////////////////
//  Sync for real internal account ?
///////////////////////////////////////////////////////////////////////////////
    if (GwLkLocalDomain(pPopLnk))
    {
///////////////////////////////////////////////////////////////////////////////
//  Verify user credentials
///////////////////////////////////////////////////////////////////////////////
        UserInfo       *pUI = UsrGetUserByName(pPopLnk->pszDomain, pPopLnk->pszName);

        if (pUI != NULL)
        {
            SysLogMessage(LOG_LEV_MESSAGE, "[PSYNC] User = \"%s\" - Domain = \"%s\"\n",
                    pPopLnk->pszName, pPopLnk->pszDomain);

///////////////////////////////////////////////////////////////////////////////
//  Sync
///////////////////////////////////////////////////////////////////////////////
            char            szUserAddress[MAX_ADDR_NAME] = "";

            UsrGetAddress(pUI, szUserAddress);

            if (UPopSyncRemoteLink(szUserAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName,
                            pPopLnk->pszRmtPassword, pPopLnk->pszAuthType, pszErrorAccount) < 0)
                ErrLogMessage(LOG_LEV_MESSAGE, "[PSYNC] User = \"%s\" - Domain = \"%s\" Failed !\n",
                        pPopLnk->pszName, pPopLnk->pszDomain);


            UsrFreeUserInfo(pUI);
        }
        else
            SysLogMessage(LOG_LEV_MESSAGE, "[PSYNC] User = \"%s\" - Domain = \"%s\" Failed !\n"
                    "Error = %s\n", pPopLnk->pszName, pPopLnk->pszDomain, ErrGetErrorString());

    }
    else if (GwLkMasqueradeDomain(pPopLnk))
    {
        SysLogMessage(LOG_LEV_MESSAGE,
                "[PSYNC/MASQ] MasqDomain = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\"\n",
                pPopLnk->pszDomain + 1, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);

///////////////////////////////////////////////////////////////////////////////
//  Sync ( "pszDomain" == "?" + masq-domain or "pszDomain" == "&" + add-domain )
///////////////////////////////////////////////////////////////////////////////
        if (UPopSyncRemoteLink(pPopLnk->pszDomain, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName,
                        pPopLnk->pszRmtPassword, pPopLnk->pszAuthType, pszErrorAccount) < 0)
            ErrLogMessage(LOG_LEV_MESSAGE,
                    "[PSYNC/MASQ] MasqDomain = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\" Failed !\n",
                    pPopLnk->pszDomain + 1, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);

    }
    else
    {
        char            szSyncAddress[MAX_ADDR_NAME] = "";

        sprintf(szSyncAddress, "%s%s", pPopLnk->pszName, pPopLnk->pszDomain);


        SysLogMessage(LOG_LEV_MESSAGE,
                "[PSYNC/EXT] Acount = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\"\n",
                szSyncAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);

///////////////////////////////////////////////////////////////////////////////
//  Sync ( "pszDomain" == "@" + domain )
///////////////////////////////////////////////////////////////////////////////
        if (UPopSyncRemoteLink(szSyncAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName,
                        pPopLnk->pszRmtPassword, pPopLnk->pszAuthType, pszErrorAccount) < 0)
            ErrLogMessage(LOG_LEV_MESSAGE,
                    "[PSYNC/EXT] Acount = \"%s\" - RmtDomain = \"%s\" - RmtName = \"%s\" Failed !\n",
                    szSyncAddress, pPopLnk->pszRmtDomain, pPopLnk->pszRmtName);
    }

///////////////////////////////////////////////////////////////////////////////
//  Decrease threads count
///////////////////////////////////////////////////////////////////////////////
    PSYNCThreadCountAdd(-1, hShbPSYNC);


    GwLkLinkUnlock(pPopLnk);
    SvrReleaseConfigHandle(hSvrConfig);
    GwLkFreePOP3Link(pPopLnk);

///////////////////////////////////////////////////////////////////////////////
//  Notify thread exit semaphore
///////////////////////////////////////////////////////////////////////////////
    PSYNCThreadNotifyExit();


    SysLogMessage(LOG_LEV_MESSAGE, "[PSYNC] exit\n");

    return (0);

}
