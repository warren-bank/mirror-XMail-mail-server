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
#include "SvrDefines.h"
#include "StrUtils.h"
#include "BuffSock.h"







#define BSOCK_EOF                   INT_MIN








struct BuffSocketData
{
    SYS_SOCKET      SockFD;
    int             iBufferSize;
    char           *pszBuffer;
    int             iBytesInBuffer;
    int             iReadIndex;
    int             iBufferIndex;
};





static int      BSckReadData(BuffSocketData * pBSD, int iTimeout);






BSOCK_HANDLE    BSckAttach(SYS_SOCKET SockFD, int iBufferSize)
{

    BuffSocketData *pBSD = (BuffSocketData *) SysAlloc(sizeof(BuffSocketData));

    if (pBSD == NULL)
        return (INVALID_BSOCK_HANDLE);

    char           *pszBuffer = (char *) SysAlloc(iBufferSize);

    if (pszBuffer == NULL)
    {
        SysFree(pBSD);
        return (INVALID_BSOCK_HANDLE);
    }

    pBSD->SockFD = SockFD;
    pBSD->iBufferSize = iBufferSize;
    pBSD->pszBuffer = pszBuffer;
    pBSD->iBytesInBuffer = 0;
    pBSD->iReadIndex = 0;
    pBSD->iBufferIndex = 0;

    return ((BSOCK_HANDLE) pBSD);

}



SYS_SOCKET      BSckDetach(BSOCK_HANDLE hBSock, int iCloseSocket)
{

    BuffSocketData *pBSD = (BuffSocketData *) hBSock;
    SYS_SOCKET      SockFD = pBSD->SockFD;

    SysFree(pBSD->pszBuffer);

    SysFree(pBSD);

    if (iCloseSocket)
    {
        SysCloseSocket(SockFD);
        return (SYS_INVALID_SOCKET);
    }

    return (SockFD);

}



static int      BSckReadData(BuffSocketData * pBSD, int iTimeout)
{

    int             iMaxRead = pBSD->iBufferSize - pBSD->iBytesInBuffer;
    char           *pszBuffer = (char *) SysAlloc(iMaxRead + 1);

    if (pszBuffer == NULL)
        return (ErrGetErrorCode());

    int             iReadedBytes = SysRecvData(pBSD->SockFD, pszBuffer, iMaxRead, iTimeout);

    if (iReadedBytes > 0)
    {
        int             iHeadSize = Min(pBSD->iBufferSize - pBSD->iBufferIndex, iReadedBytes);

        if (iHeadSize > 0)
            memcpy(pBSD->pszBuffer + pBSD->iBufferIndex, pszBuffer, iHeadSize);

        pBSD->iBufferIndex += iHeadSize;

        if (pBSD->iBufferIndex == pBSD->iBufferSize)
            pBSD->iBufferIndex = 0;

        int             iBackSize = iReadedBytes - iHeadSize;

        if (iBackSize > 0)
            memcpy(pBSD->pszBuffer + pBSD->iBufferIndex, pszBuffer + iHeadSize, iBackSize);

        pBSD->iBufferIndex += iBackSize;

        pBSD->iBytesInBuffer += iReadedBytes;
    }
    else if (iReadedBytes == 0)
        ErrSetErrorCode(ERR_SOCK_NOMORE_DATA);

    SysFree(pszBuffer);

    return (iReadedBytes);

}



int             BSckGetChar(BSOCK_HANDLE hBSock, int iTimeout)
{

    BuffSocketData *pBSD = (BuffSocketData *) hBSock;

    if ((pBSD->iBytesInBuffer == 0) &&
            (BSckReadData(pBSD, iTimeout) <= 0))
        return (BSOCK_EOF);

    int             iChar = (int) pBSD->pszBuffer[pBSD->iReadIndex];

    pBSD->iReadIndex = INext(pBSD->iReadIndex, pBSD->iBufferSize);

    --pBSD->iBytesInBuffer;

    return (iChar);

}



char           *BSckGetString(BSOCK_HANDLE hBSock, char *pszBuffer, int iMaxChars, int iTimeout)
{

    for (int ii = 0; ii < iMaxChars; ii++)
    {
        int             iChar = BSckGetChar(hBSock, iTimeout);

        if (iChar == BSOCK_EOF)
            return (NULL);

        if (iChar == '\n')
        {
            for (; (ii > 0) && (pszBuffer[ii - 1] == '\r'); ii--);

            pszBuffer[ii] = '\0';

            return (pszBuffer);
        }
        else
            pszBuffer[ii] = (char) iChar;

    }


    ErrSetErrorCode(ERR_LINE_TOO_LONG);

    return (NULL);

}



int             BSckSendString(BSOCK_HANDLE hBSock, char const * pszBuffer, int iTimeout)
{

    BuffSocketData *pBSD = (BuffSocketData *) hBSock;
    char           *pszSendBuffer = (char *) SysAlloc(strlen(pszBuffer) + 3);

    if (pszSendBuffer == NULL)
        return (ErrGetErrorCode());

    sprintf(pszSendBuffer, "%s\r\n", pszBuffer);

    int             iSendLength = strlen(pszSendBuffer);

    if (SysSend(pBSD->SockFD, pszSendBuffer, iSendLength, iTimeout) != iSendLength)
    {
        SysFree(pszSendBuffer);
        return (ErrGetErrorCode());
    }

    SysFree(pszSendBuffer);

    return (iSendLength);

}




int             BSckVSendString(BSOCK_HANDLE hBSock, int iTimeout, char const * pszFormat,...)
{

    va_list         Args;

    va_start(Args, pszFormat);


    char           *pszBuffer = StrVSprint(pszFormat, Args);


    va_end(Args);


    if (BSckSendString(hBSock, pszBuffer, iTimeout) < 0)
    {
        ErrorPush();
        SysFree(pszBuffer);
        return (ErrorPop());
    }

    SysFree(pszBuffer);

    return (0);

}




int             BSckSendData(BSOCK_HANDLE hBSock, char const * pszBuffer, int iSize, int iTimeout)
{

    BuffSocketData *pBSD = (BuffSocketData *) hBSock;

    if (SysSend(pBSD->SockFD, pszBuffer, iSize, iTimeout) != iSize)
        return (ErrGetErrorCode());

    return (iSize);

}




int             BSckReadData(BSOCK_HANDLE hBSock, char *pszBuffer, int iSize, int iTimeout)
{

    BuffSocketData *pBSD = (BuffSocketData *) hBSock;
    int             iReadedBytes = 0,
                    iReadFromBuffer = Min(iSize, pBSD->iBytesInBuffer);

    if (iReadFromBuffer > 0)
    {
        int             iHeadSize = Min(pBSD->iBufferSize - pBSD->iReadIndex, iReadFromBuffer);

        if (iHeadSize > 0)
            memcpy(pszBuffer, pBSD->pszBuffer + pBSD->iReadIndex, iHeadSize);

        pBSD->iReadIndex += iHeadSize;

        if (pBSD->iReadIndex == pBSD->iBufferSize)
            pBSD->iReadIndex = 0;

        int             iBackSize = iReadFromBuffer - iHeadSize;

        if (iBackSize > 0)
            memcpy(pszBuffer + iHeadSize, pBSD->pszBuffer + pBSD->iReadIndex, iBackSize);

        pBSD->iReadIndex += iBackSize;

        pBSD->iBytesInBuffer -= iReadFromBuffer;


        iReadedBytes = iReadFromBuffer;
    }


    if ((iReadedBytes < iSize) &&
            (SysRecv(pBSD->SockFD, pszBuffer + iReadedBytes, iSize - iReadedBytes, iTimeout) != (iSize - iReadedBytes)))
        return (ErrGetErrorCode());


    return (iSize);

}




SYS_SOCKET      BSckGetAttachedSocket(BSOCK_HANDLE hBSock)
{

    BuffSocketData *pBSD = (BuffSocketData *) hBSock;

    return (pBSD->SockFD);

}
