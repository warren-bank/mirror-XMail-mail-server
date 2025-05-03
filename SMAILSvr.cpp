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


#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "SList.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "Queue.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "SMTPUtils.h"
#include "SMAILUtils.h"
#include "ExtAliases.h"
#include "UsrMailList.h"
#include "MailConfig.h"
#include "SMAILSvr.h"
#include "AppDefines.h"
#include "MailSvr.h"








#define CUSTOM_PROC_LINE_MAX        1024
#define FILTER_LINE_MAX             1024
#define FILTER_TIMEOUT              60
#define FILTER_PRIORITY             SYS_PRIORITY_NORMAL
#define FILTER_OUT_EXITCODE         99
#define MODIFY_EXITCODE             100
#define MAX_PEEK_FILES              32
#define STD_SMAILTHREAD_SLEEP_TIME  4












static SMAILConfig *SMAILGetConfigCopy(SHB_HANDLE hShbSMAIL);
static int      SMAILThreadCountAdd(long lCount, SHB_HANDLE hShbSMAIL,
                        SMAILConfig * pSMAILCfg = NULL);
static int      SMAILLogEnabled(SHB_HANDLE hShbSMAIL, SMAILConfig * pSMAILCfg = NULL);
static int      SMAILTryProcessFile(char const * pszMessFilePath, SHB_HANDLE hShbSMAIL,
                        SMAILConfig * pSMAILCfg);
static int      SMAILTryProcessSpool(SHB_HANDLE hShbSMAIL);
static int      SMAILProcessFile(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool);
static int      SMAILMailingListExplode(UserInfo * pUI, SPLF_HANDLE hFSpool);
static int      SMAILRemoteMsgSMTPSend(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool,
                        char const * pszDestDomain, SMTPError * pSMTPE = NULL);
static int      SMAILHandleRemoteUserMessage(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool,
                        char const * pszDestDomain);
static int      SMAILCustomProcessMessage(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool,
                        char const * pszDestDomain, char const * pszCustFilePath);
static int      SMAILCmdMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool);
static int      SMAILCmd_external(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool);
static int      SMAILCmd_wait(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool);
static int      SMAILCmd_smtp(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool);
static int      SMAILCmd_smtprelay(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool);
static int      SMAILCmd_redirect(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool);
static int      SMAILCmd_lredirect(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool);
static int      SMAILFilterMessage(SHB_HANDLE hShbSMAIL, char const * pszMessFilePath);
static int      SMAILFilterMacroSubstitutes(char **ppszCmdTokens, char const * pszSpoolFilePath,
                        SpoolFileHeader const & SFH);











static SMAILConfig *SMAILGetConfigCopy(SHB_HANDLE hShbSMAIL)
{

    SMAILConfig    *pPSYNCCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

    if (pPSYNCCfg == NULL)
        return (NULL);

    SMAILConfig    *pNewPSYNCCfg = (SMAILConfig *) SysAlloc(sizeof(SMAILConfig));

    if (pNewPSYNCCfg != NULL)
        memcpy(pNewPSYNCCfg, pPSYNCCfg, sizeof(SMAILConfig));

    ShbUnlock(hShbSMAIL);

    return (pNewPSYNCCfg);

}



static int      SMAILThreadCountAdd(long lCount, SHB_HANDLE hShbSMAIL,
                        SMAILConfig * pSMAILCfg)
{

    int             iDoUnlock = 0;

    if (pSMAILCfg == NULL)
    {
        if ((pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL)) == NULL)
            return (ErrGetErrorCode());

        ++iDoUnlock;
    }

    pSMAILCfg->lThreadCount += lCount;

    if (iDoUnlock)
        ShbUnlock(hShbSMAIL);

    return (0);

}



static int      SMAILLogEnabled(SHB_HANDLE hShbSMAIL, SMAILConfig * pSMAILCfg)
{

    int             iDoUnlock = 0;

    if (pSMAILCfg == NULL)
    {
        if ((pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL)) == NULL)
            return (ErrGetErrorCode());

        ++iDoUnlock;
    }

    unsigned long   ulFlags = pSMAILCfg->ulFlags;

    if (iDoUnlock)
        ShbUnlock(hShbSMAIL);

    return ((ulFlags & SMAILF_LOG_ENABLED) ? 1 : 0);

}



unsigned int    SMAILThreadProc(void *pThreadData)
{

    SHB_HANDLE      hShbSMAIL = ShbConnectBlock(SHB_SMAILSvr);

    if (hShbSMAIL == SHB_INVALID_HANDLE)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
        return (ErrorPop());
    }

    SMAILConfig    *pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

    if (pSMAILCfg == NULL)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
        ShbCloseBlock(hShbSMAIL);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Get retry timeout and thread id
///////////////////////////////////////////////////////////////////////////////
    long            lThreadId = pSMAILCfg->lThreadCount;

///////////////////////////////////////////////////////////////////////////////
//  Increase thread count
///////////////////////////////////////////////////////////////////////////////
    SMAILThreadCountAdd(+1, hShbSMAIL, pSMAILCfg);

    ShbUnlock(hShbSMAIL);


    SysLogMessage(LOG_LEV_MESSAGE, "SMAIL thread [%02ld] started\n", lThreadId);

    for (;;)
    {
///////////////////////////////////////////////////////////////////////////////
//  Check shutdown condition
///////////////////////////////////////////////////////////////////////////////
        pSMAILCfg = (SMAILConfig *) ShbLock(hShbSMAIL);

        if ((pSMAILCfg == NULL) || (pSMAILCfg->ulFlags & SMAILF_STOP_SERVER))
        {
            SysLogMessage(LOG_LEV_MESSAGE, "SMAIL thread [%02ld] exiting\n", lThreadId);

            if (pSMAILCfg != NULL)
                ShbUnlock(hShbSMAIL);
            break;
        }

        ShbUnlock(hShbSMAIL);

///////////////////////////////////////////////////////////////////////////////
//  Process spool files
///////////////////////////////////////////////////////////////////////////////
        int             iProcessResult = SMAILTryProcessSpool(hShbSMAIL);

        if (iProcessResult == ERR_NO_SMTP_SPOOL_FILES)
            SysSleep(STD_SMAILTHREAD_SLEEP_TIME);

    }

///////////////////////////////////////////////////////////////////////////////
//  Decrease thread count
///////////////////////////////////////////////////////////////////////////////
    SMAILThreadCountAdd(-1, hShbSMAIL);


    ShbCloseBlock(hShbSMAIL);

    SysLogMessage(LOG_LEV_MESSAGE, "SMAIL thread [%02ld] stopped\n", lThreadId);

    return (0);

}



static int      SMAILTryProcessFile(char const * pszMessFilePath, SHB_HANDLE hShbSMAIL,
                        SMAILConfig * pSMAILCfg)
{

///////////////////////////////////////////////////////////////////////////////
//  Filter message
///////////////////////////////////////////////////////////////////////////////
    if (SMAILFilterMessage(hShbSMAIL, pszMessFilePath) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create the handle to manage the spool file
///////////////////////////////////////////////////////////////////////////////
    SPLF_HANDLE     hFSpool = USmlCreateHandle(pszMessFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
        QueCleanupMessage(pszMessFilePath, true, true);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check for mail loops
///////////////////////////////////////////////////////////////////////////////
    if (USmlMailLoopCheck(hFSpool) < 0)
    {
        ErrorPush();

///////////////////////////////////////////////////////////////////////////////
//  Notify root and remove the message
///////////////////////////////////////////////////////////////////////////////
        char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
        char const     *pszSpoolFile = USmlGetSpoolFile(hFSpool);

        ErrLogMessage(LOG_LEV_MESSAGE,
                "Message <%s> blocked by mail loop check !\n",
                pszSmtpMessageID);
        QueErrLogMessage(pszMessFilePath,
                "Message <%s> blocked by mail loop check !\n",
                pszSmtpMessageID);

        QueSpoolRemoveNotifyRoot(pszMessFilePath, ErrGetErrorString(ErrorFetch()));


        USmlCloseHandle(hFSpool);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Process spool file
///////////////////////////////////////////////////////////////////////////////
    if (SMAILProcessFile(hShbSMAIL, hFSpool) < 0)
    {
        ErrorPush();
        USmlCloseHandle(hFSpool);

        QueResendMessage(pszMessFilePath);

        return (ErrorPop());
    }


    USmlCloseHandle(hFSpool);


///////////////////////////////////////////////////////////////////////////////
//  Remove spool file
///////////////////////////////////////////////////////////////////////////////
    QueCleanupMessage(pszMessFilePath, true, false);


    return (0);

}



static int      SMAILTryProcessSpool(SHB_HANDLE hShbSMAIL)
{

    SMAILConfig    *pSMAILCfg = SMAILGetConfigCopy(hShbSMAIL);

    if (pSMAILCfg == NULL)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Get spool files to process
///////////////////////////////////////////////////////////////////////////////
    char           *pszMessFilesPath[MAX_PEEK_FILES + 1];

    if (QuePeekLockedFiles(NULL, pszMessFilesPath, MAX_PEEK_FILES,
                    pSMAILCfg->iRetryTimeout, pSMAILCfg->iRetryIncrRatio,
                    pSMAILCfg->iMaxRetry) < 0)
    {
        ErrorPush();
        SysFree(pSMAILCfg);
        return (ErrorPop());
    }


///////////////////////////////////////////////////////////////////////////////
//  Messages process loop
///////////////////////////////////////////////////////////////////////////////
    for (int ii = 0; pszMessFilesPath[ii] != NULL; ii++)
    {
///////////////////////////////////////////////////////////////////////////////
//  Check shutdown condition
///////////////////////////////////////////////////////////////////////////////
        if (SvrInShutdown())
        {
            for (; pszMessFilesPath[ii] != NULL; ii++)
            {
                QueUnlockMessage(pszMessFilesPath[ii]);
                SysFree(pszMessFilesPath[ii]);
            }

            SysFree(pSMAILCfg);

            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            return (ERR_SERVER_SHUTDOWN);
        }

///////////////////////////////////////////////////////////////////////////////
//  Process spool file
///////////////////////////////////////////////////////////////////////////////
        SMAILTryProcessFile(pszMessFilesPath[ii], hShbSMAIL, pSMAILCfg);


///////////////////////////////////////////////////////////////////////////////
//  Unlock spool file
///////////////////////////////////////////////////////////////////////////////
        QueUnlockMessage(pszMessFilesPath[ii]);

        SysFree(pszMessFilesPath[ii]);
    }


    SysFree(pSMAILCfg);

    return (0);

}



static int      SMAILProcessFile(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool)
{

    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *pszSpoolFile = USmlGetSpoolFile(hFSpool);
    char const     *pszMailFrom = USmlMailFrom(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);

    int             iFromDomains = StrStringsCount(ppszFrom),
                    iRcptDomains = StrStringsCount(ppszRcpt);

    char            szDestUser[MAX_ADDR_NAME] = "",
                    szDestDomain[MAX_ADDR_NAME] = "";

    if ((iRcptDomains < 1) ||
            (USmtpSplitEmailAddr(ppszRcpt[0], szDestUser, szDestDomain) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Check if we are at home
///////////////////////////////////////////////////////////////////////////////
    UserInfo       *pUI = UsrGetUserByNameOrAlias(szDestDomain, szDestUser);

    if (pUI != NULL)
    {
        SysLogMessage(LOG_LEV_MESSAGE, "SMAIL local SMTP = \"%s\" From = <%s> To = <%s>\n",
                pszSMTPDomain, pszMailFrom, ppszRcpt[0]);

        if (UsrGetUserType(pUI) == usrTypeUser)
        {
///////////////////////////////////////////////////////////////////////////////
//  Local user case
///////////////////////////////////////////////////////////////////////////////
            LocalMailProcConfig LMPC;

            ZeroData(LMPC);
            LMPC.ulFlags = (SMAILLogEnabled(hShbSMAIL)) ? LMPCF_LOG_ENABLED : 0;

            if (USmlProcessLocalUserMessage(pUI, hFSpool, LMPC) < 0)
            {
                ErrorPush();
                UsrFreeUserInfo(pUI);
                return (ErrorPop());
            }
        }
        else
        {
///////////////////////////////////////////////////////////////////////////////
//  Local mailing list case
///////////////////////////////////////////////////////////////////////////////
            if (SMAILMailingListExplode(pUI, hFSpool) < 0)
            {
                ErrorPush();
                UsrFreeUserInfo(pUI);
                return (ErrorPop());
            }
        }

        UsrFreeUserInfo(pUI);
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Remote user case ( or custom domain user )
///////////////////////////////////////////////////////////////////////////////
        if (SMAILHandleRemoteUserMessage(hShbSMAIL, hFSpool, szDestDomain) < 0)
            return (ErrGetErrorCode());

    }

    return (0);

}




static int      SMAILMailingListExplode(UserInfo * pUI, SPLF_HANDLE hFSpool)
{

    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);

    char            szDestUser[MAX_ADDR_NAME] = "",
                    szDestDomain[MAX_ADDR_NAME] = "";

    if (USmtpSplitEmailAddr(ppszRcpt[0], szDestUser, szDestDomain) < 0)
        return (ErrGetErrorCode());


    USRML_HANDLE    hUsersDB = UsrMLOpenDB(pUI);

    if (hUsersDB == INVALID_USRML_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Mailing list scan
///////////////////////////////////////////////////////////////////////////////
    MLUserInfo     *pMLUI = UsrMLGetFirstUser(hUsersDB);

    for (; pMLUI != NULL; pMLUI = UsrMLGetNextUser(hUsersDB))
    {
///////////////////////////////////////////////////////////////////////////////
//  Get unique spool/tmp file path
///////////////////////////////////////////////////////////////////////////////
        char            szSpoolTmpFile[SYS_MAX_PATH] = "";

        if (QueGetTempFile(NULL, szSpoolTmpFile, iQueueSplitLevel) < 0)
        {
            ErrorPush();
            UsrMLFreeUser(pMLUI);
            UsrMLCloseDB(hUsersDB);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Create spool file
///////////////////////////////////////////////////////////////////////////////
        if (USmlCreateSpoolFile(hFSpool, NULL, pMLUI->pszAddress, szSpoolTmpFile) < 0)
        {
            ErrorPush();
            UsrMLFreeUser(pMLUI);
            UsrMLCloseDB(hUsersDB);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitTempMessage(szSpoolTmpFile) < 0)
        {
            ErrorPush();
            SysRemove(szSpoolTmpFile);
            UsrMLFreeUser(pMLUI);
            UsrMLCloseDB(hUsersDB);
            return (ErrorPop());
        }

        UsrMLFreeUser(pMLUI);
    }

    UsrMLCloseDB(hUsersDB);


    return (0);

}




static int      SMAILRemoteMsgSMTPSend(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool,
                        char const * pszDestDomain, SMTPError * pSMTPE)
{

    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *pszSpoolFile = USmlGetSpoolFile(hFSpool);
    char const     *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);
    char const     *pszMailFrom = USmlMailFrom(hFSpool);
    char const     *pszSendMailFrom = USmlSendMailFrom(hFSpool);
    char const     *pszRcptTo = USmlRcptTo(hFSpool);
    char const     *pszSendRcptTo = USmlSendRcptTo(hFSpool);
    char const     *pszMailFile = USmlGetMailFile(hFSpool);
    char const     *pszRelayDomain = USmlGetRelayDomain(hFSpool);

///////////////////////////////////////////////////////////////////////////////
//  If it's a relayed message use the associated relay
///////////////////////////////////////////////////////////////////////////////
    if (pszRelayDomain != NULL)
    {
        SysLogMessage(LOG_LEV_MESSAGE, "SMAIL SMTP-Send CMX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
                pszRelayDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

        if (pSMTPE != NULL)
            USmtpCleanupError(pSMTPE);

        if (USmtpSendMail(pszRelayDomain, pszSMTPDomain, pszSendMailFrom, pszSendRcptTo,
                        pszMailFile, pSMTPE) < 0)
        {
            ErrorPush();

            ErrLogMessage(LOG_LEV_MESSAGE,
                    "SMAIL SMTP-Send CMX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                    pszRelayDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

            QueErrLogMessage(pszSpoolFilePath,
                    "SMAIL SMTP-Send CMX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                    pszRelayDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

            return (ErrorPop());
        }

        if (SMAILLogEnabled(hShbSMAIL))
            USmlLogMessage(hFSpool, "SMTP", pszRelayDomain);

        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Load server configuration
///////////////////////////////////////////////////////////////////////////////
    SVRCFG_HANDLE   hSvrConfig = SvrGetConfigHandle();

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Check the existance of direct SMTP forwarders
///////////////////////////////////////////////////////////////////////////////
    char          **ppszFwdGws = USmtpGetFwdGateways(hSvrConfig, pszDestDomain);

    if (ppszFwdGws != NULL)
    {
        SvrReleaseConfigHandle(hSvrConfig);

///////////////////////////////////////////////////////////////////////////////
//  By initializing this to zero makes XMail to discharge all mail for domains
//  that have an empty forwarders list
///////////////////////////////////////////////////////////////////////////////
        int             iSendErrorCode = 0;

        for (int ss = 0; ppszFwdGws[ss] != NULL; ss++)
        {
            SysLogMessage(LOG_LEV_MESSAGE, "SMAIL SMTP-Send FWD = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
                    ppszFwdGws[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

            if (pSMTPE != NULL)
                USmtpCleanupError(pSMTPE);

            if ((iSendErrorCode = USmtpSendMail(ppszFwdGws[ss], pszSMTPDomain,
                                    pszSendMailFrom, pszSendRcptTo, pszMailFile, pSMTPE)) == 0)
            {
///////////////////////////////////////////////////////////////////////////////
//  Log Mailer operation
///////////////////////////////////////////////////////////////////////////////
                if (SMAILLogEnabled(hShbSMAIL))
                    USmlLogMessage(hFSpool, "FWD", ppszFwdGws[ss]);

                break;
            }


            ErrLogMessage(LOG_LEV_MESSAGE,
                    "SMAIL SMTP-Send FWD = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                    ppszFwdGws[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

            QueErrLogMessage(pszSpoolFilePath,
                    "SMAIL SMTP-Send FWD = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                    ppszFwdGws[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

            if ((pSMTPE != NULL) && USmtpIsFatalError(pSMTPE))
                break;
        }

        StrFreeStrings(ppszFwdGws);

        return (iSendErrorCode);
    }

///////////////////////////////////////////////////////////////////////////////
//  Try to get custom mail exchangers or DNS mail exchangers and if booth tests
//  fails try direct ( I don't know if this is a standard but exist domains
//  that does not have MXs )
///////////////////////////////////////////////////////////////////////////////
    char          **ppszMXGWs = USmtpGetMailExchangers(hSvrConfig, pszDestDomain);

    if (ppszMXGWs == NULL)
    {
        char            szDomainMXHost[256] = "";
        MXS_HANDLE      hMXSHandle = USmtpGetMXFirst(hSvrConfig, pszDestDomain,
                szDomainMXHost);

        SvrReleaseConfigHandle(hSvrConfig);

        if (hMXSHandle != INVALID_MXS_HANDLE)
        {
            int             iSendErrorCode = 0;

            do
            {
                SysLogMessage(LOG_LEV_MESSAGE, "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
                        szDomainMXHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

                if (pSMTPE != NULL)
                    USmtpCleanupError(pSMTPE);

                if ((iSendErrorCode = USmtpSendMail(szDomainMXHost, pszSMTPDomain,
                                        pszSendMailFrom, pszSendRcptTo, pszMailFile, pSMTPE)) == 0)
                {
///////////////////////////////////////////////////////////////////////////////
//  Log Mailer operation
///////////////////////////////////////////////////////////////////////////////
                    if (SMAILLogEnabled(hShbSMAIL))
                        USmlLogMessage(hFSpool, "SMTP", szDomainMXHost);

                    break;
                }


                ErrLogMessage(LOG_LEV_MESSAGE,
                        "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                        szDomainMXHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

                QueErrLogMessage(pszSpoolFilePath,
                        "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                        szDomainMXHost, pszSMTPDomain, pszMailFrom, pszRcptTo);

                if ((pSMTPE != NULL) && USmtpIsFatalError(pSMTPE))
                    break;


            } while (USmtpGetMXNext(hMXSHandle, szDomainMXHost) == 0);

            USmtpMXSClose(hMXSHandle);

            if (iSendErrorCode < 0)
                return (iSendErrorCode);

        }
        else
        {
///////////////////////////////////////////////////////////////////////////////
//  MX records for destination domain not found, try direct !
///////////////////////////////////////////////////////////////////////////////
            SysLogMessage(LOG_LEV_MESSAGE,
                    "MX records for domain \"%s\" not found, trying direct.\n", pszDestDomain);

            SysLogMessage(LOG_LEV_MESSAGE, "SMAIL SMTP-Send FF = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
                    pszDestDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);


            if (pSMTPE != NULL)
                USmtpCleanupError(pSMTPE);

            if (USmtpSendMail(pszDestDomain, pszSMTPDomain, pszSendMailFrom, pszSendRcptTo,
                            pszMailFile, pSMTPE) < 0)
            {
                ErrorPush();

                ErrLogMessage(LOG_LEV_MESSAGE,
                        "SMAIL SMTP-Send FF = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                        pszDestDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

                QueErrLogMessage(pszSpoolFilePath,
                        "SMAIL SMTP-Send FF = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                        pszDestDomain, pszSMTPDomain, pszMailFrom, pszRcptTo);

                return (ErrorPop());
            }

///////////////////////////////////////////////////////////////////////////////
//  Log Mailer operation
///////////////////////////////////////////////////////////////////////////////
            if (SMAILLogEnabled(hShbSMAIL))
                USmlLogMessage(hFSpool, "SMTP", pszDestDomain);
        }
    }
    else
    {
        SvrReleaseConfigHandle(hSvrConfig);

        int             iSendErrorCode = 0;

        for (int ss = 0; ppszMXGWs[ss] != NULL; ss++)
        {
            SysLogMessage(LOG_LEV_MESSAGE, "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
                    ppszMXGWs[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

            if (pSMTPE != NULL)
                USmtpCleanupError(pSMTPE);

            if ((iSendErrorCode = USmtpSendMail(ppszMXGWs[ss], pszSMTPDomain,
                                    pszSendMailFrom, pszSendRcptTo, pszMailFile, pSMTPE)) == 0)
            {
///////////////////////////////////////////////////////////////////////////////
//  Log Mailer operation
///////////////////////////////////////////////////////////////////////////////
                if (SMAILLogEnabled(hShbSMAIL))
                    USmlLogMessage(hFSpool, "SMTP", ppszMXGWs[ss]);

                break;
            }


            ErrLogMessage(LOG_LEV_MESSAGE,
                    "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                    ppszMXGWs[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

            QueErrLogMessage(pszSpoolFilePath,
                    "SMAIL SMTP-Send MX = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                    ppszMXGWs[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

            if ((pSMTPE != NULL) && USmtpIsFatalError(pSMTPE))
                break;
        }

        StrFreeStrings(ppszMXGWs);

        if (iSendErrorCode < 0)
            return (iSendErrorCode);
    }

    return (0);

}



static int      SMAILHandleRemoteUserMessage(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool,
                        char const * pszDestDomain)
{

///////////////////////////////////////////////////////////////////////////////
//  If not exist domain custom processing use standard SMTP delivery
///////////////////////////////////////////////////////////////////////////////
    char            szCustFilePath[SYS_MAX_PATH] = "";

    if (USmlGetDomainMsgCustomFile(hFSpool, pszDestDomain, szCustFilePath) < 0)
    {
        SMTPError       SMTPE;

        USmtpInitError(&SMTPE);

        if (SMAILRemoteMsgSMTPSend(hShbSMAIL, hFSpool, pszDestDomain, &SMTPE) < 0)
        {
            ErrorPush();
///////////////////////////////////////////////////////////////////////////////
//  If a permanent SMTP error has been detected, then notify the message
//  sender and remove the spool file
///////////////////////////////////////////////////////////////////////////////
            if (USmtpIsFatalError(&SMTPE))
                QueSpoolRemoveNotifySender(USmlGetSpoolFilePath(hFSpool),
                        USmtpGetErrorMessage(&SMTPE));

            USmtpCleanupError(&SMTPE);

            return (ErrorPop());
        }

        USmtpCleanupError(&SMTPE);

        return (0);
    }


///////////////////////////////////////////////////////////////////////////////
//  Do custom message processing
///////////////////////////////////////////////////////////////////////////////

    return (SMAILCustomProcessMessage(hShbSMAIL, hFSpool, pszDestDomain, szCustFilePath));

}




static int      SMAILCustomProcessMessage(SHB_HANDLE hShbSMAIL, SPLF_HANDLE hFSpool,
                        char const * pszDestDomain, char const * pszCustFilePath)
{

    FILE           *pCPFile = fopen(pszCustFilePath, "rt");

    if (pCPFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Create pushback command file
///////////////////////////////////////////////////////////////////////////////
    char            szTmpFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szTmpFile);

    FILE           *pPushBFile = fopen(szTmpFile, "wt");

    if (pPushBFile == NULL)
    {
        fclose(pCPFile);

        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    int             iPushBackCmds = 0;
    char            szCmdLine[CUSTOM_PROC_LINE_MAX] = "";

    while (MscGetConfigLine(szCmdLine, sizeof(szCmdLine) - 1, pCPFile) != NULL)
    {
        char          **ppszCmdTokens = StrGetTabLineStrings(szCmdLine);

        if (ppszCmdTokens == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszCmdTokens);

        if (iFieldsCount > 0)
        {
///////////////////////////////////////////////////////////////////////////////
//  Do command line macro substitution
///////////////////////////////////////////////////////////////////////////////
            SMAILCmdMacroSubstitutes(ppszCmdTokens, hFSpool);


            int             iCmdResult = 0;

            if (stricmp(ppszCmdTokens[0], "external") == 0)
                iCmdResult = SMAILCmd_external(hShbSMAIL, pszDestDomain, ppszCmdTokens,
                        iFieldsCount, hFSpool);
            else if (stricmp(ppszCmdTokens[0], "wait") == 0)
                iCmdResult = SMAILCmd_wait(hShbSMAIL, pszDestDomain, ppszCmdTokens,
                        iFieldsCount, hFSpool);
            else if (stricmp(ppszCmdTokens[0], "smtp") == 0)
                iCmdResult = SMAILCmd_smtp(hShbSMAIL, pszDestDomain, ppszCmdTokens,
                        iFieldsCount, hFSpool);
            else if (stricmp(ppszCmdTokens[0], "smtprelay") == 0)
                iCmdResult = SMAILCmd_smtprelay(hShbSMAIL, pszDestDomain, ppszCmdTokens,
                        iFieldsCount, hFSpool);
            else if (stricmp(ppszCmdTokens[0], "redirect") == 0)
                iCmdResult = SMAILCmd_redirect(hShbSMAIL, pszDestDomain, ppszCmdTokens,
                        iFieldsCount, hFSpool);
            else if (stricmp(ppszCmdTokens[0], "lredirect") == 0)
                iCmdResult = SMAILCmd_lredirect(hShbSMAIL, pszDestDomain, ppszCmdTokens,
                        iFieldsCount, hFSpool);

///////////////////////////////////////////////////////////////////////////////
//  Test if We must save a failed command
//  <0 = Error ; ==0 = Success ; >0 = Transient error ( save the command )
///////////////////////////////////////////////////////////////////////////////
            if (iCmdResult > 0)
            {
                fprintf(pPushBFile, "%s\n", szCmdLine);

                ++iPushBackCmds;
            }
        }

        StrFreeStrings(ppszCmdTokens);
    }

    fclose(pPushBFile);
    fclose(pCPFile);

    SysRemove(pszCustFilePath);

    if (iPushBackCmds > 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  If commands left out of processing, push them into the custom file
///////////////////////////////////////////////////////////////////////////////
        if (MscMoveFile(szTmpFile, pszCustFilePath) < 0)
            return (ErrGetErrorCode());

        ErrSetErrorCode(ERR_INCOMPLETE_PROCESSING);
        return (ERR_INCOMPLETE_PROCESSING);
    }

    SysRemove(szTmpFile);

    return (0);

}




static int      SMAILCmdMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool)
{

    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *pszMessageID = USmlGetSpoolFile(hFSpool);
    char const     *pszMailFile = USmlGetMailFile(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom),
                    iRcptDomains = StrStringsCount(ppszRcpt);

    for (int ii = 0; ppszCmdTokens[ii] != NULL; ii++)
    {
        if (strcmp(ppszCmdTokens[ii], "@@FROM") == 0)
        {
            char           *pszNewValue = SysStrDup((iFromDomains > 0) ? ppszFrom[iFromDomains - 1] : "");

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@RCPT") == 0)
        {
            char           *pszNewValue = SysStrDup((iRcptDomains > 0) ? ppszRcpt[iRcptDomains - 1] : "");

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@FILE") == 0)
        {
            char           *pszNewValue = SysStrDup(pszMailFile);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@MSGID") == 0)
        {
            char           *pszNewValue = SysStrDup(pszMessageID);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@MSGREF") == 0)
        {
            char           *pszNewValue = SysStrDup(pszSmtpMessageID);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@TMPFILE") == 0)
        {
            char            szTmpFile[SYS_MAX_PATH] = "";

            SysGetTmpFile(szTmpFile);

            if (MscCopyFile(szTmpFile, pszMailFile) < 0)
            {
                ErrorPush();
                CheckRemoveFile(szTmpFile);
                return (ErrorPop());
            }


            char           *pszNewValue = SysStrDup(szTmpFile);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }

    }

    return (0);

}




static int      SMAILCmd_external(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool)
{

    if (iNumTokens < 5)
    {
        ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
        return (ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
    }

    int             iPriority = atoi(ppszCmdTokens[1]),
                    iWaitTimeout = atoi(ppszCmdTokens[2]),
                    iExitStatus = 0;

    if (SysExec(ppszCmdTokens[3], &ppszCmdTokens[3], iWaitTimeout, iPriority,
                    &iExitStatus) < 0)
    {
        ErrorPush();

        char const     *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);
        char const     *pszMailFrom = USmlMailFrom(hFSpool);
        char const     *pszRcptTo = USmlRcptTo(hFSpool);

        QueErrLogMessage(pszSpoolFilePath,
                "SMAIL EXTRN-Send Prg = \"%s\" Domain = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                ppszCmdTokens[3], pszDestDomain, pszMailFrom, pszRcptTo);

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Log Mailer operation
///////////////////////////////////////////////////////////////////////////////
    if (SMAILLogEnabled(hShbSMAIL))
        USmlLogMessage(hFSpool, "EXTRN", ppszCmdTokens[3]);

    return (0);

}




static int      SMAILCmd_wait(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool)
{

    if (iNumTokens != 2)
    {
        ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
        return (ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
    }

    int             iWaitTimeout = atoi(ppszCmdTokens[1]);

    SysSleep(iWaitTimeout);

    return (0);

}



static int      SMAILCmd_smtp(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool)
{

    if (iNumTokens != 1)
    {
        ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
        return (ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
    }


    SMTPError       SMTPE;

    USmtpInitError(&SMTPE);

    if (SMAILRemoteMsgSMTPSend(hShbSMAIL, hFSpool, pszDestDomain, &SMTPE) < 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  If We get an SMTP fatal error We must return <0 , otherwise >0 to give
//  XMail to ability to resume the command
///////////////////////////////////////////////////////////////////////////////
        int             iReturnCode = USmtpIsFatalError(&SMTPE) ? ErrGetErrorCode() : -ErrGetErrorCode();

        USmtpCleanupError(&SMTPE);

        return (iReturnCode);
    }

    USmtpCleanupError(&SMTPE);

    return (0);

}



static int      SMAILCmd_smtprelay(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool)
{

    if (iNumTokens != 2)
    {
        ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
        return (ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
    }


    char          **ppszRelays = NULL;

    if (ppszCmdTokens[1][0] == '#')
    {
        if ((ppszRelays = StrTokenize(ppszCmdTokens[1] + 1, ",")) != NULL)
        {
            int             iRelayCount = StrStringsCount(ppszRelays);

            srand((unsigned int) time(NULL));

            for (int ii = 0; ii < (iRelayCount / 2); ii++)
            {
                int             iSwap1 = rand() % iRelayCount,
                                iSwap2 = rand() % iRelayCount;
                char           *pszRly1 = ppszRelays[iSwap1],
                               *pszRly2 = ppszRelays[iSwap2];

                ppszRelays[iSwap1] = pszRly2;
                ppszRelays[iSwap2] = pszRly1;
            }
        }
    }
    else
        ppszRelays = StrTokenize(ppszCmdTokens[1], ",");


    if (ppszRelays == NULL)
        return (ErrGetErrorCode());



    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *pszMailFrom = USmlMailFrom(hFSpool);
    char const     *pszRcptTo = USmlRcptTo(hFSpool);
    char const     *pszSendMailFrom = USmlSendMailFrom(hFSpool);
    char const     *pszSendRcptTo = USmlSendRcptTo(hFSpool);
    char const     *pszMailFile = USmlGetMailFile(hFSpool);
    char const     *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);

    SMTPError       SMTPE;

    USmtpInitError(&SMTPE);

///////////////////////////////////////////////////////////////////////////////
//  By initializing this to zero makes XMail to discharge all mail for domains
//  that have an empty relay list
///////////////////////////////////////////////////////////////////////////////
    int             iReturnCode = 0;

    for (int ss = 0; ppszRelays[ss] != NULL; ss++)
    {
        SysLogMessage(LOG_LEV_MESSAGE, "SMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\"\n",
                ppszRelays[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);


        USmtpCleanupError(&SMTPE);

        if (USmtpSendMail(ppszRelays[ss], pszSMTPDomain, pszSendMailFrom, pszSendRcptTo,
                pszMailFile, &SMTPE) == 0)
        {
///////////////////////////////////////////////////////////////////////////////
//  Log Mailer operation
///////////////////////////////////////////////////////////////////////////////
            if (SMAILLogEnabled(hShbSMAIL))
                USmlLogMessage(hFSpool, "RLYS", ppszRelays[ss]);

            USmtpCleanupError(&SMTPE);

            StrFreeStrings(ppszRelays);

            return (0);
        }


        ErrLogMessage(LOG_LEV_MESSAGE,
                "SMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                ppszRelays[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);

        QueErrLogMessage(pszSpoolFilePath,
                "SMAIL SMTP-Send RLYS = \"%s\" SMTP = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                ppszRelays[ss], pszSMTPDomain, pszMailFrom, pszRcptTo);


        iReturnCode = USmtpIsFatalError(&SMTPE) ? ErrGetErrorCode() : -ErrGetErrorCode();
    }

    USmtpCleanupError(&SMTPE);

    StrFreeStrings(ppszRelays);

    return (iReturnCode);

}



static int      SMAILCmd_redirect(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool)
{

    if (iNumTokens < 2)
    {
        ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
        return (ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
    }


    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);

    int             iFromDomains = StrStringsCount(ppszFrom),
                    iRcptDomains = StrStringsCount(ppszRcpt);

    char            szLocalUser[MAX_ADDR_NAME] = "",
                    szLocalDomain[MAX_ADDR_NAME] = "";

    if ((iRcptDomains < 1) ||
            (USmtpSplitEmailAddr(ppszRcpt[iRcptDomains - 1], szLocalUser, szLocalDomain) < 0))
        return (ErrGetErrorCode());


    for (int ii = 1; ppszCmdTokens[ii] != NULL; ii++)
    {
        char            szSpoolTmpFile[SYS_MAX_PATH] = "",
                        szAliasAddr[MAX_ADDR_NAME] = "";

        if (QueGetTempFile(NULL, szSpoolTmpFile, iQueueSplitLevel) < 0)
            continue;

        sprintf(szAliasAddr, "%s@%s", szLocalUser, ppszCmdTokens[ii]);

        if (USmlCreateSpoolFile(hFSpool, NULL, szAliasAddr, szSpoolTmpFile) < 0)
        {
            CheckRemoveFile(szSpoolTmpFile);
            continue;
        }

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitTempMessage(szSpoolTmpFile) < 0)
        {
            ErrorPush();
            SysRemove(szSpoolTmpFile);
            return (ErrorPop());
        }
    }

    return (0);

}




static int      SMAILCmd_lredirect(SHB_HANDLE hShbSMAIL, char const * pszDestDomain, char **ppszCmdTokens,
                        int iNumTokens, SPLF_HANDLE hFSpool)
{

    if (iNumTokens < 2)
    {
        ErrSetErrorCode(ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
        return (ERR_BAD_DOMAIN_PROC_CMD_SYNTAX);
    }


    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);

    int             iFromDomains = StrStringsCount(ppszFrom),
                    iRcptDomains = StrStringsCount(ppszRcpt);

    char            szLocalUser[MAX_ADDR_NAME] = "",
                    szLocalDomain[MAX_ADDR_NAME] = "";

    if ((iRcptDomains < 1) ||
            (USmtpSplitEmailAddr(ppszRcpt[iRcptDomains - 1], szLocalUser, szLocalDomain) < 0))
        return (ErrGetErrorCode());


    for (int ii = 1; ppszCmdTokens[ii] != NULL; ii++)
    {
        char            szSpoolTmpFile[SYS_MAX_PATH] = "",
                        szAliasAddr[MAX_ADDR_NAME] = "";

        if (QueGetTempFile(NULL, szSpoolTmpFile, iQueueSplitLevel) < 0)
            continue;

        sprintf(szAliasAddr, "%s@%s", szLocalUser, ppszCmdTokens[ii]);

        if (USmlCreateSpoolFile(hFSpool, ppszRcpt[iRcptDomains - 1], szAliasAddr,
                        szSpoolTmpFile) < 0)
        {
            CheckRemoveFile(szSpoolTmpFile);
            continue;
        }

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitTempMessage(szSpoolTmpFile) < 0)
        {
            ErrorPush();
            SysRemove(szSpoolTmpFile);
            return (ErrorPop());
        }
    }

    return (0);

}




static int      SMAILFilterMessage(SHB_HANDLE hShbSMAIL, char const * pszMessFilePath)
{

///////////////////////////////////////////////////////////////////////////////
//  Load spool file header
///////////////////////////////////////////////////////////////////////////////
    SpoolFileHeader SFH;

    if (USmlLoadSpoolFileHeader(pszMessFilePath, SFH) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Extract target domain and user
///////////////////////////////////////////////////////////////////////////////
    char            szDestUser[MAX_ADDR_NAME] = "",
                    szDestDomain[MAX_ADDR_NAME] = "";

    if ((StrStringsCount(SFH.ppszRcpt) < 1) ||
            (USmtpSplitEmailAddr(SFH.ppszRcpt[0], szDestUser, szDestDomain) < 0))
    {
        USmlCleanupSpoolFileHeader(SFH);
        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  If no filter is defined message pass through
///////////////////////////////////////////////////////////////////////////////
    char            szFilterFilePath[SYS_MAX_PATH] = "";

    if (USmlGetMessageFilterFile(szDestDomain, szDestUser, szFilterFilePath) < 0)
    {
        USmlCleanupSpoolFileHeader(SFH);
        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Filter this message
///////////////////////////////////////////////////////////////////////////////
    FILE           *pFiltFile = fopen(szFilterFilePath, "rt");

    if (pFiltFile == NULL)
    {
        USmlCleanupSpoolFileHeader(SFH);

        ErrSetErrorCode(ERR_FILE_OPEN, szFilterFilePath);
        return (ERR_FILE_OPEN);
    }

    char            szFiltLine[FILTER_LINE_MAX] = "";

    while (MscGetConfigLine(szFiltLine, sizeof(szFiltLine) - 1, pFiltFile) != NULL)
    {
        char          **ppszCmdTokens = StrGetTabLineStrings(szFiltLine);

        if (ppszCmdTokens == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszCmdTokens);

        if (iFieldsCount > 0)
        {
///////////////////////////////////////////////////////////////////////////////
//  Do filter line macro substitution
///////////////////////////////////////////////////////////////////////////////
            SMAILFilterMacroSubstitutes(ppszCmdTokens, pszMessFilePath, SFH);


            int             iExitCode = 0;

            if (SysExec(ppszCmdTokens[0], &ppszCmdTokens[0], FILTER_TIMEOUT,
                            FILTER_PRIORITY, &iExitCode) == 0)
            {
                if (iExitCode == FILTER_OUT_EXITCODE)
                {
                    StrFreeStrings(ppszCmdTokens);
                    fclose(pFiltFile);

///////////////////////////////////////////////////////////////////////////////
//  Filter out message
///////////////////////////////////////////////////////////////////////////////
                    QueSpoolRemoveNotifySender(pszMessFilePath, ErrGetErrorString(ERR_FILTERED_MESSAGE));


                    USmlCleanupSpoolFileHeader(SFH);

                    ErrSetErrorCode(ERR_FILTERED_MESSAGE);
                    return (ERR_FILTERED_MESSAGE);
                }
                else if (iExitCode == MODIFY_EXITCODE)
                {


                }
            }
            else
                SysLogMessage(LOG_LEV_MESSAGE, "Filter error for domain \"%s\" (%s)\n",
                        szDestDomain, ppszCmdTokens[0]);

        }

        StrFreeStrings(ppszCmdTokens);
    }

    fclose(pFiltFile);

    USmlCleanupSpoolFileHeader(SFH);

    return (0);

}




static int      SMAILFilterMacroSubstitutes(char **ppszCmdTokens, char const * pszSpoolFilePath,
                        SpoolFileHeader const & SFH)
{

    int             iFromDomains = StrStringsCount(SFH.ppszFrom),
                    iRcptDomains = StrStringsCount(SFH.ppszRcpt);

    for (int ii = 0; ppszCmdTokens[ii] != NULL; ii++)
    {
        if (strcmp(ppszCmdTokens[ii], "@@FROM") == 0)
        {
            char           *pszNewValue = SysStrDup((iFromDomains > 0) ? SFH.ppszFrom[iFromDomains - 1] : "");

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@RCPT") == 0)
        {
            char           *pszNewValue = SysStrDup((iRcptDomains > 0) ? SFH.ppszRcpt[iRcptDomains - 1] : "");

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@FILE") == 0)
        {
            char           *pszNewValue = SysStrDup(pszSpoolFilePath);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@MSGID") == 0)
        {
            char           *pszNewValue = SysStrDup(SFH.szSpoolFile);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@MSGREF") == 0)
        {
            char           *pszNewValue = SysStrDup(SFH.szMessageID);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@TMPFILE") == 0)
        {
            char            szTmpFile[SYS_MAX_PATH] = "";

            SysGetTmpFile(szTmpFile);

            if (MscCopyFile(szTmpFile, pszSpoolFilePath) < 0)
            {
                ErrorPush();
                CheckRemoveFile(szTmpFile);
                return (ErrorPop());
            }


            char           *pszNewValue = SysStrDup(szTmpFile);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }

    }

    return (0);

}
