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
#include "ShBlocks.h"
#include "ResLocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SMAILUtils.h"
#include "SMAILSvr.h"
#include "AppDefines.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"






#define SVR_PROFILE_FILE            "server.tab"
#define MESSAGEID_FILE              "message.id"
#define SMTP_SPOOL_DIR              "spool"
#define MAX_MSG_FILENAME_LENGTH     80
#define SVR_PROFILE_LINE_MAX        2048
#define SVR_SMTP_MAILER_ERROR_HDR   "X-MailerError"
#define SVR_MAILER_HDR              "X-MailerServer"








struct ServerInfoVar
{
    LISTLINK        LL;
    char           *pszName;
    char           *pszValue;
};

struct ServerConfigData
{
    int             iWriteLock;
    HSLIST          hConfigList;
};






static char    *SvrGetProfileFilePath(char *pszFilePath);
static char   **SvrGetLineStrings(const char *pszSvrLine);
static ServerInfoVar *SvrAllocVar(const char *pszName, const char *pszValue);
static void     SvrFreeVar(ServerInfoVar * pSIV);
static void     SvrFreeInfoList(HSLIST & hConfigList);
static ServerInfoVar *SvrGetUserVar(HSLIST & hConfigList, const char *pszName);
static int      SvrWriteInfoList(HSLIST & hConfigList, FILE * pProfileFile);
static int      SvrLoadServerConfig(HSLIST & hConfigList, const char *pszFilePath);
static char    *SvrGetSpoolLogFile(char const * pszSpoolFileName, char *pszSpoolLogFile);
static int      SvrReadyToProcess(char const * pszSpoolFileName, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry);
static bool     SvrRemoveSpoolErrors(void);
static int      SvrSpoolMoveToFile_LK(const char *pszSpoolFileName, char const * pszSubDir);
static int      SvrSpoolRemoveFile_LK(const char *pszSpoolFileName);
static int      SvrCopyToSpool_LK(const char *pszFileName, const char *pszSpoolFileName);
static int      SvrMoveToSpool_LK(const char *pszFileName, const char *pszSpoolFileName);
static int      SvrMoveTmpToSpool_LK(char const *pszTmpSpoolFilePath, char *pszMessageFile);
static int      SvrTXErrorNotifySender_LK(SPLF_HANDLE hFSpool, char const * pszReason);
static int      SvrTXErrorNotifySender_LK(char const * pszMsgFileName, char const * pszReason);
static int      SvrTXErrorNotifyRoot_LK(SPLF_HANDLE hFSpool, char const * pszReason);
static int      SvrTXErrorNotifyRoot_LK(char const * pszMsgFileName, char const * pszReason);
static int      SvrBuildErrorRespose(char const * pszSMTPDomain, SPLF_HANDLE hFSpool,
                        char const * pszFrom, char const * pszTo, char const * pszResponseFile,
                        char const * pszReason);









static char    *SvrGetProfileFilePath(char *pszFilePath)
{

    CfgGetRootPath(pszFilePath);

    strcat(pszFilePath, SVR_PROFILE_FILE);

    return (pszFilePath);

}



static char   **SvrGetLineStrings(const char *pszSvrLine)
{

    char          **ppszStrings = StrTokenize(pszSvrLine, "\t");

    if (ppszStrings == NULL)
        return (NULL);

    for (int ii = 0; ppszStrings[ii] != NULL; ii++)
        StrDeQuote(ppszStrings[ii], '"');

    return (ppszStrings);

}



SVRCFG_HANDLE   SvrGetConfigHandle(int iWriteLock)
{

    char            szProfilePath[SYS_MAX_PATH] = "";

    SvrGetProfileFilePath(szProfilePath);

    if (iWriteLock && (SysLockFile(szProfilePath) < 0))
        return (INVALID_SVRCFG_HANDLE);

    ServerConfigData *pSCD = (ServerConfigData *) SysAlloc(sizeof(ServerConfigData));

    if (pSCD == NULL)
    {
        if (iWriteLock)
            SysUnlockFile(szProfilePath);
        return (INVALID_SVRCFG_HANDLE);
    }

    pSCD->iWriteLock = iWriteLock;

    ListInit(pSCD->hConfigList);

    if (SvrLoadServerConfig(pSCD->hConfigList, szProfilePath) < 0)
    {
        if (iWriteLock)
            SysUnlockFile(szProfilePath);
        SysFree(pSCD);
        return (INVALID_SVRCFG_HANDLE);
    }

    return ((SVRCFG_HANDLE) pSCD);

}



void            SvrReleaseConfigHandle(SVRCFG_HANDLE hSvrConfig)
{

    ServerConfigData *pSCD = (ServerConfigData *) hSvrConfig;

    if (pSCD->iWriteLock)
    {
        char            szProfilePath[SYS_MAX_PATH] = "";

        SvrGetProfileFilePath(szProfilePath);

        SysUnlockFile(szProfilePath);
    }

    SvrFreeInfoList(pSCD->hConfigList);

    SysFree(pSCD);

}



char           *SvrGetConfigVar(SVRCFG_HANDLE hSvrConfig, const char *pszName,
                        const char *pszDefault)
{

    ServerConfigData *pSCD = (ServerConfigData *) hSvrConfig;

    ServerInfoVar  *pSIV = SvrGetUserVar(pSCD->hConfigList, pszName);

    if (pSIV != NULL)
        return (SysStrDup(pSIV->pszValue));

    return ((pszDefault != NULL) ? SysStrDup(pszDefault) : NULL);

}



bool            SvrTestConfigFlag(char const * pszName, bool bDefault, SVRCFG_HANDLE hSvrConfig)
{

    char            szValue[64] = "";

    SvrConfigVar(pszName, szValue, sizeof(szValue) - 1, hSvrConfig,
            (bDefault) ? "1" : "0");

    return ((atoi(szValue) != 0) ? true : false);

}



int             SysFlushConfig(SVRCFG_HANDLE hSvrConfig)
{

    ServerConfigData *pSCD = (ServerConfigData *) hSvrConfig;

    if (!pSCD->iWriteLock)
    {
        ErrSetErrorCode(ERR_SVR_PRFILE_NOT_LOCKED);
        return (ERR_SVR_PRFILE_NOT_LOCKED);
    }

    char            szProfilePath[SYS_MAX_PATH] = "";

    SvrGetProfileFilePath(szProfilePath);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szProfilePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pProfileFile = fopen(szProfilePath, "wt");

    if (pProfileFile == NULL)
    {
        RLckUnlockEX(hResLock);
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    int             iFlushResult = SvrWriteInfoList(pSCD->hConfigList, pProfileFile);


    fclose(pProfileFile);

    RLckUnlockEX(hResLock);

    return (iFlushResult);

}



static ServerInfoVar *SvrAllocVar(const char *pszName, const char *pszValue)
{

    ServerInfoVar  *pSIV = (ServerInfoVar *) SysAlloc(sizeof(ServerInfoVar));

    if (pSIV == NULL)
        return (NULL);

    ListLinkInit(pSIV);
    pSIV->pszName = SysStrDup(pszName);
    pSIV->pszValue = SysStrDup(pszValue);

    return (pSIV);

}



static void     SvrFreeVar(ServerInfoVar * pSIV)
{

    SysFree(pSIV->pszName);
    SysFree(pSIV->pszValue);

    SysFree(pSIV);

}



static void     SvrFreeInfoList(HSLIST & hConfigList)
{

    ServerInfoVar  *pSIV;

    while ((pSIV = (ServerInfoVar *) ListRemove(hConfigList)) != INVALID_SLIST_PTR)
        SvrFreeVar(pSIV);

}



static ServerInfoVar *SvrGetUserVar(HSLIST & hConfigList, const char *pszName)
{

    ServerInfoVar  *pSIV = (ServerInfoVar *) ListFirst(hConfigList);

    for (; pSIV != INVALID_SLIST_PTR; pSIV = (ServerInfoVar *)
            ListNext(hConfigList, (PLISTLINK) pSIV))
        if (strcmp(pSIV->pszName, pszName) == 0)
            return (pSIV);

    return (NULL);

}



static int      SvrWriteInfoList(HSLIST & hConfigList, FILE * pProfileFile)
{

    ServerInfoVar  *pSIV = (ServerInfoVar *) ListFirst(hConfigList);

    for (; pSIV != INVALID_SLIST_PTR; pSIV = (ServerInfoVar *)
            ListNext(hConfigList, (PLISTLINK) pSIV))
    {
///////////////////////////////////////////////////////////////////////////////
//  Write variabile name
///////////////////////////////////////////////////////////////////////////////
        char           *pszQuoted = StrQuote(pSIV->pszName, '"');

        if (pszQuoted == NULL)
            return (ErrGetErrorCode());

        fprintf(pProfileFile, "%s\t", pszQuoted);

        SysFree(pszQuoted);

///////////////////////////////////////////////////////////////////////////////
//  Write variabile value
///////////////////////////////////////////////////////////////////////////////
        pszQuoted = StrQuote(pSIV->pszValue, '"');

        if (pszQuoted == NULL)
            return (ErrGetErrorCode());

        fprintf(pProfileFile, "%s\n", pszQuoted);

        SysFree(pszQuoted);
    }

    return (0);

}



static int      SvrLoadServerConfig(HSLIST & hConfigList, const char *pszFilePath)
{

    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(pszFilePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pProfileFile = fopen(pszFilePath, "rt");

    if (pProfileFile == NULL)
    {
        RLckUnlockSH(hResLock);

        ErrSetErrorCode(ERR_NO_USER_PRFILE);
        return (ERR_NO_USER_PRFILE);
    }

    char            szProfileLine[SVR_PROFILE_LINE_MAX] = "";

    while (MscGetConfigLine(szProfileLine, sizeof(szProfileLine) - 1, pProfileFile) != NULL)
    {
        char          **ppszStrings = SvrGetLineStrings(szProfileLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if (iFieldsCount == 2)
        {
            ServerInfoVar  *pSIV = SvrAllocVar(ppszStrings[0], ppszStrings[1]);

            if (pSIV != NULL)
                ListAddTail(hConfigList, (PLISTLINK) pSIV);
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pProfileFile);

    RLckUnlockSH(hResLock);

    return (0);

}



int             SvrGetMessageID(SYS_UINT64 * pullMessageID)
{

    char            szMsgIDFile[SYS_MAX_PATH] = "";

    CfgGetRootPath(szMsgIDFile);
    strcat(szMsgIDFile, MESSAGEID_FILE);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szMsgIDFile, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pMsgIDFile = fopen(szMsgIDFile, "r+b");

    if (pMsgIDFile == NULL)
    {
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_FILE_OPEN, szMsgIDFile);
        return (ERR_FILE_OPEN);
    }

    char            szMessageID[128] = "";

    if ((MscGetString(pMsgIDFile, szMessageID, sizeof(szMessageID) - 1) == NULL) ||
            !isdigit(szMessageID[0]))
    {
        fclose(pMsgIDFile);
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_INVALID_FILE, szMsgIDFile);
        return (ERR_INVALID_FILE);
    }

    if (sscanf(szMessageID, SYS_LLU_FMT, pullMessageID) != 1)
    {
        fclose(pMsgIDFile);
        RLckUnlockEX(hResLock);

        ErrSetErrorCode(ERR_INVALID_FILE, szMsgIDFile);
        return (ERR_INVALID_FILE);
    }

    ++*pullMessageID;

    fseek(pMsgIDFile, 0, SEEK_SET);

    fprintf(pMsgIDFile, SYS_LLU_FMT "\r\n", *pullMessageID);

    fclose(pMsgIDFile);

    RLckUnlockEX(hResLock);

    return (0);

}



char           *SvrGetLogsDir(char *pszLogsPath)
{

    CfgGetRootPath(pszLogsPath);
    strcat(pszLogsPath, SVR_LOGS_DIR);

    return (pszLogsPath);

}



char           *SvrGetSpoolDir(char *pszSpoolPath)
{

    CfgGetRootPath(pszSpoolPath);
    strcat(pszSpoolPath, SMTP_SPOOL_DIR);

    return (pszSpoolPath);

}



char           *SvrGetSpoolSubDir(char const * pszSubDir, char *pszSpoolSubPath)
{

    SvrGetSpoolDir(pszSpoolSubPath);

    AppendSlash(pszSpoolSubPath);
    strcat(pszSpoolSubPath, pszSubDir);

    return (pszSpoolSubPath);

}



char           *SvrGetSpoolFilePath(const char *pszSpoolFileName, char *pszSpoolFilePath)
{

    SvrGetSpoolDir(pszSpoolFilePath);

    AppendSlash(pszSpoolFilePath);
    strcat(pszSpoolFilePath, pszSpoolFileName);

    return (pszSpoolFilePath);

}



char           *SvrGetSpoolTmpFilePath(const char *pszSpoolFileName, char *pszSpoolTmpFilePath)
{

    SvrGetSpoolSubDir(SMTP_SPOOL_TMP_DIR, pszSpoolTmpFilePath);

    AppendSlash(pszSpoolTmpFilePath);
    strcat(pszSpoolTmpFilePath, pszSpoolFileName);

    return (pszSpoolTmpFilePath);

}



int             SvrLockSpoolFile(const char *pszFileName)
{

    char            szSpoolLocksPath[SYS_MAX_PATH] = "";

    SvrGetSpoolSubDir(SMTP_SPOOL_LOCKS_DIR, szSpoolLocksPath);

    AppendSlash(szSpoolLocksPath);
    strcat(szSpoolLocksPath, pszFileName);

    if (SysLockFile(szSpoolLocksPath) < 0)
        return (ErrGetErrorCode());

    return (0);

}



void            SvrUnlockSpoolFile(const char *pszFileName)
{

    char            szSpoolLocksPath[SYS_MAX_PATH] = "";

    SvrGetSpoolSubDir(SMTP_SPOOL_LOCKS_DIR, szSpoolLocksPath);

    AppendSlash(szSpoolLocksPath);
    strcat(szSpoolLocksPath, pszFileName);

    SysUnlockFile(szSpoolLocksPath);

}



int             SvrClearSpoolLocksDir(void)
{

    char            szSpoolLocksPath[SYS_MAX_PATH] = "";

    SvrGetSpoolSubDir(SMTP_SPOOL_LOCKS_DIR, szSpoolLocksPath);

    return (MscClearDirectory(szSpoolLocksPath));

}



int             SvrGetSpoolFileLock(char *pszSpoolFileName, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry)
{

    char            szSpoolPath[SYS_MAX_PATH] = "";

    SvrGetSpoolDir(szSpoolPath);


    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    char            szMsgFileName[SYS_MAX_PATH] = "";
    FSCAN_HANDLE    hFileScan = MscFirstFile(szSpoolPath, 0, szMsgFileName);

    if (hFileScan != INVALID_FSCAN_HANDLE)
    {
        do
        {
            if (!IsDotFilename(szMsgFileName) && (SvrLockSpoolFile(szMsgFileName) == 0))
            {
                int             iReadyResult = SvrReadyToProcess(szMsgFileName,
                        iRetryTimeout, iRetryIncrRatio, iMaxRetry);

                if (iReadyResult == 0)
                {
                    strcpy(pszSpoolFileName, szMsgFileName);

                    MscCloseFindFile(hFileScan);
                    RLckUnlockEX(hResLock);
                    return (0);
                }

///////////////////////////////////////////////////////////////////////////////
//  Check spool file removing ( max number of retries )
///////////////////////////////////////////////////////////////////////////////
                if (iReadyResult == ERR_SPOOL_FILE_EXPIRED)
                {
///////////////////////////////////////////////////////////////////////////////
//  Send a mail error notify to the sender. "SvrTXErrorNotifySender_LK" must be
//  called with the spool lock _ON_ !!
///////////////////////////////////////////////////////////////////////////////
                    SvrTXErrorNotifySender_LK(szMsgFileName,
                            "The maximum number of tentatives has been reached.");

///////////////////////////////////////////////////////////////////////////////
//  Be advised that "SvrSpoolRemoveFile_LK" and "SvrSpoolMoveToFile_LK"
//  must be used only with the spool lock _ON_ !!
///////////////////////////////////////////////////////////////////////////////
                    if (SvrRemoveSpoolErrors())
                        SvrSpoolRemoveFile_LK(szMsgFileName);
                    else
                        SvrSpoolMoveToFile_LK(szMsgFileName, SMTP_SPOOL_ERROR_DIR);
                }

                SvrUnlockSpoolFile(szMsgFileName);
            }

        } while (MscNextFile(hFileScan, szMsgFileName));

        MscCloseFindFile(hFileScan);
    }


    RLckUnlockEX(hResLock);

    ErrSetErrorCode(ERR_NO_SMTP_SPOOL_FILES);
    return (ERR_NO_SMTP_SPOOL_FILES);

}



static char    *SvrGetSpoolLogFile(char const * pszSpoolFileName, char *pszSpoolLogFile)
{

    char            szSpoolPath[SYS_MAX_PATH] = "";

    SvrGetSpoolSubDir(SMTP_SPOOL_LOGS_DIR, szSpoolPath);

    AppendSlash(szSpoolPath);


    sprintf(pszSpoolLogFile, "%s%s.log", szSpoolPath, pszSpoolFileName);

    return (pszSpoolLogFile);

}




int             SvrSpoolErrLogMessage(char const * pszSpoolFileName, char const * pszFormat,...)
{

    char            szSpoolLogFile[SYS_MAX_PATH] = "";

    SvrGetSpoolLogFile(pszSpoolFileName, szSpoolLogFile);


    va_list         Args;

    va_start(Args, pszFormat);

    if (ErrFileVLogMessage(szSpoolLogFile, pszFormat, Args) < 0)
    {
        va_end(Args);
        return (ErrGetErrorCode());
    }

    va_end(Args);

    return (0);

}




static int      SvrReadyToProcess(char const * pszSpoolFileName, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry)
{

    char            szSpoolLogFile[SYS_MAX_PATH] = "";

    SvrGetSpoolLogFile(pszSpoolFileName, szSpoolLogFile);


    time_t          tCurr;

    time(&tCurr);


    FILE           *pLogFile = fopen(szSpoolLogFile, "r+t");

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

            ErrSetErrorCode(ERR_SPOOL_FILE_NOT_READY);
            return (ERR_SPOOL_FILE_NOT_READY);
        }
    }
    else
    {
        pLogFile = fopen(szSpoolLogFile, "w+t");

        if (pLogFile == NULL)
        {
            ErrSetErrorCode(ERR_FILE_CREATE, szSpoolLogFile);
            return (ERR_FILE_CREATE);
        }
    }

    fseek(pLogFile, 0, SEEK_END);


    fprintf(pLogFile, "[PeekTime] %lu\n", (unsigned long) tCurr);


    fclose(pLogFile);

    return (0);

}



static bool     SvrRemoveSpoolErrors(void)
{

    return (SvrTestConfigFlag("RemoveSpoolErrors", false));

}



static int      SvrSpoolMoveToFile_LK(const char *pszSpoolFileName, char const * pszSubDir)
{

    char            szCleanupFile[SYS_MAX_PATH] = "";

    SvrGetSpoolFilePath(pszSpoolFileName, szCleanupFile);


    char            szSpoolSubPath[SYS_MAX_PATH] = "";

    SvrGetSpoolSubDir(pszSubDir, szSpoolSubPath);

    AppendSlash(szSpoolSubPath);
    strcat(szSpoolSubPath, pszSpoolFileName);

///////////////////////////////////////////////////////////////////////////////
//  Move the spool file
///////////////////////////////////////////////////////////////////////////////
    if (SysMoveFile(szCleanupFile, szSpoolSubPath) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Move the log file ( same name of spool file plus  .log )
///////////////////////////////////////////////////////////////////////////////
    SvrGetSpoolLogFile(pszSpoolFileName, szCleanupFile);

    strcat(szSpoolSubPath, ".log");

    if (MscMoveFile(szCleanupFile, szSpoolSubPath) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Check if exist a spooled copy of custom domain processing file
///////////////////////////////////////////////////////////////////////////////
    USmlGetDomainCustomSpoolFile(pszSpoolFileName, szCleanupFile);

    CheckRemoveFile(szCleanupFile);

    return (0);

}



int             SvrSpoolMoveToErrorsFile(const char *pszSpoolFileName)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    int             iMoveResult = SvrSpoolMoveToFile_LK(pszSpoolFileName, SMTP_SPOOL_ERROR_DIR);


    RLckUnlockEX(hResLock);

    return (iMoveResult);

}



static int      SvrSpoolRemoveFile_LK(const char *pszSpoolFileName)
{

    char            szCleanupFile[SYS_MAX_PATH] = "";

    SvrGetSpoolFilePath(pszSpoolFileName, szCleanupFile);

///////////////////////////////////////////////////////////////////////////////
//  Remove spool file
///////////////////////////////////////////////////////////////////////////////
    if (SysRemove(szCleanupFile) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Remove log file if exist
///////////////////////////////////////////////////////////////////////////////
    SvrGetSpoolLogFile(pszSpoolFileName, szCleanupFile);

    CheckRemoveFile(szCleanupFile);

///////////////////////////////////////////////////////////////////////////////
//  Check if exist a spooled copy of custom domain processing file
///////////////////////////////////////////////////////////////////////////////
    USmlGetDomainCustomSpoolFile(pszSpoolFileName, szCleanupFile);

    CheckRemoveFile(szCleanupFile);

    return (0);

}



int             SvrSpoolRemoveFile(const char *pszSpoolFileName)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    if (SvrSpoolRemoveFile_LK(pszSpoolFileName) < 0)
    {
        RLckUnlockEX(hResLock);
        return (ErrGetErrorCode());
    }


    RLckUnlockEX(hResLock);

    return (0);

}



static int      SvrCopyToSpool_LK(const char *pszFileName, const char *pszSpoolFileName)
{

    char            szSpoolTmpFile[SYS_MAX_PATH] = "",
                    szSpoolFile[SYS_MAX_PATH] = "";

    SvrGetSpoolTmpFilePath(pszSpoolFileName, szSpoolTmpFile);
    SvrGetSpoolFilePath(pszSpoolFileName, szSpoolFile);

///////////////////////////////////////////////////////////////////////////////
//  Do a copy to spool/tmp and then a fast system move to spool
///////////////////////////////////////////////////////////////////////////////
    if (MscCopyFile(szSpoolTmpFile, pszFileName) < 0)
        return (ErrGetErrorCode());

    if (SysMoveFile(szSpoolTmpFile, szSpoolFile) < 0)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Release SMAIL semaphore by 1
///////////////////////////////////////////////////////////////////////////////
    SYS_SEMAPHORE   SemSpoolID = SysConnectSemaphore(0, MAX_SPOOL_FILES, SemSMAILSpoolName);

    if (SemSpoolID == SYS_INVALID_SEMAPHORE)
        return (ErrGetErrorCode());

    SysReleaseSemaphore(SemSpoolID, 1);

    SysCloseSemaphore(SemSpoolID);

    return (0);

}



int             SvrCopyToSpool(const char *pszFileName, const char *pszSpoolFileName)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    int             iCopyResult = SvrCopyToSpool_LK(pszFileName, pszSpoolFileName);


    RLckUnlockEX(hResLock);

    return (iCopyResult);

}



static int      SvrMoveToSpool_LK(const char *pszFileName, const char *pszSpoolFileName)
{

    char            szSpoolTmpFile[SYS_MAX_PATH] = "",
                    szSpoolFile[SYS_MAX_PATH] = "";

    SvrGetSpoolTmpFilePath(pszSpoolFileName, szSpoolTmpFile);
    SvrGetSpoolFilePath(pszSpoolFileName, szSpoolFile);

///////////////////////////////////////////////////////////////////////////////
//  Do a copy&delete to spool/tmp and then a fast system move to spool
///////////////////////////////////////////////////////////////////////////////
    if (MscMoveFile(pszFileName, szSpoolTmpFile) < 0)
        return (ErrGetErrorCode());

    if (SysMoveFile(szSpoolTmpFile, szSpoolFile) < 0)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Release SMAIL semaphore by 1
///////////////////////////////////////////////////////////////////////////////
    SYS_SEMAPHORE   SemSpoolID = SysConnectSemaphore(0, MAX_SPOOL_FILES, SemSMAILSpoolName);

    if (SemSpoolID == SYS_INVALID_SEMAPHORE)
        return (ErrGetErrorCode());


    SysReleaseSemaphore(SemSpoolID, 1);


    SysCloseSemaphore(SemSpoolID);

    return (0);

}



static int      SvrMoveTmpToSpool_LK(char const *pszTmpSpoolFilePath, char *pszMessageFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Extract filename part and build spool path. No check is done on return
//  value of strrchr() coz the path is supposed to be OK.
///////////////////////////////////////////////////////////////////////////////
    char const     *pszSpoolFileName = strrchr(pszTmpSpoolFilePath, SYS_SLASH_CHAR) + 1;
    char            szSpoolFile[SYS_MAX_PATH] = "";

    SvrGetSpoolFilePath(pszSpoolFileName, szSpoolFile);

    if (pszMessageFile != NULL)
        StrNCpy(pszMessageFile, pszSpoolFileName, SYS_MAX_PATH);

///////////////////////////////////////////////////////////////////////////////
//  Do a fast system move from spool/tmp to spool
///////////////////////////////////////////////////////////////////////////////
    if (SysMoveFile(pszTmpSpoolFilePath, szSpoolFile) < 0)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Release SMAIL semaphore by 1
///////////////////////////////////////////////////////////////////////////////
    SYS_SEMAPHORE   SemSpoolID = SysConnectSemaphore(0, MAX_SPOOL_FILES, SemSMAILSpoolName);

    if (SemSpoolID == SYS_INVALID_SEMAPHORE)
        return (ErrGetErrorCode());


    SysReleaseSemaphore(SemSpoolID, 1);


    SysCloseSemaphore(SemSpoolID);

    return (0);

}



int             SvrMoveToSpool(const char *pszFileName, const char *pszSpoolFileName)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    int             iMoveResult = SvrMoveToSpool_LK(pszFileName, pszSpoolFileName);


    RLckUnlockEX(hResLock);

    return (iMoveResult);

}



int             SvrMoveTmpToSpool(char const *pszTmpSpoolFilePath, char *pszMessageFile)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    int             iMoveResult = SvrMoveTmpToSpool_LK(pszTmpSpoolFilePath, pszMessageFile);


    RLckUnlockEX(hResLock);

    return (iMoveResult);

}



int             SvrSpoolRemoveNotifySender(const char *pszSpoolFileName, char const * pszReason)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = SvrTXErrorNotifySender_LK(pszSpoolFileName, pszReason);

    if ((iNotifyResult == 0) && SvrRemoveSpoolErrors())
        SvrSpoolRemoveFile_LK(pszSpoolFileName);
    else
        SvrSpoolMoveToFile_LK(pszSpoolFileName, SMTP_SPOOL_ERROR_DIR);


    RLckUnlockEX(hResLock);

    return (0);

}



int             SvrSpoolRemoveNotifyRoot(const char *pszSpoolFileName, char const * pszReason)
{

    RLCK_HANDLE     hResLock = RLckLockEX(SMTP_SPOOL_DIR);

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = SvrTXErrorNotifyRoot_LK(pszSpoolFileName, pszReason);

    if ((iNotifyResult == 0) && SvrRemoveSpoolErrors())
        SvrSpoolRemoveFile_LK(pszSpoolFileName);
    else
        SvrSpoolMoveToFile_LK(pszSpoolFileName, SMTP_SPOOL_ERROR_DIR);


    RLckUnlockEX(hResLock);

    return (0);

}



int             SvrChargeSMAILSpool(void)
{

    char            szSpoolPath[SYS_MAX_PATH] = "";

    SvrGetSpoolDir(szSpoolPath);


    SYS_SEMAPHORE   SemSpoolID = SysConnectSemaphore(0, MAX_SPOOL_FILES, SemSMAILSpoolName);

    if (SemSpoolID == SYS_INVALID_SEMAPHORE)
        return (ErrGetErrorCode());

    char            szMsgFileName[SYS_MAX_PATH] = "";
    SYS_HANDLE      hFind = SysFirstFile(szSpoolPath, szMsgFileName);

    if (hFind != SYS_INVALID_HANDLE)
    {
        do
        {
            if (!SysIsDirectory(hFind) && !IsDotFilename(szMsgFileName))
                SysReleaseSemaphore(SemSpoolID, 1);

        } while (SysNextFile(hFind, szMsgFileName));

        SysFindClose(hFind);
    }


    SysCloseSemaphore(SemSpoolID);

    return (0);

}



int             SvrGetUniqueMessageTmpPath(char *pszMessagePath)
{

    char            szSpoolTmpPath[SYS_MAX_PATH] = "";

    SvrGetSpoolSubDir(SMTP_SPOOL_TMP_DIR, szSpoolTmpPath);

///////////////////////////////////////////////////////////////////////////////
//  Get unique spool file path
///////////////////////////////////////////////////////////////////////////////
    if (MscUniqueFile(szSpoolTmpPath, pszMessagePath) < 0)
        return (ErrGetErrorCode());


    return (0);

}



int             SvrGetUniqueMessageFile(char *pszMessageFile)
{
///////////////////////////////////////////////////////////////////////////////
//  Get unique spool/tmp file path
///////////////////////////////////////////////////////////////////////////////
    if (SvrGetUniqueMessageTmpPath(pszMessageFile) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Extract filename part
///////////////////////////////////////////////////////////////////////////////
    char            szFName[SYS_MAX_PATH] = "",
                    szExt[SYS_MAX_PATH] = "";

    MscSplitPath(pszMessageFile, NULL, szFName, szExt);

    sprintf(pszMessageFile, "%s%s", szFName, szExt);

    return (0);

}



static int      SvrTXErrorNotifySender_LK(SPLF_HANDLE hFSpool, char const * pszReason)
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
    char const     *const * ppszFrom = USmlGetMailFrom(hFSpool);
    int             iFromDomains = StrStringsCount(ppszFrom);
    char            szResponseFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szResponseFile);

    if (SvrBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, ppszFrom[iFromDomains - 1],
                    szResponseFile, pszReason) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szResponseFile);
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    SvrReleaseConfigHandle(hSvrConfig);


///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file. Be advised that "SvrMoveToSpool_LK" must be
//  called with the spool lock _ON_ !!
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolFile[SYS_MAX_PATH] = "";

    if ((SvrGetUniqueMessageFile(szSpoolFile) < 0) ||
            (SvrMoveToSpool_LK(szResponseFile, szSpoolFile) < 0))
    {
        ErrorPush();
        SysRemove(szResponseFile);
        return (ErrorPop());
    }

    return (0);

}



static int      SvrTXErrorNotifySender_LK(char const * pszMsgFileName, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Build the full file pathname
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolFilePath[SYS_MAX_PATH] = "";

    SvrGetSpoolFilePath(pszMsgFileName, szSpoolFilePath);


    SPLF_HANDLE     hFSpool = USmlCreateHandle(szSpoolFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = SvrTXErrorNotifySender_LK(hFSpool, pszReason);


    USmlCloseHandle(hFSpool);

    return (iNotifyResult);

}



static int      SvrTXErrorNotifyRoot_LK(SPLF_HANDLE hFSpool, char const * pszReason)
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

    SysGetTmpFile(szResponseFile);

    if (SvrBuildErrorRespose(szMailDomain, hFSpool, szPMAddress, szPMAddress,
                    szResponseFile, pszReason) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szResponseFile);
        SvrReleaseConfigHandle(hSvrConfig);
        return (ErrorPop());
    }

    SvrReleaseConfigHandle(hSvrConfig);


///////////////////////////////////////////////////////////////////////////////
//  Send error response mail file. Be advised that "SvrMoveToSpool_LK" must be
//  called with the spool lock _ON_ !!
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolFile[SYS_MAX_PATH] = "";

    if ((SvrGetUniqueMessageFile(szSpoolFile) < 0) ||
            (SvrMoveToSpool_LK(szResponseFile, szSpoolFile) < 0))
    {
        ErrorPush();
        SysRemove(szResponseFile);
        return (ErrorPop());
    }

    return (0);

}



static int      SvrTXErrorNotifyRoot_LK(char const * pszMsgFileName, char const * pszReason)
{
///////////////////////////////////////////////////////////////////////////////
//  Build the full file pathname
///////////////////////////////////////////////////////////////////////////////
    char            szSpoolFilePath[SYS_MAX_PATH] = "";

    SvrGetSpoolFilePath(pszMsgFileName, szSpoolFilePath);


    SPLF_HANDLE     hFSpool = USmlCreateHandle(szSpoolFilePath);

    if (hFSpool == INVALID_SPLF_HANDLE)
        return (ErrGetErrorCode());


    int             iNotifyResult = SvrTXErrorNotifyRoot_LK(hFSpool, pszReason);


    USmlCloseHandle(hFSpool);

    return (iNotifyResult);

}



int             SvrConfigVar(char const * pszVarName, char *pszVarValue, int iMaxVarValue,
                        SVRCFG_HANDLE hSvrConfig, char const * pszDefault)
{

    int             iReleaseConfig = 0;

    if (hSvrConfig == INVALID_SVRCFG_HANDLE)
    {
        if ((hSvrConfig = SvrGetConfigHandle()) == INVALID_SVRCFG_HANDLE)
            return (ErrGetErrorCode());

        ++iReleaseConfig;
    }


    char           *pszValue = SvrGetConfigVar(hSvrConfig, pszVarName, pszDefault);


    if (pszValue == NULL)
    {
        if (iReleaseConfig)
            SvrReleaseConfigHandle(hSvrConfig);

        ErrSetErrorCode(ERR_CFG_VAR_NOT_FOUND);
        return (ERR_CFG_VAR_NOT_FOUND);
    }

    strncpy(pszVarValue, pszValue, iMaxVarValue - 1);
    pszVarValue[iMaxVarValue - 1] = '\0';

    SysFree(pszValue);

    if (iReleaseConfig)
        SvrReleaseConfigHandle(hSvrConfig);

    return (0);

}



static int      SvrBuildErrorRespose(char const * pszSMTPDomain, SPLF_HANDLE hFSpool,
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
    fprintf(pRespFile, "%s: %s\r\n", SVR_MAILER_HDR, APP_NAME_VERSION_OS_STR);

///////////////////////////////////////////////////////////////////////////////
//  Write X-SMTP-Mailer-Error ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "%s: Message = [%s] Server = [%s]\r\n",
            SVR_SMTP_MAILER_ERROR_HDR, pszSpoolFileName, pszSMTPDomain);

///////////////////////////////////////////////////////////////////////////////
//  Write blank line ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Write error message ( mail data )
///////////////////////////////////////////////////////////////////////////////
    fprintf(pRespFile, "Error sending message [%s] from [%s].\r\n\r\n"
            "<Failure Reason>\r\n"
            "%s\r\n"
            "</Failure Reason>\r\n\r\n"
            "Below is reported the message header:\r\n"
            "\r\n", pszSpoolFileName, pszSMTPDomain, pszReason);


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
        if (StrNComp(szBuffer, SVR_SMTP_MAILER_ERROR_HDR) == 0)
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
