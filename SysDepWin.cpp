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
#include "AppDefines.h"






#define SOCK_VERSION_REQUESTED          MAKEWORD(2, 0)
#define SHUTDOWN_RECV_TIMEOUT           2

///////////////////////////////////////////////////////////////////////////////
//  Under certain circumstances ( M$ Proxy installed ?! ) a waiting operation
//  may be unlocked even if the IO terminal is not ready to perform following
//  IO request ( an error code WSAEWOULDBLOCK is returned ). The only solution
//  I see is to sleep a bit to prevent processor time wasting :(
///////////////////////////////////////////////////////////////////////////////
#define BLOCKED_SNDRCV_MSSLEEP          200
#define HANDLE_SOCKS_SUCKS()            do { Sleep(BLOCKED_SNDRCV_MSSLEEP); } while (0)

#define SAIN_Addr(s)                    (s).sin_addr.S_un.S_addr

#define MIN_TCP_SEND_SIZE               1024
#define MAX_TCP_SEND_SIZE               16384
#define MIN_BYTES_SEC_TIMEOUT           64






struct FileFindData
{
    char            szFindPath[SYS_MAX_PATH];
    HANDLE          hFind;
    WIN32_FIND_DATA WFD;
};

struct ThreadRunner
{
    unsigned int    (*pThreadProc) (void *);
    void           *pThreadData;
};






static char const *SysGetLastError(void);
static int      SysBlockSocket(SYS_SOCKET SockFD, int OnOff);
static int      SysSetSocketsOptions(SYS_SOCKET SockFD);
static int      SysRecvLL(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize);
static int      SysSendLL(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize);
static unsigned int SysThreadRunner(void *pRunData);
static int      SysThreadSetup(void);
static int      SysThreadCleanup(void);
static BOOL WINAPI SysBreakHandlerRoutine(DWORD dwCtrlType);








static SYS_IPCNAME LogSemIPCName;
static SYS_SEMAPHORE RootLogSemID;
static THRDLS SYS_SEMAPHORE LogSemID;
static void     (*SysBreakHandler) (void) = NULL;








static char const *SysGetLastError(void)
{

    char           *pszMessage = NULL;
    DWORD           dwError = GetLastError(),
                    dwRet = FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY,
            NULL,
            dwError,
            LANG_NEUTRAL,
            (LPTSTR) & pszMessage,
            0,
            NULL);

    static char     szMessage[1024] = "";

    if (dwRet == 0)
        _snprintf(szMessage, sizeof(szMessage) - 1, "(0x%lX) Unknown error", (unsigned long) dwError);
    else
        _snprintf(szMessage, sizeof(szMessage) - 1, "(0x%lX) %s", (unsigned long) dwError, pszMessage);

    if (pszMessage != NULL)
        LocalFree((HLOCAL) pszMessage);

    return (szMessage);

}




int             SysInitLibrary(void)
{

    WSADATA         WSD;

    ZeroData(WSD);

    if (WSAStartup(SOCK_VERSION_REQUESTED, &WSD))
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (-1);
    }


    LogSemIPCName = SysCreateIPCName();
    if ((RootLogSemID = SysCreateSemaphore(1, SYS_DEFAULT_MAXCOUNT,
                            LogSemIPCName)) == SYS_INVALID_SEMAPHORE)
        return (ErrGetErrorCode());


    if (SysThreadSetup() < 0)
    {
        ErrorPush();
        SysCloseSemaphore(RootLogSemID);
        SysKillSemaphore(RootLogSemID);
        return (ErrorPop());
    }


    return (0);

}



void            SysCleanupLibrary(void)
{

    SysThreadCleanup();

    SysCloseSemaphore(RootLogSemID);
    SysKillSemaphore(RootLogSemID);

    WSACleanup();

}



SYS_SOCKET      SysCreateSocket(int iAddressFamily, int iType, int iProtocol)
{

    SOCKET          SockFD = WSASocket(iAddressFamily, iType, iProtocol, NULL, 0,
            WSA_FLAG_OVERLAPPED);

    if (SockFD == INVALID_SOCKET)
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



static int      SysBlockSocket(SYS_SOCKET SockFD, int OnOff)
{

    u_long          IoctlLong = (OnOff) ? 0 : 1;

    if (ioctlsocket(SockFD, FIONBIO, &IoctlLong) == SOCKET_ERROR)
    {
        ErrSetErrorCode(ERR_NETWORK);
        return (ERR_NETWORK);
    }

    return (0);

}



static int      SysSetSocketsOptions(SYS_SOCKET SockFD)
{

    int             iActivate = 1;

    if (setsockopt(SockFD, SOL_SOCKET, SO_REUSEADDR, (const char *) &iActivate,
                    sizeof(iActivate)) != 0)
    {
        ErrSetErrorCode(ERR_SETSOCKOPT);
        return (ERR_SETSOCKOPT);
    }

///////////////////////////////////////////////////////////////////////////////
//  Set KEEPALIVE if supported
///////////////////////////////////////////////////////////////////////////////
    setsockopt(SockFD, SOL_SOCKET, SO_KEEPALIVE, (const char *) &iActivate,
            sizeof(iActivate));


    return (0);

}



void            SysCloseSocket(SYS_SOCKET SockFD, int iHardClose)
{

    if (!iHardClose)
    {
        shutdown(SockFD, SD_SEND);

        int             iRecvResult;
        HANDLE          hSockEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

        do
        {
            if (hSockEvent != NULL)
            {
                WSAEventSelect(SockFD, (WSAEVENT) hSockEvent, FD_READ | FD_CLOSE);

                WSAWaitForMultipleEvents(1, &hSockEvent, TRUE,
                        (DWORD) (SHUTDOWN_RECV_TIMEOUT * 1000), TRUE);

                WSAEventSelect(SockFD, (WSAEVENT) hSockEvent, 0);
            }

            char            szBuffer[256] = "";

            iRecvResult = SysRecvLL(SockFD, szBuffer, sizeof(szBuffer));

        } while (iRecvResult > 0);

        if (hSockEvent != NULL)
            CloseHandle(hSockEvent);

        shutdown(SockFD, SD_BOTH);
    }

    closesocket(SockFD);

}



int             SysBindSocket(SYS_SOCKET SockFD, const struct sockaddr * SockName, int iNameLen)
{

    if (bind(SockFD, SockName, iNameLen) == SOCKET_ERROR)
    {
        ErrSetErrorCode(ERR_SOCKET_BIND);
        return (ERR_SOCKET_BIND);
    }

    return (0);

}



void            SysListenSocket(SYS_SOCKET SockFD, int iConnections)
{

    listen(SockFD, iConnections);

}



static int      SysRecvLL(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize)
{

    DWORD           dwRtxBytes = 0,
                    dwRtxFlags = 0;
    WSABUF          WSABuff;

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = pszBuffer;

    return ((WSARecv(SockFD, &WSABuff, 1, &dwRtxBytes, &dwRtxFlags,
                            NULL, NULL) == 0) ? (int) dwRtxBytes : -WSAGetLastError());

}



static int      SysSendLL(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize)
{

    DWORD           dwRtxBytes = 0;
    WSABUF          WSABuff;

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = (char *) pszBuffer;

    return ((WSASend(SockFD, &WSABuff, 1, &dwRtxBytes, 0,
                            NULL, NULL) == 0) ? (int) dwRtxBytes : -WSAGetLastError());

}



int             SysRecvData(SYS_SOCKET SockFD, char *pszBuffer, int iBufferSize, int iTimeout)
{

    HANDLE          hReadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hReadEvent == NULL)
    {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return (ERR_CREATEEVENT);
    }

    int             iRecvBytes = 0;

    for (;;)
    {
        WSAEventSelect(SockFD, (WSAEVENT) hReadEvent, FD_READ | FD_CLOSE);


        DWORD           dwWaitResult = WSAWaitForMultipleEvents(1, &hReadEvent, TRUE,
                (DWORD) (iTimeout * 1000), TRUE);


        WSAEventSelect(SockFD, (WSAEVENT) hReadEvent, 0);

        if (dwWaitResult != WSA_WAIT_EVENT_0)
        {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }


        if ((iRecvBytes = SysRecvLL(SockFD, pszBuffer, iBufferSize)) >= 0)
            break;


        int             iErrorCode = -iRecvBytes;

        if (iErrorCode != WSAEWOULDBLOCK)
        {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return (ERR_NETWORK);
        }

///////////////////////////////////////////////////////////////////////////////
//  You should never get here if Win32 API worked fine
///////////////////////////////////////////////////////////////////////////////
        HANDLE_SOCKS_SUCKS();
    }

    CloseHandle(hReadEvent);

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

    HANDLE          hReadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hReadEvent == NULL)
    {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return (ERR_CREATEEVENT);
    }

    DWORD           dwRtxBytes = 0;
    WSABUF          WSABuff;

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = pszBuffer;

    for (;;)
    {
        WSAEventSelect(SockFD, (WSAEVENT) hReadEvent, FD_READ | FD_CLOSE);


        DWORD           dwWaitResult = WSAWaitForMultipleEvents(1, &hReadEvent, TRUE,
                (DWORD) (iTimeout * 1000), TRUE);


        WSAEventSelect(SockFD, (WSAEVENT) hReadEvent, 0);

        if (dwWaitResult != WSA_WAIT_EVENT_0)
        {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        INT             FromLen = (INT) iFromlen;
        DWORD           dwRtxFlags = 0;

        if (WSARecvFrom(SockFD, &WSABuff, 1, &dwRtxBytes, &dwRtxFlags,
                        pFrom, &FromLen, NULL, NULL) == 0)
            break;

        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            CloseHandle(hReadEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return (ERR_NETWORK);
        }

///////////////////////////////////////////////////////////////////////////////
//  You should never get here if Win32 API worked fine
///////////////////////////////////////////////////////////////////////////////
        HANDLE_SOCKS_SUCKS();
    }

    CloseHandle(hReadEvent);

    return ((int) dwRtxBytes);

}



int             SysSendData(SYS_SOCKET SockFD, char const * pszBuffer, int iBufferSize, int iTimeout)
{

    HANDLE          hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hWriteEvent == NULL)
    {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return (ERR_CREATEEVENT);
    }

    int             iSendBytes = 0;

    for (;;)
    {
        WSAEventSelect(SockFD, (WSAEVENT) hWriteEvent, FD_WRITE | FD_CLOSE);


        DWORD           dwWaitResult = WSAWaitForMultipleEvents(1, &hWriteEvent, TRUE,
                (DWORD) (iTimeout * 1000), TRUE);


        WSAEventSelect(SockFD, (WSAEVENT) hWriteEvent, 0);

        if (dwWaitResult != WSA_WAIT_EVENT_0)
        {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }


        if ((iSendBytes = SysSendLL(SockFD, pszBuffer, iBufferSize)) >= 0)
            break;


        int             iErrorCode = -iSendBytes;

        if (iErrorCode != WSAEWOULDBLOCK)
        {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return (ERR_NETWORK);
        }

///////////////////////////////////////////////////////////////////////////////
//  You should never get here if Win32 API worked fine
///////////////////////////////////////////////////////////////////////////////
        HANDLE_SOCKS_SUCKS();
    }

    CloseHandle(hWriteEvent);

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

    HANDLE          hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hWriteEvent == NULL)
    {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return (ERR_CREATEEVENT);
    }

    DWORD           dwRtxBytes = 0;
    WSABUF          WSABuff;

    ZeroData(WSABuff);
    WSABuff.len = iBufferSize;
    WSABuff.buf = (char *) pszBuffer;

    for (;;)
    {
        WSAEventSelect(SockFD, (WSAEVENT) hWriteEvent, FD_WRITE | FD_CLOSE);


        DWORD           dwWaitResult = WSAWaitForMultipleEvents(1, &hWriteEvent, TRUE,
                (DWORD) (iTimeout * 1000), TRUE);


        WSAEventSelect(SockFD, (WSAEVENT) hWriteEvent, 0);

        if (dwWaitResult != WSA_WAIT_EVENT_0)
        {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        DWORD           dwRtxFlags = 0;

        if (WSASendTo(SockFD, &WSABuff, 1, &dwRtxBytes, dwRtxFlags,
                        pTo, iToLen, NULL, NULL) == 0)
            break;

        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            CloseHandle(hWriteEvent);
            ErrSetErrorCode(ERR_NETWORK);
            return (ERR_NETWORK);
        }

///////////////////////////////////////////////////////////////////////////////
//  You should never get here if Win32 API worked fine
///////////////////////////////////////////////////////////////////////////////
        HANDLE_SOCKS_SUCKS();
    }

    CloseHandle(hWriteEvent);

    return ((int) dwRtxBytes);

}



int             SysConnect(SYS_SOCKET SockFD, const SYS_INET_ADDR * pSockName, int iNameLen,
                        int iTimeout)
{

    HANDLE          hConnectEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hConnectEvent == NULL)
    {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return (ERR_CREATEEVENT);
    }

    WSAEventSelect(SockFD, (WSAEVENT) hConnectEvent, FD_CONNECT);

    int             iConnectResult = WSAConnect(SockFD, (const struct sockaddr *) & pSockName->Addr,
            iNameLen, NULL, NULL, NULL, NULL),
                    iConnectError = WSAGetLastError();

    if ((iConnectResult != 0) && (iConnectError == WSAEWOULDBLOCK))
    {
        DWORD           dwWaitResult = WSAWaitForMultipleEvents(1, &hConnectEvent, TRUE,
                (DWORD) (iTimeout * 1000), TRUE);

        if (dwWaitResult == WSA_WAIT_EVENT_0)
            iConnectResult = 0;
        else
        {
            ErrSetErrorCode(ERR_TIMEOUT);

            iConnectResult = ERR_TIMEOUT;
        }
    }

    WSAEventSelect(SockFD, (WSAEVENT) hConnectEvent, 0);
    CloseHandle(hConnectEvent);

    return (iConnectResult);

}



SYS_SOCKET      SysAccept(SYS_SOCKET SockFD, SYS_INET_ADDR * pSockName, int *iNameLen,
                        int iTimeout)
{

    HANDLE          hAcceptEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hAcceptEvent == NULL)
    {
        ErrSetErrorCode(ERR_CREATEEVENT);
        return (SYS_INVALID_SOCKET);
    }

    WSAEventSelect(SockFD, (WSAEVENT) hAcceptEvent, FD_ACCEPT);

    SOCKET          SockFDAccept = WSAAccept(SockFD, (struct sockaddr *) & pSockName->Addr, iNameLen, NULL, 0);

    int             iConnectError = WSAGetLastError();

    if ((SockFDAccept == INVALID_SOCKET) && (iConnectError == WSAEWOULDBLOCK))
    {
        DWORD           dwWaitResult = WSAWaitForMultipleEvents(1, &hAcceptEvent, TRUE,
                (DWORD) (iTimeout * 1000), TRUE);

        if (dwWaitResult == WSA_WAIT_EVENT_0)
            SockFDAccept = WSAAccept(SockFD, (struct sockaddr *) & pSockName->Addr, iNameLen, NULL, 0);
        else
            ErrSetErrorCode(ERR_TIMEOUT);
    }

    WSAEventSelect(SockFD, (WSAEVENT) hAcceptEvent, 0);
    CloseHandle(hAcceptEvent);

    if (SockFDAccept != INVALID_SOCKET)
    {
        if ((SysBlockSocket(SockFDAccept, 0) < 0) ||
                (SysSetSocketsOptions(SockFDAccept) < 0))
        {
            SysCloseSocket(SockFDAccept, 1);
            return (ErrGetErrorCode());
        }
    }

    return (SockFDAccept);

}



int             SysSelect(int iMaxFD, SYS_fd_set * pReadFDs, SYS_fd_set * pWriteFDs, SYS_fd_set * pExcptFDs,
                        int iTimeout)
{

    struct timeval  TV;

    ZeroData(TV);
    TV.tv_sec = iTimeout;
    TV.tv_usec = 0;


    int             iSelectResult = select(iMaxFD + 1, pReadFDs, pWriteFDs, pExcptFDs, &TV);


    if (iSelectResult < 0)
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



int             SysSendFile(SYS_SOCKET SockFD, char const * pszFileName, int iTimeout)
{
///////////////////////////////////////////////////////////////////////////////
//  Open the source file
///////////////////////////////////////////////////////////////////////////////
    HANDLE          hFile = CreateFile(pszFileName, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Create file mapping and the file
///////////////////////////////////////////////////////////////////////////////
    DWORD           dwFileSizeHi = 0,
                    dwFileSizeLo = GetFileSize(hFile, &dwFileSizeHi);
    HANDLE          hFileMap = CreateFileMapping(hFile, NULL, PAGE_READONLY,
            dwFileSizeHi, dwFileSizeLo, NULL);

    if (hFileMap == NULL)
    {
        CloseHandle(hFile);

        ErrSetErrorCode(ERR_CREATEFILEMAPPING);
        return (ERR_CREATEFILEMAPPING);
    }

    void           *pAddress = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 0);

    if (pAddress == NULL)
    {
        CloseHandle(hFileMap);
        CloseHandle(hFile);

        ErrSetErrorCode(ERR_MAPVIEWOFFILE);
        return (ERR_MAPVIEWOFFILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup the maximum data transfer size
///////////////////////////////////////////////////////////////////////////////
    int             iSndBuffSize = 0,
                    iOptLenght = sizeof(iSndBuffSize);

    if (getsockopt(SockFD, SOL_SOCKET, SO_SNDBUF, (char *) &iSndBuffSize, &iOptLenght) != 0)
        iSndBuffSize = MIN_TCP_SEND_SIZE;
    else
        iSndBuffSize = min(iSndBuffSize, MAX_TCP_SEND_SIZE);


///////////////////////////////////////////////////////////////////////////////
//  Send the file
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT64      ullFileSize = (((SYS_UINT64) dwFileSizeHi) << 32) | (SYS_UINT64) dwFileSizeLo,
                    ullSentBytes = 0;
    char           *pszBuffer = (char *) pAddress;

    while (ullSentBytes < ullFileSize)
    {
        int             iCurrSend = (int) min(iSndBuffSize, ullFileSize - ullSentBytes);

        if ((iCurrSend = SysSendData(SockFD, pszBuffer, iCurrSend,
                                max(iTimeout, iCurrSend / MIN_BYTES_SEC_TIMEOUT))) < 0)
        {
            ErrorPush();
            UnmapViewOfFile(pAddress);
            CloseHandle(hFileMap);
            CloseHandle(hFile);
            return (ErrorPop());
        }

        pszBuffer += iCurrSend;
        ullSentBytes += (SYS_UINT64) iCurrSend;
    }


    UnmapViewOfFile(pAddress);
    CloseHandle(hFileMap);
    CloseHandle(hFile);

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

    return ((((SYS_IPCNAME) uKeyMagic) << 32) | ((SYS_IPCNAME)++ uIPCSeqNr));

}




SYS_SEMAPHORE   SysCreateSemaphore(int iInitCount, int iMaxCount, SYS_IPCNAME SemName)
{

    char            szSemName[128] = "";

    sprintf(szSemName, "Sem%I64X", SemName);

    HANDLE          hSemaphore = CreateSemaphore(NULL, iInitCount, iMaxCount, szSemName);

    if (hSemaphore == NULL)
    {
        ErrSetErrorCode(ERR_CREATESEMAPHORE);
        return (SYS_INVALID_SEMAPHORE);
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hSemaphore);

        ErrSetErrorCode(ERR_SEM_ALREADY_EXIST);
        return (SYS_INVALID_SEMAPHORE);
    }

    return ((SYS_SEMAPHORE) hSemaphore);

}



SYS_SEMAPHORE   SysConnectSemaphore(int iInitCount, int iMaxCount, SYS_IPCNAME SemName)
{

    char            szSemName[128] = "";

    sprintf(szSemName, "Sem%I64X", SemName);

    HANDLE          hSemaphore = CreateSemaphore(NULL, iInitCount, iMaxCount, szSemName);

    if (hSemaphore == NULL)
    {
        ErrSetErrorCode(ERR_CREATESEMAPHORE);
        return (SYS_INVALID_SEMAPHORE);
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hSemaphore);

        ErrSetErrorCode(ERR_SEM_NOT_EXIST);
        return (SYS_INVALID_SEMAPHORE);
    }

    return ((SYS_SEMAPHORE) hSemaphore);

}



int             SysCloseSemaphore(SYS_SEMAPHORE SemID)
{

    if (!CloseHandle((HANDLE) SemID))
    {
        ErrSetErrorCode(ERR_CLOSEHANDLE);
        return (ERR_CLOSEHANDLE);
    }

    return (0);

}



int             SysKillSemaphore(SYS_SEMAPHORE SemID)
{

    return (0);

}



int             SysWaitSemaphore(SYS_SEMAPHORE SemID, int iTimeout)
{

    if (WaitForSingleObject((HANDLE) SemID, (DWORD) (iTimeout * 1000)) != WAIT_OBJECT_0)
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    return (0);

}



int             SysReleaseSemaphore(SYS_SEMAPHORE SemID, int iCount)
{

    if (!ReleaseSemaphore((HANDLE) SemID, (LONG) iCount, NULL))
    {
        ErrSetErrorCode(ERR_RELEASESEMAPHORE);
        return (ERR_RELEASESEMAPHORE);
    }

    return (0);

}




static unsigned int SysThreadRunner(void *pRunData)
{

    ThreadRunner   *pTR = (ThreadRunner *) pRunData;

    if (SysThreadSetup() < 0)
    {
        SysFree(pTR);
        return (ErrGetErrorCode());
    }


    unsigned int    uResultCode = pTR->pThreadProc(pTR->pThreadData);


    SysThreadCleanup();

    SysFree(pTR);

    return (uResultCode);

}




SYS_THREAD      SysCreateThread(unsigned int (*pThreadProc) (void *), void *pThreadData)
{
///////////////////////////////////////////////////////////////////////////////
//  Alloc thread runner data
///////////////////////////////////////////////////////////////////////////////
    ThreadRunner   *pTR = (ThreadRunner *) SysAlloc(sizeof(ThreadRunner));

    if (pTR == NULL)
        return (SYS_INVALID_THREAD);

    pTR->pThreadProc = pThreadProc;
    pTR->pThreadData = pThreadData;

///////////////////////////////////////////////////////////////////////////////
//  Create the thread
///////////////////////////////////////////////////////////////////////////////
    unsigned int    uThreadId = 0;
    unsigned long   ulThread = _beginthreadex(NULL, 0,
            (unsigned (__stdcall *) (void *)) SysThreadRunner, pTR, 0, &uThreadId);

    if (ulThread == 0)
    {
        SysFree(pTR);
        ErrSetErrorCode(ERR_BEGINTHREADEX);
        return (SYS_INVALID_THREAD);
    }

    return ((SYS_THREAD) ulThread);

}



SYS_THREAD      SysCreateServiceThread(unsigned int (*pThreadProc) (void *), SYS_SOCKET SockFD)
{

    SYS_THREAD      ThreadID = SysCreateThread(pThreadProc, (void *) (unsigned int) SockFD);


    return (ThreadID);

}



static int      SysThreadSetup(void)
{

    if ((LogSemID = SysConnectSemaphore(1, SYS_DEFAULT_MAXCOUNT,
                            LogSemIPCName)) == SYS_INVALID_SEMAPHORE)
        return (ErrGetErrorCode());


    return (0);

}



static int      SysThreadCleanup(void)
{

    extern void     ErrCleanupErrorInfos(int);


    SysCloseSemaphore(LogSemID);


    ErrCleanupErrorInfos(1);

    return (0);

}



void            SysCloseThread(SYS_THREAD ThreadID, int iForce)
{

    if (iForce)
        TerminateThread((HANDLE) ThreadID, (DWORD) - 1);

    CloseHandle((HANDLE) ThreadID);

}



int             SysWaitThread(SYS_THREAD ThreadID, int iTimeout)
{

    if (WaitForSingleObject((HANDLE) ThreadID, (DWORD) (iTimeout * 1000)) != WAIT_OBJECT_0)
    {
        ErrSetErrorCode(ERR_TIMEOUT);
        return (ERR_TIMEOUT);
    }

    return (0);

}



unsigned long   SysGetCurrentThreadId(void)
{

    return ((unsigned long) GetCurrentThreadId());

}



void            SysIgnoreThreadsExit(void)
{


}



int             SysExec(char const * pszCommand, char const * const * pszArgs, int iWaitTimeout,
                        int iPriority, int *piExitStatus)
{

    int             ii,
                    iCommandLength = strlen(pszCommand) + 4;

    for (ii = 1; pszArgs[ii] != NULL; ii++)
        iCommandLength += strlen(pszArgs[ii]) + 4;

    char           *pszCmdLine = (char *) SysAlloc(iCommandLength + 1);

    if (pszCmdLine == NULL)
        return (ErrGetErrorCode());

    strcpy(pszCmdLine, pszCommand);

    for (ii = 1; pszArgs[ii] != NULL; ii++)
        sprintf(StrAppend(pszCmdLine), " \"%s\"", pszArgs[ii]);


    STARTUPINFO     SI;
    PROCESS_INFORMATION PI;

    ZeroData(SI);
    ZeroData(PI);
    SI.cb = sizeof(STARTUPINFO);

    if (!CreateProcess(NULL, pszCmdLine, NULL, NULL, FALSE,
                    CREATE_NO_WINDOW | DETACHED_PROCESS | NORMAL_PRIORITY_CLASS, NULL, NULL, &SI, &PI))
    {
        SysFree(pszCmdLine);

        ErrSetErrorCode(ERR_PROCESS_EXECUTE);
        return (ERR_PROCESS_EXECUTE);
    }

    SysFree(pszCmdLine);


    switch (iPriority)
    {
        case (SYS_PRIORITY_LOWER):
            SetThreadPriority(PI.hThread, THREAD_PRIORITY_BELOW_NORMAL);
            break;

        case (SYS_PRIORITY_HIGHER):
            SetThreadPriority(PI.hThread, THREAD_PRIORITY_ABOVE_NORMAL);
            break;
    }


    if (iWaitTimeout > 0)
    {
        if (WaitForSingleObject(PI.hProcess, iWaitTimeout * 1000) != WAIT_OBJECT_0)
        {
            CloseHandle(PI.hThread);
            CloseHandle(PI.hProcess);

            ErrSetErrorCode(ERR_TIMEOUT);
            return (ERR_TIMEOUT);
        }

        if (piExitStatus != NULL)
        {
            DWORD           dwExitCode = 0;

            if (!GetExitCodeProcess(PI.hProcess, &dwExitCode))
                *piExitStatus = -1;
            else
                *piExitStatus = (int) dwExitCode;
        }
    }
    else if (piExitStatus != NULL)
        *piExitStatus = -1;

    CloseHandle(PI.hThread);
    CloseHandle(PI.hProcess);

    return (0);

}




static BOOL WINAPI SysBreakHandlerRoutine(DWORD dwCtrlType)
{

    BOOL            bReturnValue = FALSE;

    switch (dwCtrlType)
    {
        case (CTRL_C_EVENT):
        case (CTRL_CLOSE_EVENT):
        case (CTRL_SHUTDOWN_EVENT):

            if (SysBreakHandler != NULL)
                SysBreakHandler(), bReturnValue = TRUE;

            break;

    }

    return (bReturnValue);

}



void            SysSetBreakHandler(void (*BreakHandler) (void))
{

    if (SysBreakHandler == NULL)
        SetConsoleCtrlHandler(SysBreakHandlerRoutine,
                (BreakHandler != NULL) ? TRUE : FALSE);

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



int             SysLockFile(const char *pszFileName)
{

    char            szLockFile[SYS_MAX_PATH] = "";

    sprintf(szLockFile, "%s.lock", pszFileName);

    int             iFileID = _open(szLockFile, _O_CREAT | _O_EXCL | _O_BINARY | _O_RDWR,
            _S_IREAD | _S_IWRITE);

    if (iFileID == -1)
    {
        ErrSetErrorCode(ERR_LOCKED);
        return (ERR_LOCKED);
    }

    char            szLock[128] = "";

    sprintf(szLock, "%lu", (unsigned long) GetCurrentThreadId());

    _write(iFileID, szLock, strlen(szLock) + 1);

    _close(iFileID);

    return (0);

}



int             SysUnlockFile(const char *pszFileName)
{

    char            szLockFile[SYS_MAX_PATH] = "";

    sprintf(szLockFile, "%s.lock", pszFileName);

    if (_unlink(szLockFile) != 0)
    {
        ErrSetErrorCode(ERR_NOT_LOCKED);
        return (ERR_NOT_LOCKED);
    }

    return (0);

}



SYS_SHMEM       SysCreateSharedMem(unsigned int uSize, SYS_IPCNAME ShmName)
{

    char            szShmName[128] = "";

    sprintf(szShmName, "Shm%I64X", ShmName);

    HANDLE          hMemMap = CreateFileMapping((HANDLE) 0xFFFFFFFF, NULL, PAGE_READWRITE,
            0, uSize, szShmName);

    if (hMemMap == NULL)
    {
        ErrSetErrorCode(ERR_CREATEFILEMAPPING);
        return (SYS_INVALID_SHMEM);
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMemMap);

        ErrSetErrorCode(ERR_SHMEM_ALREADY_EXIST);
        return (SYS_INVALID_SHMEM);
    }

    return ((SYS_SHMEM) hMemMap);

}



SYS_SHMEM       SysConnectSharedMem(unsigned int uSize, SYS_IPCNAME ShmName)
{

    char            szShmName[128] = "";

    sprintf(szShmName, "Shm%I64X", ShmName);

    HANDLE          hMemMap = CreateFileMapping((HANDLE) 0xFFFFFFFF, NULL, PAGE_READWRITE,
            0, uSize, szShmName);

    if (hMemMap == NULL)
    {
        ErrSetErrorCode(ERR_CREATEFILEMAPPING);
        return (SYS_INVALID_SHMEM);
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMemMap);

        ErrSetErrorCode(ERR_SHMEM_NOT_EXIST);
        return (SYS_INVALID_SHMEM);
    }

    return ((SYS_SHMEM) hMemMap);

}



int             SysCloseSharedMem(SYS_SHMEM ShMemID)
{

    if (!CloseHandle((HANDLE) ShMemID))
    {
        ErrSetErrorCode(ERR_CLOSEHANDLE);
        return (ERR_CLOSEHANDLE);
    }

    return (0);

}



int             SysKillSharedMem(SYS_SHMEM ShMemID)
{

    return (0);

}



void           *SysMapSharedMem(SYS_SHMEM ShMemID)
{

    void           *pAddress = MapViewOfFile((HANDLE) ShMemID, FILE_MAP_WRITE, 0, 0, 0);

    if (pAddress == NULL)
        ErrSetErrorCode(ERR_MAPVIEWOFFILE);

    return (pAddress);

}



int             SysUnmapSharedMem(SYS_SHMEM ShMemID, void *pAddress)
{

    if (!UnmapViewOfFile(pAddress))
    {
        ErrSetErrorCode(ERR_UNMAPVIEWOFFILE);
        return (ERR_UNMAPVIEWOFFILE);
    }

    return (0);

}




int             SysEventLogV(char const * pszFormat, va_list Args)
{

    HANDLE          hEventSource = RegisterEventSource(NULL, APP_NAME_STR);

    if (hEventSource == NULL)
    {
        ErrSetErrorCode(ERR_REGISTER_EVENT_SOURCE);
        return (ERR_REGISTER_EVENT_SOURCE);
    }


    char           *pszStrings[2];
    char            szBuffer[2048] = "";

    _vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);
    pszStrings[0] = szBuffer;


    ReportEvent(hEventSource,
            EVENTLOG_ERROR_TYPE,
            0,
            0,
            NULL,
            1,
            0,
            (const char **) pszStrings,
            NULL);


    DeregisterEventSource(hEventSource);

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

    Sleep(iTimeout * 1000);

}



void            SysMsSleep(int iMsTimeout)
{

    Sleep(iMsTimeout);

}



int             SysExistFile(const char *pszFilePath)
{

    return ((_access(pszFilePath, 00) == 0) ? 1 : 0);

}



SYS_HANDLE      SysFirstFile(const char *pszPath, char *pszFileName)
{

    char            szMatch[SYS_MAX_PATH] = "";

    strcpy(szMatch, pszPath);
    AppendSlash(szMatch);
    strcat(szMatch, "*");

    WIN32_FIND_DATA WFD;
    HANDLE          hFind = FindFirstFile(szMatch, &WFD);

    if (hFind == INVALID_HANDLE_VALUE)
        return (SYS_INVALID_HANDLE);

    FileFindData   *pFFD = (FileFindData *) SysAlloc(sizeof(FileFindData));

    if (pFFD == NULL)
    {
        FindClose(hFind);
        return (SYS_INVALID_HANDLE);
    }

    strcpy(pFFD->szFindPath, pszPath);
    AppendSlash(pFFD->szFindPath);

    pFFD->hFind = hFind;
    pFFD->WFD = WFD;

    strcpy(pszFileName, pFFD->WFD.cFileName);

    return ((SYS_HANDLE) pFFD);

}



int             SysIsDirectory(SYS_HANDLE hFind)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    return ((pFFD->WFD.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0);

}



unsigned long   SysGetSize(SYS_HANDLE hFind)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    return ((unsigned long) pFFD->WFD.nFileSizeLow);

}



int             SysNextFile(SYS_HANDLE hFind, char *pszFileName)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    if (!FindNextFile(pFFD->hFind, &pFFD->WFD))
        return (0);

    strcpy(pszFileName, pFFD->WFD.cFileName);

    return (1);

}



void            SysFindClose(SYS_HANDLE hFind)
{

    FileFindData   *pFFD = (FileFindData *) hFind;

    FindClose(pFFD->hFind);

    SysFree(pFFD);

}



int             SysGetFileInfo(char const * pszFileName, SYS_FILE_INFO & FI)
{

    struct _stat    stat_buffer;

    if (_stat(pszFileName, &stat_buffer) < 0)
    {
        ErrSetErrorCode(ERR_STAT);
        return (ERR_STAT);
    }

    ZeroData(FI);
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

    char            szRKeyPath[256] = "";

    sprintf(szRKeyPath, "SOFTWARE\\%s\\%s", APP_PRODUCER, APP_NAME_STR);

    HKEY            hKey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, szRKeyPath, 0, KEY_QUERY_VALUE,
                    &hKey) == ERROR_SUCCESS)
    {
        char            szKeyValue[2048] = "";
        DWORD           dwSize = sizeof(szKeyValue),
                        dwKeyType;

        if (RegQueryValueEx(hKey, pszVarName, NULL, &dwKeyType, (u_char *) szKeyValue,
                        &dwSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);

            return (SysStrDup(szKeyValue));
        }

        RegCloseKey(hKey);
    }

    const char     *pszValue = getenv(pszVarName);

    return ((pszValue != NULL) ? SysStrDup(pszValue) : NULL);

}



char           *SysGetTmpFile(char *pszFileName)
{

    char            szTmpPath[SYS_MAX_PATH] = "";

    GetTempPath(sizeof(szTmpPath), szTmpPath);

    static THRDLS unsigned int uFileSeqNr = 0;
    SYS_LONGLONG    llFileID = (((SYS_LONGLONG) GetCurrentThreadId()) << 32) | (SYS_LONGLONG)++ uFileSeqNr;

    sprintf(pszFileName, "%smsrv%I64x.tmp", szTmpPath, llFileID);

    return (pszFileName);

}



int             SysRemove(const char *pszFileName)
{

    if (!DeleteFile(pszFileName))
    {
        ErrSetErrorCode(ERR_FILE_DELETE);
        return (ERR_FILE_DELETE);
    }

    return (0);

}



int             SysMakeDir(const char *pszPath)
{

    if (!CreateDirectory(pszPath, NULL))
    {
        ErrSetErrorCode(ERR_DIR_CREATE);
        return (ERR_DIR_CREATE);
    }

    return (0);

}



int             SysRemoveDir(const char *pszPath)
{

    if (!RemoveDirectory(pszPath))
    {
        ErrSetErrorCode(ERR_DIR_DELETE);
        return (ERR_DIR_DELETE);
    }

    return (0);

}



int             SysMoveFile(char const * pszOldName, char const * pszNewName)
{

    if (!MoveFileEx(pszOldName, pszNewName, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
        ErrSetErrorCode(ERR_FILE_MOVE);
        return (ERR_FILE_MOVE);
    }

    return (0);

}
