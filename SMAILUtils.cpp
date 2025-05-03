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
#include "StrUtils.h"
#include "SList.h"
#include "ShBlocks.h"
#include "ResLocks.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "MessQueue.h"
#include "QueueUtils.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "ExtAliases.h"
#include "MiscUtils.h"
#include "MailDomains.h"
#include "SMTPSvr.h"
#include "SMTPUtils.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "SMAILUtils.h"







#define SFF_HEADER_MODIFIED             (1 << 0)

#define STD_TAG_BUFFER_LENGTH           1024
#define CUSTOM_CMD_LINE_MAX             512
#define SMAIL_DOMAIN_PROC_DIR           "custdomains"
#define SMAIL_DOMAIN_FILTER_DIR         "filters"
#define SMAIL_DEFAULT_FILTER            ".tab"
#define SMAIL_LOG_FILE                  "smail"
#define MAX_MTA_OPS                     16
#define ADDRESS_TOKENIZER               ","










struct SpoolFileData
{
    char          **ppszFrom;
    char           *pszMailFrom;
    char           *pszSendMailFrom;
    char          **ppszRcpt;
    char           *pszRcptTo;
    char           *pszSendRcptTo;
    char           *pszRelayDomain;
    char            szSMTPDomain[MAX_ADDR_NAME];
    char            szMessageID[128];
    char            szMessFilePath[SYS_MAX_PATH];
    unsigned long   ulMessageOffset;
    unsigned long   ulMailDataOffset;
    char            szSpoolFile[SYS_MAX_PATH];
    HSLIST          hTagList;
    unsigned long   ulFlags;
};

struct MessageTagData
{
    LISTLINK        LL;
    char           *pszTagName;
    char           *pszTagData;
};









static MessageTagData *USmlAllocTag(char const * pszTagName, char const * pszTagData);
static void     USmlFreeTag(MessageTagData * pMTD);
static MessageTagData *USmlFindTag(HSLIST & hTagList, char const * pszTagName);
static int      USmlAddTag(HSLIST & hTagList, char const * pszTagName,
                        char const * pszTagData, int iUpdate = 0);
static void     USmlFreeTagsList(HSLIST & hTagList);
static int      USmlLoadTags(FILE * pSpoolFile, HSLIST & hTagList);
static int      USmlDumpHeaders(FILE * pMsgFile, HSLIST & hTagList);
static void     USmlFreeData(SpoolFileData * pSFD);
static int      USmlFlushMessageFile(SpoolFileData * pSFD);
static int      USmlProcessCustomMailingFile(UserInfo * pUI, SPLF_HANDLE hFSpool,
                        QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char const * pszMPFile,
                        LocalMailProcConfig & LMPC);
static int      USmlCmdMacroSubstitutes(char **ppszCmdTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool);
static int      USmlCmd_external(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC);
static int      USmlCmd_wait(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC);
static int      USmlCmd_mailbox(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC);
static int      USmlCmd_redirect(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC);
static int      USmlCmd_lredirect(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC);
static int      USmlLogMessage(char const * pszSMTPDomain, char const * pszMessageID,
                        char const * pszSmtpMessageID, char const * pszFrom, char const * pszRcpt,
                        char const * pszMedium, char const * pszParam);
static int      USmlExtractFromAddress(HSLIST & hTagList, char *pszFromAddr,
                        int iMaxAddress);
static char const *USmlAddressFromAtPtr(char const * pszAt, char const * pszBase,
                        char *pszAddress, int iMaxAddress);
static int      USmlAddAddresses(char const * pszAddrList, DynString * pAddrDS,
                        char const * const * ppszMatchDomains = NULL);
static char   **USmlGetAddressList(HSLIST & hTagList, char const * const * ppszMatchDomains, ...);
static int      USmlExtractToAddress(HSLIST & hTagList, char *pszToAddr, int iMaxAddress);
static char   **USmlBuildTargetRcptList(char const * pszRcptTo, HSLIST & hTagList);
static int      USmlCreateSpoolFile(FILE * pMailFile, char const * pszMailFrom,
                        char const * pszRcptTo, char const * pszSpoolFile);











int             USmlLoadSpoolFileHeader(char const * pszSpoolFile, SpoolFileHeader & SFH)
{

    ZeroData(SFH);

    FILE           *pSpoolFile = fopen(pszSpoolFile, "rb");

    if (pSpoolFile == NULL)
    {
        ErrSetErrorCode(ERR_SPOOL_FILE_NOT_FOUND, pszSpoolFile);
        return (ERR_SPOOL_FILE_NOT_FOUND);
    }

///////////////////////////////////////////////////////////////////////////////
//  Build spool file name
///////////////////////////////////////////////////////////////////////////////
    char            szFName[SYS_MAX_PATH] = "",
                    szExt[SYS_MAX_PATH] = "";

    MscSplitPath(pszSpoolFile, NULL, szFName, szExt);

    sprintf(SFH.szSpoolFile, "%s%s", szFName, szExt);

///////////////////////////////////////////////////////////////////////////////
//  Read SMTP domain ( 1st row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if (MscGetString(pSpoolFile, SFH.szSMTPDomain, sizeof(SFH.szSMTPDomain) - 1) == NULL)
    {
        fclose(pSpoolFile);
        ZeroData(SFH);

        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
        return (ERR_SPOOL_FILE_NOT_FOUND);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read message ID ( 2nd row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if (MscGetString(pSpoolFile, SFH.szMessageID, sizeof(SFH.szMessageID) - 1) == NULL)
    {
        fclose(pSpoolFile);
        ZeroData(SFH);

        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
        return (ERR_SPOOL_FILE_NOT_FOUND);
    }

    char            szSpoolLine[MAX_SPOOL_LINE] = "";

///////////////////////////////////////////////////////////////////////////////
//  Read "MAIL FROM:" ( 3th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (StrINComp(szSpoolLine, MAIL_FROM_STR) != 0) ||
            ((SFH.ppszFrom = USmtpGetPathStrings(szSpoolLine)) == NULL))
    {
        fclose(pSpoolFile);
        ZeroData(SFH);

        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
        return (ERR_INVALID_SPOOL_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read "RCPT TO:" ( 4th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (StrINComp(szSpoolLine, RCPT_TO_STR) != 0) ||
            ((SFH.ppszRcpt = USmtpGetPathStrings(szSpoolLine)) == NULL))
    {
        StrFreeStrings(SFH.ppszFrom);
        fclose(pSpoolFile);
        ZeroData(SFH);

        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE, pszSpoolFile);
        return (ERR_INVALID_SPOOL_FILE);
    }

    fclose(pSpoolFile);

    return (0);

}




void            USmlCleanupSpoolFileHeader(SpoolFileHeader & SFH)
{

    if (SFH.ppszRcpt != NULL)
        StrFreeStrings(SFH.ppszRcpt);

    if (SFH.ppszFrom != NULL)
        StrFreeStrings(SFH.ppszFrom);

    ZeroData(SFH);

}




static MessageTagData *USmlAllocTag(char const * pszTagName, char const * pszTagData)
{

    MessageTagData *pMTD = (MessageTagData *) SysAlloc(sizeof(MessageTagData));

    if (pMTD == NULL)
        return (NULL);

    ListLinkInit(pMTD);
    pMTD->pszTagName = SysStrDup(pszTagName);
    pMTD->pszTagData = SysStrDup(pszTagData);

    return (pMTD);

}



static void     USmlFreeTag(MessageTagData * pMTD)
{

    SysFree(pMTD->pszTagName);

    SysFree(pMTD->pszTagData);

    SysFree(pMTD);

}



static MessageTagData *USmlFindTag(HSLIST & hTagList, char const * pszTagName)
{

    MessageTagData *pMTD = (MessageTagData *) ListFirst(hTagList);

    for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
            ListNext(hTagList, (PLISTLINK) pMTD))
        if (strcmp(pMTD->pszTagName, pszTagName) == 0)
            return (pMTD);

    return (NULL);

}



static int      USmlAddTag(HSLIST & hTagList, char const * pszTagName,
                        char const * pszTagData, int iUpdate)
{

    if (!iUpdate)
    {
        MessageTagData *pMTD = USmlAllocTag(pszTagName, pszTagData);

        if (pMTD == NULL)
            return (ErrGetErrorCode());

        ListAddTail(hTagList, (PLISTLINK) pMTD);
    }
    else
    {
        MessageTagData *pMTD = USmlFindTag(hTagList, pszTagName);

        if (pMTD != NULL)
        {
            SysFree(pMTD->pszTagData);

            pMTD->pszTagData = SysStrDup(pszTagData);
        }
        else
        {
            if ((pMTD = USmlAllocTag(pszTagName, pszTagData)) == NULL)
                return (ErrGetErrorCode());

            ListAddTail(hTagList, (PLISTLINK) pMTD);
        }
    }

    return (0);

}



static void     USmlFreeTagsList(HSLIST & hTagList)
{

    MessageTagData *pMTD;

    while ((pMTD = (MessageTagData *) ListRemove(hTagList)) != INVALID_SLIST_PTR)
        USmlFreeTag(pMTD);

}



static int      USmlLoadTags(FILE * pSpoolFile, HSLIST & hTagList)
{

    DynString       TagDS;

    StrDynInit(&TagDS);


    unsigned long   ulFilePos = (unsigned long) ftell(pSpoolFile);
    char            szSpoolLine[MAX_SPOOL_LINE] = "",
                    szTagName[256] = "";

    while (MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) != NULL)
    {
        if (IsEmptyString(szSpoolLine))
        {
            if (StrDynSize(&TagDS) > 0)
            {
                if (USmlAddTag(hTagList, szTagName, StrDynGet(&TagDS)) < 0)
                {
                    ErrorPush();
                    StrDynFree(&TagDS);
                    fseek(pSpoolFile, ulFilePos, SEEK_SET);
                    return (ErrorPop());
                }

                SetEmptyString(szTagName);
                StrDynTruncate(&TagDS);
            }

            break;
        }

        if ((szSpoolLine[0] == ' ') || (szSpoolLine[0] == '\t'))
        {
            if (IsEmptyString(szTagName))
            {
                StrDynFree(&TagDS);
                fseek(pSpoolFile, ulFilePos, SEEK_SET);

                ErrSetErrorCode(ERR_INVALID_MESSAGE_FORMAT);
                return (ERR_INVALID_MESSAGE_FORMAT);
            }

            if ((StrDynAdd(&TagDS, "\r\n") < 0) ||
                    (StrDynAdd(&TagDS, szSpoolLine) < 0))
            {
                ErrorPush();
                StrDynFree(&TagDS);
                fseek(pSpoolFile, ulFilePos, SEEK_SET);
                return (ErrorPop());
            }
        }
        else
        {
            if (StrDynSize(&TagDS) > 0)
            {
                if (USmlAddTag(hTagList, szTagName, StrDynGet(&TagDS)) < 0)
                {
                    ErrorPush();
                    StrDynFree(&TagDS);
                    fseek(pSpoolFile, ulFilePos, SEEK_SET);
                    return (ErrorPop());
                }

                SetEmptyString(szTagName);
                StrDynTruncate(&TagDS);
            }

            char           *pszEndTag = strchr(szSpoolLine, ':');

            if (pszEndTag == NULL)
            {
                StrDynFree(&TagDS);
                fseek(pSpoolFile, ulFilePos, SEEK_SET);

                ErrSetErrorCode(ERR_INVALID_MESSAGE_FORMAT);
                return (ERR_INVALID_MESSAGE_FORMAT);
            }

            int             iNameLength = Min((int) (pszEndTag - szSpoolLine),
                    sizeof(szTagName) - 1);
            char           *pszTagValue = pszEndTag + 1;

            strncpy(szTagName, szSpoolLine, iNameLength);
            szTagName[iNameLength] = '\0';

            StrSkipSpaces(pszTagValue);

            if (StrDynAdd(&TagDS, pszTagValue) < 0)
            {
                ErrorPush();
                StrDynFree(&TagDS);
                fseek(pSpoolFile, ulFilePos, SEEK_SET);
                return (ErrorPop());
            }
        }

        ulFilePos = (unsigned long) ftell(pSpoolFile);
    }

    StrDynFree(&TagDS);

    return (0);

}



static int      USmlDumpHeaders(FILE * pMsgFile, HSLIST & hTagList)
{

    MessageTagData *pMTD = (MessageTagData *) ListFirst(hTagList);

    for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
            ListNext(hTagList, (PLISTLINK) pMTD))
    {

        fprintf(pMsgFile, "%s: %s\r\n", pMTD->pszTagName, pMTD->pszTagData);

    }

    return (0);

}



static void     USmlFreeData(SpoolFileData * pSFD)
{

    USmlFreeTagsList(pSFD->hTagList);

    if (pSFD->ppszFrom != NULL)
        StrFreeStrings(pSFD->ppszFrom);

    if (pSFD->pszMailFrom != NULL)
        SysFree(pSFD->pszMailFrom);

    if (pSFD->pszSendMailFrom != NULL)
        SysFree(pSFD->pszSendMailFrom);

    if (pSFD->ppszRcpt != NULL)
        StrFreeStrings(pSFD->ppszRcpt);

    if (pSFD->pszRcptTo != NULL)
        SysFree(pSFD->pszRcptTo);

    if (pSFD->pszSendRcptTo != NULL)
        SysFree(pSFD->pszSendRcptTo);

    if (pSFD->pszRelayDomain != NULL)
        SysFree(pSFD->pszRelayDomain);

    SysFree(pSFD);

}



char           *USmlAddrConcat(char const * const * ppszStrings)
{

    int             ii,
                    iStrCount = StrStringsCount(ppszStrings),
                    iSumLength = 0;

    for (ii = 0; ii < iStrCount; ii++)
        iSumLength += strlen(ppszStrings[ii]) + 1;


    char           *pszConcat = (char *) SysAlloc(iSumLength + 1);

    if (pszConcat == NULL)
        return (NULL);

    SetEmptyString(pszConcat);

    for (ii = 0; ii < iStrCount; ii++)
    {
        if (ii > 0)
            strcat(pszConcat, (ii == (iStrCount - 1)) ? ":" : ",");

        strcat(pszConcat, ppszStrings[ii]);
    }

    return (pszConcat);

}




char           *USmlBuildSendMailFrom(char const * const * ppszFrom, char const * const * ppszRcpt)
{

    int             iRcptCount = StrStringsCount(ppszRcpt),
                    iFromCount = StrStringsCount(ppszFrom);

    if (iRcptCount == 0)
    {
        ErrSetErrorCode(ERR_BAD_FORWARD_PATH);
        return (NULL);
    }

    if (iRcptCount == 1)
        return (USmlAddrConcat(ppszFrom));

    int             ii,
                    iSumLength = strlen(ppszRcpt[0]) + 1;

    for (ii = 0; ii < iFromCount; ii++)
        iSumLength += strlen(ppszFrom[ii]) + 1;


    char           *pszConcat = (char *) SysAlloc(iSumLength + 1);

    if (pszConcat == NULL)
        return (NULL);

    strcpy(pszConcat, ppszRcpt[0]);

    for (ii = 0; ii < iFromCount; ii++)
    {
        strcat(pszConcat, (ii == (iFromCount - 1)) ? ":" : ",");

        strcat(pszConcat, ppszFrom[ii]);
    }

    return (pszConcat);

}




char           *USmlBuildSendRcptTo(char const * const * ppszFrom, char const * const * ppszRcpt)
{

    int             iRcptCount = StrStringsCount(ppszRcpt),
                    iFromCount = StrStringsCount(ppszFrom);

    if (iRcptCount == 0)
    {
        ErrSetErrorCode(ERR_BAD_FORWARD_PATH);
        return (NULL);
    }

    if (iRcptCount == 1)
        return (USmlAddrConcat(ppszRcpt));

    int             ii,
                    iSumLength = 0;

    for (ii = 1; ii < iRcptCount; ii++)
        iSumLength += strlen(ppszRcpt[ii]) + 1;


    char           *pszConcat = (char *) SysAlloc(iSumLength + 1);

    if (pszConcat == NULL)
        return (NULL);

    SetEmptyString(pszConcat);

    for (ii = 1; ii < iRcptCount; ii++)
    {
        if (ii > 1)
            strcat(pszConcat, (ii == (iRcptCount - 1)) ? ":" : ",");

        strcat(pszConcat, ppszRcpt[ii]);
    }

    return (pszConcat);

}




SPLF_HANDLE     USmlCreateHandle(const char *pszMessFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Structure allocation and initialization
///////////////////////////////////////////////////////////////////////////////
    SpoolFileData  *pSFD = (SpoolFileData *) SysAlloc(sizeof(SpoolFileData));

    if (pSFD == NULL)
        return (INVALID_SPLF_HANDLE);

    pSFD->ppszFrom = NULL;
    pSFD->pszMailFrom = NULL;
    pSFD->pszSendMailFrom = NULL;
    pSFD->ppszRcpt = NULL;
    pSFD->pszRcptTo = NULL;
    pSFD->pszSendRcptTo = NULL;
    pSFD->pszRelayDomain = NULL;
    SetEmptyString(pSFD->szSMTPDomain);
    pSFD->ulFlags = 0;
    ListInit(pSFD->hTagList);

    StrSNCpy(pSFD->szMessFilePath, pszMessFilePath);


    char            szFName[SYS_MAX_PATH] = "",
                    szExt[SYS_MAX_PATH] = "";

    MscSplitPath(pszMessFilePath, NULL, szFName, szExt);

    sprintf(pSFD->szSpoolFile, "%s%s", szFName, szExt);


    FILE           *pSpoolFile = fopen(pszMessFilePath, "rb");

    if (pSpoolFile == NULL)
    {
        SysFree(pSFD);
        ErrSetErrorCode(ERR_SPOOL_FILE_NOT_FOUND);
        return (INVALID_SPLF_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read SMTP domain ( 1st row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if (MscGetString(pSpoolFile, pSFD->szSMTPDomain, sizeof(pSFD->szSMTPDomain) - 1) == NULL)
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read message ID ( 2nd row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if (MscGetString(pSpoolFile, pSFD->szMessageID, sizeof(pSFD->szMessageID) - 1) == NULL)
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

    char            szSpoolLine[MAX_SPOOL_LINE] = "";

///////////////////////////////////////////////////////////////////////////////
//  Read "MAIL FROM:" ( 3th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (StrINComp(szSpoolLine, MAIL_FROM_STR) != 0))
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

    if ((pSFD->ppszFrom = USmtpGetPathStrings(szSpoolLine)) == NULL)
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read "RCPT TO:" ( 4th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (StrINComp(szSpoolLine, RCPT_TO_STR) != 0))
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

    if ((pSFD->ppszRcpt = USmtpGetPathStrings(szSpoolLine)) == NULL)
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Check the presence of the init data mark ( 5th row of the spool file )
///////////////////////////////////////////////////////////////////////////////
    if ((MscGetString(pSpoolFile, szSpoolLine, sizeof(szSpoolLine) - 1) == NULL) ||
            (strncmp(szSpoolLine, SPOOL_FILE_DATA_START, strlen(SPOOL_FILE_DATA_START)) != 0))
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Get real message position
///////////////////////////////////////////////////////////////////////////////
    pSFD->ulMessageOffset = (unsigned long) ftell(pSpoolFile);

///////////////////////////////////////////////////////////////////////////////
//  Build address strings
///////////////////////////////////////////////////////////////////////////////
    if (((pSFD->pszMailFrom = USmlAddrConcat(pSFD->ppszFrom)) == NULL) ||
            ((pSFD->pszSendMailFrom = USmlBuildSendMailFrom(pSFD->ppszFrom, pSFD->ppszRcpt)) == NULL) ||
            ((pSFD->pszRcptTo = (char *) USmlAddrConcat(pSFD->ppszRcpt)) == NULL) ||
            ((pSFD->pszSendRcptTo = USmlBuildSendRcptTo(pSFD->ppszFrom, pSFD->ppszRcpt)) == NULL))
    {
        fclose(pSpoolFile);
        USmlFreeData(pSFD);
        ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
        return (INVALID_SPLF_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Check if it's a relay message
///////////////////////////////////////////////////////////////////////////////
    if (StrStringsCount(pSFD->ppszRcpt) > 1)
    {
        char            szRelayDomain[MAX_ADDR_NAME] = "";

        if (USmtpSplitEmailAddr(pSFD->ppszRcpt[0], NULL, szRelayDomain) < 0)
        {
            fclose(pSpoolFile);
            USmlFreeData(pSFD);
            ErrSetErrorCode(ERR_INVALID_SPOOL_FILE);
            return (INVALID_SPLF_HANDLE);
        }

        pSFD->pszRelayDomain = SysStrDup(szRelayDomain);
    }

///////////////////////////////////////////////////////////////////////////////
//  Load message tags
///////////////////////////////////////////////////////////////////////////////
    if (USmlLoadTags(pSpoolFile, pSFD->hTagList) < 0)
        SysLogMessage(LOG_LEV_MESSAGE, "Invalid headers section : %s\n",
                pSFD->szSpoolFile);

///////////////////////////////////////////////////////////////////////////////
//  Get spool file position
///////////////////////////////////////////////////////////////////////////////
    pSFD->ulMailDataOffset = (unsigned long) ftell(pSpoolFile);

    fclose(pSpoolFile);

    return ((SPLF_HANDLE) pSFD);

}



void            USmlCloseHandle(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;


    USmlFreeData(pSFD);

}




char const     *USmlGetRelayDomain(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->pszRelayDomain);

}



char const     *USmlGetSpoolFilePath(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->szMessFilePath);

}



char const     *USmlGetSpoolFile(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->szSpoolFile);

}



char const     *USmlGetSMTPDomain(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->szSMTPDomain);

}



char const     *USmlGetSmtpMessageID(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->szMessageID);

}



char const     *const * USmlGetMailFrom(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->ppszFrom);

}



char const     *USmlMailFrom(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->pszMailFrom);

}



char const     *USmlSendMailFrom(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->pszSendMailFrom);

}



char const     *const * USmlGetRcptTo(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->ppszRcpt);

}



char const     *USmlRcptTo(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->pszRcptTo);

}



char const     *USmlSendRcptTo(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    return (pSFD->pszSendRcptTo);

}



static int      USmlFlushMessageFile(SpoolFileData * pSFD)
{

    char            szTmpMsgFile[SYS_MAX_PATH] = "";

    SysSNPrintf(szTmpMsgFile, sizeof(szTmpMsgFile) - 1, "%s.flush", pSFD->szMessFilePath);

///////////////////////////////////////////////////////////////////////////////
//  Create temporary file
///////////////////////////////////////////////////////////////////////////////
    FILE           *pMsgFile = fopen(szTmpMsgFile, "wb");

    if (pMsgFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE, szTmpMsgFile);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Open message file
///////////////////////////////////////////////////////////////////////////////
    FILE           *pMessFile = fopen(pSFD->szMessFilePath, "rb");

    if (pMessFile == NULL)
    {
        fclose(pMsgFile);
        CheckRemoveFile(szTmpMsgFile);

        ErrSetErrorCode(ERR_FILE_OPEN, pSFD->szMessFilePath);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Dump info section ( start = 0 - bytes = ulMessageOffset )
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pMsgFile, pMessFile, 0, pSFD->ulMessageOffset) < 0)
    {
        ErrorPush();
        fclose(pMessFile);
        fclose(pMsgFile);
        CheckRemoveFile(szTmpMsgFile);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Dump message headers
///////////////////////////////////////////////////////////////////////////////
    if (USmlDumpHeaders(pMsgFile, pSFD->hTagList) < 0)
    {
        ErrorPush();
        fclose(pMessFile);
        fclose(pMsgFile);
        CheckRemoveFile(szTmpMsgFile);
        return (ErrorPop());
    }

    fprintf(pMsgFile, "\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Get the new message body offset
///////////////////////////////////////////////////////////////////////////////
    unsigned long       ulMailDataOffset = (unsigned long) ftell(pMsgFile);

///////////////////////////////////////////////////////////////////////////////
//  Dump message data ( start = ulMailDataOffset - bytes = -1 [EOF] )
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pMsgFile, pMessFile, pSFD->ulMailDataOffset, (unsigned long) -1) < 0)
    {
        ErrorPush();
        fclose(pMessFile);
        fclose(pMsgFile);
        CheckRemoveFile(szTmpMsgFile);
        return (ErrorPop());
    }

    fclose(pMessFile);

    if (SysFileSync(pMsgFile) < 0)
    {
        ErrorPush();
        fclose(pMsgFile);
        CheckRemoveFile(szTmpMsgFile);
        return (ErrorPop());
    }

    if (fclose(pMsgFile))
    {
        CheckRemoveFile(szTmpMsgFile);
        ErrSetErrorCode(ERR_FILE_WRITE, szTmpMsgFile);
        return (ERR_FILE_WRITE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Move the file
///////////////////////////////////////////////////////////////////////////////
    if ((SysRemove(pSFD->szMessFilePath) < 0) ||
            (SysMoveFile(szTmpMsgFile, pSFD->szMessFilePath) < 0))
    {
        ErrorPush();
        CheckRemoveFile(szTmpMsgFile);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Set the new message body offset
///////////////////////////////////////////////////////////////////////////////
    pSFD->ulMailDataOffset = ulMailDataOffset;


    return (0);

}



int             USmlSyncChanges(SPLF_HANDLE hFSpool)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    if (pSFD->ulFlags & SFF_HEADER_MODIFIED)
    {

        if (USmlFlushMessageFile(pSFD) == 0)
            pSFD->ulFlags &= ~SFF_HEADER_MODIFIED;

    }

    return (0);

}



int             USmlGetMsgFileSection(SPLF_HANDLE hFSpool, FileSection & FS)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

///////////////////////////////////////////////////////////////////////////////
//  Sync message file
///////////////////////////////////////////////////////////////////////////////
    if (USmlSyncChanges(hFSpool) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Setup file section fields
///////////////////////////////////////////////////////////////////////////////
    ZeroData(FS);

    StrSNCpy(FS.szFilePath, pSFD->szMessFilePath);

    FS.ulStartOffset = pSFD->ulMessageOffset;

    FS.ulEndOffset = (unsigned long) -1;


    return (0);

}



int             USmlWriteMailFile(SPLF_HANDLE hFSpool, FILE * pMsgFile)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

///////////////////////////////////////////////////////////////////////////////
//  Dump message tags
///////////////////////////////////////////////////////////////////////////////
    if (USmlDumpHeaders(pMsgFile, pSFD->hTagList) < 0)
        return (ErrGetErrorCode());

    fprintf(pMsgFile, "\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Dump message data
///////////////////////////////////////////////////////////////////////////////
    FILE           *pMessFile = fopen(pSFD->szMessFilePath, "rb");

    if (pMessFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pSFD->szMessFilePath);
        return (ERR_FILE_OPEN);
    }

    if (MscCopyFile(pMsgFile, pMessFile, pSFD->ulMailDataOffset, (unsigned long) -1) < 0)
    {
        ErrorPush();
        fclose(pMessFile);
        return (ErrorPop());
    }

    fclose(pMessFile);

    return (0);

}



char           *USmlGetTag(SPLF_HANDLE hFSpool, char const * pszTagName)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    MessageTagData *pMTD = USmlFindTag(pSFD->hTagList, pszTagName);

    return ((pMTD != NULL) ? SysStrDup(pMTD->pszTagData) : NULL);

}



int             USmlAddTag(SPLF_HANDLE hFSpool, char const * pszTagName,
                        char const * pszTagData, int iUpdate)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    if (USmlAddTag(pSFD->hTagList, pszTagName, pszTagData, iUpdate) < 0)
        return (ErrGetErrorCode());

    pSFD->ulFlags |= SFF_HEADER_MODIFIED;

    return (0);

}



int             USmlSetTagAddress(SPLF_HANDLE hFSpool, char const * pszTagName,
                        char const * pszAddress)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

    char           *pszOldAddress = USmlGetTag(hFSpool, pszTagName);

    if (pszOldAddress == NULL)
    {
        char            szTagData[512] = "";

        SysSNPrintf(szTagData, sizeof(szTagData) - 1, "<%s>", pszAddress);

        if (USmlAddTag(pSFD->hTagList, pszTagName, szTagData, 1) < 0)
            return (ErrGetErrorCode());

    }
    else
    {
        char           *pszOpen = strrchr(pszOldAddress, '<');

        if (pszOpen != NULL)
        {
///////////////////////////////////////////////////////////////////////////////
//  Case : NAME <ADDRESS>
///////////////////////////////////////////////////////////////////////////////
            char           *pszClose = strrchr(pszOpen + 1, '>');

            if (pszClose == NULL)
            {
                SysFree(pszOldAddress);

                ErrSetErrorCode(ERR_INVALID_MESSAGE_FORMAT);
                return (ERR_INVALID_MESSAGE_FORMAT);
            }


            DynString       DS;

            StrDynInit(&DS);

            StrDynAdd(&DS, pszOldAddress, (int) (pszOpen - pszOldAddress) + 1);
            StrDynAdd(&DS, pszAddress);
            StrDynAdd(&DS, pszClose);


            SysFree(pszOldAddress);

            if (USmlAddTag(pSFD->hTagList, pszTagName, StrDynGet(&DS), 1) < 0)
            {
                ErrorPush();
                StrDynFree(&DS);
                return (ErrorPop());
            }

            StrDynFree(&DS);

        }
        else
        {
///////////////////////////////////////////////////////////////////////////////
//  Case : ADDRESS
///////////////////////////////////////////////////////////////////////////////
            SysFree(pszOldAddress);

            if (USmlAddTag(pSFD->hTagList, pszTagName, pszAddress, 1) < 0)
                return (ErrGetErrorCode());

        }

    }

    return (0);

}



int             USmlMapAddress(char const * pszAddress, char *pszDomain, char *pszName)
{

    char            szRmtDomain[MAX_ADDR_NAME] = "",
                    szRmtName[MAX_ADDR_NAME] = "";

    if (USmtpSplitEmailAddr(pszAddress, szRmtName, szRmtDomain) < 0)
        return (ErrGetErrorCode());


    ExtAlias       *pExtAlias = ExAlGetAlias(szRmtDomain, szRmtName);

    if (pExtAlias == NULL)
        return (ErrGetErrorCode());


    strcpy(pszDomain, pExtAlias->pszDomain);
    strcpy(pszName, pExtAlias->pszName);


    ExAlFreeAlias(pExtAlias);

    return (0);

}




int             USmlCreateMBFile(UserInfo * pUI, char const * pszFileName,
                        SPLF_HANDLE hFSpool)
{

    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);

    FILE           *pMBFile = fopen(pszFileName, "wb");

    if (pMBFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Check the existence of the return path string ( PSYNC messages have )
///////////////////////////////////////////////////////////////////////////////
    char           *pszReturnPath = USmlGetTag(hFSpool, "Return-Path");

    if (pszReturnPath == NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  Build return path string
///////////////////////////////////////////////////////////////////////////////
        int             iFromDomains = StrStringsCount(ppszFrom);
        char            szDomain[MAX_ADDR_NAME] = "",
                        szName[MAX_ADDR_NAME] = "",
                        szReturnPath[1024] = "Return-Path: <>";

        if ((iFromDomains == 0) ||
                (USmlMapAddress(ppszFrom[iFromDomains - 1], szDomain, szName) < 0))
        {
            char           *pszRetPath = USmlAddrConcat(ppszFrom);

            if (pszRetPath != NULL)
            {
                int             iRetLength = strlen(pszRetPath),
                                iExtraLength = CStringSize("Return-Path: <>");

                if (iRetLength > (int) (sizeof(szReturnPath) - iExtraLength - 2))
                    pszRetPath[sizeof(szReturnPath) - iExtraLength - 2] = '\0';

                SysSNPrintf(szReturnPath, sizeof(szReturnPath) - 1,
                        "Return-Path: <%s>", pszRetPath);

                SysFree(pszRetPath);
            }
        }
        else
        {
            SysSNPrintf(szReturnPath, sizeof(szReturnPath) - 1, "Return-Path: <%s@%s>",
                    szName, szDomain);


            char            szAddress[MAX_ADDR_NAME] = "";

            SysSNPrintf(szAddress, sizeof(szAddress) - 1, "%s@%s", szName, szDomain);

            USmlSetTagAddress(hFSpool, "From", szAddress);
        }

        fprintf(pMBFile, "%s\r\n", szReturnPath);

///////////////////////////////////////////////////////////////////////////////
//  Add "Delivered-To:" tag
///////////////////////////////////////////////////////////////////////////////
        char            szUserAddress[MAX_ADDR_NAME] = "";

        UsrGetAddress(pUI, szUserAddress);

        fprintf(pMBFile, "Delivered-To: %s\r\n", szUserAddress);
    }
    else
        SysFree(pszReturnPath);

///////////////////////////////////////////////////////////////////////////////
//  Write mail file
///////////////////////////////////////////////////////////////////////////////
    if (USmlWriteMailFile(hFSpool, pMBFile) < 0)
    {
        ErrorPush();
        fclose(pMBFile);
        SysRemove(pszFileName);
        return (ErrorPop());
    }

    fclose(pMBFile);

    return (0);

}




int             USmlCreateSpoolFile(SPLF_HANDLE hFSpool, char const * pszFromUser,
                        char const * pszRcptUser, char const * pszFileName)
{

    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);


    FILE           *pSpoolFile = fopen(pszFileName, "wb");

    if (pSpoolFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Write SMTP domain
///////////////////////////////////////////////////////////////////////////////
    fprintf(pSpoolFile, "%s\r\n", pszSMTPDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write message ID
///////////////////////////////////////////////////////////////////////////////
    fprintf(pSpoolFile, "%s\r\n", pszSmtpMessageID);

///////////////////////////////////////////////////////////////////////////////
//  Write "MAIL FROM:"
///////////////////////////////////////////////////////////////////////////////
    char const     *pszMailFrom = USmlMailFrom(hFSpool);

    fprintf(pSpoolFile, "MAIL FROM: <%s>\r\n",
            (pszFromUser != NULL) ? pszFromUser : pszMailFrom);

///////////////////////////////////////////////////////////////////////////////
//  Write "RCPT TO:"
///////////////////////////////////////////////////////////////////////////////
    char const     *pszRcptTo = USmlRcptTo(hFSpool);

    fprintf(pSpoolFile, "RCPT TO: <%s>\r\n",
            (pszRcptUser != NULL) ? pszRcptUser : pszRcptTo);

///////////////////////////////////////////////////////////////////////////////
//  Write SPOOL_FILE_DATA_START
///////////////////////////////////////////////////////////////////////////////
    fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

///////////////////////////////////////////////////////////////////////////////
//  Than write mail data
///////////////////////////////////////////////////////////////////////////////
    if (USmlWriteMailFile(hFSpool, pSpoolFile) < 0)
    {
        ErrorPush();
        fclose(pSpoolFile);
        SysRemove(pszFileName);
        return (ErrorPop());
    }

    fclose(pSpoolFile);

    return (0);

}




int             USmlProcessLocalUserMessage(UserInfo * pUI, SPLF_HANDLE hFSpool,
                        QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, LocalMailProcConfig & LMPC)
{
///////////////////////////////////////////////////////////////////////////////
//  Exist user custom message processing ?
///////////////////////////////////////////////////////////////////////////////
    char            szMPFile[SYS_MAX_PATH] = "";

    if (UsrGetMailProcessFile(pUI, szMPFile) < 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  Create mailbox file ...
///////////////////////////////////////////////////////////////////////////////
        char            szMBFile[SYS_MAX_PATH] = "";

        SysGetTmpFile(szMBFile);

        if (USmlCreateMBFile(pUI, szMBFile, hFSpool) < 0)
            return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  and send it home
///////////////////////////////////////////////////////////////////////////////
        char const     *pszMessageID = USmlGetSpoolFile(hFSpool);

        if (UsrMoveToMailBox(pUI, szMBFile, pszMessageID) < 0)
        {
            ErrorPush();
            SysRemove(szMBFile);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Log operation
///////////////////////////////////////////////////////////////////////////////
        if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
        {
            char            szLocalAddress[MAX_ADDR_NAME] = "";

            USmlLogMessage(hFSpool, "LOCAL", UsrGetAddress(pUI, szLocalAddress));
        }
    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Process custom mailings
///////////////////////////////////////////////////////////////////////////////
        if (USmlProcessCustomMailingFile(pUI, hFSpool, hQueue, hMessage,
                        szMPFile, LMPC) < 0)
        {
            ErrorPush();
            SysRemove(szMPFile);
            return (ErrorPop());
        }

        SysRemove(szMPFile);
    }

    return (0);

}




static int      USmlProcessCustomMailingFile(UserInfo * pUI, SPLF_HANDLE hFSpool,
                        QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char const * pszMPFile,
                        LocalMailProcConfig & LMPC)
{

    FILE           *pMPFile = fopen(pszMPFile, "rt");

    if (pMPFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszMPFile);
        return (ERR_FILE_OPEN);
    }

    char            szCmdLine[CUSTOM_CMD_LINE_MAX] = "";

    while (MscGetConfigLine(szCmdLine, sizeof(szCmdLine) - 1, pMPFile) != NULL)
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
            USmlCmdMacroSubstitutes(ppszCmdTokens, pUI, hFSpool);


            if (stricmp(ppszCmdTokens[0], "external") == 0)
                USmlCmd_external(ppszCmdTokens, iFieldsCount, pUI, hFSpool,
                        hQueue, hMessage, LMPC);
            else if (stricmp(ppszCmdTokens[0], "wait") == 0)
                USmlCmd_wait(ppszCmdTokens, iFieldsCount, pUI, hFSpool,
                        hQueue, hMessage, LMPC);
            else if (stricmp(ppszCmdTokens[0], "mailbox") == 0)
                USmlCmd_mailbox(ppszCmdTokens, iFieldsCount, pUI, hFSpool,
                        hQueue, hMessage, LMPC);
            else if (stricmp(ppszCmdTokens[0], "redirect") == 0)
                USmlCmd_redirect(ppszCmdTokens, iFieldsCount, pUI, hFSpool,
                        hQueue, hMessage, LMPC);
            else if (stricmp(ppszCmdTokens[0], "lredirect") == 0)
                USmlCmd_lredirect(ppszCmdTokens, iFieldsCount, pUI, hFSpool,
                        hQueue, hMessage, LMPC);
            else
            {
                SysLogMessage(LOG_LEV_ERROR, "Invalid command \"%s\" in file \"%s\"\n",
                        ppszCmdTokens[0], pszMPFile);

            }

        }

        StrFreeStrings(ppszCmdTokens);
    }

    fclose(pMPFile);

    return (0);

}




static int      USmlCmdMacroSubstitutes(char **ppszCmdTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool)
{

    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    char const     *const * ppszRcpt = USmlGetRcptTo(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *pszMessageID = USmlGetSpoolFile(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom),
                    iRcptDomains = StrStringsCount(ppszRcpt);
    FileSection     FS;

///////////////////////////////////////////////////////////////////////////////
//  This function retrieve the spool file message section and sync the content.
//  This is necessary before passing the file name to external commands.
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
        else if (strcmp(ppszCmdTokens[ii], "@@TMPFILE") == 0)
        {
            char            szTmpFile[SYS_MAX_PATH] = "";

            SysGetTmpFile(szTmpFile);

            if (MscCopyFile(szTmpFile, FS.szFilePath) < 0)
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




static int      USmlCmd_external(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC)
{

    if (iNumTokens < 5)
    {
        ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
        return (ERR_BAD_MAILPROC_CMD_SYNTAX);
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

        QueUtErrLogMessage(hQueue, hMessage,
                "USMAIL EXTRN-Send Prg = \"%s\" From = \"%s\" To = \"%s\" Failed !\n",
                ppszCmdTokens[3], pszMailFrom, pszRcptTo);

        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Log operation
///////////////////////////////////////////////////////////////////////////////
    if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
        USmlLogMessage(hFSpool, "EXTRN", ppszCmdTokens[3]);

    return (0);

}




static int      USmlCmd_wait(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC)
{

    if (iNumTokens != 2)
    {
        ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
        return (ERR_BAD_MAILPROC_CMD_SYNTAX);
    }

    int             iWaitTimeout = atoi(ppszCmdTokens[1]);

    SysSleep(iWaitTimeout);

    return (0);

}




static int      USmlCmd_mailbox(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC)
{

    if (iNumTokens != 1)
    {
        ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
        return (ERR_BAD_MAILPROC_CMD_SYNTAX);
    }

///////////////////////////////////////////////////////////////////////////////
//  Create mailbox file ...
///////////////////////////////////////////////////////////////////////////////
    char            szMBFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szMBFile);

    if (USmlCreateMBFile(pUI, szMBFile, hFSpool) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  and send it home
///////////////////////////////////////////////////////////////////////////////
    char const     *pszMessageID = USmlGetSpoolFile(hFSpool);

    if (UsrMoveToMailBox(pUI, szMBFile, pszMessageID) < 0)
    {
        ErrorPush();
        SysRemove(szMBFile);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Log operation
///////////////////////////////////////////////////////////////////////////////
    if (LMPC.ulFlags & LMPCF_LOG_ENABLED)
    {
        char            szLocalAddress[MAX_ADDR_NAME] = "";

        USmlLogMessage(hFSpool, "LOCAL", UsrGetAddress(pUI, szLocalAddress));
    }

    return (0);

}




static int      USmlCmd_redirect(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC)
{

    if (iNumTokens < 2)
    {
        ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
        return (ERR_BAD_MAILPROC_CMD_SYNTAX);
    }

///////////////////////////////////////////////////////////////////////////////
//  Redirection loop
///////////////////////////////////////////////////////////////////////////////
    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);

    for (int ii = 1; ppszCmdTokens[ii] != NULL; ii++)
    {
///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
        QMSG_HANDLE     hRedirMessage = QueCreateMessage(hSpoolQueue);

        if (hRedirMessage == INVALID_QMSG_HANDLE)
            return (ErrGetErrorCode());


        char            szQueueFilePath[SYS_MAX_PATH] = "";

        QueGetFilePath(hSpoolQueue, hRedirMessage, szQueueFilePath);


        if (USmlCreateSpoolFile(hFSpool, NULL, ppszCmdTokens[ii], szQueueFilePath) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hRedirMessage);
            QueCloseMessage(hSpoolQueue, hRedirMessage);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitMessage(hSpoolQueue, hRedirMessage) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hRedirMessage);
            QueCloseMessage(hSpoolQueue, hRedirMessage);
            return (ErrorPop());
        }
    }

    return (0);

}



static int      USmlCmd_lredirect(char **ppszCmdTokens, int iNumTokens, UserInfo * pUI,
                        SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        LocalMailProcConfig & LMPC)
{

    if (iNumTokens < 2)
    {
        ErrSetErrorCode(ERR_BAD_MAILPROC_CMD_SYNTAX);
        return (ERR_BAD_MAILPROC_CMD_SYNTAX);
    }

    char            szUserAddress[MAX_ADDR_NAME] = "";

    UsrGetAddress(pUI, szUserAddress);

///////////////////////////////////////////////////////////////////////////////
//  Redirection loop
///////////////////////////////////////////////////////////////////////////////
    for (int ii = 1; ppszCmdTokens[ii] != NULL; ii++)
    {
///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
        QMSG_HANDLE     hRedirMessage = QueCreateMessage(hSpoolQueue);

        if (hRedirMessage == INVALID_QMSG_HANDLE)
            return (ErrGetErrorCode());


        char            szQueueFilePath[SYS_MAX_PATH] = "";

        QueGetFilePath(hSpoolQueue, hRedirMessage, szQueueFilePath);


        if (USmlCreateSpoolFile(hFSpool, szUserAddress, ppszCmdTokens[ii], szQueueFilePath) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hRedirMessage);
            QueCloseMessage(hSpoolQueue, hRedirMessage);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitMessage(hSpoolQueue, hRedirMessage) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hRedirMessage);
            QueCloseMessage(hSpoolQueue, hRedirMessage);
            return (ErrorPop());
        }
    }

    return (0);

}




int             USmlGetDomainCustomDir(char *pszCustomDir, int iFinalSlash)
{

    CfgGetRootPath(pszCustomDir);

    strcat(pszCustomDir, SMAIL_DOMAIN_PROC_DIR);
    if (iFinalSlash)
        AppendSlash(pszCustomDir);

    return (0);

}



int             USmlDomainCustomFileName(char const * pszDestDomain, char *pszCustFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Make domain name lower case
///////////////////////////////////////////////////////////////////////////////
    char            szDestDomain[MAX_HOST_NAME] = "";

    StrSNCpy(szDestDomain, pszDestDomain);
    StrLower(szDestDomain);

///////////////////////////////////////////////////////////////////////////////
//  Build file name
///////////////////////////////////////////////////////////////////////////////
    char            szCustomDir[SYS_MAX_PATH] = "";

    USmlGetDomainCustomDir(szCustomDir);


    sprintf(pszCustFilePath, "%s%s.tab", szCustomDir, szDestDomain);

    return (0);

}



int             USmlGetDomainCustomFile(char const * pszDestDomain, char *pszCustFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Make domain name lower case
///////////////////////////////////////////////////////////////////////////////
    char            szDestDomain[MAX_HOST_NAME] = "";

    StrSNCpy(szDestDomain, pszDestDomain);
    StrLower(szDestDomain);

///////////////////////////////////////////////////////////////////////////////
//  Lookup custom files
///////////////////////////////////////////////////////////////////////////////
    char            szCustomDir[SYS_MAX_PATH] = "";

    USmlGetDomainCustomDir(szCustomDir);

    for (char const * pszSubDom = szDestDomain; pszSubDom != NULL;
            pszSubDom = strchr(pszSubDom + 1, '.'))
    {
        sprintf(pszCustFilePath, "%s%s.tab", szCustomDir, pszSubDom);

        if (SysExistFile(pszCustFilePath))
            return (0);

    }

    sprintf(pszCustFilePath, "%s.tab", szCustomDir);

    if (SysExistFile(pszCustFilePath))
        return (0);


    ErrSetErrorCode(ERR_NOT_A_CUSTOM_DOMAIN);
    return (ERR_NOT_A_CUSTOM_DOMAIN);

}




int             USmlGetDomainCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        char *pszCustFilePath)
{

    return (QueGetFilePath(hQueue, hMessage, pszCustFilePath, QUEUE_CUST_DIR));

}




int             USmlGetDomainMsgCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
                        QMSG_HANDLE hMessage, char const * pszDestDomain, char *pszCustFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Check if exist a spooled copy
///////////////////////////////////////////////////////////////////////////////
    char const     *pszSpoolFilePath = USmlGetSpoolFilePath(hFSpool);

    USmlGetDomainCustomSpoolFile(hQueue, hMessage, pszCustFilePath);

    if (SysExistFile(pszCustFilePath))
        return (0);

///////////////////////////////////////////////////////////////////////////////
//  Check if this is a custom domain
///////////////////////////////////////////////////////////////////////////////
    char            szCustDomainFile[SYS_MAX_PATH] = "";

    if (USmlGetDomainCustomFile(pszDestDomain, szCustDomainFile) < 0)
        return (ErrGetErrorCode());


    RLCK_HANDLE     hResLock = RLckLockSH(szCustDomainFile);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Make a copy into the spool
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pszCustFilePath, szCustDomainFile) < 0)
    {
        ErrorPush();
        RLckUnlockSH(hResLock);
        return (ErrorPop());
    }


    RLckUnlockSH(hResLock);

    return (0);

}



int             USmlGetCustomDomainFile(char const * pszDestDomain, char const * pszCustFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Check if this is a custom domain
///////////////////////////////////////////////////////////////////////////////
    char            szCustDomainFile[SYS_MAX_PATH] = "";

    if (USmlGetDomainCustomFile(pszDestDomain, szCustDomainFile) < 0)
        return (ErrGetErrorCode());


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szCustDomainFile, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Make a copy onto user supplied file
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pszCustFilePath, szCustDomainFile) < 0)
    {
        ErrorPush();
        RLckUnlockSH(hResLock);
        return (ErrorPop());
    }


    RLckUnlockSH(hResLock);

    return (0);

}



int             USmlSetCustomDomainFile(char const * pszDestDomain, char const * pszCustFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Check if this is a custom domain
///////////////////////////////////////////////////////////////////////////////
    char            szCustDomainFile[SYS_MAX_PATH] = "";

    USmlDomainCustomFileName(pszDestDomain, szCustDomainFile);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szCustDomainFile, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    if (pszCustFilePath != NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  Overwrite current file
///////////////////////////////////////////////////////////////////////////////
        if (MscCopyFile(szCustDomainFile, pszCustFilePath) < 0)
        {
            ErrorPush();
            RLckUnlockEX(hResLock);
            return (ErrorPop());
        }
    }
    else
        SysRemove(szCustDomainFile);


    RLckUnlockEX(hResLock);

    return (0);

}



int             USmlGetMessageFilterFile(char const * pszDomain, char const * pszUser,
                        char *pszFilterFilePath)
{

    char            szMailRootPath[SYS_MAX_PATH] = "";

    CfgGetRootPath(szMailRootPath);

///////////////////////////////////////////////////////////////////////////////
//  Get lower case user and domain
///////////////////////////////////////////////////////////////////////////////
    char            szLoUser[MAX_ADDR_NAME] = "",
                    szLoDomain[MAX_ADDR_NAME] = "";

    StrSNCpy(szLoUser, pszUser);
    StrLower(szLoUser);

    StrSNCpy(szLoDomain, pszDomain);
    StrLower(szLoDomain);

///////////////////////////////////////////////////////////////////////////////
//  Check for user specific file
///////////////////////////////////////////////////////////////////////////////
    sprintf(pszFilterFilePath, "%s%s%s%s@%s.tab", szMailRootPath,
            SMAIL_DOMAIN_FILTER_DIR, SYS_SLASH_STR, szLoUser, szLoDomain);

    if (!SysExistFile(pszFilterFilePath))
    {
///////////////////////////////////////////////////////////////////////////////
//  Check for domain file
///////////////////////////////////////////////////////////////////////////////
        sprintf(pszFilterFilePath, "%s%s%s%s.tab", szMailRootPath,
                SMAIL_DOMAIN_FILTER_DIR, SYS_SLASH_STR, szLoDomain);

        if (!SysExistFile(pszFilterFilePath))
        {
///////////////////////////////////////////////////////////////////////////////
//  Check for default file
///////////////////////////////////////////////////////////////////////////////
            sprintf(pszFilterFilePath, "%s%s%s%s", szMailRootPath,
                    SMAIL_DOMAIN_FILTER_DIR, SYS_SLASH_STR, SMAIL_DEFAULT_FILTER);

            if (!SysExistFile(pszFilterFilePath))
            {
                ErrSetErrorCode(ERR_NO_DOMAIN_FILTER);
                return (ERR_NO_DOMAIN_FILTER);
            }
        }
    }

    return (0);

}



int             USmlCustomizedDomain(char const * pszDestDomain)
{

    char            szCustFilePath[SYS_MAX_PATH] = "";

    return (USmlGetDomainCustomFile(pszDestDomain, szCustFilePath));

}




static int      USmlLogMessage(char const * pszSMTPDomain, char const * pszMessageID,
                        char const * pszSmtpMessageID, char const * pszFrom, char const * pszRcpt,
                        char const * pszMedium, char const * pszParam)
{

    char            szTime[256] = "";

    MscGetTimeNbrString(szTime, sizeof(szTime) - 1);


    RLCK_HANDLE     hResLock = RLckLockEX(SVR_LOGS_DIR SYS_SLASH_STR SMAIL_LOG_FILE);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    MscFileLog(SMAIL_LOG_FILE, "\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\t\"%s\""
            "\n", pszSMTPDomain, pszMessageID, pszSmtpMessageID, pszFrom, pszRcpt,
            pszMedium, pszParam, szTime);


    RLckUnlockEX(hResLock);

    return (0);

}



int             USmlLogMessage(SPLF_HANDLE hFSpool, char const * pszMedium, char const * pszParam)
{

    char const     *pszSMTPDomain = USmlGetSMTPDomain(hFSpool);
    char const     *pszSmtpMessageID = USmlGetSmtpMessageID(hFSpool);
    char const     *pszMessageID = USmlGetSpoolFile(hFSpool);
    char const     *pszMailFrom = USmlMailFrom(hFSpool);
    char const     *pszRcptTo = USmlRcptTo(hFSpool);


    USmlLogMessage(pszSMTPDomain, pszMessageID, pszSmtpMessageID, pszMailFrom, pszRcptTo,
            pszMedium, pszParam);


    return (0);

}



int             USmlParseAddress(char const * pszAddress, char *pszPreAddr,
                        int iMaxPreAddress, char *pszEmailAddr, int iMaxAddress)
{

    for (; (*pszAddress == ' ') || (*pszAddress == '\t'); pszAddress++);

    if (*pszAddress == '\0')
    {
        ErrSetErrorCode(ERR_BAD_TAG_ADDRESS);
        return (ERR_BAD_TAG_ADDRESS);
    }


    char const     *pszOpen = strrchr(pszAddress, '<');

    if (pszOpen != NULL)
    {
        if (pszPreAddr != NULL)
        {
            int             iPreCount = Min((int) (pszOpen - pszAddress), iMaxPreAddress - 1);

            strncpy(pszPreAddr, pszAddress, iPreCount);
            pszPreAddr[iPreCount] = '\0';

            StrTrim(pszPreAddr, " \t");
        }

        if (pszEmailAddr != NULL)
        {
            char const     *pszClose = strrchr(pszAddress, '>');

            if (pszClose == NULL)
            {
                ErrSetErrorCode(ERR_BAD_TAG_ADDRESS);
                return (ERR_BAD_TAG_ADDRESS);
            }

            int             iEmailCount = (int) Min((pszClose - pszOpen) - 1, iMaxAddress - 1);

            if (iEmailCount < 0)
            {
                ErrSetErrorCode(ERR_BAD_TAG_ADDRESS);
                return (ERR_BAD_TAG_ADDRESS);
            }

            strncpy(pszEmailAddr, pszOpen + 1, iEmailCount);
            pszEmailAddr[iEmailCount] = '\0';

            StrTrim(pszEmailAddr, " \t");
        }
    }
    else
    {
        if (pszPreAddr != NULL)
            SetEmptyString(pszPreAddr);

        if (pszEmailAddr != NULL)
        {
            strncpy(pszEmailAddr, pszAddress, iMaxAddress - 1);
            pszEmailAddr[iMaxAddress - 1] = '\0';

            StrTrim(pszEmailAddr, " \t");
        }
    }

    return (0);

}



static int      USmlExtractFromAddress(HSLIST & hTagList, char *pszFromAddr,
                        int iMaxAddress)
{
///////////////////////////////////////////////////////////////////////////////
//  Try to discover the "Return-Path" ( or eventually "From" ) tag to setup
//  the "MAIL FROM: <>" part of the spool message
///////////////////////////////////////////////////////////////////////////////
    MessageTagData *pMTD = USmlFindTag(hTagList, "Return-Path");

    if ((pMTD != NULL) &&
            (USmlParseAddress(pMTD->pszTagData, NULL, 0, pszFromAddr, iMaxAddress) == 0))
        return (0);


    if (((pMTD = USmlFindTag(hTagList, "From")) != NULL) &&
            (USmlParseAddress(pMTD->pszTagData, NULL, 0, pszFromAddr, iMaxAddress) == 0))
        return (0);


    ErrSetErrorCode(ERR_MAILFROM_UNKNOWN);
    return (ERR_MAILFROM_UNKNOWN);

}



static char const *USmlAddressFromAtPtr(char const * pszAt, char const * pszBase,
                        char *pszAddress, int iMaxAddress)
{

    char const     *pszStart = pszAt;

    for (; (pszStart >= pszBase) && (strchr("<> \t,\":;'\r\n", *pszStart) == NULL); pszStart--);

    ++pszStart;

    char const     *pszEnd = pszAt + 1;

    for (; (*pszEnd != '\0') && (strchr("<> \t,\":;'\r\n", *pszEnd) == NULL); pszEnd++);

    int             iAddrLength = Min((int) (pszEnd - pszStart), iMaxAddress - 1);

    strncpy(pszAddress, pszStart, iAddrLength);
    pszAddress[iAddrLength] = '\0';

    return (pszEnd);

}



static int      USmlAddAddresses(char const * pszAddrList, DynString * pAddrDS,
                        char const * const * ppszMatchDomains)
{

    char const     *pszCurr = pszAddrList;

    for (; (pszCurr != NULL) && (*pszCurr != '\0');)
    {
        char const     *pszAt = strchr(pszCurr, '@');

        if (pszAt == NULL)
            break;


        char            szAddress[MAX_SMTP_ADDRESS] = "",
                        szDomain[MAX_ADDR_NAME] = "";

        if (((pszCurr = USmlAddressFromAtPtr(pszAt, pszAddrList, szAddress,
                                sizeof(szAddress) - 1)) != NULL) &&
                (USmtpSplitEmailAddr(szAddress, NULL, szDomain) == 0) &&
                ((ppszMatchDomains == NULL) || StrStringsIMatch(ppszMatchDomains, szDomain)))
        {
            if (StrDynSize(pAddrDS) > 0)
                StrDynAdd(pAddrDS, ADDRESS_TOKENIZER);

            StrDynAdd(pAddrDS, szAddress);
        }

    }

    return (0);

}



static char   **USmlGetAddressList(HSLIST & hTagList, char const * const * ppszMatchDomains, ...)
{

    DynString       AddrDS;

    StrDynInit(&AddrDS);


    char const     *pszTag = NULL;
    va_list         Args;

    va_start(Args, ppszMatchDomains);

    while ((pszTag = va_arg(Args, char *)) != NULL)
    {
        MessageTagData *pMTD = USmlFindTag(hTagList, pszTag);

        if (pMTD != NULL)
            USmlAddAddresses(pMTD->pszTagData, &AddrDS, ppszMatchDomains);

    }

    va_end(Args);


    char          **ppszAddresses = StrTokenize(StrDynGet(&AddrDS), ADDRESS_TOKENIZER);


    StrDynFree(&AddrDS);


    return (ppszAddresses);

}



static int      USmlExtractToAddress(HSLIST & hTagList, char *pszToAddr, int iMaxAddress)
{
///////////////////////////////////////////////////////////////////////////////
//  Try to extract the "To:" tag from the mail headers
///////////////////////////////////////////////////////////////////////////////
    MessageTagData *pMTD = USmlFindTag(hTagList, "To");

    if ((pMTD == NULL) ||
            (USmlParseAddress(pMTD->pszTagData, NULL, 0, pszToAddr, iMaxAddress) < 0))
    {
        ErrSetErrorCode(ERR_RCPTTO_UNKNOWN);
        return (ERR_RCPTTO_UNKNOWN);
    }

    return (0);

}



static char   **USmlBuildTargetRcptList(char const * pszRcptTo, HSLIST & hTagList)
{

    char          **ppszRcptList = NULL;

    if (pszRcptTo == NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  If the recipient is NULL try to extract the "To:", "Cc:" and "Bcc:"
//  addresses from the message
///////////////////////////////////////////////////////////////////////////////
        if ((ppszRcptList = USmlGetAddressList(hTagList, NULL,
                                "To", "Cc", "Bcc", NULL)) == NULL)
            return (NULL);


    }
    else if (*pszRcptTo == '?')
    {
///////////////////////////////////////////////////////////////////////////////
//  Extract matching domains
///////////////////////////////////////////////////////////////////////////////
        char          **ppszDomains = StrTokenize(pszRcptTo, ",");

        if (ppszDomains == NULL)
            return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  We need to masquerade incoming domain. In this case "pszRcptTo" is made by
//  "?" + masquerade-domain
///////////////////////////////////////////////////////////////////////////////
        if ((ppszRcptList = USmlGetAddressList(hTagList, &ppszDomains[1],
                                "To", "Cc", "Bcc", NULL)) == NULL)
        {
            StrFreeStrings(ppszDomains);
            return (NULL);
        }

        StrFreeStrings(ppszDomains);


        int             iAddrCount = StrStringsCount(ppszRcptList);

        for (int ii = 0; ii < iAddrCount; ii++)
        {
            char            szToUser[MAX_ADDR_NAME] = "",
                            szRecipient[MAX_ADDR_NAME] = "";

            if (USmtpSplitEmailAddr(ppszRcptList[ii], szToUser, NULL) == 0)
            {
                SysSNPrintf(szRecipient, sizeof(szRecipient) - 1, "%s@%s",
                        szToUser, pszRcptTo + 1);

                SysFree(ppszRcptList[ii]);

                ppszRcptList[ii] = SysStrDup(szRecipient);
            }
        }

    }
    else if (*pszRcptTo == '&')
    {
///////////////////////////////////////////////////////////////////////////////
//  Extract matching domains
///////////////////////////////////////////////////////////////////////////////
        char          **ppszDomains = StrTokenize(pszRcptTo, ",");

        if (ppszDomains == NULL)
            return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  We need to masquerade incoming domain. In this case "pszRcptTo" is made by
//  "&" + add-domain
///////////////////////////////////////////////////////////////////////////////
        if ((ppszRcptList = USmlGetAddressList(hTagList, &ppszDomains[1],
                                "To", "Cc", "Bcc", NULL)) == NULL)
        {
            StrFreeStrings(ppszDomains);
            return (NULL);
        }

        StrFreeStrings(ppszDomains);


        int             iAddrCount = StrStringsCount(ppszRcptList);

        for (int ii = 0; ii < iAddrCount; ii++)
        {
            char            szRecipient[MAX_ADDR_NAME] = "";

            SysSNPrintf(szRecipient, sizeof(szRecipient) - 1, "%s%s",
                    ppszRcptList[ii], pszRcptTo + 1);

            SysFree(ppszRcptList[ii]);

            ppszRcptList[ii] = SysStrDup(szRecipient);
        }

    }
    else
        ppszRcptList = StrBuildList(pszRcptTo, NULL);


    return (ppszRcptList);

}



int             USmlDeliverFetchedMsg(char const * pszSyncAddr, char const * pszMailFile)
{

    FILE           *pMailFile = fopen(pszMailFile, "rb");

    if (pMailFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Load message tags
///////////////////////////////////////////////////////////////////////////////
    HSLIST          hTagList;

    ListInit(hTagList);

    if (USmlLoadTags(pMailFile, hTagList) < 0)
    {
        ErrorPush();
        fclose(pMailFile);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Extract "MAIL FROM: <>" address
///////////////////////////////////////////////////////////////////////////////
    char            szFromAddr[MAX_SMTP_ADDRESS] = "";

    USmlExtractFromAddress(hTagList, szFromAddr, sizeof(szFromAddr) - 1);

///////////////////////////////////////////////////////////////////////////////
//  Extract recipient list
///////////////////////////////////////////////////////////////////////////////
    char          **ppszRcptList = USmlBuildTargetRcptList(pszSyncAddr, hTagList);

    if (ppszRcptList == NULL)
    {
        ErrorPush();
        USmlFreeTagsList(hTagList);
        fclose(pMailFile);
        return (ErrorPop());
    }

    USmlFreeTagsList(hTagList);

///////////////////////////////////////////////////////////////////////////////
//  Loop through extracted recipients and deliver
///////////////////////////////////////////////////////////////////////////////
    int             iDeliverCount = 0,
                    iAddrCount = StrStringsCount(ppszRcptList);

    for (int ii = 0; ii < iAddrCount; ii++)
    {
///////////////////////////////////////////////////////////////////////////////
//  Check address validity and skip invalid ( or not handled ) ones
///////////////////////////////////////////////////////////////////////////////
        char            szDestDomain[MAX_HOST_NAME] = "";

        if ((USmtpSplitEmailAddr(ppszRcptList[ii], NULL, szDestDomain) < 0) ||
                ((MDomIsHandledDomain(szDestDomain) < 0) &&
                        (USmlCustomizedDomain(szDestDomain) < 0)))
            continue;


///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
        QMSG_HANDLE     hMessage = QueCreateMessage(hSpoolQueue);

        if (hMessage == INVALID_QMSG_HANDLE)
        {
            ErrorPush();
            StrFreeStrings(ppszRcptList);
            fclose(pMailFile);
            return (ErrorPop());
        }


        char            szQueueFilePath[SYS_MAX_PATH] = "";

        QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);


        if (USmlCreateSpoolFile(pMailFile, szFromAddr, ppszRcptList[ii], szQueueFilePath) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszRcptList);
            fclose(pMailFile);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Transfer file to the spool
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitMessage(hSpoolQueue, hMessage) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            StrFreeStrings(ppszRcptList);
            fclose(pMailFile);
            return (ErrorPop());
        }

        ++iDeliverCount;
    }

    StrFreeStrings(ppszRcptList);

    fclose(pMailFile);

///////////////////////////////////////////////////////////////////////////////
//  Check if the message has been delivered at least one time
///////////////////////////////////////////////////////////////////////////////
    if (iDeliverCount == 0)
    {
        ErrSetErrorCode(ERR_FETCHMSG_UNDELIVERED);
        return (ERR_FETCHMSG_UNDELIVERED);
    }

    return (0);

}



static int      USmlCreateSpoolFile(FILE * pMailFile, char const * pszMailFrom,
                        char const * pszRcptTo, char const * pszSpoolFile)
{

    FILE           *pSpoolFile = fopen(pszSpoolFile, "wb");

    if (pSpoolFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE, pszSpoolFile);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Write SMTP domain
///////////////////////////////////////////////////////////////////////////////
    char            szSmtpDomain[MAX_HOST_NAME] = "";

    if (USmtpSplitEmailAddr(pszRcptTo, NULL, szSmtpDomain) < 0)
    {
        ErrorPush();
        fclose(pSpoolFile);
        SysRemove(pszSpoolFile);
        return (ErrorPop());
    }

    fprintf(pSpoolFile, "%s\r\n", szSmtpDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write message ID
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT64      ullMessageID = 0;

    if (SvrGetMessageID(&ullMessageID) < 0)
    {
        ErrorPush();
        fclose(pSpoolFile);
        SysRemove(pszSpoolFile);
        return (ErrorPop());
    }

    fprintf(pSpoolFile, "P" SYS_LLX_FMT "\r\n", ullMessageID);

///////////////////////////////////////////////////////////////////////////////
//  Write "MAIL FROM:"
///////////////////////////////////////////////////////////////////////////////
    fprintf(pSpoolFile, "MAIL FROM: <%s>\r\n", pszMailFrom);

///////////////////////////////////////////////////////////////////////////////
//  Write "RCPT TO:"
///////////////////////////////////////////////////////////////////////////////
    fprintf(pSpoolFile, "RCPT TO: <%s>\r\n", pszRcptTo);

///////////////////////////////////////////////////////////////////////////////
//  Write SPOOL_FILE_DATA_START
///////////////////////////////////////////////////////////////////////////////
    fprintf(pSpoolFile, "%s\r\n", SPOOL_FILE_DATA_START);

///////////////////////////////////////////////////////////////////////////////
//  Write message body
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pSpoolFile, pMailFile, 0, (unsigned long) -1) < 0)
    {
        ErrorPush();
        fclose(pSpoolFile);
        SysRemove(pszSpoolFile);
        return (ErrorPop());
    }

    fclose(pSpoolFile);

    return (0);

}



int             USmlMailLoopCheck(SPLF_HANDLE hFSpool, SVRCFG_HANDLE hSvrConfig)
{

    SpoolFileData  *pSFD = (SpoolFileData *) hFSpool;

///////////////////////////////////////////////////////////////////////////////
//  Count MTA ops
///////////////////////////////////////////////////////////////////////////////
    int             iReceivedCount = 0,
                    iMaxMTAOps = SvrGetConfigInt("MaxMTAOps", MAX_MTA_OPS, hSvrConfig);
    MessageTagData *pMTD = (MessageTagData *) ListFirst(pSFD->hTagList);

    for (; pMTD != INVALID_SLIST_PTR; pMTD = (MessageTagData *)
            ListNext(pSFD->hTagList, (PLISTLINK) pMTD))
        if (stricmp(pMTD->pszTagName, "received") == 0)
            ++iReceivedCount;

///////////////////////////////////////////////////////////////////////////////
//  Check MTA count
///////////////////////////////////////////////////////////////////////////////
    if (iReceivedCount > iMaxMTAOps)
    {
        ErrSetErrorCode(ERR_MAIL_LOOP_DETECTED);
        return (ERR_MAIL_LOOP_DETECTED);
    }


    return (0);

}
