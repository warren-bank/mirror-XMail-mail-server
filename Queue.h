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


#ifndef _QUEUE_H
#define _QUEUE_H



#define QUEUE_MESS_DIR              "mess"
#define QUEUE_RSND_DIR              "rsnd"
#define QUEUE_INFO_DIR              "info"
#define QUEUE_TEMP_DIR              "temp"
#define QUEUE_SLOG_DIR              "slog"
#define QUEUE_LOCK_DIR              "lock"
#define QUEUE_CUST_DIR              "cust"
#define QUEUE_FROZ_DIR              "froz"

#define STD_QUEUEFS_DIRS_X_LEVEL    23

#define INVALID_NQS_HANDLE          ((NQS_HANDLE) 0)





typedef struct NQS_HANDLE_struct
{
}              *NQS_HANDLE;







int             QueHandlerInit(void);
int             QueHandlerCleanup(void);
int             QueCreateQueue(SharedBlock & SHB_Queue, char const * pszRootPath,
                        int iNumDirsLevel = STD_QUEUEFS_DIRS_X_LEVEL);
int             QueCloseQueue(SharedBlock & SHB_Queue);
int             QueGetTempFile(char const * pszRootPath, char *pszFilePath,
                        int iNumDirsLevel = STD_QUEUEFS_DIRS_X_LEVEL);
int             QueGetBasePath(char const * pszFilePath, char *pszBasePath,
                        char const ** ppszFileName = NULL, char const ** ppszQueueDir = NULL);
int             QueGetQueuePath(char const * pszFilePath, char const * pszQueueDir,
                        char * pszQueuePath);
int             QueGetFrozenList(char const * pszRootPath, char const * pszListFile);
int             QueCommitTempMessage(char const * pszFilePath);
int             QueLockMessage(char const * pszFilePath);
int             QueUnlockMessage(char const * pszFilePath);
int             QueResendMessage(char const * pszFilePath);
int             QueCleanupMessage(char const * pszFilePath, bool bLockQueue, bool bFreezeMessage);
NQS_HANDLE      QueCreateNewStream(char const * pszRootPath, int iNumDirsLevel);
int             QueCloseNewStream(NQS_HANDLE hQSHandle, bool bCommit);
FILE           *QueGetMessageStream(NQS_HANDLE hQSHandle);
FILE           *QueGetInfoStream(NQS_HANDLE hQSHandle);
char const     *QueGetMessageId(NQS_HANDLE hQSHandle);
int             QuePeekLockedFiles(char const * pszRootPath, char ** ppszMessFilePath,
                        int iMaxPeekFiles, int iRetryTimeout, int iRetryIncrRatio,
                        int iMaxRetry);
int             QueErrLogMessage(char const * pszMessFilePath, char const * pszFormat,...);
int             QueSpoolRemoveNotifySender(const char *pszMessFilePath, char const * pszReason,
                        bool bLockQueue = true);
int             QueSpoolRemoveNotifyRoot(const char *pszMessFilePath, char const * pszReason,
                        bool bLockQueue = true);




#endif
