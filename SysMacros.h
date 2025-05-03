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


#ifndef _SYSMACROS_H
#define _SYSMACROS_H





#define SRand()                 srand((unsigned int) (SysMsTime() * SysGetCurrentThreadId()))
#define NbrCeil(n, a)           ((((n) + (a) - 1) / (a)) * (a))
#define NbrFloor(n, a)          (((n) / (a)) * (a))
#define Sign(v)                 (((v) < 0) ? -1: +1)
#define Min(a, b)               (((a) < (b)) ? (a): (b))
#define Max(a, b)               (((a) > (b)) ? (a): (b))
#define Abs(v)                  (((v) > 0) ? (v): -(v))
#define INext(i, n)             ((((i) + 1) < (n)) ? ((i) + 1): 0)
#define IPrev(i, n)             (((i) > 0) ? ((i) - 1): ((n) - 1))
#define LIndex2D(i, j, n)       ((i) * (n) + (j))
#define ZeroData(d)             memset(&(d), 0, sizeof(d))
#define CountOf(t)              (sizeof(t) / sizeof((t)[0]))
#define SetEmptyString(s)       (s)[0] = '\0'
#define CharISame(a, b)         (tolower(a) == tolower(b))
#define StrINComp(s, t)         strnicmp(s, t, strlen(t))
#define StrNComp(s, t)          strncmp(s, t, strlen(t))
#define StrNCpy(t, s, n)        do { strncpy(t, s, n); (t)[(n) - 1] = '\0'; } while (0)
#define StrSNCpy(t, s)          StrNCpy(t, s, sizeof(t))
#define StrAppend(s)            ((char *) (s) + strlen(s))
#define CheckRemoveFile(fp)     ((SysExistFile(fp)) ? SysRemove(fp) : 0)
#define ErrorPush()             int iPushedError = ErrGetErrorCode();
#define ErrorPop()              iPushedError
#define ErrorFetch()            iPushedError
#define SysFreeCheck(p)         do { if ((p) != NULL) SysFree(p), (p) = NULL; } while(0)
#define IsDotFilename(f)        ((f)[0] == '.')






///////////////////////////////////////////////////////////////////////////////
//  Inline functions
///////////////////////////////////////////////////////////////////////////////

inline char    *AppendSlash(char *pszPath)
{

    int             iPathLength = strlen(pszPath);

    if ((iPathLength == 0) || (pszPath[iPathLength - 1] != SYS_SLASH_CHAR))
        strcat(pszPath, SYS_SLASH_STR);

    return (pszPath);

};




inline bool     IsPrimeNumber(int iNumber)
{

    if (iNumber > 3)
    {
        if (iNumber & 1)
        {
            int             iHalfNumber = iNumber / 2;

            for (int ii = 3; ii < iHalfNumber; ii += 2)
                if ((iNumber % ii) == 0)
                    return (false);
        }
        else
            return (false);
    }

    return (true);

};




#endif
