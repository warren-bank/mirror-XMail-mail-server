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
#include "StrUtils.h"
#include "SList.h"
#include "MD5.h"
#include "Base64Enc.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "UsrUtils.h"
#include "SvrUtils.h"
#include "MessQueue.h"
#include "MailSvr.h"
#include "MiscUtils.h"









enum IPMapFileds
{
    ipmFromIP = 0,
    ipmFromMask,
    ipmAllow,
    ipmPrecedence,

    ipmMax
};

struct FileScan
{
    char            szListFile[SYS_MAX_PATH];
    FILE           *pListFile;
};










int             MscUniqueFile(char const * pszDir, char *pszFilePath)
{
///////////////////////////////////////////////////////////////////////////////
//  Get thread ID and host name
///////////////////////////////////////////////////////////////////////////////
    unsigned long   ulThreadID = SysGetCurrentThreadId();
    char            szHostName[MAX_HOST_NAME] = "";

    gethostname(szHostName, sizeof(szHostName) - 1);


    SYS_FILE_INFO   FI;

    do
    {
        SysMsSleep(2);

        SYS_INT64       iMsTime = SysMsTime();

        sprintf(pszFilePath, "%s" SYS_SLASH_STR SYS_LLU_FMT ".%lu.%s",
                pszDir, iMsTime, ulThreadID, szHostName);

    } while (SysGetFileInfo(pszFilePath, FI) == 0);


    return (0);

}



int             MscRecvTextFile(const char *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
                        int (*pStopProc) (void *), void *pParam)
{

    FILE           *pFile = fopen(pszFileName, "wt");

    if (pFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
        return (ERR_FILE_CREATE);
    }

    char            szBuffer[2048] = "";

    while (BSckGetString(hBSock, szBuffer, sizeof(szBuffer) - 1, iTimeout) != NULL)
    {
        if (strcmp(szBuffer, ".") == 0)
            break;

        if (szBuffer[0] == '.')
            fprintf(pFile, "%s\n", szBuffer + 1);
        else
            fprintf(pFile, "%s\n", szBuffer);


        if ((pStopProc != NULL) && pStopProc(pParam))
        {
            fclose(pFile);
            return (ErrGetErrorCode());
        }
    }

    fclose(pFile);

    return (0);

}



int             MscSendTextFile(const char *pszFileName, BSOCK_HANDLE hBSock, int iTimeout,
                        int (*pStopProc) (void *), void *pParam)
{

    FILE           *pFile = fopen(pszFileName, "rt");

    if (pFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszFileName);
        return (ERR_FILE_OPEN);
    }

    char            szBuffer[2048] = "";

    while (MscFGets(szBuffer, sizeof(szBuffer) - 1, pFile) != NULL)
    {
        if (szBuffer[0] == '.')
            for (int ii = strlen(szBuffer); ii >= 0; ii--)
                szBuffer[ii + 1] = szBuffer[ii];

        if (BSckSendString(hBSock, szBuffer, iTimeout) <= 0)
        {
            fclose(pFile);
            return (ErrGetErrorCode());
        }


        if ((pStopProc != NULL) && pStopProc(pParam))
        {
            fclose(pFile);
            return (ErrGetErrorCode());
        }
    }

    fclose(pFile);

    return (BSckSendString(hBSock, ".", iTimeout));

}



char           *MscTranslatePath(char *pszPath)
{

    for (int ii = 0; pszPath[ii] != '\0'; ii++)
    {
        switch (pszPath[ii])
        {
            case ('/'):
            case ('\\'):
                pszPath[ii] = SYS_SLASH_CHAR;
                break;
        }
    }

    return (pszPath);

}



void           *MscLoadFile(char const * pszFilePath, unsigned int &uFileSize)
{

    FILE           *pFile = fopen(pszFilePath, "rb");

    if (pFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszFilePath);
        return (NULL);
    }

    fseek(pFile, 0, SEEK_END);


    uFileSize = (unsigned int) ftell(pFile);

///////////////////////////////////////////////////////////////////////////////
//  Alloc one extra byte to enable placing a '\0' to terminate an eventual
//  string representation and to avoid SysAlloc() to fail if uFileSize == 0
///////////////////////////////////////////////////////////////////////////////
    void           *pFileData = SysAlloc(uFileSize + 1);

    if (pFileData == NULL)
    {
        fclose(pFile);
        return (NULL);
    }

    rewind(pFile);

    if (uFileSize != 0)
        fread(pFileData, uFileSize, 1, pFile);

    fclose(pFile);

    return (pFileData);

}



int             MscLockFile(const char *pszFileName, int iMaxWait, int iWaitStep)
{

    while ((iMaxWait > 0) && (SysLockFile(pszFileName) < 0))
    {
        SysSleep(iWaitStep);

        iMaxWait -= iWaitStep;
    }

    return ((iMaxWait > 0) ? 0 : SysLockFile(pszFileName));

}




int             MscGetTimeNbrString(char *pszTimeStr, int iStringSize, time_t tTime)
{

    if (tTime == 0)
        time(&tTime);


    struct tm       tmSession;

    SysLocalTime(&tTime, &tmSession);

    SysSNPrintf(pszTimeStr, iStringSize, "%04d-%02d-%02d %02d:%02d:%02d",
            tmSession.tm_year + 1900,
            tmSession.tm_mon + 1,
            tmSession.tm_mday,
            tmSession.tm_hour,
            tmSession.tm_min,
            tmSession.tm_sec);

    return (0);

}




int             MscGetTime(struct tm & tmLocal, int &iDiffHours, int &iDiffMins,
                        time_t tCurr)
{

    if (tCurr == 0)
        time(&tCurr);

    SysLocalTime(&tCurr, &tmLocal);


    struct tm       tmTimeLOC = tmLocal,
                    tmTimeGM;

    SysGMTime(&tCurr, &tmTimeGM);

    tmTimeLOC.tm_isdst = 0;
    tmTimeGM.tm_isdst = 0;

    time_t          tLocal = mktime(&tmTimeLOC),
                    tGM = mktime(&tmTimeGM);

    int             iSecsDiff = (int) difftime(tLocal, tGM),
                    iSignDiff = Sign(iSecsDiff),
                    iMinutes = Abs(iSecsDiff) / 60;

    iDiffMins = iMinutes % 60;
    iDiffHours = iSignDiff * (iMinutes / 60);


    return (0);

}




int             MscGetTimeStr(char *pszTimeStr, int iStringSize, time_t tCurr)
{

    int             iDiffHours = 0,
                    iDiffMins = 0;
    struct tm       tmTime;

    MscGetTime(tmTime, iDiffHours, iDiffMins, tCurr);


    char            szDiffTime[128] = "";

    if (iDiffHours > 0)
        sprintf(szDiffTime, " +%02d%02d", iDiffHours, iDiffMins);
    else
        sprintf(szDiffTime, " -%02d%02d", -iDiffHours, iDiffMins);


    strftime(pszTimeStr, iStringSize - strlen(szDiffTime) - 1, "%a, %d %b %Y %H:%M:%S", &tmTime);

    strcat(pszTimeStr, szDiffTime);


    return (0);

}



int             MscGetDirectorySize(char const * pszPath, bool bRecurse, unsigned long &ulDirSize,
                        unsigned long &ulNumFiles, int (*pFNValidate) (char const *))
{

    char            szFileName[SYS_MAX_PATH] = "";
    SYS_HANDLE      hFind = SysFirstFile(pszPath, szFileName);

    if (hFind == SYS_INVALID_HANDLE)
        return (ErrGetErrorCode());

    ulDirSize = 0;

    do
    {
        if (SysIsDirectory(hFind))
        {
            if (bRecurse && SYS_IS_VALID_FILENAME(szFileName))
            {
                unsigned long   ulSubDirSize = 0,
                                ulSubNumFiles = 0;
                char            szSubPath[SYS_MAX_PATH] = "";

                strcpy(szSubPath, pszPath);
                AppendSlash(szSubPath);
                strcat(szSubPath, szFileName);

                if (MscGetDirectorySize(szSubPath, bRecurse, ulSubDirSize,
                        ulSubNumFiles, pFNValidate) < 0)
                {
                    ErrorPush();
                    SysFindClose(hFind);
                    return (ErrorPop());
                }

                ulNumFiles += ulSubNumFiles;
                ulDirSize += ulSubDirSize;
            }
        }
        else if ((pFNValidate == NULL) || pFNValidate(szFileName))
        {
            ++ulNumFiles;

            ulDirSize += SysGetSize(hFind);
        }

    } while (SysNextFile(hFind, szFileName));

    SysFindClose(hFind);

    return (0);

}




FSCAN_HANDLE    MscFirstFile(char const * pszPath, int iListDirs, char *pszFileName)
{

    FileScan       *pFS = (FileScan *) SysAlloc(sizeof(FileScan));

    if (pFS == NULL)
        return (INVALID_FSCAN_HANDLE);

    SysGetTmpFile(pFS->szListFile);

    if (MscGetFileList(pszPath, pFS->szListFile, iListDirs) < 0)
    {
        if (SysExistFile(pFS->szListFile))
            SysRemove(pFS->szListFile);
        SysFree(pFS);
        return (INVALID_FSCAN_HANDLE);
    }

    if ((pFS->pListFile = fopen(pFS->szListFile, "rb")) == NULL)
    {
        SysRemove(pFS->szListFile);
        SysFree(pFS);
        return (INVALID_FSCAN_HANDLE);
    }

    if (MscGetString(pFS->pListFile, pszFileName, SYS_MAX_PATH - 1) == NULL)
    {
        fclose(pFS->pListFile);
        SysRemove(pFS->szListFile);
        SysFree(pFS);
        return (INVALID_FSCAN_HANDLE);
    }

    return ((FSCAN_HANDLE) pFS);

}



int             MscNextFile(FSCAN_HANDLE hFileScan, char *pszFileName)
{

    FileScan       *pFS = (FileScan *) hFileScan;

    if (MscGetString(pFS->pListFile, pszFileName, SYS_MAX_PATH - 1) == NULL)
        return (0);

    return (1);

}



void            MscCloseFindFile(FSCAN_HANDLE hFileScan)
{

    FileScan       *pFS = (FileScan *) hFileScan;

    fclose(pFS->pListFile);

    SysRemove(pFS->szListFile);

    SysFree(pFS);

}



int             MscGetFileList(char const * pszPath, const char *pszListFile,
                        int iListDirs)
{

    FILE           *pListFile = fopen(pszListFile, "wb");

    if (pListFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    char            szFileName[SYS_MAX_PATH] = "";
    SYS_HANDLE      hFind = SysFirstFile(pszPath, szFileName);

    if (hFind != SYS_INVALID_HANDLE)
    {
        do
        {
            if (iListDirs || !SysIsDirectory(hFind))
                fprintf(pListFile, "%s\r\n", szFileName);

        } while (SysNextFile(hFind, szFileName));

        SysFindClose(hFind);
    }

    fclose(pListFile);

    return (0);

}



int             MscCreateEmptyFile(const char *pszFileName)
{

    FILE           *pFile = fopen(pszFileName, "wb");

    if (pFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }

    fclose(pFile);

    return (0);

}



int             MscClearDirectory(const char *pszPath, int iRecurseSubs)
{

    char            szFileName[SYS_MAX_PATH] = "";
    SYS_HANDLE      hFind = SysFirstFile(pszPath, szFileName);

    if (hFind == SYS_INVALID_HANDLE)
        return (0);

    char            szTmpFileName[SYS_MAX_PATH] = "";

    SysGetTmpFile(szTmpFileName);

    FILE           *pFile = fopen(szTmpFileName, "w+t");

    if (pFile == NULL)
    {
        SysFindClose(hFind);
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }

    int             iFileCount = 0;

    do
    {
        if (SysIsDirectory(hFind))
        {
            if (iRecurseSubs && SYS_IS_VALID_FILENAME(szFileName))
            {
                char            szSubPath[SYS_MAX_PATH] = "";

                strcpy(szSubPath, pszPath);
                AppendSlash(szSubPath);
                strcat(szSubPath, szFileName);

                if (MscClearDirectory(szSubPath, iRecurseSubs) < 0)
                {
                    fclose(pFile);
                    SysRemove(szTmpFileName);
                    return (ErrGetErrorCode());
                }

                if (SysRemoveDir(szSubPath) < 0)
                {
                    fclose(pFile);
                    SysRemove(szTmpFileName);
                    return (ErrGetErrorCode());
                }
            }
        }
        else
        {
            fprintf(pFile, "%s\n", szFileName);

            ++iFileCount;
        }

    } while (SysNextFile(hFind, szFileName));

    SysFindClose(hFind);

    if (iFileCount > 0)
    {
        fseek(pFile, 0, SEEK_SET);

        while (MscFGets(szFileName, sizeof(szFileName) - 1, pFile) != NULL)
        {
            char            szFilePath[SYS_MAX_PATH] = "";

            strcpy(szFilePath, pszPath);
            AppendSlash(szFilePath);
            strcat(szFilePath, szFileName);

            if (SysRemove(szFilePath) < 0)
            {
                fclose(pFile);
                SysRemove(szTmpFileName);
                return (ErrGetErrorCode());
            }
        }
    }

    fclose(pFile);

    SysRemove(szTmpFileName);

    return (0);

}



int             MscCopyFile(const char *pszCopyTo, const char *pszCopyFrom)
{

    FILE           *pFileIn = fopen(pszCopyFrom, "rb");

    if (pFileIn == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

    FILE           *pFileOut = fopen(pszCopyTo, "wb");

    if (pFileOut == NULL)
    {
        fclose(pFileIn);
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }

    unsigned int    uReaded;
    char            szBuffer[2048];

    do
    {
        uReaded = fread(szBuffer, 1, sizeof(szBuffer), pFileIn);

        if (uReaded > 0)
        {
            if (fwrite(szBuffer, 1, uReaded, pFileOut) != uReaded)
            {
                fclose(pFileOut);
                fclose(pFileIn);
                SysRemove(pszCopyTo);

                ErrSetErrorCode(ERR_FILE_WRITE);
                return (ERR_FILE_WRITE);
            }
        }

    } while (uReaded == sizeof(szBuffer));

    fclose(pFileOut);
    fclose(pFileIn);

    return (0);

}



int             MscCopyFile(FILE * pFileOut, FILE * pFileIn, unsigned long ulBaseOffset,
                        unsigned long ulCopySize)
{
///////////////////////////////////////////////////////////////////////////////
//  Setup copy size and seek start byte
///////////////////////////////////////////////////////////////////////////////
    if (ulBaseOffset == (unsigned long) -1)
        ulBaseOffset = (unsigned long) ftell(pFileIn);

    fseek(pFileIn, 0, SEEK_END);

    unsigned long   ulFileSize = (unsigned long) ftell(pFileIn);

    if (ulCopySize == (unsigned long) -1)
        ulCopySize = ulFileSize - ulBaseOffset;
    else
        ulCopySize = Min(ulCopySize, ulFileSize - ulBaseOffset);

    fseek(pFileIn, ulBaseOffset, SEEK_SET);

///////////////////////////////////////////////////////////////////////////////
//  Copy bytes
///////////////////////////////////////////////////////////////////////////////
    char            szBuffer[2048];

    while (ulCopySize > 0)
    {
        unsigned int    uToRead = (unsigned int) Min(ulCopySize, sizeof(szBuffer)),
                        uReaded = fread(szBuffer, 1, uToRead, pFileIn);

        if (uReaded > 0)
        {
            if (fwrite(szBuffer, 1, uReaded, pFileOut) != uReaded)
            {
                ErrSetErrorCode(ERR_FILE_WRITE);
                return (ERR_FILE_WRITE);
            }

            ulCopySize -= uReaded;
        }

        if (uReaded != uToRead)
        {
            ErrSetErrorCode(ERR_FILE_READ);
            return (ERR_FILE_READ);
        }
    }

    return (0);

}



int             MscMoveFile(char const * pszOldName, char const * pszNewName)
{

    if (MscCopyFile(pszNewName, pszOldName) < 0)
        return (ErrGetErrorCode());

    return (SysRemove(pszOldName));

}



char           *MscGetString(FILE * pFile, char *pszBuffer, int iMaxChars)
{

    return ((fgets(pszBuffer, iMaxChars, pFile) != NULL) ? StrEOLTrim(pszBuffer) : NULL);

}




char           *MscFGets(char *pszLine, int iLineSize, FILE * pFile)
{

    if (fgets(pszLine, iLineSize, pFile) == NULL)
        return (NULL);

    int             iLineLength = strlen(pszLine) - 1;

    if (iLineLength >= 0)
        pszLine[iLineLength] = '\0';

    return (pszLine);

}




char           *MscGetConfigLine(char *pszLine, int iLineSize, FILE * pFile,
                        bool bSkipComments)
{

    while (MscFGets(pszLine, iLineSize, pFile) != NULL)
    {

        if ((strlen(pszLine) > 0) &&
                (!bSkipComments || (pszLine[0] != TAB_COMMENT_CHAR)))
            return (pszLine);

    }


    ErrSetErrorCode(ERR_FILE_EOF);
    return (NULL);

}




int             MscGetPeerHost(SYS_SOCKET SockFD, char *pszFQDN)
{

    SYS_INET_ADDR   PeerInfo;

    if (SysGetPeerInfo(SockFD, PeerInfo) < 0)
        return (ErrGetErrorCode());

    return (SysGetHostByAddr(PeerInfo, pszFQDN));

}



int             MscGetSockHost(SYS_SOCKET SockFD, char *pszFQDN)
{

    SYS_INET_ADDR   SockInfo;

    if (SysGetSockInfo(SockFD, SockInfo) < 0)
        return (ErrGetErrorCode());

    return (SysGetHostByAddr(SockInfo, pszFQDN));

}



int             MscGetServerAddress(char const * pszServer, NET_ADDRESS & NetAddr)
{

    if ((NetAddr = SysInetAddr(pszServer)) == SYS_INVALID_NET_ADDRESS)
        NetAddr = SysGetHostByName(pszServer);

    if (NetAddr == SYS_INVALID_NET_ADDRESS)
    {
        ErrSetErrorCode(ERR_BAD_SERVER_ADDR, pszServer);
        return (ERR_BAD_SERVER_ADDR);
    }

    return (0);

}



int             MscSplitFQDN(const char *pszFQDN, char *pszHost, char *pszDomain)
{

    const char     *pszDot = strchr(pszFQDN, '.');

    if (pszDot == NULL)
    {
        if (pszHost != NULL)
            strcpy(pszHost, pszFQDN);

        if (pszDomain != NULL)
            SetEmptyString(pszDomain);
    }
    else
    {
        if (pszHost != NULL)
        {
            int             iHostLength = (int) (pszDot - pszFQDN);

            strncpy(pszHost, pszFQDN, iHostLength);
            pszHost[iHostLength] = '\0';
        }

        if (pszDomain != NULL)
        {
            strcpy(pszDomain, pszDot + 1);

            int             iDomainLength = strlen(pszDomain);

            if ((iDomainLength > 0) && (pszDomain[iDomainLength - 1] == '.'))
                pszDomain[iDomainLength - 1] = '\0';
        }
    }

    return (0);

}



char           *MscLogFilePath(char const * pszLogFile, char *pszLogFilePath)
{

    time_t          tCurrent;

    time(&tCurrent);

    unsigned long   ulRotStep = (unsigned long) (3600L * iLogRotateHours),
                    ulTimeZone = SysGetTimeZone();
    time_t          tLogFileTime = (time_t) (NbrFloor((unsigned long) tCurrent - ulTimeZone,
                            ulRotStep) + ulTimeZone);
    struct tm       tmLogFileTime;
    char            szLogsDir[SYS_MAX_PATH] = "";

    SysLocalTime(&tLogFileTime, &tmLogFileTime);

    SvrGetLogsDir(szLogsDir);
    AppendSlash(szLogsDir);

    sprintf(pszLogFilePath, "%s%s-%04d%02d%02d%02d%02d",
            szLogsDir, pszLogFile,
            tmLogFileTime.tm_year + 1900,
            tmLogFileTime.tm_mon + 1,
            tmLogFileTime.tm_mday,
            tmLogFileTime.tm_hour,
            tmLogFileTime.tm_min);


    return (pszLogFilePath);

}



int             MscFileLog(char const * pszLogFile, char const * pszFormat,...)
{

    char            szLogFilePath[SYS_MAX_PATH] = "";

    MscLogFilePath(pszLogFile, szLogFilePath);


    FILE           *pLogFile = fopen(szLogFilePath, "a+t");

    if (pLogFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }


    va_list         Args;

    va_start(Args, pszFormat);

    vfprintf(pLogFile, pszFormat, Args);

    va_end(Args);


    fclose(pLogFile);

    return (0);

}



int             MscSplitPath(char const * pszFilePath, char *pszDir, char *pszFName,
                        char *pszExt)
{

    char const     *pszSlash = strrchr(pszFilePath, SYS_SLASH_CHAR);
    char const     *pszFile = NULL;

    if (pszSlash != NULL)
    {
        pszFile = pszSlash + 1;

        if (pszDir != NULL)
        {
            int             iDirLength = (int) (pszFile - pszFilePath);

            strncpy(pszDir, pszFilePath, iDirLength);
            pszDir[iDirLength] = '\0';
        }
    }
    else
    {
        pszFile = pszFilePath;

        if (pszDir != NULL)
            SetEmptyString(pszDir);
    }

    char const     *pszDot = strrchr(pszFile, '.');

    if (pszDot != NULL)
    {
        if (pszFName != NULL)
        {
            int             iNameLength = (int) (pszDot - pszFile);

            strncpy(pszFName, pszFile, iNameLength);
            pszFName[iNameLength] = '\0';
        }

        if (pszExt != NULL)
            strcpy(pszExt, pszDot);
    }
    else
    {
        if (pszFName != NULL)
            strcpy(pszFName, pszFile);

        if (pszExt != NULL)
            SetEmptyString(pszExt);
    }

    return (0);

}



int             MscGetFileName(char const * pszFilePath, char *pszFileName)
{

    char const     *pszSlash = strrchr(pszFilePath, SYS_SLASH_CHAR);

    strcpy(pszFileName, (pszSlash != NULL) ? (pszSlash + 1) : pszFilePath);

    return (0);

}



int             MscCreateClientSocket(char const * pszServer, int iPortNo, int iSockType,
                        SYS_SOCKET * pSockFD, SYS_INET_ADDR * pSvrAddr,
                        SYS_INET_ADDR * pSockAddr, int iTimeout)
{
///////////////////////////////////////////////////////////////////////////////
//  Get server address
///////////////////////////////////////////////////////////////////////////////
    NET_ADDRESS     NetAddr;

    if (MscGetServerAddress(pszServer, NetAddr) < 0)
        return (ErrGetErrorCode());


    SYS_INET_ADDR   SvrAddr;

    SysSetupAddress(SvrAddr, AF_INET, NetAddr, htons(iPortNo));


    SYS_SOCKET      SockFD = SysCreateSocket(AF_INET, iSockType, 0);

    if (SockFD == SYS_INVALID_SOCKET)
        return (ErrGetErrorCode());


    if (SysConnect(SockFD, &SvrAddr, sizeof(SvrAddr), iTimeout) < 0)
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        return (ErrorPop());
    }

    SYS_INET_ADDR   SockAddr;

    ZeroData(SockAddr);

    if (SysGetSockInfo(SockFD, SockAddr) < 0)
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        return (ErrorPop());
    }


    *pSockFD = SockFD;

    if (pSvrAddr != NULL)
        *pSvrAddr = SvrAddr;

    if (pSockAddr != NULL)
        *pSockAddr = SockAddr;

    return (0);

}



int             MscCreateServerSockets(int iNumAddr, ServerNetPath const * pSvrPath, int iPortNo,
                        int iListenSize, SYS_SOCKET * pSockFDs, int &iNumSockFDs)
{

    if (iNumAddr == 0)
    {
        SYS_SOCKET      SvrSockFD = SysCreateSocket(AF_INET, SOCK_STREAM, 0);

        if (SvrSockFD == SYS_INVALID_SOCKET)
            return (ErrGetErrorCode());

        SYS_INET_ADDR   InSvrAddr;

        SysSetupAddress(InSvrAddr, AF_INET, htonl(INADDR_ANY), htons(iPortNo));

        if (SysBindSocket(SvrSockFD, (struct sockaddr *) & InSvrAddr, sizeof(InSvrAddr)) < 0)
        {
            ErrorPush();
            SysCloseSocket(SvrSockFD);
            return (ErrorPop());
        }

        SysListenSocket(SvrSockFD, iListenSize);

        *pSockFDs = SvrSockFD;
        iNumSockFDs = 1;
    }
    else
    {
        iNumSockFDs = 0;

        for (int ii = 0; ii < iNumAddr; ii++)
        {
            SYS_SOCKET      SvrSockFD = SysCreateSocket(AF_INET, SOCK_STREAM, 0);

            if (SvrSockFD == SYS_INVALID_SOCKET)
            {
                ErrorPush();
                for (--iNumSockFDs; iNumSockFDs >= 0; iNumSockFDs--)
                    SysCloseSocket(pSockFDs[iNumSockFDs]);
                return (ErrorPop());
            }

            SYS_INET_ADDR   InSvrAddr;

            SysSetupAddress(InSvrAddr, AF_INET, pSvrPath[ii].NetAddr,
                    htons((pSvrPath[ii].iPortNo > 0) ? pSvrPath[ii].iPortNo : iPortNo));

            if (SysBindSocket(SvrSockFD, (struct sockaddr *) & InSvrAddr, sizeof(InSvrAddr)) < 0)
            {
                ErrorPush();
                SysCloseSocket(SvrSockFD);
                for (--iNumSockFDs; iNumSockFDs >= 0; iNumSockFDs--)
                    SysCloseSocket(pSockFDs[iNumSockFDs]);
                return (ErrorPop());
            }

            SysListenSocket(SvrSockFD, iListenSize);

            pSockFDs[iNumSockFDs++] = SvrSockFD;
        }
    }

    return (0);

}



int             MscGetMaxSockFD(SYS_SOCKET const * pSockFDs, int iNumSockFDs)
{

    int             iMaxFD = 0;

    for (int ii = 0; ii < iNumSockFDs; ii++)
        if (iMaxFD < (int) pSockFDs[ii])
            iMaxFD = (int) pSockFDs[ii];

    return (iMaxFD);

}




int             MscAcceptServerConnection(SYS_SOCKET const * pSockFDs, int iNumSockFDs,
                        SYS_SOCKET * pConnSockFD, int &iNumConnSockFD, int iTimeout)
{

    int             ii;
    SYS_fd_set      fdReadSet;

    ZeroData(fdReadSet);
    SYS_FD_ZERO(&fdReadSet);

    for (ii = 0; ii < iNumSockFDs; ii++)
        SYS_FD_SET(pSockFDs[ii], &fdReadSet);


    int             iSelectResult = SysSelect(MscGetMaxSockFD(pSockFDs, iNumSockFDs),
            &fdReadSet, NULL, NULL, iTimeout);


    if (iSelectResult < 0)
        return (ErrGetErrorCode());


    iNumConnSockFD = 0;

    for (ii = 0; ii < iNumSockFDs; ii++)
    {
        if (SYS_FD_ISSET(pSockFDs[ii], &fdReadSet))
        {
            SYS_INET_ADDR   ConnAddr;
            int             iConnAddrLength = sizeof(ConnAddr);

            ZeroData(ConnAddr);

            SYS_SOCKET      ConnSockFD = SysAccept(pSockFDs[ii], &ConnAddr,
                    &iConnAddrLength, iTimeout);

            if (ConnSockFD != SYS_INVALID_SOCKET)
                pConnSockFD[iNumConnSockFD++] = ConnSockFD;
        }
    }

    return (0);

}




int             MscLoadAddressFilter(char const * const * ppszFilter, int iNumTokens,
                        AddressFilter & AF)
{

    ZeroData(AF);

    if (iNumTokens > 1)
    {
        *((NET_ADDRESS *) AF.Addr) = SysInetAddr(ppszFilter[0]);
        *((NET_ADDRESS *) AF.Mask) = SysInetAddr(ppszFilter[1]);
    }
    else
    {
        *((NET_ADDRESS *) AF.Addr) = SysInetAddr(ppszFilter[0]);

        for (int ii = 0; ii < sizeof(NET_ADDRESS); ii++)
            AF.Mask[ii] = (AF.Addr[ii] != 0) ? 0xff : 0x00;
    }

    return (0);

}




bool            MscAddressMatch(AddressFilter const & AF, NET_ADDRESS TestAddr)
{

    SYS_UINT8       ByteAddr[sizeof(NET_ADDRESS)];

    *((NET_ADDRESS *) ByteAddr) = TestAddr;

    for (int ii = 0; ii < sizeof(NET_ADDRESS); ii++)
        if ((ByteAddr[ii] & AF.Mask[ii]) != (AF.Addr[ii] & AF.Mask[ii]))
            return (false);

    return (true);

}



int             MscCheckAllowedIP(char const * pszMapFile, const SYS_INET_ADDR & PeerInfo,
                        bool bDefault)
{

    FILE           *pMapFile = fopen(pszMapFile, "rt");

    if (pMapFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszMapFile);
        return (ERR_FILE_OPEN);
    }


    NET_ADDRESS     TestAddr = SysGetAddrAddress(PeerInfo);


    bool            bAllow = bDefault;
    int             iPrecedence = -1;
    char            szMapLine[512] = "";

    while (MscGetConfigLine(szMapLine, sizeof(szMapLine) - 1, pMapFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szMapLine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);
        AddressFilter   AF;

        if ((iFieldsCount >= ipmMax) &&
                (MscLoadAddressFilter(ppszStrings, iFieldsCount, AF) == 0) &&
                MscAddressMatch(AF, TestAddr))
        {
            int             iCurrPrecedence = atoi(ppszStrings[ipmPrecedence]);

            if (iCurrPrecedence >= iPrecedence)
            {
                iPrecedence = iCurrPrecedence;

                bAllow = (stricmp(ppszStrings[ipmAllow], "ALLOW") == 0) ? true : false;
            }
        }

        StrFreeStrings(ppszStrings);
    }

    fclose(pMapFile);


    if (!bAllow)
    {
        ErrSetErrorCode(ERR_IP_NOT_ALLOWED);
        return (ERR_IP_NOT_ALLOWED);
    }

    return (0);

}




int             MscMD5Authenticate(const char *pszPassword, const char *pszTimeStamp,
                        const char *pszDigest)
{

    char           *pszHash = StrSprint("%s%s", pszTimeStamp, pszPassword);

    if (pszHash == NULL)
        return (ErrGetErrorCode());

    char            szMD5[128] = "";

    do_md5_string(pszHash, strlen(pszHash), szMD5);

    SysFree(pszHash);


    if (stricmp(pszDigest, szMD5) != 0)
    {
        ErrSetErrorCode(ERR_MD5_AUTH_FAILED);
        return (ERR_MD5_AUTH_FAILED);
    }

    return (0);

}




char           *MscExtractServerTimeStamp(char const * pszResponse, char *pszTimeStamp,
                        int iMaxTimeStamp)
{

    char const     *pszStartTS = strchr(pszResponse, '<'),
                   *pszEndTS = strchr(pszResponse, '>');

    if ((pszStartTS == NULL) || (pszEndTS == NULL))
        return (NULL);

    int             iLengthTS = (int) (pszEndTS - pszStartTS) + 1;

    if (iLengthTS <= 0)
        return (NULL);

    iLengthTS = Min(iLengthTS, iMaxTimeStamp - 1);

    strncpy(pszTimeStamp, pszStartTS, iLengthTS);
    pszTimeStamp[iLengthTS] = '\0';

    return (pszTimeStamp);

}




int             MscBase64FileEncode(char const * pszBoundary, char const * pszFilePath,
                        FILE * pFileOut)
{

    FILE           *pFileIn = fopen(pszFilePath, "rb");

    if (pFileIn == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszFilePath);
        return (ERR_FILE_OPEN);
    }

    char            szFName[SYS_MAX_PATH] = "",
                    szExt[SYS_MAX_PATH] = "";

    MscSplitPath(pszFilePath, NULL, szFName, szExt);


    fprintf(pFileOut, "\r\n--%s\r\n"
            "Content-Type: application/octet-stream;\r\n"
            "\tname=\"%s%s\"\r\n"
            "Content-Transfer-Encoding: base64\r\n"
            "Content-Disposition: attachment;\r\n"
            "\tfilename=\"%s%s\"\r\n\r\n", pszBoundary, szFName, szExt, szFName, szExt);


///////////////////////////////////////////////////////////////////////////////
//  Note that sizeof(szFileBuffer) must be multiple of 3
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uReadSize;
    char            szFileBuffer[80 * 3] = "",
                    szEncBuffer[512] = "";

    do
    {
        uReadSize = fread(szFileBuffer, 1, sizeof(szFileBuffer), pFileIn);

        if (uReadSize > 0)
        {
            unsigned int    uOutLength = sizeof(szEncBuffer);

            encode64(szFileBuffer, uReadSize, szEncBuffer, sizeof(szEncBuffer) - 1, &uOutLength);

            unsigned int    uWriteSize = 80;
            char           *pszWrite = szEncBuffer;

            for (; uOutLength > 0; uOutLength -= uWriteSize, pszWrite += uWriteSize)
            {
                uWriteSize = Min(uOutLength, uWriteSize);

                if (!fwrite(pszWrite, uWriteSize, 1, pFileOut))
                {
                    fclose(pFileIn);

                    ErrSetErrorCode(ERR_FILE_WRITE);
                    return (ERR_FILE_WRITE);
                }

                fputs("\r\n", pFileOut);
            }
        }

    } while (uReadSize == sizeof(szFileBuffer));


    fclose(pFileIn);

    return (0);

}




int             MscRootedName(char const * pszHostName)
{

    char const     *pszDot = strrchr(pszHostName, '.');

    return ((pszDot == NULL) ? 0 : ((strlen(pszDot) == 0) ? 1 : 0));

}




int             MscCramMD5(char const * pszSecret, char const * pszChallenge,
                        char *pszDigest)
{

    int             iLenght = (int) strlen(pszSecret);
    struct md5_ctx  ctx;
    unsigned char   isecret[64];
    unsigned char   osecret[64];
    unsigned char   md5secret[16];

    if (iLenght > 64)
    {
        md5_init_ctx(&ctx);
        md5_process_bytes(pszSecret, iLenght, &ctx);
        md5_finish_ctx(&ctx, md5secret);

        pszSecret = (char const *) md5secret;
        iLenght = 16;
    }

    ZeroData(isecret);
    memcpy(isecret, pszSecret, iLenght);

    ZeroData(osecret);
    memcpy(osecret, pszSecret, iLenght);

    for (int ii = 0; ii < 64; ii++)
    {
        isecret[ii] ^= 0x36;
        osecret[ii] ^= 0x5c;
    }


    md5_init_ctx(&ctx);
    md5_process_bytes(isecret, 64, &ctx);
    md5_process_bytes((unsigned char *) pszChallenge, (int) strlen(pszChallenge), &ctx);
    md5_finish_ctx(&ctx, md5secret);


    md5_init_ctx(&ctx);
    md5_process_bytes(osecret, 64, &ctx);
    md5_process_bytes(md5secret, 16, &ctx);
    md5_finish_ctx(&ctx, md5secret);

    md5_hex(md5secret, pszDigest);

    return (0);

}




SYS_UINT32      MscHashString(char const * pszBuffer, int iLength, SYS_UINT32 uHashInit)
{

    SYS_UINT32      uHashVal = uHashInit;

    while (iLength > 0)
    {
        --iLength;

        uHashVal += (uHashVal << 5);
        uHashVal ^= (SYS_UINT32) * pszBuffer++;
    }

    return (uHashVal);

}




int             MscSetupServerNetPath(ServerNetPath & SvrPath, char const * pszConnSpec,
                        int iDefPortNo)
{

    char const     *pszColon = strchr(pszConnSpec, ':');

    ZeroData(SvrPath);

    if (pszColon != NULL)
    {
        int             iIPLen = (int) (pszColon - pszConnSpec);
        char            szIP[256] = "";

        strncpy(szIP, pszConnSpec, iIPLen = Min(iIPLen, sizeof(szIP) - 1));
        szIP[iIPLen] = '\0';

        SvrPath.NetAddr = SysInetAddr(szIP);
        SvrPath.iPortNo = atoi(pszColon + 1);
    }
    else
    {
        SvrPath.NetAddr = SysInetAddr(pszConnSpec);
        SvrPath.iPortNo = iDefPortNo;
    }

    return (0);

}




int             MscSplitAddressPort(char const * pszConnSpec, char *pszAddress,
                        int &iPortNo, int iDefPortNo)
{

    char const     *pszColon = strchr(pszConnSpec, ':');

    if (pszColon != NULL)
    {
        int             iAddrLen = (int) (pszColon - pszConnSpec);

        strncpy(pszAddress, pszConnSpec, iAddrLen);
        pszAddress[iAddrLen] = '\0';

        iPortNo = atoi(pszColon + 1);
    }
    else
    {
        strcpy(pszAddress, pszConnSpec);

        iPortNo = iDefPortNo;
    }

    return (0);

}




SYS_UINT16      MscReadUint16(void const * pData)
{

#if defined(CPU_NEED_ALIGNMENT)

    SYS_UINT16      uValue;

    memcpy(&uValue, pData, sizeof(uValue));

    return (uValue);

#else           // #if defined(CPU_NEED_ALIGNMENT)

    return (*(SYS_UINT16 const *) pData);

#endif          // #if defined(CPU_NEED_ALIGNMENT)

}




SYS_UINT32      MscReadUint32(void const * pData)
{

#if defined(CPU_NEED_ALIGNMENT)

    SYS_UINT32      uValue;

    memcpy(&uValue, pData, sizeof(uValue));

    return (uValue);

#else           // #if defined(CPU_NEED_ALIGNMENT)

    return (*(SYS_UINT32 const *) pData);

#endif          // #if defined(CPU_NEED_ALIGNMENT)

}




SYS_UINT64      MscReadUint64(void const * pData)
{

#if defined(CPU_NEED_ALIGNMENT)

    SYS_UINT64      uValue;

    memcpy(&uValue, pData, sizeof(uValue));

    return (uValue);

#else           // #if defined(CPU_NEED_ALIGNMENT)

    return (*(SYS_UINT64 const *) pData);

#endif          // #if defined(CPU_NEED_ALIGNMENT)

}




void           *MscWriteUint16(void *pData, SYS_UINT16 uValue)
{

#if defined(CPU_NEED_ALIGNMENT)

    return (memcpy(pData, &uValue, sizeof(uValue)));

#else           // #if defined(CPU_NEED_ALIGNMENT)

    *((SYS_UINT16 *) pData) = uValue;

    return (pData);

#endif          // #if defined(CPU_NEED_ALIGNMENT)

}




void           *MscWriteUint32(void *pData, SYS_UINT32 uValue)
{

#if defined(CPU_NEED_ALIGNMENT)

    return (memcpy(pData, &uValue, sizeof(uValue)));

#else           // #if defined(CPU_NEED_ALIGNMENT)

    *((SYS_UINT32 *) pData) = uValue;

    return (pData);

#endif          // #if defined(CPU_NEED_ALIGNMENT)

}




void           *MscWriteUint64(void *pData, SYS_UINT64 uValue)
{

#if defined(CPU_NEED_ALIGNMENT)

    return (memcpy(pData, &uValue, sizeof(uValue)));

#else           // #if defined(CPU_NEED_ALIGNMENT)

    *((SYS_UINT64 *) pData) = uValue;

    return (pData);

#endif          // #if defined(CPU_NEED_ALIGNMENT)

}




int             MscCmdStringCheck(char const * pszString)
{

    for (; *pszString != '\0'; pszString++)
        if (((*pszString < ' ') || (*pszString > '~')) && (*pszString != '\t'))
        {
            ErrSetErrorCode(ERR_BAD_CMDSTR_CHARS);
            return (ERR_BAD_CMDSTR_CHARS);
        }


    return (0);

}




int             MscGetSectionSize(FileSection const * pFS, unsigned long * pulSize)
{

    if (pFS->ulEndOffset == (unsigned long) -1)
    {
        SYS_FILE_INFO       FI;

        if (SysGetFileInfo(pFS->szFilePath, FI) < 0)
            return (ErrGetErrorCode());


        *pulSize = FI.ulSize - pFS->ulStartOffset;
    }
    else
        *pulSize = pFS->ulEndOffset - pFS->ulStartOffset;


    return (0);

}
