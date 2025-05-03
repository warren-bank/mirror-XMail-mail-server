/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,...,2002  Davide Libenzi
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
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "UsrAuth.h"
#include "SvrUtils.h"
#include "MiscUtils.h"
#include "DNS.h"
#include "DNSCache.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "SMTPSvr.h"
#include "SMTPUtils.h"
#include "Base64Enc.h"
#include "MD5.h"
#include "MailSvr.h"







#define STD_SMTP_TIMEOUT        STD_SERVER_TIMEOUT
#define SMTPGW_LINE_MAX         1024
#define SMTPGW_TABLE_FILE       "smtpgw.tab"
#define SMTPFWD_LINE_MAX        1024
#define SMTPFWD_TABLE_FILE      "smtpfwd.tab"
#define SMTPRELAY_LINE_MAX      512
#define SMTP_RELAY_FILE         "smtprelay.tab"
#define MAX_MX_RECORDS          32
#define SMTP_SPAMMERS_FILE      "spammers.tab"
#define SMTP_SPAM_ADDRESS_FILE  "spam-address.tab"
#define SPAMMERS_LINE_MAX       512
#define SPAM_ADDRESS_LINE_MAX   512
#define SMTPAUTH_LINE_MAX       512
#define SMTP_EXTAUTH_TIMEOUT    60
#define SMTP_EXTAUTH_PRIORITY   SYS_PRIORITY_NORMAL
#define SMTP_EXTAUTH_SUCCESS    0
#define DEFAULT_SMTP_ERR        "417 Temporary delivery error"

#define SMTPCH_SUPPORT_SIZE     (1 << 0)









enum SmtpGwFileds
{
    gwDomain = 0,
    gwGateway,

    gwMax
};

enum SmtpFwdFileds
{
    fwdDomain = 0,
    fwdGateway,

    fwdMax
};

enum SmtpRelayFileds
{
    rlyFromIP = 0,
    rlyFromMask,

    rlyMax
};

enum SpammerFileds
{
    spmFromIP = 0,
    spmFromMask,

    spmMax
};

struct SmtpMXRecords
{
    int             iNumMXRecords;
    int             iMXCost[MAX_MX_RECORDS];
    char           *pszMXName[MAX_MX_RECORDS];
    int             iCurrMxCost;
};

struct SmtpChannel
{
    BSOCK_HANDLE    hBSock;
    unsigned long   ulFlags;
    unsigned long   ulMaxMsgSize;

};





static int      USmtpWriteGateway(FILE * pGwFile, const char *pszDomain, const char *pszGateway);
static char    *USmtpGetGwTableFilePath(char *pszGwFilePath, int iMaxPath);
static char    *USmtpGetFwdTableFilePath(char *pszFwdFilePath, int iMaxPath);
static char    *USmtpGetRelayFilePath(char *pszRelayFilePath, int iMaxPath);
static int      USmtpSetError(SMTPError * pSMTPE, int iSTMPResponse, char const * pszSTMPResponse);
static int      USmtpResponseClass(int iResponseCode, int iResponseClass);
static int      USmtpGetResultCode(const char *pszResult);
static int      USmtpIsPartialResponse(char const * pszResponse);
static int      USmtpGetResponse(BSOCK_HANDLE hBSock, char *pszResponse, int iMaxResponse,
                        int iTimeout = STD_SMTP_TIMEOUT);
static int      USmtpSendCommand(BSOCK_HANDLE hBSock, const char *pszCommand,
                        char *pszResponse, int iMaxResponse, int iTimeout = STD_SMTP_TIMEOUT);
static int      USmtpGetServerAuthFile(char const * pszServer, char *pszAuthFilePath);
static int      USmtpDoPlainAuth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char const * const * ppszAuthTokens, SMTPError * pSMTPE);
static int      USmtpDoLoginAuth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char const * const * ppszAuthTokens, SMTPError * pSMTPE);
static int      USmtpDoCramMD5Auth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char const * const * ppszAuthTokens, SMTPError * pSMTPE);
static int      USmtpExternalAuthSubstitute(char **ppszAuthTokens, char const * pszChallenge,
                        char const * pszSecret, char const * pszRespFile);
static int      USmtpDoExternAuth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char **ppszAuthTokens, SMTPError * pSMTPE);
static int      USmtpServerAuthenticate(SmtpChannel * pSmtpCh, char const * pszServer,
                        SMTPError * pSMTPE);
static int      USmtpParseEhloResponse(SmtpChannel * pSmtpCh, char const * pszResponse);
static int      USmtpGetDomainMX(SVRCFG_HANDLE hSvrConfig, const char *pszDomain,
                        char *&pszMXDomains);
static char    *USmtpGetSpammersFilePath(char *pszSpamFilePath, int iMaxPath);
static char    *USmtpGetSpamAddrFilePath(char *pszSpamFilePath, int iMaxPath);










static char    *USmtpGetGwTableFilePath(char *pszGwFilePath, int iMaxPath)
{

    CfgGetRootPath(pszGwFilePath, iMaxPath);

    StrNCat(pszGwFilePath, SMTPGW_TABLE_FILE, iMaxPath);

    return (pszGwFilePath);

}



static char    *USmtpGetFwdTableFilePath(char *pszFwdFilePath, int iMaxPath)
{

    CfgGetRootPath(pszFwdFilePath, iMaxPath);

    StrNCat(pszFwdFilePath, SMTPFWD_TABLE_FILE, iMaxPath);

    return (pszFwdFilePath);

}



char          **USmtpGetFwdGateways(SVRCFG_HANDLE hSvrConfig, const char *pszDomain)
{

    char            szFwdFilePath[SYS_MAX_PATH] = "";

    USmtpGetFwdTableFilePath(szFwdFilePath, sizeof(szFwdFilePath));


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szFwdFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (NULL);


    FILE           *pFwdFile = fopen(szFwdFilePath, "rt");

    if (pFwdFile == NULL)
    {
        RLckUnlockSH(hResLock);

        ErrSetErrorCode(ERR_SMTPFWD_FILE_NOT_FOUND);
        return (NULL);
    }

    char            szFwdLine[SMTPFWD_LINE_MAX] = "";

    while (MscGetConfigLine(szFwdLine, sizeof(szFwdLine) - 1, pFwdFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szFwdLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= fwdMax) && StrIWildMatch(pszDomain, ppszStrings[fwdDomain]))
        {
            char          **ppszFwdGws = NULL;

            if (ppszStrings[fwdGateway][0] == '#')
            {
                if ((ppszFwdGws = StrTokenize(ppszStrings[fwdGateway] + 1, ",")) != NULL)
                {
                    int             iGwCount = StrStringsCount(ppszFwdGws);

                    srand((unsigned int) time(NULL));

                    for (int ii = 0; ii < (iGwCount / 2); ii++)
                    {
                        int             iSwap1 = rand() % iGwCount,
                                        iSwap2 = rand() % iGwCount;
                        char           *pszGw1 = ppszFwdGws[iSwap1],
                                       *pszGw2 = ppszFwdGws[iSwap2];

                        ppszFwdGws[iSwap1] = pszGw2;
                        ppszFwdGws[iSwap2] = pszGw1;
                    }
                }
            }
            else
                ppszFwdGws = StrTokenize(ppszStrings[fwdGateway], ",");

            StrFreeStrings(ppszStrings);
            fclose(pFwdFile);

            RLckUnlockSH(hResLock);

            return (ppszFwdGws);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pFwdFile);

    RLckUnlockSH(hResLock);

    ErrSetErrorCode(ERR_SMTPFWD_NOT_FOUND);
    return (NULL);

}



static char    *USmtpGetRelayFilePath(char *pszRelayFilePath, int iMaxPath)
{

    CfgGetRootPath(pszRelayFilePath, iMaxPath);

    StrNCat(pszRelayFilePath, SMTP_RELAY_FILE, iMaxPath);

    return (pszRelayFilePath);

}



int             USmtpGetGateway(SVRCFG_HANDLE hSvrConfig, const char *pszDomain,
                        char *pszGateway)
{

    char            szGwFilePath[SYS_MAX_PATH] = "";

    USmtpGetGwTableFilePath(szGwFilePath, sizeof(szGwFilePath));


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szGwFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pGwFile = fopen(szGwFilePath, "rt");

    if (pGwFile == NULL)
    {
        RLckUnlockSH(hResLock);

        ErrSetErrorCode(ERR_SMTPGW_FILE_NOT_FOUND);
        return (ERR_SMTPGW_FILE_NOT_FOUND);
    }

    char            szGwLine[SMTPGW_LINE_MAX] = "";

    while (MscGetConfigLine(szGwLine, sizeof(szGwLine) - 1, pGwFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szGwLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= gwMax) && StrIWildMatch(pszDomain, ppszStrings[gwDomain]))
        {
            strcpy(pszGateway, ppszStrings[gwGateway]);

            StrFreeStrings(ppszStrings);
            fclose(pGwFile);

            RLckUnlockSH(hResLock);

            return (0);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pGwFile);

    RLckUnlockSH(hResLock);

    ErrSetErrorCode(ERR_SMTPGW_NOT_FOUND);
    return (ERR_SMTPGW_NOT_FOUND);

}



static int      USmtpWriteGateway(FILE * pGwFile, const char *pszDomain, const char *pszGateway)
{

///////////////////////////////////////////////////////////////////////////////
//  Domain
///////////////////////////////////////////////////////////////////////////////
    char           *pszQuoted = StrQuote(pszDomain, '"');

    if (pszQuoted == NULL)
        return (ErrGetErrorCode());

    fprintf(pGwFile, "%s\t", pszQuoted);

    SysFree(pszQuoted);

///////////////////////////////////////////////////////////////////////////////
//  Gateway
///////////////////////////////////////////////////////////////////////////////
    pszQuoted = StrQuote(pszGateway, '"');

    if (pszQuoted == NULL)
        return (ErrGetErrorCode());

    fprintf(pGwFile, "%s\n", pszQuoted);

    SysFree(pszQuoted);

    return (0);

}



int             USmtpAddGateway(const char *pszDomain, const char *pszGateway)
{

    char            szGwFilePath[SYS_MAX_PATH] = "";

    USmtpGetGwTableFilePath(szGwFilePath, sizeof(szGwFilePath));


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szGwFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pGwFile = fopen(szGwFilePath, "r+t");

    if (pGwFile == NULL)
    {
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_SMTPGW_FILE_NOT_FOUND);
        return (ERR_SMTPGW_FILE_NOT_FOUND);
    }

    char            szGwLine[SMTPGW_LINE_MAX] = "";

    while (MscGetConfigLine(szGwLine, sizeof(szGwLine) - 1, pGwFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szGwLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= gwMax) && (stricmp(pszDomain, ppszStrings[gwDomain]) == 0) &&
                (stricmp(pszGateway, ppszStrings[gwGateway]) == 0))
        {
            StrFreeStrings(ppszStrings);
            fclose(pGwFile);
            RLckUnlockEX(hResLock);

            ErrSetErrorCode(ERR_GATEWAY_ALREADY_EXIST);
            return (ERR_GATEWAY_ALREADY_EXIST);
        }

        StrFreeStrings(ppszStrings);
    }

    fseek(pGwFile, 0, SEEK_END);


    if (USmtpWriteGateway(pGwFile, pszDomain, pszGateway) < 0)
    {
        fclose(pGwFile);
        RLckUnlockEX(hResLock);
        return (ErrGetErrorCode());
    }


    fclose(pGwFile);

    RLckUnlockEX(hResLock);

    return (0);

}



int             USmtpRemoveGateway(const char *pszDomain)
{

    char            szGwFilePath[SYS_MAX_PATH] = "";

    USmtpGetGwTableFilePath(szGwFilePath, sizeof(szGwFilePath));


    char            szTmpFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szTmpFile);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szGwFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pGwFile = fopen(szGwFilePath, "rt");

    if (pGwFile == NULL)
    {
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_SMTPGW_FILE_NOT_FOUND);
        return (ERR_SMTPGW_FILE_NOT_FOUND);
    }

    FILE           *pTmpFile = fopen(szTmpFile, "wt");

    if (pTmpFile == NULL)
    {
        fclose(pGwFile);
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    int             iGatewayFound = 0;
    char            szGwLine[SMTPGW_LINE_MAX] = "";

    while (MscGetConfigLine(szGwLine, sizeof(szGwLine) - 1, pGwFile, false) != NULL)
    {
        if (szGwLine[0] == TAB_COMMENT_CHAR)
        {
            fprintf(pTmpFile, "%s\n", szGwLine);
            continue;
        }


        char          **ppszStrings = StrGetTabLineStrings(szGwLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= gwMax) && (stricmp(pszDomain, ppszStrings[gwDomain]) == 0))
        {

            ++iGatewayFound;

        }
        else
            fprintf(pTmpFile, "%s\n", szGwLine);

        StrFreeStrings(ppszStrings);
    }

    fclose(pGwFile);
    fclose(pTmpFile);


    if (iGatewayFound == 0)
    {
        SysRemove(szTmpFile);
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_GATEWAY_NOT_FOUND);
        return (ERR_GATEWAY_NOT_FOUND);
    }

    char            szTmpGwFilePath[SYS_MAX_PATH] = "";

    sprintf(szTmpGwFilePath, "%s.tmp", szGwFilePath);

    if (MscMoveFile(szGwFilePath, szTmpGwFilePath) < 0)
    {
        ErrorPush();
        RLckUnlockEX(hResLock);
        return (ErrorPop());
    }

    if (MscMoveFile(szTmpFile, szGwFilePath) < 0)
    {
        ErrorPush();
        MscMoveFile(szTmpGwFilePath, szGwFilePath);
        RLckUnlockEX(hResLock);
        return (ErrorPop());
    }

    SysRemove(szTmpGwFilePath);


    RLckUnlockEX(hResLock);

    return (0);

}



int             USmtpGetSpoolFileInfo(char const * pszPkgFile, char *pszDomain, char *pszSmtpMessageID,
                        char *pszFrom, char *pszRcpt)
{

    FILE           *pPkgFile = fopen(pszPkgFile, "rb");

    if (pPkgFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszPkgFile);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read SMTP domain ( 1st row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolLine[2048] = "";

    if (MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL)
    {
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

    if (pszDomain != NULL)
        StrNCpy(pszDomain, szSpoolLine, MAX_HOST_NAME);

///////////////////////////////////////////////////////////////////////////////
//  Read message ID ( 2nd row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    char            szMessageID[128] = "";

    if (MscGetString(pPkgFile, szMessageID, sizeof(szMessageID) - 1) == NULL)
    {
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

    if (pszSmtpMessageID != NULL)
        strcpy(pszSmtpMessageID, szMessageID);

///////////////////////////////////////////////////////////////////////////////
//  Read "MAIL FROM:" ( 3th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (StrINComp(szSpoolLine, MAIL_FROM_STR) != 0))
    {
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

    if (pszFrom != NULL)
    {
        char          **ppszFrom = USmtpGetPathStrings(szSpoolLine);

        if (ppszFrom == NULL)
        {
            ErrorPush();
            fclose(pPkgFile);
            return (ErrorPop());
        }

        int             iFromDomains = StrStringsCount(ppszFrom);

        if (iFromDomains > 0)
            StrNCpy(pszFrom, ppszFrom[iFromDomains - 1], MAX_ADDR_NAME);
        else
            SetEmptyString(pszFrom);

        StrFreeStrings(ppszFrom);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read "RCPT TO:" ( 4th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pPkgFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (StrINComp(szSpoolLine, RCPT_TO_STR) != 0))
    {
        fclose(pPkgFile);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (ERR_INVALID_SPOOL_FILE);
    }

    if (pszRcpt != NULL)
    {
        char          **ppszRcpt = USmtpGetPathStrings(szSpoolLine);

        if (ppszRcpt == NULL)
        {
            ErrorPush();
            fclose(pPkgFile);
            return (ErrorPop());
        }

        int             iRcptDomains = StrStringsCount(ppszRcpt);

        StrNCpy(pszRcpt, ppszRcpt[iRcptDomains - 1], MAX_ADDR_NAME);

        StrFreeStrings(ppszRcpt);
    }


    fclose(pPkgFile);

    return (0);

}



int             USmtpIsAllowedRelay(const SYS_INET_ADDR & PeerInfo,
                        SVRCFG_HANDLE hSvrConfig)
{

    char            szRelayFilePath[SYS_MAX_PATH] = "";

    USmtpGetRelayFilePath(szRelayFilePath, sizeof(szRelayFilePath));


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szRelayFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pRelayFile = fopen(szRelayFilePath, "rt");

    if (pRelayFile == NULL)
    {
        RLckUnlockSH(hResLock);

        ErrSetErrorCode(ERR_SMTPRELAY_FILE_NOT_FOUND);
        return (ERR_SMTPRELAY_FILE_NOT_FOUND);
    }


    NET_ADDRESS     TestAddr = SysGetAddrAddress(PeerInfo);


    char            szRelayLine[SMTPRELAY_LINE_MAX] = "";

    while (MscGetConfigLine(szRelayLine, sizeof(szRelayLine) - 1, pRelayFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szRelayLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);
        AddressFilter   AF;

        if ((iFieldsCount > 0) &&
                (MscLoadAddressFilter(ppszStrings, iFieldsCount, AF) == 0) &&
                MscAddressMatch(AF, TestAddr))
        {
            StrFreeStrings(ppszStrings);
            fclose(pRelayFile);
            RLckUnlockSH(hResLock);

            return (0);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pRelayFile);

    RLckUnlockSH(hResLock);


    ErrSetErrorCode(ERR_RELAY_NOT_ALLOWED);

    return (ERR_RELAY_NOT_ALLOWED);

}



char          **USmtpGetPathStrings(const char *pszMailCmd)
{

    const char     *pszOpen = strchr(pszMailCmd, '<'),
                   *pszClose = strchr(pszMailCmd, '>');

    if ((pszOpen == NULL) || (pszClose == NULL))
    {
        ErrSetErrorCode(ERR_SMTP_PATH_PARSE_ERROR);
        return (NULL);
    }

    int             iPathLength = (int) (pszClose - pszOpen) - 1;

    if ((iPathLength < 0) || (iPathLength >= MAX_SMTP_ADDRESS))
    {
        ErrSetErrorCode(ERR_SMTP_PATH_PARSE_ERROR, pszMailCmd);
        return (NULL);
    }

    char           *pszPath = (char *) SysAlloc(iPathLength + 1);

    if (pszPath == NULL)
        return (NULL);

    strncpy(pszPath, pszOpen + 1, iPathLength);
    pszPath[iPathLength] = '\0';


    char          **ppszDomains = StrTokenize(pszPath, ",:");


    SysFree(pszPath);

    return (ppszDomains);

}



int             USmtpSplitEmailAddr(const char *pszAddr, char *pszUser, char *pszDomain)
{

    const char     *pszAT = strchr(pszAddr, '@');

    if (pszAT == NULL)
    {
        ErrSetErrorCode(ERR_BAD_EMAIL_ADDR);
        return (ERR_BAD_EMAIL_ADDR);
    }

    if (pszUser != NULL)
    {
        int             iUserLength = (int) (pszAT - pszAddr);

        iUserLength = Min(iUserLength, MAX_ADDR_NAME - 1);

        strncpy(pszUser, pszAddr, iUserLength);
        pszUser[iUserLength] = '\0';
    }

    if (pszDomain != NULL)
        StrNCpy(pszDomain, pszAT + 1, MAX_ADDR_NAME);

    return (0);

}



int             USmtpInitError(SMTPError * pSMTPE)
{

    ZeroData(*pSMTPE);
    pSMTPE->iSTMPResponse = 0;
    pSMTPE->pszSTMPResponse = NULL;

    return (0);

}



static int      USmtpSetError(SMTPError * pSMTPE, int iSTMPResponse, char const * pszSTMPResponse)
{

    pSMTPE->iSTMPResponse = iSTMPResponse;

    if (pSMTPE->pszSTMPResponse != NULL)
        SysFree(pSMTPE->pszSTMPResponse);

    pSMTPE->pszSTMPResponse = SysStrDup(pszSTMPResponse);

    return (0);

}



bool            USmtpIsFatalError(SMTPError const * pSMTPE)
{

    return ((pSMTPE->iSTMPResponse == SMTP_FATAL_ERROR) ||
            ((pSMTPE->iSTMPResponse >= 500) && (pSMTPE->iSTMPResponse < 600)));

}



char const     *USmtpGetErrorMessage(SMTPError const * pSMTPE)
{

    return ((pSMTPE->pszSTMPResponse != NULL) ? pSMTPE->pszSTMPResponse : "");

}



int             USmtpCleanupError(SMTPError * pSMTPE)
{

    if (pSMTPE->pszSTMPResponse != NULL)
        SysFree(pSMTPE->pszSTMPResponse);

    USmtpInitError(pSMTPE);

    return (0);

}



char           *USmtpGetSMTPError(SMTPError * pSMTPE, char * pszError, int iMaxError)
{

    char const     *pszSmtpErr = (pSMTPE != NULL) ? USmtpGetErrorMessage(pSMTPE): DEFAULT_SMTP_ERR;

    if (IsEmptyString(pszSmtpErr))
        pszSmtpErr = DEFAULT_SMTP_ERR;

    StrNCpy(pszError, pszSmtpErr, iMaxError);

    return (pszError);

}



static int      USmtpResponseClass(int iResponseCode, int iResponseClass)
{

    return (((iResponseCode >= iResponseClass) &&
                    (iResponseCode < (iResponseClass + 100))) ? 1 : 0);

}



static int      USmtpGetResultCode(const char *pszResult)
{

    int             ii;
    char            szResCode[64] = "";

    for (ii = 0; isdigit(pszResult[ii]); ii++)
        szResCode[ii] = pszResult[ii];

    szResCode[ii] = '\0';

    if (ii == 0)
    {
        ErrSetErrorCode(ERR_BAD_SMTP_RESPONSE);
        return (ERR_BAD_SMTP_RESPONSE);
    }

    return (atoi(szResCode));

}



static int      USmtpIsPartialResponse(char const * pszResponse)
{

    return (((strlen(pszResponse) >= 4) && (pszResponse[3] == '-')) ? 1 : 0);

}



static int      USmtpGetResponse(BSOCK_HANDLE hBSock, char *pszResponse, int iMaxResponse,
                        int iTimeout)
{

    int             iResultCode = -1,
                    iResponseLenght = 0;
    char            szPartial[1024] = "";

    do
    {
        int             iLineLength = 0;

        if (BSckGetString(hBSock, szPartial, sizeof(szPartial) - 1, iTimeout,
                &iLineLength) == NULL)
            return (ErrGetErrorCode());

        if (iResponseLenght < iMaxResponse)
        {
            if (iResponseLenght > 0)
                strcat(pszResponse, "\r\n"), iResponseLenght += 2;

            int             iCopyLenght = Min(iMaxResponse - 1 - iResponseLenght, iLineLength);

            if (iCopyLenght > 0)
            {
                strncpy(pszResponse + iResponseLenght, szPartial, iCopyLenght);

                iResponseLenght += iCopyLenght;

                pszResponse[iResponseLenght] = '\0';
            }
        }

        if ((iResultCode = USmtpGetResultCode(szPartial)) < 0)
            return (ErrGetErrorCode());

    } while (USmtpIsPartialResponse(szPartial));

    return (iResultCode);

}



static int      USmtpSendCommand(BSOCK_HANDLE hBSock, const char *pszCommand,
                        char *pszResponse, int iMaxResponse, int iTimeout)
{

    if (BSckSendString(hBSock, pszCommand, iTimeout) <= 0)
        return (ErrGetErrorCode());

    return (USmtpGetResponse(hBSock, pszResponse, iMaxResponse, iTimeout));

}



static int      USmtpGetServerAuthFile(char const * pszServer, char *pszAuthFilePath)
{

    int             iRootedName = MscRootedName(pszServer);
    char            szAuthPath[SYS_MAX_PATH] = "";

    UAthGetRootPath(AUTH_SERVICE_SMTP, szAuthPath, sizeof(szAuthPath));

    char const     *pszDot = pszServer;

    while ((pszDot != NULL) && (strlen(pszDot) > 0))
    {
        if (iRootedName)
            sprintf(pszAuthFilePath, "%s%stab", szAuthPath, pszDot);
        else
            sprintf(pszAuthFilePath, "%s%s.tab", szAuthPath, pszDot);

        if (SysExistFile(pszAuthFilePath))
            return (0);


        if ((pszDot = strchr(pszDot, '.')) != NULL)
            ++pszDot;
    }


    ErrSetErrorCode(ERR_NO_SMTP_AUTH_CONFIG);
    return (ERR_NO_SMTP_AUTH_CONFIG);

}



static int      USmtpDoPlainAuth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char const * const * ppszAuthTokens, SMTPError * pSMTPE)
{

    if (StrStringsCount(ppszAuthTokens) < 3)
    {
        ErrSetErrorCode(ERR_BAD_SMTP_AUTH_CONFIG);
        return (ERR_BAD_SMTP_AUTH_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Build plain text authentication token ( "\0" Username "\0" Password "\0" )
///////////////////////////////////////////////////////////////////////////////
    int             iAuthLength = 1;
    char            szAuthBuffer[2048] = "";

    strcpy(szAuthBuffer + iAuthLength, ppszAuthTokens[1]);
    iAuthLength += strlen(ppszAuthTokens[1]) + 1;

    strcpy(szAuthBuffer + iAuthLength, ppszAuthTokens[2]);
    iAuthLength += strlen(ppszAuthTokens[2]);

    unsigned int    uEnc64Length = 0;
    char            szEnc64Token[1024] = "";

    encode64(szAuthBuffer, iAuthLength, szEnc64Token, sizeof(szEnc64Token), &uEnc64Length);

///////////////////////////////////////////////////////////////////////////////
//  Send AUTH command
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse;

    SysSNPrintf(szAuthBuffer, sizeof(szAuthBuffer) - 1, "AUTH PLAIN %s", szEnc64Token);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }


    return (0);

}



static int      USmtpDoLoginAuth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char const * const * ppszAuthTokens, SMTPError * pSMTPE)
{

    if (StrStringsCount(ppszAuthTokens) < 3)
    {
        ErrSetErrorCode(ERR_BAD_SMTP_AUTH_CONFIG);
        return (ERR_BAD_SMTP_AUTH_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Send AUTH command
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse;
    char            szAuthBuffer[1024] = "";

    sprintf(szAuthBuffer, "AUTH LOGIN");

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 300))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Send username
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uEnc64Length = 0;

    encode64(ppszAuthTokens[1], strlen(ppszAuthTokens[1]), szAuthBuffer,
            sizeof(szAuthBuffer), &uEnc64Length);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 300))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Send password
///////////////////////////////////////////////////////////////////////////////
    encode64(ppszAuthTokens[2], strlen(ppszAuthTokens[2]), szAuthBuffer,
            sizeof(szAuthBuffer), &uEnc64Length);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }


    return (0);

}



static int      USmtpDoCramMD5Auth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char const * const * ppszAuthTokens, SMTPError * pSMTPE)
{

    if (StrStringsCount(ppszAuthTokens) < 3)
    {
        ErrSetErrorCode(ERR_BAD_SMTP_AUTH_CONFIG);
        return (ERR_BAD_SMTP_AUTH_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Send AUTH command
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse;
    char            szAuthBuffer[1024] = "";

    sprintf(szAuthBuffer, "AUTH CRAM-MD5");

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 300) ||
            (strlen(szAuthBuffer) < 4))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Retrieve server challenge
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uDec64Length = 0;
    char           *pszAuth = szAuthBuffer + 4;
    char            szChallenge[1024] = "";

    if (decode64(pszAuth, strlen(pszAuth), szChallenge, &uDec64Length) != 0)
    {
        if (pSMTPE != NULL)
            USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

        ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        return (ERR_BAD_SERVER_RESPONSE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Compute MD5 response ( secret , challenge , digest )
///////////////////////////////////////////////////////////////////////////////
    if (MscCramMD5(ppszAuthTokens[2], szChallenge, szChallenge) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Send response
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uEnc64Length = 0;
    char            szResponse[1024] = "";

    SysSNPrintf(szResponse, sizeof(szResponse) - 1, "%s %s", ppszAuthTokens[1], szChallenge);

    encode64(szResponse, strlen(szResponse), szAuthBuffer,
            sizeof(szAuthBuffer), &uEnc64Length);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }


    return (0);

}



static int      USmtpExternalAuthSubstitute(char **ppszAuthTokens, char const * pszChallenge,
                        char const * pszSecret, char const * pszRespFile)
{

    for (int ii = 0; ppszAuthTokens[ii] != NULL; ii++)
    {
        if (strcmp(ppszAuthTokens[ii], "@@CHALL") == 0)
        {
            char           *pszNewValue = SysStrDup(pszChallenge);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszAuthTokens[ii]);

            ppszAuthTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszAuthTokens[ii], "@@SECRT") == 0)
        {
            char           *pszNewValue = SysStrDup(pszSecret);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszAuthTokens[ii]);

            ppszAuthTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszAuthTokens[ii], "@@RFILE") == 0)
        {
            char           *pszNewValue = SysStrDup(pszRespFile);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszAuthTokens[ii]);

            ppszAuthTokens[ii] = pszNewValue;
        }
    }

    return (0);

}



static int      USmtpDoExternAuth(SmtpChannel * pSmtpCh, char const * pszServer,
                        char **ppszAuthTokens, SMTPError * pSMTPE)
{

    if (StrStringsCount(ppszAuthTokens) < 4)
    {
        ErrSetErrorCode(ERR_BAD_SMTP_AUTH_CONFIG);
        return (ERR_BAD_SMTP_AUTH_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Send AUTH command
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse;
    char            szAuthBuffer[1024] = "";

    SysSNPrintf(szAuthBuffer, sizeof(szAuthBuffer) - 1, "AUTH %s", ppszAuthTokens[1]);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 300))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Retrieve server challenge
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uDec64Length = 0;
    char           *pszAuth = szAuthBuffer + 4;
    char            szChallenge[1024] = "";

    if ((strlen(szAuthBuffer) < 4) ||
            (decode64(pszAuth, strlen(pszAuth), szChallenge, &uDec64Length) != 0))
    {
        if (pSMTPE != NULL)
            USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

        ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        return (ERR_BAD_SERVER_RESPONSE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Create temp filename for module response and do macro substitution
///////////////////////////////////////////////////////////////////////////////
    char            szRespFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szRespFile);

    USmtpExternalAuthSubstitute(ppszAuthTokens, szChallenge, ppszAuthTokens[2], szRespFile);

///////////////////////////////////////////////////////////////////////////////
//  Call external program to compute the response
///////////////////////////////////////////////////////////////////////////////
    int             iExitCode = -1;

    if (SysExec(ppszAuthTokens[3], &ppszAuthTokens[3], SMTP_EXTAUTH_TIMEOUT,
                    SMTP_EXTAUTH_PRIORITY, &iExitCode) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szRespFile);

        return (ErrorPop());
    }

    if (iExitCode != SMTP_EXTAUTH_SUCCESS)
    {
        CheckRemoveFile(szRespFile);

        ErrSetErrorCode(ERR_BAD_EXTRNPRG_EXITCODE);
        return (ERR_BAD_EXTRNPRG_EXITCODE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Load response file
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uRespSize = 0;
    char           *pAuthResp = (char *) MscLoadFile(szRespFile, uRespSize);

    CheckRemoveFile(szRespFile);

    if (pAuthResp == NULL)
        return (ErrGetErrorCode());

    while ((uRespSize > 0) &&
            ((pAuthResp[uRespSize - 1] == '\r') || (pAuthResp[uRespSize - 1] == '\n')))
        --uRespSize;

///////////////////////////////////////////////////////////////////////////////
//  Send response
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uEnc64Length = 0;

    encode64(pAuthResp, uRespSize, szAuthBuffer, sizeof(szAuthBuffer), &uEnc64Length);

    SysFree(pAuthResp);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szAuthBuffer,
            szAuthBuffer, sizeof(szAuthBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szAuthBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szAuthBuffer);
        }

        return (ErrGetErrorCode());
    }


    return (0);

}



static int      USmtpServerAuthenticate(SmtpChannel * pSmtpCh, char const * pszServer,
                        SMTPError * pSMTPE)
{
///////////////////////////////////////////////////////////////////////////////
//  Try to retrieve SMTP authentication config for  "pszServer"
///////////////////////////////////////////////////////////////////////////////
    char            szAuthFilePath[SYS_MAX_PATH] = "";

    if (USmtpGetServerAuthFile(pszServer, szAuthFilePath) < 0)
        return (0);


    FILE           *pAuthFile = fopen(szAuthFilePath, "rt");

    if (pAuthFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, szAuthFilePath);
        return (ERR_FILE_OPEN);
    }

    char            szAuthLine[SMTPAUTH_LINE_MAX] = "";

    while (MscGetConfigLine(szAuthLine, sizeof(szAuthLine) - 1, pAuthFile) != NULL)
    {
        char          **ppszTokens = StrGetTabLineStrings(szAuthLine);

        if (ppszTokens == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszTokens);

        if (iFieldsCount > 0)
        {
            int             iAuthResult = 0;

            if (stricmp(ppszTokens[0], "plain") == 0)
                iAuthResult = USmtpDoPlainAuth(pSmtpCh, pszServer, ppszTokens, pSMTPE);
            else if (stricmp(ppszTokens[0], "login") == 0)
                iAuthResult = USmtpDoLoginAuth(pSmtpCh, pszServer, ppszTokens, pSMTPE);
            else if (stricmp(ppszTokens[0], "cram-md5") == 0)
                iAuthResult = USmtpDoCramMD5Auth(pSmtpCh, pszServer, ppszTokens, pSMTPE);
            else if (stricmp(ppszTokens[0], "external") == 0)
                iAuthResult = USmtpDoExternAuth(pSmtpCh, pszServer, ppszTokens, pSMTPE);
            else
                ErrSetErrorCode(iAuthResult = ERR_UNKNOWN_SMTP_AUTH, ppszTokens[0]);


            StrFreeStrings(ppszTokens);
            fclose(pAuthFile);

            return (iAuthResult);
        }

        StrFreeStrings(ppszTokens);
    }

    fclose(pAuthFile);

    return (0);

}



static int      USmtpParseEhloResponse(SmtpChannel * pSmtpCh, char const * pszResponse)
{

    char const     *pszLine = pszResponse;

    for (; pszLine != NULL; pszLine = strchr(pszLine, '\n'))
    {
        if (*pszLine == '\n')
            ++pszLine;

///////////////////////////////////////////////////////////////////////////////
//  Skip SMTP code and ' ' or '-'
///////////////////////////////////////////////////////////////////////////////
        if (strlen(pszLine) < 4)
            continue;

        pszLine += 4;

///////////////////////////////////////////////////////////////////////////////
//  SIZE suport detection
///////////////////////////////////////////////////////////////////////////////
        if ((strnicmp(pszLine, "SIZE", CStringSize("SIZE")) == 0) &&
                (strchr(" \r\n", pszLine[CStringSize("SIZE")]) != NULL))
        {
            pSmtpCh->ulFlags |= SMTPCH_SUPPORT_SIZE;

            if ((pszLine[CStringSize("SIZE")] == ' ') &&
                    isdigit(pszLine[CStringSize("SIZE") + 1]))
                pSmtpCh->ulMaxMsgSize = (unsigned long) atol(pszLine + CStringSize("SIZE") + 1);


            continue;
        }


    }


    return (0);

}



SMTPCH_HANDLE   USmtpCreateChannel(const char *pszServer, const char *pszDomain,
                        SMTPError * pSMTPE)
{
///////////////////////////////////////////////////////////////////////////////
//  Decode server address
///////////////////////////////////////////////////////////////////////////////
    int             iPortNo = STD_SMTP_PORT;
    char            szAddress[MAX_ADDR_NAME] = "";

    if (MscSplitAddressPort(pszServer, szAddress, iPortNo, STD_SMTP_PORT) < 0)
        return (INVALID_SMTPCH_HANDLE);


    NET_ADDRESS     NetAddr;

    if (MscGetServerAddress(szAddress, NetAddr) < 0)
        return (INVALID_SMTPCH_HANDLE);


    SYS_SOCKET      SockFD = SysCreateSocket(AF_INET, SOCK_STREAM, 0);

    if (SockFD == SYS_INVALID_SOCKET)
        return (INVALID_SMTPCH_HANDLE);

    SYS_INET_ADDR   SvrAddr;

    SysSetupAddress(SvrAddr, AF_INET, NetAddr, htons(iPortNo));

    if (SysConnect(SockFD, &SvrAddr, sizeof(SvrAddr), STD_SMTP_TIMEOUT) < 0)
    {
        SysCloseSocket(SockFD);
        return (INVALID_SMTPCH_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Check if We need to supply an HELO host
///////////////////////////////////////////////////////////////////////////////
    char            szHeloHost[MAX_HOST_NAME] = "";

    if (pszDomain == NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  Get the DNS name of the local interface
///////////////////////////////////////////////////////////////////////////////
        if (MscGetSockHost(SockFD, szHeloHost) < 0)
        {
            SYS_INET_ADDR   SockInfo;

            if (SysGetSockInfo(SockFD, SockInfo) < 0)
            {
                SysCloseSocket(SockFD);
                return (INVALID_SMTPCH_HANDLE);
            }

            char            szIP[128] = "???.???.???.???";

            StrSNCpy(szHeloHost, SysInetNToA(SockInfo, szIP));
        }

        pszDomain = szHeloHost;
    }

///////////////////////////////////////////////////////////////////////////////
//  Attach socket to buffered reader
///////////////////////////////////////////////////////////////////////////////
    BSOCK_HANDLE    hBSock = BSckAttach(SockFD);

    if (hBSock == INVALID_BSOCK_HANDLE)
    {
        SysCloseSocket(SockFD);
        return (INVALID_SMTPCH_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read welcome message
///////////////////////////////////////////////////////////////////////////////
    SmtpChannel    *pSmtpCh = (SmtpChannel *) SysAlloc(sizeof(SmtpChannel));

    if (pSmtpCh == NULL)
    {
        BSckDetach(hBSock, 1);
        return (INVALID_SMTPCH_HANDLE);
    }

    pSmtpCh->hBSock = hBSock;
    pSmtpCh->ulFlags = 0;
    pSmtpCh->ulMaxMsgSize = 0;

///////////////////////////////////////////////////////////////////////////////
//  Read welcome message
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse = -1;
    char            szRTXBuffer[2048] = "";

    if (!USmtpResponseClass(iSvrReponse = USmtpGetResponse(pSmtpCh->hBSock, szRTXBuffer,
                            sizeof(szRTXBuffer) - 1), 200))
    {
        BSckDetach(pSmtpCh->hBSock, 1);
        SysFree(pSmtpCh);

        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szRTXBuffer);
        }

        return (INVALID_SMTPCH_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Try the EHLO ESMTP command before
///////////////////////////////////////////////////////////////////////////////
    SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer) - 1, "EHLO %s", pszDomain);

    if (USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szRTXBuffer,
            szRTXBuffer, sizeof(szRTXBuffer) - 1), 200))
    {
///////////////////////////////////////////////////////////////////////////////
//  Parse EHLO response
///////////////////////////////////////////////////////////////////////////////
        if (USmtpParseEhloResponse(pSmtpCh, szRTXBuffer) < 0)
        {
            BSckDetach(pSmtpCh->hBSock, 1);
            SysFree(pSmtpCh);
            return (INVALID_SMTPCH_HANDLE);
        }

    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Send HELO and read result
///////////////////////////////////////////////////////////////////////////////
        SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer) - 1, "HELO %s", pszDomain);

        if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szRTXBuffer,
                szRTXBuffer, sizeof(szRTXBuffer) - 1), 200))
        {
            BSckDetach(pSmtpCh->hBSock, 1);
            SysFree(pSmtpCh);

            if (iSvrReponse > 0)
            {
                if (pSMTPE != NULL)
                    USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

                ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szRTXBuffer);
            }

            return (INVALID_SMTPCH_HANDLE);
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Check if We need authentication
///////////////////////////////////////////////////////////////////////////////
    if (USmtpServerAuthenticate(pSmtpCh, szAddress, pSMTPE) < 0)
    {
        USmtpCloseChannel((SMTPCH_HANDLE) pSmtpCh, 0, pSMTPE);

        return (INVALID_SMTPCH_HANDLE);
    }


    return ((SMTPCH_HANDLE) pSmtpCh);

}



int             USmtpCloseChannel(SMTPCH_HANDLE hSmtpCh, int iHardClose, SMTPError * pSMTPE)
{

    SmtpChannel    *pSmtpCh = (SmtpChannel *) hSmtpCh;

    if (!iHardClose)
    {
///////////////////////////////////////////////////////////////////////////////
//  Send QUIT and read result
///////////////////////////////////////////////////////////////////////////////
        int             iSvrReponse = -1;
        char            szRTXBuffer[2048] = "";

        if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, "QUIT",
                szRTXBuffer, sizeof(szRTXBuffer) - 1), 200))
        {
            BSckDetach(pSmtpCh->hBSock, 1);

            SysFree(pSmtpCh);

            if (iSvrReponse > 0)
            {
                if (pSMTPE != NULL)
                    USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

                ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szRTXBuffer);
            }

            return (ErrGetErrorCode());
        }
    }

    BSckDetach(pSmtpCh->hBSock, 1);

    SysFree(pSmtpCh);

    return (0);

}



int             USmtpChannelReset(SMTPCH_HANDLE hSmtpCh, SMTPError * pSMTPE)
{

    SmtpChannel    *pSmtpCh = (SmtpChannel *) hSmtpCh;

///////////////////////////////////////////////////////////////////////////////
//  Send RSET and read result
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse = -1;
    char            szRTXBuffer[2048] = "";

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, "RSET",
            szRTXBuffer, sizeof(szRTXBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szRTXBuffer);
        }

        return (ErrGetErrorCode());
    }

    return (0);

}



int             USmtpSendMail(SMTPCH_HANDLE hSmtpCh, const char *pszFrom, const char *pszRcpt,
                        FileSection const * pFS, SMTPError * pSMTPE)
{

    SmtpChannel    *pSmtpCh = (SmtpChannel *) hSmtpCh;

///////////////////////////////////////////////////////////////////////////////
//  Check message size ( if the remote server support the SIZE extension )
///////////////////////////////////////////////////////////////////////////////
    unsigned long   ulMessageSize = 0;

    if (pSmtpCh->ulMaxMsgSize != 0)
    {
        if (MscGetSectionSize(pFS, &ulMessageSize) < 0)
            return (ErrGetErrorCode());


        if (ulMessageSize >= pSmtpCh->ulMaxMsgSize)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, SMTP_FATAL_ERROR, ErrGetErrorString(ERR_SMTPSRV_MSG_SIZE));

            ErrSetErrorCode(ERR_SMTPSRV_MSG_SIZE);
            return (ERR_SMTPSRV_MSG_SIZE);
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Send MAIL FROM: and read result
///////////////////////////////////////////////////////////////////////////////
    int             iSvrReponse = -1;
    char            szRTXBuffer[2048] = "";

    if (pSmtpCh->ulFlags & SMTPCH_SUPPORT_SIZE)
    {
        if ((ulMessageSize == 0) && (MscGetSectionSize(pFS, &ulMessageSize) < 0))
            return (ErrGetErrorCode());


        SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer) - 1, "MAIL FROM:<%s> SIZE=%lu",
                pszFrom, ulMessageSize);
    }
    else
        SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer) - 1, "MAIL FROM:<%s>", pszFrom);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szRTXBuffer,
            szRTXBuffer, sizeof(szRTXBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

            ErrSetErrorCode(ERR_SMTP_BAD_MAIL_FROM, szRTXBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Send RCPT TO: and read result
///////////////////////////////////////////////////////////////////////////////
    SysSNPrintf(szRTXBuffer, sizeof(szRTXBuffer) - 1, "RCPT TO:<%s>", pszRcpt);

    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, szRTXBuffer,
            szRTXBuffer, sizeof(szRTXBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

            ErrSetErrorCode(ERR_SMTP_BAD_RCPT_TO, szRTXBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Send DATA and read the "ready to receive"
///////////////////////////////////////////////////////////////////////////////
    if (!USmtpResponseClass(iSvrReponse = USmtpSendCommand(pSmtpCh->hBSock, "DATA",
            szRTXBuffer, sizeof(szRTXBuffer) - 1), 300))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

            ErrSetErrorCode(ERR_SMTP_BAD_DATA, szRTXBuffer);
        }

        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Send file
///////////////////////////////////////////////////////////////////////////////
    if (SysSendFile(BSckGetAttachedSocket(pSmtpCh->hBSock), pFS->szFilePath, pFS->ulStartOffset,
                    pFS->ulEndOffset, STD_SMTP_TIMEOUT) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Send END OF DATA and read transfer result
///////////////////////////////////////////////////////////////////////////////
    if (BSckSendString(pSmtpCh->hBSock, ".", STD_SMTP_TIMEOUT) <= 0)
        return (ErrGetErrorCode());

    if (!USmtpResponseClass(iSvrReponse = USmtpGetResponse(pSmtpCh->hBSock, szRTXBuffer,
                            sizeof(szRTXBuffer) - 1), 200))
    {
        if (iSvrReponse > 0)
        {
            if (pSMTPE != NULL)
                USmtpSetError(pSMTPE, iSvrReponse, szRTXBuffer);

            ErrSetErrorCode(ERR_BAD_SERVER_RESPONSE, szRTXBuffer);
        }

        return (ErrGetErrorCode());
    }

    return (0);

}




int             USmtpSendMail(const char *pszServer, const char *pszDomain,
                        const char *pszFrom, const char *pszRcpt, FileSection const * pFS,
                        SMTPError * pSMTPE)
{
///////////////////////////////////////////////////////////////////////////////
//  Open STMP channel and try to send the message
///////////////////////////////////////////////////////////////////////////////
    SMTPCH_HANDLE   hSmtpCh = USmtpCreateChannel(pszServer, pszDomain, pSMTPE);

    if (hSmtpCh == INVALID_SMTPCH_HANDLE)
        return (ErrGetErrorCode());


    int             iResultCode = USmtpSendMail(hSmtpCh, pszFrom, pszRcpt, pFS, pSMTPE);


    USmtpCloseChannel(hSmtpCh, 0, pSMTPE);

    return (iResultCode);

}



char           *USmtpBuildRcptPath(char const * const * ppszRcptTo, SVRCFG_HANDLE hSvrConfig)
{

    int             iRcptCount = StrStringsCount(ppszRcptTo);
    char            szDestDomain[MAX_HOST_NAME] = "";

    if (USmtpSplitEmailAddr(ppszRcptTo[0], NULL, szDestDomain) < 0)
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Try to get routing path, if not found simply return an address concat
//  of "ppszRcptTo"
///////////////////////////////////////////////////////////////////////////////
    char            szSpecMXHost[1024] = "";

    if (USmtpGetGateway(hSvrConfig, szDestDomain, szSpecMXHost) < 0)
        return (USmlAddrConcat(ppszRcptTo));


    char           *pszSendRcpt = USmlAddrConcat(ppszRcptTo);

    if (pszSendRcpt == NULL)
        return (NULL);

    char           *pszRcptPath = (char *) SysAlloc(strlen(pszSendRcpt) +
            strlen(szSpecMXHost) + 2);

    if (iRcptCount == 1)
        sprintf(pszRcptPath, "%s:%s", szSpecMXHost, pszSendRcpt);
    else
        sprintf(pszRcptPath, "%s,%s", szSpecMXHost, pszSendRcpt);

    SysFree(pszSendRcpt);


    return (pszRcptPath);

}



char          **USmtpGetMailExchangers(SVRCFG_HANDLE hSvrConfig, const char *pszDomain)
{
///////////////////////////////////////////////////////////////////////////////
//  Try to get default gateways
///////////////////////////////////////////////////////////////////////////////
    char           *pszDefaultGws = SvrGetConfigVar(hSvrConfig, "DefaultSMTPGateways");

    if (pszDefaultGws == NULL)
    {
        ErrSetErrorCode(ERR_NO_PREDEFINED_MX);
        return (NULL);
    }

    char          **ppszMXGWs = StrTokenize(pszDefaultGws, ",; \t\r\n");


    SysFree(pszDefaultGws);

    return (ppszMXGWs);

}




static int      USmtpGetDomainMX(SVRCFG_HANDLE hSvrConfig, const char *pszDomain,
                        char *&pszMXDomains)
{
///////////////////////////////////////////////////////////////////////////////
//  Exist a configured list of smart DNS hosts ?
///////////////////////////////////////////////////////////////////////////////
    char           *pszSmartDNS = SvrGetConfigVar(hSvrConfig, "SmartDNSHost");


    int             iQueryResult = CDNS_GetDomainMX(pszDomain, pszMXDomains, pszSmartDNS);


    if (pszSmartDNS != NULL)
        SysFree(pszSmartDNS);

    return (iQueryResult);

}




int             USmtpCheckMailDomain(SVRCFG_HANDLE hSvrConfig, char const * pszDomain)
{

    NET_ADDRESS     NetAddr = SysGetHostByName(pszDomain);

    if (NetAddr == SYS_INVALID_NET_ADDRESS)
    {
        char           *pszMXDomains = NULL;

        if (USmtpGetDomainMX(hSvrConfig, pszDomain, pszMXDomains) < 0)
        {
            ErrSetErrorCode(ERR_INVALID_MAIL_DOMAIN);
            return (ERR_INVALID_MAIL_DOMAIN);
        }

        SysFree(pszMXDomains);
    }

    return (0);

}




MXS_HANDLE      USmtpGetMXFirst(SVRCFG_HANDLE hSvrConfig, const char *pszDomain,
                        char *pszMXHost)
{

///////////////////////////////////////////////////////////////////////////////
//  Make a DNS query for domain MXs
///////////////////////////////////////////////////////////////////////////////
    char           *pszMXHosts = NULL;

    if (USmtpGetDomainMX(hSvrConfig, pszDomain, pszMXHosts) < 0)
        return (INVALID_MXS_HANDLE);

///////////////////////////////////////////////////////////////////////////////
//  MX records structure allocation
///////////////////////////////////////////////////////////////////////////////
    SmtpMXRecords  *pMXR = (SmtpMXRecords *) SysAlloc(sizeof(SmtpMXRecords));

    if (pMXR == NULL)
    {
        SysFree(pszMXHosts);
        return (INVALID_MXS_HANDLE);
    }

    pMXR->iNumMXRecords = 0;
    pMXR->iCurrMxCost = -1;


///////////////////////////////////////////////////////////////////////////////
//  MX hosts string format = c:h[,c:h]  where "c = cost" and "h = hosts"
///////////////////////////////////////////////////////////////////////////////
    int             iMXCost = INT_MAX,
                    iCurrIndex = -1;
    char           *pszToken = NULL,
                   *pszSavePtr = NULL;

    pszToken = SysStrTok(pszMXHosts, ":, \t\r\n", &pszSavePtr);

    while ((pMXR->iNumMXRecords < MAX_MX_RECORDS) && (pszToken != NULL))
    {
///////////////////////////////////////////////////////////////////////////////
//  Get MX cost
///////////////////////////////////////////////////////////////////////////////
        int             iCost = atoi(pszToken);

        if ((pszToken = SysStrTok(NULL, ":, \t\r\n", &pszSavePtr)) == NULL)
        {
            for (--pMXR->iNumMXRecords; pMXR->iNumMXRecords >= 0; pMXR->iNumMXRecords--)
                SysFree(pMXR->pszMXName[pMXR->iNumMXRecords]);
            SysFree(pMXR);

            SysFree(pszMXHosts);

            ErrSetErrorCode(ERR_INVALID_MXRECS_STRING);
            return (INVALID_MXS_HANDLE);
        }

        pMXR->iMXCost[pMXR->iNumMXRecords] = iCost;
        pMXR->pszMXName[pMXR->iNumMXRecords] = SysStrDup(pszToken);

        if ((iCost < iMXCost) && (iCost >= pMXR->iCurrMxCost))
        {
            iMXCost = iCost;

            strcpy(pszMXHost, pMXR->pszMXName[pMXR->iNumMXRecords]);

            iCurrIndex = pMXR->iNumMXRecords;
        }

        ++pMXR->iNumMXRecords;

        pszToken = SysStrTok(NULL, ":, \t\r\n", &pszSavePtr);
    }

    SysFree(pszMXHosts);

    if (iMXCost == INT_MAX)
    {
        for (--pMXR->iNumMXRecords; pMXR->iNumMXRecords >= 0; pMXR->iNumMXRecords--)
            SysFree(pMXR->pszMXName[pMXR->iNumMXRecords]);
        SysFree(pMXR);

        ErrSetErrorCode(ERR_INVALID_MXRECS_STRING);
        return (INVALID_MXS_HANDLE);
    }

    pMXR->iCurrMxCost = iMXCost;
    pMXR->iMXCost[iCurrIndex] = iMXCost - 1;

    return ((MXS_HANDLE) pMXR);

}



int             USmtpGetMXNext(MXS_HANDLE hMXSHandle, char *pszMXHost)
{

    SmtpMXRecords  *pMXR = (SmtpMXRecords *) hMXSHandle;

    int             iMXCost = INT_MAX,
                    iCurrIndex = -1;

    for (int ii = 0; ii < pMXR->iNumMXRecords; ii++)
    {
        if ((pMXR->iMXCost[ii] < iMXCost) && (pMXR->iMXCost[ii] >= pMXR->iCurrMxCost))
        {
            iMXCost = pMXR->iMXCost[ii];

            strcpy(pszMXHost, pMXR->pszMXName[ii]);

            iCurrIndex = ii;
        }
    }

    if (iMXCost == INT_MAX)
    {
        ErrSetErrorCode(ERR_NO_MORE_MXRECORDS);
        return (ERR_NO_MORE_MXRECORDS);
    }

    pMXR->iCurrMxCost = iMXCost;
    pMXR->iMXCost[iCurrIndex] = iMXCost - 1;

    return (0);

}



void            USmtpMXSClose(MXS_HANDLE hMXSHandle)
{

    SmtpMXRecords  *pMXR = (SmtpMXRecords *) hMXSHandle;

    for (--pMXR->iNumMXRecords; pMXR->iNumMXRecords >= 0; pMXR->iNumMXRecords--)
        SysFree(pMXR->pszMXName[pMXR->iNumMXRecords]);

    SysFree(pMXR);

}




bool            USmtpDnsMapsContained(SYS_INET_ADDR const & PeerInfo, char const * pszMapsServer)
{

    NET_ADDRESS     NetAddr = SysGetAddrAddress(PeerInfo);
    SYS_UINT8       AddrBytes[sizeof(NET_ADDRESS)];

    *((NET_ADDRESS *) AddrBytes) = NetAddr;

    char            szMapsQuery[256] = "";

    sprintf(szMapsQuery, "%u.%u.%u.%u.%s",
            (unsigned int) AddrBytes[3], (unsigned int) AddrBytes[2],
            (unsigned int) AddrBytes[1], (unsigned int) AddrBytes[0], pszMapsServer);


    return ((SysGetHostByName(szMapsQuery) != SYS_INVALID_NET_ADDRESS) ? true : false);

}



static char    *USmtpGetSpammersFilePath(char *pszSpamFilePath, int iMaxPath)
{

    CfgGetRootPath(pszSpamFilePath, iMaxPath);

    StrNCat(pszSpamFilePath, SMTP_SPAMMERS_FILE, iMaxPath);

    return (pszSpamFilePath);

}




int             USmtpSpammerCheck(const SYS_INET_ADDR & PeerInfo)
{

    char            szSpammersFilePath[SYS_MAX_PATH] = "";

    USmtpGetSpammersFilePath(szSpammersFilePath, sizeof(szSpammersFilePath));


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szSpammersFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pSpammersFile = fopen(szSpammersFilePath, "rt");

    if (pSpammersFile == NULL)
    {
        RLckUnlockSH(hResLock);

        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }


    NET_ADDRESS     TestAddr = SysGetAddrAddress(PeerInfo);


    char            szSpammerLine[SPAMMERS_LINE_MAX] = "";

    while (MscGetConfigLine(szSpammerLine, sizeof(szSpammerLine) - 1, pSpammersFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szSpammerLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);
        AddressFilter   AF;

        if ((iFieldsCount > 0) &&
                (MscLoadAddressFilter(ppszStrings, iFieldsCount, AF) == 0) &&
                MscAddressMatch(AF, TestAddr))
        {
            StrFreeStrings(ppszStrings);
            fclose(pSpammersFile);
            RLckUnlockSH(hResLock);

            char            szIP[128] = "???.???.???.???";

            ErrSetErrorCode(ERR_SPAMMER_IP, SysInetNToA(PeerInfo, szIP));
            return (ERR_SPAMMER_IP);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pSpammersFile);

    RLckUnlockSH(hResLock);

    return (0);

}



static char    *USmtpGetSpamAddrFilePath(char *pszSpamFilePath, int iMaxPath)
{

    CfgGetRootPath(pszSpamFilePath, iMaxPath);

    StrNCat(pszSpamFilePath, SMTP_SPAM_ADDRESS_FILE, iMaxPath);

    return (pszSpamFilePath);

}




int             USmtpSpamAddressCheck(char const * pszAddress)
{

    char            szSpammersFilePath[SYS_MAX_PATH] = "";

    USmtpGetSpamAddrFilePath(szSpammersFilePath, sizeof(szSpammersFilePath));


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szSpammersFilePath, szResLock,
                            sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pSpammersFile = fopen(szSpammersFilePath, "rt");

    if (pSpammersFile == NULL)
    {
        RLckUnlockSH(hResLock);
        return (0);
    }


    char            szSpammerLine[SPAM_ADDRESS_LINE_MAX] = "";

    while (MscGetConfigLine(szSpammerLine, sizeof(szSpammerLine) - 1, pSpammersFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szSpammerLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount > 0) && StrIWildMatch(pszAddress, ppszStrings[0]))
        {
            StrFreeStrings(ppszStrings);
            fclose(pSpammersFile);
            RLckUnlockSH(hResLock);

            ErrSetErrorCode(ERR_SPAM_ADDRESS, pszAddress);
            return (ERR_SPAM_ADDRESS);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pSpammersFile);

    RLckUnlockSH(hResLock);

    return (0);

}



int             USmtpAddMessageInfo(FILE * pMsgFile, char const * pszClientDomain,
                        SYS_INET_ADDR const & PeerInfo, char const * pszServerDomain,
                        SYS_INET_ADDR const & SockInfo, char const * pszSmtpServerLogo)
{

    char            szTime[256] = "";

    MscGetTimeStr(szTime, sizeof(szTime) - 1);


    char            szPeerIP[128] = "",
                    szSockIP[128] = "";

    SysInetNToA(PeerInfo, szPeerIP);
    SysInetNToA(SockInfo, szSockIP);

///////////////////////////////////////////////////////////////////////////////
//  Write message info. If You change the order ( or add new fields ) You must
//  arrange fields into the SmtpMsgInfo union defined in SMTPUtils.h
///////////////////////////////////////////////////////////////////////////////
    fprintf(pMsgFile, "%s;%s;%s;%s;%s;%s\r\n",
            pszClientDomain, szPeerIP,
            pszServerDomain, szSockIP,
            szTime, pszSmtpServerLogo);

    return (0);

}



char           *USmtpGetReceived(int iType, char const * const * ppszMsgInfo, char const * pszMailFrom,
                        char const * pszRcptTo, char const * pszMessageID)
{

    char            szFrom[MAX_SMTP_ADDRESS] = "",
                    szRcpt[MAX_SMTP_ADDRESS] = "";

    if ((USmlParseAddress(pszMailFrom, NULL, 0, szFrom, sizeof(szFrom) - 1) < 0) ||
            (USmlParseAddress(pszRcptTo, NULL, 0, szRcpt, sizeof(szRcpt) - 1) < 0))
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Return "Received:" tag
///////////////////////////////////////////////////////////////////////////////
    char           *pszReceived = NULL;

    switch (iType)
    {
        case (RECEIVED_TYPE_STRICT):
            pszReceived = StrSprint(
                    "Received: from %s\r\n"
                    "\tby %s with %s\r\n"
                    "\tid <%s> for <%s> from <%s>;\r\n"
                    "\t%s\r\n", ppszMsgInfo[smsgiClientDomain], ppszMsgInfo[smsgiServerDomain],
                    ppszMsgInfo[smsgiSeverName], pszMessageID, szRcpt, szFrom, ppszMsgInfo[smsgiTime]);
            break;

        case (RECEIVED_TYPE_VERBOSE):
            pszReceived = StrSprint(
                    "Received: from %s (%s)\r\n"
                    "\tby %s (%s) with %s\r\n"
                    "\tid <%s> for <%s> from <%s>;\r\n"
                    "\t%s\r\n", ppszMsgInfo[smsgiClientDomain], ppszMsgInfo[smsgiClientIP],
                    ppszMsgInfo[smsgiServerDomain], ppszMsgInfo[smsgiServerIP],
                    ppszMsgInfo[smsgiSeverName], pszMessageID, szRcpt, szFrom, ppszMsgInfo[smsgiTime]);
            break;

        case (RECEIVED_TYPE_STD):
        default:
            pszReceived = StrSprint(
                    "Received: from %s (%s)\r\n"
                    "\tby %s with %s\r\n"
                    "\tid <%s> for <%s> from <%s>;\r\n"
                    "\t%s\r\n", ppszMsgInfo[smsgiClientDomain], ppszMsgInfo[smsgiClientIP],
                    ppszMsgInfo[smsgiServerDomain], ppszMsgInfo[smsgiSeverName], pszMessageID,
                    szRcpt, szFrom, ppszMsgInfo[smsgiTime]);
            break;
    }

    return (pszReceived);

}
