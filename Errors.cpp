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




struct ErrorInfo
{
    int             iErrorCode;
    char const     *pszError;
    char           *pszInfo;
};






static ErrorInfo *ErrGetErrorInfo(int iErrorCode);








static THRDLS int iErrorNo = 0;
static THRDLS ErrorInfo Errors[] =
{
    {ERR_SUCCESS, "Success", NULL},
    {ERR_SERVER_SHUTDOWN, "Server shutdown", NULL},
    {ERR_MEMORY, "Memory allocation error", NULL},
    {ERR_NETWORK, "Network kernel error", NULL},
    {ERR_SOCKET_CREATE, "Cannot create socket", NULL},
    {ERR_TIMEOUT, "Timeout error", NULL},
    {ERR_LOCKED, "Already locked", NULL},
    {ERR_SOCKET_BIND, "Socket bind error", NULL},
    {ERR_CONF_PATH, "Mail root path not found", NULL},
    {ERR_USERS_FILE_NOT_FOUND, "Users table file not found", NULL},
    {ERR_FILE_CREATE, "Unable to create file", NULL},
    {ERR_USER_NOT_FOUND, "User not found", NULL},
    {ERR_USER_EXIST, "User already exist", NULL},
    {ERR_WRITE_USERS_FILE, "Error writing users file", NULL},
    {ERR_NO_USER_PRFILE, "User profile file not found", NULL},
    {ERR_FILE_DELETE, "Unable to remove file", NULL},
    {ERR_DIR_CREATE, "Unable to create directory", NULL},
    {ERR_DIR_DELETE, "Unable to remove directory", NULL},
    {ERR_FILE_OPEN, "Unable to open file", NULL},
    {ERR_INVALID_FILE, "Invalid file structure", NULL},
    {ERR_FILE_WRITE, "Unable to write file", NULL},
    {ERR_MSG_NOT_IN_RANGE, "Message out of range", NULL},
    {ERR_MSG_DELETED, "Message deleted", NULL},
    {ERR_INVALID_PASSWORD, "Invalid password", NULL},
    {ERR_ALIAS_FILE_NOT_FOUND, "Users alias file not found", NULL},
    {ERR_ALIAS_EXIST, "Alias already exist", NULL},
    {ERR_WRITE_ALIAS_FILE, "Error writing alias file", NULL},
    {ERR_ALIAS_NOT_FOUND, "Alias not found", NULL},
    {ERR_SVR_PRFILE_NOT_LOCKED, "Server profile not locked", NULL},
    {ERR_GET_PEER_INFO, "Error getting peer address info", NULL},
    {ERR_SMTP_PATH_PARSE_ERROR, "Error parsing SMTP command line", NULL},
    {ERR_BAD_RETURN_PATH, "Bad return path syntax", NULL},
    {ERR_BAD_EMAIL_ADDR, "Bad email address", NULL},
    {ERR_RELAY_NOT_ALLOWED, "Relay not allowed", NULL},
    {ERR_BAD_FORWARD_PATH, "Bad forward path syntax", NULL},
    {ERR_GET_SOCK_INFO, "Error getting sock address info", NULL},
    {ERR_GET_SOCK_HOST, "Error getting sock host name", NULL},
    {ERR_NO_DOMAIN, "Unable to know server domain", NULL},
    {ERR_USER_NOT_LOCAL, "User not local", NULL},
    {ERR_BAD_SERVER_ADDR, "Invalid server address", NULL},
    {ERR_BAD_SERVER_RESPONSE, "Bad server response", NULL},
    {ERR_INVALID_POP3_RESPONSE, "Invalid POP3 response", NULL},
    {ERR_LINKS_FILE_NOT_FOUND, "POP3 links file not found", NULL},
    {ERR_LINK_EXIST, "POP3 link already exist", NULL},
    {ERR_WRITE_LINKS_FILE, "Error writing POP3 links file", NULL},
    {ERR_LINK_NOT_FOUND, "POP3 link not found", NULL},
    {ERR_NO_SMTP_SPOOL_FILES, "No SMTP spool files ready to process", NULL},
    {ERR_SMTPGW_FILE_NOT_FOUND, "SMTP gateway file not found", NULL},
    {ERR_GATEWAY_ALREADY_EXIST, "SMTP gateway already exist", NULL},
    {ERR_GATEWAY_NOT_FOUND, "SMTP gateway not found", NULL},
    {ERR_USER_NOT_MAILINGLIST, "User is not a mailing list", NULL},
    {ERR_NO_USER_MLTABLE_FILE, "Mailing list users table file not found", NULL},
    {ERR_MLUSER_ALREADY_EXIST, "Mailing list user already exist", NULL},
    {ERR_MLUSER_NOT_FOUND, "Mailing list user not found", NULL},
    {ERR_SPOOL_FILE_NOT_FOUND, "Spool file not found", NULL},
    {ERR_INVALID_SPOOL_FILE, "Invalid spool file", NULL},
    {ERR_SPOOL_FILE_NOT_READY, "Spool file not ready", NULL},
    {ERR_SPOOL_FILE_EXPIRED, "Spool file has reached max retry ops", NULL},
    {ERR_SMTPRELAY_FILE_NOT_FOUND, "SMTP relay file not found", NULL},
    {ERR_DOMAINS_FILE_NOT_FOUND, "POP3 domains file not found", NULL},
    {ERR_DOMAIN_NOT_HANDLED, "POP3 domain not handled", NULL},
    {ERR_BAD_SMTP_RESPONSE, "Bad SMTP response", NULL},
    {ERR_CFG_VAR_NOT_FOUND, "Config variabile not found", NULL},
    {ERR_BAD_DNS_RESPONSE, "Bad DNS response", NULL},
    {ERR_SMTPGW_NOT_FOUND, "SMTP gateway not found", NULL},
    {ERR_INCOMPLETE_CONFIG, "Incomplete server configuration file", NULL},
    {ERR_MAIL_ERROR_LOOP, "Mail error loop detected", NULL},
    {ERR_EXTALIAS_FILE_NOT_FOUND, "External-Alias file not found", NULL},
    {ERR_EXTALIAS_EXIST, "External alias already exist", NULL},
    {ERR_WRITE_EXTALIAS_FILE, "Error writing External-Alias file", NULL},
    {ERR_EXTALIAS_NOT_FOUND, "External alias not found", NULL},
    {ERR_NO_USER_DEFAULT_PRFILE, "Unable to open default user profile", NULL},
    {ERR_FINGER_QUERY_FORMAT, "Error in FINGER query", NULL},
    {ERR_LOCKED_RESOURCE, "Resource already locked", NULL},
    {ERR_NO_PREDEFINED_MX, "No predefined mail exchanger", NULL},
    {ERR_NO_MORE_MXRECORDS, "No more MX records", NULL},
    {ERR_INVALID_MESSAGE_FORMAT, "Invalid message format", NULL},
    {ERR_SMTP_BAD_MAIL_FROM, "[MAIL FROM:] not permitted by remote SMTP server", NULL},
    {ERR_SMTP_BAD_RCPT_TO, "[RCPT TO:] not permitted by remote SMTP server", NULL},
    {ERR_SMTP_BAD_DATA, "[DATA] not permitted by remote SMTP server", NULL},
    {ERR_INVALID_MXRECS_STRING, "Invalid MX records string format", NULL},
    {ERR_SETSOCKOPT, "Error in function {setsockopt}", NULL},
    {ERR_CREATEEVENT, "Error in function {CreateEvent}", NULL},
    {ERR_CREATESEMAPHORE, "Error in function {CreateSemaphore}", NULL},
    {ERR_CLOSEHANDLE, "Error in function {CloseHandle}", NULL},
    {ERR_RELEASESEMAPHORE, "Error in function {ReleaseSemaphore}", NULL},
    {ERR_BEGINTHREADEX, "Error in function {_beginthreadex}", NULL},
    {ERR_CREATEFILEMAPPING, "Error in function {CreateFileMapping}", NULL},
    {ERR_MAPVIEWOFFILE, "Error in function {MapViewOfFile}", NULL},
    {ERR_UNMAPVIEWOFFILE, "Error in function {UnmapViewOfFile}", NULL},
    {ERR_SEMGET, "Error in function {semget}", NULL},
    {ERR_SEMCTL, "Error in function {semctl}", NULL},
    {ERR_SEMOP, "Error in function {semop}", NULL},
    {ERR_FORK, "Error in function {fork}", NULL},
    {ERR_SHMGET, "Error in function {shmget}", NULL},
    {ERR_SHMCTL, "Error in function {shmctl}", NULL},
    {ERR_SHMAT, "Error in function {shmat}", NULL},
    {ERR_SHMDT, "Error in function {shmdt}", NULL},
    {ERR_OPENDIR, "Error in function {opendir}", NULL},
    {ERR_STAT, "Error in function {stat}", NULL},
    {ERR_SMTP_BAD_CMD_SEQUENCE, "Bad SMTP command sequence", NULL},
    {ERR_NO_ROOT_DOMAIN_VAR, "RootDomain config var not found", NULL},
    {ERR_NS_NOT_FOUND, "Name Server for domain not found", NULL},
    {ERR_NO_DEFINED_MXS_FOR_DOMAIN, "No MX records defined for domain", NULL},
    {ERR_BAD_CTRL_COMMAND, "Bad CTRL command syntax", NULL},
    {ERR_DOMAIN_ALREADY_HANDLED, "Domain already exist", NULL},
    {ERR_BAD_CTRL_LOGIN, "Bad controller login", NULL},
    {ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND, "Controller accounts file not found", NULL},
    {ERR_RBL_SPAMMER, "RBL registered spammer (rbl.maps.vix.com.)", NULL},
    {ERR_SPAMMER_IP, "Server registered spammer IP", NULL},
    {ERR_TRUNCATED_DGRAM_DNS_RESPONSE, "Truncated UDP DNS response", NULL},
    {ERR_NO_DGRAM_DNS_RESPONSE, "Unable to get UDP DNS response", NULL},
    {ERR_EMPTY_DNS_RESPONSE, "Empty DNS response", NULL},
    {ERR_BAD_SMARTDNSHOST_SYNTAX, "Bad SmartDNSHost config syntax", NULL},
    {ERR_MAILBOX_SIZE, "User maximum mailbox size reached", NULL},
    {ERR_DYNDNS_CONFIG, "Bad \"DynDnsSetup\" config syntax", NULL},
    {ERR_INVALID_SHARED_BLOCK, "Invalid shared block", NULL},
    {ERR_PROCESS_EXECUTE, "Error executing external process", NULL},
    {ERR_BAD_MAILPROC_CMD_SYNTAX, "Bad mailproc.tab command syntax", NULL},
    {ERR_NO_MAILPROC_FILE, "User mail processing file not present", NULL},
    {ERR_DNS_RECURSION_NOT_AVAILABLE, "DNS recursion not available", NULL},
    {ERR_POP3_EXTERNAL_LINK_DISABLED, "External POP3 link disabled", NULL},
    {ERR_RSS_SPAMMER, "RSS registered spammer (relays.mail-abuse.org.)", NULL},
    {ERR_BAD_DOMAIN_PROC_CMD_SYNTAX, "Error in custom domain processing file syntax", NULL},
    {ERR_NOT_A_CUSTOM_DOMAIN, "Not a custom domain", NULL},
    {ERR_NO_MORE_TOKENS, "No more tokens", NULL},
    {ERR_SELECT, "Error in function {select}", NULL},
    {ERR_REGISTER_EVENT_SOURCE, "Error in function {RegisterEventSource}", NULL},
    {ERR_NOMORESEMS, "No more semaphores available", NULL},
    {ERR_INVALID_SEMAPHORE, "Invalid semaphore", NULL},
    {ERR_SHMEM_ALREADY_EXIST, "Shared memory already exist", NULL},
    {ERR_SHMEM_NOT_EXIST, "Shared memory not exist", NULL},
    {ERR_SEM_ALREADY_EXIST, "Semaphore already exist", NULL},
    {ERR_SEM_NOT_EXIST, "Semaphore not exist", NULL},
    {ERR_SERVER_BUSY, "Server too busy, retry later", NULL},
    {ERR_IP_NOT_ALLOWED, "Server does not like Your IP", NULL},
    {ERR_FILE_EOF, "End of file reached", NULL},
    {ERR_BAD_TAG_ADDRESS, "Bad tag ( From: , etc ... ) address", NULL},
    {ERR_MAILFROM_UNKNOWN, "Unable to extract \"MAIL FROM: <>\" address", NULL},
    {ERR_FILTERED_MESSAGE, "Message rejected by server filters", NULL},
    {ERR_NO_DOMAIN_FILTER, "Domain filter not defined", NULL},
    {ERR_POP3_RETR_BROKEN, "POP3 RETR operation broken in data retrieval", NULL},
    {ERR_CCLN_INVALID_RESPONSE, "Invalid controller response", NULL},
    {ERR_CCLN_ERROR_RESPONSE, "Controller response error", NULL},
    {ERR_INCOMPLETE_PROCESSING, "Custom domain processing incomplete", NULL},
    {ERR_NO_EXTERNAL_AUTH_DEFINED, "No external auth defined for requested username@domain", NULL},
    {ERR_EXTERNAL_AUTH_FAILURE, "External authentication error", NULL},
    {ERR_MD5_AUTH_FAILED, "MD5 authentication failed", NULL},
    {ERR_NO_SMTP_AUTH_CONFIG, "SMTP authentication config not found", NULL},
    {ERR_UNKNOWN_SMTP_AUTH, "Unknown SMTP authentication mode", NULL},
    {ERR_BAD_SMTP_AUTH_CONFIG, "Bad SMTP authentication config", NULL},
    {ERR_BAD_EXTRNPRG_EXITCODE, "Bad external program exit code", NULL},
    {ERR_BAD_SMTP_CMD_SYNTAX, "Bad SMTP command syntax", NULL},
    {ERR_SMTP_AUTH_FAILED, "SMTP client authentication failed", NULL},
    {ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE, "Bad external SMTP auth response file syntax", NULL},
    {ERR_SMTP_USE_FORBIDDEN, "Server use is forbidden", NULL},
    {ERR_SPAM_ADDRESS, "Server registered spammer domain", NULL},
    {ERR_SOCK_NOMORE_DATA, "End of socket stream data", NULL},
    {ERR_BAD_TAB_INDEX_FIELD, "Bad TAB field index", NULL},
    {ERR_FILE_READ, "Error reading file", NULL},
    {ERR_BAD_INDEX_FILE, "Bad index file format", NULL},
    {ERR_INDEX_HASH_NOT_FOUND, "Record hash not found in index", NULL},
    {ERR_RECORD_NOT_FOUND, "Record not found in TAB file", NULL},
    {ERR_HEAP_ALLOC, "Heap block alloc error", NULL},
    {ERR_HEAP_FREE, "Heap block free error", NULL},
    {ERR_RESOURCE_NOT_LOCKED, "Trying to unlock an unlocked resource", NULL},
    {ERR_LOCK_ENTRY_NOT_FOUND, "Resource lock entry not found", NULL},
    {ERR_LINE_TOO_LONG, "Stream line too long", NULL},
    {ERR_MAIL_LOOP_DETECTED, "Mail loop detected", NULL},
    {ERR_FILE_MOVE, "Unable to move file", NULL},
    {ERR_INVALID_MAILDIR_SUBPATH, "Error invalid Maildir sub path", NULL},
    {ERR_SMTP_TOO_MANY_RECIPIENTS, "Too many SMTP recipients", NULL},
    {ERR_DNS_CACHE_FILE_FMT, "Error in DNS cache file format", NULL},
    {ERR_DNS_CACHE_FILE_EXPIRED, "DNS cache file expired", NULL},
    {ERR_MMAP, "Error in function {mmap}", NULL},
    {ERR_NOT_LOCKED, "Not locked", NULL},
    {ERR_SMTPFWD_FILE_NOT_FOUND, "SMTP forward gateway file not found", NULL},
    {ERR_SMTPFWD_NOT_FOUND, "SMTP forward gateway not found", NULL},
    {ERR_USER_BREAK, "Operation interrupted", NULL},
    {ERR_SET_THREAD_PRIORITY, "Error setting thread priority", NULL},
    {ERR_INVALID_QUEUE_PATH, "Invalid queue path", NULL},
    {ERR_QUEUE_ARRAY_FULL, "Queue array is full", NULL},
    {ERR_QUEUE_ENTRY_NOT_FOUND, "Queue entry not found", NULL},
    {ERR_NULL_SENDER, "Empty message sender", NULL},

};








static ErrorInfo *ErrGetErrorInfo(int iErrorCode)
{

    for (int ii = 0; ii < CountOf(Errors); ii++)
        if (Errors[ii].iErrorCode == iErrorCode)
            return (&Errors[ii]);

    return (NULL);

}




int             ErrGetErrorCode(void)
{

    return (iErrorNo);

}



void            ErrSetErrorCode(int iError, char const * pszInfo)
{

    iErrorNo = iError;

    if (pszInfo != NULL)
    {
        ErrorInfo      *pEI = ErrGetErrorInfo(iErrorNo);

        if (pEI != NULL)
        {
            if (pEI->pszInfo != NULL)
                SysFree(pEI->pszInfo);

            pEI->pszInfo = SysStrDup(pszInfo);
        }
    }

}



const char     *ErrGetErrorString(int iError)
{

    ErrorInfo      *pEI = ErrGetErrorInfo(iError);

    return ((pEI != NULL) ? pEI->pszError : "Unknown error code");

}



const char     *ErrGetErrorString(void)
{

    return (ErrGetErrorString(iErrorNo));

}



char           *ErrGetErrorStringInfo(int iError)
{

    ErrorInfo      *pEI = ErrGetErrorInfo(iError);

    if (pEI == NULL)
        return (SysStrDup("Unknown error code"));

    int             iInfoLength = (pEI->pszInfo != NULL) ? strlen(pEI->pszInfo) : 0;
    char           *pszErrorInfo = (char *) SysAlloc(strlen(pEI->pszError) + iInfoLength + 256);

    if (pszErrorInfo == NULL)
        return (NULL);

    if (pEI->pszInfo != NULL)
        sprintf(pszErrorInfo,
                "ErrCode   = %d\n"
                "ErrString = %s\n"
                "ErrInfo   = %s", pEI->iErrorCode, pEI->pszError, pEI->pszInfo);
    else
        sprintf(pszErrorInfo,
                "ErrCode   = %d\n"
                "ErrString = %s", pEI->iErrorCode, pEI->pszError);

    return (pszErrorInfo);

}




int             ErrLogMessage(int iLogLevel, char const * pszFormat,...)
{

    char           *pszErrorInfo = ErrGetErrorStringInfo(ErrGetErrorCode());

    if (pszErrorInfo == NULL)
        return (ErrGetErrorCode());


    va_list         Args;

    va_start(Args, pszFormat);

    char           *pszUserMessage = StrVSprint(pszFormat, Args);

    va_end(Args);


    if (pszUserMessage == NULL)
    {
        SysFree(pszErrorInfo);
        return (ErrGetErrorCode());
    }


    SysLogMessage(iLogLevel,
            "<<\n"
            "%s\n"
            "%s"
            ">>\n", pszErrorInfo, pszUserMessage);


    SysFree(pszUserMessage);
    SysFree(pszErrorInfo);

    return (0);

}




int             ErrFileVLogMessage(char const * pszFileName, char const * pszFormat,
                        va_list Args)
{

    char           *pszErrorInfo = ErrGetErrorStringInfo(ErrGetErrorCode());

    if (pszErrorInfo == NULL)
        return (ErrGetErrorCode());


    char           *pszUserMessage = StrVSprint(pszFormat, Args);


    if (pszUserMessage == NULL)
    {
        SysFree(pszErrorInfo);
        return (ErrGetErrorCode());
    }


    FILE           *pLogFile = fopen(pszFileName, "a+t");

    if (pLogFile == NULL)
    {
        SysFree(pszUserMessage);
        SysFree(pszErrorInfo);

        ErrSetErrorCode(ERR_FILE_CREATE, pszFileName);
        return (ERR_FILE_CREATE);
    }


    fprintf(pLogFile,
            "<<\n"
            "%s\n"
            "%s"
            ">>\n", pszErrorInfo, pszUserMessage);


    fclose(pLogFile);
    SysFree(pszUserMessage);
    SysFree(pszErrorInfo);

    return (0);

}




void            ErrCleanupErrorInfos(int iFreeInfos)
{

    for (int ii = 0; ii < CountOf(Errors); ii++)
        if (Errors[ii].pszInfo != NULL)
        {
            if (iFreeInfos)
                SysFree(Errors[ii].pszInfo);
            Errors[ii].pszInfo = NULL;
        }

}
