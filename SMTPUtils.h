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


#ifndef _SMTPUTILS_H
#define _SMTPUTILS_H




#define INVALID_MXS_HANDLE          ((MXS_HANDLE) 0)




typedef struct MXS_HANDLE_struct
{
}              *MXS_HANDLE;

struct SMTPError
{
    int             iSTMPResponse;
    char           *pszSTMPResponse;
};

enum SmtpMsgInfo
{
    smsgiClientDomain = 0,
    smsgiClientIP,
    smsgiServerDomain,
    smsgiSeverIP,
    smsgiTime,
    smsgiSeverName,

    smsgiMax
};





char          **USmtpGetFwdGateways(SVRCFG_HANDLE hSvrConfig, const char *pszDomain);
int             USmtpGetGateway(SVRCFG_HANDLE hSvrConfig, const char *pszDomain,
                        char *pszGateway);
int             USmtpAddGateway(const char *pszDomain, const char *pszGateway);
int             USmtpRemoveGateway(const char *pszDomain);
int             USmtpGetSpoolFileInfo(char const * pszPkgFile, char *pszDomain, char *pszSmtpMessageID,
                        char *pszFrom, char *pszRcpt);
int             USmtpIsAllowedRelay(const SYS_INET_ADDR & PeerInfo,
                        SVRCFG_HANDLE hSvrConfig);
char          **USmtpGetPathStrings(const char *pszMailCmd);
int             USmtpSplitEmailAddr(const char *pszAddr, char *pszUser, char *pszDomain);
int             USmtpInitError(SMTPError * pSMTPE);
bool            USmtpIsFatalError(SMTPError const * pSMTPE);
char const     *USmtpGetErrorMessage(SMTPError const * pSMTPE);
int             USmtpCleanupError(SMTPError * pSMTPE);
BSOCK_HANDLE    USmtpCreateChannel(const char *pszServer, const char *pszDomain,
                        SMTPError * pSMTPE = NULL);
int             USmtpCloseChannel(BSOCK_HANDLE hBSock, int iHardClose = 0, SMTPError * pSMTPE = NULL);
int             USmtpChannelReset(BSOCK_HANDLE hBSock, SMTPError * pSMTPE = NULL);
int             USmtpSendMail(BSOCK_HANDLE hBSock, const char *pszFrom, const char *pszRcpt,
                        const char *pszFileName, SMTPError * pSMTPE = NULL);
int             USmtpSendMail(const char *pszServer, const char *pszDomain,
                        const char *pszFrom, const char *pszRcpt, const char *pszFileName,
                        SMTPError * pSMTPE = NULL);
char           *USmtpBuildRcptPath(char const * const * ppszRcptTo, SVRCFG_HANDLE hSvrConfig);
char          **USmtpGetMailExchangers(SVRCFG_HANDLE hSvrConfig, const char *pszDomain);
int             USmtpCheckMailDomain(SVRCFG_HANDLE hSvrConfig, char const * pszDomain);
MXS_HANDLE      USmtpGetMXFirst(SVRCFG_HANDLE hSvrConfig, const char *pszDomain,
                        char *pszMXHost);
int             USmtpGetMXNext(MXS_HANDLE hMXSHandle, char *pszMXHost);
void            USmtpMXSClose(MXS_HANDLE hMXSHandle);
int             USmtpRBLCheck(SYS_INET_ADDR const & PeerInfo);
int             USmtpRSSCheck(SYS_INET_ADDR const & PeerInfo);
int             USmtpORBSCheck(SYS_INET_ADDR const & PeerInfo);
bool            USmtpDnsMapsContained(SYS_INET_ADDR const & PeerInfo, char const * pszMapsServer);
int             USmtpSpammerCheck(const SYS_INET_ADDR & PeerInfo);
int             USmtpSpamAddressCheck(char const * pszAddress);
int             USmtpAddMessageInfo(FILE * pMsgFile, char const * pszClientDomain,
                        SYS_INET_ADDR const & PeerInfo, char const * pszServerDomain,
                        SYS_INET_ADDR const & SockInfo, char const * pszSmtpServerLogo);
char           *USmtpGetReceived(char const * const * ppszMsgInfo, char const * pszMailFrom,
                        char const * pszRcptTo);




#endif
