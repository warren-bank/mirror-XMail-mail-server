/*
 *  SendMail by Davide Libenzi ( sendmail replacement for XMail )
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

#if defined(WIN32)

#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <direct.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>




#define SYS_SLASH_CHAR              '\\'
#define SYS_SLASH_STR               "\\"
#define SYS_MAX_PATH                256
#define SysFileSync(fp)             do { fflush(fp); _commit(_fileno(fp)); } while (0)




int             SysPathExist(char const * pszPathName)
{

    return ((_access(pszPathName, 0) == 0) ? 1 : 0);

}

int             SysMakeDir(char const * pszPathName)
{

    return ((_mkdir(pszPathName) == 0) ? 1 : 0);

}

int             SysErrNo(void)
{

    return (errno);

}

char const     *SysErrStr(void)
{

    return (strerror(errno));

}

unsigned long   SysGetProcessId(void)
{

    return ((unsigned long) GetCurrentThreadId());

}

int             SysMoveFile(char const * pszOldName, char const * pszNewName)
{

    if (!MoveFileEx(pszOldName, pszNewName, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return (-1);

    return (0);

}

int             SysGetHostName(char * pszHostName, int iNameSize)
{

    DWORD           dwSize = (DWORD) iNameSize;

    GetComputerName(pszHostName, &dwSize);

    return (0);

}


#else           // #if defined(WIN32)
#if defined(__LINUX__) || defined(__SOLARIS__)

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>



#define SYS_SLASH_CHAR              '/'
#define SYS_SLASH_STR               "/"
#define SYS_MAX_PATH                256
#define SysFileSync(fp)             do { fflush(fp); fdatasync(fileno(fp)); } while (0)



int             SysPathExist(char const * pszPathName)
{

    return ((access(pszPathName, 0) == 0) ? 1 : 0);

}

int             SysMakeDir(char const * pszPathName)
{

    return ((mkdir(pszPathName, 0700) == 0) ? 1 : 0);

}

int             SysErrNo(void)
{

    return (errno);

}

char const     *SysErrStr(void)
{

    return (strerror(errno));

}

unsigned long   SysGetProcessId(void)
{

    return ((unsigned long) getpid());

}

int             SysMoveFile(char const * pszOldName, char const * pszNewName)
{

    if (rename(pszOldName, pszNewName) != 0)
        return (-1);

    return (0);

}

int             SysGetHostName(char * pszHostName, int iNameSize)
{

    gethostname(pszHostName, iNameSize);

    return (0);

}


#else           // #if defined(__LINUX__) || defined(__SOLARIS__)

#error system type not defined !

#endif          // #if defined(__LINUX__) || defined(__SOLARIS__)
#endif          // #if defined(WIN32)




#define ENV_MAIL_ROOT           "MAIL_ROOT"
#define LOCAL_TEMP_SUBPATH      "spool" SYS_SLASH_STR "temp" SYS_SLASH_STR
#define LOCAL_SUBPATH           "spool" SYS_SLASH_STR "local" SYS_SLASH_STR











int             main(int iArgCount, char *pszArgs[])
{
///////////////////////////////////////////////////////////////////////////////
//  Get the mail root path
///////////////////////////////////////////////////////////////////////////////
    int             iVarLength = 0;
    char const     *pszMailRoot = getenv(ENV_MAIL_ROOT);
    char            szMailRoot[SYS_MAX_PATH] = "";

    if ((pszMailRoot == NULL) || ((iVarLength = strlen(pszMailRoot)) == 0))
    {
        fprintf(stderr, "cannot find environment variable: %s\n", ENV_MAIL_ROOT);
        return (1);
    }

    strcpy(szMailRoot, pszMailRoot);

    if (szMailRoot[iVarLength - 1] != SYS_SLASH_CHAR)
        strcat(szMailRoot, SYS_SLASH_STR);

///////////////////////////////////////////////////////////////////////////////
//  Parse command line
///////////////////////////////////////////////////////////////////////////////
    int             ii;
    char            szMailFrom[256] = "";

    for (ii = 1; ii < iArgCount; ii++)
    {
        if (pszArgs[ii][0] != '-')
            break;

        if ((strcmp(pszArgs[ii], "-N") == 0) || (strcmp(pszArgs[ii], "-O") == 0) ||
                (strcmp(pszArgs[ii], "-o") == 0) || (strcmp(pszArgs[ii], "-R") == 0) ||
                (strcmp(pszArgs[ii], "-V") == 0) || (strcmp(pszArgs[ii], "-X") == 0))
        {
            ++ii;

            continue;
        }

        if (strncmp(pszArgs[ii], "-f", 2) == 0)
        {
            strcpy(szMailFrom, pszArgs[ii] + 2);

            continue;
        }

        if (strncmp(pszArgs[ii], "-F", 2) == 0)
        {
            char const     *pszOpen = strchr(pszArgs[ii], '<');

            if (pszOpen == NULL)
                strcpy(szMailFrom, pszArgs[ii] + 2);
            else
            {
                strcpy(szMailFrom, pszOpen + 1);

                char           *pszClose = (char *) strchr(szMailFrom, '>');

                if (pszClose != NULL)
                    *pszClose = '\0';
            }

            continue;
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Check if recipients are supplied
///////////////////////////////////////////////////////////////////////////////
    if (ii == iArgCount)
    {
        fprintf(stderr, "empty recipient list\n");
        return (2);
    }

///////////////////////////////////////////////////////////////////////////////
//  Create file name
///////////////////////////////////////////////////////////////////////////////
    char            szHostName[256] = "",
                    szMailFile[SYS_MAX_PATH] = "";

    SysGetHostName(szHostName, sizeof(szHostName) - 1);

    sprintf(szMailFile, "%s%s%lu.%lu.%s",
            szMailRoot,
            LOCAL_TEMP_SUBPATH,
            (unsigned long) time(NULL) * 1000,
            SysGetProcessId(),
            szHostName);


    FILE           *pMailFile = fopen(szMailFile, "wb");

    if (pMailFile == NULL)
    {
        perror(szMailFile);
        return (3);
    }

///////////////////////////////////////////////////////////////////////////////
//  Emit sender
///////////////////////////////////////////////////////////////////////////////
    fprintf(pMailFile, "mail from:<%s>\r\n", szMailFrom);

///////////////////////////////////////////////////////////////////////////////
//  Emit recipients
///////////////////////////////////////////////////////////////////////////////
    for (; ii < iArgCount; ii++)
        fprintf(pMailFile, "rcpt to:<%s>\r\n", pszArgs[ii]);

///////////////////////////////////////////////////////////////////////////////
//  Emit empty line separator
///////////////////////////////////////////////////////////////////////////////
    fprintf(pMailFile, "\r\n");


    char            szBuffer[1024] = "";

    while (fgets(szBuffer, sizeof(szBuffer) - 1, stdin) != NULL)
    {
        int             iLineLength = strlen(szBuffer);

        for (;(iLineLength > 0) &&
                ((szBuffer[iLineLength - 1] == '\r') || (szBuffer[iLineLength - 1] == '\n')); iLineLength--);

        szBuffer[iLineLength] = '\0';

///////////////////////////////////////////////////////////////////////////////
//  Emit mail line
///////////////////////////////////////////////////////////////////////////////
        fprintf(pMailFile, "%s\r\n", szBuffer);

    }

///////////////////////////////////////////////////////////////////////////////
//  Sync and close the mail file
///////////////////////////////////////////////////////////////////////////////
    SysFileSync(pMailFile);
    fclose(pMailFile);

///////////////////////////////////////////////////////////////////////////////
//  Move the mail file
///////////////////////////////////////////////////////////////////////////////
    char            szDropFile[SYS_MAX_PATH] = "";

    sprintf(szDropFile, "%s%s%lu.%lu.%s",
            szMailRoot,
            LOCAL_SUBPATH,
            (unsigned long) time(NULL),
            SysGetProcessId(),
            szHostName);

    if (SysMoveFile(szMailFile, szDropFile) < 0)
    {
        remove(szMailFile);
        fprintf(stderr, "cannot move file: %s\n", szMailFile);
        return (4);
    }


    return (0);

}
