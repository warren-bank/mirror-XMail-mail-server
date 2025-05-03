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
#include "SList.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "SvrUtils.h"
#include "UsrUtils.h"
#include "MessQueue.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"
#include "MailDomains.h"
#include "SMTPUtils.h"
#include "ExtAliases.h"
#include "UsrMailList.h"
#include "Filter.h"
#include "MailConfig.h"
#include "AppDefines.h"
#include "MailSvr.h"






#define FILTER_STORAGE_DIR          "filters"
#define FILTER_SELECT_MAX           128
#define FILTER_DB_LINE_MAX          512
#define FILTER_LINE_MAX             1024
#define FILTER_PRIORITY             SYS_PRIORITY_NORMAL
#define FILTER_OUT_NNF_EXITCODE     97
#define FILTER_OUT_NN_EXITCODE      98
#define FILTER_OUT_EXITCODE         99
#define FILTER_MODIFY_EXITCODE      100




struct FilterMsgInfo
{
    char            szSender[MAX_ADDR_NAME];
    char            szRecipient[MAX_ADDR_NAME];
    SYS_INET_ADDR   LocalAddr;
    SYS_INET_ADDR   RemoteAddr;
};





static int      FilLoadMsgInfo(SPLF_HANDLE hFSpool, FilterMsgInfo &FMI);
static void     FilFreeMsgInfo(FilterMsgInfo &FMI);
static int      FilGetFilePath(char const *pszMode, char *pszFilePath, int iMaxPath);
static int      FilAddFilter(char **ppszFilters, int &iNumFilters, char const *pszFilterName);
static int      FilSelectFilters(char const *pszFilterFilePath, char const *pszMode,
                                 FilterMsgInfo const &FMI, char **ppszFilters, int iMaxFilters);
static void     FilFreeFilters(char **ppszFilters, int iNumFilters);
static int      FilGetFilterPath(char const *pszFileName, char *pszFilePath, int iMaxPath);
static int      FilApplyFilter(char const *pszFilterPath, SPLF_HANDLE hFSpool,
                               QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                               FilterMsgInfo const &FMI);
static int      FilFilterMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool,
                                          FilterMsgInfo const &FMI);







static int      FilLoadMsgInfo(SPLF_HANDLE hFSpool, FilterMsgInfo &FMI)
{

    UserInfo       *pUI;
    char const     *const * ppszInfo = USmlGetInfo(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom);
    int             iRcptDomains = StrStringsCount(ppszRcpt);
    char            szUser[MAX_ADDR_NAME] = "";
    char            szDomain[MAX_ADDR_NAME] = "";

    ZeroData(FMI);

    if ((iFromDomains > 0) &&
        (USmtpSplitEmailAddr(ppszFrom[iFromDomains - 1], szUser, szDomain) == 0))
    {
        if ((pUI = UsrGetUserByNameOrAlias(szDomain, szUser)) != NULL)
        {
            UsrGetAddress(pUI, FMI.szSender);

            UsrFreeUserInfo(pUI);
        }
        else
            StrSNCpy(FMI.szSender, ppszRcpt[iFromDomains - 1]);
    }
    else
        SetEmptyString(FMI.szSender);


    if ((iRcptDomains > 0) &&
        (USmtpSplitEmailAddr(ppszRcpt[iRcptDomains - 1], szUser, szDomain) == 0))
    {
        if ((pUI = UsrGetUserByNameOrAlias(szDomain, szUser)) != NULL)
        {
            UsrGetAddress(pUI, FMI.szRecipient);

            UsrFreeUserInfo(pUI);
        }
        else
            StrSNCpy(FMI.szRecipient, ppszRcpt[iRcptDomains - 1]);
    }
    else
        SetEmptyString(FMI.szRecipient);


    if ((MscGetServerAddress(ppszInfo[smiServerAddr], FMI.LocalAddr) < 0) ||
        (MscGetServerAddress(ppszInfo[smiClientAddr], FMI.RemoteAddr) < 0))
        return (ErrGetErrorCode());


    return (0);

}




static void     FilFreeMsgInfo(FilterMsgInfo &FMI)
{



}




static int      FilGetFilePath(char const *pszMode, char *pszFilePath, int iMaxPath)
{

    char            szMailRootPath[SYS_MAX_PATH] = "";

    CfgGetRootPath(szMailRootPath, sizeof(szMailRootPath));

    SysSNPrintf(pszFilePath, iMaxPath - 1, "%sfilters.%s.tab", szMailRootPath, pszMode);

    return (0);

}




static int      FilAddFilter(char **ppszFilters, int &iNumFilters, char const *pszFilterName)
{

    for (int ii = 0; ii < iNumFilters; ii++)
        if (strcmp(ppszFilters[ii], pszFilterName) == 0)
            return (0);

    if ((ppszFilters[iNumFilters] = SysStrDup(pszFilterName)) == NULL)
        return (ErrGetErrorCode());

    iNumFilters++;

    return (0);

}




static int      FilSelectFilters(char const *pszFilterFilePath, char const *pszMode,
                                 FilterMsgInfo const &FMI, char **ppszFilters, int iMaxFilters)
{
///////////////////////////////////////////////////////////////////////////////
//  Share lock the filter table file
///////////////////////////////////////////////////////////////////////////////
    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(pszFilterFilePath, szResLock,
                                                          sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Get local and remote IP addresses
///////////////////////////////////////////////////////////////////////////////
    NET_ADDRESS     LocalAddr;
    NET_ADDRESS     RemoteAddr;

    SysGetAddrAddress(FMI.LocalAddr, LocalAddr);
    SysGetAddrAddress(FMI.RemoteAddr, RemoteAddr);

///////////////////////////////////////////////////////////////////////////////
//  Open the filter database. Fail smootly if the file does not exist
///////////////////////////////////////////////////////////////////////////////
    FILE           *pFile = fopen(pszFilterFilePath, "rt");

    if (pFile == NULL)
    {
        RLckUnlockSH(hResLock);
        return (0);
    }

    int             iNumFilters = 0;
    char            szLine[FILTER_DB_LINE_MAX] = "";

    while ((iNumFilters < iMaxFilters) &&
           (MscGetConfigLine(szLine, sizeof(szLine) - 1, pFile) != NULL))
    {
        char          **ppszTokens = StrGetTabLineStrings(szLine);

        if (ppszTokens == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszTokens);

        if ((iFieldsCount >= filMax) &&
            StrIWildMatch(FMI.szSender, ppszTokens[filSender]) &&
            StrIWildMatch(FMI.szRecipient, ppszTokens[filRecipient]))
        {
            AddressFilter   AFRemote;
            AddressFilter   AFLocal;

            if ((MscLoadAddressFilter(&ppszTokens[filRemoteAddr], 1, AFRemote) == 0) &&
                MscAddressMatch(AFRemote, RemoteAddr) &&
                (MscLoadAddressFilter(&ppszTokens[filLocalAddr], 1, AFLocal) == 0) &&
                MscAddressMatch(AFLocal, LocalAddr))
            {

                FilAddFilter(ppszFilters, iNumFilters, ppszTokens[filFileName]);

            }
        }

        StrFreeStrings(ppszTokens);
    }

    fclose(pFile);

    RLckUnlockSH(hResLock);

    return (iNumFilters);

}




static void     FilFreeFilters(char **ppszFilters, int iNumFilters)
{

    for (iNumFilters--; iNumFilters >= 0; iNumFilters--)
        SysFree(ppszFilters[iNumFilters]);

}



static int      FilGetFilterPath(char const *pszFileName, char *pszFilePath, int iMaxPath)
{

    char            szMailRootPath[SYS_MAX_PATH] = "";

    CfgGetRootPath(szMailRootPath, sizeof(szMailRootPath));

    SysSNPrintf(pszFilePath, iMaxPath - 1, "%s%s%s%s",
                szMailRootPath, FILTER_STORAGE_DIR, SYS_SLASH_STR, pszFileName);

    return (0);

}



static int      FilApplyFilter(char const *pszFilterPath, SPLF_HANDLE hFSpool,
                               QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                               FilterMsgInfo const &FMI)
{
///////////////////////////////////////////////////////////////////////////////
//  Share lock the filter file
///////////////////////////////////////////////////////////////////////////////
    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(pszFilterPath, szResLock,
                                                          sizeof(szResLock)));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  This should not happen but if it happens we let the message pass through
///////////////////////////////////////////////////////////////////////////////
    FILE           *pFiltFile = fopen(pszFilterPath, "rt");

    if (pFiltFile == NULL)
    {
        RLckUnlockSH(hResLock);
        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Filter this message
///////////////////////////////////////////////////////////////////////////////
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
            FilFilterMacroSubstitutes(ppszCmdTokens, hFSpool, FMI);


            int             iExitCode = 0;

            if (SysExec(ppszCmdTokens[0], &ppszCmdTokens[0], iFilterTimeout,
                        FILTER_PRIORITY, &iExitCode) == 0)
            {
                if ((iExitCode == FILTER_OUT_EXITCODE) || (iExitCode == FILTER_OUT_NN_EXITCODE) ||
                    (iExitCode == FILTER_OUT_NNF_EXITCODE))
                {
                    StrFreeStrings(ppszCmdTokens);
                    fclose(pFiltFile);
                    RLckUnlockSH(hResLock);

///////////////////////////////////////////////////////////////////////////////
//  Filter out message
///////////////////////////////////////////////////////////////////////////////
                    if (iExitCode == FILTER_OUT_EXITCODE)
                        QueUtCleanupNotifyErrDelivery(hQueue, hMessage, NULL,
                                                      ErrGetErrorString(ERR_FILTERED_MESSAGE), NULL);
                    else if (iExitCode == FILTER_OUT_NN_EXITCODE)
                        QueCleanupMessage(hQueue, hMessage, !QueUtRemoveSpoolErrors());
                    else
                        QueCleanupMessage(hQueue, hMessage, false);

                    ErrSetErrorCode(ERR_FILTERED_MESSAGE);
                    return (ERR_FILTERED_MESSAGE);
                }
                else if (iExitCode == FILTER_MODIFY_EXITCODE)
                {
///////////////////////////////////////////////////////////////////////////////
//  Filter modified the message, we need to reload the spool handle
///////////////////////////////////////////////////////////////////////////////
                    if (USmlReloadHandle(hFSpool) < 0)
                    {
                        ErrorPush();
                        StrFreeStrings(ppszCmdTokens);
                        fclose(pFiltFile);
                        RLckUnlockSH(hResLock);

                        SysLogMessage(LOG_LEV_MESSAGE,
                                      "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
                                      FMI.szSender, FMI.szRecipient, ppszCmdTokens[0]);

                        QueUtErrLogMessage(hQueue, hMessage,
                                           "Filter error [ Modified message corrupted ]: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
                                           FMI.szSender, FMI.szRecipient, ppszCmdTokens[0]);

                        QueCleanupMessage(hQueue, hMessage, true);

                        return (ErrorPop());
                    }
                }
            }
            else
            {
                SysLogMessage(LOG_LEV_MESSAGE,
                              "Filter error: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
                              FMI.szSender, FMI.szRecipient, ppszCmdTokens[0]);

                QueUtErrLogMessage(hQueue, hMessage,
                                   "Filter error: Sender = \"%s\" Recipient = \"%s\" (%s)\n",
                                   FMI.szSender, FMI.szRecipient, ppszCmdTokens[0]);
            }
        }

        StrFreeStrings(ppszCmdTokens);
    }

    fclose(pFiltFile);
    RLckUnlockSH(hResLock);

    return (0);

}



int             FilFilterMessage(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
                                 QMSG_HANDLE hMessage, char const *pszMode)
{
///////////////////////////////////////////////////////////////////////////////
//  Get filter file path and returns immediately if no file is defined
///////////////////////////////////////////////////////////////////////////////
    char            szFilterFilePath[SYS_MAX_PATH] = "";

    FilGetFilePath(pszMode, szFilterFilePath, sizeof(szFilterFilePath));

    if (!SysExistFile(szFilterFilePath))
        return (0);

///////////////////////////////////////////////////////////////////////////////
//  Load the message info
///////////////////////////////////////////////////////////////////////////////
    FilterMsgInfo   FMI;

    if (FilLoadMsgInfo(hFSpool, FMI) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Select applicable filters
///////////////////////////////////////////////////////////////////////////////
    int             iNumFilters;
    char           *pszFilters[FILTER_SELECT_MAX];

    if ((iNumFilters = FilSelectFilters(szFilterFilePath, pszMode, FMI, pszFilters,
                                        CountOf(pszFilters))) < 0)
    {
        ErrorPush();
        FilFreeMsgInfo(FMI);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Sequentially apply each selected filter
///////////////////////////////////////////////////////////////////////////////
    for (int ii = 0; ii < iNumFilters; ii++)
    {
        char            szFilterPath[SYS_MAX_PATH] = "";

        FilGetFilterPath(pszFilters[ii], szFilterPath, sizeof(szFilterPath));

        if (FilApplyFilter(szFilterPath, hFSpool, hQueue, hMessage, FMI) < 0)
        {
            ErrorPush();
            FilFreeFilters(pszFilters, iNumFilters);
            FilFreeMsgInfo(FMI);
            return (ErrorPop());
        }
    }

    FilFreeFilters(pszFilters, iNumFilters);
    FilFreeMsgInfo(FMI);

    return (0);

}



static int      FilFilterMacroSubstitutes(char **ppszCmdTokens, SPLF_HANDLE hFSpool,
                                          FilterMsgInfo const &FMI)
{

    char const     *const * ppszInfo = USmlGetInfo(hFSpool);
    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *pszMessageID = USmlGetSpoolFile(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom);
    int             iRcptDomains = StrStringsCount(ppszRcpt);
    FileSection     FS;

///////////////////////////////////////////////////////////////////////////////
//  This function retrieve the spool file message section and sync the content.
//  This is necessary before passing the file name to external programs
///////////////////////////////////////////////////////////////////////////////
    if (USmlGetMsgFileSection(hFSpool, FS) < 0)
        return (ErrGetErrorCode());

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
        else if (strcmp(ppszCmdTokens[ii], "@@RFROM") == 0)
        {
            char           *pszNewValue = SysStrDup(FMI.szSender);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@RRCPT") == 0)
        {
            char           *pszNewValue = SysStrDup(FMI.szRecipient);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@FILE") == 0)
        {
            char           *pszNewValue = SysStrDup(FS.szFilePath);

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
        else if (strcmp(ppszCmdTokens[ii], "@@LOCALADDR") == 0)
        {
            char           *pszNewValue = SysStrDup(ppszInfo[smiServerAddr]);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }
        else if (strcmp(ppszCmdTokens[ii], "@@REMOTEADDR") == 0)
        {
            char           *pszNewValue = SysStrDup(ppszInfo[smiClientAddr]);

            if (pszNewValue == NULL)
                return (ErrGetErrorCode());

            SysFree(ppszCmdTokens[ii]);

            ppszCmdTokens[ii] = pszNewValue;
        }

    }

    return (0);

}

