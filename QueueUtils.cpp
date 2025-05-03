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
#include "StrUtils.h"
#include "SList.h"
#include "MD5.h"
#include "Base64Enc.h"
#include "BuffSock.h"
#include "MessQueue.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SMTPUtils.h"
#include "SMAILUtils.h"
#include "QueueUtils.h"






#define QUE_SMTP_MAILER_ERROR_HDR   "X-MailerError"
#define QUE_MAILER_HDR              "X-MailerServer"









static int      QueUtDumpFrozen(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, FILE * pListFile);
static bool     QueUtRemoveSpoolErrors(void);
static char    *QueUtGetReplyAddress(SPLF_HANDLE hFSpool);
static int      QueUtTXErrorNotifySender(char const * pszMessFilePath, char const * pszReason);
static int      QueUtTXErrorNotifyRoot(SPLF_HANDLE hFSpool, char const * pszReason);
static int      QueUtTXErrorNotifyRoot(char const * pszMessFilePath, char const * pszReason);
static int      QueUtBuildErrorRespose(char const * pszSMTPDomain, SPLF_HANDLE hFSpool,
                        char const * pszFrom, char const * pszTo, char const * pszResponseFile,
                        char const * pszReason);













static int      QueUtDumpFrozen(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, FILE * pListFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Get message file path
///////////////////////////////////////////////////////////////////////////////
    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hQueue, hMessage, szQueueFilePath);

///////////////////////////////////////////////////////////////////////////////
//  Load spool file info and make a type check
///////////////////////////////////////////////////////////////////////////////
    SYS_FILE_INFO   FI;

    if (SysGetFileInfo(szQueueFilePath, FI) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Load the spool file header
///////////////////////////////////////////////////////////////////////////////
    SpoolFileHeader SFH;

    if (USmlLoadSpoolFileHeader(szQueueFilePath, SFH) < 0)
        return (ErrGetErrorCode());

    char           *pszFrom = USmlAddrConcat(SFH.ppszFrom);

    if (pszFrom == NULL)
    {
        ErrorPush();
        USmlCleanupSpoolFileHeader(SFH);
        return (ErrorPop());
    }

    char           *pszRcpt = USmlAddrConcat(SFH.ppszRcpt);

    if (pszRcpt == NULL)
    {
        ErrorPush();
        SysFree(pszFrom);
        USmlCleanupSpoolFileHeader(SFH);
        return (ErrorPop());
    }


    char            szTime[128] = "";

    MscGetTimeNbrString(szTime, sizeof(szTime) - 1, FI.tCreat);


    fprintf(pListFile,
            "\"%s\"\t"
            "\"%d\"\t"
            "\"%d\"\t"
            "\"<%s>\"\t"
            "\"<%s>\"\t"
            "\"%s\"\t"
            "\"%lu\"\n",
            QueGetFileName(hMessage), QueGetLevel1(hMessage), QueGetLevel2(hMessage),
            pszFrom, pszRcpt, szTime, FI.ulSize);


    SysFree(pszRcpt);
    SysFree(pszFrom);

    USmlCleanupSpoolFileHeader(SFH);

    return (0);

}



int             QueUtGetFrozenList(QUEUE_HANDLE hQueue, char const * pszListFile)
{

    int             iNumDirsLevel = QueGetDirsLevel(hQueue);
    char const     *pszRootPath = QueGetRootPath(hQueue);

///////////////////////////////////////////////////////////////////////////////
//  Creates the list file and start scanning the frozen queue
///////////////////////////////////////////////////////////////////////////////
    FILE           *pListFile = fopen(pszListFile, "wt");

    if (pListFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE, pszListFile);
        return (ERR_FILE_CREATE);
    }


    for (int ii = 0; ii < iNumDirsLevel; ii++)
    {
        for (int jj = 0; jj < iNumDirsLevel; jj++)
        {
            char            szCurrPath[SYS_MAX_PATH] = "";

            sprintf(szCurrPath, "%s%d%s%d%s%s",
                    pszRootPath, ii, SYS_SLASH_STR, jj, SYS_SLASH_STR, QUEUE_FROZ_DIR);


            char            szFrozFileName[SYS_MAX_PATH] = "";
            FSCAN_HANDLE    hFileScan = MscFirstFile(szCurrPath, 0, szFrozFileName);

            if (hFileScan != INVALID_FSCAN_HANDLE)
            {
                do
                {
                    if (!SYS_IS_VALID_FILENAME(szFrozFileName))
                        continue;

///////////////////////////////////////////////////////////////////////////////
//  Create queue file handle
///////////////////////////////////////////////////////////////////////////////
                    QMSG_HANDLE     hMessage = QueGetHandle(hQueue, ii, jj, QUEUE_FROZ_DIR,
                            szFrozFileName);

                    if (hMessage != INVALID_QMSG_HANDLE)
                    {
                        QueUtDumpFrozen(hQueue, hMessage, pListFile);

                        QueCloseMessage(hQueue, hMessage);
                    }

                } while (MscNextFile(hFileScan, szFrozFileName));

                MscCloseFindFile(hFileScan);
            }

        }
    }

    fclose(pListFile);

    return (0);

}



int             QueUtUnFreezeMessage(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
                        char const * pszMessageFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Create queue file handle
///////////////////////////////////////////////////////////////////////////////
    QMSG_HANDLE     hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
            pszMessageFile);

    if (hMessage == INVALID_QMSG_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Init message statistics
///////////////////////////////////////////////////////////////////////////////
    QueInitMessageStats(hQueue, hMessage);

///////////////////////////////////////////////////////////////////////////////
//  Try to re-commit the frozen message
///////////////////////////////////////////////////////////////////////////////
    if (QueCommitMessage(hQueue, hMessage) < 0)
    {
        ErrorPush();
        QueCloseMessage(hQueue, hMessage);
        return (ErrorPop());
    }


    return (0);

}



int             QueUtDeleteFrozenMessage(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
                        char const * pszMessageFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Create queue file handle
///////////////////////////////////////////////////////////////////////////////
    QMSG_HANDLE     hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
            pszMessageFile);

    if (hMessage == INVALID_QMSG_HANDLE)
        return (ErrGetErrorCode());


    QueCleanupMessage(hQueue, hMessage);

    QueCloseMessage(hQueue, hMessage);

    return (0);

}



int             QueUtGetFrozenMsgFile(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
                        char const * pszMessageFile, char const * pszOutFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Create queue file handle
///////////////////////////////////////////////////////////////////////////////
    QMSG_HANDLE     hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
            pszMessageFile);

    if (hMessage == INVALID_QMSG_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Get slog file path
///////////////////////////////////////////////////////////////////////////////
    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hQueue, hMessage, szQueueFilePath);

///////////////////////////////////////////////////////////////////////////////
//  Copy the requested file
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pszOutFile, szQueueFilePath) < 0)
    {
        ErrorPush();
        QueCloseMessage(hQueue, hMessage);
        return (ErrorPop());
    }


    QueCloseMessage(hQueue, hMessage);

    return (0);

}



int             QueUtGetFrozenLogFile(QUEUE_HANDLE hQueue, int iLevel1, int iLevel2,
                        char const * pszMessageFile, char const * pszOutFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Create queue file handle
///////////////////////////////////////////////////////////////////////////////
    QMSG_HANDLE     hMessage = QueGetHandle(hQueue, iLevel1, iLevel2, QUEUE_FROZ_DIR,
            pszMessageFile);

    if (hMessage == INVALID_QMSG_HANDLE)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Get slog file path
///////////////////////////////////////////////////////////////////////////////
    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hQueue, hMessage, szQueueFilePath, QUEUE_SLOG_DIR);

///////////////////////////////////////////////////////////////////////////////
//  Copy the requested file
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(pszOutFile, szQueueFilePath) < 0)
    {
        ErrorPush();
        QueCloseMessage(hQueue, hMessage);
        return (ErrorPop());
    }


    QueCloseMessage(hQueue, hMessage);

    return (0);

}



int             QueUtErrLogMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        char const * pszFormat, ...)
{
///////////////////////////////////////////////////////////////////////////////
//  Get slog file path
///////////////////////////////////////////////////////////////////////////////
    char            szSlogFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hQueue, hMessage, szSlogFilePath, QUEUE_SLOG_DIR);


    va_list         Args;

    va_start(Args, pszFormat);

    if (ErrFileVLogMessage(szSlogFilePath, pszFormat, Args) < 0)
    {
        va_end(Args);
        return (ErrGetErrorCode());
    }

    va_end(Args);

    return (0);

}



static bool     QueUtRemoveSpoolErrors(void)
{

    return (SvrTestConfigFlag("RemoveSpoolErrors", false));

}



int             QueUtCleanupNotifyErrDelivery(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Get message file path
///////////////////////////////////////////////////////////////////////////////
    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hQueue, hMessage, szQueueFilePath);


    bool            bFreeze = false;
    int             iNotifyResult = QueUtTXErrorNotifySender(szQueueFilePath, pszReason);

    if ((iNotifyResult != ERR_NULL_SENDER) &&
            ((iNotifyResult != 0) || !QueUtRemoveSpoolErrors()))
        bFreeze = true;


    QueCleanupMessage(hQueue, hMessage, bFreeze);

    return (0);

}



int             QueUtCleanupNotifyRoot(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Get message file path
///////////////////////////////////////////////////////////////////////////////
    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hQueue, hMessage, szQueueFilePath);


    bool            bFreeze = false;
    int             iNotifyResult = QueUtTXErrorNotifyRoot(szQueueFilePath, pszReason);

    if ((iNotifyResult != 0) || !QueUtRemoveSpoolErrors())
        bFreeze = true;


    QueCleanupMessage(hQueue, hMessage, bFreeze);

    return (0);

}



static char    *QueUtGetReplyAddress(SPLF_HANDLE hFSpool)
{
///////////////////////////////////////////////////////////////////////////////
//  Extract the sender
///////////////////////////////////////////////////////////////////////////////
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom);

    if (iFromDomains == 0)
    {
        ErrSetErrorCode(ERR_NULL_SENDER);
        return (NULL);
    }

    char const     *pszSender = ppszFrom[iFromDomains - 1];
    char            szSenderDomain[MAX_ADDR_NAME] = "",
                    szSenderName[MAX_ADDR_NAME] = "";

    if (USmtpSplitEmailAddr(pszSender, szSenderName, szSenderDomain) < 0)
        return (SysStrDup(pszSender));

///////////////////////////////////////////////////////////////////////////////
//  Lookup special reply-to header tags
///////////////////////////////////////////////////////////////////////////////




    return (SysStrDup(pszSender));

}



static int      QueUtTXErrorNotifySender(SPLF_HANDLE hFSpool, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Extract the sender
///////////////////////////////////////////////////////////////////////////////
    char           *pszReplyTo = QueUtGetReplyAddress(hFSpool);

    if (pszReplyTo == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Load configuration handle
///////////////////////////////////////////////////////////////////////////////
    SVRCFG_HANDLE   hSvrConfig = SvrGetConfigHandle();

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
    {
        ErrorPush();
        SysFree(pszReplyTo);
        return (ErrorPop());
    }


    char            szPMAddress[MAX_ADDR_NAME] = "",
                    szMailDomain[MAX_HOST_NAME] = "";

    if ((SvrConfigVar("PostMaster", szPMAddress, sizeof(szPMAddress) - 1, hSvrConfig) < 0) ||
            (SvrConfigVar("RootDomain", szMailDomain, sizeof(szMailDomain) - 1, hSvrConfig) < 0))
    {
        SvrReleaseConfigHandle(hSvrConfig);
        SysFree(pszReplyTo);

        ErrSetErrorCode(ERR_INCOMPLETE_CONFIG);
        return (ERR_INCOMPLETE_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
    QMSG_HANDLE     hMessage = QueGetTempMsg(hSpoolQueue);

    if (hMessage == INVALID_QMSG_HANDLE)
    {
        ErrorPush();
        SvrReleaseConfigHandle(hSvrConfig);
        SysFree(pszReplyTo);
        return (ErrorPop());
    }

    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

///////////////////////////////////////////////////////////////////////////////
//  Build error response mail file
///////////////////////////////////////////////////////////////////////////////
    if (QueUtBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, pszReplyTo,
                    szQueueFilePath, pszReason) < 0)
    {
        ErrorPush();
        QueCleanupMessage(hSpoolQueue, hMessage);
        QueCloseMessage(hSpoolQueue, hMessage);
        SvrReleaseConfigHandle(hSvrConfig);
        SysFree(pszReplyTo);
        return (ErrorPop());
    }

    SysFree(pszReplyTo);

///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file
///////////////////////////////////////////////////////////////////////////////
    if (QueCommitMessage(hSpoolQueue, hMessage) < 0)
    {
        ErrorPush();
        QueCleanupMessage(hSpoolQueue, hMessage);
        QueCloseMessage(hSpoolQueue, hMessage);
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }


///////////////////////////////////////////////////////////////////////////////
//  Notify "error-handler" admin
///////////////////////////////////////////////////////////////////////////////
    char            szEHAdmin[MAX_ADDR_NAME] = "";

    if ((SvrConfigVar("ErrorsAdmin", szEHAdmin, sizeof(szEHAdmin) - 1, hSvrConfig) == 0) &&
            !IsEmptyString(szEHAdmin))
    {
///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
        if ((hMessage = QueGetTempMsg(hSpoolQueue)) == INVALID_QMSG_HANDLE)
        {
            ErrorPush();
            SvrReleaseConfigHandle(hSvrConfig);
            return (ErrorPop());
        }

        QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);

///////////////////////////////////////////////////////////////////////////////
//  Build error response mail file
///////////////////////////////////////////////////////////////////////////////
        if (QueUtBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, szEHAdmin,
                        szQueueFilePath, pszReason) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            SvrReleaseConfigHandle(hSvrConfig);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file
///////////////////////////////////////////////////////////////////////////////
        if (QueCommitMessage(hSpoolQueue, hMessage) < 0)
        {
            ErrorPush();
            QueCleanupMessage(hSpoolQueue, hMessage);
            QueCloseMessage(hSpoolQueue, hMessage);
            SvrReleaseConfigHandle(hSvrConfig);
            return (ErrorPop());
        }

    }

    SvrReleaseConfigHandle(hSvrConfig);

    return (0);

}



static int      QueUtTXErrorNotifySender(char const * pszMessFilePath, char const * pszReason)
{

    SPLF_HANDLE     hFSpool = USmlCreateHandle(pszMessFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = QueUtTXErrorNotifySender(hFSpool, pszReason);


    USmlCloseHandle(hFSpool);

    return (iNotifyResult);

}



static int      QueUtTXErrorNotifyRoot(SPLF_HANDLE hFSpool, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Load configuration handle
///////////////////////////////////////////////////////////////////////////////
    SVRCFG_HANDLE   hSvrConfig = SvrGetConfigHandle();

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
        return (ErrGetErrorCode());


    char            szPMAddress[MAX_ADDR_NAME] = "",
                    szMailDomain[MAX_HOST_NAME] = "";

    if ((SvrConfigVar("PostMaster", szPMAddress, sizeof(szPMAddress) - 1, hSvrConfig) < 0) ||
            (SvrConfigVar("RootDomain", szMailDomain, sizeof(szMailDomain) - 1, hSvrConfig) < 0))
    {
        SvrReleaseConfigHandle(hSvrConfig);

        ErrSetErrorCode(ERR_INCOMPLETE_CONFIG);
        return (ERR_INCOMPLETE_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Get message handle
///////////////////////////////////////////////////////////////////////////////
    QMSG_HANDLE     hMessage = QueGetTempMsg(hSpoolQueue);

    if (hMessage == INVALID_QMSG_HANDLE)
    {
        ErrorPush();
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    char            szQueueFilePath[SYS_MAX_PATH] = "";

    QueGetFilePath(hSpoolQueue, hMessage, szQueueFilePath);


///////////////////////////////////////////////////////////////////////////////
//  Build error response mail file
///////////////////////////////////////////////////////////////////////////////
    if (QueUtBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, szPMAddress,
                    szQueueFilePath, pszReason) < 0)
    {
        ErrorPush();
        QueCleanupMessage(hSpoolQueue, hMessage);
        QueCloseMessage(hSpoolQueue, hMessage);
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    SvrReleaseConfigHandle(hSvrConfig);


///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file
///////////////////////////////////////////////////////////////////////////////
    if (QueCommitMessage(hSpoolQueue, hMessage) < 0)
    {
        ErrorPush();
        QueCleanupMessage(hSpoolQueue, hMessage);
        QueCloseMessage(hSpoolQueue, hMessage);
        return (ErrorPop());
    }

    return (0);

}



static int      QueUtTXErrorNotifyRoot(char const * pszMessFilePath, char const * pszReason)
{

    SPLF_HANDLE     hFSpool = USmlCreateHandle(pszMessFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = QueUtTXErrorNotifyRoot(hFSpool, pszReason);


    USmlCloseHandle(hFSpool);

    return (iNotifyResult);

}



static int      QueUtBuildErrorRespose(char const * pszSMTPDomain, SPLF_HANDLE hFSpool,
                        char const * pszFrom, char const * pszTo, char const * pszResponseFile,
                        char const * pszReason)
{

    char const     *pszSpoolFileName = USmlGetSpoolFile(hFSpool);
    char const     *pszSmtpMsgID = USmlGetSmtpMessageID(hFSpool);

///////////////////////////////////////////////////////////////////////////////
//  Retrieve a new message ID
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT64      ullMessageID = 0;

    if (SvrGetMessageID(&ullMessageID) < 0)
        return (ErrGetErrorCode());


    FILE           *pRespFile = fopen(pszResponseFile, "wb");

    if (pRespFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Try to remap target user address
///////////////////////////////////////////////////////////////////////////////
    char            szDomain[MAX_ADDR_NAME] = "",
                    szName[MAX_ADDR_NAME] = "",
                    szTo[MAX_ADDR_NAME] = "";

    if (USmlMapAddress(pszTo, szDomain, szName) < 0)
        strcpy(szTo, pszTo);
    else
        sprintf(szTo, "%s@%s", szName, szDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write domain
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "%s\r\n", pszSMTPDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write message ID
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "X" SYS_LLX_FMT "\r\n", ullMessageID);

///////////////////////////////////////////////////////////////////////////////
//  Write MAIL FROM
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "MAIL FROM:<%s>\r\n", pszFrom);

///////////////////////////////////////////////////////////////////////////////
//  Write RCPT TO
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "RCPT TO:<%s>\r\n", szTo);

///////////////////////////////////////////////////////////////////////////////
//  Write SPOOL_FILE_DATA_START
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "%s\r\n", SPOOL_FILE_DATA_START);


///////////////////////////////////////////////////////////////////////////////
//  Write Date ( mail data )
///////////////////////////////////////////////////////////////////////////////
    char            szTime[256] = "";

    MscGetTimeStr(szTime, sizeof(szTime) - 1);

    fprintf(pRespFile, "Date:   %s\r\n", szTime);

///////////////////////////////////////////////////////////////////////////////
//  Write X-MessageId ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "X-MessageId: <%s>\r\n", pszSpoolFileName);

///////////////////////////////////////////////////////////////////////////////
//  Write X-SmtpMessageId ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "X-SmtpMessageId: <%s>\r\n", pszSmtpMsgID);

///////////////////////////////////////////////////////////////////////////////
//  Write From ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "From:   %s PostMaster <%s>\r\n", pszSMTPDomain, pszFrom);

///////////////////////////////////////////////////////////////////////////////
//  Write To ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "To:     %s\r\n", pszTo);

///////////////////////////////////////////////////////////////////////////////
//  Write Subject ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "Subject: Error sending message [%s] from [%s]\r\n",
            pszSpoolFileName, pszSMTPDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write X-MailerServer ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "%s: %s\r\n", QUE_MAILER_HDR, APP_NAME_VERSION_OS_STR);

///////////////////////////////////////////////////////////////////////////////
//  Write X-SMTP-Mailer-Error ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "%s: Message = [%s] Server = [%s]\r\n",
            QUE_SMTP_MAILER_ERROR_HDR, pszSpoolFileName, pszSMTPDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write blank line ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Write error message ( mail data )
///////////////////////////////////////////////////////////////////////////////
    char const     *pszMailFrom = USmlMailFrom(hFSpool);
    char const     *pszRcptTo = USmlRcptTo(hFSpool);

    fprintf(pRespFile, "Error sending message [%s] from [%s].\r\n\r\n"
            "ID:        <%s>\r\n"
            "Mail From: <%s>\r\n"
            "Rcpt To:   <%s>\r\n\r\n"
            "<Failure Reason>\r\n"
            "%s\r\n"
            "</Failure Reason>\r\n\r\n"
            "Below is reported the message header:\r\n"
            "\r\n", pszSpoolFileName, pszSMTPDomain, pszSmtpMsgID,
            pszMailFrom, pszRcptTo, pszReason);


///////////////////////////////////////////////////////////////////////////////
//  Write message header ( mail data )
///////////////////////////////////////////////////////////////////////////////
    char const     *pszMailFile = USmlGetMailFile(hFSpool);
    FILE           *pMsgFile = fopen(pszMailFile, "rb");

    if (pMsgFile == NULL)
    {
        fclose(pRespFile);

        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

    char            szBuffer[2048] = "";

    while (MscGetString(pMsgFile, szBuffer, sizeof(szBuffer) - 1) != NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  Mail error loop deteced
///////////////////////////////////////////////////////////////////////////////
        if (StrNComp(szBuffer, QUE_SMTP_MAILER_ERROR_HDR) == 0)
        {
            fclose(pMsgFile);
            fclose(pRespFile);
            SysRemove(pszResponseFile);

            ErrSetErrorCode(ERR_MAIL_ERROR_LOOP);
            return (ERR_MAIL_ERROR_LOOP);
        }

        if (strlen(szBuffer) == 0)
            break;

        fprintf(pRespFile, ">> %s\r\n", szBuffer);
    }

    fclose(pMsgFile);

    fclose(pRespFile);

    return (0);

}



int             QueUtResendMessage(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage)
{
///////////////////////////////////////////////////////////////////////////////
//  Try to resend the message
///////////////////////////////////////////////////////////////////////////////
    int             iResendResult = QueResendMessage(hQueue, hMessage);

///////////////////////////////////////////////////////////////////////////////
//  If the message is expired ...
///////////////////////////////////////////////////////////////////////////////
    if (iResendResult == ERR_SPOOL_FILE_EXPIRED)
    {
///////////////////////////////////////////////////////////////////////////////
//  Handle notifications and cleanup the message
///////////////////////////////////////////////////////////////////////////////
        iResendResult = QueUtCleanupNotifyErrDelivery(hQueue, hMessage,
                "The maximum number of tentatives has been reached");

        QueCloseMessage(hQueue, hMessage);
    }


    return (iResendResult);

}
