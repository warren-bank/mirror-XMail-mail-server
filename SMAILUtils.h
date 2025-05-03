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


#ifndef _SMAILUTILS_H
#define _SMAILUTILS_H





#define MAX_SPOOL_LINE                  1537

#define MAIL_FROM_STR                   "MAIL FROM:"
#define RCPT_TO_STR                     "RCPT TO:"
#define SPOOL_FILE_DATA_START           "<<MAIL-DATA>>"

#define INVALID_SPLF_HANDLE             ((SPLF_HANDLE) 0)

#define LMPCF_LOG_ENABLED               (1 << 0)







struct SpoolFileHeader
{
    char            szSpoolFile[SYS_MAX_PATH];
    char            szSMTPDomain[MAX_ADDR_NAME];
    char            szMessageID[128];
    char          **ppszFrom;
    char          **ppszRcpt;
};

typedef struct SPLF_HANDLE_struct
{
}              *SPLF_HANDLE;

struct LocalMailProcConfig
{
    unsigned long   ulFlags;

};







int             USmlLoadSpoolFileHeader(char const * pszSpoolFile, SpoolFileHeader & SFH);
void            USmlCleanupSpoolFileHeader(SpoolFileHeader & SFH);
char           *USmlAddrConcat(char const * const * ppszStrings);
char           *USmlBuildSendMailFrom(char const * const * ppszFrom, char const * const * ppszRcpt);
char           *USmlBuildSendRcptTo(char const * const * ppszFrom, char const * const * ppszRcpt);
SPLF_HANDLE     USmlCreateHandle(const char *pszMessFilePath);
void            USmlCloseHandle(SPLF_HANDLE hFSpool);
char const     *USmlGetRelayDomain(SPLF_HANDLE hFSpool);
char const     *USmlGetSpoolFilePath(SPLF_HANDLE hFSpool);
char const     *USmlGetSpoolFile(SPLF_HANDLE hFSpool);
char const     *USmlGetSMTPDomain(SPLF_HANDLE hFSpool);
char const     *USmlGetSmtpMessageID(SPLF_HANDLE hFSpool);
char const     *const * USmlGetMailFrom(SPLF_HANDLE hFSpool);
char const     *USmlMailFrom(SPLF_HANDLE hFSpool);
char const     *USmlSendMailFrom(SPLF_HANDLE hFSpool);
char const     *const * USmlGetRcptTo(SPLF_HANDLE hFSpool);
char const     *USmlRcptTo(SPLF_HANDLE hFSpool);
char const     *USmlSendRcptTo(SPLF_HANDLE hFSpool);
int             USmlSyncChanges(SPLF_HANDLE hFSpool);
int             USmlGetMsgFileSection(SPLF_HANDLE hFSpool, FileSection & FS);
int             USmlWriteMailFile(SPLF_HANDLE hFSpool, FILE * pMsgFile);
char           *USmlGetTag(SPLF_HANDLE hFSpool, char const * pszTagName);
int             USmlAddTag(SPLF_HANDLE hFSpool, char const * pszTagName,
                        char const * pszTagData, int iUpdate = 0);
int             USmlSetTagAddress(SPLF_HANDLE hFSpool, char const * pszTagName,
                        char const * pszAddress);
int             USmlMapAddress(char const * pszAddress, char *pszDomain, char *pszName);
int             USmlCreateMBFile(UserInfo * pUI, char const * pszFileName,
                        SPLF_HANDLE hFSpool);
int             USmlCreateSpoolFile(SPLF_HANDLE hFSpool, char const * pszFromUser,
                        char const * pszRcptUser, char const * pszFileName);
int             USmlProcessLocalUserMessage(UserInfo * pUI, SPLF_HANDLE hFSpool,
                        QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage, LocalMailProcConfig & LMPC);
int             USmlGetDomainCustomDir(char *pszCustomDir, int iFinalSlash = 1);
int             USmlDomainCustomFileName(char const * pszDestDomain, char *pszCustFilePath);
int             USmlGetDomainCustomFile(char const * pszDestDomain, char *pszCustFilePath);
int             USmlGetDomainCustomSpoolFile(QUEUE_HANDLE hQueue, QMSG_HANDLE hMessage,
                        char *pszCustFilePath);
int             USmlGetDomainMsgCustomFile(SPLF_HANDLE hFSpool, QUEUE_HANDLE hQueue,
                        QMSG_HANDLE hMessage, char const * pszDestDomain, char *pszCustFilePath);
int             USmlGetCustomDomainFile(char const * pszDestDomain, char const * pszCustFilePath);
int             USmlSetCustomDomainFile(char const * pszDestDomain, char const * pszCustFilePath);
int             USmlGetMessageFilterFile(char const * pszDomain, char const * pszUser,
                        char *pszFilterFilePath);
int             USmlCustomizedDomain(char const * pszDestDomain);
int             USmlLogMessage(SPLF_HANDLE hFSpool, char const * pszMedium, char const * pszParam);
int             USmlParseAddress(char const * pszAddress, char *pszPreAddr,
                        int iMaxPreAddress, char *pszEmailAddr, int iMaxAddress);
int             USmlDeliverFetchedMsg(char const *pszSyncAddr, char const * pszMailFile);
int             USmlMailLoopCheck(SPLF_HANDLE hFSpool, SVRCFG_HANDLE hSvrConfig);




#endif
