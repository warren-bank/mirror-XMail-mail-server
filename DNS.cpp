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
#include "StrUtils.h"
#include "BuffSock.h"
#include "SList.h"
#include "MailConfig.h"
#include "MailSvr.h"
#include "MiscUtils.h"
#include "SvrUtils.h"
#include "DNS.h"





#define DNS_PORTNO              53
#define DNS_SOCKET_TIMEOUT      20
#define DNS_QUERY_EXTRA         512
#define DNS_MAX_RESP_PACKET     1024
#define DNS_SEND_RETRIES        4
#define DNS_MAX_RR_DATA         256
#define DNS_LABEL_LEN_MASK      0xff3f
#define DNS_LABEL_LEN_INVMASK   0xc0

#define ROOTS_FILE              "dnsroots"








struct DNSQuery
{
    DNS_HEADER      DNSH;
    SYS_UINT8       QueryData[DNS_QUERY_EXTRA];
};

struct DNSResourceRecord
{
    char            szName[256];
    SYS_UINT16      Type;
    SYS_UINT16      Class;
    SYS_UINT32      TTL;
    SYS_UINT16      Lenght;
    SYS_UINT8       RespData[DNS_MAX_RR_DATA];
};

struct DNSNameNode
{
    LISTLINK        LL;
    char           *pszName;
};






static DNSNameNode *DNS_AllocNameNode(char const * pszName);
static void     DNS_FreeNameNode(DNSNameNode * pDNSNN);
static void     DNS_FreeNameList(HSLIST & hNameList);
static DNSNameNode *DNS_GetNameNode(HSLIST & hNameList, char const * pszName);
static int      DNS_AddNameNode(HSLIST & hNameList, char const * pszName);
static int      DNS_GetResourceRecord(SYS_UINT8 const * pBaseData, SYS_UINT8 const * pRespData,
                        DNSResourceRecord * pRR = NULL);
static int      DNS_GetName(SYS_UINT8 const * pBaseData, SYS_UINT8 const * pRespData,
                        char *pszInetName = NULL);
static int      DNS_GetQuery(SYS_UINT8 const * pBaseData, SYS_UINT8 const * pRespData,
                        char *pszInetName = NULL, SYS_UINT16 * pType = NULL,
                        SYS_UINT16 * pClass = NULL);
static int      DNS_NameCopy(SYS_UINT8 * pDNSQName, char const * pszInetName);
static SYS_UINT16 DNS_GetUniqueQueryId(void);
static int      DNS_RequestSetup(DNSQuery & DNSQ, unsigned int uOpCode,
                        unsigned int uQType, char const * pszInetName,
                        int &iQueryLenght, bool bQueryRecursion = false);
static SYS_UINT8 *DNS_QueryExec(char const * pszDNSServer, int iPortNo, int iTimeout,
                        unsigned int uOpCode, unsigned int uQType,
                        char const * pszInetName, bool bQueryRecursion = false);
static SYS_UINT8 *DNS_QuerySendStream(char const * pszDNSServer, int iPortNo, int iTimeout,
                        DNSQuery const & DNSQ, int iQueryLenght);
static SYS_UINT8 *DNS_QuerySendDGram(char const * pszDNSServer, int iPortNo, int iTimeout,
                        DNSQuery const & DNSQ, int iQueryLenght, bool & bTruncated);
static int      DNS_DecodeResponseMX(SYS_UINT8 * pRespData, char const * pszDomain,
                        HSLIST & hNameList, char const * pszRespFile, SYS_UINT32 * pTTL = NULL);
static int      DNS_DecodeResponseMX(SYS_UINT8 * pRespData, char const * pszRespFile,
                        SYS_UINT32 * pTTL = NULL);
static int      DNS_DecodeResponseNS(SYS_UINT8 * pRespData, char const * pszRespFile,
                        bool & bAuth, SYS_UINT32 * pTTL = NULL);
static int      DNS_FindDomainMX(char const * pszDNSServer, char const * pszDomain,
                        HSLIST & hNameList, char const * pszRespFile, SYS_UINT32 * pTTL = NULL);
static int      DNS_QueryDomainMX(char const * pszDNSServer, char const * pszDomain,
                        char *&pszMXDomains, SYS_UINT32 * pTTL = NULL);
static int      DNS_GetNameServersLL(char const * pszDNSServer, char const * pszDomain,
                        char const * pszRespFile, HSLIST & hNameList, SYS_UINT32 * pTTL = NULL);
static char    *DNS_GetRootsFile(char *pszRootsFilePath);









static DNSNameNode *DNS_AllocNameNode(char const * pszName)
{

    DNSNameNode    *pDNSNN = (DNSNameNode *) SysAlloc(sizeof(DNSNameNode));

    if (pDNSNN == NULL)
        return (NULL);

    ListLinkInit(pDNSNN);

    if ((pDNSNN->pszName = SysStrDup(pszName)) == NULL)
    {
        SysFree(pDNSNN);
        return (NULL);
    }

    return (pDNSNN);

}




static void     DNS_FreeNameNode(DNSNameNode * pDNSNN)
{

    SysFree(pDNSNN->pszName);

    SysFree(pDNSNN);

}




static void     DNS_FreeNameList(HSLIST & hNameList)
{

    DNSNameNode    *pDNSNN;

    while ((pDNSNN = (DNSNameNode *) ListRemove(hNameList)) != INVALID_SLIST_PTR)
        DNS_FreeNameNode(pDNSNN);

}




static DNSNameNode *DNS_GetNameNode(HSLIST & hNameList, char const * pszName)
{

    DNSNameNode    *pDNSNN = (DNSNameNode *) ListFirst(hNameList);

    for (; pDNSNN != INVALID_SLIST_PTR; pDNSNN = (DNSNameNode *)
            ListNext(hNameList, (PLISTLINK) pDNSNN))
        if (stricmp(pDNSNN->pszName, pszName) == 0)
            return (pDNSNN);

    return (NULL);

}



static int      DNS_AddNameNode(HSLIST & hNameList, char const * pszName)
{

    DNSNameNode    *pDNSNN = DNS_AllocNameNode(pszName);

    if (pDNSNN == NULL)
        return (ErrGetErrorCode());

    ListAddTail(hNameList, (PLISTLINK) pDNSNN);

    return (0);

}



static int      DNS_GetResourceRecord(SYS_UINT8 const * pBaseData, SYS_UINT8 const * pRespData,
                        DNSResourceRecord * pRR)
{

    if (pRR != NULL)
        ZeroData(*pRR);

///////////////////////////////////////////////////////////////////////////////
//  Read name field
///////////////////////////////////////////////////////////////////////////////
    char           *pszName = (pRR != NULL) ? pRR->szName : NULL;
    int             iRRLen = DNS_GetName(pBaseData, pRespData, pszName);

    pRespData += iRRLen;

///////////////////////////////////////////////////////////////////////////////
//  Read type field
///////////////////////////////////////////////////////////////////////////////
    if (pRR != NULL)
        pRR->Type = ntohs(*((SYS_UINT16 *) pRespData));

    pRespData += sizeof(SYS_UINT16);
    iRRLen += sizeof(SYS_UINT16);

///////////////////////////////////////////////////////////////////////////////
//  Read class field
///////////////////////////////////////////////////////////////////////////////
    if (pRR != NULL)
        pRR->Class = ntohs(*((SYS_UINT16 *) pRespData));

    pRespData += sizeof(SYS_UINT16);
    iRRLen += sizeof(SYS_UINT16);

///////////////////////////////////////////////////////////////////////////////
//  Read TTL field
///////////////////////////////////////////////////////////////////////////////
    if (pRR != NULL)
        pRR->TTL = ntohl(*((SYS_UINT32 *) pRespData));

    pRespData += sizeof(SYS_UINT32);
    iRRLen += sizeof(SYS_UINT32);

///////////////////////////////////////////////////////////////////////////////
//  Read lenght field
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT16      Lenght = ntohs(*((SYS_UINT16 *) pRespData));

    if (pRR != NULL)
        pRR->Lenght = Lenght;

    pRespData += sizeof(SYS_UINT16);
    iRRLen += sizeof(SYS_UINT16);

///////////////////////////////////////////////////////////////////////////////
//  Read RR data
///////////////////////////////////////////////////////////////////////////////
    if (pRR != NULL)
        memcpy(pRR->RespData, pRespData, Lenght);

    iRRLen += (int) Lenght;


    return (iRRLen);

}



static int      DNS_GetName(SYS_UINT8 const * pBaseData, SYS_UINT8 const * pRespData,
                        char *pszInetName)
{

    char            szNameBuffer[512] = "";

    if (pszInetName == NULL)
        pszInetName = szNameBuffer;

    int             iNameLen = 0,
                    iBackLink = 0;

    while (*pRespData != 0)
    {
        if (*pRespData & DNS_LABEL_LEN_INVMASK)
        {
            int             iLabelOffset = (int) ntohs(*((SYS_UINT16 *) pRespData) & DNS_LABEL_LEN_MASK);

            pRespData = pBaseData + iLabelOffset;

            if (!iBackLink)
                iNameLen += sizeof(SYS_UINT16), ++iBackLink;

            continue;
        }

        int             iLabelLen = (int) *pRespData;

        memcpy(pszInetName, pRespData + 1, iLabelLen);
        pszInetName[iLabelLen] = '.';

        pszInetName += iLabelLen + 1;
        if (!iBackLink)
            iNameLen += iLabelLen + 1;
        pRespData += iLabelLen + 1;
    }

    *pszInetName = '\0';

    return ((!iBackLink) ? iNameLen + 1 : iNameLen);

}



static int      DNS_GetQuery(SYS_UINT8 const * pBaseData, SYS_UINT8 const * pRespData,
                        char *pszInetName, SYS_UINT16 * pType, SYS_UINT16 * pClass)
{

///////////////////////////////////////////////////////////////////////////////
//  Read name field
///////////////////////////////////////////////////////////////////////////////
    int             iQueryLen = DNS_GetName(pBaseData, pRespData, pszInetName);

    pRespData += iQueryLen;

///////////////////////////////////////////////////////////////////////////////
//  Read type field
///////////////////////////////////////////////////////////////////////////////
    if (pType != NULL)
        *pType = ntohs(*((SYS_UINT16 *) pRespData));

    pRespData += sizeof(SYS_UINT16);
    iQueryLen += sizeof(SYS_UINT16);

///////////////////////////////////////////////////////////////////////////////
//  Read class field
///////////////////////////////////////////////////////////////////////////////
    if (pClass != NULL)
        *pClass = ntohs(*((SYS_UINT16 *) pRespData));

    pRespData += sizeof(SYS_UINT16);
    iQueryLen += sizeof(SYS_UINT16);

    return (iQueryLen);

}



static int      DNS_NameCopy(SYS_UINT8 * pDNSQName, char const * pszInetName)
{

    char           *pszNameCopy = SysStrDup(pszInetName);

    if (pszNameCopy == NULL)
        return (ErrGetErrorCode());

    int             iNameLen = 0;
    char           *pszToken = strtok(pszNameCopy, ".");

    while (pszToken != NULL)
    {
        int             iTokLen = strlen(pszToken);

        *pDNSQName = (SYS_UINT8) iTokLen;

        strcpy((char *) pDNSQName + 1, pszToken);

        pDNSQName += iTokLen + 1;
        iNameLen += iTokLen + 1;

        pszToken = strtok(NULL, ".");
    }

    *pDNSQName = 0;

    SysFree(pszNameCopy);

    return (iNameLen + 1);

}



static SYS_UINT16 DNS_GetUniqueQueryId(void)
{

    SYS_UINT32      uThreadId = (SYS_UINT32) SysGetCurrentThreadId();

    return ((SYS_UINT16) uThreadId);

}



static int      DNS_RequestSetup(DNSQuery & DNSQ, unsigned int uOpCode,
                        unsigned int uQType, char const * pszInetName,
                        int &iQueryLenght, bool bQueryRecursion)
{

///////////////////////////////////////////////////////////////////////////////
//  Setup query header
///////////////////////////////////////////////////////////////////////////////
    ZeroData(DNSQ);
    DNSQ.DNSH.Id = DNS_GetUniqueQueryId();
    DNSQ.DNSH.QR = 0;
    DNSQ.DNSH.RD = (bQueryRecursion) ? 1 : 0;
    DNSQ.DNSH.OpCode = uOpCode;
    DNSQ.DNSH.QDCount = htons(1);
    DNSQ.DNSH.ANCount = htons(0);
    DNSQ.DNSH.NSCount = htons(0);
    DNSQ.DNSH.ARCount = htons(0);

    SYS_UINT8      *pQueryData = DNSQ.QueryData;
///////////////////////////////////////////////////////////////////////////////
//  Copy name to query
///////////////////////////////////////////////////////////////////////////////
    int             iNameLen = DNS_NameCopy(pQueryData, pszInetName);

    if (iNameLen < 0)
        return (ErrGetErrorCode());

    pQueryData += iNameLen;

///////////////////////////////////////////////////////////////////////////////
//  Set query type
///////////////////////////////////////////////////////////////////////////////
    *((SYS_UINT16 *) pQueryData) = (SYS_UINT16) htons(uQType);
    pQueryData += sizeof(SYS_UINT16);

///////////////////////////////////////////////////////////////////////////////
//  Set query class
///////////////////////////////////////////////////////////////////////////////
    *((SYS_UINT16 *) pQueryData) = (SYS_UINT16) htons(QCLASS_IN);
    pQueryData += sizeof(SYS_UINT16);


    iQueryLenght = (int) (pQueryData - (SYS_UINT8 *) & DNSQ);


    return (0);

}



static SYS_UINT8 *DNS_QueryExec(char const * pszDNSServer, int iPortNo, int iTimeout,
                        unsigned int uOpCode, unsigned int uQType,
                        char const * pszInetName, bool bQueryRecursion)
{

    int             iQueryLenght;
    DNSQuery        DNSQ;

    if (DNS_RequestSetup(DNSQ, uOpCode, uQType, pszInetName, iQueryLenght,
                    bQueryRecursion) < 0)
        return (NULL);

    bool            bTruncated = false;

    SYS_UINT8      *pRespData = DNS_QuerySendDGram(pszDNSServer, iPortNo, iTimeout,
            DNSQ, iQueryLenght, bTruncated);

    if ((pRespData == NULL) && bTruncated)
        pRespData = DNS_QuerySendStream(pszDNSServer, iPortNo, iTimeout,
                DNSQ, iQueryLenght);

    return (pRespData);

}




static SYS_UINT8 *DNS_QuerySendStream(char const * pszDNSServer, int iPortNo, int iTimeout,
                        DNSQuery const & DNSQ, int iQueryLenght)
{

///////////////////////////////////////////////////////////////////////////////
//  Open DNS server socket
///////////////////////////////////////////////////////////////////////////////
    SYS_SOCKET      SockFD;
    SYS_INET_ADDR   SvrAddr;
    SYS_INET_ADDR   SockAddr;

    if (MscCreateClientSocket(pszDNSServer, iPortNo, SOCK_STREAM, &SockFD, &SvrAddr,
                    &SockAddr, iTimeout) < 0)
        return (NULL);


    SYS_UINT16      QLenght = (SYS_UINT16) htons(iQueryLenght);

///////////////////////////////////////////////////////////////////////////////
//  Send packet lenght
///////////////////////////////////////////////////////////////////////////////
    if (SysSend(SockFD, (char *) &QLenght, sizeof(QLenght), iTimeout) != sizeof(QLenght))
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        ErrSetErrorCode(ErrorFetch());
        return (NULL);
    }

///////////////////////////////////////////////////////////////////////////////
//  Send packet
///////////////////////////////////////////////////////////////////////////////
    if (SysSend(SockFD, (char const *) &DNSQ, iQueryLenght, iTimeout) != iQueryLenght)
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        ErrSetErrorCode(ErrorFetch());
        return (NULL);
    }

///////////////////////////////////////////////////////////////////////////////
//  Receive packet lenght
///////////////////////////////////////////////////////////////////////////////
    if (SysRecv(SockFD, (char *) &QLenght, sizeof(QLenght), iTimeout) != sizeof(QLenght))
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        ErrSetErrorCode(ErrorFetch());
        return (NULL);
    }

    int             iPacketLenght = (int) ntohs(QLenght);

    SYS_UINT8      *pRespData = (SYS_UINT8 *) SysAlloc(iPacketLenght + 1);

    if (pRespData == NULL)
    {
        ErrorPush();
        SysCloseSocket(SockFD);
        ErrSetErrorCode(ErrorFetch());
        return (NULL);
    }

///////////////////////////////////////////////////////////////////////////////
//  Receive packet
///////////////////////////////////////////////////////////////////////////////
    if (SysRecv(SockFD, (char *) pRespData, iPacketLenght, iTimeout) != iPacketLenght)
    {
        ErrorPush();
        SysFree(pRespData);
        SysCloseSocket(SockFD);
        ErrSetErrorCode(ErrorFetch());
        return (NULL);
    }

    SysCloseSocket(SockFD);


    DNS_HEADER     *pDNSH = (DNS_HEADER *) pRespData;

    if (DNSQ.DNSH.RD && !pDNSH->RA)
    {
        SysFree(pRespData);
        ErrSetErrorCode(ERR_DNS_RECURSION_NOT_AVAILABLE);
        return (NULL);
    }

    return (pRespData);

}



static SYS_UINT8 *DNS_QuerySendDGram(char const * pszDNSServer, int iPortNo, int iTimeout,
                        DNSQuery const & DNSQ, int iQueryLenght, bool & bTruncated)
{

    bTruncated = false;

///////////////////////////////////////////////////////////////////////////////
//  Open DNS server socket
///////////////////////////////////////////////////////////////////////////////
    SYS_SOCKET      SockFD;
    SYS_INET_ADDR   SvrAddr;
    SYS_INET_ADDR   SockAddr;

    if (MscCreateClientSocket(pszDNSServer, iPortNo, SOCK_DGRAM, &SockFD, &SvrAddr,
                    &SockAddr, iTimeout) < 0)
        return (NULL);


///////////////////////////////////////////////////////////////////////////////
//  Query loop
///////////////////////////////////////////////////////////////////////////////
    for (int iSendLoops = 0; iSendLoops < DNS_SEND_RETRIES; iSendLoops++)
    {
///////////////////////////////////////////////////////////////////////////////
//  Send packet
///////////////////////////////////////////////////////////////////////////////
        if (SysSendDataTo(SockFD, (const struct sockaddr *) & SvrAddr, sizeof(SvrAddr),
                        (char const *) &DNSQ, iQueryLenght, iTimeout) != iQueryLenght)
            continue;

///////////////////////////////////////////////////////////////////////////////
//  Receive packet lenght
///////////////////////////////////////////////////////////////////////////////
        SYS_INET_ADDR   RecvAddr;
        SYS_UINT8       RespBuffer[1024];

        ZeroData(RecvAddr);

        int             iPacketLenght = SysRecvDataFrom(SockFD, (struct sockaddr *) & RecvAddr, sizeof(RecvAddr),
                (char *) RespBuffer, sizeof(RespBuffer), iTimeout);


        if (iPacketLenght < sizeof(DNS_HEADER))
            continue;

        DNS_HEADER     *pDNSH = (DNS_HEADER *) RespBuffer;

        if (pDNSH->Id != DNSQ.DNSH.Id)
            continue;

        if (DNSQ.DNSH.RD && !pDNSH->RA)
        {
            SysCloseSocket(SockFD);
            ErrSetErrorCode(ERR_DNS_RECURSION_NOT_AVAILABLE);
            return (NULL);
        }

        if (pDNSH->TC)
        {
            bTruncated = true;

            SysCloseSocket(SockFD);
            ErrSetErrorCode(ERR_TRUNCATED_DGRAM_DNS_RESPONSE);
            return (NULL);
        }

        SYS_UINT8      *pRespData = (SYS_UINT8 *) SysAlloc(iPacketLenght + 1);

        if (pRespData == NULL)
        {
            ErrorPush();
            SysCloseSocket(SockFD);
            ErrSetErrorCode(ErrorFetch());
            return (NULL);
        }

        memcpy(pRespData, RespBuffer, iPacketLenght);

        SysCloseSocket(SockFD);

        return (pRespData);
    }

    SysCloseSocket(SockFD);

    ErrSetErrorCode(ERR_NO_DGRAM_DNS_RESPONSE);

    return (NULL);

}




static int      DNS_DecodeResponseMX(SYS_UINT8 * pRespData, char const * pszDomain,
                        HSLIST & hNameList, char const * pszRespFile, SYS_UINT32 * pTTL)
{

    SYS_UINT8      *pBaseData = pRespData;
    DNSQuery       *pDNSQ = (DNSQuery *) pRespData;

    if (pDNSQ->DNSH.RCode != 0)
    {
        ErrSetErrorCode(ERR_BAD_DNS_RESPONSE);
        return (ERR_BAD_DNS_RESPONSE);
    }

    pDNSQ->DNSH.QDCount = ntohs(pDNSQ->DNSH.QDCount);
    pDNSQ->DNSH.ANCount = ntohs(pDNSQ->DNSH.ANCount);
    pDNSQ->DNSH.NSCount = ntohs(pDNSQ->DNSH.NSCount);
    pDNSQ->DNSH.ARCount = ntohs(pDNSQ->DNSH.ARCount);


    FILE           *pMXFile = NULL;

    if (pDNSQ->DNSH.ANCount != 0)
    {
        if ((pMXFile = fopen(pszRespFile, "wb")) == NULL)
        {
            ErrSetErrorCode(ERR_FILE_CREATE);
            return (ERR_FILE_CREATE);
        }
    }


    pRespData = pDNSQ->QueryData;

///////////////////////////////////////////////////////////////////////////////
//  Scan query data
///////////////////////////////////////////////////////////////////////////////
    int             ii;

    for (ii = 0; ii < (int) pDNSQ->DNSH.QDCount; ii++)
    {
        SYS_UINT16      Type = 0,
                        Class = 0;
        char            szInetName[MAX_HOST_NAME] = "";


        int             iQLenght = DNS_GetQuery(pBaseData, pRespData, szInetName, &Type, &Class);


        pRespData += iQLenght;
    }

///////////////////////////////////////////////////////////////////////////////
//  Scan answer data
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT32      TTL = 0;

    for (ii = 0; ii < (int) pDNSQ->DNSH.ANCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;


        SYS_UINT8      *pMXData = RR.RespData;
        SYS_UINT16      Preference = ntohs(*((SYS_UINT16 *) pMXData));

        pMXData += sizeof(SYS_UINT16);

        char            szMXDomain[MAX_HOST_NAME] = "";

        DNS_GetName(pBaseData, pMXData, szMXDomain);

        if (ii == 0)
            fprintf(pMXFile, "%d:%s", (int) Preference, szMXDomain);
        else
            fprintf(pMXFile, ",%d:%s", (int) Preference, szMXDomain);

        if ((TTL == 0) || (RR.TTL < TTL))
            TTL = RR.TTL;
    }

///////////////////////////////////////////////////////////////////////////////
//  Got answers ?
///////////////////////////////////////////////////////////////////////////////
    if (pMXFile != NULL)
    {
        fclose(pMXFile);
        if (pTTL != NULL)
            *pTTL = TTL;
        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Scan name servers data
///////////////////////////////////////////////////////////////////////////////
    for (ii = 0; ii < (int) pDNSQ->DNSH.NSCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;


        SYS_UINT8      *pNSData = RR.RespData;

        char            szNSName[MAX_HOST_NAME] = "";

        DNS_GetName(pBaseData, pNSData, szNSName);

///////////////////////////////////////////////////////////////////////////////
//  Recursively try authority name servers
///////////////////////////////////////////////////////////////////////////////
        if ((DNS_GetNameNode(hNameList, szNSName) == NULL) &&
                DNS_FindDomainMX(szNSName, pszDomain, hNameList, pszRespFile, pTTL) == 0)
            return (0);

    }

///////////////////////////////////////////////////////////////////////////////
//  Scan additional records data
///////////////////////////////////////////////////////////////////////////////
    for (ii = 0; ii < (int) pDNSQ->DNSH.ARCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;

    }

    ErrSetErrorCode(ERR_BAD_DNS_RESPONSE);
    return (ERR_BAD_DNS_RESPONSE);

}




static int      DNS_DecodeResponseMX(SYS_UINT8 * pRespData, char const * pszRespFile,
                        SYS_UINT32 * pTTL)
{

    SYS_UINT8      *pBaseData = pRespData;
    DNSQuery       *pDNSQ = (DNSQuery *) pRespData;

    if (pDNSQ->DNSH.RCode != 0)
    {
        ErrSetErrorCode(ERR_BAD_DNS_RESPONSE);
        return (ERR_BAD_DNS_RESPONSE);
    }

    pDNSQ->DNSH.QDCount = ntohs(pDNSQ->DNSH.QDCount);
    pDNSQ->DNSH.ANCount = ntohs(pDNSQ->DNSH.ANCount);
    pDNSQ->DNSH.NSCount = ntohs(pDNSQ->DNSH.NSCount);
    pDNSQ->DNSH.ARCount = ntohs(pDNSQ->DNSH.ARCount);

    if (pDNSQ->DNSH.ANCount == 0)
    {
        ErrSetErrorCode(ERR_EMPTY_DNS_RESPONSE);
        return (ERR_EMPTY_DNS_RESPONSE);
    }


    FILE           *pMXFile = fopen(pszRespFile, "wb");

    if (pMXFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    pRespData = pDNSQ->QueryData;

///////////////////////////////////////////////////////////////////////////////
//  Scan query data
///////////////////////////////////////////////////////////////////////////////
    int             ii;

    for (ii = 0; ii < (int) pDNSQ->DNSH.QDCount; ii++)
    {
        SYS_UINT16      Type = 0,
                        Class = 0;
        char            szInetName[MAX_HOST_NAME] = "";


        int             iQLenght = DNS_GetQuery(pBaseData, pRespData, szInetName, &Type, &Class);


        pRespData += iQLenght;
    }

///////////////////////////////////////////////////////////////////////////////
//  Scan answer data
///////////////////////////////////////////////////////////////////////////////
    int             iMXRecords = 0;
    SYS_UINT32      TTL = 0;

    for (ii = 0; ii < (int) pDNSQ->DNSH.ANCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;


        SYS_UINT8      *pMXData = RR.RespData;
        SYS_UINT16      Preference = ntohs(*((SYS_UINT16 *) pMXData));

        pMXData += sizeof(SYS_UINT16);

        char            szMXDomain[MAX_HOST_NAME] = "";

        DNS_GetName(pBaseData, pMXData, szMXDomain);

        if (ii == 0)
            fprintf(pMXFile, "%d:%s", (int) Preference, szMXDomain);
        else
            fprintf(pMXFile, ",%d:%s", (int) Preference, szMXDomain);

        if ((TTL == 0) || (RR.TTL < TTL))
            TTL = RR.TTL;

        ++iMXRecords;
    }

    fclose(pMXFile);

    if (pTTL != NULL)
        *pTTL = TTL;

    return (0);

}




static int      DNS_DecodeResponseNS(SYS_UINT8 * pRespData, char const * pszRespFile,
                        bool & bAuth, SYS_UINT32 * pTTL)
{

    SYS_UINT8      *pBaseData = pRespData;
    DNSQuery       *pDNSQ = (DNSQuery *) pRespData;

    if (pDNSQ->DNSH.RCode != 0)
    {
        ErrSetErrorCode(ERR_BAD_DNS_RESPONSE);
        return (ERR_BAD_DNS_RESPONSE);
    }


    FILE           *pNSFile = fopen(pszRespFile, "wt");

    if (pNSFile == NULL)
    {
        ErrSetErrorCode(ERR_FILE_CREATE);
        return (ERR_FILE_CREATE);
    }


    pDNSQ->DNSH.QDCount = ntohs(pDNSQ->DNSH.QDCount);
    pDNSQ->DNSH.ANCount = ntohs(pDNSQ->DNSH.ANCount);
    pDNSQ->DNSH.NSCount = ntohs(pDNSQ->DNSH.NSCount);
    pDNSQ->DNSH.ARCount = ntohs(pDNSQ->DNSH.ARCount);


    pRespData = pDNSQ->QueryData;

///////////////////////////////////////////////////////////////////////////////
//  Scan query data
///////////////////////////////////////////////////////////////////////////////
    int             ii;

    for (ii = 0; ii < (int) pDNSQ->DNSH.QDCount; ii++)
    {
        SYS_UINT16      Type = 0,
                        Class = 0;
        char            szInetName[MAX_HOST_NAME] = "";


        int             iQLenght = DNS_GetQuery(pBaseData, pRespData, szInetName, &Type, &Class);


        pRespData += iQLenght;
    }

///////////////////////////////////////////////////////////////////////////////
//  Scan answer data
///////////////////////////////////////////////////////////////////////////////
    int             iNSRecords = 0;
    SYS_UINT32      TTL = 0;

    for (ii = 0; ii < (int) pDNSQ->DNSH.ANCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;


        SYS_UINT8      *pNSData = RR.RespData;

        char            szNSName[MAX_HOST_NAME] = "";

        DNS_GetName(pBaseData, pNSData, szNSName);

        fprintf(pNSFile, "%s\n", szNSName);

        if ((TTL == 0) || (RR.TTL < TTL))
            TTL = RR.TTL;

        ++iNSRecords;
    }

///////////////////////////////////////////////////////////////////////////////
//  Got answers ?
///////////////////////////////////////////////////////////////////////////////
    if (iNSRecords > 0)
    {
        fclose(pNSFile);
        bAuth = true;
        if (pTTL != NULL)
            *pTTL = TTL;
        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Scan name servers data
///////////////////////////////////////////////////////////////////////////////
    for (ii = 0; ii < (int) pDNSQ->DNSH.NSCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;


        SYS_UINT8      *pNSData = RR.RespData;

        char            szNSName[MAX_HOST_NAME] = "";

        DNS_GetName(pBaseData, pNSData, szNSName);

        fprintf(pNSFile, "%s\n", szNSName);

        if ((TTL == 0) || (RR.TTL < TTL))
            TTL = RR.TTL;

        ++iNSRecords;
    }

///////////////////////////////////////////////////////////////////////////////
//  Got answers ?
///////////////////////////////////////////////////////////////////////////////
    if (iNSRecords > 0)
    {
        fclose(pNSFile);
        bAuth = false;
        if (pTTL != NULL)
            *pTTL = TTL;
        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Scan additional records data
///////////////////////////////////////////////////////////////////////////////
    for (ii = 0; ii < (int) pDNSQ->DNSH.ARCount; ii++)
    {
        DNSResourceRecord RR;


        int             iRRLenght = DNS_GetResourceRecord(pBaseData, pRespData, &RR);


        pRespData += iRRLenght;

    }

    fclose(pNSFile);

    ErrSetErrorCode(ERR_BAD_DNS_RESPONSE);
    return (ERR_BAD_DNS_RESPONSE);

}




static int      DNS_FindDomainMX(char const * pszDNSServer, char const * pszDomain,
                        HSLIST & hNameList, char const * pszRespFile, SYS_UINT32 * pTTL)
{

///////////////////////////////////////////////////////////////////////////////
//  Send query and read result
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT8      *pRespData = DNS_QueryExec(pszDNSServer, DNS_PORTNO, DNS_SOCKET_TIMEOUT,
            0, QTYPE_MX, pszDomain);

    if (pRespData == NULL)
        return (ErrGetErrorCode());

///////////////////////////////////////////////////////////////////////////////
//  Add this server to the visited list
///////////////////////////////////////////////////////////////////////////////
    DNS_AddNameNode(hNameList, pszDNSServer);

///////////////////////////////////////////////////////////////////////////////
//  Decode server response ( recursive )
///////////////////////////////////////////////////////////////////////////////
    int             iDecodeResult = DNS_DecodeResponseMX(pRespData, pszDomain,
            hNameList, pszRespFile, pTTL);


    SysFree(pRespData);

    return (iDecodeResult);

}



static int      DNS_QueryDomainMX(char const * pszDNSServer, char const * pszDomain,
                        char *&pszMXDomains, SYS_UINT32 * pTTL)
{

    HSLIST          hNameList;
    char            szMXFileName[SYS_MAX_PATH] = "";

    SysGetTmpFile(szMXFileName);

    ListInit(hNameList);


    if (DNS_FindDomainMX(pszDNSServer, pszDomain, hNameList, szMXFileName, pTTL) < 0)
    {
        ErrorPush();
        DNS_FreeNameList(hNameList);
        SysRemove(szMXFileName);
        return (ErrorPop());
    }

    DNS_FreeNameList(hNameList);


    FILE           *pMXFile = fopen(szMXFileName, "rb");

    if (pMXFile == NULL)
    {
        SysRemove(szMXFileName);

        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

    fseek(pMXFile, 0, SEEK_END);

    unsigned int    uFileSize = (unsigned int) ftell(pMXFile);

    if ((pszMXDomains = (char *) SysAlloc(uFileSize + 1)) == NULL)
    {
        fclose(pMXFile);
        SysRemove(szMXFileName);
        return (ErrGetErrorCode());
    }

    fseek(pMXFile, 0, SEEK_SET);

    fread(pszMXDomains, uFileSize, 1, pMXFile);
    pszMXDomains[uFileSize] = '\0';

    fclose(pMXFile);

    SysRemove(szMXFileName);

    return (0);

}




int             DNS_QueryNameServers(char const * pszDNSServer, char const * pszDomain,
                        char const * pszRespFile, bool & bAuth, SYS_UINT32 * pTTL)
{
///////////////////////////////////////////////////////////////////////////////
//  Send query and read result
///////////////////////////////////////////////////////////////////////////////
    SYS_UINT8      *pRespData = DNS_QueryExec(pszDNSServer, DNS_PORTNO, DNS_SOCKET_TIMEOUT,
            0, QTYPE_NS, pszDomain);

    if (pRespData == NULL)
        return (ErrGetErrorCode());


    if (DNS_DecodeResponseNS(pRespData, pszRespFile, bAuth, pTTL) < 0)
    {
        ErrorPush();
        SysFree(pRespData);

        return (ErrorPop());
    }


    SysFree(pRespData);

    return (0);

}





static int      DNS_GetNameServersLL(char const * pszDNSServer, char const * pszDomain,
                        char const * pszRespFile, HSLIST & hNameList, SYS_UINT32 * pTTL)
{

    char            szRespFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szRespFile);


    bool            bAuth = false;

    if (DNS_QueryNameServers(pszDNSServer, pszDomain, szRespFile, bAuth, pTTL) < 0)
    {
        ErrorPush();
        CheckRemoveFile(szRespFile);

        return (ErrorPop());
    }

    if (bAuth)
    {
        if (MscCopyFile(pszRespFile, szRespFile) < 0)
        {
            ErrorPush();
            SysRemove(szRespFile);

            return (ErrorPop());
        }

        SysRemove(szRespFile);

        return (0);
    }

///////////////////////////////////////////////////////////////////////////////
//  Add this server to the visited list
///////////////////////////////////////////////////////////////////////////////
    DNS_AddNameNode(hNameList, pszDNSServer);


    FILE           *pNSFile = fopen(szRespFile, "rt");

    if (pNSFile == NULL)
    {
        SysRemove(szRespFile);

        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

    char            szNS[MAX_HOST_NAME] = "";

    while (MscFGets(szNS, sizeof(szNS) - 1, pNSFile) != NULL)
    {
        if ((DNS_GetNameNode(hNameList, szNS) == NULL) &&
                (DNS_GetNameServersLL(szNS, pszDomain, pszRespFile, hNameList, pTTL) == 0))
        {
            fclose(pNSFile);
            SysRemove(szRespFile);

            return (0);
        }
    }

    fclose(pNSFile);

    SysRemove(szRespFile);

    ErrSetErrorCode(ERR_NS_NOT_FOUND);
    return (ERR_NS_NOT_FOUND);

}




int             DNS_GetNameServers(char const * pszDNSServer, char const * pszDomain,
                        char const * pszRespFile, SYS_UINT32 * pTTL)
{

    HSLIST          hNameList;

    ListInit(hNameList);


    int             iQueryResult = DNS_GetNameServersLL(pszDNSServer, pszDomain,
            pszRespFile, hNameList, pTTL);


    DNS_FreeNameList(hNameList);

    return (iQueryResult);

}




int             DNS_DomainNameServers(char const * pszDomain, char const * pszRespFile,
                        SYS_UINT32 * pTTL)
{

    char            szRootsFile[SYS_MAX_PATH] = "",
                    szRespFile[SYS_MAX_PATH] = "";

    DNS_GetRootsFile(szRootsFile);
    strcpy(szRespFile, szRootsFile);


    char          **ppszDomains = StrTokenize(pszDomain, ". \t");

    if (ppszDomains == NULL)
        return (ErrGetErrorCode());

    int             iSubDomains = StrStringsCount(ppszDomains);
    char            szPrevDomain[MAX_HOST_NAME] = "";

    for (--iSubDomains; iSubDomains >= 0; iSubDomains--)
    {
        char            szCurrDomain[MAX_HOST_NAME] = "";

        sprintf(szCurrDomain, "%s.%s", ppszDomains[iSubDomains], szPrevDomain);
        strcpy(szPrevDomain, szCurrDomain);


        FILE           *pNSFile = fopen(szRespFile, "rt");

        if (pNSFile == NULL)
        {
            StrFreeStrings(ppszDomains);
            if (strcmp(szRespFile, szRootsFile) != 0)
                SysRemove(szRespFile);

            ErrSetErrorCode(ERR_FILE_OPEN);
            return (ERR_FILE_OPEN);
        }

        char            szRespFile2[SYS_MAX_PATH] = "";

        SysGetTmpFile(szRespFile2);


        int             iNSGetResult = -1;
        char            szNS[MAX_HOST_NAME] = "";

        while (MscFGets(szNS, sizeof(szNS) - 1, pNSFile) != NULL)
        {

            if ((iNSGetResult = DNS_GetNameServers(szNS, szCurrDomain, szRespFile2, pTTL)) == 0)
                break;

        }

        fclose(pNSFile);

        if (iNSGetResult < 0)
        {
            SysRemove(szRespFile2);

            break;
        }

        if (strcmp(szRespFile, szRootsFile) != 0)
            SysRemove(szRespFile);

        strcpy(szRespFile, szRespFile2);
    }

    StrFreeStrings(ppszDomains);

    if (MscCopyFile(pszRespFile, szRespFile) < 0)
    {
        ErrorPush();
        if (strcmp(szRespFile, szRootsFile) != 0)
            SysRemove(szRespFile);

        return (ErrorPop());
    }

    if (strcmp(szRespFile, szRootsFile) != 0)
        SysRemove(szRespFile);

    return (0);

}




static char    *DNS_GetRootsFile(char *pszRootsFilePath)
{

    CfgGetRootPath(pszRootsFilePath);

    strcat(pszRootsFilePath, ROOTS_FILE);

    return (pszRootsFilePath);

}




int             DNS_GetRoots(char const * pszDNSServer, char const * pszRespFile)
{

    return (DNS_GetNameServers(pszDNSServer, ".", pszRespFile));

}




int             DNS_GetDomainMX(char const * pszDomain, char *&pszMXDomains, SYS_UINT32 * pTTL)
{

    char            szRespFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szRespFile);

    if (DNS_DomainNameServers(pszDomain, szRespFile) < 0)
    {
        ErrorPush();
        SysRemove(szRespFile);

        return (ErrorPop());
    }


    FILE           *pNSFile = fopen(szRespFile, "rt");

    if (pNSFile == NULL)
    {
        SysRemove(szRespFile);

        ErrSetErrorCode(ERR_FILE_OPEN);
        return (ERR_FILE_OPEN);
    }

    char            szNS[MAX_HOST_NAME] = "";

    while (MscFGets(szNS, sizeof(szNS) - 1, pNSFile) != NULL)
    {
        if (DNS_QueryDomainMX(szNS, pszDomain, pszMXDomains, pTTL) == 0)
        {
            fclose(pNSFile);
            SysRemove(szRespFile);

            return (0);
        }
    }

    fclose(pNSFile);

    SysRemove(szRespFile);

    ErrSetErrorCode(ERR_NO_DEFINED_MXS_FOR_DOMAIN);
    return (ERR_NO_DEFINED_MXS_FOR_DOMAIN);

}




int             DNS_GetDomainMXDirect(char const * pszDNSServer, char const * pszDomain,
                        int iQuerySockType, char *&pszMXDomains, SYS_UINT32 * pTTL)
{

///////////////////////////////////////////////////////////////////////////////
//  Setup DNS MX query with recursion requested
///////////////////////////////////////////////////////////////////////////////
    int             iQueryLenght;
    DNSQuery        DNSQ;

    if (DNS_RequestSetup(DNSQ, 0, QTYPE_MX, pszDomain, iQueryLenght, true) < 0)
        return (ErrGetErrorCode());


    SYS_UINT8      *pRespData = NULL;

    switch (iQuerySockType)
    {
        case (DNS_QUERY_TCP):
            {

                if ((pRespData = DNS_QuerySendStream(pszDNSServer, DNS_PORTNO, DNS_SOCKET_TIMEOUT,
                                        DNSQ, iQueryLenght)) == NULL)
                    return (ErrGetErrorCode());

            }
            break;

        case (DNS_QUERY_UDP):
        default:
            {
///////////////////////////////////////////////////////////////////////////////
//  Try needed UDP query first, if it's truncated switch to TCP query
///////////////////////////////////////////////////////////////////////////////
                bool            bTruncated = false;

                if (((pRespData = DNS_QuerySendDGram(pszDNSServer, DNS_PORTNO, DNS_SOCKET_TIMEOUT,
                                                DNSQ, iQueryLenght, bTruncated)) == NULL) && bTruncated)
                    pRespData = DNS_QuerySendStream(pszDNSServer, DNS_PORTNO, DNS_SOCKET_TIMEOUT,
                            DNSQ, iQueryLenght);

                if (pRespData == NULL)
                    return (ErrGetErrorCode());

            }
            break;
    }


    char            szRespFile[SYS_MAX_PATH] = "";

    SysGetTmpFile(szRespFile);

    if (DNS_DecodeResponseMX(pRespData, szRespFile, pTTL) < 0)
    {
        ErrorPush();
        SysFree(pRespData);
        CheckRemoveFile(szRespFile);

        return (ErrorPop());
    }

    SysFree(pRespData);


    FILE           *pMXFile = fopen(szRespFile, "rb");

    if (pMXFile == NULL)
    {
        ErrorPush();
        SysRemove(szRespFile);

        return (ErrorPop());
    }

    fseek(pMXFile, 0, SEEK_END);

    unsigned int    uFileSize = (unsigned int) ftell(pMXFile);

    if ((pszMXDomains = (char *) SysAlloc(uFileSize + 1)) == NULL)
    {
        ErrorPush();
        fclose(pMXFile);
        SysRemove(szRespFile);

        return (ErrorPop());
    }

    fseek(pMXFile, 0, SEEK_SET);

    fread(pszMXDomains, uFileSize, 1, pMXFile);
    pszMXDomains[uFileSize] = '\0';

    fclose(pMXFile);
    SysRemove(szRespFile);

    return (0);

}
