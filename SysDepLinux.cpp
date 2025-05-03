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

#define MAX_SEM_X_ID                40
#define MAX_SEM_ID_CREATED          5
#define MAKE_SYS_SEMAPHORE(s, i)    ((SYS_SEMAPHORE) ((s) * MAX_SEM_X_ID + (i) + 1))
#define SYS_SEMAPHORE_SLOT(sem)     (((int) (sem) - 1) / MAX_SEM_X_ID)
#define SYS_SEMAPHORE_NBR(sem)      (((int) (sem) - 1) % MAX_SEM_X_ID)

#define SCHED_PRIORITY_INC          6

#define MIN_TCP_SEND_SIZE           1024
#define MAX_TCP_SEND_SIZE           (1024 * 8)
#define MIN_BYTES_SEC_TIMEOUT       64
#define STD_SENDFILE_BLKSIZE        (4092 * 2)

///////////////////////////////////////////////////////////////////////////////
//  Comment this if You want to use select() instead of alarm()
///////////////////////////////////////////////////////////////////////////////
#define USE_ALARM_TIMEOUT

///////////////////////////////////////////////////////////////////////////////
//  Uncomment this if You want to use sendfile()
///////////////////////////////////////////////////////////////////////////////
#define USE_SENDFILE







struct SemMapData
{
    SYS_IPCNAME     SemName;
    int             iCount;
};

struct SemUsageData
{
    SYS_IPCNAME     SemName;
    int             iSemID;
    int             iUsageCount;
    SemMapData      SMD[MAX_SEM_X_ID];
};

struct SemArenaData
{
    int             iInsertSlot;
    int             iSemCount;
    SemUsageData    SUD[MAX_SEM_ID_CREATED];
};


///////////////////////////////////////////////////////////////////////////////
//  Definition of union semun
///////////////////////////////////////////////////////////////////////////////
#ifdef _SEM_SEMUN_UNDEFINED

union semun
{
    int             val;
    struct semid_ds *buf;
    unsigned short int *array;
//    struct seminfo *__buf;
};

#endif



struct FileFindData
{
    char            szPath[SYS_MAX_PATH];
    DIR            *pDIR;
    struct dirent   DE;
    struct stat     FS;
};







static char const *SysGetLastError(void);
static void     SysAlarmProc(int iSignal);
static void     SysIgnoreProc(int iSignal);
static void     SysSetAlarm(int iTimeout);
static int      SysSetupSemaphoresHandling(void);
static int      SysClenupSemaphoresHandling(void);
static int      SysConnectSemaphoresHandling(void);
static int      SysDisconnectSemaphoresHandling(void);
static int      SysSemGet(SYS_IPCNAME SemName, SYS_SEMAPHORE & SemID, bool & bCreated);
static int      SysSemRelease(SYS_SEMAPHORE SemID);
static int      SysForcedSemGet(SYS_IPCNAME SemName, int iSemCount, int iFlags);
static int      SysForcedShmGet(SYS_IPCNAME ShmName, unsigned int uSize, int iFlags);
static int      SysSemLock(int iSemID, int iCount, int iSemNumber);
static int      SysSemUnlock(int iSemID, int iCount, int iSemNumber);
static int      SysSetSemaphore(int iSemID, int iSemCount, int iSemNumber);
static int      SysSemKill(int iSemID);
static int      SysShmKill(int iShmID);
static int      SysSetSockNoDelay(SYS_SOCKET SockFD, int iNoDelay);
static int      SysSetSocketsOptions(SYS_SOCKET SockFD);
static int      SysSendFileTimeout(int iSockFD, int iFileID, off_t ulStartOffset,
                        unsigned long ulSize, int iTimeout);
static int      SysThreadSetup(void);
static int      SysThreadCleanup(void);
static void     SysBreakHandlerRoutine(int iSignal);










static SYS_IPCNAME SASemIPCName,
                SAShmIPCName,
                LogSemIPCName;
static THRDLS int iSASemID,
                iSAShmID;
static THRDLS SemArenaData *pSAD;
static SYS_SEMAPHORE RootLogSemID;
static THRDLS SYS_SEMAPHORE LogSemID;
static void     (*SysBreakHandler) (void) = NULL;









static char const *SysGetLastError(void)
{

    static char     szMessage[1024] = "";

    snprintf(szMessage, sizeof(szMessage) - 1, "(0x%lX) %s", (unsigned long) errno, strerror(errno));

    return (szMessage);

}




static void     SysAlarmProc(int iSignal)
{


}




static void     SysIgnoreProc(int iSignal)
{

    signal(iSignal, SysIgnoreProc);

}




static void     SysSetAlarm(int iTimeout)
{

    if (iTimeout > 0)
        signal(SIGALRM, SysAlarmProc);
    else
        signal(SIGALRM, SIG_IGN);

    alarm(iTimeout);

}




static int      SysSetupSemaphoresHandling(void)
{
///////////////////////////////////////////////////////////////////////////////
//  Create IPC names
///////////////////////////////////////////////////////////////////////////////
    SASemIPCName = SysCreateIPCName();
    SAShmIPCName = SysCreateIPCName();

///////////////////////////////////////////////////////////////////////////////
//  Create common access semaphore
///////////////////////////////////////////////////////////////////////////////
    if ((iSASemID = SysForcedSemGet(SASemIPCName, 1, 0600)) == -1)
    {
        ErrSetErrorCode(ERR_SEMGET);
        return (ERR_SEMGET);
    }

    if (SysSetSemaphore(iSASemID, 1, 0) < 0)
    {
        ErrorPush();
        SysSemKill(iSASemID);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Create semaphore arena data
///////////////////////////////////////////////////////////////////////////////
    if ((iSAShmID = SysForcedShmGet(SAShmIPCName, sizeof(SemArenaData), 0600)) == -1)
    {
        ErrorPush();
        SysSemKill(iSASemID);
        return (ErrorPop());
    }

///////////////////////////////////////////////////////////////////////////////
//  Initializes arena data
///////////////////////////////////////////////////////////////////////////////
    if ((pSAD = (SemArenaData *) SysMapSharedMem((SYS_SHMEM) iSAShmID)) == NULL)
    {
        ErrorPush();
        SysShmKill(iSAShmID);
        SysSemKill(iSASemID);
        return (ErrorPop());
    }

    memset(pSAD, 0, sizeof(SemArenaData));

    pSAD->iInsertSlot = IPrev(0, MAX_SEM_ID_CREATED);
    pSAD->iSemCount = 0;


///////////////////////////////////////////////////////////////////////////////
//  Create semaphore pool
///////////////////////////////////////////////////////////////////////////////
    SemUsageData   *pSUD = pSAD->SUD;

    for (int ii = 0; ii < MAX_SEM_ID_CREATED; ii++)
    {
        pSUD[ii].iUsageCount = 0;
        pSUD[ii].SemName = SysCreateIPCName();

///////////////////////////////////////////////////////////////////////////////
//  Get or create semaphore
///////////////////////////////////////////////////////////////////////////////
        if ((pSUD[ii].iSemID = SysForcedSemGet(pSUD[ii].SemName, MAX_SEM_X_ID, 0600)) == -1)
        {
            for (--ii; ii >= 0; ii--)
                SysSemKill(pSUD[ii].iSemID);

            SysUnmapSharedMem((SYS_SHMEM) iSAShmID, pSAD);

            SysSemKill(iSASemID);
            SysShmKill(iSAShmID);

            ErrSetErrorCode(ERR_SEMGET);
            return (ERR_SEMGET);
        }

        for (int jj = 0; jj < MAX_SEM_X_ID; jj++)
        {
            pSUD[ii].SMD[jj].SemName = SYS_INVALID_IPCNAME;
            pSUD[ii].SMD[jj].iCount = 0;
        }

    }


    return (0);

}




static int      SysClenupSemaphoresHandling(void)
{

    SysSemLock(iSASemID, 1, 0);

    SysUnmapSharedMem((SYS_SHMEM) iSAShmID, pSAD);

///////////////////////////////////////////////////////////////////////////////
//  Kill allocated semaphores
///////////////////////////////////////////////////////////////////////////////
    SemArenaData   *pSAD = (SemArenaData *) SysMapSharedMem((SYS_SHMEM) iSAShmID);

    if (pSAD != NULL)
    {
        SemUsageData   *pSUD = pSAD->SUD;

        for (int ii = 0; ii < MAX_SEM_ID_CREATED; ii++)
            SysSemKill(pSUD[ii].iSemID);


        SysUnmapSharedMem((SYS_SHMEM) iSAShmID, pSAD);
    }

    SysShmKill(iSAShmID);

///////////////////////////////////////////////////////////////////////////////
//  Kill common access semaphore
///////////////////////////////////////////////////////////////////////////////
    SysSemUnlock(iSASemID, 1, 0);
    SysSemKill(iSASemID);

    return (0);

}




static int      SysConnectSemaphoresHandling(void)
{
///////////////////////////////////////////////////////////////////////////////
//  Get common access semaphore id
///////////////////////////////////////////////////////////////////////////////
    if ((iSASemID = semget(SASemIPCName, 1, 0)) == -1)
    {
        ErrSetErrorCode(ERR_SEMGET);
        return (ERR_SEMGET);
    }

///////////////////////////////////////////////////////////////////////////////
//  Get semaphore arena data id
///////////////////////////////////////////////////////////////////////////////
    if ((iSAShmID = shmget(SAShmIPCName, sizeof(SemArenaData), 0)) == -1)
    {
        ErrSetErrorCode(ERR_SHMGET);
        return (ERR_SHMGET);
    }

///////////////////////////////////////////////////////////////////////////////
//  Map it to user space
///////////////////////////////////////////////////////////////////////////////
    if ((pSAD = (SemArenaData *) SysMapSharedMem((SYS_SHMEM) iSAShmID)) == NULL)
        return (ErrGetErrorCode());

    return (0);

}




static int      SysDisconnectSemaphoresHandling(void)
{
///////////////////////////////////////////////////////////////////////////////
//  Unmap semaphore arena data
///////////////////////////////////////////////////////////////////////////////
    SysUnmapSharedMem((SYS_SHMEM) iSAShmID, pSAD);

    return (0);

}




static int      SysSemGet(SYS_IPCNAME SemName, SYS_SEMAPHORE & SemID, bool & bCreated)
{

    if (SysSemLock(iSASemID, 1, 0) < 0)
        return (ErrGetErrorCode());


    int             ii,
                    jj;
    SemUsageData   *pSUD = pSAD->SUD;

    for (ii = 0; ii < MAX_SEM_ID_CREATED; ii++)
    {
        if (pSUD[ii].iUsageCount > 0)
        {
            for (jj = 0; jj < MAX_SEM_X_ID; jj++)
            {
                if (pSUD[ii].SMD[jj].SemName == SemName)
                {
///////////////////////////////////////////////////////////////////////////////
//  Semaphore already defined, incrment usage count
///////////////////////////////////////////////////////////////////////////////
                    ++pSUD[ii].SMD[jj].iCount;
                    SemID = MAKE_SYS_SEMAPHORE(ii, jj);
                    bCreated = false;

                    SysSemUnlock(iSASemID, 1, 0);

                    return (0);
                }
            }
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Semaphore not found, search a slot for a new creation
///////////////////////////////////////////////////////////////////////////////
    for (ii = INext(pSAD->iInsertSlot, MAX_SEM_ID_CREATED); ii != pSAD->iInsertSlot;
            ii = INext(ii, MAX_SEM_ID_CREATED))
    {
        if (pSUD[ii].iUsageCount < MAX_SEM_X_ID)
        {
            for (jj = 0; jj < MAX_SEM_X_ID; jj++)
            {
                if (pSUD[ii].SMD[jj].SemName == SYS_INVALID_IPCNAME)
                {
///////////////////////////////////////////////////////////////////////////////
//  Define a fresh new semaphore
///////////////////////////////////////////////////////////////////////////////
                    pSUD[ii].SMD[jj].iCount = 1;
                    pSUD[ii].SMD[jj].SemName = SemName;
                    ++pSUD[ii].iUsageCount;

                    SemID = MAKE_SYS_SEMAPHORE(ii, jj);
                    bCreated = true;

                    ++pSAD->iSemCount;
                    pSAD->iInsertSlot = ii;

                    SysSemUnlock(iSASemID, 1, 0);

                    return (0);
                }
            }
        }
    }

    SysSemUnlock(iSASemID, 1, 0);


    ErrSetErrorCode(ERR_NOMORESEMS);
    return (ERR_NOMORESEMS);

}




static int      SysSemRelease(SYS_SEMAPHORE SemID)
{

    if (SysSemLock(iSASemID, 1, 0) < 0)
        return (ErrGetErrorCode());


    int             iSemSlot = SYS_SEMAPHORE_SLOT(SemID),
                    iSemNumber = SYS_SEMAPHORE_NBR(SemID);

    if ((iSemSlot < 0) || (iSemSlot >= MAX_SEM_ID_CREATED) ||
            (iSemNumber < 0) || (iSemNumber >= MAX_SEM_X_ID) ||
            (pSAD->SUD[iSemSlot].SMD[iSemNumber].SemName == SYS_INVALID_IPCNAME))
    {
        SysSemUnlock(iSASemID, 1, 0);

        ErrSetErrorCode(ERR_INVALID_SEMAPHORE);
        return (ERR_INVALID_SEMAPHORE);
    }


    SemUsageData   *pSUD = &pSAD->SUD[iSemSlot];

    if (--pSUD->SMD[iSemNumber].iCount == 0)
    {
///////////////////////////////////////////////////////////////////////////////
//  Usage count reached zero, clear semaphore definition
///////////////////////////////////////////////////////////////////////////////
        pSUD->SMD[iSemNumber].SemName = SYS_INVALID_IPCNAME;
        --pSUD->iUsageCount;

        --pSAD->iSemCount;
        pSAD->iInsertSlot = IPrev(iSemSlot, MAX_SEM_ID_CREATED);
    }


    SysSemUnlock(iSASemID, 1, 0);

    return (0);

}



static int      SysForcedSemGet(SYS_IPCNAME SemName, int iSemCount, int iFlags)
{

    int             iSemID = semget(SemName, iSemCount, 0);

    if (iSemID != -1)
        SysSemKill(iSemID);

    if ((iSemID = semget(SemName, iSemCount, IPC_CREAT | IPC_EXCL | iFlags)) == -1)
    {
        ErrSetErrorCode(ERR_SEMGET);
        return (ERR_SEMGET);
    }

    return (iSemID);

}



static int      SysForcedShmGet(SYS_IPCNAME ShmName, unsigned int uSize, int iFlags)
{

    int             iShmID = shmget(ShmName, uSize, 0);

    if (iShmID != -1)
        SysShmKill(iShmID);

    if ((iShmID = shmget(ShmName, uSize, IPC_CREAT | IPC_EXCL | iFlags)) == -1)
    {
        ErrSetErrorCode(ERR_SHMGET);
        return (-1);
    }

    return (iShmID);

}



static int      SysSemLock(int iSemID, int iCount, int iSemNumber)
{

    struct sembuf   SemBuf;

    ZeroData(SemBuf);
    SemBuf.sem_num = iSemNumber;
    SemBuf.sem_op = -iCount;
    SemBuf.sem_flg = 0;

    if (semop(iSemID, &SemBuf, 1) == -1)
    {
        ErrSetErrorCode(ERR_SEMOP);
        return (ERR_SEMOP);
    }

    return (0);

}



static int      SysSemUnlock(int iSemID, int iCount, int iSemNumber)
{

    struct sembuf   SemBuf;

    ZeroData(SemBuf);
    SemBuf.sem_num = iSemNumber;
    SemBuf.sem_op = iCount;
    SemBuf.sem_flg = 0;

    if (semop(iSemID, &SemBuf, 1) == -1)
    {
        ErrSetErrorCode(ERR_SEMOP);
        return (ERR_SEMOP);
    }

    return (0);

}



static int      SysSetSemaphore(int iSemID, int iSemCount, int iSemNumber)
{

    union semun     SemU;

    ZeroData(SemU);
    SemU.val = iSemCount;

    if (semctl(iSemID, iSemNumber, SETVAL, SemU) == -1)
    {
        ErrSetErrorCode(ERR_SEMCTL);
        return (ERR_SEMCTL);
    }

    return (0);

}



static int      SysSemKill(int iSemID)
{

    union semun     SemU;

    ZeroData(SemU);

    if (semctl(iSemID, 0, IPC_RMID, SemU) == -1)
    {
        ErrSetErrorCode(ERR_SEMCTL);
        return (ERR_SEMCTL);
    }

    return (0);

}



static int      SysShmKill(int iShmID)
{

    if (shmctl(iShmID, IPC_RMID, (struct shmid_ds *) NULL) == -1)
    {
        ErrSetErrorCode(ERR_SHMCTL);
        return (ERR_SHMCTL);
    }

    return (0);

}



int             SysInitLibrary(void)
{

    if (SysSetupSemaphoresHandling() < 0)
        return (ErrGetErrorCode());


///////////////////////////////////////////////////////////////////////////////
//  Create log semaphore
///////////////////////////////////////////////////////////////////////////////
    LogSemIPCName = SysCreateIPCName();
    if ((RootLogSemID = SysCreateSemaphore(1, SYS_DEFAULT_MAXCOUNT,
                            LogSemIPCName)) == SYS_INVALID_SEMAPHORE)
    {
        ErrorPush();
        SysClenupSemaphoresHandling();
        return (ErrorPop());
    }


    if (SysThreadSetup() < 0)
    {
        ErrorPush();
        SysClenupSemaphoresHandling();
        return (ErrorPop());
    }


    return (0);

}



void            SysCleanupLibrary(void)
{

    SysCloseSemaphore(RootLogSemID);
    SysKillSemaphore(RootLogSemID);

    SysThreadCleanup();

    SysClenupSemaphoresHandling();

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
        SysCloseSocket((SYS_SOCKET) SockFD, 1);

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
//  Set KEEPALIVE if supported
///////////////////////////////////////////////////////////////////////////////
    setsockopt(SockFD, SOL_SOCKET, SO_KEEPALIVE, &iActivate, sizeof(iActivate));


    return (0);

}



void            SysCloseSocket(SYS_SOCKET SockFD, int iHardClose)
{

    if (!iHardClose)
    {
        shutdown(SockFD, 1);

        int             iRecvResult;

        do
        {
            char            szBuffer[256] = "";

            iRecvResult = SysRecvData(SockFD, szBuffer, sizeof(szBuffer),
                    SHUTDOWN_RECV_TIMEOUT);

        } while (iRecvResult > 0);

        shutdown(SockFD, 2);
    }

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

#ifdef USE_ALARM_TIMEOUT

    SysSetAlarm(iTimeout);

    int             iRecvBytes = recv((int) SockFD, pszBuffer, iBufferSize, 0);

    SysSetAlarm(0);

    if (iRecvBytes == -1)
    {
        if (errno == EINTR)
        {
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iRecvBytes);

#else           // #ifdef USE_ALARM_TIMEOUT

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

#endif          // #ifdef USE_ALARM_TIMEOUT

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

#ifdef USE_ALARM_TIMEOUT

    SysSetAlarm(iTimeout);

    socklen_t       SockALen = (socklen_t) iFromlen;
    int             iRecvBytes = recvfrom((int) SockFD, pszBuffer, iBufferSize, 0, pFrom, &SockALen);

    SysSetAlarm(0);

    if (iRecvBytes == -1)
    {
        if (errno == EINTR)
        {
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iRecvBytes);

#else           // #ifdef USE_ALARM_TIMEOUT

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

#endif          // #ifdef USE_ALARM_TIMEOUT

}



int             SysSendData(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize, int iTimeout)
{

#ifdef USE_ALARM_TIMEOUT

    SysSetAlarm(iTimeout);

    int             iSendBytes = send((int) SockFD, pszBuffer, iBufferSize, 0);

    SysSetAlarm(0);

    if (iSendBytes == -1)
    {
        if (errno == EINTR)
        {
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iSendBytes);

#else           // #ifdef USE_ALARM_TIMEOUT

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

#endif          // #ifdef USE_ALARM_TIMEOUT

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

#ifdef USE_ALARM_TIMEOUT

    SysSetAlarm(iTimeout);

    int             iSendBytes = sendto((int) SockFD, pszBuffer, iBufferSize, 0, pTo, iToLen);

    SysSetAlarm(0);

    if (iSendBytes == -1)
    {
        if (errno == EINTR)
        {
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iSendBytes);

#else           // #ifdef USE_ALARM_TIMEOUT

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

#endif          // #ifdef USE_ALARM_TIMEOUT

}



int             SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR * pSockName, int iNameLen, int iTimeout)
{

#ifdef USE_ALARM_TIMEOUT

    SysSetAlarm(iTimeout);

    int             iConnectResult = connect((int) SockFD,
            (const struct sockaddr *) & pSockName->Addr, iNameLen);

    SysSetAlarm(0);

    if (iConnectResult == -1)
    {
        if (errno == EINTR)
        {
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (iConnectResult);

#else           // #ifdef USE_ALARM_TIMEOUT

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
	
	fd_set wfds;
	struct timeval tv;

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

#endif          // #ifdef USE_ALARM_TIMEOUT

}



SYS_SOCKET      SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR * pSockName, int *iNameLen, int iTimeout)
{

#ifdef USE_ALARM_TIMEOUT

    SysSetAlarm(iTimeout);

    socklen_t       SockALen = (socklen_t) * iNameLen;
    int             iAcptSock = accept((int) SockFD,
            (struct sockaddr *) & pSockName->Addr, &SockALen);

    SysSetAlarm(0);

    if (iAcptSock == -1)
    {
        if (errno == EINTR)
        {
            ErrSetErrorCode(ERR_TIMEOUT);
            return (SYS_INVALID_SOCKET);
        }

        ErrSetErrorCode(ERR_NETWORK);
        return (SYS_INVALID_SOCKET);
    }

#else           // #ifdef USE_ALARM_TIMEOUT

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

#endif          // #ifdef USE_ALARM_TIMEOUT


    if (SysSetSocketsOptions((SYS_SOCKET) iAcptSock) < 0)
    {
        SysCloseSocket((SYS_SOCKET) iAcptSock, 1);

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



static int      SysSendFileTimeout(int iSockFD, int iFileID, off_t ulStartOffset,
                        unsigned long ulSize, int iTimeout)
{

    void            (*pOldHandler) (int) = signal(SIGALRM, SysAlarmProc);

    alarm(iTimeout);


    unsigned long   ulSendSize = (unsigned long) sendfile(iSockFD, iFileID,
            &ulStartOffset, ulSize);


    alarm(0);

    signal(SIGALRM, pOldHandler);

    return ((ulSendSize == ulSize) ? 0 : -1);

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

    siginterrupt(SIGALRM, 1);

    unsigned long   ulSent = 0;

    while (ulSent < ulFileSize)
    {
        unsigned long   ulToSend = min(STD_SENDFILE_BLKSIZE, ulFileSize - ulSent);


        int             iSendResult = SysSendFileTimeout((int) SockFD, iFileID, (off_t) ulSent,
                ulToSend, max(iTimeout, ulToSend / MIN_BYTES_SEC_TIMEOUT));


        if (iSendResult < 0)
        {
            close(iFileID);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
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

    void           *pMapAddress = (void *) mmap((void *) 0, (size_t) ulFileSize, PROT_READ,
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
            munmap(pMapAddress, (size_t) ulFileSize);
            close(iFileID);
            return (ErrorPop());
        }

        if ((pSendCB != NULL) && (pSendCB(pUserData) < 0))
        {
            munmap(pMapAddress, (size_t) ulFileSize);
            close(iFileID);
            ErrSetErrorCode(ERR_USER_BREAK);
            return (ERR_USER_BREAK);
        }

        pszBuffer += iCurrSend;
        ulSentBytes += (unsigned long) iCurrSend;
    }

    munmap(pMapAddress, (size_t) ulFileSize);

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

    struct hostent *pHostEnt = gethostbyname(pszName);

    return ((pHostEnt != NULL) ? (NET_ADDRESS) * ((SYS_UINT32 *) pHostEnt->h_addr_list[0]) :
            SYS_INVALID_NET_ADDRESS);

}



int             SysGetHostByAddr(SYS_INET_ADDR const & AddrInfo, char *pszFQDN)
{

    struct hostent *pHostEnt = gethostbyaddr((const char *) &SAIN_Addr(AddrInfo.Addr),
            sizeof(SAIN_Addr(AddrInfo.Addr)), AF_INET);

    if (pHostEnt == NULL)
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




SYS_IPCNAME     SysCreateIPCName(void)
{

    extern unsigned int uKeyMagic;
    static unsigned int uIPCSeqNr = 0;

    return ((((SYS_IPCNAME) uKeyMagic) << 16) | ((SYS_IPCNAME)++ uIPCSeqNr));

}




SYS_SEMAPHORE   SysCreateSemaphore(int iInitCount, int iMaxCount, SYS_IPCNAME SemName)
{

    bool            bCreated;
    SYS_SEMAPHORE   SemID;

    if (SysSemGet(SemName, SemID, bCreated) < 0)
        return (SYS_INVALID_SEMAPHORE);


    if (!bCreated)
    {
        SysSemRelease(SemID);

        ErrSetErrorCode(ERR_SEM_ALREADY_EXIST);
        return (SYS_INVALID_SEMAPHORE);
    }


///////////////////////////////////////////////////////////////////////////////
//  Set initial count
///////////////////////////////////////////////////////////////////////////////
    int             iSemSlot = SYS_SEMAPHORE_SLOT(SemID),
                    iSemNumber = SYS_SEMAPHORE_NBR(SemID);

    SysSetSemaphore(pSAD->SUD[iSemSlot].iSemID, iInitCount, iSemNumber);


    return (SemID);

}



SYS_SEMAPHORE   SysConnectSemaphore(int iInitCount, int iMaxCount, SYS_IPCNAME SemName)
{

    bool            bCreated;
    SYS_SEMAPHORE   SemID;

    if (SysSemGet(SemName, SemID, bCreated) < 0)
        return (SYS_INVALID_SEMAPHORE);


    if (bCreated)
    {
        SysSemRelease(SemID);

        ErrSetErrorCode(ERR_SEM_NOT_EXIST);
        return (SYS_INVALID_SEMAPHORE);
    }


    return (SemID);

}



int             SysCloseSemaphore(SYS_SEMAPHORE SemID)
{

    return (0);

}



int             SysKillSemaphore(SYS_SEMAPHORE SemID)
{

    return (SysSemRelease(SemID));

}



int             SysWaitSemaphore(SYS_SEMAPHORE SemID, int iTimeout)
{

    int             iSemSlot = SYS_SEMAPHORE_SLOT(SemID),
                    iSemNumber = SYS_SEMAPHORE_NBR(SemID);

    if ((iSemSlot < 0) || (iSemSlot >= MAX_SEM_ID_CREATED) ||
            (iSemNumber < 0) || (iSemNumber >= MAX_SEM_X_ID))
    {
        ErrSetErrorCode(ERR_INVALID_SEMAPHORE);
        return (ERR_INVALID_SEMAPHORE);
    }


    SysSetAlarm(iTimeout);

    if (SysSemLock(pSAD->SUD[iSemSlot].iSemID, 1, iSemNumber) < 0)
    {
        SysSetAlarm(0);

        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    SysSetAlarm(0);

    return (0);

}



int             SysReleaseSemaphore(SYS_SEMAPHORE SemID, int iCount)
{

    int             iSemSlot = SYS_SEMAPHORE_SLOT(SemID),
                    iSemNumber = SYS_SEMAPHORE_NBR(SemID);

    if ((iSemSlot < 0) || (iSemSlot >= MAX_SEM_ID_CREATED) ||
            (iSemNumber < 0) || (iSemNumber >= MAX_SEM_X_ID))
    {
        ErrSetErrorCode(ERR_INVALID_SEMAPHORE);
        return (ERR_INVALID_SEMAPHORE);
    }

    return (SysSemUnlock(pSAD->SUD[iSemSlot].iSemID, iCount, iSemNumber));

}



SYS_THREAD      SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData)
{

    SYS_THREAD      ThreadID = (SYS_THREAD) fork();

    if (ThreadID == 0)
    {
        if (SysThreadSetup() < 0)
            return (SYS_INVALID_THREAD);


        unsigned int    uResultCode = pThreadProc(pThreadData);


        SysThreadCleanup();

        exit((int) uResultCode);
    }

    if (ThreadID == -1)
    {
        ErrSetErrorCode(ERR_FORK);
        return (SYS_INVALID_THREAD);
    }

    return (ThreadID);

}



SYS_THREAD      SysCreateServiceThread(unsigned int (*pThreadProc) (void *), SYS_SOCKET SockFD)
{

    SYS_THREAD      ThreadID = (SYS_THREAD) fork();

    if (ThreadID == 0)
    {
        if (SysThreadSetup() < 0)
            return (SYS_INVALID_THREAD);


        unsigned int    uResultCode = pThreadProc((void *) (unsigned int) SockFD);


        SysThreadCleanup();

        exit((int) uResultCode);
    }

    if (ThreadID == -1)
    {
        ErrSetErrorCode(ERR_FORK);
        return (SYS_INVALID_THREAD);
    }

    SysCloseSocket(SockFD, 1);

    return (ThreadID);

}



static int      SysThreadSetup(void)
{

    if (SysConnectSemaphoresHandling() < 0)
        return (ErrGetErrorCode());


    if ((LogSemID = SysConnectSemaphore(1, SYS_DEFAULT_MAXCOUNT,
                            LogSemIPCName)) == SYS_INVALID_SEMAPHORE)
    {
        ErrorPush();
        SysDisconnectSemaphoresHandling();
        return (ErrorPop());
    }


    siginterrupt(SIGALRM, 1);

    signal(SIGPIPE, SysIgnoreProc);
    signal(SIGINT, SysIgnoreProc);
    signal(SIGQUIT, SysIgnoreProc);

    return (0);

}



static int      SysThreadCleanup(void)
{

    extern void     ErrCleanupErrorInfos(int);


    SysCloseSemaphore(LogSemID);

    SysDisconnectSemaphoresHandling();

    ErrCleanupErrorInfos(1);

    return (0);

}



void            SysCloseThread(SYS_THREAD ThreadID, int iForce)
{

    if (iForce)
        kill((pid_t) ThreadID, SIGKILL);

}



int             SysSetThreadPriority(SYS_THREAD ThreadID, int iPriority)
{

    int             iSetResult = -1;

    switch (iPriority)
    {
        case (SYS_PRIORITY_NORMAL):
            iSetResult = setpriority(PRIO_PROCESS, (pid_t) ThreadID, 0);
            break;

        case (SYS_PRIORITY_LOWER):
            iSetResult = setpriority(PRIO_PROCESS, (pid_t) ThreadID, SCHED_PRIORITY_INC);
            break;

        case (SYS_PRIORITY_HIGHER):
            iSetResult = setpriority(PRIO_PROCESS, (pid_t) ThreadID, -SCHED_PRIORITY_INC);
            break;
    }

    if (iSetResult < 0)
    {
        ErrSetErrorCode(ERR_SET_THREAD_PRIORITY);
        return (ERR_SET_THREAD_PRIORITY);
    }

    return (0);

}



int             SysWaitThread(SYS_THREAD ThreadID, int iTimeout)
{

    SysSetAlarm(iTimeout);

    if (waitpid((pid_t) ThreadID, NULL, WUNTRACED) == -1)
    {
        SysSetAlarm(0);

        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    SysSetAlarm(0);

    return (0);

}



unsigned long   SysGetCurrentThreadId(void)
{

    return ((unsigned long) getpid());

}



void            SysIgnoreThreadsExit(void)
{

    signal(SIGCHLD, SIG_IGN);

}



int             SysExec(char const * pszCommand, char const * const * pszArgs, int iWaitTimeout,
                        int iPriority, int *piExitStatus)
{

    pid_t           ThreadID = (pid_t) fork();

    if (ThreadID == 0)
    {

        exit((int) execv(pszCommand, (char **) pszArgs));
    }

    if (ThreadID == (pid_t) (-1))
    {
        ErrSetErrorCode(ERR_FORK);
        return (ERR_FORK);
    }


    SysSetThreadPriority((SYS_THREAD) ThreadID, iPriority);


    if (iWaitTimeout > 0)
    {
        SysSetAlarm(iWaitTimeout);

        int             iExitStatus = 0;

        if (waitpid((pid_t) ThreadID, &iExitStatus, WUNTRACED) == -1)
        {
            SysSetAlarm(0);

            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        SysSetAlarm(0);

        if (WEXITSTATUS(iExitStatus) < 0)
        {
            ErrSetErrorCode(ERR_PROCESS_EXECUTE);
            return (ERR_PROCESS_EXECUTE);
        }

        if (piExitStatus != NULL)
            *piExitStatus = WEXITSTATUS(iExitStatus);
    }
    else if (piExitStatus != NULL)
        *piExitStatus = -1;


    return (0);

}



static void     SysBreakHandlerRoutine(int iSignal)
{

    if (SysBreakHandler != NULL)
    {
        SysBreakHandler();

        signal(SIGINT, SysBreakHandlerRoutine);
        signal(SIGQUIT, SysBreakHandlerRoutine);
    }

}



void            SysSetBreakHandler(void (*BreakHandler) (void))
{

    if (BreakHandler != NULL)
    {
        signal(SIGINT, SysBreakHandlerRoutine);
        signal(SIGQUIT, SysBreakHandlerRoutine);
    }
    else
    {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
    }

    SysBreakHandler = BreakHandler;

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



SYS_SHMEM       SysCreateSharedMem(unsigned int uSize, SYS_IPCNAME ShmName)
{

    int             iShmID = SysForcedShmGet(ShmName, uSize, 0600);

    return ((iShmID == -1) ? SYS_INVALID_SHMEM : (SYS_SHMEM) iShmID);

}




SYS_SHMEM       SysConnectSharedMem(unsigned int uSize, SYS_IPCNAME ShmName)
{

    SYS_SHMEM       ShmID = shmget(ShmName, uSize, 0);

    if (ShmID == -1)
    {
        ErrSetErrorCode(ERR_SHMGET);

        return (SYS_INVALID_SHMEM);
    }

    return (ShmID);

}



int             SysCloseSharedMem(SYS_SHMEM ShMemID)
{

    return (0);

}



int             SysKillSharedMem(SYS_SHMEM ShMemID)
{

    return (SysShmKill((int) ShMemID));

}



void           *SysMapSharedMem(SYS_SHMEM ShMemID)
{

    void           *pAddress = shmat(ShMemID, (char *) NULL, 0);

    if (pAddress == (void *) (-1))
    {
        ErrSetErrorCode(ERR_SHMAT);
        return (NULL);
    }

    return (pAddress);

}



int             SysUnmapSharedMem(SYS_SHMEM ShMemID, void *pAddress)
{

    if (shmdt(pAddress) == -1)
    {
        ErrSetErrorCode(ERR_SHMDT);
        return (ERR_SHMDT);
    }

    return (0);

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

    if (SysWaitSemaphore(LogSemID, SYS_INFINITE_TIMEOUT) < 0)
        return (ErrGetErrorCode());


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


    SysReleaseSemaphore(LogSemID, 1);

    return (0);

}




void            SysSleep(int iTimeout)
{

    sleep(iTimeout);

}



void            SysMsSleep(int iMsTimeout)
{

    usleep(1000 * (unsigned long) iMsTimeout);

}



SYS_INT64       SysMsTime(void)
{

    struct timeval  tv;

    if (gettimeofday(&tv, NULL) != 0)
        return (0);

    return (1000 * (SYS_INT64) tv.tv_sec + (SYS_INT64) tv.tv_usec / 1000 );

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

    struct dirent  *pDirEntry = readdir(pDIR);

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
    struct dirent  *pDirEntry = readdir(pFFD->pDIR);

    if (pDirEntry == NULL)
        return (0);

    pFFD->DE = *pDirEntry;

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
    FI.iFileType = (S_ISREG(stat_buffer.st_mode)) ? ftNormal:
            ((S_ISDIR(stat_buffer.st_mode)) ? ftDirectory:
            ((S_ISLNK(stat_buffer.st_mode)) ? ftLink: ftOther));
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

    static THRDLS unsigned int uFileSeqNr = 0;
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
