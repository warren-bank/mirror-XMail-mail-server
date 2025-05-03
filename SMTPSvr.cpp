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
#include "SList.h"
#include "ShBlocks.h"
#include "BuffSock.h"
#include "ResLocks.h"
#include "StrUtils.h"
#include "MessQueue.h"
#include "QueueUtils.h"
#include "SvrUtils.h"
#include "MiscUtils.h"
#include "UsrUtils.h"
#include "Base64Enc.h"
#include "MD5.h"
#include "UsrMailList.h"
#include "SMTPSvr.h"
#include "SMTPUtils.h"
#include "SMAILUtils.h"
#include "MailDomains.h"
#include "POP3Utils.h"
#include "MailConfig.h"
#include "AppDefines.h"
#include "MailSvr.h"





#define SMTP_MAX_LINE_SIZE      2048
#define SMTPSRV_ACCEPT_TIMEOUT  4
#define STD_SMTP_TIMEOUT        30
#define SMTP_LISTEN_SIZE        64
#define SMTP_WAIT_SLEEP         2
#define MAX_CLIENTS_WAIT        300
#define SMTP_IPMAP_FILE         "smtp.ipmap.tab"
#define SMTP_LOG_FILE           "smtp"
#define SMTP_SERVER_NAME        "[" APP_NAME_VERSION_OS_STR " ESMTP Server]"
#define PLAIN_AUTH_PARAM_SIZE   1024
#define LOGIN_AUTH_USERNAME     "Username:"
#define LOGIN_AUTH_PASSWORD     "Password:"
#define SVR_SMTP_AUTH_FILE      "smtpauth.tab"
#define SVR_SMTPAUTH_LINE_MAX   512
#define SVR_SMTP_EXTAUTH_FILE   "smtpextauth.tab"
#define SVR_SMTP_EXTAUTH_LINE_MAX   1024
#define SVR_SMTP_EXTAUTH_TIMEOUT    60
#define SVR_SMTP_EXTAUTH_PRIORITY   SYS_PRIORITY_NORMAL
#define SVR_SMTP_EXTAUTH_SUCCESS    0

#define SMTPF_RELAY_ENABLED     (1 << 0)
#define SMTPF_MAIL_LOCKED       (1 << 1)
#define SMTPF_MAIL_UNLOCKED     (1 << 2)
#define SMTPF_AUTHENTICATED     (1 << 3)
#define SMTPF_VRFY_ENABLED      (1 << 4)
#define SMTPF_BLOCKED_IP        (1 << 5)
#define SMTPF_ETRN_ENABLED      (1 << 6)

#define SMTPF_STATIC_MASK       SMTPF_BLOCKED_IP
#define SMTPF_AUTH_MASK         (SMTPF_RELAY_ENABLED | SMTPF_MAIL_UNLOCKED | SMTPF_AUTHENTICATED | \
                                        SMTPF_VRFY_ENABLED | SMTPF_ETRN_ENABLED)
#define SMTPF_RESET_MASK        (SMTPF_AUTH_MASK | SMTPF_STATIC_MASK)








enum SMTPStates
{
    stateInit = 0,
    stateHelo,
    stateAuthenticated,
    stateMail,
    stateRcpt,

    stateExit
};

struct SMTPSession
{
    int             iSMTPState;
    SHB_HANDLE      hShbSMTP;
    SMTPConfig     *pSMTPCfg;
    SVRCFG_HANDLE   hSvrConfig;
    SYS_INET_ADDR   PeerInfo;
    SYS_INET_ADDR   SockInfo;
    int             iCmdDelay;
    unsigned long   ulMaxMsgSize;
    char            szSvrFQDN[MAX_ADDR_NAME];
    char            szSvrDomain[MAX_ADDR_NAME];
    char            szClientFQDN[MAX_ADDR_NAME];
    char            szClientDomain[MAX_ADDR_NAME];
    char            szDestDomain[MAX_ADDR_NAME];
    char            szLogonUser[128];
    char            szMsgFile[SYS_MAX_PATH];
    FILE           *pMsgFile;
    char           *pszFrom;
    char           *pszRcpt;
    char           *pszSendRcpt;
    int             iRcptCount;
    SYS_UINT64      ullMessageID;
    char            szMessageID[128];
    char            szTimeStamp[256];
    unsigned long   ulSetupFlags;
    unsigned long   ulFlags;

};

enum SmtpAuthFields
{
    smtpaUsername = 0,
    smtpaPassword,
    smtpaPerms,

    smtpaMax
};








static SMTPConfig *SMTPGetConfigCopy(SHB_HANDLE hShbSMTP);
static int      SMTPLogEnabled(SHB_HANDLE hShbSMTP, SMTPConfig * pSMTPCfg = NULL);
static int      SMTPCheckPeerIP(SYS_SOCKET SockFD);
static int      SMTPThreadCountAdd(long lCount, SHB_HANDLE hShbSMTP,
                        SMTPConfig * pSMTPCfg = NULL);
static unsigned int SMTPClientThread(void *pThreadData);
static int      SMTPCheckSysResources(SVRCFG_HANDLE hSvrConfig);
static int      SMTPCheckMapsList(SYS_INET_ADDR const & PeerInfo, char const * pszMapList,
                        int & iMapCode);
static int      SMTPInitSession(SHB_HANDLE hShbSMTP, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPLoadConfig(SMTPSession & SMTPS, char const * pszSvrConfig);
static int      SMTPApplyPerms(SMTPSession & SMTPS, char const * pszPerms);
static int      SMTPLogSession(SMTPSession & SMTPS, char const * pszSender,
                        char const * pszRecipient, char const * pszStatus,
                        unsigned long ulMsgSize);
static int      SMTPHandleSession(SHB_HANDLE hShbSMTP, BSOCK_HANDLE hBSock);
static void     SMTPClearSession(SMTPSession & SMTPS);
static void     SMTPResetSession(SMTPSession & SMTPS);
static int      SMTPHandleCommand(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPCheckReturnPath(const char *pszCommand, char **ppszRetDomains,
                        SMTPSession & SMTPS, char *&pszSMTPError);
static int      SMTPTryPopAuthIpCheck(SMTPSession & SMTPS, char const * pszUser,
                        char const * pszDomain);
static int      SMTPAddMessageInfo(SMTPSession & SMTPS);
static int      SMTPCheckMailParams(const char *pszCommand, char **ppszRetDomains,
                        SMTPSession & SMTPS, char *&pszSMTPError);
static int      SMTPHandleCmd_MAIL(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPCheckRelayCapability(SMTPSession & SMTPS, char const * pszDestDomain);
static int      SMTPCheckForwardPath(char **ppszFwdDomains, SMTPSession & SMTPS,
                        char *&pszSMTPError);
static int      SMTPHandleCmd_RCPT(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPHandleCmd_DATA(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPAddReceived(char const * const * ppszMsgInfo, char const * pszMailFrom,
                        char const * pszRcptTo, char const * pszMessageID, FILE * pMailFile);
static int      SMTPSubmitPackedFile(const char *pszPkgFile);
static int      SMTPHandleCmd_HELO(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPHandleCmd_EHLO(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPListExtAuths(FILE * pRespFile, SMTPSession & SMTPS);
static int      SMTPExternalAuthSubstitute(char **ppszAuthTokens, char const * pszChallenge,
                        char const * pszDigest, char const * pszSecretsFile);
static int      SMTPCreateSecretsFile(char const * pszSecretsFile);
static int      SMTPExternalAuthenticate(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char **ppszAuthTokens);
static int      SMTPDoAuthExternal(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthType);
static int      SMTPDoAuthPlain(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthParam);
static int      SMTPDoAuthLogin(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthParam);
static char    *SMTPGetAuthFilePath(char *pszFilePath);
static char    *SMTPGetExtAuthFilePath(char *pszFilePath);
static int      SMTPCheckLocalAuth(SVRCFG_HANDLE hSvrConfig, char const * pszUsername,
                        char const * pszPassword, char * pszPerms);
static int      SMTPGetUserSmtpPerms(UserInfo * pUI, SVRCFG_HANDLE hSvrConfig, char *pszPerms);
static int      SMTPCheckLocalCramMD5Auth(SVRCFG_HANDLE hSvrConfig, char const * pszChallenge,
                        char const * pszUsername, char const * pszDigest, char *pszPerms);
static int      SMTPCheckUsrPwdAuth(SVRCFG_HANDLE hSvrConfig, char const * pszUsername,
                        char const * pszPassword, char *pszPerms);
static int      SMTPCheckCramMD5Auth(char const * pszChallenge, char const * pszUsername,
                        char const * pszDigest, char *pszPerms);
static int      SMTPDoAuthCramMD5(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthParam);
static int      SMTPHandleCmd_AUTH(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPSendMultilineResponse(BSOCK_HANDLE hBSock, int iTimeout, FILE * pRespFile);
static int      SMTPHandleCmd_RSET(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPHandleCmd_NOOP(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPHandleCmd_QUIT(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPHandleCmd_VRFY(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);
static int      SMTPHandleCmd_ETRN(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS);









static SMTPConfig *SMTPGetConfigCopy(SHB_HANDLE hShbSMTP)
{

    SMTPConfig     *pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP);

    if (pSMTPCfg == NULL)
        return (NULL);

    SMTPConfig     *pSMTPCfgCopy = (SMTPConfig *) SysAlloc(sizeof(SMTPConfig));

    if (pSMTPCfgCopy != NULL)
        memcpy(pSMTPCfgCopy, pSMTPCfg, sizeof(SMTPConfig));

    ShbUnlock(hShbSMTP);

    return (pSMTPCfgCopy);

}



static int      SMTPLogEnabled(SHB_HANDLE hShbSMTP, SMTPConfig * pSMTPCfg)
{

    int             iDoUnlock = 0;

    if (pSMTPCfg == NULL)
    {
        if ((pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP)) == NULL)
            return (ErrGetErrorCode());

        ++iDoUnlock;
    }

    unsigned long   ulFlags = pSMTPCfg->ulFlags;

    if (iDoUnlock)
        ShbUnlock(hShbSMTP);

    return ((ulFlags & SMTPF_LOG_ENABLED) ? 1 : 0);

}



static int      SMTPCheckPeerIP(SYS_SOCKET SockFD)
{

    char            szIPMapFile[SYS_MAX_PATH] = "";

    CfgGetRootPath(szIPMapFile);
    strcat(szIPMapFile, SMTP_IPMAP_FILE);

    if (SysExistFile(szIPMapFile))
    {
        SYS_INET_ADDR   PeerInfo;

        if (SysGetPeerInfo(SockFD, PeerInfo) < 0)
            return (ErrGetErrorCode());

        if (MscCheckAllowedIP(szIPMapFile, PeerInfo, true) < 0)
            return (ErrGetErrorCode());
    }

    return (0);

}



static int      SMTPThreadCountAdd(long lCount, SHB_HANDLE hShbSMTP,
                        SMTPConfig * pSMTPCfg)
{

    int             iDoUnlock = 0;

    if (pSMTPCfg == NULL)
    {
        if ((pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP)) == NULL)
            return (ErrGetErrorCode());

        ++iDoUnlock;
    }

    if ((pSMTPCfg->lThreadCount + lCount) > pSMTPCfg->lMaxThreads)
    {
        if (iDoUnlock)
            ShbUnlock(hShbSMTP);

        ErrSetErrorCode(ERR_SERVER_BUSY);
        return (ERR_SERVER_BUSY);
    }

    pSMTPCfg->lThreadCount += lCount;

    if (iDoUnlock)
        ShbUnlock(hShbSMTP);

    return (0);

}



static unsigned int SMTPClientThread(void *pThreadData)
{

    SYS_SOCKET      SockFD = (SYS_SOCKET) (unsigned int) pThreadData;

///////////////////////////////////////////////////////////////////////////////
//  Link socket to the bufferer
///////////////////////////////////////////////////////////////////////////////
    BSOCK_HANDLE    hBSock = BSckAttach(SockFD);

    if (hBSock == INVALID_BSOCK_HANDLE)
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check IP permission
///////////////////////////////////////////////////////////////////////////////
    if (SMTPCheckPeerIP(SockFD) < 0)
    {
        ErrorPush();

        BSckVSendString(hBSock, STD_SMTP_TIMEOUT, "421 %s - %s",
                SMTP_SERVER_NAME, ErrGetErrorString(ErrorFetch()));

        BSckDetach(hBSock, 1);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Increase threads count
///////////////////////////////////////////////////////////////////////////////
    if (SMTPThreadCountAdd(+1, hShbSMTP) < 0)
    {
        ErrorPush();

        BSckVSendString(hBSock, STD_SMTP_TIMEOUT, "421 %s - %s",
                SMTP_SERVER_NAME, ErrGetErrorString(ErrorFetch()));

        BSckDetach(hBSock, 1);
        return (ErrorPop());
    }


///////////////////////////////////////////////////////////////////////////////
//  Handle client session
///////////////////////////////////////////////////////////////////////////////
    SMTPHandleSession(hShbSMTP, hBSock);


///////////////////////////////////////////////////////////////////////////////
//  Decrease threads count
///////////////////////////////////////////////////////////////////////////////
    SMTPThreadCountAdd(-1, hShbSMTP);

///////////////////////////////////////////////////////////////////////////////
//  Unlink socket from the bufferer and close it
///////////////////////////////////////////////////////////////////////////////
    BSckDetach(hBSock, 1);


    return (0);

}



unsigned int    SMTPThreadProc(void *pThreadData)
{

    SMTPConfig     *pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP);

    if (pSMTPCfg == NULL)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
        return (ErrorPop());
    }


    int             iNumSockFDs = 0;
    SYS_SOCKET      SockFDs[MAX_SMTP_ACCEPT_ADDRESSES];

    if (MscCreateServerSockets(pSMTPCfg->iNumAddr, pSMTPCfg->SvrPath, pSMTPCfg->iPort,
                    SMTP_LISTEN_SIZE, SockFDs, iNumSockFDs) < 0)
    {
        ErrorPush();
        SysLogMessage(LOG_LEV_ERROR, "%s\n", ErrGetErrorString());
        ShbUnlock(hShbSMTP);
        return (ErrorPop());
    }

    ShbUnlock(hShbSMTP);


    SysLogMessage(LOG_LEV_MESSAGE, "%s started\n", SMTP_SERVER_NAME);

    for (;;)
    {
        int             iNumConnSockFD = 0;
        SYS_SOCKET      ConnSockFD[MAX_SMTP_ACCEPT_ADDRESSES];

        if (MscAcceptServerConnection(SockFDs, iNumSockFDs, ConnSockFD,
                        iNumConnSockFD, SMTPSRV_ACCEPT_TIMEOUT) < 0)
        {
            unsigned long   ulFlags = SMTPF_STOP_SERVER;

            pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP);

            if (pSMTPCfg != NULL)
                ulFlags = pSMTPCfg->ulFlags;

            ShbUnlock(hShbSMTP);

            if (ulFlags & SMTPF_STOP_SERVER)
                break;
            else
                continue;
        }


        for (int ss = 0; ss < iNumConnSockFD; ss++)
        {
            SYS_THREAD      hClientThread = SysCreateServiceThread(SMTPClientThread, ConnSockFD[ss]);

            if (hClientThread != SYS_INVALID_THREAD)
                SysCloseThread(hClientThread, 0);
            else
                SysCloseSocket(ConnSockFD[ss]);

        }
    }

    for (int ss = 0; ss < iNumSockFDs; ss++)
        SysCloseSocket(SockFDs[ss]);

///////////////////////////////////////////////////////////////////////////////
//  Wait for client completion
///////////////////////////////////////////////////////////////////////////////
    for (int iTotalWait = 0; (iTotalWait < MAX_CLIENTS_WAIT); iTotalWait += SMTP_WAIT_SLEEP)
    {
        pSMTPCfg = (SMTPConfig *) ShbLock(hShbSMTP);

        if (pSMTPCfg == NULL)
            break;

        long            lThreadCount = pSMTPCfg->lThreadCount;

        ShbUnlock(hShbSMTP);

        if (lThreadCount == 0)
            break;

        SysSleep(SMTP_WAIT_SLEEP);
    }

    SysLogMessage(LOG_LEV_MESSAGE, "%s stopped\n", SMTP_SERVER_NAME);

    return (0);

}



static int      SMTPCheckSysResources(SVRCFG_HANDLE hSvrConfig)
{
///////////////////////////////////////////////////////////////////////////////
//  Check disk space
///////////////////////////////////////////////////////////////////////////////
    int             iMinValue = SvrGetConfigInt("SmtpMinDiskSpace", -1, hSvrConfig);

    if ((iMinValue > 0) && (SvrCheckDiskSpace(1024 * (unsigned long) iMinValue) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Check virtual memory
///////////////////////////////////////////////////////////////////////////////
    if (((iMinValue = SvrGetConfigInt("SmtpMinVirtMemSpace", -1, hSvrConfig)) > 0) &&
            (SvrCheckVirtMemSpace(1024 * (unsigned long) iMinValue) < 0))
        return (ErrGetErrorCode());


    return (0);

}



static int      SMTPCheckMapsList(SYS_INET_ADDR const & PeerInfo, char const * pszMapList,
                        int & iMapCode)
{

    for (; pszMapList != NULL; pszMapList++)
    {
        char const     *pszColon = strchr(pszMapList, ':');

        if (pszColon == NULL)
            break;

        int             iRetCode = atoi(pszColon + 1),
                        iMapLength = (int) (pszColon - pszMapList);
        char            szMapName[MAX_HOST_NAME] = "";

        strncpy(szMapName, pszMapList, iMapLength);
        szMapName[iMapLength] = '\0';

        if (USmtpDnsMapsContained(PeerInfo, szMapName))
        {
            iMapCode = iRetCode;

            char            szIP[128] = "???.???.???.???",
                            szMapSpec[MAX_HOST_NAME + 128] = "";

            SysInetNToA(PeerInfo, szIP);
            SysSNPrintf(szMapSpec, sizeof(szMapSpec) - 1, "%s:%s", szMapName, szIP);

            ErrSetErrorCode(ERR_MAPS_CONTAINED, szMapSpec);
            return (ERR_MAPS_CONTAINED);
        }

        pszMapList = strchr(pszColon, ',');
    }

    return (0);

}



static int      SMTPInitSession(SHB_HANDLE hShbSMTP, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    ZeroData(SMTPS);
    SMTPS.iSMTPState = stateInit;
    SMTPS.hShbSMTP = hShbSMTP;
    SMTPS.hSvrConfig = INVALID_SVRCFG_HANDLE;
    SMTPS.pSMTPCfg = NULL;
    SMTPS.pMsgFile = NULL;
    SMTPS.pszFrom = NULL;
    SMTPS.pszRcpt = NULL;
    SMTPS.pszSendRcpt = NULL;
    SMTPS.iRcptCount = 0;
    SMTPS.iCmdDelay = 0;
    SMTPS.ulMaxMsgSize = 0;
    SetEmptyString(SMTPS.szMessageID);
    SetEmptyString(SMTPS.szDestDomain);
    SetEmptyString(SMTPS.szClientFQDN);
    SetEmptyString(SMTPS.szClientDomain);
    SetEmptyString(SMTPS.szLogonUser);
    SMTPS.ulFlags = 0;
    SMTPS.ulSetupFlags = 0;

    SysGetTmpFile(SMTPS.szMsgFile);

    if ((SMTPS.hSvrConfig = SvrGetConfigHandle()) == INVALID_SVRCFG_HANDLE)
        return (ErrGetErrorCode());

    if ((SMTPCheckSysResources(SMTPS.hSvrConfig) < 0) ||
            (SysGetPeerInfo(BSckGetAttachedSocket(hBSock), SMTPS.PeerInfo) < 0) ||
            (SysGetSockInfo(BSckGetAttachedSocket(hBSock), SMTPS.SockInfo) < 0))
    {
        ErrorPush();
        SvrReleaseConfigHandle(SMTPS.hSvrConfig);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check if SMTP client is in "spammers.tab" file
///////////////////////////////////////////////////////////////////////////////
    if (USmtpSpammerCheck(SMTPS.PeerInfo) < 0)
    {
        ErrorPush();
        SvrReleaseConfigHandle(SMTPS.hSvrConfig);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Custom maps checking
///////////////////////////////////////////////////////////////////////////////
    char           *pszMapsList = SvrGetConfigVar(SMTPS.hSvrConfig, "CustMapsList");

    if (pszMapsList != NULL)
    {
        int             iMapCode = 0;

        if (SMTPCheckMapsList(SMTPS.PeerInfo, pszMapsList, iMapCode) < 0)
        {
            if (iMapCode == 1)
            {
                ErrorPush();
                SysFree(pszMapsList);
                SvrReleaseConfigHandle(SMTPS.hSvrConfig);
                return (ErrorPop());
            }

            if (iMapCode == 0)
                SMTPS.ulFlags |= SMTPF_BLOCKED_IP;
            else
                SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, Abs(iMapCode));
        }

        SysFree(pszMapsList);
    }

///////////////////////////////////////////////////////////////////////////////
//  RDNS client check
///////////////////////////////////////////////////////////////////////////////
    int             iCheckValue = SvrGetConfigInt("SMTP-RDNSCheck", 0, SMTPS.hSvrConfig);

    if ((iCheckValue != 0) &&
            (SysGetHostByAddr(SMTPS.PeerInfo, SMTPS.szClientFQDN) < 0))
    {
        if (iCheckValue > 0)
            SMTPS.ulFlags |= SMTPF_BLOCKED_IP;
        else
            SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, -iCheckValue);
    }

///////////////////////////////////////////////////////////////////////////////
//  RBL-MAPS client check (rbl.maps.vix.com.)
///////////////////////////////////////////////////////////////////////////////
    if (((iCheckValue = SvrGetConfigInt("RBL-MAPSCheck", 0, SMTPS.hSvrConfig)) != 0) &&
            (USmtpRBLCheck(SMTPS.PeerInfo) < 0))
    {
        if (iCheckValue > 0)
        {
            ErrorPush();
            SvrReleaseConfigHandle(SMTPS.hSvrConfig);
            return (ErrorPop());
        }

        SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, -iCheckValue);
    }

///////////////////////////////////////////////////////////////////////////////
//  RSS-MAPS client check (relays.mail-abuse.org.)
///////////////////////////////////////////////////////////////////////////////
    if (((iCheckValue = SvrGetConfigInt("RSS-MAPSCheck", 0, SMTPS.hSvrConfig)) != 0) &&
            (USmtpRSSCheck(SMTPS.PeerInfo) < 0))
    {
        if (iCheckValue > 0)
        {
            ErrorPush();
            SvrReleaseConfigHandle(SMTPS.hSvrConfig);
            return (ErrorPop());
        }

        SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, -iCheckValue);
    }

///////////////////////////////////////////////////////////////////////////////
//  DUL-MAPS client check (dialups.mail-abuse.org.)
///////////////////////////////////////////////////////////////////////////////
    if (((iCheckValue = SvrGetConfigInt("DUL-MAPSCheck", 0, SMTPS.hSvrConfig)) != 0) &&
            (USmtpDULCheck(SMTPS.PeerInfo) < 0))
    {
        if (iCheckValue > 0)
            SMTPS.ulFlags |= SMTPF_BLOCKED_IP;
        else
            SMTPS.iCmdDelay = Max(SMTPS.iCmdDelay, -iCheckValue);
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup SMTP domain
///////////////////////////////////////////////////////////////////////////////
    char            szIP[128] = "???.???.???.???";

    if (MscGetSockHost(BSckGetAttachedSocket(hBSock), SMTPS.szSvrFQDN) < 0)
        strcpy(SMTPS.szSvrFQDN, SysInetNToA(SMTPS.SockInfo, szIP));
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Try to get a valid domain from the FQDN
///////////////////////////////////////////////////////////////////////////////
        if (MDomGetClientDomain(SMTPS.szSvrFQDN, SMTPS.szSvrDomain,
                        sizeof(SMTPS.szSvrDomain) - 1) < 0)
            StrSNCpy(SMTPS.szSvrDomain, SMTPS.szSvrFQDN);
    }

    if (IsEmptyString(SMTPS.szSvrDomain))
    {
        char           *pszDefDomain = SvrGetConfigVar(SMTPS.hSvrConfig, "RootDomain");

        if (pszDefDomain == NULL)
        {
            SvrReleaseConfigHandle(SMTPS.hSvrConfig);
            ErrSetErrorCode(ERR_NO_DOMAIN);
            return (ERR_NO_DOMAIN);
        }

        strcpy(SMTPS.szSvrDomain, pszDefDomain);

        SysFree(pszDefDomain);
    }

///////////////////////////////////////////////////////////////////////////////
//  Create timestamp
///////////////////////////////////////////////////////////////////////////////
    sprintf(SMTPS.szTimeStamp, "<%lu.%lu@%s>",
            (unsigned long) time(NULL), SysGetCurrentThreadId(), SMTPS.szSvrDomain);

    if ((SMTPS.pSMTPCfg = SMTPGetConfigCopy(hShbSMTP)) == NULL)
    {
        ErrorPush();
        SvrReleaseConfigHandle(SMTPS.hSvrConfig);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Get maximum accepted message size
///////////////////////////////////////////////////////////////////////////////
    SMTPS.ulMaxMsgSize = 1024 * (unsigned long) SvrGetConfigInt("MaxMessageSize",
            0, SMTPS.hSvrConfig);

///////////////////////////////////////////////////////////////////////////////
//  Try to load specific configuration
///////////////////////////////////////////////////////////////////////////////
    char            szConfigName[128] = "";

    sprintf(szConfigName, "SmtpConfig-%s", SysInetNToA(SMTPS.SockInfo, szIP));

    char           *pszSvrConfig = SvrGetConfigVar(SMTPS.hSvrConfig, szConfigName);

    if ((pszSvrConfig != NULL) ||
            ((pszSvrConfig = SvrGetConfigVar(SMTPS.hSvrConfig, "SmtpConfig")) != NULL))
    {
        SMTPLoadConfig(SMTPS, pszSvrConfig);

        SysFree(pszSvrConfig);
    }

    SMTPS.ulFlags |= SMTPS.ulSetupFlags;

    return (0);

}



static int      SMTPLoadConfig(SMTPSession & SMTPS, char const * pszSvrConfig)
{

    char          **ppszCfgTokens = StrTokenize(pszSvrConfig, ",");

    if (ppszCfgTokens == NULL)
        return (ErrGetErrorCode());

    for (int ii = 0; ppszCfgTokens[ii] != NULL; ii++)
    {

        if (stricmp(ppszCfgTokens[ii], "mail-auth") == 0)
            SMTPS.ulSetupFlags |= SMTPF_MAIL_LOCKED;

    }

    StrFreeStrings(ppszCfgTokens);

    return (0);

}



static int      SMTPApplyPerms(SMTPSession & SMTPS, char const * pszPerms)
{

    for (int ii = 0; pszPerms[ii] != '\0'; ii++)
    {

        switch (pszPerms[ii])
        {
            case ('M'):
                SMTPS.ulFlags |= SMTPF_MAIL_UNLOCKED;
                break;

            case ('R'):
                SMTPS.ulFlags |= SMTPF_RELAY_ENABLED;
                break;

            case ('V'):
                SMTPS.ulFlags |= SMTPF_VRFY_ENABLED;
                break;

            case ('T'):
                SMTPS.ulFlags |= SMTPF_ETRN_ENABLED;
                break;

            case ('Z'):
                SMTPS.ulMaxMsgSize = 0;
                break;
        }

    }

///////////////////////////////////////////////////////////////////////////////
//  Clear bad ip mask and command delay
///////////////////////////////////////////////////////////////////////////////
    SMTPS.ulFlags &= ~SMTPF_BLOCKED_IP;

    SMTPS.iCmdDelay = 0;


    return (0);

}



static int      SMTPLogSession(SMTPSession & SMTPS, char const * pszSender,
                        char const * pszRecipient, char const * pszStatus, unsigned long ulMsgSize)
{

    char            szTime[256] = "";

    MscGetTimeNbrString(szTime, sizeof(szTime) - 1);


    RLCK_HANDLE     hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR SMTP_LOG_FILE);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    char            szIP[128] = "???.???.???.???";

    MscFileLog(SMTP_LOG_FILE, "\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%lu\""
            "\n", SMTPS.szSvrFQDN, SMTPS.szSvrDomain, SysInetNToA(SMTPS.PeerInfo, szIP),
            szTime, SMTPS.szClientDomain, SMTPS.szDestDomain, pszSender, pszRecipient,
            SMTPS.szMessageID, pszStatus, SMTPS.szLogonUser, ulMsgSize);


    RLckUnlockEX(hResLock);

    return (0);

}



static int      SMTPHandleSession(SHB_HANDLE hShbSMTP, BSOCK_HANDLE hBSock)
{

///////////////////////////////////////////////////////////////////////////////
//  Session structure declaration and init
///////////////////////////////////////////////////////////////////////////////
    SMTPSession     SMTPS;

    if (SMTPInitSession(hShbSMTP, hBSock, SMTPS) < 0)
    {
        BSckVSendString(hBSock, STD_SMTP_TIMEOUT,
                "421 %s service not available, closing transmission channel", SMTP_SERVER_NAME);

        return (ErrGetErrorCode());
    }


    char            szIP[128] = "???.???.???.???";

    SysLogMessage(LOG_LEV_MESSAGE, "SMTP client connection from [%s]\n",
            SysInetNToA(SMTPS.PeerInfo, szIP));

///////////////////////////////////////////////////////////////////////////////
//  Send welcome message
///////////////////////////////////////////////////////////////////////////////
    char            szTime[256] = "";

    MscGetTimeStr(szTime, sizeof(szTime) - 1);

    if (BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                    "220 %s %s service ready; %s", SMTPS.szTimeStamp,
                    SMTP_SERVER_NAME, szTime) < 0)
    {
        ErrorPush();
        SMTPClearSession(SMTPS);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Command loop
///////////////////////////////////////////////////////////////////////////////
    char            szCommand[1024] = "";

    while (!SvrInShutdown() && (SMTPS.iSMTPState != stateExit) &&
            (BSckGetString(hBSock, szCommand, sizeof(szCommand) - 1,
                            SMTPS.pSMTPCfg->iSessionTimeout) != NULL) &&
            (MscCmdStringCheck(szCommand) == 0))
    {
///////////////////////////////////////////////////////////////////////////////
//  Retrieve a fresh new configuration copy and test shutdown flag
///////////////////////////////////////////////////////////////////////////////
        SysFree(SMTPS.pSMTPCfg);

        SMTPS.pSMTPCfg = SMTPGetConfigCopy(hShbSMTP);

        if ((SMTPS.pSMTPCfg == NULL) || (SMTPS.pSMTPCfg->ulFlags & SMTPF_STOP_SERVER))
            break;

///////////////////////////////////////////////////////////////////////////////
//  Handle command
///////////////////////////////////////////////////////////////////////////////
        SMTPHandleCommand(szCommand, hBSock, SMTPS);

    }

    SysLogMessage(LOG_LEV_MESSAGE, "SMTP client exit [%s]\n",
            SysInetNToA(SMTPS.PeerInfo, szIP));

    SMTPClearSession(SMTPS);

    return (0);

}



static void     SMTPClearSession(SMTPSession & SMTPS)
{

    if (SMTPS.pMsgFile != NULL)
        fclose(SMTPS.pMsgFile), SMTPS.pMsgFile = NULL;

    SysRemove(SMTPS.szMsgFile);

    if (SMTPS.hSvrConfig != INVALID_SVRCFG_HANDLE)
        SvrReleaseConfigHandle(SMTPS.hSvrConfig), SMTPS.hSvrConfig = INVALID_SVRCFG_HANDLE;

    if (SMTPS.pSMTPCfg != NULL)
        SysFree(SMTPS.pSMTPCfg), SMTPS.pSMTPCfg = NULL;

    if (SMTPS.pszFrom != NULL)
        SysFree(SMTPS.pszFrom), SMTPS.pszFrom = NULL;

    if (SMTPS.pszRcpt != NULL)
        SysFree(SMTPS.pszRcpt), SMTPS.pszRcpt = NULL;

    if (SMTPS.pszSendRcpt != NULL)
        SysFree(SMTPS.pszSendRcpt), SMTPS.pszSendRcpt = NULL;

}



static void     SMTPResetSession(SMTPSession & SMTPS)
{

    SMTPS.ulFlags = SMTPS.ulSetupFlags | (SMTPS.ulFlags & SMTPF_RESET_MASK);
    SMTPS.ullMessageID = 0;
    SMTPS.iRcptCount = 0;
    SetEmptyString(SMTPS.szMessageID);

    if (SMTPS.pMsgFile != NULL)
        fclose(SMTPS.pMsgFile), SMTPS.pMsgFile = NULL;

    SysRemove(SMTPS.szMsgFile);

    SetEmptyString(SMTPS.szDestDomain);

    if (SMTPS.pszFrom != NULL)
        SysFree(SMTPS.pszFrom), SMTPS.pszFrom = NULL;

    if (SMTPS.pszRcpt != NULL)
        SysFree(SMTPS.pszRcpt), SMTPS.pszRcpt = NULL;

    if (SMTPS.pszSendRcpt != NULL)
        SysFree(SMTPS.pszSendRcpt), SMTPS.pszSendRcpt = NULL;


    SMTPS.iSMTPState = (SMTPS.ulFlags & SMTPF_AUTHENTICATED) ? stateAuthenticated :
            Min(SMTPS.iSMTPState, stateHelo);

}



static int      SMTPHandleCommand(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

///////////////////////////////////////////////////////////////////////////////
//  Delay protection against massive spammers
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.iCmdDelay > 0)
        SysSleep(SMTPS.iCmdDelay);

///////////////////////////////////////////////////////////////////////////////
//  Command parsing and processing
///////////////////////////////////////////////////////////////////////////////
    int             iCmdResult = -1;

    if (StrINComp(pszCommand, MAIL_FROM_STR) == 0)
        iCmdResult = SMTPHandleCmd_MAIL(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, RCPT_TO_STR) == 0)
        iCmdResult = SMTPHandleCmd_RCPT(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "DATA") == 0)
        iCmdResult = SMTPHandleCmd_DATA(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "HELO") == 0)
        iCmdResult = SMTPHandleCmd_HELO(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "EHLO") == 0)
        iCmdResult = SMTPHandleCmd_EHLO(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "AUTH") == 0)
        iCmdResult = SMTPHandleCmd_AUTH(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "RSET") == 0)
        iCmdResult = SMTPHandleCmd_RSET(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "VRFY") == 0)
        iCmdResult = SMTPHandleCmd_VRFY(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "ETRN") == 0)
        iCmdResult = SMTPHandleCmd_ETRN(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "NOOP") == 0)
        iCmdResult = SMTPHandleCmd_NOOP(pszCommand, hBSock, SMTPS);
    else if (StrINComp(pszCommand, "QUIT") == 0)
        iCmdResult = SMTPHandleCmd_QUIT(pszCommand, hBSock, SMTPS);
    else
        BSckSendString(hBSock, "500 Syntax error, command unrecognized", SMTPS.pSMTPCfg->iTimeout);

    return (iCmdResult);

}



static int      SMTPTryPopAuthIpCheck(SMTPSession & SMTPS, char const * pszUser,
                        char const * pszDomain)
{
///////////////////////////////////////////////////////////////////////////////
//  Load user info
///////////////////////////////////////////////////////////////////////////////
    UserInfo       *pUI = UsrGetUserByNameOrAlias(pszDomain, pszUser);

    if (pUI == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Perform IP file checking
///////////////////////////////////////////////////////////////////////////////
    int             iCheckResult = UPopUserIpCheck(pUI, &SMTPS.PeerInfo,
            SMTPS.pSMTPCfg->uPopAuthExpireTime);


    UsrFreeUserInfo(pUI);

    if (iCheckResult < 0)
        return (iCheckResult);

///////////////////////////////////////////////////////////////////////////////
//  Load user perms ( default with this implementation )
///////////////////////////////////////////////////////////////////////////////
    char            szPerms[128] = "";

    char           *pszDefultPerms = SvrGetConfigVar(SMTPS.hSvrConfig, "DefaultSmtpPerms", "MR");

    if (pszDefultPerms != NULL)
    {
        strcpy(szPerms, pszDefultPerms);

        SysFree(pszDefultPerms);
    }

///////////////////////////////////////////////////////////////////////////////
//  Apply user perms
///////////////////////////////////////////////////////////////////////////////
    SMTPApplyPerms(SMTPS, szPerms);


    return (0);

}



static int      SMTPCheckReturnPath(const char *pszCommand, char **ppszRetDomains,
                        SMTPSession & SMTPS, char *&pszSMTPError)
{

    int             iDomainCount = StrStringsCount(ppszRetDomains);

    if (iDomainCount == 0)
    {
        if (!SvrTestConfigFlag("AllowNullSender", true, SMTPS.hSvrConfig))
        {
            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, "", "", "SNDR=EEMPTY", 0);

            pszSMTPError = SysStrDup("501 Syntax error in return path");

            ErrSetErrorCode(ERR_BAD_RETURN_PATH);
            return (ERR_BAD_RETURN_PATH);
        }

        if (SMTPS.pszFrom != NULL)
            SysFree(SMTPS.pszFrom);

        SMTPS.pszFrom = SysStrDup("");

        return (0);
    }


    char            szMailerUser[MAX_ADDR_NAME] = "",
                    szMailerDomain[MAX_ADDR_NAME] = "";

    if (USmtpSplitEmailAddr(ppszRetDomains[0], szMailerUser, szMailerDomain) < 0)
    {
        ErrorPush();

        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, ppszRetDomains[0], "", "SNDR=ESYNTAX", 0);

        pszSMTPError = SysStrDup("501 Syntax error in return path");

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check mailer domain for DNS/MX entries
///////////////////////////////////////////////////////////////////////////////
    if (SvrTestConfigFlag("CheckMailerDomain", false, SMTPS.hSvrConfig) &&
            (USmtpCheckMailDomain(SMTPS.hSvrConfig, szMailerDomain) < 0))
    {
        ErrorPush();

        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, ppszRetDomains[0], "", "SNDR=ENODNS", 0);

        pszSMTPError = SysStrDup("505 Your domain has not DNS/MX entries");

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check if mail come from a spammer address ( spam-address.tab )
///////////////////////////////////////////////////////////////////////////////
    if (USmtpSpamAddressCheck(ppszRetDomains[0]) < 0)
    {
        ErrorPush();

        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, ppszRetDomains[0], "", "SNDR=ESPAM", 0);

        pszSMTPError = SysStrDup("504 You are registered as spammer");

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check SMTP after POP3 authentication
///////////////////////////////////////////////////////////////////////////////
    if (SvrTestConfigFlag("EnableAuthSMTP-POP3", true, SMTPS.hSvrConfig))
        SMTPTryPopAuthIpCheck(SMTPS, szMailerUser, szMailerDomain);

///////////////////////////////////////////////////////////////////////////////
//  Check extended mail from parameters
///////////////////////////////////////////////////////////////////////////////
    if (SMTPCheckMailParams(pszCommand, ppszRetDomains, SMTPS, pszSMTPError) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Setup From string
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.pszFrom != NULL)
        SysFree(SMTPS.pszFrom);

    SMTPS.pszFrom = SysStrDup(ppszRetDomains[0]);

    return (0);

}



static int      SMTPAddMessageInfo(SMTPSession & SMTPS)
{

    return (USmtpAddMessageInfo(SMTPS.pMsgFile, SMTPS.szClientDomain, SMTPS.PeerInfo,
                    SMTPS.szSvrDomain, SMTPS.SockInfo, SMTP_SERVER_NAME));

}



static int      SMTPCheckMailParams(const char *pszCommand, char **ppszRetDomains,
                        SMTPSession & SMTPS, char *&pszSMTPError)
{

    char const     *pszParams = strrchr(pszCommand, '>');

    if (pszParams == NULL)
        pszParams = pszCommand;

///////////////////////////////////////////////////////////////////////////////
//  Check the SIZE parameter
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.ulMaxMsgSize != 0)
    {
        char const     *pszSize = pszParams;

        while ((pszSize = StrIStr(pszSize, " SIZE=")) != NULL)
        {
            pszSize += CStringSize(" SIZE=");

            if (isdigit(*pszSize) && (SMTPS.ulMaxMsgSize < (unsigned long) atol(pszSize)))
            {
                if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                    SMTPLogSession(SMTPS, (ppszRetDomains[0] != NULL) ? ppszRetDomains[0]: "",
                            "", "SIZE=EBIG", (unsigned long) atol(pszSize));

                pszSMTPError = SysStrDup("552 Message exceeds fixed maximum message size");

                ErrSetErrorCode(ERR_MESSAGE_SIZE);
                return (ERR_MESSAGE_SIZE);
            }
        }

    }



    return (0);

}



static int      SMTPHandleCmd_MAIL(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    if ((SMTPS.iSMTPState != stateHelo) && (SMTPS.iSMTPState != stateAuthenticated))
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "503 Bad sequence of commands", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
        return (ERR_SMTP_BAD_CMD_SEQUENCE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Split the RETURN PATH
///////////////////////////////////////////////////////////////////////////////
    char          **ppszRetDomains = USmtpGetPathStrings(pszCommand);

    if (ppszRetDomains == NULL)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Check RETURN PATH
///////////////////////////////////////////////////////////////////////////////
    char           *pszSMTPError = NULL;

    if (SMTPCheckReturnPath(pszCommand, ppszRetDomains, SMTPS, pszSMTPError) < 0)
    {
        ErrorPush();
        StrFreeStrings(ppszRetDomains);
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, pszSMTPError, SMTPS.pSMTPCfg->iTimeout);
        SysFree(pszSMTPError);
        return (ErrorPop());
    }

    StrFreeStrings(ppszRetDomains);

///////////////////////////////////////////////////////////////////////////////
//  If the incoming IP is "mapped" stop here
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.ulFlags & SMTPF_BLOCKED_IP)
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "551 Server access forbidden by your IP", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_USE_FORBIDDEN);
        return (ERR_SMTP_USE_FORBIDDEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  If MAIL command is locked stop here
///////////////////////////////////////////////////////////////////////////////
    if ((SMTPS.ulFlags & SMTPF_MAIL_LOCKED) && !(SMTPS.ulFlags & SMTPF_MAIL_UNLOCKED))
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "551 Server use forbidden", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_USE_FORBIDDEN);
        return (ERR_SMTP_USE_FORBIDDEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Prepare mail file
///////////////////////////////////////////////////////////////////////////////
    if ((SMTPS.pMsgFile = fopen(SMTPS.szMsgFile, "wb")) == NULL)
    {
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ERR_FILE_CREATE);

        ErrSetErrorCode(ERR_FILE_CREATE, SMTPS.szMsgFile);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Write message infos ( 1st row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    if (SMTPAddMessageInfo(SMTPS) < 0)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Write domain ( 2nd row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    if (StrWriteCRLFString(SMTPS.pMsgFile, SMTPS.szSvrDomain) < 0)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Get SMTP message ID and write it to file ( 3th row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    if (SvrGetMessageID(&SMTPS.ullMessageID) < 0)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }

    sprintf(SMTPS.szMessageID, "S" SYS_LLX_FMT, SMTPS.ullMessageID);

    if (StrWriteCRLFString(SMTPS.pMsgFile, SMTPS.szMessageID) < 0)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Write MAIL FROM ( 4th row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    if (StrWriteCRLFString(SMTPS.pMsgFile, pszCommand) < 0)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }

    BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

    SMTPS.iSMTPState = stateMail;

    return (0);

}



static int      SMTPCheckRelayCapability(SMTPSession & SMTPS, char const * pszDestDomain)
{
///////////////////////////////////////////////////////////////////////////////
//  OK if enabled ( authentication )
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.ulFlags & SMTPF_RELAY_ENABLED)
        return (0);

///////////////////////////////////////////////////////////////////////////////
//  OK if destination domain is a custom-handled domain
///////////////////////////////////////////////////////////////////////////////
    if (USmlCustomizedDomain(pszDestDomain) == 0)
        return (0);

///////////////////////////////////////////////////////////////////////////////
//  IP based relay check
///////////////////////////////////////////////////////////////////////////////
    return (USmtpIsAllowedRelay(SMTPS.PeerInfo, SMTPS.hSvrConfig));

}




static int      SMTPCheckForwardPath(char **ppszFwdDomains, SMTPSession & SMTPS,
                        char *&pszSMTPError)
{

    int             iDomainCount = StrStringsCount(ppszFwdDomains);

    if (iDomainCount == 0)
    {
        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, SMTPS.pszFrom, "", "RCPT=ESYNTAX", 0);

        pszSMTPError = SysStrDup("501 Syntax error in forward path");

        ErrSetErrorCode(ERR_BAD_FORWARD_PATH);
        return (ERR_BAD_FORWARD_PATH);
    }

    char            szDestUser[MAX_ADDR_NAME] = "",
                    szDestDomain[MAX_ADDR_NAME] = "";

    if (USmtpSplitEmailAddr(ppszFwdDomains[0], szDestUser, szDestDomain) < 0)
    {
        ErrorPush();

        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ESYNTAX", 0);

        pszSMTPError = SysStrDup("501 Syntax error in forward path");

        return (ErrorPop());
    }

    if (iDomainCount == 1)
    {
        if (MDomIsHandledDomain(szDestDomain) == 0)
        {
///////////////////////////////////////////////////////////////////////////////
//  Check user existance
///////////////////////////////////////////////////////////////////////////////
            UserInfo       *pUI = UsrGetUserByNameOrAlias(szDestDomain, szDestUser);

            if (pUI != NULL)
            {
///////////////////////////////////////////////////////////////////////////////
//  Check if the account is enabled for receiving
///////////////////////////////////////////////////////////////////////////////
                if (!UsrGetUserInfoVarInt(pUI, "ReceiveEnable", 1))
                {
                    UsrFreeUserInfo(pUI);

                    if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                        SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=EDSBL", 0);

                    pszSMTPError = StrSprint("550 Account disabled <%s@%s>",
                            szDestUser, szDestDomain);

                    ErrSetErrorCode(ERR_USER_DISABLED);
                    return (ERR_USER_DISABLED);
                }


                if (UsrGetUserType(pUI) == usrTypeUser)
                {
///////////////////////////////////////////////////////////////////////////////
//  Target is a normal user
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//  Check user mailbox size
///////////////////////////////////////////////////////////////////////////////
                    if (UPopCheckMailboxSize(pUI) < 0)
                    {
                        ErrorPush();
                        UsrFreeUserInfo(pUI);

                        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                            SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=EFULL", 0);

                        pszSMTPError = StrSprint("452 Mailbox full <%s@%s>",
                                szDestUser, szDestDomain);

                        return (ErrorPop());
                    }

                }
                else
                {
///////////////////////////////////////////////////////////////////////////////
//  Target is a mailing list
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//  Check if client can post to this mailing list
///////////////////////////////////////////////////////////////////////////////
                    if (UsrMLCheckUserPost(pUI, SMTPS.pszFrom) < 0)
                    {
                        ErrorPush();
                        UsrFreeUserInfo(pUI);

                        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                            SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=EACCESS", 0);

                        pszSMTPError = StrSprint("557 Access denied <%s@%s> for user <%s>",
                                szDestUser, szDestDomain, SMTPS.pszFrom);

                        return (ErrorPop());
                    }

                }


                UsrFreeUserInfo(pUI);
            }
            else if (USmlIsCmdAliasAccount(szDestDomain, szDestUser) < 0)
            {
///////////////////////////////////////////////////////////////////////////////
//  Recipient domain is local but no account is found inside the standard
//  users/aliases database and the account is not handled with cmdaliases.
//  It's pretty much time to report a recipient error
///////////////////////////////////////////////////////////////////////////////
                if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                    SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=EAVAIL", 0);

                pszSMTPError = StrSprint("550 Mailbox unavailable <%s@%s>",
                        szDestUser, szDestDomain);

                ErrSetErrorCode(ERR_USER_NOT_LOCAL);
                return (ERR_USER_NOT_LOCAL);
            }
        }
        else
        {
///////////////////////////////////////////////////////////////////////////////
//  Check relay permission
///////////////////////////////////////////////////////////////////////////////
            if (SMTPCheckRelayCapability(SMTPS, szDestDomain) < 0)
            {
                ErrorPush();

                if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                    SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ERELAY", 0);

                pszSMTPError = SysStrDup("550 Relay denied");

                return (ErrorPop());
            }
        }
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Check relay permission
///////////////////////////////////////////////////////////////////////////////
        if (SMTPCheckRelayCapability(SMTPS, szDestDomain) < 0)
        {
            ErrorPush();

            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ERELAY", 0);

            pszSMTPError = SysStrDup("550 Relay denied");

            return (ErrorPop());
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Retrieve destination domain
///////////////////////////////////////////////////////////////////////////////
    if (USmtpSplitEmailAddr(ppszFwdDomains[0], NULL, SMTPS.szDestDomain) < 0)
    {
        ErrorPush();

        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ESYNTAX", 0);

        pszSMTPError = SysStrDup("501 Syntax error in forward path");

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup SendRcpt string ( it'll be used to build "RCPT TO:<>" line into
//  the message file
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.pszSendRcpt != NULL)
        SysFree(SMTPS.pszSendRcpt);

    if ((SMTPS.pszSendRcpt = USmtpBuildRcptPath(ppszFwdDomains, SMTPS.hSvrConfig)) == NULL)
    {
        ErrorPush();

        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, SMTPS.pszFrom, ppszFwdDomains[0], "RCPT=ESYNTAX", 0);

        pszSMTPError = SysStrDup("501 Syntax error in forward path");

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup Rcpt string
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.pszRcpt != NULL)
        SysFree(SMTPS.pszRcpt);

    SMTPS.pszRcpt = SysStrDup(ppszFwdDomains[0]);

    return (0);

}



static int      SMTPHandleCmd_RCPT(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    if ((SMTPS.iSMTPState != stateMail) && (SMTPS.iSMTPState != stateRcpt))
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "503 Bad sequence of commands", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
        return (ERR_SMTP_BAD_CMD_SEQUENCE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Check recipients count
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.iRcptCount >= SMTPS.pSMTPCfg->iMaxRcpts)
    {
        BSckSendString(hBSock, "552 Too many recipients", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_TOO_MANY_RECIPIENTS);
        return (ERR_SMTP_TOO_MANY_RECIPIENTS);
    }

    char          **ppszFwdDomains = USmtpGetPathStrings(pszCommand);

    if (ppszFwdDomains == NULL)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }
///////////////////////////////////////////////////////////////////////////////
//  Check FORWARD PATH
///////////////////////////////////////////////////////////////////////////////
    char           *pszSMTPError = NULL;

    if (SMTPCheckForwardPath(ppszFwdDomains, SMTPS, pszSMTPError) < 0)
    {
        ErrorPush();
        StrFreeStrings(ppszFwdDomains);

        BSckSendString(hBSock, pszSMTPError, SMTPS.pSMTPCfg->iTimeout);
        SysFree(pszSMTPError);
        return (ErrorPop());
    }

    StrFreeStrings(ppszFwdDomains);

///////////////////////////////////////////////////////////////////////////////
//  Log SMTP session
///////////////////////////////////////////////////////////////////////////////
    if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
        SMTPLogSession(SMTPS, SMTPS.pszFrom, SMTPS.pszRcpt, "RCPT=OK", 0);

///////////////////////////////////////////////////////////////////////////////
//  Write RCPT TO ( 5th[,...] row(s) of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    fprintf(SMTPS.pMsgFile, "RCPT TO:<%s>\r\n", SMTPS.pszSendRcpt);


    BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

    ++SMTPS.iRcptCount;

    SMTPS.iSMTPState = stateRcpt;

    return (0);

}



static int      SMTPHandleCmd_DATA(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    if (SMTPS.iSMTPState != stateRcpt)
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "503 Bad sequence of commands", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
        return (ERR_SMTP_BAD_CMD_SEQUENCE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Write data begin marker
///////////////////////////////////////////////////////////////////////////////
    if (StrWriteCRLFString(SMTPS.pMsgFile, SPOOL_FILE_DATA_START) < 0)
    {
        ErrorPush();
        SMTPResetSession(SMTPS);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());
        return (ErrorPop());
    }


    BSckSendString(hBSock, "354 Start mail input; end with <CRLF>.<CRLF>", SMTPS.pSMTPCfg->iTimeout);

///////////////////////////////////////////////////////////////////////////////
//  Write data
///////////////////////////////////////////////////////////////////////////////
    int             iErrorCode = 0,
                    iLineLength;
    unsigned long   ulMessageSize = 0,
                    ulMaxMsgSize = SMTPS.ulMaxMsgSize;
    char const     *pszSmtpError = NULL;
    char            szBuffer[SMTP_MAX_LINE_SIZE + 4];

    for (;;)
    {
        if (BSckGetString(hBSock, szBuffer, sizeof(szBuffer) - 3, SMTPS.pSMTPCfg->iTimeout,
                &iLineLength) == NULL)
        {
            ErrorPush();
            fclose(SMTPS.pMsgFile), SMTPS.pMsgFile = NULL;
            SMTPResetSession(SMTPS);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Check end of data condition
///////////////////////////////////////////////////////////////////////////////
        if (strcmp(szBuffer, ".") == 0)
            break;

///////////////////////////////////////////////////////////////////////////////
//  Correctly terminate the line
///////////////////////////////////////////////////////////////////////////////
        memcpy(szBuffer + iLineLength, "\r\n", 3);

        iLineLength += 2;


        if (iErrorCode == 0)
        {
///////////////////////////////////////////////////////////////////////////////
//  Write data on disk
///////////////////////////////////////////////////////////////////////////////
            if (!fwrite(szBuffer, iLineLength, 1, SMTPS.pMsgFile))
            {
                ErrSetErrorCode(iErrorCode = ERR_FILE_WRITE, SMTPS.szMsgFile);

            }

        }


        ulMessageSize += (unsigned long) iLineLength;


///////////////////////////////////////////////////////////////////////////////
//  Check the message size
///////////////////////////////////////////////////////////////////////////////
        if ((ulMaxMsgSize != 0) && (ulMaxMsgSize < ulMessageSize))
        {
            pszSmtpError = "552 Message exceeds fixed maximum message size";

            ErrSetErrorCode(iErrorCode = ERR_MESSAGE_SIZE);
        }


        if (SvrInShutdown())
        {
            fclose(SMTPS.pMsgFile), SMTPS.pMsgFile = NULL;
            SMTPResetSession(SMTPS);

            ErrSetErrorCode(ERR_SERVER_SHUTDOWN);
            return (ERR_SERVER_SHUTDOWN);
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Check fclose() return value coz data might be buffered and fail to flush
///////////////////////////////////////////////////////////////////////////////
    if (fclose(SMTPS.pMsgFile))
        ErrSetErrorCode(iErrorCode = ERR_FILE_WRITE, SMTPS.szMsgFile);

    SMTPS.pMsgFile = NULL;

    if (iErrorCode == 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  Transfer spool file
///////////////////////////////////////////////////////////////////////////////
        if (SMTPSubmitPackedFile(SMTPS.szMsgFile) < 0)
            BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                    "451 Requested action aborted: (%d) local error in processing", ErrGetErrorCode());
        else
        {
///////////////////////////////////////////////////////////////////////////////
//  Log the message receive
///////////////////////////////////////////////////////////////////////////////
            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, SMTPS.pszFrom, SMTPS.pszRcpt, "RECV=OK", ulMessageSize);

///////////////////////////////////////////////////////////////////////////////
//  Send the ack only when everything is OK
///////////////////////////////////////////////////////////////////////////////
            BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "250 OK <%s>", SMTPS.szMessageID);

        }
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Notify the client the error condition
///////////////////////////////////////////////////////////////////////////////
        if (pszSmtpError == NULL)
            BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                    "451 Requested action aborted: (%d) local error in processing",
                    ErrGetErrorCode());
        else
            BSckSendString(hBSock, pszSmtpError, SMTPS.pSMTPCfg->iTimeout);

    }


    SMTPResetSession(SMTPS);

    return (iErrorCode);

}



static int      SMTPAddReceived(char const * const * ppszMsgInfo, char const * pszMailFrom,
                        char const * pszRcptTo, char const * pszMessageID, FILE * pMailFile)
{

    char           *pszReceived = USmtpGetReceived(ppszMsgInfo, pszMailFrom, pszRcptTo, pszMessageID);

    if (pszReceived == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Write "Received:" tag
///////////////////////////////////////////////////////////////////////////////
    fputs(pszReceived, pMailFile);


    SysFree(pszReceived);

    return (0);

}



static int      SMTPSubmitPackedFile(const char *pszPkgFile)
{

    FILE           *pPkgFile = fopen(pszPkgFile, "rb");

    if (pPkgFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }


    char            szSpoolLine[MAX_SPOOL_LINE] = "";

    while ((MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL) &&
            (strncmp(szSpoolLine, SPOOL_FILE_DATA_START, CStringSize(SPOOL_FILE_DATA_START)) != 0));

    if (strncmp(szSpoolLine, SPOOL_FILE_DATA_START, CStringSize(SPOOL_FILE_DATA_START)) != 0)
    {
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Get the offset at which the message data begin and rewind the file
///////////////////////////////////////////////////////////////////////////////
    unsigned long   ulMsgOffset = (unsigned long) ftell(pPkgFile);

    rewind(pPkgFile);

///////////////////////////////////////////////////////////////////////////////
//  Read SMTP message info ( 1st row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    char          **ppszMsgInfo = NULL;

    if ((MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            ((ppszMsgInfo = StrTokenize(szSpoolLine, ";")) == NULL) ||
            (StrStringsCount(ppszMsgInfo) < smsgiMax))
    {
        if (ppszMsgInfo != NULL)
            StrFreeStrings(ppszMsgInfo);
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read SMTP domain ( 2nd row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    char            szSMTPDomain[256] = "";

    if (MscGetString(pPkgFile, szSMTPDomain, sizeof(szSMTPDomain) - 1) == NULL)
    {
        StrFreeStrings(ppszMsgInfo);
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read message ID ( 3th row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    char            szMessageID[128] = "";

    if (MscGetString(pPkgFile, szMessageID, sizeof(szMessageID) - 1) == NULL)
    {
        StrFreeStrings(ppszMsgInfo);
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read "MAIL FROM:" ( 4th row of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    char            szMailFrom[MAX_SPOOL_LINE] = "";

    if ((MscGetString(pPkgFile, szMailFrom, sizeof(szMailFrom) - 1) == NULL) ||
            (StrINComp(szMailFrom, MAIL_FROM_STR) != 0))
    {
        StrFreeStrings(ppszMsgInfo);
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }


///////////////////////////////////////////////////////////////////////////////
//  Read "RCPT TO:" ( 5th[,...] row(s) of the smtp-mail file )
///////////////////////////////////////////////////////////////////////////////
    while ((MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL) &&
            (StrINComp(szSpoolLine, RCPT_TO_STR) == 0))
    {
///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
        QMSG_HANDLE     hMessage = QueCreateMessage(hSpoolQueue);

        if (hMessage == INVALID_QMSG_HANDLE)
        {
            ErrorPush();
            StrFreeStrings(ppszMsgInfo);
            fclose(pPkgFile);
            return (ErrorPop());
        }

        char            szQueueFilePath[SYS_MAX_PATH] = "";

        QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);


        FILE           *pSpoolFile = fopen(szQueueFilePath, "wb");

        if (pSpoolFile == NULL)
        {
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszMsgInfo);
            fclose(pPkgFile);
            ErrSetErrorCode(ERR_FILE_CREATE);
            return (ERR_FILE_CREATE);
        }


///////////////////////////////////////////////////////////////////////////////
//  Write SMTP domain
///////////////////////////////////////////////////////////////////////////////
        fprintf(pSpoolFile, "%s\r\n", szSMTPDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write message ID
///////////////////////////////////////////////////////////////////////////////
        fprintf(pSpoolFile, "%s\r\n", szMessageID);

///////////////////////////////////////////////////////////////////////////////
//  Write "MAIL FROM:"
///////////////////////////////////////////////////////////////////////////////
        fprintf(pSpoolFile, "%s\r\n", szMailFrom);

///////////////////////////////////////////////////////////////////////////////
//  Write "RCPT TO:"
///////////////////////////////////////////////////////////////////////////////
        fprintf(pSpoolFile, "%s\r\n", szSpoolLine);

///////////////////////////////////////////////////////////////////////////////
//  Write SPOOL_FILE_DATA_START
///////////////////////////////////////////////////////////////////////////////
        fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

///////////////////////////////////////////////////////////////////////////////
//  Write "Received:" tag
///////////////////////////////////////////////////////////////////////////////
        SMTPAddReceived(ppszMsgInfo, szMailFrom, szSpoolLine, szMessageID, pSpoolFile);

///////////////////////////////////////////////////////////////////////////////
//  Write mail data, saving and restoring the current file pointer
///////////////////////////////////////////////////////////////////////////////
        unsigned long   ulCurrOffset = (unsigned long) ftell(pPkgFile);

        if (MscCopyFile(pSpoolFile, pPkgFile, ulMsgOffset, (unsigned long) -1) < 0)
        {
            ErrorPush();
            fclose(pSpoolFile);
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszMsgInfo);
            fclose(pPkgFile);
            return (ErrorPop());
        }

        if (SysFileSync(pSpoolFile) < 0)
        {
            ErrorPush();
            fclose(pSpoolFile);
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszMsgInfo);
            fclose(pPkgFile);
            return (ErrorPop());
        }

        if (fclose(pSpoolFile))
        {
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszMsgInfo);
            fclose(pPkgFile);
            ErrSetErrorCode(ERR_FILE_WRITE, szQueueFilePath);
            return (ERR_FILE_WRITE);
        }

        fseek(pPkgFile, ulCurrOffset, SEEK_SET);

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitMessage(hSpoolQueue, hMessage) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszMsgInfo);
            fclose(pPkgFile);
            return (ErrorPop());
        }
    }

    StrFreeStrings(ppszMsgInfo);
    fclose(pPkgFile);

    return (0);

}



static int      SMTPHandleCmd_HELO(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    if ((SMTPS.iSMTPState != stateInit) && (SMTPS.iSMTPState != stateHelo))
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "503 Bad sequence of commands", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
        return (ERR_SMTP_BAD_CMD_SEQUENCE);
    }


    char          **ppszTokens = StrTokenize(pszCommand, " ");

    if ((ppszTokens == NULL) || (StrStringsCount(ppszTokens) != 2))
    {
        if (ppszTokens != NULL)
            StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "501 Syntax error in parameters or arguments", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }


    StrSNCpy(SMTPS.szClientDomain, ppszTokens[1]);


    StrFreeStrings(ppszTokens);


    char           *pszDomain = SvrGetConfigVar(SMTPS.hSvrConfig, "RootDomain");

    if (pszDomain == NULL)
    {
        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ERR_NO_ROOT_DOMAIN_VAR);

        ErrSetErrorCode(ERR_NO_ROOT_DOMAIN_VAR);
        return (ERR_NO_ROOT_DOMAIN_VAR);
    }


    BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "250 %s", pszDomain);


    SysFree(pszDomain);

    SMTPS.iSMTPState = stateHelo;

    return (0);

}



static int      SMTPHandleCmd_EHLO(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    if ((SMTPS.iSMTPState != stateInit) && (SMTPS.iSMTPState != stateHelo))
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "503 Bad sequence of commands", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
        return (ERR_SMTP_BAD_CMD_SEQUENCE);
    }


    char          **ppszTokens = StrTokenize(pszCommand, " ");

    if ((ppszTokens == NULL) || (StrStringsCount(ppszTokens) != 2))
    {
        if (ppszTokens != NULL)
            StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "501 Syntax error in parameters or arguments", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }


    StrSNCpy(SMTPS.szClientDomain, ppszTokens[1]);


    StrFreeStrings(ppszTokens);


///////////////////////////////////////////////////////////////////////////////
//  Create response file
///////////////////////////////////////////////////////////////////////////////
    char            szRespFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szRespFile);

    FILE           *pRespFile = fopen(szRespFile, "w+b");

    if (pRespFile == NULL)
    {
        CheckRemoveFile(szRespFile);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ERR_FILE_CREATE);

        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Get root domain ( "RootDomain" )
///////////////////////////////////////////////////////////////////////////////
    char           *pszDomain = SvrGetConfigVar(SMTPS.hSvrConfig, "RootDomain");

    if (pszDomain == NULL)
    {
        fclose(pRespFile);
        SysRemove(szRespFile);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ERR_NO_ROOT_DOMAIN_VAR);

        ErrSetErrorCode(ERR_NO_ROOT_DOMAIN_VAR);
        return (ERR_NO_ROOT_DOMAIN_VAR);
    }

///////////////////////////////////////////////////////////////////////////////
//  Build EHLO response file ( domain + auths )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile,
            "250 %s\r\n", pszDomain);

    SysFree(pszDomain);

///////////////////////////////////////////////////////////////////////////////
//  Emit extended SMTP command and internal auths
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile,
            "250 VRFY\r\n"
            "250 ETRN\r\n"
            "250 8BITMIME\r\n"
            "250 PIPELINING\r\n"
            "250 AUTH LOGIN PLAIN CRAM-MD5");

///////////////////////////////////////////////////////////////////////////////
//  Emit external authentication methods
///////////////////////////////////////////////////////////////////////////////
    SMTPListExtAuths(pRespFile, SMTPS);

    fprintf(pRespFile, "\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Emit maximum message size ( if set )
///////////////////////////////////////////////////////////////////////////////
    if (SMTPS.ulMaxMsgSize != 0)
        fprintf(pRespFile, "250 SIZE %lu\r\n", SMTPS.ulMaxMsgSize);
    else
        fprintf(pRespFile, "250 SIZE\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Send EHLO response file
///////////////////////////////////////////////////////////////////////////////
    if (SMTPSendMultilineResponse(hBSock, SMTPS.pSMTPCfg->iTimeout, pRespFile) < 0)
    {
        ErrorPush();
        fclose(pRespFile);
        SysRemove(szRespFile);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());

        return (ErrorPop());
    }

    fclose(pRespFile);
    SysRemove(szRespFile);

    SMTPS.iSMTPState = stateHelo;

    return (0);

}



static int      SMTPListExtAuths(FILE * pRespFile, SMTPSession & SMTPS)
{

    char            szExtAuthFilePath[SYS_MAX_PATH] = "";

    SMTPGetExtAuthFilePath(szExtAuthFilePath);


    FILE           *pExtAuthFile = fopen(szExtAuthFilePath, "rt");

    if (pExtAuthFile != NULL)
    {
        char            szExtAuthLine[SVR_SMTP_EXTAUTH_LINE_MAX] = "";

        while (MscGetConfigLine(szExtAuthLine, sizeof(szExtAuthLine) - 1, pExtAuthFile) != NULL)
        {
            char          **ppszStrings = StrGetTabLineStrings(szExtAuthLine);

            if (ppszStrings == NULL)
                continue;

            if (StrStringsCount(ppszStrings) > 0)
                fprintf(pRespFile, " %s", ppszStrings[0]);

            StrFreeStrings(ppszStrings);
        }

        fclose(pExtAuthFile);
    }

    return (0);

}



static int      SMTPExternalAuthSubstitute(char **ppszAuthTokens, char const * pszChallenge,
                        char const * pszDigest, char const * pszSecretsFile)
{

    for (int ii = 0; ppszAuthTokens[ii] != NULL; ii++)
    {
        if (strcmp(ppszAuthTokens[ii], "@@FSECRT") == 0)
        {
            char           *pszNewValue = SysStrDup(pszSecretsFile);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszAuthTokens[ii]);

            ppszAuthTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszAuthTokens[ii], "@@CHALL") == 0)
        {
            char           *pszNewValue = SysStrDup(pszChallenge);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszAuthTokens[ii]);

            ppszAuthTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszAuthTokens[ii], "@@DGEST") == 0)
        {
            char           *pszNewValue = SysStrDup(pszDigest);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszAuthTokens[ii]);

            ppszAuthTokens[ii] = pszNewValue;
        }
    }

    return (0);

}



static int      SMTPCreateSecretsFile(char const * pszSecretsFile)
{

    char            szAuthFilePath[SYS_MAX_PATH] = "";

    SMTPGetAuthFilePath(szAuthFilePath);


    FILE           *pAuthFile = fopen(szAuthFilePath, "rt");

    if (pAuthFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, szAuthFilePath);
        return (ERR_FILE_OPEN);
    }


    FILE           *pSecretFile = fopen(pszSecretsFile, "wt");

    if (pSecretFile == NULL)
    {
        fclose(pAuthFile);

        ErrSetErrorCode(ERR_FILE_CREATE, pszSecretsFile);
        return (ERR_FILE_CREATE);
    }


    char            szAuthLine[SVR_SMTPAUTH_LINE_MAX] = "";

    while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szAuthLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if (iFieldsCount >= smtpaMax)
            fprintf(pSecretFile, "%s:%s\n", ppszStrings[smtpaUsername], ppszStrings[smtpaPassword]);

        StrFreeStrings(ppszStrings);
    }

    fclose(pSecretFile);
    fclose(pAuthFile);

    return (0);

}



static int      SMTPExternalAuthenticate(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char **ppszAuthTokens)
{
///////////////////////////////////////////////////////////////////////////////
//  Emit encoded ( base64 ) challenge ( param1 + ':' + timestamp )
//  and get client response
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uEnc64Length = 0;
    char            szChallenge[1024] = "",
                    szDigest[1024] = "";

    SysSNPrintf(szDigest, sizeof(szDigest) - 1, "%s:%s", ppszAuthTokens[1], SMTPS.szTimeStamp);
    encode64(szDigest, strlen(szDigest), szChallenge, sizeof(szChallenge) - 1, &uEnc64Length);

    if ((BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s", szChallenge) < 0) ||
            (BSckGetString(hBSock, szChallenge, sizeof(szChallenge) - 1, SMTPS.pSMTPCfg->iTimeout) == NULL))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Decode ( base64 ) client response
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uDec64Length = 0;

    if (decode64(szChallenge, strlen(szChallenge), szDigest, &uDec64Length) != 0)
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

    SysSNPrintf(szChallenge, sizeof(szChallenge) - 1, "%s:%s", ppszAuthTokens[1],
            SMTPS.szTimeStamp);

///////////////////////////////////////////////////////////////////////////////
//  Create secrets file ( username + ':' + password )
///////////////////////////////////////////////////////////////////////////////
    char            szSecretsFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szSecretsFile);

    if (SMTPCreateSecretsFile(szSecretsFile) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szSecretsFile);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Do macro substitution
///////////////////////////////////////////////////////////////////////////////
    SMTPExternalAuthSubstitute(ppszAuthTokens, szChallenge, szDigest, szSecretsFile);

///////////////////////////////////////////////////////////////////////////////
//  Call external program to compute the response
///////////////////////////////////////////////////////////////////////////////
    int             iExitCode = -1;

    if (SysExec(ppszAuthTokens[2], &ppszAuthTokens[2], SVR_SMTP_EXTAUTH_TIMEOUT,
                    SVR_SMTP_EXTAUTH_PRIORITY, &iExitCode) < 0)
    {
        ErrorPush();
        SysRemove(szSecretsFile);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());

        return (ErrorPop());
    }

    if (iExitCode != SVR_SMTP_EXTAUTH_SUCCESS)
    {
        SysRemove(szSecretsFile);

        BSckSendString(hBSock, "503 Authentication failed", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_EXTRNPRG_EXITCODE);
        return (ERR_BAD_EXTRNPRG_EXITCODE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Load response file containing the matching secret
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uRespSize = 0;
    char           *pMatchSecret = (char *) MscLoadFile(szSecretsFile, uRespSize);

    CheckRemoveFile(szSecretsFile);

    if (pMatchSecret == NULL)
    {
        ErrorPush();

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing", ErrorFetch());

        return (ErrorPop());
    }

    while ((uRespSize > 0) &&
            ((pMatchSecret[uRespSize - 1] == '\r') || (pMatchSecret[uRespSize - 1] == '\n')))
        --uRespSize;

    pMatchSecret[uRespSize] = '\0';

///////////////////////////////////////////////////////////////////////////////
//  Try to extract username and password tokens ( username + ':' + password )
///////////////////////////////////////////////////////////////////////////////
    char          **ppszTokens = StrTokenize(pMatchSecret, ":");

    SysFree(pMatchSecret);

    if (StrStringsCount(ppszTokens) != 2)
    {
        StrFreeStrings(ppszTokens);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing",
                ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE);

        ErrSetErrorCode(ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE);
        return (ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Lookup smtp auth file
///////////////////////////////////////////////////////////////////////////////
    char            szPerms[128] = "";

    if (SMTPCheckUsrPwdAuth(SMTPS.hSvrConfig, ppszTokens[0], ppszTokens[1], szPerms) < 0)
    {
        ErrorPush();
        StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "503 Authentication failed", SMTPS.pSMTPCfg->iTimeout);

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Apply user perms to SMTP config
///////////////////////////////////////////////////////////////////////////////
    SMTPApplyPerms(SMTPS, szPerms);

///////////////////////////////////////////////////////////////////////////////
//  Set the logon user
///////////////////////////////////////////////////////////////////////////////
    StrSNCpy(SMTPS.szLogonUser, ppszTokens[0]);


    StrFreeStrings(ppszTokens);


    SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
    SMTPS.iSMTPState = stateAuthenticated;

    BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

    return (0);

}



static int      SMTPDoAuthExternal(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthType)
{

    char            szExtAuthFilePath[SYS_MAX_PATH] = "";

    SMTPGetExtAuthFilePath(szExtAuthFilePath);


    FILE           *pExtAuthFile = fopen(szExtAuthFilePath, "rt");

    if (pExtAuthFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, szExtAuthFilePath);
        return (ERR_FILE_OPEN);
    }

    char            szExtAuthLine[SVR_SMTP_EXTAUTH_LINE_MAX] = "";

    while (MscGetConfigLine(szExtAuthLine, sizeof(szExtAuthLine) - 1, pExtAuthFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szExtAuthLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount > 5) && (stricmp(ppszStrings[0], pszAuthType) == 0))
        {
            fclose(pExtAuthFile);


            int             iAuthResult = SMTPExternalAuthenticate(hBSock, SMTPS, ppszStrings);


            StrFreeStrings(ppszStrings);

            return (iAuthResult);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pExtAuthFile);


    BSckSendString(hBSock, "504 Unknown authentication", SMTPS.pSMTPCfg->iTimeout);

    ErrSetErrorCode(ERR_UNKNOWN_SMTP_AUTH, pszAuthType);
    return (ERR_UNKNOWN_SMTP_AUTH);

}



static int      SMTPDoAuthPlain(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthParam)
{
///////////////////////////////////////////////////////////////////////////////
//  Parameter validation
///////////////////////////////////////////////////////////////////////////////
    if ((pszAuthParam == NULL) || (strlen(pszAuthParam) == 0))
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

///////////////////////////////////////////////////////////////////////////////
//  Decode ( base64 ) auth parameter
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uDec64Length = 0;
    char            szClientAuth[PLAIN_AUTH_PARAM_SIZE] = "";

    ZeroData(szClientAuth);

    if (decode64(pszAuthParam, strlen(pszAuthParam), szClientAuth, &uDec64Length) != 0)
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

///////////////////////////////////////////////////////////////////////////////
//  Extract plain auth params ( unused + 0 + username + 0 + password )
///////////////////////////////////////////////////////////////////////////////
    char           *pszUsername = szClientAuth + strlen(szClientAuth) + 1,
                   *pszPassword = pszUsername + strlen(pszUsername) + 1;

///////////////////////////////////////////////////////////////////////////////
//  Validate client response
///////////////////////////////////////////////////////////////////////////////
    char            szPerms[128] = "";

    if ((SMTPCheckLocalAuth(SMTPS.hSvrConfig, pszUsername, pszPassword, szPerms) < 0) &&
            (SMTPCheckUsrPwdAuth(SMTPS.hSvrConfig, pszUsername, pszPassword, szPerms) < 0))
    {
        ErrorPush();

        BSckSendString(hBSock, "503 Authentication failed", SMTPS.pSMTPCfg->iTimeout);

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Apply user perms to SMTP config
///////////////////////////////////////////////////////////////////////////////
    SMTPApplyPerms(SMTPS, szPerms);

///////////////////////////////////////////////////////////////////////////////
//  Set the logon user
///////////////////////////////////////////////////////////////////////////////
    StrSNCpy(SMTPS.szLogonUser, pszUsername);


    SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
    SMTPS.iSMTPState = stateAuthenticated;

    BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

    return (0);

}



static int      SMTPDoAuthLogin(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthParam)
{
///////////////////////////////////////////////////////////////////////////////
//  Emit encoded64 username request
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uEnc64Length = 0;
    char            szUsername[512] = "";

    encode64(LOGIN_AUTH_USERNAME, strlen(LOGIN_AUTH_USERNAME), szUsername,
            sizeof(szUsername), &uEnc64Length);

    if ((BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s", szUsername) < 0) ||
            (BSckGetString(hBSock, szUsername, sizeof(szUsername) - 1, SMTPS.pSMTPCfg->iTimeout) == NULL))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Emit encoded64 password request
///////////////////////////////////////////////////////////////////////////////
    char            szPassword[512] = "";

    encode64(LOGIN_AUTH_PASSWORD, strlen(LOGIN_AUTH_PASSWORD), szPassword,
            sizeof(szPassword), &uEnc64Length);

    if ((BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s", szPassword) < 0) ||
            (BSckGetString(hBSock, szPassword, sizeof(szPassword) - 1, SMTPS.pSMTPCfg->iTimeout) == NULL))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Decode ( base64 ) username
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uDec64Length = 0;
    char            szDecodeBuffer[512] = "";

    if (decode64(szUsername, strlen(szUsername), szDecodeBuffer, &uDec64Length) != 0)
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

    strcpy(szUsername, szDecodeBuffer);

///////////////////////////////////////////////////////////////////////////////
//  Decode ( base64 ) password
///////////////////////////////////////////////////////////////////////////////
    if (decode64(szPassword, strlen(szPassword), szDecodeBuffer, &uDec64Length) != 0)
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

    strcpy(szPassword, szDecodeBuffer);

///////////////////////////////////////////////////////////////////////////////
//  Validate client response
///////////////////////////////////////////////////////////////////////////////
    char            szPerms[128] = "";

    if ((SMTPCheckLocalAuth(SMTPS.hSvrConfig, szUsername, szPassword, szPerms) < 0) &&
            (SMTPCheckUsrPwdAuth(SMTPS.hSvrConfig, szUsername, szPassword, szPerms) < 0))
    {
        ErrorPush();

        BSckSendString(hBSock, "503 Authentication failed", SMTPS.pSMTPCfg->iTimeout);

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Apply user perms to SMTP config
///////////////////////////////////////////////////////////////////////////////
    SMTPApplyPerms(SMTPS, szPerms);


    SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
    SMTPS.iSMTPState = stateAuthenticated;

    BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

    return (0);

}



static char    *SMTPGetAuthFilePath(char *pszFilePath)
{

    CfgGetRootPath(pszFilePath);

    strcat(pszFilePath, SVR_SMTP_AUTH_FILE);

    return (pszFilePath);

}



static char    *SMTPGetExtAuthFilePath(char *pszFilePath)
{

    CfgGetRootPath(pszFilePath);

    strcat(pszFilePath, SVR_SMTP_EXTAUTH_FILE);

    return (pszFilePath);

}



static int      SMTPCheckLocalAuth(SVRCFG_HANDLE hSvrConfig, char const * pszUsername,
                        char const * pszPassword, char * pszPerms)
{
///////////////////////////////////////////////////////////////////////////////
//  First try to lookup  mailusers.tab
///////////////////////////////////////////////////////////////////////////////
    char            szAccountUser[MAX_ADDR_NAME] = "",
                    szAccountDomain[MAX_HOST_NAME] = "";

    if (StrSplitString(pszUsername, POP3_USER_SPLITTERS, szAccountUser, sizeof(szAccountUser),
                    szAccountDomain, sizeof(szAccountDomain)) < 0)
        return (ErrGetErrorCode());


    UserInfo       *pUI = UsrGetUserByName(szAccountDomain, szAccountUser);

    if (pUI != NULL)
    {
        if (strcmp(pUI->pszPassword, pszPassword) == 0)
        {
            if (SMTPGetUserSmtpPerms(pUI, hSvrConfig, pszPerms) < 0)
            {
                ErrorPush();
                UsrFreeUserInfo(pUI);
                return (ErrorPop());
            }

            UsrFreeUserInfo(pUI);

            return (0);
        }

        UsrFreeUserInfo(pUI);

    }


    ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
    return (ERR_SMTP_AUTH_FAILED);

}




static int      SMTPGetUserSmtpPerms(UserInfo * pUI, SVRCFG_HANDLE hSvrConfig, char *pszPerms)
{

    char           *pszUserPerms = UsrGetUserInfoVar(pUI, "SmtpPerms");

    if (pszUserPerms != NULL)
    {
        strcpy(pszPerms, pszUserPerms);

        SysFree(pszUserPerms);
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Match found, get the default permissions
///////////////////////////////////////////////////////////////////////////////
        char           *pszDefultPerms = SvrGetConfigVar(hSvrConfig,
                                "DefaultSmtpPerms", "MR");

        if (pszDefultPerms != NULL)
        {
            strcpy(pszPerms, pszDefultPerms);

            SysFree(pszDefultPerms);
        }
        else
            SetEmptyString(pszPerms);

    }

    return (0);

}




static int      SMTPCheckLocalCramMD5Auth(SVRCFG_HANDLE hSvrConfig, char const * pszChallenge,
                        char const * pszUsername, char const * pszDigest, char *pszPerms)
{
///////////////////////////////////////////////////////////////////////////////
//  First try to lookup  mailusers.tab
///////////////////////////////////////////////////////////////////////////////
    char            szAccountUser[MAX_ADDR_NAME] = "",
                    szAccountDomain[MAX_HOST_NAME] = "";

    if (StrSplitString(pszUsername, POP3_USER_SPLITTERS, szAccountUser, sizeof(szAccountUser),
                    szAccountDomain, sizeof(szAccountDomain)) < 0)
        return (ErrGetErrorCode());

    UserInfo       *pUI = UsrGetUserByName(szAccountDomain, szAccountUser);

    if (pUI != NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  Compute MD5 response ( secret , challenge , digest )
///////////////////////////////////////////////////////////////////////////////
        char            szCurrDigest[512] = "";

        if (MscCramMD5(pUI->pszPassword, pszChallenge, szCurrDigest) < 0)
        {
            UsrFreeUserInfo(pUI);

            return (ErrGetErrorCode());
        }

        if (stricmp(szCurrDigest, pszDigest) == 0)
        {
            if (SMTPGetUserSmtpPerms(pUI, hSvrConfig, pszPerms) < 0)
            {
                ErrorPush();
                UsrFreeUserInfo(pUI);
                return (ErrorPop());
            }

            UsrFreeUserInfo(pUI);

            return (0);
        }

        UsrFreeUserInfo(pUI);

    }


    ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
    return (ERR_SMTP_AUTH_FAILED);

}




static int      SMTPCheckUsrPwdAuth(SVRCFG_HANDLE hSvrConfig, char const * pszUsername,
                        char const * pszPassword, char *pszPerms)
{

    char            szAuthFilePath[SYS_MAX_PATH] = "";

    SMTPGetAuthFilePath(szAuthFilePath);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szAuthFilePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pAuthFile = fopen(szAuthFilePath, "rt");

    if (pAuthFile == NULL)
    {
        RLckUnlockSH(hResLock);
        ErrSetErrorCode(ERR_FILE_OPEN, szAuthFilePath);
        return (ERR_FILE_OPEN);
    }

    char            szAuthLine[SVR_SMTPAUTH_LINE_MAX] = "";

    while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szAuthLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= smtpaMax) &&
                (strcmp(ppszStrings[smtpaUsername], pszUsername) == 0) &&
                (strcmp(ppszStrings[smtpaPassword], pszPassword) == 0))
        {
            if (pszPerms != NULL)
                strcpy(pszPerms, ppszStrings[smtpaPerms]);

            StrFreeStrings(ppszStrings);
            fclose(pAuthFile);
            RLckUnlockSH(hResLock);

            return (0);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pAuthFile);

    RLckUnlockSH(hResLock);

    ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
    return (ERR_SMTP_AUTH_FAILED);

}



static int      SMTPCheckCramMD5Auth(char const * pszChallenge, char const * pszUsername,
                        char const * pszDigest, char *pszPerms)
{

    char            szAuthFilePath[SYS_MAX_PATH] = "";

    SMTPGetAuthFilePath(szAuthFilePath);


    FILE           *pAuthFile = fopen(szAuthFilePath, "rt");

    if (pAuthFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, szAuthFilePath);
        return (ERR_FILE_OPEN);
    }

    char            szAuthLine[SVR_SMTPAUTH_LINE_MAX] = "";

    while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szAuthLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= smtpaMax) &&
                (strcmp(ppszStrings[smtpaUsername], pszUsername) == 0))
        {
            char            szCurrDigest[512] = "";

///////////////////////////////////////////////////////////////////////////////
//  Compute MD5 response ( secret , challenge , digest )
///////////////////////////////////////////////////////////////////////////////
            if (MscCramMD5(ppszStrings[smtpaPassword], pszChallenge, szCurrDigest) < 0)
            {
                StrFreeStrings(ppszStrings);
                fclose(pAuthFile);

                return (ErrGetErrorCode());
            }

            if (stricmp(szCurrDigest, pszDigest) == 0)
            {
                if (pszPerms != NULL)
                    strcpy(pszPerms, ppszStrings[smtpaPerms]);

                StrFreeStrings(ppszStrings);
                fclose(pAuthFile);

                return (0);
            }
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pAuthFile);


    ErrSetErrorCode(ERR_SMTP_AUTH_FAILED);
    return (ERR_SMTP_AUTH_FAILED);

}



static int      SMTPDoAuthCramMD5(BSOCK_HANDLE hBSock, SMTPSession & SMTPS,
                        char const * pszAuthParam)
{
///////////////////////////////////////////////////////////////////////////////
//  Emit encoded64 challenge and get client response
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uEnc64Length = 0;
    char            szChallenge[512] = "";

    encode64(SMTPS.szTimeStamp, strlen(SMTPS.szTimeStamp), szChallenge,
            sizeof(szChallenge), &uEnc64Length);

    if ((BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout, "334 %s", szChallenge) < 0) ||
            (BSckGetString(hBSock, szChallenge, sizeof(szChallenge) - 1, SMTPS.pSMTPCfg->iTimeout) == NULL))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Decode ( base64 ) client response
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uDec64Length = 0;
    char            szClientResp[512] = "";

    if (decode64(szChallenge, strlen(szChallenge), szClientResp, &uDec64Length) != 0)
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

///////////////////////////////////////////////////////////////////////////////
//  Extract the username and client digest
///////////////////////////////////////////////////////////////////////////////
    char           *pszUsername = szClientResp,
                   *pszDigest = strchr(szClientResp, ' ');

    if (pszDigest == NULL)
    {
        BSckSendString(hBSock, "501 Syntax error in parameters or arguments",
                SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_BAD_SMTP_CMD_SYNTAX);
        return (ERR_BAD_SMTP_CMD_SYNTAX);
    }

    *pszDigest++ = '\0';

///////////////////////////////////////////////////////////////////////////////
//  Validate client response
///////////////////////////////////////////////////////////////////////////////
    char            szPerms[128] = "";

    if ((SMTPCheckLocalCramMD5Auth(SMTPS.hSvrConfig, SMTPS.szTimeStamp, pszUsername,
                            pszDigest, szPerms) < 0) &&
            (SMTPCheckCramMD5Auth(SMTPS.szTimeStamp, pszUsername, pszDigest, szPerms) < 0))
    {
        ErrorPush();

        BSckSendString(hBSock, "503 Authentication failed", SMTPS.pSMTPCfg->iTimeout);

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Apply user perms to SMTP config
///////////////////////////////////////////////////////////////////////////////
    SMTPApplyPerms(SMTPS, szPerms);

///////////////////////////////////////////////////////////////////////////////
//  Set the logon user
///////////////////////////////////////////////////////////////////////////////
    StrSNCpy(SMTPS.szLogonUser, pszUsername);


    SMTPS.ulFlags |= SMTPF_AUTHENTICATED;
    SMTPS.iSMTPState = stateAuthenticated;

    BSckSendString(hBSock, "235 Authentication successful", SMTPS.pSMTPCfg->iTimeout);

    return (0);

}



static int      SMTPHandleCmd_AUTH(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    if (SMTPS.iSMTPState != stateHelo)
    {
        SMTPResetSession(SMTPS);

        BSckSendString(hBSock, "503 Bad sequence of commands", SMTPS.pSMTPCfg->iTimeout);

        ErrSetErrorCode(ERR_SMTP_BAD_CMD_SEQUENCE);
        return (ERR_SMTP_BAD_CMD_SEQUENCE);
    }


    int             iTokensCount;
    char          **ppszTokens = StrTokenize(pszCommand, " ");

    if ((ppszTokens == NULL) || ((iTokensCount = StrStringsCount(ppszTokens)) < 2))
    {
        if (ppszTokens != NULL)
            StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "501 Syntax error in parameters or arguments", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }

///////////////////////////////////////////////////////////////////////////////
//  Decode AUTH command params
///////////////////////////////////////////////////////////////////////////////
    char            szAuthType[128] = "",
                    szAuthParam[PLAIN_AUTH_PARAM_SIZE] = "";

    StrSNCpy(szAuthType, ppszTokens[1]);

    if (iTokensCount > 2)
        StrSNCpy(szAuthParam, ppszTokens[2]);

    StrFreeStrings(ppszTokens);

///////////////////////////////////////////////////////////////////////////////
//  Handle authentication methods
///////////////////////////////////////////////////////////////////////////////
    if (stricmp(szAuthType, "plain") == 0)
    {

        if (SMTPDoAuthPlain(hBSock, SMTPS, szAuthParam) < 0)
        {
            ErrorPush();

            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=PLAIN", 0);

            return (ErrorPop());
        }

    }
    else if (stricmp(szAuthType, "login") == 0)
    {

        if (SMTPDoAuthLogin(hBSock, SMTPS, szAuthParam) < 0)
        {
            ErrorPush();

            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=LOGIN", 0);

            return (ErrorPop());
        }

    }
    else if (stricmp(szAuthType, "cram-md5") == 0)
    {

        if (SMTPDoAuthCramMD5(hBSock, SMTPS, szAuthParam) < 0)
        {
            ErrorPush();

            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=CRAM-MD5", 0);

            return (ErrorPop());
        }

    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Handle external authentication methods
///////////////////////////////////////////////////////////////////////////////
        if (SMTPDoAuthExternal(hBSock, SMTPS, szAuthType) < 0)
        {
            ErrorPush();

            if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
                SMTPLogSession(SMTPS, "", "", "AUTH=EFAIL:TYPE=EXTRN", 0);

            return (ErrorPop());
        }

    }


    return (0);

}



static int      SMTPSendMultilineResponse(BSOCK_HANDLE hBSock, int iTimeout, FILE * pRespFile)
{

    rewind(pRespFile);

    char            szCurrLine[1024] = "",
                    szPrevLine[1024] = "";

    if (MscGetString(pRespFile, szPrevLine, sizeof(szPrevLine) - 1) != NULL)
    {
        while (MscGetString(pRespFile, szCurrLine, sizeof(szCurrLine) - 1) != NULL)
        {
            szPrevLine[3] = '-';

            if (BSckSendString(hBSock, szPrevLine, iTimeout) < 0)
                return (ErrGetErrorCode());

            strcpy(szPrevLine, szCurrLine);
        }

        if (BSckSendString(hBSock, szPrevLine, iTimeout) < 0)
            return (ErrGetErrorCode());
    }

    return (0);

}



static int      SMTPHandleCmd_RSET(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    SMTPResetSession(SMTPS);

    BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

    return (0);

}



static int      SMTPHandleCmd_NOOP(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    BSckSendString(hBSock, "250 OK", SMTPS.pSMTPCfg->iTimeout);

    return (0);

}



static int      SMTPHandleCmd_QUIT(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

    SMTPS.iSMTPState = stateExit;


    BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
            "221 %s service closing transmission channel", SMTP_SERVER_NAME);

    return (0);

}




static int      SMTPHandleCmd_VRFY(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

///////////////////////////////////////////////////////////////////////////////
//  Check if VRFY is enabled
///////////////////////////////////////////////////////////////////////////////
    if (((SMTPS.ulFlags & SMTPF_VRFY_ENABLED) == 0) &&
            !SvrTestConfigFlag("AllowSmtpVRFY", false, SMTPS.hSvrConfig))
    {
        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, "", "", "VRFY=EACCESS", 0);

        BSckSendString(hBSock, "501 Command not accepted", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }


    char          **ppszTokens = StrTokenize(pszCommand, " ");

    if ((ppszTokens == NULL) || (StrStringsCount(ppszTokens) != 2))
    {
        if (ppszTokens != NULL)
            StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "501 Syntax error in parameters or arguments", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }


    char            szVrfyUser[MAX_ADDR_NAME] = "",
                    szVrfyDomain[MAX_ADDR_NAME] = "";

    if (USmtpSplitEmailAddr(ppszTokens[1], szVrfyUser, szVrfyDomain) < 0)
    {
        ErrorPush();
        StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "501 Syntax error in parameters or arguments", SMTPS.pSMTPCfg->iTimeout);
        return (ErrorPop());
    }

    StrFreeStrings(ppszTokens);


    UserInfo       *pUI = UsrGetUserByNameOrAlias(szVrfyDomain, szVrfyUser);

    if (pUI != NULL)
    {
        char           *pszRealName = UsrGetUserInfoVar(pUI, "RealName", "Unknown");


        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "250 %s <%s@%s>", pszRealName, pUI->pszName, pUI->pszDomain);


        SysFree(pszRealName);

        UsrFreeUserInfo(pUI);
    }
    else
    {
        if (USmlIsCmdAliasAccount(szVrfyDomain, szVrfyUser) < 0)
        {
            BSckSendString(hBSock, "550 String does not match anything", SMTPS.pSMTPCfg->iTimeout);

            ErrSetErrorCode(ERR_USER_NOT_LOCAL);
            return (ERR_USER_NOT_LOCAL);
        }

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "250 Local account <%s@%s>", szVrfyUser, szVrfyDomain);

    }

    return (0);

}




static int      SMTPHandleCmd_ETRN(const char *pszCommand, BSOCK_HANDLE hBSock,
                        SMTPSession & SMTPS)
{

///////////////////////////////////////////////////////////////////////////////
//  Check if ETRN is enabled
///////////////////////////////////////////////////////////////////////////////
    if (((SMTPS.ulFlags & SMTPF_ETRN_ENABLED) == 0) &&
            !SvrTestConfigFlag("AllowSmtpETRN", false, SMTPS.hSvrConfig))
    {
        if (SMTPLogEnabled(SMTPS.hShbSMTP, SMTPS.pSMTPCfg))
            SMTPLogSession(SMTPS, "", "", "ETRN=EACCESS", 0);

        BSckSendString(hBSock, "501 Command not accepted", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }


    char          **ppszTokens = StrTokenize(pszCommand, " ");

    if ((ppszTokens == NULL) || (StrStringsCount(ppszTokens) != 2))
    {
        if (ppszTokens != NULL)
            StrFreeStrings(ppszTokens);

        BSckSendString(hBSock, "501 Syntax error in parameters or arguments", SMTPS.pSMTPCfg->iTimeout);
        return (-1);
    }

///////////////////////////////////////////////////////////////////////////////
//  Do a matched flush of the rsnd arena
///////////////////////////////////////////////////////////////////////////////
    if (QueFlushRsndArena(hSpoolQueue, ppszTokens[1]) < 0)
    {
        ErrorPush();
        StrFreeStrings(ppszTokens);

        BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
                "451 Requested action aborted: (%d) local error in processing",
                ErrorFetch());

        return (ErrorPop());
    }


    BSckVSendString(hBSock, SMTPS.pSMTPCfg->iTimeout,
            "250 Queueing for '%s' has been started", ppszTokens[1]);


    StrFreeStrings(ppszTokens);

    return (0);

}
