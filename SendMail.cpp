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
#include "AppDefines.h"




#define SYS_SLASH_CHAR              '\\'
#define SYS_SLASH_STR               "\\"
#define SYS_MAX_PATH                256




int             SysFileSync(FILE *pFile)
{

    if (fflush(pFile) || _commit(_fileno(pFile)))
        return (-1);

    return (0);

}

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

void            SysMsSleep(int iMsTimeout)
{

    Sleep(iMsTimeout);

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

            return (strdup(szKeyValue));
        }

        RegCloseKey(hKey);
    }

    const char     *pszValue = getenv(pszVarName);

    return ((pszValue != NULL) ? strdup(pszValue) : NULL);

}

#else           // #if defined(WIN32)
#if defined(__LINUX__) || defined(__SOLARIS__) || defined(__FREEBSD__)

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

#define stricmp                     strcasecmp
#define strnicmp                    strncasecmp




int             SysFileSync(FILE *pFile)
{

    if (fflush(pFile) || fsync(fileno(pFile)))
        return (-1);

    return (0);

}

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

void            SysMsSleep(int iMsTimeout)
{

    usleep(iMsTimeout * 1000);

}

char           *SysGetEnv(const char *pszVarName)
{

    const char     *pszValue = getenv(pszVarName);

    return ((pszValue != NULL) ? strdup(pszValue) : NULL);

}


#else           // #if defined(__LINUX__) || defined(__SOLARIS__)

#error system type not defined !

#endif          // #if defined(__LINUX__) || defined(__SOLARIS__)
#endif          // #if defined(WIN32)




#define ENV_MAIL_ROOT           "MAIL_ROOT"
#define LOCAL_TEMP_SUBPATH      "spool" SYS_SLASH_STR "temp" SYS_SLASH_STR
#define LOCAL_SUBPATH           "spool" SYS_SLASH_STR "local" SYS_SLASH_STR
#define MAIL_DATA_TAG           "<<MAIL-DATA>>"
#define MAX_ADDR_NAME           256
#define SAPE_OPEN_TENTATIVES    5
#define SAPE_OPEN_DELAY         500

#define SetEmptyString(s)       (s)[0] = '\0'
#define IsEmptyString(s)        (*(s) == '\0')
#define StrNCpy(t, s, n)        do { strncpy(t, s, n); (t)[(n) - 1] = '\0'; } while (0)
#define StrSNCpy(t, s)          StrNCpy(t, s, sizeof(t))



static FILE    *SafeOpenFile(char const * pszFilePath, char const * pszMode)
{

    FILE           *pFile;

    for (int ii = 0; ii < SAPE_OPEN_TENTATIVES; ii++)
    {
        if ((pFile = fopen(pszFilePath, pszMode)) != NULL)
            return (pFile);

        SysMsSleep(SAPE_OPEN_DELAY);
    }

    return (NULL);

}



static char const *AddressFromAtPtr(char const * pszAt, char const * pszBase,
                        char *pszAddress)
{

    char const     *pszStart = pszAt;

    for (; (pszStart >= pszBase) && (strchr("<> \t,\":;'\r\n", *pszStart) == NULL); pszStart--);

    ++pszStart;

    char const     *pszEnd = pszAt + 1;

    for (; (*pszEnd != '\0') && (strchr("<> \t,\":;'\r\n", *pszEnd) == NULL); pszEnd++);

    int             iAddrLength = (int) (pszEnd - pszStart);

    strncpy(pszAddress, pszStart, iAddrLength);
    pszAddress[iAddrLength] = '\0';

    return (pszEnd);

}



static int      EmitRecipients(FILE * pMailFile, char const * pszAddrList)
{

    int             iRcptCount = 0;
    char const     *pszCurr = pszAddrList;

    for (; (pszCurr != NULL) && (*pszCurr != '\0');)
    {
        char const     *pszAt = strchr(pszCurr, '@');

        if (pszAt == NULL)
            break;


        char            szAddress[256] = "";

        if ((pszCurr = AddressFromAtPtr(pszAt, pszAddrList, szAddress)) != NULL)
        {
            fprintf(pMailFile, "rcpt to:<%s>\r\n", szAddress);

            ++iRcptCount;
        }

    }


    return (iRcptCount);

}



int             main(int iArgCount, char *pszArgs[])
{
///////////////////////////////////////////////////////////////////////////////
//  Get the mail root path
///////////////////////////////////////////////////////////////////////////////
    int             iVarLength = 0;
    FILE           *pInFile = stdin;
    char           *pszMailRoot = SysGetEnv(ENV_MAIL_ROOT);
    char            szMailRoot[SYS_MAX_PATH] = "";

    if ((pszMailRoot == NULL) || ((iVarLength = strlen(pszMailRoot)) == 0))
    {
        if (pszMailRoot != NULL)
            free(pszMailRoot);
        fprintf(stderr, "cannot find environment variable: %s\n", ENV_MAIL_ROOT);
        return (1);
    }

    strcpy(szMailRoot, pszMailRoot);

    if (szMailRoot[iVarLength - 1] != SYS_SLASH_CHAR)
        strcat(szMailRoot, SYS_SLASH_STR);

    free(pszMailRoot);

///////////////////////////////////////////////////////////////////////////////
//  Parse command line
///////////////////////////////////////////////////////////////////////////////
    int             ii;
    bool            bExtractRcpts = false,
                    bXMailFormat = false,
                    bDotMode = true;
    char            szMailFrom[256] = "",
                    szExtMailFrom[256] = "",
                    szInputFile[SYS_MAX_PATH] = "",
                    szRcptFile[SYS_MAX_PATH] = "";

    for (ii = 1; ii < iArgCount; ii++)
    {
        if (pszArgs[ii][0] != '-')
            break;

        if (strcmp(pszArgs[ii], "--") == 0)
        {
            ++ii;
            break;
        }

        if (pszArgs[ii][1] != '-')
        {
            int             iSkipParam = 0;
            bool            bEatAll = false;

            for (int jj = 1; !bEatAll && (pszArgs[ii][jj] != '\0'); jj++)
            {
                switch (pszArgs[ii][jj])
                {
                    case ('N'):
                    case ('O'):
                    case ('o'):
                    case ('R'):
                    case ('V'):
                    case ('X'):
                        iSkipParam = 1;
                        break;

                    case ('i'):
                        bDotMode = false;
                        break;

                    case ('t'):
                        bExtractRcpts = true;
                        break;

                    case ('f'):
                        StrSNCpy(szMailFrom, pszArgs[ii] + jj + 1);

                        bEatAll = true;
                        break;

                    case ('F'):
                        {
                            StrSNCpy(szExtMailFrom, pszArgs[ii] + jj + 1);

                            char const     *pszOpen = strchr(pszArgs[ii] + jj + 1, '<');

                            if (pszOpen == NULL)
                                StrSNCpy(szMailFrom, pszArgs[ii] + jj + 1);
                            else
                            {
                                StrSNCpy(szMailFrom, pszOpen + 1);

                                char           *pszClose = (char *) strchr(szMailFrom, '>');

                                if (pszClose != NULL)
                                    *pszClose = '\0';
                            }

                            bEatAll = true;
                        }
                        break;
                }
            }

            if (iSkipParam)
                ++ii;
        }
        else
        {
            if (strcmp(pszArgs[ii], "--rcpt-file") == 0)
            {
                if (++ii < iArgCount)
                    StrSNCpy(szRcptFile, pszArgs[ii]);
            }
            else if (strcmp(pszArgs[ii], "--xinput-file") == 0)
            {
                if (++ii < iArgCount)
                {
                    StrSNCpy(szInputFile, pszArgs[ii]);

                    bXMailFormat = true;
                }
            }
            else if (strcmp(pszArgs[ii], "--input-file") == 0)
            {
                if (++ii < iArgCount)
                    StrSNCpy(szInputFile, pszArgs[ii]);
            }
        }

    }

///////////////////////////////////////////////////////////////////////////////
//  Check if recipients are supplied
///////////////////////////////////////////////////////////////////////////////
    if (!bExtractRcpts && (ii >= iArgCount) && IsEmptyString(szRcptFile))
    {
        fprintf(stderr, "empty recipient list\n");
        return (2);
    }

    if (!IsEmptyString(szInputFile))
    {
        if ((pInFile = fopen(szInputFile, "rb")) == NULL)
        {
            perror(szInputFile);
            return (3);
        }
    }

///////////////////////////////////////////////////////////////////////////////
//  Save recipients index
///////////////////////////////////////////////////////////////////////////////
    int             iRcptIndex = ii,
                    iRcptCount = iArgCount - iRcptIndex;

///////////////////////////////////////////////////////////////////////////////
//  Create file name
///////////////////////////////////////////////////////////////////////////////
    char            szHostName[256] = "",
                    szDataFile[SYS_MAX_PATH] = "",
                    szMailFile[SYS_MAX_PATH] = "";

    SysGetHostName(szHostName, sizeof(szHostName) - 1);

    sprintf(szDataFile, "%s%s%lu000.%lu.%s",
            szMailRoot,
            LOCAL_TEMP_SUBPATH,
            (unsigned long) time(NULL),
            SysGetProcessId(),
            szHostName);

    sprintf(szMailFile, "%s.mail", szDataFile);

///////////////////////////////////////////////////////////////////////////////
//  Open raw data file
///////////////////////////////////////////////////////////////////////////////
    FILE           *pDataFile = fopen(szDataFile, "w+b");

    if (pDataFile == NULL)
    {
        perror(szDataFile);
        if (pInFile != stdin) fclose(pInFile);
        return (4);
    }

///////////////////////////////////////////////////////////////////////////////
//  Open maildrop file
///////////////////////////////////////////////////////////////////////////////
    FILE           *pMailFile = fopen(szMailFile, "wb");

    if (pMailFile == NULL)
    {
        perror(szMailFile);
        fclose(pDataFile), remove(szDataFile);
        if (pInFile != stdin) fclose(pInFile);
        return (5);
    }

///////////////////////////////////////////////////////////////////////////////
//  Emit sender
///////////////////////////////////////////////////////////////////////////////
    fprintf(pMailFile, "mail from:<%s>\r\n", szMailFrom);

///////////////////////////////////////////////////////////////////////////////
//  Emit recipients
///////////////////////////////////////////////////////////////////////////////
    for (ii = iRcptIndex; ii < iArgCount; ii++)
        fprintf(pMailFile, "rcpt to:<%s>\r\n", pszArgs[ii]);

///////////////////////////////////////////////////////////////////////////////
//  Emit message by reading from stdin
///////////////////////////////////////////////////////////////////////////////
    bool            bInHeaders = true,
                    bHasFrom = false,
                    bRcptSource = false;
    char            szBuffer[1536] = "";

    while (fgets(szBuffer, sizeof(szBuffer) - 1, pInFile) != NULL)
    {
        int             iLineLength = strlen(szBuffer);

        for (;(iLineLength > 0) &&
                ((szBuffer[iLineLength - 1] == '\r') || (szBuffer[iLineLength - 1] == '\n')); iLineLength--);

        szBuffer[iLineLength] = '\0';

///////////////////////////////////////////////////////////////////////////////
//  Is it time to stop reading ?
///////////////////////////////////////////////////////////////////////////////
        if (bDotMode && (strcmp(szBuffer, ".") == 0))
            break;

///////////////////////////////////////////////////////////////////////////////
//  Decode XMail spool file format
///////////////////////////////////////////////////////////////////////////////
        if (bXMailFormat)
        {
            if (strcmp(szBuffer, MAIL_DATA_TAG) == 0)
                bXMailFormat = false;

            continue;
        }

///////////////////////////////////////////////////////////////////////////////
//  Extract mail from
///////////////////////////////////////////////////////////////////////////////
        if (bInHeaders)
        {
            if (iLineLength == 0)
            {
                bInHeaders = false;

                if (!bHasFrom)
                {
///////////////////////////////////////////////////////////////////////////////
//  Add mail from ( if not present )
///////////////////////////////////////////////////////////////////////////////
                    if (strlen(szExtMailFrom) != 0)
                        fprintf(pDataFile, "From: %s\r\n", szExtMailFrom);
                    else
                        fprintf(pDataFile, "From: <%s>\r\n", szMailFrom);
                }
            }


            if ((szBuffer[0] == ' ') || (szBuffer[0] == '\t'))
            {
                if (bExtractRcpts && bRcptSource)
                {
                    int             iRcptCurr = EmitRecipients(pMailFile, szBuffer);

                    if (iRcptCurr > 0)
                        iRcptCount += iRcptCurr;
                }
            }
            else
            {
                bRcptSource = (strnicmp(szBuffer, "To:", 3) == 0) ||
                        (strnicmp(szBuffer, "Cc:", 3) == 0) ||
                        (strnicmp(szBuffer, "Bcc:", 4) == 0);

                if (bExtractRcpts && bRcptSource)
                {
                    int             iRcptCurr = EmitRecipients(pMailFile, szBuffer);

                    if (iRcptCurr > 0)
                        iRcptCount += iRcptCurr;
                }

                if (!bHasFrom && (strnicmp(szBuffer, "From:", 5) == 0))
                    bHasFrom = true;
            }
        }

///////////////////////////////////////////////////////////////////////////////
//  Emit mail line
///////////////////////////////////////////////////////////////////////////////
        fprintf(pDataFile, "%s\r\n", szBuffer);

    }

///////////////////////////////////////////////////////////////////////////////
//  Close input file if different from stdin
///////////////////////////////////////////////////////////////////////////////
    if (pInFile != stdin) fclose(pInFile);

///////////////////////////////////////////////////////////////////////////////
//  Dump recipient file
///////////////////////////////////////////////////////////////////////////////
    if (!IsEmptyString(szRcptFile))
    {
        FILE           *pRcptFile = SafeOpenFile(szRcptFile, "rb");

        if (pRcptFile == NULL)
        {
            perror(szRcptFile);
            fclose(pDataFile), remove(szDataFile);
            fclose(pMailFile), remove(szMailFile);
            return (6);
        }

        while (fgets(szBuffer, sizeof(szBuffer) - 1, pRcptFile) != NULL)
        {
            int             iLineLength = strlen(szBuffer);

            for (;(iLineLength > 0) &&
                    ((szBuffer[iLineLength - 1] == '\r') || (szBuffer[iLineLength - 1] == '\n')); iLineLength--);

            szBuffer[iLineLength] = '\0';

            if (iLineLength >= MAX_ADDR_NAME)
                continue;

            char           *pszAt = strchr(szBuffer, '@');

            if (pszAt == NULL)
                continue;

            char            szRecipient[MAX_ADDR_NAME] = "";

            if (AddressFromAtPtr(pszAt, szBuffer, szRecipient) != NULL)
            {
                fprintf(pMailFile, "rcpt to:<%s>\r\n", szRecipient);

                ++iRcptCount;
            }
        }

        fclose(pRcptFile);
    }

///////////////////////////////////////////////////////////////////////////////
//  Check the number of recipients
///////////////////////////////////////////////////////////////////////////////
    if (iRcptCount == 0)
    {
        fprintf(stderr, "empty recipient list\n");
        fclose(pDataFile), remove(szDataFile);
        fclose(pMailFile), remove(szMailFile);
        return (7);
    }

///////////////////////////////////////////////////////////////////////////////
//  Empty line separator between maildrop header and data
///////////////////////////////////////////////////////////////////////////////
    fprintf(pMailFile, "\r\n");

///////////////////////////////////////////////////////////////////////////////
//  Append data file
///////////////////////////////////////////////////////////////////////////////
    rewind(pDataFile);

    unsigned int    uReaded;

    do
    {
        if (((uReaded = fread(szBuffer, 1, sizeof(szBuffer), pDataFile)) != 0) &&
                (fwrite(szBuffer, 1, uReaded, pMailFile) != uReaded))
        {
            perror(szMailFile);
            fclose(pDataFile), remove(szDataFile);
            fclose(pMailFile), remove(szMailFile);
            return (8);
        }

    } while (uReaded == sizeof(szBuffer));

    fclose(pDataFile), remove(szDataFile);

///////////////////////////////////////////////////////////////////////////////
//  Sync and close the mail file
///////////////////////////////////////////////////////////////////////////////
    if ((SysFileSync(pMailFile) < 0) || fclose(pMailFile))
    {
        remove(szMailFile);
        fprintf(stderr, "cannot write file: %s\n", szMailFile);
        return (9);
    }

///////////////////////////////////////////////////////////////////////////////
//  Move the mail file
///////////////////////////////////////////////////////////////////////////////
    char            szDropFile[SYS_MAX_PATH] = "";

    sprintf(szDropFile, "%s%s%lu000.%lu.%s",
            szMailRoot,
            LOCAL_SUBPATH,
            (unsigned long) time(NULL),
            SysGetProcessId(),
            szHostName);

    if (SysMoveFile(szMailFile, szDropFile) < 0)
    {
        remove(szMailFile);
        fprintf(stderr, "cannot move file: %s\n", szMailFile);
        return (10);
    }


    return (0);

}
