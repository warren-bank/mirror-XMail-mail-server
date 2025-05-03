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


#include "SysInclude.h"
#include "SysDep.h"
#include "AppDefines.h"






#define SHUTDOWN_RECV_TIMEOUT       2
#define SAIN_Addr(s)                (s).sin_addr.s_addr

#define SCHED_PRIORITY_INC          5

#define WAIT_PID_TIME_STEP          250
#define MAX_SPIN_COUNT              64
#define SPIN_SLEEP_TIME             (50 * 1000)

#define MIN_TCP_SEND_SIZE           1024
#define MAX_TCP_SEND_SIZE           (1024 * 8)
#define MIN_BYTES_SEC_TIMEOUT       64
#define STD_SENDFILE_BLKSIZE        (4096 * 2)

///////////////////////////////////////////////////////////////////////////////
//  Uncomment this if You want to use sendfile()
///////////////////////////////////////////////////////////////////////////////
#define USE_SENDFILE






struct SemData
{
    pthread_mutex_t Mtx;
    pthread_cond_t  WaitCond;
    int             iSemCounter;
    int             iMaxCount;
};

struct MutexData
{
    pthread_mutex_t Mtx;
    pthread_cond_t  WaitCond;
    int             iLocked;
};

struct EventData
{
    pthread_mutex_t Mtx;
    pthread_cond_t  WaitCond;
    int             iSignaled;
    int             iManualReset;
};

struct ThrData
{
    pthread_t       ThreadId;
    unsigned int    (*ThreadProc) (void *);
    void           *pThreadData;
    pthread_mutex_t Mtx;
    pthread_cond_t  ExitWaitCond;
    int             iThreadEnded;
    int             iExitCode;
    int             iUseCount;
};

struct FileFindData
{
    char            szPath[SYS_MAX_PATH];
    DIR            *pDIR;
    struct dirent   DE;
    struct stat     FS;
};

struct PIDWaitData
{
    SysListHead     LLink;
    pthread_t       WaitThreadId;
    pid_t           PID;
    int             iExitCode;
};







static char const *SysGetLastError(void);
static void     SysIgnoreProc(int iSignal);
static int      SysSetSockNoDelay(SYS_SOCKET SockFD, int iNoDelay);
static int      SysSetSocketsOptions(SYS_SOCKET SockFD);
static int      SysFreeThreadData(ThrData * pTD);
static void    *SysThreadStartup(void *pThreadData);
static void     SysSigChildHandler(int iSignal);
static int      SysThreadSetup(ThrData * pTD);
static void     SysThreadCleanup(ThrData * pTD);
static int      SysExitPID(pid_t PID, int iExitCode);
static int      SysWaitPID(pid_t PID, int * piExitCode, int iTimeout);
static void     SysBreakHandlerRoutine(int iSignal);
static SYS_SPINLOCK SysTestAndSet(SYS_SPINLOCK * pSpinLock);









static pthread_mutex_t LogMutex = PTHREAD_MUTEX_INITIALIZER;
static void     (*SysBreakHandler) (void) = NULL;
static SYS_SPINLOCK WaitPIDSpin = 0;
static SYS_LIST_HEAD(WaitPIDList);










static char const *SysGetLastError(void)
{

    static char     szMessage[1024] = "";

    snprintf(szMessage, sizeof(szMessage) - 1, "(0x%lX) %s", (unsigned long) errno, strerror(errno));

    return (szMessage);

}




static void     SysIgnoreProc(int iSignal)
{

    signal(iSignal, SysIgnoreProc);

}



int             SysInitLibrary(void)
{


    if (SysThreadSetup(NULL) < 0)
        return (ErrGetErrorCode());


    return (0);

}



void            SysCleanupLibrary(void)
{


    SysThreadCleanup(NULL);

}



SYS_SOCKET      SysCreateSocket(int iAddressFamily, int iType, int iProtocol)
{

    int             SockFD = socket(AF_INET, iType, iProtocol);

    if (SockFD == -1)
    {
        ErrSetErrorCode(ERR_SOCKET_CREATE);
        return (SYS_INVALID_SOCKET);
    }

    if (SysSetSocketsOptions((SYS_SOCKET) SockFD) < 0)
    {
        SysCloseSocket((SYS_SOCKET) SockFD);

        return (SYS_INVALID_SOCKET);
    }

    return ((SYS_SOCKET) SockFD);

}



static int      SysSetSockNoDelay(SYS_SOCKET SockFD, int iNoDelay)
{

    long            lSockFlags = fcntl((int) SockFD, F_GETFL, 0);

    if (lSockFlags == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    if (iNoDelay)
        lSockFlags |= O_NONBLOCK;
    else
        lSockFlags &= ~O_NONBLOCK;

    if (fcntl((int) SockFD, F_SETFL, lSockFlags) == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (0);

}



static int      SysSetSocketsOptions(SYS_SOCKET SockFD)
{

    int             iActivate = 1;

    if (setsockopt(SockFD, SOL_SOCKET, SO_REUSEADDR, &iActivate,
                    sizeof(iActivate)) != 0)
    {
        ErrSetErrorCode(ERR_SETSOCKOPT);
        return (ERR_SETSOCKOPT);
    }

///////////////////////////////////////////////////////////////////////////////
//  Disable linger
///////////////////////////////////////////////////////////////////////////////
    struct linger   Ling;

    ZeroData(Ling);
    Ling.l_onoff = 0;
    Ling.l_linger = 0;

    setsockopt(SockFD, SOL_SOCKET, SO_LINGER, (const char *) &Ling, sizeof(Ling));

///////////////////////////////////////////////////////////////////////////////
//  Set KEEPALIVE if supported
///////////////////////////////////////////////////////////////////////////////
    setsockopt(SockFD, SOL_SOCKET, SO_KEEPALIVE, &iActivate, sizeof(iActivate));


    return (0);

}



void            SysCloseSocket(SYS_SOCKET SockFD)
{

    close(SockFD);

}



int             SysBindSocket(SYS_SOCKET SockFD, const struct sockaddr * SockName, int iNameLen)
{

    if (bind((int) SockFD, SockName, iNameLen) == -1)
    {
        ErrSetErrorCode(ERR_SOCKET_BIND);
        return (ERR_SOCKET_BIND);
    }

    return (0);

}



void            SysListenSocket(SYS_SOCKET SockFD, int iConnections)
{

    listen((int) SockFD, iConnections);

}



int             SysRecvData(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{

    fd_set          rfds;
    struct timeval  tv;

    ZeroData(tv);
    tv.tv_sec = iTimeout;
    tv.tv_usec = 0;

    FD_ZERO(&rfds);
    FD_SET((int) SockFD, &rfds);

    if (select((int) SockFD + 1, &rfds, (fd_set *) 0, (fd_set *) 0, &tv) == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    if (!FD_ISSET((int) SockFD, &rfds))
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    int             iRecvBytes = recv((int) SockFD, pszBuffer, iBufferSize, 0);

    if (iRecvBytes == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iRecvBytes);

}



int             SysRecv(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{

    int             iRtxBytes = 0;

    while (iRtxBytes < iBufferSize)
    {
        int             iRtxCurrent = SysRecvData(SockFD, pszBuffer + iRtxBytes,
                iBufferSize - iRtxBytes, iTimeout);

        if (iRtxCurrent <= 0)
            return (iRtxBytes);

        iRtxBytes += iRtxCurrent;
    }

    return (iRtxBytes);

}



int             SysRecvDataFrom(SYS_SOCKET SockFD, struct sockaddr * pFrom, int iFromlen,
                        char *pszBuffer, int iBufferSize, int iTimeout)
{

    fd_set          rfds;
    struct timeval  tv;

    ZeroData(tv);
    tv.tv_sec = iTimeout;
    tv.tv_usec = 0;

    FD_ZERO(&rfds);
    FD_SET((int) SockFD, &rfds);

    if (select((int) SockFD + 1, &rfds, (fd_set *) 0, (fd_set *) 0, &tv) == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    if (!FD_ISSET((int) SockFD, &rfds))
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    socklen_t       SockALen = (socklen_t) iFromlen;
    int             iRecvBytes = recvfrom((int) SockFD, pszBuffer, iBufferSize, 0, pFrom, &SockALen);

    if (iRecvBytes == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iRecvBytes);

}



int             SysSendData(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize, int iTimeout)
{

    fd_set          wfds;
    struct timeval  tv;

    ZeroData(tv);
    tv.tv_sec = iTimeout;
    tv.tv_usec = 0;

    FD_ZERO(&wfds);
    FD_SET((int) SockFD, &wfds);

    if (select((int) SockFD + 1, (fd_set *) 0, &wfds, (fd_set *) 0, &tv) == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    if (!FD_ISSET((int) SockFD, &wfds))
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    int             iSendBytes = send((int) SockFD, pszBuffer, iBufferSize, 0);

    if (iSendBytes == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iSendBytes);

}



int             SysSend(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize, int iTimeout)
{

    int             iRtxBytes = 0;

    while (iRtxBytes < iBufferSize)
    {
        int             iRtxCurrent = SysSendData(SockFD, pszBuffer + iRtxBytes,
                iBufferSize - iRtxBytes, iTimeout);

        if (iRtxCurrent <= 0)
            return (iRtxBytes);

        iRtxBytes += iRtxCurrent;
    }

    return (iRtxBytes);

}



int             SysSendDataTo(SYS_SOCKET SockFD, const struct sockaddr * pTo,
                        int iToLen, char const * pszBuffer, int iBufferSize, int iTimeout)
{

    fd_set          wfds;
    struct timeval  tv;

    ZeroData(tv);
    tv.tv_sec = iTimeout;
    tv.tv_usec = 0;

    FD_ZERO(&wfds);
    FD_SET((int) SockFD, &wfds);

    if (select((int) SockFD + 1, (fd_set *) 0, &wfds, (fd_set *) 0, &tv) == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    if (!FD_ISSET((int) SockFD, &wfds))
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    int             iSendBytes = sendto((int) SockFD, pszBuffer, iBufferSize, 0, pTo, iToLen);

    if (iSendBytes == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iSendBytes);

}



int             SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR * pSockName, int iNameLen, int iTimeout)
{

    if (SysSetSockNoDelay(SockFD, 1) < 0)
        return (ErrGetErrorCode());

    if (connect((int) SockFD, (const struct sockaddr *) & pSockName->Addr, iNameLen) == 0)
    {
        SysSetSockNoDelay(SockFD, 0);
        return (0);
    }

    if ((errno != EINPROGRESS) && (errno != EWOULDBLOCK))
    {
        SysSetSockNoDelay(SockFD, 0);

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    fd_set          wfds;
    struct timeval  tv;

    FD_ZERO(&wfds);
    FD_SET((int) SockFD, &wfds);
    tv.tv_sec = iTimeout;
    tv.tv_usec = 0;

    if (select((int) SockFD + 1, (fd_set *) 0, &wfds, (fd_set *) 0, &tv) == -1)
    {
        SysSetSockNoDelay(SockFD, 0);

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    SysSetSockNoDelay(SockFD, 0);

    if (!FD_ISSET((int) SockFD, &wfds))
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    return (0);

}



SYS_SOCKET      SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR * pSockName, int *iNameLen, int iTimeout)
{

    fd_set          rfds;
    struct timeval  tv;

    ZeroData(tv);
    tv.tv_sec = iTimeout;
    tv.tv_usec = 0;

    FD_ZERO(&rfds);
    FD_SET((int) SockFD, &rfds);

    if (select((int) SockFD + 1, &rfds, (fd_set *) 0, (fd_set *) 0, &tv) == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    if (!FD_ISSET((int) SockFD, &rfds))
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    socklen_t       SockALen = (socklen_t) * iNameLen;
    int             iAcptSock = accept((int) SockFD,
            (struct sockaddr *) & pSockName->Addr, &SockALen);

    if (iAcptSock == -1)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }


    if (SysSetSocketsOptions((SYS_SOCKET) iAcptSock) < 0)
    {
        SysCloseSocket((SYS_SOCKET) iAcptSock);

        return (SYS_INVALID_SOCKET);
    }

    *iNameLen = (int) SockALen;

    return ((SYS_SOCKET) iAcptSock);

}



int             SysSelect(int iMaxFD, SYS_fd_set * pReadFDs, SYS_fd_set * pWriteFDs, SYS_fd_set * pExcptFDs,
                        int iTimeout)
{

    struct timeval  TV;

    ZeroData(TV);
    TV.tv_sec = iTimeout;
    TV.tv_usec = 0;


    int             iSelectResult = select(iMaxFD + 1, pReadFDs, pWriteFDs, pExcptFDs, &TV);


    if (iSelectResult == -1)
    {
        ErrSetErrorCode(ERR_SELECT);
        return (ERR_SELECT);
    }

    if (iSelectResult == 0)
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    return (iSelectResult);

}



int             SysSendFile(SYS_SOCKET SockFD, char const * pszFileName, int iTimeout,
                        int (*pSendCB) (void *), void *pUserData)
{

    int             iFileID = open(pszFileName, O_RDONLY);

    if (iFileID == -1)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

    unsigned long   ulFileSize = (unsigned long) lseek(iFileID, 0, SEEK_END);

    lseek(iFileID, 0, SEEK_SET);

#ifdef USE_SENDFILE

    unsigned long   ulSent = 0;

    while (ulSent < ulFileSize)
    {
        unsigned long   ulToSend = min(STD_SENDFILE_BLKSIZE, ulFileSize - ulSent);
        off_t           ulStartOffset = (off_t) ulSent;


        unsigned long   ulSendSize = (unsigned long) sendfile((int) SockFD, iFileID,
                &ulStartOffset, ulToSend);


        if (ulSendSize != ulToSend)
        {
            close(iFileID);
            ErrSetErrorCode(ERR_SENDFILE);
            return (ERR_SENDFILE);
        }

        if ((pSendCB != NULL) && (pSendCB(pUserData) < 0))
        {
            close(iFileID);
            ErrSetErrorCode(ERR_USER_BREAK);
            return (ERR_USER_BREAK);
        }

        ulSent += ulToSend;
    }

#else           // #ifdef USE_SENDFILE

    void           *pMapAddress = (void *) mmap((char *) 0, (size_t) ulFileSize, PROT_READ,
            MAP_SHARED, iFileID, 0);

    if (pMapAddress == (void *) -1)
    {
        close(iFileID);
        ErrSetErrorCode(ERR_MMAP);
        return (ERR_MMAP);
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup the maximum data transfer size
///////////////////////////////////////////////////////////////////////////////
    int             iSndBuffSize = 0;
    socklen_t       OptLenght = sizeof(iSndBuffSize);

    if (getsockopt(SockFD, SOL_SOCKET, SO_SNDBUF, (char *) &iSndBuffSize, &OptLenght) != 0)
        iSndBuffSize = MIN_TCP_SEND_SIZE;
    else
        iSndBuffSize = min(iSndBuffSize, MAX_TCP_SEND_SIZE);

///////////////////////////////////////////////////////////////////////////////
//  Send the file
///////////////////////////////////////////////////////////////////////////////
    unsigned long   ulSentBytes = 0;
    char           *pszBuffer = (char *) pMapAddress;

    while (ulSentBytes < ulFileSize)
    {
        int             iCurrSend = (int) min(iSndBuffSize, ulFileSize - ulSentBytes);

        if ((iCurrSend = SysSendData(SockFD, pszBuffer, iCurrSend,
                                max(iTimeout, iCurrSend / MIN_BYTES_SEC_TIMEOUT))) < 0)
        {
            ErrorPush();
            munmap((char *) pMapAddress, (size_t) ulFileSize);
            close(iFileID);
            return (ErrorPop());
        }

        if ((pSendCB != NULL) && (pSendCB(pUserData) < 0))
        {
            munmap((char *) pMapAddress, (size_t) ulFileSize);
            close(iFileID);
            ErrSetErrorCode(ERR_USER_BREAK);
            return (ERR_USER_BREAK);
        }

        pszBuffer += iCurrSend;
        ulSentBytes += (unsigned long) iCurrSend;
    }

    munmap((char *) pMapAddress, (size_t) ulFileSize);

#endif          // #ifdef USE_SENDFILE


    close(iFileID);

    return (0);

}



int             SysSetupAddress(SYS_INET_ADDR & AddrInfo, int iFamily, NET_ADDRESS NetAddr, int iPortNo)
{

    ZeroData(AddrInfo);
    AddrInfo.Addr.sin_family = iFamily;
    SAIN_Addr(AddrInfo.Addr) = NetAddr;
    AddrInfo.Addr.sin_port = iPortNo;

    return (0);

}



NET_ADDRESS     SysGetAddrAddress(SYS_INET_ADDR const & AddrInfo)
{

    return (SAIN_Addr(AddrInfo.Addr));

}



NET_ADDRESS     SysGetHostByName(char const * pszName)
{

    int             iErrorNo = 0;
    struct hostent *pHostEnt;
    struct hostent  HostEnt;
    char            szBuffer[1024];

    if ((gethostbyname_r(pszName, &HostEnt, szBuffer, sizeof(szBuffer),
                            &pHostEnt, &iErrorNo) != 0) || (pHostEnt == NULL) ||
            (pHostEnt->h_addr_list[0] == NULL))
        return (SYS_INVALID_NET_ADDRESS);


    NET_ADDRESS     Addr;

    memcpy(&Addr, pHostEnt->h_addr_list[0], sizeof(Addr));

    return (Addr);

}



int             SysGetHostByAddr(SYS_INET_ADDR const & AddrInfo, char *pszFQDN)
{

    int             iErrorNo = 0;
    struct hostent *pHostEnt;
    struct hostent  HostEnt;
    char            szBuffer[1024];

    if ((gethostbyaddr_r((const char *) &SAIN_Addr(AddrInfo.Addr), sizeof(SAIN_Addr(AddrInfo.Addr)),
                            AF_INET, &HostEnt, szBuffer, sizeof(szBuffer), &pHostEnt, &iErrorNo) != 0) ||
            (pHostEnt == NULL) || (pHostEnt->h_name == NULL))
    {
        ErrSetErrorCode(ERR_GET_SOCK_HOST, SysInetNToA(AddrInfo));
        return (ERR_GET_SOCK_HOST);
    }

    strcpy(pszFQDN, pHostEnt->h_name);

    return (0);

}




int             SysGetPeerInfo(SYS_SOCKET SockFD, SYS_INET_ADDR & AddrInfo)
{

    ZeroData(AddrInfo);

    socklen_t       InfoSize = sizeof(AddrInfo.Addr);

    if (getpeername(SockFD, (struct sockaddr *) & AddrInfo.Addr, &InfoSize) == -1)
    {
        ErrSetErrorCode(ERR_GET_PEER_INFO);
        return (ERR_GET_PEER_INFO);
    }

    return (0);

}




int             SysGetSockInfo(SYS_SOCKET SockFD, SYS_INET_ADDR & AddrInfo)
{

    ZeroData(AddrInfo);

    socklen_t       InfoSize = sizeof(AddrInfo.Addr);

    if (getsockname(SockFD, (struct sockaddr *) & AddrInfo.Addr, &InfoSize) == -1)
    {
        ErrSetErrorCode(ERR_GET_SOCK_INFO);
        return (ERR_GET_SOCK_INFO);
    }

    return (0);

}




char const     *SysInetNToA(SYS_INET_ADDR const & AddrInfo)
{

    return (inet_ntoa(AddrInfo.Addr.sin_addr));

}




NET_ADDRESS     SysInetAddr(char const * pszDotName)
{

    return ((NET_ADDRESS) inet_addr(pszDotName));

}




SYS_SEMAPHORE   SysCreateSemaphore(int iInitCount, int iMaxCount)
{

    SemData        *pSD = (SemData *) SysAlloc(sizeof(SemData));

    if (pSD == NULL)
        return (SYS_INVALID_SEMAPHORE);

    if (pthread_mutex_init(&pSD->Mtx, NULL) != 0)
    {
        SysFree(pSD);

        ErrSetErrorCode(ERR_MUTEXINIT, NULL);
        return (SYS_INVALID_SEMAPHORE);
    }

    if (pthread_cond_init(&pSD->WaitCond, NULL) != 0)
    {
        pthread_mutex_destroy(&pSD->Mtx);
        SysFree(pSD);

        ErrSetErrorCode(ERR_CONDINIT, NULL);
        return (SYS_INVALID_SEMAPHORE);
    }

    pSD->iSemCounter = iInitCount;

    pSD->iMaxCount = iMaxCount;

    return ((SYS_SEMAPHORE) pSD);

}



int             SysCloseSemaphore(SYS_SEMAPHORE hSemaphore)
{

    SemData        *pSD = (SemData *) hSemaphore;

    pthread_cond_destroy(&pSD->WaitCond);

    pthread_mutex_destroy(&pSD->Mtx);

    SysFree(pSD);

    return (0);

}



int             SysWaitSemaphore(SYS_SEMAPHORE hSemaphore, int iTimeout)
{

    SemData        *pSD = (SemData *) hSemaphore;

    pthread_mutex_lock(&pSD->Mtx);

    if (iTimeout == SYS_INFINITE_TIMEOUT)
    {

        while (pSD->iSemCounter <= 0)
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pSD->Mtx);

            pthread_cond_wait(&pSD->WaitCond, &pSD->Mtx);

            pthread_cleanup_pop(0);
        }

        pSD->iSemCounter -= 1;

    }
    else
    {
        struct timeval  tvNow;
        struct timespec tsTimeout;

        gettimeofday(&tvNow, NULL);

        tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
        tsTimeout.tv_nsec = tvNow.tv_usec * 1000;

        int             iRetCode = 0;

        while ((pSD->iSemCounter <= 0) && (iRetCode != ETIMEDOUT))
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pSD->Mtx);

            iRetCode = pthread_cond_timedwait(&pSD->WaitCond, &pSD->Mtx, &tsTimeout);

            pthread_cleanup_pop(0);
        }

        if (iRetCode == ETIMEDOUT)
        {
            pthread_mutex_unlock(&pSD->Mtx);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        pSD->iSemCounter -= 1;

    }

    pthread_mutex_unlock(&pSD->Mtx);

    return (0);

}



int             SysReleaseSemaphore(SYS_SEMAPHORE hSemaphore, int iCount)
{

    SemData        *pSD = (SemData *) hSemaphore;

    pthread_mutex_lock(&pSD->Mtx);

    pSD->iSemCounter += iCount;

    if (pSD->iSemCounter > 0)
        pthread_cond_broadcast(&pSD->WaitCond);

    pthread_mutex_unlock(&pSD->Mtx);

    return (0);

}



int             SysTryWaitSemaphore(SYS_SEMAPHORE hSemaphore)
{

    SemData        *pSD = (SemData *) hSemaphore;

    pthread_mutex_lock(&pSD->Mtx);

    if (pSD->iSemCounter <= 0)
    {
        pthread_mutex_unlock(&pSD->Mtx);
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    pSD->iSemCounter -= 1;

    pthread_mutex_unlock(&pSD->Mtx);

    return (0);

}



SYS_MUTEX       SysCreateMutex(void)
{

    MutexData      *pMD = (MutexData *) SysAlloc(sizeof(MutexData));

    if (pMD == NULL)
        return (SYS_INVALID_MUTEX);

    if (pthread_mutex_init(&pMD->Mtx, NULL) != 0)
    {
        SysFree(pMD);
        ErrSetErrorCode(ERR_MUTEXINIT);
        return (SYS_INVALID_MUTEX);
    }

    if (pthread_cond_init(&pMD->WaitCond, NULL) != 0)
    {
        pthread_mutex_destroy(&pMD->Mtx);
        SysFree(pMD);
        ErrSetErrorCode(ERR_CONDINIT);
        return (SYS_INVALID_MUTEX);
    }

    pMD->iLocked = 0;

    return ((SYS_MUTEX) pMD);


}



int             SysCloseMutex(SYS_MUTEX hMutex)
{

    MutexData      *pMD = (MutexData *) hMutex;

    pthread_cond_destroy(&pMD->WaitCond);

    pthread_mutex_destroy(&pMD->Mtx);

    SysFree(pMD);

    return (0);

}



int             SysLockMutex(SYS_MUTEX hMutex, int iTimeout)
{

    MutexData      *pMD = (MutexData *) hMutex;

    pthread_mutex_lock(&pMD->Mtx);

    if (iTimeout == SYS_INFINITE_TIMEOUT)
    {

        while (pMD->iLocked)
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pMD->Mtx);

            pthread_cond_wait(&pMD->WaitCond, &pMD->Mtx);

            pthread_cleanup_pop(0);
        }

        pMD->iLocked = 1;

    }
    else
    {
        struct timeval  tvNow;
        struct timespec tsTimeout;

        gettimeofday(&tvNow, NULL);

        tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
        tsTimeout.tv_nsec = tvNow.tv_usec * 1000;

        int             iRetCode = 0;

        while (pMD->iLocked && (iRetCode != ETIMEDOUT))
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pMD->Mtx);

            iRetCode = pthread_cond_timedwait(&pMD->WaitCond, &pMD->Mtx, &tsTimeout);

            pthread_cleanup_pop(0);
        }

        if (iRetCode == ETIMEDOUT)
        {
            pthread_mutex_unlock(&pMD->Mtx);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        pMD->iLocked = 1;

    }

    pthread_mutex_unlock(&pMD->Mtx);

    return (0);

}



int             SysUnlockMutex(SYS_MUTEX hMutex)
{

    MutexData      *pMD = (MutexData *) hMutex;

    pthread_mutex_lock(&pMD->Mtx);

    pMD->iLocked = 0;

    pthread_cond_broadcast(&pMD->WaitCond);

    pthread_mutex_unlock(&pMD->Mtx);

    return (0);

}



int             SysTryLockMutex(SYS_MUTEX hMutex)
{

    MutexData      *pMD = (MutexData *) hMutex;

    pthread_mutex_lock(&pMD->Mtx);

    if (pMD->iLocked)
    {
        pthread_mutex_unlock(&pMD->Mtx);
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    pMD->iLocked = 1;

    pthread_mutex_unlock(&pMD->Mtx);

    return (0);

}



SYS_EVENT       SysCreateEvent(int iManualReset)
{

    EventData      *pED = (EventData *) SysAlloc(sizeof(EventData));

    if (pED == NULL)
        return (SYS_INVALID_EVENT);

    if (pthread_mutex_init(&pED->Mtx, NULL) != 0)
    {
        SysFree(pED);
        ErrSetErrorCode(ERR_MUTEXINIT);
        return (SYS_INVALID_EVENT);
    }

    if (pthread_cond_init(&pED->WaitCond, NULL) != 0)
    {
        pthread_mutex_destroy(&pED->Mtx);
        SysFree(pED);
        ErrSetErrorCode(ERR_CONDINIT);
        return (SYS_INVALID_EVENT);
    }

    pED->iSignaled = 0;
    pED->iManualReset = iManualReset;

    return ((SYS_EVENT) pED);

}



int             SysCloseEvent(SYS_EVENT hEvent)
{

    EventData      *pED = (EventData *) hEvent;

    pthread_cond_destroy(&pED->WaitCond);

    pthread_mutex_destroy(&pED->Mtx);

    SysFree(pED);

    return (0);

}



int             SysWaitEvent(SYS_EVENT hEvent, int iTimeout)
{

    EventData      *pED = (EventData *) hEvent;

    pthread_mutex_lock(&pED->Mtx);

    if (iTimeout == SYS_INFINITE_TIMEOUT)
    {

        while (!pED->iSignaled)
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pED->Mtx);

            pthread_cond_wait(&pED->WaitCond, &pED->Mtx);

            pthread_cleanup_pop(0);
        }

        if (!pED->iManualReset)
            pED->iSignaled = 0;

    }
    else
    {
        struct timeval  tvNow;
        struct timespec tsTimeout;

        gettimeofday(&tvNow, NULL);

        tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
        tsTimeout.tv_nsec = tvNow.tv_usec * 1000;

        int             iRetCode = 0;

        while (!pED->iSignaled && (iRetCode != ETIMEDOUT))
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pED->Mtx);

            iRetCode = pthread_cond_timedwait(&pED->WaitCond, &pED->Mtx, &tsTimeout);

            pthread_cleanup_pop(0);
        }

        if (iRetCode == ETIMEDOUT)
        {
            pthread_mutex_unlock(&pED->Mtx);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        if (!pED->iManualReset)
            pED->iSignaled = 0;

    }

    pthread_mutex_unlock(&pED->Mtx);

    return (0);

}



int             SysSetEvent(SYS_EVENT hEvent)
{

    EventData      *pED = (EventData *) hEvent;

    pthread_mutex_lock(&pED->Mtx);

    pED->iSignaled = 1;

    pthread_cond_broadcast(&pED->WaitCond);

    pthread_mutex_unlock(&pED->Mtx);

    return (0);

}



int             SysResetEvent(SYS_EVENT hEvent)
{

    EventData      *pED = (EventData *) hEvent;

    pthread_mutex_lock(&pED->Mtx);

    pED->iSignaled = 0;

    pthread_mutex_unlock(&pED->Mtx);

    return (0);

}



int             SysTryWaitEvent(SYS_EVENT hEvent)
{

    EventData      *pED = (EventData *) hEvent;

    pthread_mutex_lock(&pED->Mtx);

    if (!pED->iSignaled)
    {
        pthread_mutex_unlock(&pED->Mtx);
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    if (!pED->iManualReset)
        pED->iSignaled = 0;

    pthread_mutex_unlock(&pED->Mtx);

    return (0);

}



static int      SysFreeThreadData(ThrData * pTD)
{

    pthread_cond_destroy(&pTD->ExitWaitCond);

    pthread_mutex_destroy(&pTD->Mtx);

    SysFree(pTD);

    return (0);

}



static void    *SysThreadStartup(void *pThreadData)
{

    ThrData        *pTD = (ThrData *) pThreadData;

    SysThreadSetup(pTD);


    int             iExitCode;

    pthread_cleanup_push((void (*) (void *)) SysThreadCleanup, pTD);

    pTD->iExitCode = iExitCode = pTD->ThreadProc(pTD->pThreadData);

    pthread_cleanup_pop(1);

    return ((void *) iExitCode);

}



SYS_THREAD      SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData)
{

    ThrData        *pTD = (ThrData *) SysAlloc(sizeof(ThrData));

    if (pTD == NULL)
        return (SYS_INVALID_THREAD);

    pTD->ThreadProc = pThreadProc;
    pTD->pThreadData = pThreadData;
    pTD->iThreadEnded = 0;
    pTD->iExitCode = -1;
    pTD->iUseCount = 2;

    if (pthread_mutex_init(&pTD->Mtx, NULL) != 0)
    {
        SysFree(pTD);

        ErrSetErrorCode(ERR_MUTEXINIT);
        return (SYS_INVALID_THREAD);
    }

    if (pthread_cond_init(&pTD->ExitWaitCond, NULL) != 0)
    {
        pthread_mutex_destroy(&pTD->Mtx);
        SysFree(pTD);

        ErrSetErrorCode(ERR_CONDINIT);
        return (SYS_INVALID_THREAD);
    }

    pthread_attr_t  ThrAttr;

    pthread_attr_init(&ThrAttr);

    pthread_attr_setscope(&ThrAttr, PTHREAD_SCOPE_SYSTEM);

    if (pthread_create(&pTD->ThreadId, &ThrAttr, SysThreadStartup, pTD) != 0)
    {
        pthread_attr_destroy(&ThrAttr);
        pthread_cond_destroy(&pTD->ExitWaitCond);
        pthread_mutex_destroy(&pTD->Mtx);
        SysFree(pTD);

        ErrSetErrorCode(ERR_THREADCREATE);
        return (SYS_INVALID_THREAD);
    }

    pthread_attr_destroy(&ThrAttr);

    return ((SYS_THREAD) pTD);

}



SYS_THREAD      SysCreateServiceThread(unsigned int (*pThreadProc) (void *), SYS_SOCKET SockFD)
{

    return (SysCreateThread(pThreadProc, (void *) SockFD));

}



static void     SysSigChildHandler(int iSignal)
{

    int             iExitStatus,
                    iExitPid;

    while ((iExitPid = waitpid(0, &iExitStatus, WUNTRACED | WNOHANG)) > 0)
    {
        SysExitPID((pid_t) iExitPid, WEXITSTATUS(iExitStatus));

    }

    signal(iSignal, SysSigChildHandler);

}



static int      SysThreadSetup(ThrData * pTD)
{

    sigset_t        SigMask;

    sigemptyset(&SigMask);
    sigaddset(&SigMask, SIGALRM);
    sigaddset(&SigMask, SIGINT);
    sigaddset(&SigMask, SIGQUIT);
    sigaddset(&SigMask, SIGHUP);

    pthread_sigmask(SIG_BLOCK, &SigMask, NULL);


    signal(SIGPIPE, SysIgnoreProc);


    signal(SIGCHLD, SysSigChildHandler);

    if (pTD != NULL)
    {

    }

    return (0);

}



static void     SysThreadCleanup(ThrData * pTD)
{

    if (pTD != NULL)
    {
        pthread_mutex_lock(&pTD->Mtx);

        pTD->iThreadEnded = 1;

        pthread_cond_broadcast(&pTD->ExitWaitCond);

        if (--pTD->iUseCount == 0)
        {
            pthread_mutex_unlock(&pTD->Mtx);

            SysFreeThreadData(pTD);
        }
        else
            pthread_mutex_unlock(&pTD->Mtx);
    }

}



void            SysCloseThread(SYS_THREAD ThreadID, int iForce)
{

    ThrData        *pTD = (ThrData *) ThreadID;

    pthread_mutex_lock(&pTD->Mtx);

    pthread_detach(pTD->ThreadId);

    if (iForce && !pTD->iThreadEnded)
        pthread_cancel(pTD->ThreadId);

    if (--pTD->iUseCount == 0)
    {
        pthread_mutex_unlock(&pTD->Mtx);

        SysFreeThreadData(pTD);
    }
    else
        pthread_mutex_unlock(&pTD->Mtx);

}



int             SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority)
{

    ThrData        *pTD = (ThrData *) ThreadID;
    int             iPolicy;
    struct sched_param SchParam;

    if (pthread_getschedparam(pTD->ThreadId, &iPolicy, &SchParam) != 0)
    {
        ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
        return (ERR_SET_THREAD_PRIORITY);
    }

    int             iMinPriority = sched_get_priority_min(iPolicy),
                    iMaxPriority = sched_get_priority_max(iPolicy),
                    iStdPriority = (iMinPriority + iMaxPriority) / 2;

    switch (iPriority)
    {
        case (SYS_PRIORITY_NORMAL):
            SchParam.sched_priority = iStdPriority;
            break;

        case (SYS_PRIORITY_LOWER):
            SchParam.sched_priority = iStdPriority - (iStdPriority - iMinPriority) / 3;
            break;

        case (SYS_PRIORITY_HIGHER):
            SchParam.sched_priority = iStdPriority + (iStdPriority - iMinPriority) / 3;
            break;
    }

    if (pthread_setschedparam(pTD->ThreadId, iPolicy, &SchParam) != 0)
    {
        ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
        return (ERR_SET_THREAD_PRIORITY);
    }

    return (0);

}



int             SysWaitThread(SYS_THREAD ThreadID, int iTimeout)
{

    ThrData        *pTD = (ThrData *) ThreadID;

    pthread_mutex_lock(&pTD->Mtx);

    if (iTimeout == SYS_INFINITE_TIMEOUT)
    {

        while (!pTD->iThreadEnded)
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pTD->Mtx);

            pthread_cond_wait(&pTD->ExitWaitCond, &pTD->Mtx);

            pthread_cleanup_pop(0);
        }

    }
    else
    {
        struct timeval  tvNow;
        struct timespec tsTimeout;

        gettimeofday(&tvNow, NULL);

        tsTimeout.tv_sec = tvNow.tv_sec + iTimeout;
        tsTimeout.tv_nsec = tvNow.tv_usec * 1000;

        int             iRetCode = 0;

        while (!pTD->iThreadEnded && (iRetCode != ETIMEDOUT))
        {
            pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &pTD->Mtx);

            iRetCode = pthread_cond_timedwait(&pTD->ExitWaitCond, &pTD->Mtx, &tsTimeout);

            pthread_cleanup_pop(0);
        }

        if (iRetCode == ETIMEDOUT)
        {
            pthread_mutex_unlock(&pTD->Mtx);

            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

    }

    pthread_mutex_unlock(&pTD->Mtx);

    return (0);

}



unsigned long   SysGetCurrentThreadId(void)
{

    return ((unsigned long) pthread_self());

}



static int      SysExitPID(pid_t PID, int iExitCode)
{

    SysSpinAcquire(&WaitPIDSpin);

    SysListHead    *pLLink;

    SYS_LIST_FOR_EACH(pLLink, &WaitPIDList)
    {
        PIDWaitData    *pPWD = SYS_LIST_ENTRY(pLLink, PIDWaitData, LLink);

        if (pPWD->PID == PID)
        {
            pPWD->PID = 0;
            pPWD->iExitCode = iExitCode;
        }
    }

    SysSpinRelease(&WaitPIDSpin);

    return (0);

}



static int      SysWaitPID(pid_t PID, int * piExitCode, int iTimeout)
{

    PIDWaitData     PWD;

    ZeroData(PWD);
    SYS_INIT_LIST_HEAD(&PWD.LLink);
    PWD.WaitThreadId = pthread_self();
    PWD.PID = PID;
    PWD.iExitCode = -1;

///////////////////////////////////////////////////////////////////////////////
//  Insert into waiting list
///////////////////////////////////////////////////////////////////////////////
    SysSpinAcquire(&WaitPIDSpin);

    SYS_LIST_ADDT(&PWD.LLink, &WaitPIDList);

    SysSpinRelease(&WaitPIDSpin);

///////////////////////////////////////////////////////////////////////////////
//  Wait for PID exit
///////////////////////////////////////////////////////////////////////////////
    iTimeout *= 1000;

    while ((iTimeout > 0) && (PWD.PID != 0))
    {
        SysMsSleep(WAIT_PID_TIME_STEP);

        iTimeout -= WAIT_PID_TIME_STEP;
    }

///////////////////////////////////////////////////////////////////////////////
//  Remove from waiting list
///////////////////////////////////////////////////////////////////////////////
    SysSpinAcquire(&WaitPIDSpin);

    SYS_LIST_DEL(&PWD.LLink);

    SysSpinRelease(&WaitPIDSpin);


    if (PWD.PID != 0)
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    if (piExitCode != NULL)
        *piExitCode = PWD.iExitCode;

    return (0);

}



int             SysExec(char const * pszCommand, char const * const * pszArgs, int iWaitTimeout,
                        int iPriority, int *piExitStatus)
{

    pid_t           ProcessID = (pid_t) fork();

    if (ProcessID == 0)
    {

        exit((int) execv(pszCommand, (char **) pszArgs));
    }

    if (ProcessID == (pid_t) (-1))
    {
        ErrSetErrorCode(ERR_FORK);
        return (ERR_FORK);
    }


    switch (iPriority)
    {
        case (SYS_PRIORITY_NORMAL):
            setpriority(PRIO_PROCESS, ProcessID, 0);
            break;

        case (SYS_PRIORITY_LOWER):
            setpriority(PRIO_PROCESS, ProcessID, SCHED_PRIORITY_INC);
            break;

        case (SYS_PRIORITY_HIGHER):
            setpriority(PRIO_PROCESS, ProcessID, -SCHED_PRIORITY_INC);
            break;
    }


    if (iWaitTimeout > 0)
    {
        int             iExitStatus = 0;

        if (waitpid((pid_t) ProcessID, &iExitStatus, WUNTRACED | WNOHANG) != ProcessID)
        {
            if ((errno == ECHILD) || (WEXITSTATUS(iExitStatus) < 0))
            {
                ErrSetErrorCode(ERR_PROCESS_EXECUTE);
                return (ERR_PROCESS_EXECUTE);
            }

            if (SysWaitPID(ProcessID, &iExitStatus, iWaitTimeout) < 0)
                return (ErrGetErrorCode());
        }
        else
            iExitStatus = WEXITSTATUS(iExitStatus);

        if (piExitStatus != NULL)
            *piExitStatus = iExitStatus;
    }
    else if (piExitStatus != NULL)
        *piExitStatus = -1;


    return (0);

}



static void     SysBreakHandlerRoutine(int iSignal)
{

    if (SysBreakHandler != NULL)
        SysBreakHandler();

    signal(iSignal, SysBreakHandlerRoutine);

}



void            SysSetBreakHandler(void (*BreakHandler) (void))
{

    SysBreakHandler = BreakHandler;

///////////////////////////////////////////////////////////////////////////////
//  Setup signal handlers and enable signals
///////////////////////////////////////////////////////////////////////////////
    signal(SIGINT, SysBreakHandlerRoutine);
    signal(SIGQUIT, SysBreakHandlerRoutine);
    signal(SIGHUP, SysBreakHandlerRoutine);


    sigset_t        SigMask;

    sigemptyset(&SigMask);
    sigaddset(&SigMask, SIGINT);
    sigaddset(&SigMask, SIGQUIT);
    sigaddset(&SigMask, SIGHUP);

    pthread_sigmask(SIG_UNBLOCK, &SigMask, NULL);

}



int             SysCreateTlsKey(SYS_TLSKEY & TlsKey, void (*pFreeProc) (void *))
{

    if (pthread_key_create(&TlsKey, pFreeProc) != 0)
    {
        ErrSetErrorCode(ERR_NOMORE_TLSKEYS);
        return (ERR_NOMORE_TLSKEYS);
    }

    return (0);

}



int             SysDeleteTlsKey(SYS_TLSKEY & TlsKey)
{

    pthread_key_delete(TlsKey);

    return (0);

}



int             SysSetTlsKeyData(SYS_TLSKEY & TlsKey, void *pData)
{

    if (pthread_setspecific(TlsKey, pData) != 0)
    {
        ErrSetErrorCode(ERR_INVALID_TLSKEY);
        return (ERR_INVALID_TLSKEY);
    }

    return (0);

}



void           *SysGetTlsKeyData(SYS_TLSKEY & TlsKey)
{

    return (pthread_getspecific(TlsKey));

}



void            SysThreadOnce(SYS_THREAD_ONCE * pThrOnce, void (*pOnceProc) (void))
{

    pthread_once(pThrOnce, pOnceProc);

}



void           *SysAlloc(unsigned int uSize)
{

    void           *pData = malloc(uSize);

    if (pData != NULL)
        memset(pData, 0, uSize);
    else
        ErrSetErrorCode(ERR_MEMORY);

    return (pData);

}



void            SysFree(void *pData)
{

    free(pData);

}



void           *SysRealloc(void *pData, unsigned int uSize)
{

    void           *pNewData = realloc(pData, uSize);

    if (pNewData == NULL)
        ErrSetErrorCode(ERR_MEMORY);

    return (pNewData);

}



int             SysLockFile(const char *pszFileName, char const * pszLockExt)
{

    char            szLockFile[SYS_MAX_PATH] = "";

    sprintf(szLockFile, "%s%s", pszFileName, pszLockExt);

    int             iFileID = open(szLockFile, O_CREAT | O_EXCL | O_RDWR, S_IREAD | S_IWRITE);

    if (iFileID == -1)
    {
        ErrSetErrorCode(ERR_LOCKED);
        return (ERR_LOCKED);
    }

    char            szLock[128] = "";

    sprintf(szLock, "%lu", (unsigned long) SysGetCurrentThreadId());

    write(iFileID, szLock, strlen(szLock) + 1);

    close(iFileID);

    return (0);

}



int             SysUnlockFile(const char *pszFileName, char const * pszLockExt)
{

    char            szLockFile[SYS_MAX_PATH] = "";

    sprintf(szLockFile, "%s%s", pszFileName, pszLockExt);

    if (unlink(szLockFile) != 0)
    {
        ErrSetErrorCode(ERR_NOT_LOCKED);
        return (ERR_NOT_LOCKED);
    }

    return (0);

}



SYS_HANDLE      SysOpenModule(char const * pszFilePath)
{

    void           *pModule = dlopen(pszFilePath, RTLD_LAZY);

    if (pModule == NULL)
    {
        ErrSetErrorCode(ERR_LOADMODULE, pszFilePath);
        return (SYS_INVALID_HANDLE);
    }

    return ((SYS_HANDLE) pModule);

}



int             SysCloseModule(SYS_HANDLE hModule)
{

    dlclose((void *) hModule);

    return (0);

}



void           *SysGetSymbol(SYS_HANDLE hModule, char const * pszSymbol)
{

    void           *pSymbol = dlsym((void *) hModule, pszSymbol);

    if (pSymbol == NULL)
    {
        ErrSetErrorCode(ERR_LOADMODULESYMBOL, pszSymbol);
        return (NULL);
    }

    return (pSymbol);

}



int             SysEventLogV(char const * pszFormat, va_list Args)
{

    openlog(APP_NAME_STR, LOG_PID, LOG_DAEMON);


    char            szBuffer[2048] = "";

    vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);

    syslog(LOG_DAEMON | LOG_ERR, "%s", szBuffer);


    closelog();

    return (0);

}



int             SysEventLog(char const * pszFormat,...)
{

    va_list         Args;

    va_start(Args, pszFormat);


    int             iLogResult = SysEventLogV(pszFormat, Args);


    va_end(Args);

    return (0);

}



int             SysLogMessage(int iLogLevel, char const * pszFormat,...)
{

    extern bool     bServerDebug;

    pthread_mutex_lock(&LogMutex);


    va_list         Args;

    va_start(Args, pszFormat);


    if (bServerDebug)
    {
///////////////////////////////////////////////////////////////////////////////
//  Debug implementation
///////////////////////////////////////////////////////////////////////////////

        vprintf(pszFormat, Args);

    }
    else
    {
        switch (iLogLevel)
        {
            case (LOG_LEV_WARNING):
            case (LOG_LEV_ERROR):

                SysEventLogV(pszFormat, Args);

                break;
        }
    }


    va_end(Args);


    pthread_mutex_unlock(&LogMutex);

    return (0);

}




void            SysSleep(int iTimeout)
{

    SysMsSleep(iTimeout * 1000);

}



void            SysMsSleep(int iMsTimeout)
{

    pthread_mutex_t LK = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  CD = PTHREAD_COND_INITIALIZER;
    struct timespec TV;
    struct timeval  TmNow;
    int             iErrorCode;

    gettimeofday(&TmNow, NULL);

    TmNow.tv_sec += iMsTimeout / 1000;
    TmNow.tv_usec += (iMsTimeout % 1000) * 1000;
    TmNow.tv_sec += TmNow.tv_usec / 1000000;
    TmNow.tv_usec %= 1000000;

    TV.tv_sec = TmNow.tv_sec;
    TV.tv_nsec = TmNow.tv_usec * 1000;

    pthread_mutex_lock(&LK);
    pthread_cleanup_push((void (*) (void *)) pthread_mutex_unlock, &LK);

    iErrorCode = pthread_cond_timedwait(&CD, &LK, &TV);

    pthread_cleanup_pop(1);

    if (iErrorCode == ETIMEDOUT)
    {

    }

    pthread_mutex_destroy(&LK);

    pthread_cond_destroy(&CD);

}



SYS_INT64       SysMsTime(void)
{

    struct timeval  tv;

    if (gettimeofday(&tv, NULL) != 0)
        return (0);

    return (1000 * (SYS_INT64) tv.tv_sec + (SYS_INT64) tv.tv_usec / 1000);

}



int             SysExistFile(const char *pszFilePath)
{

    return ((access(pszFilePath, F_OK) == 0) ? 1 : 0);

}



SYS_HANDLE      SysFirstFile(const char *pszPath, char *pszFileName)
{

    DIR            *pDIR = opendir(pszPath);

    if (pDIR == NULL)
    {
        ErrSetErrorCode(ERR_OPENDIR);
        return (SYS_INVALID_HANDLE);
    }

    struct dirent   DE;
    struct dirent  *pDirEntry = NULL;

    readdir_r(pDIR, &DE, &pDirEntry);

    if (pDirEntry == NULL)
    {
        closedir(pDIR);
        return (SYS_INVALID_HANDLE);
    }

    FileFindData   *pFFD = (FileFindData *) SysAlloc(sizeof(FileFindData));

    if (pFFD == NULL)
    {
        closedir(pDIR);
        return (SYS_INVALID_HANDLE);
    }

    strcpy(pFFD->szPath, pszPath);
    AppendSlash(pFFD->szPath);
    pFFD->pDIR = pDIR;
    pFFD->DE = *pDirEntry;

    strcpy(pszFileName, pFFD->DE.d_name);

    char            szFilePath[SYS_MAX_PATH] = "";

    sprintf(szFilePath, "%s%s", pFFD->szPath, pFFD->DE.d_name);

    if (stat(szFilePath, &pFFD->FS) != 0)
    {
        SysFree(pFFD);
        closedir(pDIR);

        ErrSetErrorCode(ERR_STAT);
        return (SYS_INVALID_HANDLE);
    }

    return ((SYS_HANDLE) pFFD);

}



int             SysIsDirectory(SYS_HANDLE hFind)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    return ((S_ISDIR(pFFD->FS.st_mode)) ? 1 : 0);

}



unsigned long   SysGetSize(SYS_HANDLE hFind)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    return ((unsigned long) pFFD->FS.st_size);

}



int             SysNextFile(SYS_HANDLE hFind, char *pszFileName)
{

    FileFindData   *pFFD = (FileFindData *) hFind;
    struct dirent  *pDirEntry = NULL;

    readdir_r(pFFD->pDIR, &pFFD->DE, &pDirEntry);

    if (pDirEntry == NULL)
        return (0);

    strcpy(pszFileName, pFFD->DE.d_name);

    char            szFilePath[SYS_MAX_PATH] = "";

    sprintf(szFilePath, "%s%s", pFFD->szPath, pFFD->DE.d_name);

    if (stat(szFilePath, &pFFD->FS) != 0)
    {
        ErrSetErrorCode(ERR_STAT);
        return (0);
    }

    return (1);

}



void            SysFindClose(SYS_HANDLE hFind)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    closedir(pFFD->pDIR);

    SysFree(pFFD);

}



int             SysGetFileInfo(char const * pszFileName, SYS_FILE_INFO & FI)
{

    struct stat     stat_buffer;

    if (stat(pszFileName, &stat_buffer) != 0)
    {
        ErrSetErrorCode(ERR_STAT);
        return (ERR_STAT);
    }

    ZeroData(FI);
    FI.iFileType = (S_ISREG(stat_buffer.st_mode)) ? ftNormal :
            ((S_ISDIR(stat_buffer.st_mode)) ? ftDirectory :
            ((S_ISLNK(stat_buffer.st_mode)) ? ftLink : ftOther));
    FI.ulSize = (unsigned long) stat_buffer.st_size;
    FI.tCreat = stat_buffer.st_ctime;
    FI.tMod = stat_buffer.st_mtime;

    return (0);

}



char           *SysStrDup(const char *pszString)
{

    int             iStrLength = strlen(pszString);
    char           *pszBuffer = (char *) SysAlloc(iStrLength + 1);

    if (pszBuffer != NULL)
        strcpy(pszBuffer, pszString);

    return (pszBuffer);

}



char           *SysGetEnv(const char *pszVarName)
{

    const char     *pszValue = getenv(pszVarName);

    return ((pszValue != NULL) ? SysStrDup(pszValue) : NULL);

}



char           *SysGetTmpFile(char *pszFileName)
{

    static unsigned int uFileSeqNr = 0;
    SYS_LONGLONG    llFileID = ((((SYS_LONGLONG) SysGetCurrentThreadId()) << 32) |
            (SYS_LONGLONG)++ uFileSeqNr);

    sprintf(pszFileName, "/tmp/msrv%llx.tmp", llFileID);

    return (pszFileName);

}



int             SysRemove(const char *pszFileName)
{

    if (unlink(pszFileName) != 0)
    {
        ErrSetErrorCode(ERR_FILE_DELETE);
        return (ERR_FILE_DELETE);
    }

    return (0);

}



int             SysMakeDir(const char *pszPath)
{

    if (mkdir(pszPath, 0700) != 0)
    {
        ErrSetErrorCode(ERR_DIR_CREATE);
        return (ERR_DIR_CREATE);
    }

    return (0);

}



int             SysRemoveDir(const char *pszPath)
{

    if (rmdir(pszPath) != 0)
    {
        ErrSetErrorCode(ERR_DIR_DELETE);
        return (ERR_DIR_DELETE);
    }

    return (0);

}



int             SysMoveFile(char const * pszOldName, char const * pszNewName)
{

    if (rename(pszOldName, pszNewName) != 0)
    {
        ErrSetErrorCode(ERR_FILE_MOVE);
        return (ERR_FILE_MOVE);
    }

    return (0);

}



int             SysVSNPrintf(char *pszBuffer, int iSize, char const * pszFormat, va_list Args)
{

    int             iPrintResult = vsnprintf(pszBuffer, iSize, pszFormat, Args);

    return ((iPrintResult < iSize) ? iPrintResult : -1);

}



char           *SysStrTok(char *pszData, char const * pszDelim, char **ppszSavePtr)
{

    return (strtok_r(pszData, pszDelim, ppszSavePtr));

}



char           *SysCTime(time_t * pTimer, char *pszBuffer, int iBufferSize)
{

    return (ctime_r(pTimer, pszBuffer));

}



struct tm      *SysLocalTime(time_t * pTimer, struct tm * pTStruct)
{

    return (localtime_r(pTimer, pTStruct));

}



struct tm      *SysGMTime(time_t * pTimer, struct tm * pTStruct)
{

    return (gmtime_r(pTimer, pTStruct));

}



char           *SysAscTime(struct tm * pTStruct, char *pszBuffer, int iBufferSize)
{

    return (asctime_r(pTStruct, pszBuffer));

}



static SYS_SPINLOCK SysTestAndSet(SYS_SPINLOCK * pSpinLock)
{

    unsigned int    uValue;

    __asm__ __volatile__(
            "xchgl %0, %1"
          : "=r"(uValue), "=m"(*pSpinLock)
          : "0"(1), "m"(*pSpinLock)
          : "memory");

    return (uValue);

}



int             SysSpinAcquire(SYS_SPINLOCK * pSpinLock)
{

    int             iCount = 0;

    while (SysTestAndSet(pSpinLock))
    {
        if (iCount < MAX_SPIN_COUNT)
        {
            ++iCount;

            sched_yield();
        }
        else
        {
            usleep(SPIN_SLEEP_TIME);

            iCount = 0;
        }
    }

    return (0);

}



int             SysSpinRelease(SYS_SPINLOCK * pSpinLock)
{

    *pSpinLock = 0;

    return (0);

}
