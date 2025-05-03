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


#ifndef _SYSDEP_H
#define _SYSDEP_H



#define LOG_LEV_DEBUG               0
#define LOG_LEV_MESSAGE             1
#define LOG_LEV_WARNING             2
#define LOG_LEV_ERROR               3

#define SYS_PRIORITY_LOWER          -1
#define SYS_PRIORITY_NORMAL         0
#define SYS_PRIORITY_HIGHER         +1


#define SYS_IS_VALID_FILENAME(f)    ((strcmp(f, ".") != 0) && (strcmp(f, "..") != 0))






int             SysInitLibrary(void);
void            SysCleanupLibrary(void);

SYS_SOCKET      SysCreateSocket(int iAddressFamily, int iType, int iProtocol);
void            SysCloseSocket(SYS_SOCKET SockFD, int iHardClose = 0);
int             SysBindSocket(SYS_SOCKET SockFD, const struct sockaddr * SockName, int iNameLen);
void            SysListenSocket(SYS_SOCKET SockFD, int iConnections);
int             SysRecvData(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout);
int             SysRecv(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout);
int             SysRecvDataFrom(SYS_SOCKET SockFD, struct sockaddr * pFrom, int iFromlen,
                        char *pszBuffer, int iBufferSize, int iTimeout);
int             SysSendData(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize, int iTimeout);
int             SysSend(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize, int iTimeout);
int             SysSendDataTo(SYS_SOCKET SockFD, const struct sockaddr * pTo,
                        int iToLen, char const * pszBuffer, int iBufferSize, int iTimeout);
int             SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR * pSockName, int iNameLen,
                        int iTimeout);
SYS_SOCKET      SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR * pSockName, int *iNameLen,
                        int iTimeout);
int             SysSelect(int iMaxFD, SYS_fd_set * pReadFDs, SYS_fd_set * pWriteFDs, SYS_fd_set * pExcptFDs,
                        int iTimeout);
int             SysSendFile(SYS_SOCKET SockFD, char const * pszFileName, int iTimeout,
                        int (*pSendCB) (void *) = NULL, void *pUserData = NULL);
int             SysSetupAddress(SYS_INET_ADDR & AddrInfo, int iFamily, NET_ADDRESS NetAddr, int iPortNo);
NET_ADDRESS     SysGetAddrAddress(SYS_INET_ADDR const & AddrInfo);
NET_ADDRESS     SysGetHostByName(char const * pszName);
int             SysGetHostByAddr(SYS_INET_ADDR const & AddrInfo, char *pszFQDN);
int             SysGetPeerInfo(SYS_SOCKET SockFD, SYS_INET_ADDR & AddrInfo);
int             SysGetSockInfo(SYS_SOCKET SockFD, SYS_INET_ADDR & AddrInfo);
char const     *SysInetNToA(SYS_INET_ADDR const & AddrInfo);
NET_ADDRESS     SysInetAddr(char const * pszDotName);

SYS_IPCNAME     SysCreateIPCName(void);

SYS_SEMAPHORE   SysCreateSemaphore(int iInitCount, int iMaxCount, SYS_IPCNAME SemName);
SYS_SEMAPHORE   SysConnectSemaphore(int iInitCount, int iMaxCount, SYS_IPCNAME SemName);
int             SysCloseSemaphore(SYS_SEMAPHORE SemID);
int             SysKillSemaphore(SYS_SEMAPHORE SemID);
int             SysWaitSemaphore(SYS_SEMAPHORE SemID, int iTimeout);
int             SysReleaseSemaphore(SYS_SEMAPHORE SemID, int iCount);

SYS_THREAD      SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData);
SYS_THREAD      SysCreateServiceThread(unsigned int (*pThreadProc) (void *), SYS_SOCKET SockFD);
void            SysCloseThread(SYS_THREAD ThreadID, int iForce);
int             SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority);
int             SysWaitThread(SYS_THREAD ThreadID, int iTimeout);
unsigned long   SysGetCurrentThreadId(void);
void            SysIgnoreThreadsExit(void);
int             SysExec(char const * pszCommand, char const * const * pszArgs, int iWaitTimeout = 0,
                        int iPriority = SYS_PRIORITY_NORMAL, int *piExitStatus = NULL);
void            SysSetBreakHandler(void (*BreakHandler) (void));

void           *SysAlloc(unsigned int uSize);
void            SysFree(void *pData);
void           *SysRealloc(void *pData, unsigned int uSize);

int             SysLockFile(const char *pszFileName, char const * pszLockExt = ".lock");
int             SysUnlockFile(const char *pszFileName, char const * pszLockExt = ".lock");

SYS_SHMEM       SysCreateSharedMem(unsigned int uSize, SYS_IPCNAME ShmName);
SYS_SHMEM       SysConnectSharedMem(unsigned int uSize, SYS_IPCNAME ShmName);
int             SysCloseSharedMem(SYS_SHMEM ShMemID);
int             SysKillSharedMem(SYS_SHMEM ShMemID);
void           *SysMapSharedMem(SYS_SHMEM ShMemID);
int             SysUnmapSharedMem(SYS_SHMEM ShMemID, void *pAddress);

int             SysEventLogV(char const * pszFormat, va_list Args);
int             SysEventLog(char const * pszFormat,...);
int             SysLogMessage(int iLogLevel, char const * pszFormat,...);
void            SysSleep(int iTimeout);
void            SysMsSleep(int iMsTimeout);
SYS_INT64       SysMsTime(void);
int             SysExistFile(const char *pszFilePath);
SYS_HANDLE      SysFirstFile(const char *pszPath, char *pszFileName);
int             SysIsDirectory(SYS_HANDLE hFind);
unsigned long   SysGetSize(SYS_HANDLE hFind);
int             SysNextFile(SYS_HANDLE hFind, char *pszFileName);
void            SysFindClose(SYS_HANDLE hFind);
int             SysGetFileInfo(char const * pszFileName, SYS_FILE_INFO & FI);
char           *SysStrDup(const char *pszString);
char           *SysGetEnv(const char *pszVarName);
char           *SysGetTmpFile(char *pszFileName);
int             SysRemove(const char *pszFileName);
int             SysMakeDir(const char *pszPath);
int             SysRemoveDir(const char *pszPath);
int             SysMoveFile(char const * pszOldName, char const * pszNewName);




#endif
