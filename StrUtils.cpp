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







#define INVCHAR             '^'









int             StrCmdLineToken(char const * &pszCmdLine, char *pszToken)
{

    char const     *pszCurr = pszCmdLine;

    for (; (*pszCurr == ' ') || (*pszCurr == '\t'); pszCurr++);

    if (*pszCurr == '\0')
        return (ERR_NO_MORE_TOKENS);

    if (*pszCurr == '"')
    {
        ++pszCurr;

        for (; *pszCurr != '\0';)
        {
            if (*pszCurr == '"')
            {
                ++pszCurr;

                if (*pszCurr != '"')
                    break;

                *pszToken++ = *pszCurr++;
            }
            else
                *pszToken++ = *pszCurr++;
        }

        *pszToken = '\0';
    }
    else
    {
        for (; (*pszCurr != ' ') && (*pszCurr != '\t') && (*pszCurr != '\0');)
            *pszToken++ = *pszCurr++;

        *pszToken = '\0';
    }

    pszCmdLine = pszCurr;

    return (0);

}





char          **StrGetArgs(char const * pszCmdLine, int &iArgsCount)
{

    char const     *pszCLine = pszCmdLine;
    char            szToken[1024] = "";

    for (iArgsCount = 0; StrCmdLineToken(pszCLine, szToken) == 0; iArgsCount++);


    char          **ppszArgs = (char **) SysAlloc((iArgsCount + 1) * sizeof(char *));

    if (ppszArgs == NULL)
        return (NULL);


    int             ii = 0;

    for (pszCLine = pszCmdLine; (ii < iArgsCount) && (StrCmdLineToken(pszCLine, szToken) == 0); ii++)
        ppszArgs[ii] = SysStrDup(szToken);

    ppszArgs[ii] = NULL;

    return (ppszArgs);

}



char           *StrLower(char *pszString)
{

    char           *pszCurr = pszString;

    for (; *pszCurr != '\0'; pszCurr++)
        if ((*pszCurr >= 'A') && (*pszCurr <= 'Z'))
            *pszCurr = 'a' + (*pszCurr - 'A');

    return (pszString);

}



char           *StrUpper(char *pszString)
{

    char           *pszCurr = pszString;

    for (; *pszCurr != '\0'; pszCurr++)
        if ((*pszCurr >= 'a') && (*pszCurr <= 'z'))
            *pszCurr = 'A' + (*pszCurr - 'a');

    return (pszString);

}



char           *StrCrypt(char const * pszString, char *pszCrypt)
{

    SetEmptyString(pszCrypt);

    for (int ii = 0; pszString[ii] != '\0'; ii++)
    {
        unsigned int    uChar = (unsigned int) pszString[ii];
        char            szByte[32] = "";

        sprintf(szByte, "%02x", (uChar ^ 101) & 0xff);

        strcat(pszCrypt, szByte);
    }

    return (pszCrypt);

}



char           *StrDeCrypt(char const * pszString, char *pszDeCrypt)
{

    int             iStrLength = strlen(pszString);

    SetEmptyString(pszDeCrypt);

    if ((iStrLength % 2) != 0)
        return (NULL);

    int             ii;

    for (ii = 0; ii < iStrLength; ii += 2)
    {
        char            szByte[8] = "";

        szByte[0] = pszString[ii];
        szByte[1] = pszString[ii + 1];
        szByte[2] = '\0';

        unsigned int    uChar = 0;

        if (sscanf(szByte, "%x", &uChar) != 1)
            return (NULL);

        pszDeCrypt[ii >> 1] = (char) ((uChar ^ 101) & 0xff);
    }

    pszDeCrypt[ii >> 1] = '\0';

    return (pszDeCrypt);

}



char          **StrTokenize(const char *pszString, const char *pszTokenizer)
{

    char           *pszBuffer = SysStrDup(pszString);

    if (pszBuffer == NULL)
        return (NULL);

    int             iTokenCount = 0;
    char           *pszToken = strtok(pszBuffer, pszTokenizer);

    while (pszToken != NULL)
    {
        ++iTokenCount;

        pszToken = strtok(NULL, pszTokenizer);
    }

    char          **ppszTokens = (char **) SysAlloc((iTokenCount + 1) * sizeof(char *));

    if (ppszTokens == NULL)
    {
        SysFree(pszBuffer);
        return (NULL);
    }

    strcpy(pszBuffer, pszString);

    iTokenCount = 0;
    pszToken = strtok(pszBuffer, pszTokenizer);

    while (pszToken != NULL)
    {
        ppszTokens[iTokenCount++] = SysStrDup(pszToken);

        pszToken = strtok(NULL, pszTokenizer);
    }

    ppszTokens[iTokenCount] = NULL;

    SysFree(pszBuffer);

    return (ppszTokens);

}



void            StrFreeStrings(char **ppszStrings)
{

    for (int ii = 0; ppszStrings[ii] != NULL; ii++)
        SysFree(ppszStrings[ii]);

    SysFree(ppszStrings);

}



int             StrStringsCount(char const * const * ppszStrings)
{

    int             ii;

    for (ii = 0; ppszStrings[ii] != NULL; ii++);

    return (ii);

}



char           *StrConcat(char const * const * ppszStrings, char const * pszCStr)
{

    int             ii,
                    iStrCount = StrStringsCount(ppszStrings),
                    iCStrLength = strlen(pszCStr),
                    iSumLength = 0;

    for (ii = 0; ii < iStrCount; ii++)
        iSumLength += strlen(ppszStrings[ii]) + iCStrLength;


    char           *pszConcat = (char *) SysAlloc(iSumLength + 1);

    if (pszConcat == NULL)
        return (NULL);

    SetEmptyString(pszConcat);

    for (ii = 0; ii < iStrCount; ii++)
    {
        if (ii > 0)
            strcat(pszConcat, pszCStr);

        strcat(pszConcat, ppszStrings[ii]);
    }

    return (pszConcat);

}



char           *StrDeQuote(char *pszString, int iChar)
{

    int             ii,
                    jj;

    for (ii = 0, jj = 0; pszString[ii] != '\0'; ii++)
    {
        if ((int) pszString[ii] == iChar)
        {
            if ((int) pszString[ii + 1] == iChar)
                pszString[jj++] = pszString[ii++];
        }
        else
            pszString[jj++] = pszString[ii];
    }

    pszString[jj] = '\0';

    return (pszString);

}



char           *StrQuote(const char *pszString, int iChar)
{

    int             iQuotes = 0;

    for (int qq = 0; pszString[qq] != '\0'; qq++)
        if ((int) pszString[qq] == iChar)
            ++iQuotes;

    char           *pszBuffer = (char *) SysAlloc(3 + strlen(pszString) + iQuotes);

    if (pszBuffer == NULL)
        return (NULL);

    pszBuffer[0] = (char) iChar;

    int             ii,
                    jj;

    for (ii = 0, jj = 1; pszString[ii] != '\0'; ii++)
    {
        if ((int) pszString[ii] == iChar)
            pszBuffer[jj++] = (char) iChar;

        pszBuffer[jj++] = pszString[ii];
    }

    pszBuffer[jj++] = (char) iChar;
    pszBuffer[jj] = '\0';

    return (pszBuffer);

}



char          **StrGetTabLineStrings(const char *pszUsrLine)
{

    char          **ppszStrings = StrTokenize(pszUsrLine, "\t");

    if (ppszStrings == NULL)
        return (NULL);

    for (int ii = 0; ppszStrings[ii] != NULL; ii++)
        StrDeQuote(ppszStrings[ii], '"');

    return (ppszStrings);

}



int             StrWriteCRLFString(FILE * pFile, const char *pszString)
{

    int             iStrLength = strlen(pszString);
    char           *pszBuffer = (char *) SysAlloc(iStrLength + 3);

    if (pszBuffer == NULL)
        return (ErrGetErrorCode());

    sprintf(pszBuffer, "%s\r\n", pszString);

    if (fwrite(pszBuffer, iStrLength + 2, 1, pFile) == 0)
    {
        ErrSetErrorCode(ERR_FILE_WRITE);
        SysFree(pszBuffer);
        return (ERR_FILE_WRITE);
    }

    SysFree(pszBuffer);

    return (0);

}




int             StrWildMatch(char const * pszString, char const * pszMatch)
{

    int             iPrev,
                    iMatched,
                    iReverse,
                    iEscape;

    for (; *pszMatch; pszString++, pszMatch++)
    {
        switch (*pszMatch)
        {
            case ('\\'):
                if (!*++pszMatch)
                    return (0);

            default:
                if (*pszString != *pszMatch)
                    return (0);
                continue;

            case ('?'):
                if (*pszString == '\0')
                    return (0);
                continue;

            case ('*'):
                while (*(++pszMatch) == '*');
                if (!*pszMatch)
                    return (1);
                while (*pszString)
                    if (StrWildMatch(pszString++, pszMatch))
                        return (1);
                return (0);

            case ('['):
                iEscape = 0;
                iReverse = (pszMatch[1] == INVCHAR) ? 1 : 0;
                if (iReverse)
                    pszMatch++;
                for (iPrev = 256, iMatched = 0; *++pszMatch &&
                        (iEscape || (*pszMatch != ']')); iPrev = iEscape ? iPrev : *pszMatch)
                {
                    if (!iEscape && (iEscape = *pszMatch == '\\'))
                        continue;
                    if (!iEscape && (*pszMatch == '-'))
                    {
                        if (!*++pszMatch)
                            return (0);
                        if (*pszMatch == '\\')
                            if (!*++pszMatch)
                                return (0);
                        iMatched = iMatched || ((*pszString <= *pszMatch) &&
                                (*pszString >= iPrev));
                    }
                    else
                        iMatched = iMatched || (*pszString == *pszMatch);
                    iEscape = 0;
                }

                if ((iPrev == 256) || iEscape || (*pszMatch != ']') || (iMatched == iReverse))
                    return (0);

                continue;
        }
    }

    return ((*pszString == '\0') ? 1 : 0);

}



int             StrIWildMatch(char const * pszString, char const * pszMatch)
{

    char           *pszLowString = SysStrDup(pszString);

    if (pszLowString == NULL)
        return (0);

    char           *pszLowMatch = SysStrDup(pszMatch);

    if (pszLowMatch == NULL)
    {
        SysFree(pszLowString);
        return (0);
    }

    StrLower(pszLowString);
    StrLower(pszLowMatch);


    int             iMatchResult = StrWildMatch(pszLowString, pszLowMatch);


    SysFree(pszLowMatch);
    SysFree(pszLowString);

    return (iMatchResult);

}



char           *StrLoadFile(FILE * pFile)
{

    fseek(pFile, 0, SEEK_END);

    unsigned int    uFileSize = (unsigned int) ftell(pFile);
    char           *pszData = (char *) SysAlloc(uFileSize + 1);

    if (pszData == NULL)
        return (NULL);

    fseek(pFile, 0, SEEK_SET);

    fread(pszData, uFileSize, 1, pFile);
    pszData[uFileSize] = '\0';

    return (pszData);

}




char           *StrVSprint(char const * pszFormat, va_list Args)
{

    int             iCurrSize = 256;
    char           *pszMessage = (char *) SysAlloc(iCurrSize);

    if (pszMessage == NULL)
        return (NULL);

    for (;;)
    {
	    int             iNeededBytes = SysVSNPrintf(pszMessage, iCurrSize - 1, pszFormat, Args);

        if ((iNeededBytes >= 0) && (iNeededBytes < (iCurrSize - 1)))
            return (pszMessage);

        iCurrSize *= 2;

        char           *pszNew = (char *) SysRealloc(pszMessage, iCurrSize);

        if (pszNew == NULL)
        {
            SysFree(pszMessage);
            return (NULL);
        }

        pszMessage = pszNew;
    }

    return (NULL);

}




char           *StrSprint(char const * pszFormat,...)
{

    va_list         Args;

    va_start(Args, pszFormat);


    char           *pszMessage = StrVSprint(pszFormat, Args);


    va_end(Args);

    return (pszMessage);

}




int             StrSplitString(char const * pszString, char const * pszSplitters,
                        char *pszStrLeft, int iSizeLeft, char *pszStrRight, int iSizeRight)
{

    char const     *pszSplitChar = NULL;

    for (; (*pszSplitters != '\0') && ((pszSplitChar = strchr(pszString, *pszSplitters)) == NULL);
            ++pszSplitters);

    if (pszSplitChar == NULL)
    {
        if (pszStrLeft != NULL)
            StrNCpy(pszStrLeft, pszString, iSizeLeft);

        if (pszStrRight != NULL)
            SetEmptyString(pszStrRight);
    }
    else
    {
        if (pszStrLeft != NULL)
        {
            int             iUserLength = (int) (pszSplitChar - pszString);

            StrNCpy(pszStrLeft, pszString, min(iUserLength + 1, iSizeLeft));
        }

        if (pszStrRight != NULL)
            StrNCpy(pszStrRight, pszSplitChar + 1, iSizeRight);
    }

    return (0);

}




char           *StrLTrim(char *pszString)
{

    int             ii;

    for (ii = 0; (pszString[ii] == ' ') || (pszString[ii] == '\t'); ii++);

    if ((ii > 0) && (pszString[ii] != '\0'))
    {
        int             jj;

        for (jj = ii; pszString[jj] != '\0'; jj++)
            pszString[jj - ii] = pszString[jj];

        pszString[jj - ii] = pszString[jj];
    }

    return (pszString);

}




char           *StrRTrim(char *pszString)
{

    int             ii = strlen(pszString) - 1;

    for (; (ii >= 0) && ((pszString[ii] == ' ') || (pszString[ii] == '\t')); ii--)
        pszString[ii] = '\0';


    return (pszString);

}




char           *StrTrim(char *pszString)
{

    return (StrRTrim(StrLTrim(pszString)));

}
