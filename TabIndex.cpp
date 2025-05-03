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


#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "ShBlocks.h"
#include "StrUtils.h"
#include "SList.h"
#include "BuffSock.h"
#include "MiscUtils.h"
#include "TabIndex.h"








#define TAB_INDEX_DIR               "tabindex"
#define TAB_INDEX_MARK              (*(SYS_UINT32 *) "ABDL")
#define KEY_BUFFER_SIZE             1024
#define TAB_RECORD_BUFFER_SIZE      2048
#define TOKEN_SEP_STR               "\t"










struct IndexRecord
{
    SYS_UINT32      uHashVal;
    SYS_UINT32      uFileOffset;
};

struct TabIndex
{
    FILE           *pIndexFile;
    int             iNumRecords;
};

struct IndexLookupData
{
    FILE           *pTabFile;
    IndexRecord    *pIR;
    int             iNumRecords;
};









static int      TbixRecordsCompare(const void *pRec1, const void *pRec2);
static int      TbixBuildKey(char *pszKey, va_list Args, bool bCaseSens);
static int      TbixBuildKey(char *pszKey, char const * const * ppszTabTokens,
                        int const * piFieldsIdx, bool bCaseSens);
static int      TbixReadRecord(TabIndex & TI, int iRecNum, IndexRecord & IR);
static int      TbixOpenIndex(char const * pszIndexFile, TabIndex & TI);
static int      TbixCloseIndex(TabIndex & TI);
static int      TbixSeekHash(TabIndex & TI, SYS_UINT32 uHashVal, int &iMinIndex, int &iMaxIndex);
static IndexRecord *TbixGetSeekRange(TabIndex & TI, SYS_UINT32 uHashVal, int &iNumRecords);
static char   **TbixLoadRecord(FILE * pTabFile, IndexRecord const & IR);











char           *TbixGetIndexFile(char const * pszTabFilePath, int const * piFieldsIdx,
                        char *pszIndexFile)
{

    char            szFileDir[SYS_MAX_PATH] = "",
                    szFileName[SYS_MAX_PATH] = "";

    MscSplitPath(pszTabFilePath, szFileDir, szFileName, NULL);

    sprintf(pszIndexFile, "%s%s%s%s-", szFileDir, TAB_INDEX_DIR,
            SYS_SLASH_STR, szFileName);

    for (int ii = 0; piFieldsIdx[ii] != INDEX_SEQUENCE_TERMINATOR; ii++)
    {
        char            szIndex[64] = "";

        sprintf(szIndex, "%02d", piFieldsIdx[ii]);

        strcat(pszIndexFile, szIndex);
    }

    strcat(pszIndexFile, ".idx");

    return (pszIndexFile);

}



int             TbixCreateIndex(char const * pszTabFilePath, int const * piFieldsIdx, bool bCaseSens,
                        int (*pHashFunc) (char const * const *, int const *, SYS_UINT32 *, bool))
{
///////////////////////////////////////////////////////////////////////////////
//  Adjust hash function
///////////////////////////////////////////////////////////////////////////////
    if (pHashFunc == NULL)
        pHashFunc = TbixCalculateHash;

///////////////////////////////////////////////////////////////////////////////
//  Build index file name
///////////////////////////////////////////////////////////////////////////////
    char            szIndexFile[SYS_MAX_PATH] = "";

    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
        return (ErrGetErrorCode());


    FILE           *pTabFile = fopen(pszTabFilePath, "rb");

    if (pTabFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Count records ( for allocation needs )
///////////////////////////////////////////////////////////////////////////////
    int             iNumRecords = 0;
    char            szLineBuffer[TAB_RECORD_BUFFER_SIZE] = "";

    for (; MscGetString(pTabFile, szLineBuffer, sizeof(szLineBuffer) - 1) != NULL; iNumRecords++);

    rewind(pTabFile);

///////////////////////////////////////////////////////////////////////////////
//  Alloc index records
///////////////////////////////////////////////////////////////////////////////
    IndexRecord    *pIR = (IndexRecord *) SysAlloc((iNumRecords + 1) * sizeof(IndexRecord));

    if (pIR == NULL)
    {
        fclose(pTabFile);
        return (ErrGetErrorCode());
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup indexes records
///////////////////////////////////////////////////////////////////////////////
    int             iCurrRecord = 0;

    for (; iCurrRecord < iNumRecords;)
    {
///////////////////////////////////////////////////////////////////////////////
//  Get current offset
///////////////////////////////////////////////////////////////////////////////
        SYS_UINT32      uFileOffset = (SYS_UINT32) ftell(pTabFile);

        if (MscGetString(pTabFile, szLineBuffer, sizeof(szLineBuffer) - 1) == NULL)
            break;

        if (szLineBuffer[0] == TAB_COMMENT_CHAR)
            continue;

        char          **ppszTabTokens = StrGetTabLineStrings(szLineBuffer);

        if (ppszTabTokens == NULL)
            continue;

///////////////////////////////////////////////////////////////////////////////
//  Calculate hash value
///////////////////////////////////////////////////////////////////////////////
        SYS_UINT32      uHashVal = 0;

        if (pHashFunc(ppszTabTokens, piFieldsIdx, &uHashVal, bCaseSens) == 0)
        {
///////////////////////////////////////////////////////////////////////////////
//  Store offset + hash
///////////////////////////////////////////////////////////////////////////////
            pIR[iCurrRecord].uFileOffset = uFileOffset;
            pIR[iCurrRecord].uHashVal = uHashVal;

            ++iCurrRecord;
        }

        StrFreeStrings(ppszTabTokens);
    }

    fclose(pTabFile);

///////////////////////////////////////////////////////////////////////////////
//  Sort records
///////////////////////////////////////////////////////////////////////////////
    if (iCurrRecord > 0)
        qsort(pIR, iCurrRecord, sizeof(IndexRecord), TbixRecordsCompare);

///////////////////////////////////////////////////////////////////////////////
//  Write index file
///////////////////////////////////////////////////////////////////////////////
    FILE           *pIndexFile = fopen(szIndexFile, "wb");

    if (pIndexFile == NULL)
    {
        SysFree(pIR);

        ErrSetErrorCode(ERR_FILE_CREATE, szIndexFile);
        return (ERR_FILE_CREATE);
    }

    SYS_UINT32      uIndexMark = TAB_INDEX_MARK;

    if (!fwrite(&uIndexMark, sizeof(uIndexMark), 1, pIndexFile) ||
            ((iCurrRecord > 0) && !fwrite(pIR, iCurrRecord * sizeof(IndexRecord), 1, pIndexFile)))
    {
        fclose(pIndexFile);
        SysFree(pIR);
        SysRemove(szIndexFile);

        ErrSetErrorCode(ERR_FILE_WRITE);
        return (ERR_FILE_WRITE);
    }

    fclose(pIndexFile);

    SysFree(pIR);

    return (0);

}



static int      TbixRecordsCompare(const void *pRec1, const void *pRec2)
{

    IndexRecord    *pIR1 = (IndexRecord *) pRec1,
                   *pIR2 = (IndexRecord *) pRec2;

    return ((pIR1->uHashVal > pIR2->uHashVal) ? +1 :
            ((pIR1->uHashVal < pIR2->uHashVal) ? -1 : 0));

}



static int      TbixBuildKey(char *pszKey, va_list Args, bool bCaseSens)
{

    SetEmptyString(pszKey);

    char const     *pszToken;

    for (int ii = 0; (pszToken = va_arg(Args, char *)) != NULL; ii++)
    {
        if (ii > 0)
            strcat(pszKey, TOKEN_SEP_STR);

        strcat(pszKey, pszToken);
    }

    if (!bCaseSens)
        StrLower(pszKey);

    return (0);

}




static int      TbixBuildKey(char *pszKey, char const * const * ppszTabTokens,
                        int const * piFieldsIdx, bool bCaseSens)
{

    SetEmptyString(pszKey);

    int             iFieldsCount = StrStringsCount(ppszTabTokens);

    for (int ii = 0; piFieldsIdx[ii] != INDEX_SEQUENCE_TERMINATOR; ii++)
    {
        if ((piFieldsIdx[ii] < 0) || (piFieldsIdx[ii] >= iFieldsCount))
        {
            ErrSetErrorCode(ERR_BAD_TAB_INDEX_FIELD);
            return (ERR_BAD_TAB_INDEX_FIELD);
        }

        if (ii > 0)
            strcat(pszKey, TOKEN_SEP_STR);

        strcat(pszKey, ppszTabTokens[piFieldsIdx[ii]]);
    }

    if (!bCaseSens)
        StrLower(pszKey);

    return (0);

}




int             TbixCalculateHash(char const * const * ppszTabTokens, int const * piFieldsIdx,
                        SYS_UINT32 * puHashVal, bool bCaseSens)
{

    char            szKey[KEY_BUFFER_SIZE] = "";

    if (TbixBuildKey(szKey, ppszTabTokens, piFieldsIdx, bCaseSens) < 0)
        return (ErrGetErrorCode());


    *puHashVal = MscHashString(szKey, strlen(szKey));


    return (0);

}



static int      TbixReadRecord(TabIndex & TI, int iRecNum, IndexRecord & IR)
{

    if ((fseek(TI.pIndexFile, sizeof(SYS_UINT32) + iRecNum * sizeof(IndexRecord),
                            SEEK_SET) != 0) ||
            !fread(&IR, sizeof(IndexRecord), 1, TI.pIndexFile))
    {
        ErrSetErrorCode(ERR_FILE_READ);
        return (ERR_FILE_READ);
    }

    return (0);

}



static int      TbixOpenIndex(char const * pszIndexFile, TabIndex & TI)
{

    FILE           *pIndexFile = fopen(pszIndexFile, "rb");

    if (pIndexFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_OPEN, pszIndexFile);
        return (ERR_FILE_OPEN);
    }

///////////////////////////////////////////////////////////////////////////////
//  Read and check signature
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT32      uIndexMark = 0;

    if (!fread(&uIndexMark, sizeof(uIndexMark), 1, pIndexFile))
    {
        fclose(pIndexFile);

        ErrSetErrorCode(ERR_FILE_READ, pszIndexFile);
        return (ERR_FILE_READ);
    }

    if (uIndexMark != TAB_INDEX_MARK)
    {
        fclose(pIndexFile);

        ErrSetErrorCode(ERR_BAD_INDEX_FILE, pszIndexFile);
        return (ERR_BAD_INDEX_FILE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Calculate the number of records
///////////////////////////////////////////////////////////////////////////////
    fseek(pIndexFile, 0, SEEK_END);

    unsigned long   ulFileSize = (unsigned long) ftell(pIndexFile);
    int             iNumRecords = (int) ((ulFileSize - sizeof(uIndexMark)) / sizeof(IndexRecord));

    fseek(pIndexFile, 0, SEEK_SET);

///////////////////////////////////////////////////////////////////////////////
//  Structure setup
///////////////////////////////////////////////////////////////////////////////
    ZeroData(TI);
    TI.pIndexFile = pIndexFile;
    TI.iNumRecords = iNumRecords;


    return (0);

}



static int      TbixCloseIndex(TabIndex & TI)
{

    fclose(TI.pIndexFile);

    ZeroData(TI);

    return (0);

}



static int      TbixSeekHash(TabIndex & TI, SYS_UINT32 uHashVal, int &iMinIndex, int &iMaxIndex)
{

    int             iLeft = 0,
                    iRight = TI.iNumRecords - 1,
                    iCurr;
    IndexRecord     IR;

    while (iRight >= iLeft)
    {
        iCurr = (iLeft + iRight) / 2;

        if (TbixReadRecord(TI, iCurr, IR) < 0)
            return (ErrGetErrorCode());

        if (uHashVal < IR.uHashVal)
            iRight = iCurr - 1;
        else if (uHashVal > IR.uHashVal)
            iLeft = iCurr + 1;
        else
            break;
    }

    if (iRight < iLeft)
    {
        ErrSetErrorCode(ERR_INDEX_HASH_NOT_FOUND);
        return (ERR_INDEX_HASH_NOT_FOUND);
    }

///////////////////////////////////////////////////////////////////////////////
//  Extract hash group
///////////////////////////////////////////////////////////////////////////////
    for (iMinIndex = iCurr; iMinIndex > 0;)
    {
        if (TbixReadRecord(TI, iMinIndex - 1, IR) < 0)
            return (ErrGetErrorCode());

        if (uHashVal == IR.uHashVal)
            --iMinIndex;
        else
            break;
    }

    for (iMaxIndex = iCurr; iMaxIndex < (TI.iNumRecords - 1);)
    {
        if (TbixReadRecord(TI, iMaxIndex + 1, IR) < 0)
            return (ErrGetErrorCode());

        if (uHashVal == IR.uHashVal)
            ++iMaxIndex;
        else
            break;
    }


    return (0);

}




static IndexRecord *TbixGetSeekRange(TabIndex & TI, SYS_UINT32 uHashVal, int &iNumRecords)
{
///////////////////////////////////////////////////////////////////////////////
//  Hash group seek
///////////////////////////////////////////////////////////////////////////////
    int             iMinIndex,
                    iMaxIndex;

    if (TbixSeekHash(TI, uHashVal, iMinIndex, iMaxIndex) < 0)
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Records allocation
///////////////////////////////////////////////////////////////////////////////
    IndexRecord    *pIR = (IndexRecord *) SysAlloc((iMaxIndex - iMinIndex + 1) * sizeof(IndexRecord));

    if (pIR == NULL)
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Records load
///////////////////////////////////////////////////////////////////////////////
    iNumRecords = iMaxIndex - iMinIndex + 1;

    for (int ii = 0; ii < iNumRecords; ii++)
        if (TbixReadRecord(TI, iMinIndex + ii, pIR[ii]) < 0)
        {
            SysFree(pIR);
            return (NULL);
        }


    return (pIR);

}




char          **TbixLookup(char const * pszTabFilePath, int const * piFieldsIdx,
                        bool bCaseSens,...)
{
///////////////////////////////////////////////////////////////////////////////
//  Build index file name
///////////////////////////////////////////////////////////////////////////////
    char            szIndexFile[SYS_MAX_PATH] = "";

    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Calculate key & hash
///////////////////////////////////////////////////////////////////////////////
    va_list         Args;

    va_start(Args, bCaseSens);


    char            szRefKey[KEY_BUFFER_SIZE] = "";

    if (TbixBuildKey(szRefKey, Args, bCaseSens) < 0)
    {
        va_end(Args);
        return (NULL);
    }

    va_end(Args);


    SYS_UINT32      uHashVal = MscHashString(szRefKey, strlen(szRefKey));

///////////////////////////////////////////////////////////////////////////////
//  Open index
///////////////////////////////////////////////////////////////////////////////
    TabIndex        TI;

    if (TbixOpenIndex(szIndexFile, TI) < 0)
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Try to lookup records
///////////////////////////////////////////////////////////////////////////////
    int             iNumRecords = 0;
    IndexRecord    *pIR = TbixGetSeekRange(TI, uHashVal, iNumRecords);

    TbixCloseIndex(TI);

    if (pIR == NULL)
        return (NULL);

///////////////////////////////////////////////////////////////////////////////
//  Search for the matched one
///////////////////////////////////////////////////////////////////////////////
    FILE           *pTabFile = fopen(pszTabFilePath, "rb");

    if (pTabFile == NULL)
    {
        SysFree(pIR);

        ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
        return (NULL);
    }

    for (int ii = 0; ii < iNumRecords; ii++)
    {
        char          **ppszTabTokens = TbixLoadRecord(pTabFile, pIR[ii]);

        if (ppszTabTokens != NULL)
        {
            char            szKey[KEY_BUFFER_SIZE] = "";

            if (TbixBuildKey(szKey, ppszTabTokens, piFieldsIdx, bCaseSens) == 0)
            {
                if (bCaseSens)
                {
                    if (strcmp(szKey, szRefKey) == 0)
                    {
                        fclose(pTabFile);
                        SysFree(pIR);
                        return (ppszTabTokens);
                    }
                }
                else
                {
                    if (stricmp(szKey, szRefKey) == 0)
                    {
                        fclose(pTabFile);
                        SysFree(pIR);
                        return (ppszTabTokens);
                    }
                }
            }

            StrFreeStrings(ppszTabTokens);
        }
    }

    fclose(pTabFile);

    SysFree(pIR);


    ErrSetErrorCode(ERR_RECORD_NOT_FOUND);

    return (NULL);

}



static char   **TbixLoadRecord(FILE * pTabFile, IndexRecord const & IR)
{

    if (fseek(pTabFile, IR.uFileOffset, SEEK_SET) != 0)
    {
        ErrSetErrorCode(ERR_FILE_READ);
        return (NULL);
    }

    char            szLineBuffer[TAB_RECORD_BUFFER_SIZE] = "";

    if (MscGetString(pTabFile, szLineBuffer, sizeof(szLineBuffer) - 1) == NULL)
    {
        ErrSetErrorCode(ERR_FILE_READ);
        return (NULL);
    }

    return (StrGetTabLineStrings(szLineBuffer));

}



int             TbixCheckIndex(char const * pszTabFilePath, int const * piFieldsIdx, bool bCaseSens,
                        int (*pHashFunc) (char const * const *, int const *, SYS_UINT32 *, bool))
{

    SYS_FILE_INFO   FI_Tab;

    if (SysGetFileInfo(pszTabFilePath, FI_Tab) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Build index file name
///////////////////////////////////////////////////////////////////////////////
    char            szIndexFile[SYS_MAX_PATH] = "";

    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Compare TAB <-> Index dates
///////////////////////////////////////////////////////////////////////////////
    SYS_FILE_INFO   FI_Index;

    if ((SysGetFileInfo(szIndexFile, FI_Index) < 0) || (FI_Tab.tMod > FI_Index.tMod))
    {

///////////////////////////////////////////////////////////////////////////////
//  Rebuild the index
///////////////////////////////////////////////////////////////////////////////
        if (TbixCreateIndex(pszTabFilePath, piFieldsIdx, bCaseSens, pHashFunc) < 0)
            return (ErrGetErrorCode());

    }



    return (0);

}



INDEX_HANDLE    TbixOpenHandle(char const * pszTabFilePath, int const * piFieldsIdx,
                        SYS_UINT32 uHashVal)
{
///////////////////////////////////////////////////////////////////////////////
//  Build index file name
///////////////////////////////////////////////////////////////////////////////
    char            szIndexFile[SYS_MAX_PATH] = "";

    if (TbixGetIndexFile(pszTabFilePath, piFieldsIdx, szIndexFile) < 0)
        return (INVALID_INDEX_HANDLE);

///////////////////////////////////////////////////////////////////////////////
//  Open index
///////////////////////////////////////////////////////////////////////////////
    TabIndex        TI;

    if (TbixOpenIndex(szIndexFile, TI) < 0)
        return (INVALID_INDEX_HANDLE);

///////////////////////////////////////////////////////////////////////////////
//  Try to lookup records
///////////////////////////////////////////////////////////////////////////////
    int             iNumRecords = 0;
    IndexRecord    *pIR = TbixGetSeekRange(TI, uHashVal, iNumRecords);

    TbixCloseIndex(TI);

    if (pIR == NULL)
        return (INVALID_INDEX_HANDLE);

///////////////////////////////////////////////////////////////////////////////
//  Open tab file
///////////////////////////////////////////////////////////////////////////////
    FILE           *pTabFile = fopen(pszTabFilePath, "rb");

    if (pTabFile == NULL)
    {
        SysFree(pIR);

        ErrSetErrorCode(ERR_FILE_OPEN, pszTabFilePath);
        return (INVALID_INDEX_HANDLE);
    }

///////////////////////////////////////////////////////////////////////////////
//  Setup lookup struct
///////////////////////////////////////////////////////////////////////////////
    IndexLookupData *pILD = (IndexLookupData *) SysAlloc(sizeof(IndexLookupData));

    if (pILD == NULL)
    {
        fclose(pTabFile);
        SysFree(pIR);
        return (INVALID_INDEX_HANDLE);
    }

    pILD->pTabFile = pTabFile;
    pILD->pIR = pIR;
    pILD->iNumRecords = iNumRecords;


    return ((INDEX_HANDLE) pILD);

}



int             TbixCloseHandle(INDEX_HANDLE hIndexLookup)
{

    IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

    fclose(pILD->pTabFile);

    SysFree(pILD->pIR);

    SysFree(pILD);

    return (0);

}



int             TbixLookedUpRecords(INDEX_HANDLE hIndexLookup)
{

    IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

    return (pILD->iNumRecords);

}



char          **TbixGetRecord(INDEX_HANDLE hIndexLookup, int iRecord)
{

    IndexLookupData *pILD = (IndexLookupData *) hIndexLookup;

    if ((iRecord < 0) || (iRecord >= pILD->iNumRecords))
    {
        ErrSetErrorCode(ERR_RECORD_NOT_FOUND);
        return (NULL);
    }

    return (TbixLoadRecord(pILD->pTabFile, pILD->pIR[iRecord]));

}
