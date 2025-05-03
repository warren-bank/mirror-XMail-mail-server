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


#ifndef _SVRUTILS_H
#define _SVRUTILS_H




#define SVR_LOGS_DIR                "logs"
#define SMTP_SPOOL_TMP_DIR          "tmp"
#define SMTP_SPOOL_ERROR_DIR        "errors"
#define SMTP_SPOOL_LOCKS_DIR        "locks"
#define SMTP_SPOOL_LOGS_DIR         "logs"

#define INVALID_SVRCFG_HANDLE       ((SVRCFG_HANDLE) 0)





typedef struct SVRCFG_HANDLE_struct
{
}              *SVRCFG_HANDLE;






SVRCFG_HANDLE   SvrGetConfigHandle(int iWriteLock = 0);
void            SvrReleaseConfigHandle(SVRCFG_HANDLE hSvrConfig);
char           *SvrGetConfigVar(SVRCFG_HANDLE hSvrConfig, const char *pszName,
                        const char *pszDefault = NULL);
bool            SvrTestConfigFlag(char const * pszName, bool bDefault,
                        SVRCFG_HANDLE hSvrConfig = INVALID_SVRCFG_HANDLE);
int             SysFlushConfig(SVRCFG_HANDLE hSvrConfig);
int             SvrGetMessageID(SYS_UINT64 * pullMessageID);
char           *SvrGetLogsDir(char *pszLogsPath);
char           *SvrGetSpoolDir(char *pszSpoolPath);
char           *SvrGetSpoolSubDir(char const * pszSubDir, char *pszSpoolSubPath);
char           *SvrGetSpoolFilePath(const char *pszSpoolFileName, char *pszSpoolFilePath);
char           *SvrGetSpoolTmpFilePath(const char *pszSpoolFileName, char *pszSpoolTmpFilePath);
int             SvrLockSpoolFile(const char *pszFileName);
void            SvrUnlockSpoolFile(const char *pszFileName);
int             SvrClearSpoolLocksDir(void);
int             SvrGetSpoolFileLock(char *pszSpoolFileName, int iRetryTimeout,
                        int iRetryIncrRatio, int iMaxRetry);
int             SvrSpoolErrLogMessage(char const * pszSpoolFileName, char const * pszFormat,...);
int             SvrSpoolMoveToErrorsFile(const char *pszSpoolFileName);
int             SvrSpoolRemoveFile(const char *pszSpoolFileName);
int             SvrCopyToSpool(const char *pszFileName, const char *pszSpoolFileName);
int             SvrMoveToSpool(const char *pszFileName, const char *pszSpoolFileName);
int             SvrMoveTmpToSpool(char const *pszTmpSpoolFilePath, char *pszMessageFile);
int             SvrSpoolRemoveNotifySender(const char *pszSpoolFileName, char const * pszReason);
int             SvrSpoolRemoveNotifyRoot(const char *pszSpoolFileName, char const * pszReason);
int             SvrChargeSMAILSpool(void);
int             SvrGetUniqueMessageTmpPath(char *pszMessagePath);
int             SvrGetUniqueMessageFile(char *pszMessageFile);
int             SvrConfigVar(char const * pszVarName, char *pszVarValue, int iMaxVarValue,
                        SVRCFG_HANDLE hSvrConfig = INVALID_SVRCFG_HANDLE, char const * pszDefault = NULL);




#endif
