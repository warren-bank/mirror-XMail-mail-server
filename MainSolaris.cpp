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
#include "AppDefines.h"
#include "MailSvr.h"






#define RUNNING_PIDS_DIR            "/var/run"

#if !defined(NOFILE)
#define NOFILE                      64
#endif

#define XMAIL_DEBUG_OPTION          "-Md"








static int      MLnxEventLog(char const * pszFormat,...);
static int      MLnxSavePID(void);
static int      MLnxRemovePID(void);
static void     MLnxSIGCLD(int iSignal);
static int      MLnxDaemonBootStrap(void);
static int      MLnxIsDebugStartup(int iArgCount, char *pszArgs[]);
static int      MLnxDaemonStartup(int iArgCount, char *pszArgs[]);







static int      MLnxEventLog(char const * pszFormat,...)
{

    openlog(APP_NAME_STR, LOG_PID, LOG_DAEMON);


    va_list         Args;

    va_start(Args, pszFormat);

    char            szBuffer[2048] = "";

    vsnprintf(szBuffer, sizeof(szBuffer) - 1, pszFormat, Args);

    syslog(LOG_ERR, "%s", szBuffer);

    va_end(Args);


    closelog();

    return (0);

}




static int      MLnxSavePID(void)
{

    char            szPidFile[SYS_MAX_PATH] = "";

    sprintf(szPidFile, "%s/%s.pid", RUNNING_PIDS_DIR, APP_NAME_STR);

    FILE           *pFile = fopen(szPidFile, "w");

    if (pFile == NULL)
    {
        perror(szPidFile);
        return (-errno);
    }

    fprintf(pFile, "%u", (unsigned int) getpid());

    fclose(pFile);

    return (0);

}




static int      MLnxRemovePID(void)
{

    char            szPidFile[SYS_MAX_PATH] = "";

    sprintf(szPidFile, "%s/%s.pid", RUNNING_PIDS_DIR, APP_NAME_STR);

    if (unlink(szPidFile) != 0)
    {
        perror(szPidFile);
        return (-errno);
    }

    return (0);

}



static void     MLnxSIGCLD(int iSignal)
{
///////////////////////////////////////////////////////////////////////////////
//  For BSD
///////////////////////////////////////////////////////////////////////////////
#ifdef __BSD__

    int             iDeadPID;
    union wait      ExitStatus;

    while ((iDeadPID = wait3(&ExitStatus, WNOHANG, (struct rusage *) NULL)) > 0)
        ;

#endif

}




static int      MLnxDaemonBootStrap(void)
{
///////////////////////////////////////////////////////////////////////////////
//  This code is inspired from the code of the great Richard Stevens books.
//  May You RIP in programmers paradise great Richard.
//  I suggest You to buy all his collection, soon !
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//  For BSD
///////////////////////////////////////////////////////////////////////////////
#ifdef SIGTTOU
    signal(SIGTTOU, SIG_IGN);
#endif
#ifdef SIGTTIN
    signal(SIGTTIN, SIG_IGN);
#endif
#ifdef SIGTSTP
    signal(SIGTSTP, SIG_IGN);
#endif

///////////////////////////////////////////////////////////////////////////////
//  1st fork
///////////////////////////////////////////////////////////////////////////////
    int             iChildPID = fork();

    if (iChildPID < 0)
    {
        MLnxEventLog("Cannot fork : %s", strerror(errno));

        exit(errno);
    }
    else if (iChildPID > 0)
        exit(0);


///////////////////////////////////////////////////////////////////////////////
//  Disassociate from controlling terminal and process group. Ensure the process
//  can't reacquire a new controlling terminal.
///////////////////////////////////////////////////////////////////////////////

#ifdef __BSD__
///////////////////////////////////////////////////////////////////////////////
//  BSD
///////////////////////////////////////////////////////////////////////////////
    if (setpgrp(0, getpid()) == -1)
    {
        MLnxEventLog("Can't change process group : %s", strerror(errno));

        exit(errno);
    }

///////////////////////////////////////////////////////////////////////////////
//  Lose controlling tty
///////////////////////////////////////////////////////////////////////////////
    int             iFdTty = open("/dev/tty", O_RDWR);

    if (iFdTty >= 0)
    {
        ioctl(iFdTty, TIOCNOTTY, (char *) NULL);
        close(iFdTty);
    }

#else
///////////////////////////////////////////////////////////////////////////////
//  System V
///////////////////////////////////////////////////////////////////////////////
    if (setpgrp() == -1)
    {
        MLnxEventLog("Can't change process group : %s", strerror(errno));

        exit(errno);
    }

    signal(SIGHUP, SIG_IGN);


///////////////////////////////////////////////////////////////////////////////
//  2nd fork
///////////////////////////////////////////////////////////////////////////////
    iChildPID = fork();

    if (iChildPID < 0)
    {
        MLnxEventLog("Cannot fork : %s", strerror(errno));

        exit(errno);
    }
    else if (iChildPID > 0)
        exit(0);

#endif

///////////////////////////////////////////////////////////////////////////////
//  Close open file descriptors
///////////////////////////////////////////////////////////////////////////////
    for (int fd = 0; fd < NOFILE; fd++)
        close(fd);

///////////////////////////////////////////////////////////////////////////////
//  Probably got set to EBADF from a close
///////////////////////////////////////////////////////////////////////////////
    errno = 0;

///////////////////////////////////////////////////////////////////////////////
//  Move the current directory to root, to make sure we aren't on a mounted
//  filesystem.
///////////////////////////////////////////////////////////////////////////////
    chdir("/");

///////////////////////////////////////////////////////////////////////////////
//  Clear any inherited file mode creation mask.
///////////////////////////////////////////////////////////////////////////////
    umask(0);

///////////////////////////////////////////////////////////////////////////////
//  Ignore childs dead.
///////////////////////////////////////////////////////////////////////////////

#ifdef __BSD__
///////////////////////////////////////////////////////////////////////////////
//  BSD
///////////////////////////////////////////////////////////////////////////////
    signal(SIGCLD, MLnxSIGCLD);

#else
///////////////////////////////////////////////////////////////////////////////
//  System V
///////////////////////////////////////////////////////////////////////////////
    signal(SIGCLD, SIG_IGN);

#endif



    return (0);

}



static int      MLnxIsDebugStartup(int iArgCount, char *pszArgs[])
{

    for (int ii = 0; ii < iArgCount; ii++)
        if (strcmp(pszArgs[ii], XMAIL_DEBUG_OPTION) == 0)
            return (1);

    return (0);

}



static int      MLnxDaemonStartup(int iArgCount, char *pszArgs[])
{

///////////////////////////////////////////////////////////////////////////////
//  Daemon bootstrap code if We're not in debug mode
///////////////////////////////////////////////////////////////////////////////
    if (!MLnxIsDebugStartup(iArgCount, pszArgs))
        MLnxDaemonBootStrap();

///////////////////////////////////////////////////////////////////////////////
//  Create PID file
///////////////////////////////////////////////////////////////////////////////
    MLnxSavePID();


    int             iServerResult = SvrMain(iArgCount, pszArgs);


///////////////////////////////////////////////////////////////////////////////
//  Remove PID file
///////////////////////////////////////////////////////////////////////////////
    MLnxRemovePID();


    return (iServerResult);

}




int             main(int iArgCount, char *pszArgs[])
{

    return (MLnxDaemonStartup(iArgCount, pszArgs));

}
