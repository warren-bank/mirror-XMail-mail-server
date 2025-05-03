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


#ifndef _POP3UTILS_H
#define _POP3UTILS_H




#define INVALID_POP3_HANDLE         ((POP3_HANDLE) 0)

#define AUTH_TYPE_CLEAR             "CLR"
#define AUTH_TYPE_APOP              "APOP"





typedef struct POP3_HANDLE_struct
{
}              *POP3_HANDLE;





int             UPopGetMailboxSize(UserInfo * pUI, unsigned long &ulMBSize);
int             UPopCheckMailboxSize(UserInfo * pUI, unsigned long *pulAvailSpace = NULL);
int             UPopAuthenticateAPOP(const char *pszDomain, const char *pszUsrName,
                        const char *pszTimeStamp, const char *pszDigest);
POP3_HANDLE     UPopBuildSession(const char *pszDomain, const char *pszUsrName,
                        const char *pszUsrPass, SYS_INET_ADDR const * pPeerInfo);
void            UPopReleaseSession(POP3_HANDLE hPOPSession, int iUpdate = 1);
char           *UPopGetUserInfoVar(POP3_HANDLE hPOPSession, const char *pszName,
                        const char *pszDefault = NULL);
int             UPopGetSessionMsgCount(POP3_HANDLE hPOPSession);
unsigned long   UPopGetSessionMBSize(POP3_HANDLE hPOPSession);
int             UPopGetSessionLastAccessed(POP3_HANDLE hPOPSession);
int             UPopGetMessageSize(POP3_HANDLE hPOPSession, int iMsgIndex,
                        unsigned long &ulMessageSize);
int             UPopGetMessageUIDL(POP3_HANDLE hPOPSession, int iMsgIndex,
                        char *pszMessageUIDL);
int             UPopDeleteMessage(POP3_HANDLE hPOPSession, int iMsgIndex);
int             UPopResetSession(POP3_HANDLE hPOPSession);
int             UPopSendErrorResponse(BSOCK_HANDLE hBSock, int iErrorCode, int iTimeout);
int             UPopSessionSendMsg(POP3_HANDLE hPOPSession, int iMsgIndex,
                        BSOCK_HANDLE hBSock);
int             UPopSessionTopMsg(POP3_HANDLE hPOPSession, int iMsgIndex, int iNumLines,
                        BSOCK_HANDLE hBSock);
int             UPopSyncRemoteLink(UserInfo * pUI, const char *pszRmtServer, const char *pszRmtName,
                        const char *pszRmtPassword, const char *pszAuthType = AUTH_TYPE_CLEAR);




#endif
