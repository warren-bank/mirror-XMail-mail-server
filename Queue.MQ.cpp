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
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SMAILUtils.h"
#include "Queue.h"






#define MAX_ACTIVE_QUEUES           32
#define QUEUE_TMPFILE_INFO_EXT      ".#info#"
#define QUEUE_FROZEN_SLOG_EXT       ".#slog#"
#define QUEUE_LOCKFILE_EXT          ""
#define MAX_QUEUE_LOCK_TIME         (18 * 60 * 60)
#define QUE_SMTP_MAILER_ERROR_HDR   "X-MailerError"
#define QUE_MAILER_HDR              "X-MailerServer"
#define MIN_MESS_REENTRY_TIMEOUT    10
#define MIN_RSND_REENTRY_TIMEOUT    30
#define INVALID_TIME                0

#define QDF_BUSY                    (1 << 0)









struct QueueEntry
{
    SysListHead     LLink;
    int             iLevel1;
    int             iLevel2;
    char           *pszFileName;
    time_t          tLastTry;
};

struct QueueOpens
{
    char           *pszRootPath;
    MessageQueue   *pMQ;
};

struct QueueStream
{
    char            szMessId[MAX_MESSAGE_ID];
    char            szInfoFile[SYS_MAX_PATH];
    char            szMessFile[SYS_MAX_PATH];
    FILE           *pInfoFile;
    FILE           *pMessFile;
};







static int      QueRegister(char const * pszRootPath, MessageQueue * pMQ);
static int      QueUnregister(MessageQueue * pMQ);
static MessageQueue *QueGetRegQueue(char const * pszBasePath);
static int      QueCreateStruct(char const * pszRootPath);
static QueueEntry  *QueAllocEntry(int iLevel1, int iLevel2, char const * pszMessageFile);
static int      QueFreeEntry(QueueEntry * pQE);
static int      QueAddDirEntries(MessageQueue & MQ, char const * pszRootPath,
                        int iLevel1, int iLevel2);
static int      QueStructInitialize(MessageQueue & MQ, int iNumDirsLevel);
static int      QueStructCleanup(MessageQueue & MQ);
static int      QueDumpFrozen(char const * pszFrozFilePath, FILE * pListFile);
static int      QueBasePathIndexes(char const * pszBasePath, int &iLevel1, int &iLevel2);
static int      QueFullPathIndexes(char const * pszFilePath, int &iLevel1, int &iLevel2);
static int      QueNotifyInsert(char const * pszBasePath, char const * pszMessFile);
static int      QueNotifyRemove(char const * pszBasePath, char const * pszMessFile,
                        bool bNewMessage);
static int      QueNotifyResend(char const * pszBasePath, char const * pszMessFile);
static int      QuePeekLockedFiles(char const * pszBasePath, char const * pszQueueSubdir,
                        char **ppszMessFilePath, int iMaxPeekFiles, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry, time_t & tNearestTry);
static int      QueReadyToProcess(char const * pszMessFilePath, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry, time_t & tNextTry);
static bool     QueRemoveSpoolErrors(void);
static int      QueTXErrorNotifySender(SPLF_HANDLE hFSpool, char const * pszReason);
static int      QueTXErrorNotifySender(char const * pszMessFilePath, char const * pszReason);
static int      QueTXErrorNotifyRoot(SPLF_HANDLE hFSpool, char const * pszReason);
static int      QueTXErrorNotifyRoot(char const * pszMessFilePath, char const * pszReason);
static int      QueBuildErrorRespose(char const * pszSMTPDomain, SPLF_HANDLE hFSpool,
                        char const * pszFrom, char const * pszTo, char const * pszResponseFile,
                        char const * pszReason);








static QueueOpens RegQueue[MAX_ACTIVE_QUEUES];









int             QueHandlerInit(void)
{

    for (int ii = 0; ii < MAX_ACTIVE_QUEUES; ii++)
    {
        RegQueue[ii].pszRootPath = NULL;
        RegQueue[ii].pMQ = NULL;
    }

    return (0);

}



int             QueHandlerCleanup(void)
{

    for (int ii = 0; ii < MAX_ACTIVE_QUEUES; ii++)
    {
        if (RegQueue[ii].pszRootPath != NULL)
        {
            SysFree(RegQueue[ii].pszRootPath);

            RegQueue[ii].pszRootPath = NULL;
            RegQueue[ii].pMQ = NULL;
        }
    }

    return (0);

}



static int      QueRegister(char const * pszRootPath, MessageQueue * pMQ)
{

    for (int ii = 0; ii < MAX_ACTIVE_QUEUES; ii++)
    {
        if (RegQueue[ii].pszRootPath == NULL)
        {
            RegQueue[ii].pszRootPath = SysStrDup(pszRootPath);
            RegQueue[ii].pMQ = pMQ;

            return (0);
        }
    }

    ErrSetErrorCode(ERR_QUEUE_ARRAY_FULL);
    return (ERR_QUEUE_ARRAY_FULL);

}



static int      QueUnregister(MessageQueue * pMQ)
{

    for (int ii = 0; ii < MAX_ACTIVE_QUEUES; ii++)
    {
        if (RegQueue[ii].pMQ == pMQ)
        {
            SysFree(RegQueue[ii].pszRootPath);

            RegQueue[ii].pszRootPath = NULL;
            RegQueue[ii].pMQ = NULL;

            return (0);
        }
    }

    ErrSetErrorCode(ERR_QUEUE_ENTRY_NOT_FOUND);
    return (ERR_QUEUE_ENTRY_NOT_FOUND);

}



static MessageQueue *QueGetRegQueue(char const * pszBasePath)
{

    for (int ii = 0; ii < MAX_ACTIVE_QUEUES; ii++)
        if ((RegQueue[ii].pszRootPath != NULL) &&
                (strnicmp(RegQueue[ii].pszRootPath, pszBasePath, strlen(RegQueue[ii].pszRootPath)) == 0))
            return (RegQueue[ii].pMQ);


    ErrSetErrorCode(ERR_QUEUE_ENTRY_NOT_FOUND);
    return (NULL);

}



static int      QueCreateStruct(char const * pszRootPath)
{
///////////////////////////////////////////////////////////////////////////////
//  Create message dir ( new messages queue )
///////////////////////////////////////////////////////////////////////////////
    char            szDirPath[SYS_MAX_PATH] = "";

    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_MESS_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create message resend dir ( resend messages queue )
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_RSND_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create info dir
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_INFO_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create temp dir
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_TEMP_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create send log dir
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_SLOG_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create message locking dir
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_LOCK_DIR);

    if (SysExistFile(szDirPath))
    {
///////////////////////////////////////////////////////////////////////////////
//  Clean lock directory
///////////////////////////////////////////////////////////////////////////////
        if (MscClearDirectory(szDirPath) < 0)
            return (ErrGetErrorCode());

    }
    else if (SysMakeDir(szDirPath) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create custom message processing dir
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_CUST_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Create frozen dir
///////////////////////////////////////////////////////////////////////////////
    strcpy(szDirPath, pszRootPath);
    AppendSlash(szDirPath);
    strcat(szDirPath, QUEUE_FROZ_DIR);

    if (!SysExistFile(szDirPath) && (SysMakeDir(szDirPath) < 0))
        return (ErrGetErrorCode());


    return (0);

}



static QueueEntry  *QueAllocEntry(int iLevel1, int iLevel2, char const * pszMessageFile)
{

    QueueEntry         *pQE = (QueueEntry *) SysAlloc(sizeof(QueueEntry));

    if (pQE == NULL)
        return (NULL);

    SYS_INIT_LIST_HEAD(&pQE->LLink);

    pQE->iLevel1 = iLevel1;
    pQE->iLevel2 = iLevel2;
    pQE->pszFileName = SysStrDup(pszMessageFile);
    pQE->tLastTry = time(NULL);

    return (pQE);

}



static int      QueFreeEntry(QueueEntry * pQE)
{

    SysFree(pQE->pszFileName);

    SysFree(pQE);

    return (0);

}



static int      QueAddDirEntries(MessageQueue & MQ, char const * pszRootPath,
                        int iLevel1, int iLevel2)
{
///////////////////////////////////////////////////////////////////////////////
//  File scan the new messages dir
///////////////////////////////////////////////////////////////////////////////
    char            szDirPath[SYS_MAX_PATH] = "";

    sprintf(szDirPath, "%s%s%d%s%d%s%s",
            pszRootPath, SYS_SLASH_STR, iLevel1, SYS_SLASH_STR, iLevel2,
            SYS_SLASH_STR, QUEUE_MESS_DIR);

    char            szMsgFileName[SYS_MAX_PATH] = "";
    FSCAN_HANDLE    hFileScan = MscFirstFile(szDirPath, 0, szMsgFileName);

    if (hFileScan != INVALID_FSCAN_HANDLE)
    {
        do
        {

            if (!IsDotFilename(szMsgFileName))
            {
                QueueEntry     *pQE = QueAllocEntry(iLevel1, iLevel2, szMsgFileName);

                if (pQE != NULL)
                {
                    SYS_LIST_ADDH(&pQE->LLink, &MQ.MessQueue);

                    ++MQ.iMessCount;
                }
            }

        } while (MscNextFile(hFileScan, szMsgFileName));

        MscCloseFindFile(hFileScan);
    }

///////////////////////////////////////////////////////////////////////////////
//  File scan the resend messages dir
///////////////////////////////////////////////////////////////////////////////
    sprintf(szDirPath, "%s%s%d%s%d%s%s",
            pszRootPath, SYS_SLASH_STR, iLevel1, SYS_SLASH_STR, iLevel2,
            SYS_SLASH_STR, QUEUE_RSND_DIR);

    if ((hFileScan = MscFirstFile(szDirPath, 0, szMsgFileName)) != INVALID_FSCAN_HANDLE)
    {
        do
        {

            if (!IsDotFilename(szMsgFileName))
            {
                QueueEntry     *pQE = QueAllocEntry(iLevel1, iLevel2, szMsgFileName);

                if (pQE != NULL)
                {
                    SYS_LIST_ADDH(&pQE->LLink, &MQ.RsndQueue);

                    ++MQ.iRsndCount;
                }
            }

        } while (MscNextFile(hFileScan, szMsgFileName));

        MscCloseFindFile(hFileScan);
    }

    return (0);

}



static int      QueStructInitialize(MessageQueue & MQ, int iNumDirsLevel)
{

    ZeroData(MQ);

    MQ.iNumDirsLevel = iNumDirsLevel;
    MQ.iMessCount = 0;
    MQ.iRsndCount = 0;

    SYS_INIT_LIST_HEAD(&MQ.MessQueue);
    SYS_INIT_LIST_HEAD(&MQ.RsndQueue);

    if ((MQ.hMutex = SysCreateMutex()) == SYS_INVALID_MUTEX)
        return (ErrGetErrorCode());

    if ((MQ.hEvent = SysCreateEvent(1)) == SYS_INVALID_EVENT)
    {
        ErrorPush();
        SysCloseMutex(MQ.hMutex);
        return (ErrorPop());
    }

    return (0);

}



static int      QueStructCleanup(MessageQueue & MQ)
{
///////////////////////////////////////////////////////////////////////////////
//  Clear "mess" queue
///////////////////////////////////////////////////////////////////////////////
    SysListHead    *pLLink;

    while ((pLLink = SYS_LIST_FIRST(&MQ.MessQueue)) != NULL)
    {
        QueueEntry     *pQE = SYS_LIST_ENTRY(pLLink, QueueEntry, LLink);

        SYS_LIST_DEL(pLLink);

        QueFreeEntry(pQE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Clear "rsnd" queue
///////////////////////////////////////////////////////////////////////////////
    while ((pLLink = SYS_LIST_FIRST(&MQ.RsndQueue)) != NULL)
    {
        QueueEntry     *pQE = SYS_LIST_ENTRY(pLLink, QueueEntry, LLink);

        SYS_LIST_DEL(pLLink);

        QueFreeEntry(pQE);
    }

    SysCloseEvent(MQ.hEvent);

    SysCloseMutex(MQ.hMutex);

    ZeroData(MQ);

    return (0);

}



int             QueCreateQueue(MessageQueue & MQ, char const * pszRootPath,
                        int iNumDirsLevel)
{
///////////////////////////////////////////////////////////////////////////////
//  Create message queue
///////////////////////////////////////////////////////////////////////////////
    if (QueStructInitialize(MQ, iNumDirsLevel) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Default to spool dir if pszRootPath is NULL
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolDir[SYS_MAX_PATH] = "";

    if (pszRootPath == NULL)
    {
        SvrGetSpoolDir(szSpoolDir);

        pszRootPath = szSpoolDir;
    }


    for (int ii = 0; ii < iNumDirsLevel; ii++)
    {
        char            szCurrPath[SYS_MAX_PATH] = "";

        sprintf(szCurrPath, "%s%s%d", pszRootPath, SYS_SLASH_STR, ii);

        if (!SysExistFile(szCurrPath) && (SysMakeDir(szCurrPath) < 0))
        {
            ErrorPush();
            QueStructCleanup(MQ);
            return (ErrorPop());
        }

        for (int jj = 0; jj < iNumDirsLevel; jj++)
        {
            sprintf(szCurrPath, "%s%s%d%s%d",
                    pszRootPath, SYS_SLASH_STR, ii, SYS_SLASH_STR, jj);

            if (!SysExistFile(szCurrPath) && (SysMakeDir(szCurrPath) < 0))
            {
                ErrorPush();
                QueStructCleanup(MQ);
                return (ErrorPop());
            }

            if (QueCreateStruct(szCurrPath) < 0)
            {
                ErrorPush();
                QueStructCleanup(MQ);
                return (ErrorPop());
            }

///////////////////////////////////////////////////////////////////////////////
//  Update queue data by scanning the current directory
///////////////////////////////////////////////////////////////////////////////
            QueAddDirEntries(MQ, pszRootPath, ii, jj);

        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Set the queue event if the queue is not empty
///////////////////////////////////////////////////////////////////////////////
    if ((MQ.iMessCount > 0) || (MQ.iRsndCount > 0))
        SysSetEvent(MQ.hEvent);


///////////////////////////////////////////////////////////////////////////////
//  Register the new queue to the queue handler
///////////////////////////////////////////////////////////////////////////////
    if (QueRegister(pszRootPath, &MQ) < 0)
    {
        ErrorPush();
        QueStructCleanup(MQ);
        return (ErrorPop());
    }

    return (0);

}



int             QueCloseQueue(MessageQueue & MQ)
{
///////////////////////////////////////////////////////////////////////////////
//  Unregister the new queue from the queue handler
///////////////////////////////////////////////////////////////////////////////
    if (QueUnregister(&MQ) < 0)
        return (ErrGetErrorCode());

    QueStructCleanup(MQ);

    return (0);

}



int             QueGetTempFile(char const * pszRootPath, char *pszFilePath, int iNumDirsLevel)
{
///////////////////////////////////////////////////////////////////////////////
//  Default to spool dir if pszRootPath is NULL
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolDir[SYS_MAX_PATH] = "";

    if (pszRootPath == NULL)
    {
        SvrGetSpoolDir(szSpoolDir);

        pszRootPath = szSpoolDir;
    }


    SRand();


    int             iPos0 = rand() % iNumDirsLevel,
                    iPos1 = rand() % iNumDirsLevel;
    char            szSubPath[SYS_MAX_PATH] = "";

    sprintf(szSubPath, "%s%s%d%s%d%s%s", pszRootPath, SYS_SLASH_STR, iPos0, SYS_SLASH_STR,
            iPos1, SYS_SLASH_STR, QUEUE_TEMP_DIR);


    return (MscUniqueFile(szSubPath, pszFilePath));

}



int             QueGetBasePath(char const * pszFilePath, char *pszBasePath,
                        char const ** ppszFileName, char const ** ppszQueueDir)
{

    char const     *pszSlash = strrchr(pszFilePath, SYS_SLASH_CHAR);

    if (pszSlash == NULL)
    {
        ErrSetErrorCode(ERR_INVALID_QUEUE_PATH, pszFilePath);
        return (ERR_INVALID_QUEUE_PATH);
    }

    if (ppszFileName != NULL)
        *ppszFileName = pszSlash + 1;

    for (--pszSlash; (pszSlash >= pszFilePath) && (*pszSlash != SYS_SLASH_CHAR); pszSlash--);

    if (pszSlash < pszFilePath)
    {
        ErrSetErrorCode(ERR_INVALID_QUEUE_PATH, pszFilePath);
        return (ERR_INVALID_QUEUE_PATH);
    }

    int             iBaseLength = (int) (pszSlash - pszFilePath);

    strncpy(pszBasePath, pszFilePath, iBaseLength);
    pszBasePath[iBaseLength] = '\0';

    if (ppszQueueDir != NULL)
        *ppszQueueDir = pszSlash + 1;

    return (0);

}



int             QueGetQueuePath(char const * pszFilePath, char const * pszQueueDir,
                        char *pszQueuePath)
{

    char const     *pszFileName = NULL,
                   *pszActQueueDir = NULL;
    char            szBasePath[SYS_MAX_PATH] = "";

    if (QueGetBasePath(pszFilePath, szBasePath, &pszFileName, &pszActQueueDir) < 0)
        return (ErrGetErrorCode());


    sprintf(pszQueuePath, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, pszQueueDir,
            SYS_SLASH_STR, pszFileName);


    return (0);

}



static int      QueDumpFrozen(char const * pszFrozFilePath, FILE * pListFile)
{

    int             iLevel1 = 0,
                    iLevel2 = 0;

    if (QueFullPathIndexes(pszFrozFilePath, iLevel1, iLevel2) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Load spool file info and make a type check
///////////////////////////////////////////////////////////////////////////////
    SYS_FILE_INFO   FI;

    if (SysGetFileInfo(pszFrozFilePath, FI) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Load the spool file header
///////////////////////////////////////////////////////////////////////////////
    SpoolFileHeader SFH;

    if (USmlLoadSpoolFileHeader(pszFrozFilePath, SFH) < 0)
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


    char            szTime[128] = "",
                    szMessFile[SYS_MAX_PATH] = "";

    MscGetTimeNbrString(szTime, sizeof(szTime) - 1, FI.tCreat);

    MscGetFileName(pszFrozFilePath, szMessFile);


    fprintf(pListFile,
            "\"%s\"\t"
            "\"%d\"\t"
            "\"%d\"\t"
            "\"<%s>\"\t"
            "\"<%s>\"\t"
            "\"%s\"\t"
            "\"%lu\"\n",
            szMessFile, iLevel1, iLevel2, pszFrom, pszRcpt, szTime, FI.ulSize);


    SysFree(pszRcpt);
    SysFree(pszFrom);

    USmlCleanupSpoolFileHeader(SFH);

    return (0);

}



int             QueGetFrozenList(char const * pszRootPath, char const * pszListFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Default to spool dir if pszRootPath is NULL
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolDir[SYS_MAX_PATH] = "";

    if (pszRootPath == NULL)
    {
        SvrGetSpoolDir(szSpoolDir);

        pszRootPath = szSpoolDir;
    }

///////////////////////////////////////////////////////////////////////////////
//  Get associated queue
///////////////////////////////////////////////////////////////////////////////
    MessageQueue   *pMQ = QueGetRegQueue(pszRootPath);

    if (pMQ == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Creates the list file and start scanning the frozen queue
///////////////////////////////////////////////////////////////////////////////
    FILE           *pListFile = fopen(pszListFile, "wt");

    if (pListFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE, pszListFile);
        return (ERR_FILE_CREATE);
    }

    int             iNumDirsLevel = pMQ->iNumDirsLevel;

    for (int ii = 0; ii < iNumDirsLevel; ii++)
    {
        for (int jj = 0; jj < iNumDirsLevel; jj++)
        {
            char            szCurrPath[SYS_MAX_PATH] = "";

            sprintf(szCurrPath, "%s%s%d%s%d%s%s",
                    pszRootPath, SYS_SLASH_STR, ii, SYS_SLASH_STR, jj, SYS_SLASH_STR,
                    QUEUE_FROZ_DIR);


            char            szFrozFileName[SYS_MAX_PATH] = "";
            FSCAN_HANDLE    hFileScan = MscFirstFile(szCurrPath, 0, szFrozFileName);

            if (hFileScan != INVALID_FSCAN_HANDLE)
            {
                do
                {
                    if (!SYS_IS_VALID_FILENAME(szFrozFileName))
                        continue;

                    char            szFrozFileExt[SYS_MAX_PATH] = "";

                    MscSplitPath(szFrozFileName, NULL, NULL, szFrozFileExt);

                    if (strcmp(szFrozFileExt, QUEUE_FROZEN_SLOG_EXT) == 0)
                        continue;


                    char            szFrozFilePath[SYS_MAX_PATH] = "";

                    sprintf(szFrozFilePath, "%s%s%s", szCurrPath, SYS_SLASH_STR,
                            szFrozFileName);


                    QueDumpFrozen(szFrozFilePath, pListFile);


                } while (MscNextFile(hFileScan, szFrozFileName));

                MscCloseFindFile(hFileScan);
            }

        }
    }

    fclose(pListFile);

    return (0);

}



int             QueCommitStoredMessage(char const * pszFilePath)
{

    char const     *pszFileName = NULL,
                   *pszQueueDir = NULL;
    char            szBasePath[SYS_MAX_PATH] = "";

    if (QueGetBasePath(pszFilePath, szBasePath, &pszFileName, &pszQueueDir) < 0)
        return (ErrGetErrorCode());


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szBasePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    char            szSourceFile[SYS_MAX_PATH] = "",
                    szTargetFile[SYS_MAX_PATH] = "";

///////////////////////////////////////////////////////////////////////////////
//  Move info file
///////////////////////////////////////////////////////////////////////////////
    sprintf(szSourceFile, "%s%s", pszFilePath, QUEUE_TMPFILE_INFO_EXT);

    sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_INFO_DIR,
            SYS_SLASH_STR, pszFileName);

    if (SysExistFile(szSourceFile) && (SysMoveFile(szSourceFile, szTargetFile) < 0))
    {
        ErrorPush();
        RLckUnlockEX(hResLock);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Move message file
///////////////////////////////////////////////////////////////////////////////
    strcpy(szSourceFile, pszFilePath);

    sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_MESS_DIR,
            SYS_SLASH_STR, pszFileName);

    if (SysMoveFile(szSourceFile, szTargetFile) < 0)
    {
        ErrorPush();

        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_INFO_DIR,
                SYS_SLASH_STR, pszFileName);
        CheckRemoveFile(szTargetFile);

        RLckUnlockEX(hResLock);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Notify queue file insertion
///////////////////////////////////////////////////////////////////////////////
    if (QueNotifyInsert(szBasePath, pszFileName) < 0)
    {
        ErrorPush();

        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_MESS_DIR,
                SYS_SLASH_STR, pszFileName);
        CheckRemoveFile(szTargetFile);

        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_INFO_DIR,
                SYS_SLASH_STR, pszFileName);
        CheckRemoveFile(szTargetFile);

        RLckUnlockEX(hResLock);
        return (ErrorPop());
    }

    RLckUnlockEX(hResLock);

    return (0);

}



static int      QueBasePathIndexes(char const * pszBasePath, int &iLevel1, int &iLevel2)
{

    char          **ppszTokens = StrTokenize(pszBasePath, SYS_SLASH_STR);

    if (ppszTokens == NULL)
        return (ErrGetErrorCode());

    int             iTokenCount = StrStringsCount(ppszTokens);

    if ((iTokenCount > 0) && (strlen(ppszTokens[iTokenCount - 1]) == 0))
        --iTokenCount;

    if ((iTokenCount < 2) ||
            !isdigit(ppszTokens[iTokenCount - 2][0]) ||
            !isdigit(ppszTokens[iTokenCount - 1][0]))
    {
        StrFreeStrings(ppszTokens);

        ErrSetErrorCode(ERR_INVALID_QUEUE_PATH, pszBasePath);
        return (ERR_INVALID_QUEUE_PATH);
    }

    iLevel1 = atoi(ppszTokens[iTokenCount - 2]);
    iLevel2 = atoi(ppszTokens[iTokenCount - 1]);

    StrFreeStrings(ppszTokens);

    return (0);

}



static int      QueFullPathIndexes(char const * pszFilePath, int &iLevel1, int &iLevel2)
{

    char          **ppszTokens = StrTokenize(pszFilePath, SYS_SLASH_STR);

    if (ppszTokens == NULL)
        return (ErrGetErrorCode());

    int             iTokenCount = StrStringsCount(ppszTokens);

    if ((iTokenCount < 4) ||
            !isdigit(ppszTokens[iTokenCount - 4][0]) ||
            !isdigit(ppszTokens[iTokenCount - 3][0]))
    {
        StrFreeStrings(ppszTokens);

        ErrSetErrorCode(ERR_INVALID_QUEUE_PATH, pszFilePath);
        return (ERR_INVALID_QUEUE_PATH);
    }

    iLevel1 = atoi(ppszTokens[iTokenCount - 4]);
    iLevel2 = atoi(ppszTokens[iTokenCount - 3]);

    StrFreeStrings(ppszTokens);

    return (0);

}



static int      QueNotifyInsert(char const * pszBasePath, char const * pszMessFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Get subdirectories indexes
///////////////////////////////////////////////////////////////////////////////
    int             iLevel1 = 0,
                    iLevel2 = 0;

    if (QueBasePathIndexes(pszBasePath, iLevel1, iLevel2) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Get associated queue
///////////////////////////////////////////////////////////////////////////////
    MessageQueue   *pMQ = QueGetRegQueue(pszBasePath);

    if (pMQ == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Allocates a queue entry
///////////////////////////////////////////////////////////////////////////////
    QueueEntry     *pQE = QueAllocEntry(iLevel1, iLevel2, pszMessFile);

    if (pQE == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Add the queue entry
///////////////////////////////////////////////////////////////////////////////
    if (SysLockMutex(pMQ->hMutex) < 0)
    {
        ErrorPush();
        QueFreeEntry(pQE);
        return (ErrorPop());
    }

    SYS_LIST_ADDT(&pQE->LLink, &pMQ->MessQueue);

    ++pMQ->iMessCount;

    SysSetEvent(pMQ->hEvent);

    SysUnlockMutex(pMQ->hMutex);

    return (0);

}



static int      QueNotifyRemove(char const * pszBasePath, char const * pszMessFile,
                        bool bNewMessage)
{
///////////////////////////////////////////////////////////////////////////////
//  Get subdirectories indexes
///////////////////////////////////////////////////////////////////////////////
    int             iLevel1 = 0,
                    iLevel2 = 0;

    if (QueBasePathIndexes(pszBasePath, iLevel1, iLevel2) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Get associated queue
///////////////////////////////////////////////////////////////////////////////
    MessageQueue   *pMQ = QueGetRegQueue(pszBasePath);

    if (pMQ == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Remove the queue entry
///////////////////////////////////////////////////////////////////////////////
    if (SysLockMutex(pMQ->hMutex) < 0)
        return (ErrGetErrorCode());


    if (bNewMessage)
    {
        SysListHead    *pLLink;

        SYS_LIST_FOR_EACH(pLLink, &MQ.MessQueue)
        {
            QueueEntry     *pQE = SYS_LIST_ENTRY(pLLink, QueueEntry, LLink);

            if ((pQE->iLevel1 == iLevel1) && (pQE->iLevel2 == iLevel2) &&
                    (stricmp(pszMessFile, pQE->pszFileName) == 0))
            {
                SYS_LIST_DEL(pLLink);

                QueFreeEntry(pQE);

                --pMQ->iMessCount;

                if ((pMQ->iMessCount == 0) && (pMQ->iRsndCount == 0))
                    SysResetEvent(pMQ->hEvent);

                break;
            }
        }
    }
    else
    {
        SysListHead    *pLLink;

        SYS_LIST_FOR_EACH(pLLink, &MQ.RsndQueue)
        {
            QueueEntry     *pQE = SYS_LIST_ENTRY(pLLink, QueueEntry, LLink);

            if ((pQE->iLevel1 == iLevel1) && (pQE->iLevel2 == iLevel2) &&
                    (stricmp(pszMessFile, pQE->pszFileName) == 0))
            {
                SYS_LIST_DEL(pLLink);

                QueFreeEntry(pQE);

                --pMQ->iRsndCount;

                if ((pMQ->iMessCount == 0) && (pMQ->iRsndCount == 0))
                    SysResetEvent(pMQ->hEvent);

                break;
            }
        }
    }

    SysUnlockMutex(pMQ->hMutex);

    return (0);

}



static int      QueNotifyResend(char const * pszBasePath, char const * pszMessFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Get subdirectories indexes
///////////////////////////////////////////////////////////////////////////////
    int             iLevel1 = 0,
                    iLevel2 = 0;

    if (QueBasePathIndexes(pszBasePath, iLevel1, iLevel2) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Get associated queue
///////////////////////////////////////////////////////////////////////////////
    MessageQueue   *pMQ = QueGetRegQueue(pszBasePath);

    if (pMQ == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Move the queue entry
///////////////////////////////////////////////////////////////////////////////
    if (SysLockMutex(pMQ->hMutex) < 0)
        return (ErrGetErrorCode());


    QueueEntry     *pMoveQE = NULL;
    SysListHead    *pLLink;

    SYS_LIST_FOR_EACH(pLLink, &MQ.MessQueue)
    {
        QueueEntry     *pQE = SYS_LIST_ENTRY(pLLink, QueueEntry, LLink);

        if ((pQE->iLevel1 == iLevel1) && (pQE->iLevel2 == iLevel2) &&
                (stricmp(pszMessFile, pQE->pszFileName) == 0))
        {
            SYS_LIST_DEL(pLLink);

            --pMQ->iMessCount;

            pMoveQE = pQE;
            break;
        }
    }

    if (pMoveQE != NULL)
    {
        SYS_LIST_ADDT(&pMoveQE->LLink, &pMQ->RsndQueue);

        ++pMQ->iRsndCount;
    }

    SysUnlockMutex(pMQ->hMutex);

    return (0);

}



int             QueLockMessage(char const * pszFilePath)
{

    char            szLockFilePath[SYS_MAX_PATH] = "";

    if (QueGetQueuePath(pszFilePath, QUEUE_LOCK_DIR, szLockFilePath) < 0)
        return (ErrGetErrorCode());

    if (SysLockFile(szLockFilePath, QUEUE_LOCKFILE_EXT) < 0)
    {
        ErrorPush();

        SYS_FILE_INFO   FI;

        if (SysGetFileInfo(szLockFilePath, FI) < 0)
            return (SysLockFile(szLockFilePath, QUEUE_LOCKFILE_EXT));

        if ((time(NULL) - FI.tCreat) < MAX_QUEUE_LOCK_TIME)
            return (ErrorPop());

        CheckRemoveFile(szLockFilePath);

        return (SysLockFile(szLockFilePath, QUEUE_LOCKFILE_EXT));
    }

    return (0);

}



int             QueUnlockMessage(char const * pszFilePath)
{

    char            szLockFilePath[SYS_MAX_PATH] = "";

    if (QueGetQueuePath(pszFilePath, QUEUE_LOCK_DIR, szLockFilePath) < 0)
        return (ErrGetErrorCode());


    SysUnlockFile(szLockFilePath, QUEUE_LOCKFILE_EXT);


    return (0);

}



int             QueResendMessage(char const * pszFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Build resend file path
///////////////////////////////////////////////////////////////////////////////
    char            szRsndFilePath[SYS_MAX_PATH] = "";

    if (QueGetQueuePath(pszFilePath, QUEUE_RSND_DIR, szRsndFilePath) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Move to the resend path ( if not already exist )
///////////////////////////////////////////////////////////////////////////////
    if (SysExistFile(szRsndFilePath))
        return (0);

    if (SysMoveFile(pszFilePath, szRsndFilePath) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Notify file resend movement ( it's important to call QueNotifyResend() only
//  if the message is effectively moved from the  mess  directory to the  rsnd
///////////////////////////////////////////////////////////////////////////////
    char const     *pszFileName = NULL,
                   *pszQueueDir = NULL;
    char            szBasePath[SYS_MAX_PATH] = "";

    if (QueGetBasePath(pszFilePath, szBasePath, &pszFileName, &pszQueueDir) < 0)
        return (ErrGetErrorCode());

    if (QueNotifyResend(szBasePath, pszFileName) < 0)
        return (ErrGetErrorCode());


    return (0);

}



int             QueCleanupMessage(char const * pszFilePath, bool bLockQueue, bool bFreezeMessage)
{

    char const     *pszFileName = NULL,
                   *pszQueueDir = NULL;
    char            szBasePath[SYS_MAX_PATH] = "";

    if (QueGetBasePath(pszFilePath, szBasePath, &pszFileName, &pszQueueDir) < 0)
        return (ErrGetErrorCode());


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = INVALID_RLCK_HANDLE;

    if (bLockQueue &&
            ((hResLock = RLckLockEX(CfgGetBasedPath(szBasePath, szResLock))) == INVALID_RLCK_HANDLE))
        return (ErrGetErrorCode());


    char            szTargetFile[SYS_MAX_PATH] = "";

///////////////////////////////////////////////////////////////////////////////
//  Remove info file
///////////////////////////////////////////////////////////////////////////////
    sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_INFO_DIR,
            SYS_SLASH_STR, pszFileName);

    CheckRemoveFile(szTargetFile);

///////////////////////////////////////////////////////////////////////////////
//  Remove custom message processing file
///////////////////////////////////////////////////////////////////////////////
    sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_CUST_DIR,
            SYS_SLASH_STR, pszFileName);

    CheckRemoveFile(szTargetFile);


    bool            bNewMessage = false;

    if (bFreezeMessage)
    {
        char            szSourceFile[SYS_MAX_PATH] = "";

///////////////////////////////////////////////////////////////////////////////
//  Move message file ( new message case )
///////////////////////////////////////////////////////////////////////////////
        sprintf(szSourceFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_MESS_DIR,
                SYS_SLASH_STR, pszFileName);

        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_FROZ_DIR,
                SYS_SLASH_STR, pszFileName);

        if (SysExistFile(szSourceFile))
        {
            if (SysMoveFile(szSourceFile, szTargetFile) < 0)
            {
                ErrorPush();
                if (hResLock != INVALID_RLCK_HANDLE)
                    RLckUnlockEX(hResLock);
                return (ErrorPop());
            }

            bNewMessage = true;
        }

///////////////////////////////////////////////////////////////////////////////
//  Move message file ( resend message case )
///////////////////////////////////////////////////////////////////////////////
        sprintf(szSourceFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_RSND_DIR,
                SYS_SLASH_STR, pszFileName);

        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_FROZ_DIR,
                SYS_SLASH_STR, pszFileName);

        if (SysExistFile(szSourceFile) && (SysMoveFile(szSourceFile, szTargetFile) < 0))
        {
            ErrorPush();
            if (hResLock != INVALID_RLCK_HANDLE)
                RLckUnlockEX(hResLock);
            return (ErrorPop());
        }

///////////////////////////////////////////////////////////////////////////////
//  Move slog file
///////////////////////////////////////////////////////////////////////////////
        sprintf(szSourceFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_SLOG_DIR,
                SYS_SLASH_STR, pszFileName);

        sprintf(szTargetFile, "%s%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_FROZ_DIR,
                SYS_SLASH_STR, pszFileName, QUEUE_FROZEN_SLOG_EXT);

        if (SysExistFile(szSourceFile) && (SysMoveFile(szSourceFile, szTargetFile) < 0))
        {
            ErrorPush();
            if (hResLock != INVALID_RLCK_HANDLE)
                RLckUnlockEX(hResLock);
            return (ErrorPop());
        }

    }
    else
    {
///////////////////////////////////////////////////////////////////////////////
//  Remove message file ( new message case )
///////////////////////////////////////////////////////////////////////////////
        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_MESS_DIR,
                SYS_SLASH_STR, pszFileName);

        if (SysExistFile(szTargetFile))
        {
            SysRemove(szTargetFile);

            bNewMessage = true;
        }

///////////////////////////////////////////////////////////////////////////////
//  Remove message file ( resend message case )
///////////////////////////////////////////////////////////////////////////////
        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_RSND_DIR,
                SYS_SLASH_STR, pszFileName);

        CheckRemoveFile(szTargetFile);

///////////////////////////////////////////////////////////////////////////////
//  Remove slog file
///////////////////////////////////////////////////////////////////////////////
        sprintf(szTargetFile, "%s%s%s%s%s", szBasePath, SYS_SLASH_STR, QUEUE_SLOG_DIR,
                SYS_SLASH_STR, pszFileName);

        CheckRemoveFile(szTargetFile);
    }

///////////////////////////////////////////////////////////////////////////////
//  Notify message removal
///////////////////////////////////////////////////////////////////////////////
    if (QueNotifyRemove(szBasePath, pszFileName, bNewMessage) < 0)
    {
        ErrorPush();
        if (hResLock != INVALID_RLCK_HANDLE)
            RLckUnlockEX(hResLock);
        return (ErrorPop());
    }

    if (hResLock != INVALID_RLCK_HANDLE)
        RLckUnlockEX(hResLock);

    return (0);

}



NQS_HANDLE      QueCreateNewStream(char const * pszRootPath, int iNumDirsLevel)
{
///////////////////////////////////////////////////////////////////////////////
//  Default to spool dir if pszRootPath is NULL
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolDir[SYS_MAX_PATH] = "";

    if (pszRootPath == NULL)
    {
        SvrGetSpoolDir(szSpoolDir);

        pszRootPath = szSpoolDir;
    }


    char            szMessFile[SYS_MAX_PATH] = "";

    if (QueGetTempFile(pszRootPath, szMessFile, iNumDirsLevel) < 0)
        return (INVALID_NQS_HANDLE);

    char            szInfoFile[SYS_MAX_PATH] = "";

    sprintf(szInfoFile, "%s%s", szMessFile, QUEUE_TMPFILE_INFO_EXT);

    FILE           *pInfoFile = fopen(szInfoFile, "w+b");

    if (pInfoFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (INVALID_NQS_HANDLE);
    }

    FILE           *pMessFile = fopen(szMessFile, "w+b");

    if (pMessFile == NULL)
    {
        fclose(pInfoFile);
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (INVALID_NQS_HANDLE);
    }

    QueueStream    *pQS = (QueueStream *) SysAlloc(sizeof(QueueStream));

    if (pQS == NULL)
    {
        fclose(pMessFile);
        fclose(pInfoFile);
        return (INVALID_NQS_HANDLE);
    }

    strcpy(pQS->szInfoFile, szInfoFile);
    strcpy(pQS->szMessFile, szMessFile);
    pQS->pInfoFile = pInfoFile;
    pQS->pMessFile = pMessFile;

    MscGetFileName(szMessFile, pQS->szMessId);

    return ((NQS_HANDLE) pQS);

}



int             QueCloseNewStream(NQS_HANDLE hQSHandle, bool bCommit)
{

    QueueStream    *pQS = (QueueStream *) hQSHandle;

    fclose(pQS->pMessFile);

    fclose(pQS->pInfoFile);

    if (bCommit)
    {
        if (QueCommitStoredMessage(pQS->szMessFile) < 0)
        {
            ErrorPush();
            CheckRemoveFile(pQS->szMessFile);
            CheckRemoveFile(pQS->szInfoFile);
            SysFree(pQS);
            return (ErrorPop());
        }
    }
    else
    {
        CheckRemoveFile(pQS->szMessFile);

        CheckRemoveFile(pQS->szInfoFile);
    }

    SysFree(pQS);

    return (0);

}



FILE           *QueGetMessageStream(NQS_HANDLE hQSHandle)
{

    QueueStream    *pQS = (QueueStream *) hQSHandle;

    return (pQS->pMessFile);

}



FILE           *QueGetInfoStream(NQS_HANDLE hQSHandle)
{

    QueueStream    *pQS = (QueueStream *) hQSHandle;

    return (pQS->pInfoFile);

}



char const     *QueGetMessageId(NQS_HANDLE hQSHandle)
{

    QueueStream    *pQS = (QueueStream *) hQSHandle;

    return (pQS->szMessId);

}



static int      QuePeekLockedFiles(char const * pszBasePath, char const * pszQueueSubdir,
                        char **ppszMessFilePath, int iMaxPeekFiles, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry, time_t & tNearestTry)
{
///////////////////////////////////////////////////////////////////////////////
//  Build  mess  subdir path and initialize "tNearestTry"
///////////////////////////////////////////////////////////////////////////////
    char            szMessPath[SYS_MAX_PATH] = "";

    sprintf(szMessPath, "%s%s%s", pszBasePath, SYS_SLASH_STR, pszQueueSubdir);

    tNearestTry = INVALID_TIME;

///////////////////////////////////////////////////////////////////////////////
//  Lock queue subdir
///////////////////////////////////////////////////////////////////////////////
    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(pszBasePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Scan  mess  subdir
///////////////////////////////////////////////////////////////////////////////
    int             iFileIndex = 0;
    char            szMsgFileName[SYS_MAX_PATH] = "";
    FSCAN_HANDLE    hFileScan = MscFirstFile(szMessPath, 0, szMsgFileName);

    ppszMessFilePath[iFileIndex] = NULL;

    if (hFileScan != INVALID_FSCAN_HANDLE)
    {
        int             iAlreadyLocked = 0;

        do
        {
            if (IsDotFilename(szMsgFileName))
                continue;

///////////////////////////////////////////////////////////////////////////////
//  If We don't go over the entire queue We've to invalidate the nearest time
///////////////////////////////////////////////////////////////////////////////
            if (iFileIndex == iMaxPeekFiles)
            {
                tNearestTry = INVALID_TIME;
                break;
            }


            char            szMessFilePath[SYS_MAX_PATH] = "";

            sprintf(szMessFilePath, "%s%s%s", szMessPath, SYS_SLASH_STR, szMsgFileName);

            if (QueLockMessage(szMessFilePath) == 0)
            {
                time_t          tNextTry = 0;
                int             iReadyResult = QueReadyToProcess(szMessFilePath,
                        iRetryTimeout, iRetryIncrRatio, iMaxRetry, tNextTry);

                if (iReadyResult == 0)
                {
///////////////////////////////////////////////////////////////////////////////
//  Ready to process message is found
///////////////////////////////////////////////////////////////////////////////
                    ppszMessFilePath[iFileIndex++] = SysStrDup(szMessFilePath);
                    ppszMessFilePath[iFileIndex] = NULL;
                }
                else
                {
///////////////////////////////////////////////////////////////////////////////
//  Check spool file removing ( max number of retries )
///////////////////////////////////////////////////////////////////////////////
                    if (iReadyResult == ERR_SPOOL_FILE_EXPIRED)
                    {
///////////////////////////////////////////////////////////////////////////////
//  Send a mail error notify to the sender and remove ( or freeze ) the message
///////////////////////////////////////////////////////////////////////////////
                        QueSpoolRemoveNotifySender(szMessFilePath,
                                "The maximum number of tentatives has been reached.",
                                false);

                    }
                    else if (iReadyResult == ERR_SPOOL_FILE_NOT_READY)
                    {
///////////////////////////////////////////////////////////////////////////////
//  Calculate the nearest retry
///////////////////////////////////////////////////////////////////////////////
                        if ((tNearestTry == INVALID_TIME) || (tNearestTry > tNextTry))
                            tNearestTry = tNextTry;

                    }

                    QueUnlockMessage(szMessFilePath);
                }
            }
            else
                ++iAlreadyLocked;

        } while (MscNextFile(hFileScan, szMsgFileName));

        MscCloseFindFile(hFileScan);

///////////////////////////////////////////////////////////////////////////////
//  If We can't watch at some files We've to assume that their next try time
//  will be (NOW + RETRY)
///////////////////////////////////////////////////////////////////////////////
        if ((tNearestTry != INVALID_TIME) && (iAlreadyLocked > 0))
        {
            time_t          tNow = time(NULL);

            tNearestTry = Min(tNearestTry, tNow + (time_t) iRetryTimeout);
        }
    }


    RLckUnlockEX(hResLock);

    if (iFileIndex == 0)
    {
        ErrSetErrorCode(ERR_NO_SMTP_SPOOL_FILES);
        return (ERR_NO_SMTP_SPOOL_FILES);
    }

    return (0);

}



int             QuePeekLockedFiles(char const * pszRootPath, char **ppszMessFilePath,
                        int iMaxPeekFiles, int iRetryTimeout, int iRetryIncrRatio,
                        int iMaxRetry)
{
///////////////////////////////////////////////////////////////////////////////
//  Default to spool dir if pszRootPath is NULL
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolDir[SYS_MAX_PATH] = "";

    if (pszRootPath == NULL)
    {
        SvrGetSpoolDir(szSpoolDir);

        pszRootPath = szSpoolDir;
    }

///////////////////////////////////////////////////////////////////////////////
//  Get associated shared block
///////////////////////////////////////////////////////////////////////////////
    SharedBlock    *pSHB = QueGetRegQueue(pszRootPath);

    if (pSHB == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Connect to the shared block and get pointers to queue srtucture
///////////////////////////////////////////////////////////////////////////////
    SHB_HANDLE      hShbQueue = ShbConnectBlock(*pSHB);

    if (hShbQueue == SHB_INVALID_HANDLE)
        return (ErrGetErrorCode());

    MessageQueue     *pQA = (MessageQueue *) ShbLock(hShbQueue);

    if (pQA == NULL)
    {
        ErrorPush();
        ShbCloseBlock(hShbQueue);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Give up if there isn't something to get
///////////////////////////////////////////////////////////////////////////////
    if (pQA->iTotalMessages == 0)
    {
        ShbUnlock(hShbQueue);
        ShbCloseBlock(hShbQueue);

        ErrSetErrorCode(ERR_NO_SMTP_SPOOL_FILES);
        return (ERR_NO_SMTP_SPOOL_FILES);
    }


///////////////////////////////////////////////////////////////////////////////
//  First search loop, searching for new messages
///////////////////////////////////////////////////////////////////////////////
    SRand();


    int             iStart1 = rand() % pQA->iNumDirsLevel,
                    iStart2 = rand() % pQA->iNumDirsLevel,
                    iLevel1 = -1,
                    iLevel2 = -1,
                    iMessageCount = 0;
    time_t          tNow = time(NULL);
    char const     *pszQueueSubdir = NULL;
    char            szBasePath[SYS_MAX_PATH] = "";

    int             ii = iStart1;

    do
    {
        int             jj = iStart2;

        do
        {
            QueueDir       *pQD = pQA->Dirs + LIndex2D(ii, jj, pQA->iNumDirsLevel);

            if (pQD->ulFlags & QDF_BUSY)
                continue;


            char            szCurrPath[SYS_MAX_PATH] = "";

            sprintf(szCurrPath, "%s%s%d%s%d",
                    pszRootPath, SYS_SLASH_STR, ii, SYS_SLASH_STR, jj);


            if ((pQD->iNewMessages > iMessageCount) &&
                    ((tNow - pQD->tMessAccess) > MIN_MESS_REENTRY_TIMEOUT))
            {
                iLevel1 = ii;
                iLevel2 = jj;
                pszQueueSubdir = QUEUE_MESS_DIR;
                strcpy(szBasePath, szCurrPath);

                iMessageCount = pQD->iNewMessages;
            }

        } while ((jj = INext(jj, pQA->iNumDirsLevel)) != iStart2);

    } while ((ii = INext(ii, pQA->iNumDirsLevel)) != iStart1);


    if (pszQueueSubdir == NULL)
    {
///////////////////////////////////////////////////////////////////////////////
//  Second search loop, searching for resend messages
///////////////////////////////////////////////////////////////////////////////
        iStart1 = rand() % pQA->iNumDirsLevel;
        iStart2 = rand() % pQA->iNumDirsLevel;

        iMessageCount = 0;

        ii = iStart1;

        do
        {
            int             jj = iStart2;

            do
            {
                QueueDir       *pQD = pQA->Dirs + LIndex2D(ii, jj, pQA->iNumDirsLevel);

                if (pQD->ulFlags & QDF_BUSY)
                    continue;


                char            szCurrPath[SYS_MAX_PATH] = "";

                sprintf(szCurrPath, "%s%s%d%s%d",
                        pszRootPath, SYS_SLASH_STR, ii, SYS_SLASH_STR, jj);


                if ((pQD->iMessagesCount > iMessageCount) && (tNow >= pQD->tRsndNext) &&
                        ((tNow - pQD->tRsndAccess) > MIN_RSND_REENTRY_TIMEOUT))
                {
                    iLevel1 = ii;
                    iLevel2 = jj;
                    pszQueueSubdir = QUEUE_RSND_DIR;
                    strcpy(szBasePath, szCurrPath);

                    iMessageCount = pQD->iMessagesCount;
                }

            } while ((jj = INext(jj, pQA->iNumDirsLevel)) != iStart2);

        } while ((ii = INext(ii, pQA->iNumDirsLevel)) != iStart1);

///////////////////////////////////////////////////////////////////////////////
//  If there isn't work to be done, give up
///////////////////////////////////////////////////////////////////////////////
        if (pszQueueSubdir == NULL)
        {
            ShbUnlock(hShbQueue);
            ShbCloseBlock(hShbQueue);

            ErrSetErrorCode(ERR_NO_SMTP_SPOOL_FILES);
            return (ERR_NO_SMTP_SPOOL_FILES);
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Update current scanned directory values
///////////////////////////////////////////////////////////////////////////////
    QueueDir       *pScanQD = pQA->Dirs + LIndex2D(iLevel1, iLevel2, pQA->iNumDirsLevel);

    if (strcmp(pszQueueSubdir, QUEUE_MESS_DIR) == 0)
        pScanQD->tMessAccess = time(NULL);
    else if (strcmp(pszQueueSubdir, QUEUE_RSND_DIR) == 0)
        pScanQD->tRsndAccess = time(NULL);

    pScanQD->ulFlags |= QDF_BUSY;

    ShbUnlock(hShbQueue);


///////////////////////////////////////////////////////////////////////////////
//  Try to extract an array of messages to process
///////////////////////////////////////////////////////////////////////////////
    time_t          tNearestTry = INVALID_TIME;
    int             iPeekResult = QuePeekLockedFiles(szBasePath, pszQueueSubdir,
            ppszMessFilePath, iMaxPeekFiles, iRetryTimeout,
            iRetryIncrRatio, iMaxRetry, tNearestTry);


///////////////////////////////////////////////////////////////////////////////
//  Update scan directory data
///////////////////////////////////////////////////////////////////////////////
    if ((pQA = (MessageQueue *) ShbLock(hShbQueue)) == NULL)
    {
        ErrorPush();
        ShbCloseBlock(hShbQueue);
        return (ErrorPop());
    }

    pScanQD = pQA->Dirs + LIndex2D(iLevel1, iLevel2, pQA->iNumDirsLevel);

    pScanQD->ulFlags &= ~QDF_BUSY;

///////////////////////////////////////////////////////////////////////////////
//  If next retry time is valid and if We've scanned the resend directory
//  update "tRsndNext"
///////////////////////////////////////////////////////////////////////////////
    if ((tNearestTry != INVALID_TIME) && (strcmp(pszQueueSubdir, QUEUE_RSND_DIR) == 0))
        pScanQD->tRsndNext = tNearestTry;


    ShbUnlock(hShbQueue);

    ShbCloseBlock(hShbQueue);

    return (iPeekResult);

}



int             QueErrLogMessage(char const * pszMessFilePath, char const * pszFormat,...)
{
///////////////////////////////////////////////////////////////////////////////
//  Build  slog  file path
///////////////////////////////////////////////////////////////////////////////
    char            szSlogFilePath[SYS_MAX_PATH] = "";

    if (QueGetQueuePath(pszMessFilePath, QUEUE_SLOG_DIR, szSlogFilePath) < 0)
        return (ErrGetErrorCode());


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




static int      QueReadyToProcess(char const * pszMessFilePath, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry, time_t & tNextTry)
{
///////////////////////////////////////////////////////////////////////////////
//  Build  slog  file path
///////////////////////////////////////////////////////////////////////////////
    char            szSlogFilePath[SYS_MAX_PATH] = "";

    if (QueGetQueuePath(pszMessFilePath, QUEUE_SLOG_DIR, szSlogFilePath) < 0)
        return (ErrGetErrorCode());


    time_t          tCurr = time(NULL);
    FILE           *pLogFile = fopen(szSlogFilePath, "r+t");

    if (pLogFile != NULL)
    {
        int             iRetryCount = 0;
        unsigned long   ulPrevTime = 0;
        char            szLogLine[1024] = "";

        while (MscFGets(szLogLine, sizeof(szLogLine) - 1, pLogFile) != NULL)
        {
            if (sscanf(szLogLine, "[PeekTime] %lu", &ulPrevTime) == 1)
            {
///////////////////////////////////////////////////////////////////////////////
//  Apply an relative increment to retry timeout
///////////////////////////////////////////////////////////////////////////////
                if (iRetryIncrRatio > 0)
                    iRetryTimeout += iRetryTimeout / iRetryIncrRatio;

                ++iRetryCount;
            }
        }

        if (iRetryCount > iMaxRetry)
        {
            fclose(pLogFile);
            ErrSetErrorCode(ERR_SPOOL_FILE_EXPIRED);
            return (ERR_SPOOL_FILE_EXPIRED);
        }

        unsigned long   ulElapsed = (unsigned long) tCurr - ulPrevTime;

        if ((unsigned long) iRetryTimeout > ulElapsed)
        {
            fclose(pLogFile);

            tNextTry = (time_t) ulPrevTime + (time_t) iRetryTimeout;

            ErrSetErrorCode(ERR_SPOOL_FILE_NOT_READY);
            return (ERR_SPOOL_FILE_NOT_READY);
        }
    }
    else
    {
        pLogFile = fopen(szSlogFilePath, "w+t");

        if (pLogFile == NULL)
        {
            ErrSetErrorCode(ERR_FILE_CREATE, szSlogFilePath);
            return (ERR_FILE_CREATE);
        }
    }

    fseek(pLogFile, 0, SEEK_END);


    fprintf(pLogFile, "[PeekTime] %lu\n", (unsigned long) tCurr);


    fclose(pLogFile);

    return (0);

}



static bool     QueRemoveSpoolErrors(void)
{

    return (SvrTestConfigFlag("RemoveSpoolErrors", false));

}



int             QueSpoolRemoveNotifySender(const char *pszMessFilePath, char const * pszReason,
                        bool bLockQueue)
{

    int             iNotifyResult = QueTXErrorNotifySender(pszMessFilePath, pszReason);

    if ((iNotifyResult == ERR_NULL_SENDER) ||
            ((iNotifyResult == 0) && QueRemoveSpoolErrors()))
        QueCleanupMessage(pszMessFilePath, bLockQueue, false);
    else
        QueCleanupMessage(pszMessFilePath, bLockQueue, true);


    return (0);

}



int             QueSpoolRemoveNotifyRoot(const char *pszMessFilePath, char const * pszReason,
                        bool bLockQueue)
{

    int             iNotifyResult = QueTXErrorNotifyRoot(pszMessFilePath, pszReason);

    if ((iNotifyResult == 0) && QueRemoveSpoolErrors())
        QueCleanupMessage(pszMessFilePath, bLockQueue, false);
    else
        QueCleanupMessage(pszMessFilePath, bLockQueue, true);


    return (0);

}



static int      QueTXErrorNotifySender(SPLF_HANDLE hFSpool, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Extract the sender
///////////////////////////////////////////////////////////////////////////////
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom);

    if (iFromDomains == 0)
    {
        ErrSetErrorCode(ERR_NULL_SENDER);
        return (ERR_NULL_SENDER);
    }

    char const     *pszReplyTo = ppszFrom[iFromDomains - 1];

///////////////////////////////////////////////////////////////////////////////
//  Load configuration handle
///////////////////////////////////////////////////////////////////////////////
    SVRCFG_HANDLE   hSvrConfig = SvrGetConfigHandle();

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
        return (ErrGetErrorCode());


    char            szPMAddress[MAX_ADDR_NAME] = "",
                    szMailDomain[MAX_HOST_NAME] = "";

    if ((SvrConfigVar("PostMaster", szPMAddress, sizeof(szPMAddress), hSvrConfig) < 0) ||
            (SvrConfigVar("RootDomain", szMailDomain, sizeof(szMailDomain), hSvrConfig) < 0))
    {
        SvrReleaseConfigHandle(hSvrConfig);

        ErrSetErrorCode(ERR_INCOMPLETE_CONFIG);
        return (ERR_INCOMPLETE_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Build error response mail file
///////////////////////////////////////////////////////////////////////////////
    char            szResponseFile[SYS_MAX_PATH] = "";

    if (QueGetTempFile(NULL, szResponseFile, iQueueSplitLevel) < 0)
    {
        ErrorPush();
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    if (QueBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, pszReplyTo,
                    szResponseFile, pszReason) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szResponseFile);
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    SvrReleaseConfigHandle(hSvrConfig);


///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file
///////////////////////////////////////////////////////////////////////////////
    if (QueCommitStoredMessage(szResponseFile) < 0)
    {
        ErrorPush();
        SysRemove(szResponseFile);
        return (ErrorPop());
    }

    return (0);

}



static int      QueTXErrorNotifySender(char const * pszMessFilePath, char const * pszReason)
{

    SPLF_HANDLE     hFSpool = USmlCreateHandle(pszMessFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = QueTXErrorNotifySender(hFSpool, pszReason);


    USmlCloseHandle(hFSpool);

    return (iNotifyResult);

}



static int      QueTXErrorNotifyRoot(SPLF_HANDLE hFSpool, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Load configuration handle
///////////////////////////////////////////////////////////////////////////////
    SVRCFG_HANDLE   hSvrConfig = SvrGetConfigHandle();

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
        return (ErrGetErrorCode());


    char            szPMAddress[MAX_ADDR_NAME] = "",
                    szMailDomain[MAX_HOST_NAME] = "";

    if ((SvrConfigVar("PostMaster", szPMAddress, sizeof(szPMAddress), hSvrConfig) < 0) ||
            (SvrConfigVar("RootDomain", szMailDomain, sizeof(szMailDomain), hSvrConfig) < 0))
    {
        SvrReleaseConfigHandle(hSvrConfig);

        ErrSetErrorCode(ERR_INCOMPLETE_CONFIG);
        return (ERR_INCOMPLETE_CONFIG);
    }

///////////////////////////////////////////////////////////////////////////////
//  Build error response mail file
///////////////////////////////////////////////////////////////////////////////
    char            szResponseFile[SYS_MAX_PATH] = "";

    if (QueGetTempFile(NULL, szResponseFile, iQueueSplitLevel) < 0)
    {
        ErrorPush();
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    if (QueBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, szPMAddress,
                    szResponseFile, pszReason) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szResponseFile);
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    SvrReleaseConfigHandle(hSvrConfig);


///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file
///////////////////////////////////////////////////////////////////////////////
    if (QueCommitStoredMessage(szResponseFile) < 0)
    {
        ErrorPush();
        SysRemove(szResponseFile);
        return (ErrorPop());
    }

    return (0);

}



static int      QueTXErrorNotifyRoot(char const * pszMessFilePath, char const * pszReason)
{

    SPLF_HANDLE     hFSpool = USmlCreateHandle(pszMessFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = QueTXErrorNotifyRoot(hFSpool, pszReason);


    USmlCloseHandle(hFSpool);

    return (iNotifyResult);

}



static int      QueBuildErrorRespose(char const * pszSMTPDomain, SPLF_HANDLE hFSpool,
                        char const * pszFrom, char const * pszTo, char const * pszResponseFile,
                        char const * pszReason)
{

    char const     *pszSpoolFileName = USmlGetSpoolFile(hFSpool);

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
    fprintf(pRespFile, SYS_LLX_FMT "\r\n", ullMessageID);

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
//  Write Message-Id ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "Message-Id: <%s>\r\n", pszSpoolFileName);

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
            "Mail From: <%s>\r\n"
            "Rcpt To:   <%s>\r\n\r\n"
            "<Failure Reason>\r\n"
            "%s\r\n"
            "</Failure Reason>\r\n\r\n"
            "Below is reported the message header:\r\n"
            "\r\n", pszSpoolFileName, pszSMTPDomain, pszMailFrom, pszRcptTo, pszReason);


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
