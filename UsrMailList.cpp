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
#include "ShBlocks.h"
#include "ResLocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MailConfig.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"
#include "POP3GwLink.h"
#include "UsrUtils.h"
#include "UsrMailList.h"






#define MLU_TABLE_LINE_MAX          512






struct MLUsersScanData
{
    char            szTmpDBFile[SYS_MAX_PATH];
    FILE           *pDBFile;
    char            szCurrUser[256];
};






static int      UsrMLWriteUser(FILE * pMLUFile, const char *pszMLUser);










int             UsrMLCheckUserPost(UserInfo * pUI, char const * pszUser)
{

    char           *pszClosed = UsrGetUserInfoVar(pUI, "ClosedML");

    if (pszClosed != NULL)
    {
        int             iClosedML = atoi(pszClosed);

        SysFree(pszClosed);

        if (iClosedML)
        {
            USRML_HANDLE    hUsersDB = UsrMLOpenDB(pUI);

            if (hUsersDB == INVALID_USRML_HANDLE)
                return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Mailing list scan
///////////////////////////////////////////////////////////////////////////////
            char const     *pszMLUser = UsrMLGetFirstUser(hUsersDB);

            for (; pszMLUser != NULL; pszMLUser = UsrMLGetNextUser(hUsersDB))
            {
                if (stricmp(pszUser, pszMLUser) == 0)
                {
                    UsrMLCloseDB(hUsersDB);

                    return (0);
                }
            }

            UsrMLCloseDB(hUsersDB);

            ErrSetErrorCode(ERR_MLUSER_NOT_FOUND, pszUser);
            return (ERR_MLUSER_NOT_FOUND);
        }
    }

    return (0);

}




static int      UsrMLWriteUser(FILE * pMLUFile, const char *pszMLUser)
{

///////////////////////////////////////////////////////////////////////////////
//  User Email
///////////////////////////////////////////////////////////////////////////////
    char           *pszQuoted = StrQuote(pszMLUser, '"');

    if (pszQuoted == NULL)
        return (ErrGetErrorCode());

    fprintf(pMLUFile, "%s\n", pszQuoted);

    SysFree(pszQuoted);

    return (0);

}



int             UsrMLAddUser(UserInfo * pUI, const char *pszMLUser)
{

    if (UsrGetUserType(pUI) != usrTypeML)
    {
        ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
        return (ERR_USER_NOT_MAILINGLIST);
    }

    char            szMLTablePath[SYS_MAX_PATH] = "";

    UsrGetMLTableFilePath(pUI, szMLTablePath);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szMLTablePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pMLUFile = fopen(szMLTablePath, "r+t");

    if (pMLUFile == NULL)
    {
        RLckUnlockEX(hResLock);
        ErrSetErrorCode(ERR_NO_USER_MLTABLE_FILE);
        return (ERR_NO_USER_MLTABLE_FILE);
    }


    char            szMLULine[MLU_TABLE_LINE_MAX] = "";

    while (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szMLULine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= 1) && (stricmp(ppszStrings[0], pszMLUser) == 0))
        {
            StrFreeStrings(ppszStrings);
            fclose(pMLUFile);
            RLckUnlockEX(hResLock);

            ErrSetErrorCode(ERR_MLUSER_ALREADY_EXIST);
            return (ERR_MLUSER_ALREADY_EXIST);
        }

        StrFreeStrings(ppszStrings);
    }


    fseek(pMLUFile, 0, SEEK_END);


    if (UsrMLWriteUser(pMLUFile, pszMLUser) < 0)
    {
        fclose(pMLUFile);
        RLckUnlockEX(hResLock);
        return (ErrGetErrorCode());
    }


    fclose(pMLUFile);

    RLckUnlockEX(hResLock);

    return (0);

}



int             UsrMLRemoveUser(UserInfo * pUI, const char *pszMLUser)
{

    if (UsrGetUserType(pUI) != usrTypeML)
    {
        ErrSetErrorCode(ERR_USER_NOT_MAILINGLIST);
        return (ERR_USER_NOT_MAILINGLIST);
    }

    char            szMLTablePath[SYS_MAX_PATH] = "";

    UsrGetMLTableFilePath(pUI, szMLTablePath);


    char            szTmpFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szTmpFile);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockEX(CfgGetBasedPath(szMLTablePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    FILE           *pMLUFile = fopen(szMLTablePath, "rt");

    if (pMLUFile == NULL)
    {
        RLckUnlockEX(hResLock);
        ErrSetErrorCode(ERR_NO_USER_MLTABLE_FILE);
        return (ERR_NO_USER_MLTABLE_FILE);
    }

    FILE           *pTmpFile = fopen(szTmpFile, "wt");

    if (pTmpFile == NULL)
    {
        fclose(pMLUFile);
        RLckUnlockEX(hResLock);
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    int             iMLUserFound = 0;
    char            szMLULine[MLU_TABLE_LINE_MAX] = "";

    while (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUFile) != NULL)
    {
        char          **ppszStrings = StrGetTabLineStrings(szMLULine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if ((iFieldsCount >= 1) && (stricmp(ppszStrings[0], pszMLUser) == 0))
        {

            ++iMLUserFound;

        }
        else
            fprintf(pTmpFile, "%s\n", szMLULine);

        StrFreeStrings(ppszStrings);
    }

    fclose(pMLUFile);
    fclose(pTmpFile);


    if (iMLUserFound == 0)
    {
        SysRemove(szTmpFile);
        RLckUnlockEX(hResLock);
        ErrSetErrorCode(ERR_MLUSER_NOT_FOUND);
        return (ERR_MLUSER_NOT_FOUND);
    }

    char            szTmpMLFilePath[SYS_MAX_PATH] = "";

    sprintf(szTmpMLFilePath, "%s.tmp", szMLTablePath);

    if (MscMoveFile(szMLTablePath, szTmpMLFilePath) < 0)
    {
        RLckUnlockEX(hResLock);
        return (ErrGetErrorCode());
    }

    if (MscMoveFile(szTmpFile, szMLTablePath) < 0)
    {
        MscMoveFile(szTmpMLFilePath, szMLTablePath);
        RLckUnlockEX(hResLock);
        return (ErrGetErrorCode());
    }

    SysRemove(szTmpMLFilePath);


    RLckUnlockEX(hResLock);

    return (0);

}



int             UsrMLGetUsersFileSnapShot(UserInfo * pUI, const char *pszFileName)
{

    char            szMLTablePath[SYS_MAX_PATH] = "";

    UsrGetMLTableFilePath(pUI, szMLTablePath);


    char            szResLock[SYS_MAX_PATH] = "";
    RLCK_HANDLE     hResLock = RLckLockSH(CfgGetBasedPath(szMLTablePath, szResLock));

    if (hResLock == INVALID_RLCK_HANDLE)
        return (ErrGetErrorCode());


    if (MscCopyFile(pszFileName, szMLTablePath) < 0)
    {
        RLckUnlockSH(hResLock);
        return (ErrGetErrorCode());
    }


    RLckUnlockSH(hResLock);

    return (0);

}




USRML_HANDLE    UsrMLOpenDB(UserInfo * pUI)
{

    MLUsersScanData *pMLUSD = (MLUsersScanData *) SysAlloc(sizeof(MLUsersScanData));

    if (pMLUSD == NULL)
        return (INVALID_USRML_HANDLE);

    SysGetTmpFile(pMLUSD->szTmpDBFile);

    if (UsrMLGetUsersFileSnapShot(pUI, pMLUSD->szTmpDBFile) < 0)
    {
        SysFree(pMLUSD);
        return (INVALID_USRML_HANDLE);
    }

    if ((pMLUSD->pDBFile = fopen(pMLUSD->szTmpDBFile, "rt")) == NULL)
    {
        SysRemove(pMLUSD->szTmpDBFile);
        SysFree(pMLUSD);
        return (INVALID_USRML_HANDLE);
    }

    return ((USRML_HANDLE) pMLUSD);

}



void            UsrMLCloseDB(USRML_HANDLE hUsersDB)
{

    MLUsersScanData *pMLUSD = (MLUsersScanData *) hUsersDB;

    fclose(pMLUSD->pDBFile);

    SysRemove(pMLUSD->szTmpDBFile);

    SysFree(pMLUSD);

}



const char     *UsrMLGetFirstUser(USRML_HANDLE hUsersDB)
{

    MLUsersScanData *pMLUSD = (MLUsersScanData *) hUsersDB;

    rewind(pMLUSD->pDBFile);


    const char     *pszMLUser = NULL;
    char            szMLULine[MLU_TABLE_LINE_MAX] = "";

    while ((pszMLUser == NULL) &&
            (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUSD->pDBFile) != NULL))
    {
        char          **ppszStrings = StrGetTabLineStrings(szMLULine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if (iFieldsCount >= 1)
        {
            strcpy(pMLUSD->szCurrUser, ppszStrings[0]);

            pszMLUser = pMLUSD->szCurrUser;
        }

        StrFreeStrings(ppszStrings);
    }

    return (pszMLUser);

}



const char     *UsrMLGetNextUser(USRML_HANDLE hUsersDB)
{

    MLUsersScanData *pMLUSD = (MLUsersScanData *) hUsersDB;


    const char     *pszMLUser = NULL;
    char            szMLULine[MLU_TABLE_LINE_MAX] = "";

    while ((pszMLUser == NULL) &&
            (MscFGets(szMLULine, sizeof(szMLULine) - 1, pMLUSD->pDBFile) != NULL))
    {
        char          **ppszStrings = StrGetTabLineStrings(szMLULine);

        if (ppszStrings == NULL)
            continue;

        int             iFieldsCount = StrStringsCount(ppszStrings);

        if (iFieldsCount >= 1)
        {
            strcpy(pMLUSD->szCurrUser, ppszStrings[0]);

            pszMLUser = pMLUSD->szCurrUser;
        }

        StrFreeStrings(ppszStrings);
    }

    return (pszMLUser);

}
