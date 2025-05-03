Summary: Advanced, fast and reliable ESMTP/POP3 mail server
Name: xmail
Version: 1.6
Release: 1
Copyright: GPL
Group: System Environment/Daemons
Source: http://www.xmailserver.org/xmail-%{PACKAGE_VERSION}.tar.gz
URL: http://www.xmailserver.org
Packager: Davide Libenzi <davidel@xmailserver.org>
BuildRoot: /var/tmp/xmail-%{PACKAGE_VERSION}
Requires: glibc


%description
XMail is an Internet and intranet mail server featuring an SMTP server, POP3 server,
finger server, multiple domains, no need for users to have a real system account,
SMTP relay checking, RBL/RSS/ORBS/DUL and custom ( IP and address based ) spam protection,
SMTP authentication ( PLAIN LOGIN CRAM-MD5 POP3-before-SMTP and custom ),
POP3 mail fecthing of external POP3 accounts, account aliases, domain
aliases, custom mail processing, direct mail files delivery, custom mail filters,
mailing lists, remote administration, custom mail exchangers, logging, and multi-platform code.
XMail sources compile under GNU/Linux, FreeBSD, Solaris and NT/2000/XP.


%prep

%setup
make -f Makefile.lnx clean

%build
make -f Makefile.lnx


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/var/MailRoot/bin
cp -R MailRoot $RPM_BUILD_ROOT/var/MailRoot.sample

install -m 755 XMail $RPM_BUILD_ROOT/var/MailRoot/bin/XMail
install -m 755 XMCrypt $RPM_BUILD_ROOT/var/MailRoot/bin/XMCrypt
install -m 755 CtrlClnt $RPM_BUILD_ROOT/var/MailRoot/bin/CtrlClnt
install -m 755 MkUsers $RPM_BUILD_ROOT/var/MailRoot/bin/MkUsers
install -m 755 sendmail $RPM_BUILD_ROOT/var/MailRoot/bin/sendmail
install -m 755 sendmail.sh $RPM_BUILD_ROOT/var/MailRoot/bin/sendmail.sh

install -m 644 Readme.txt $RPM_BUILD_ROOT/var/MailRoot/bin/Readme.txt

mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc0.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc1.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc2.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc3.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc4.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc5.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc6.d

install -m 755 xmail $RPM_BUILD_ROOT/etc/rc.d/init.d/xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc0.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc1.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc2.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc6.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc3.d/S90xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc4.d/S90xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc5.d/S90xmail


%clean
rm -rf $RPM_BUILD_ROOT


%pre
if [ -f /etc/rc.d/init.d/xmail ]
then
    /etc/rc.d/init.d/xmail stop
fi


%post
if [ ! -f /var/MailRoot/server.tab ]
then
    cp -R /var/MailRoot.sample/* /var/MailRoot
fi

/etc/rc.d/init.d/xmail start


%preun
if [ -f /etc/rc.d/init.d/xmail ]
then
    /etc/rc.d/init.d/xmail stop
fi


%postun


%files
/var/MailRoot/bin/XMail
/var/MailRoot/bin/XMCrypt
/var/MailRoot/bin/CtrlClnt
/var/MailRoot/bin/MkUsers
/var/MailRoot/bin/sendmail
/var/MailRoot/bin/sendmail.sh

/var/MailRoot/bin/Readme.txt

/var/MailRoot.sample

/etc/rc.d/init.d/xmail
/etc/rc.d/rc0.d/K10xmail
/etc/rc.d/rc1.d/K10xmail
/etc/rc.d/rc2.d/K10xmail
/etc/rc.d/rc6.d/K10xmail
/etc/rc.d/rc3.d/S90xmail
/etc/rc.d/rc4.d/S90xmail
/etc/rc.d/rc5.d/S90xmail


%changelog

* Sun Mar 03 2002 Davide Libenzi <davidel@xmailserver.org>
    Added a new USER.TAB variable "UseReplyTo" ( default 1 ) to make it possible to disable the emission
    of the Reply-To: header for mailing lists.
    Fixed a bug that caused XMail to uncorrectly deliver POP3 fetched messages when used togheter with
    domain masquerading.
    Changed index file structure to use an hash table for faster lookups and index rebuilding.
    New files inside the  tabindex  directory now have the extension  .hdx  and old  .idx  files can be removed.
    Added X-Deliver-To: header to messages redirected with MAILPROC.TAB file.
    Added configurable Received: tag option in SERVER.TAB by using the variable "ReceivedHdrType".
    Added a configurable list of header tags to be used to extract addresses for POP3 fetched messages
    by using the SERVER.TAB variable "FetchHdrTags".
    History ( change log ) entries have been moved from the main documentation file and a new file ( ChangeLog.txt )
    has been created to store change-log entries.
    Removed RBL-MAPSCheck ( currently blackholes.mail-abuse.org. ), RSS-MAPSCheck ( currently relays.mail-abuse.org. )
    and DUL-MAPSCheck ( currently dialups.mail-abuse.org. ) specific variables and now everything must
    be handled with CustMapsList ( please look at the documentation ).
    Added  NotifyMsgLinesExtra  SERVER.TAB variable to specify the number of lines of the bounced message
    to include inside the notify reply ( default zero, that means only header ).
    The message log file is now listed inside the notification message sent to  ErrorsAdmin  ( or  PostMaster  ).
    Added  NotifySendLogToSender  SERVER.TAB variable to enable/disable the send of the message log file
    inside the notify message to the sender ( default is off ).
    Added  TempErrorsAdmin  SERVER.TAB variable to specify an account that will receive temporary delivery
    failures notifications ( default is empty ).
    Added a new SERVER.TAB variable  NotifyTryPattern  to specify at which delivery attempt failure
    the system has to send the notification message.
    Fixed a bug that caused alias domains to have higher priority lookup compared to standard domains.

* Tue Feb 05 2002 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug in wildcard aliases domain lookup.
    Fixed a bug in CTRL command "aliasdel" that failed to remove aliases with wildcard domains.
    Fixed a bug that caused XMail to timeout on very slow network connections.

* Fri Jan 18 2002 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug that made XMail to fail to parse custom maps lists in SERVER.TAB.
    Fixed a bug that prevented XMail to add wildcard-domain aliases.
    Added a filter feature to the CTRL commands "domainlist" and "aliasdomainlist".
    Added an extra message header field "X-AuthUser:" to log the username used by the account to send the message.
    Added Reply-To: RFC822 header for mailing lists sends.
    Fixed a Win32 subsystem API to let XMail to correctly handle network shared MAIL_ROOTs.

* Wed Dec 19 2001 Davide Libenzi <davidel@xmailserver.org>
    ORBS maps test removed due old ORBS dead, the SERVER.TAB variable "CustMapsList"
    can be used to setup new ORBS ( and other ) maps. Fixed a bug in XMail's  sendmail
    that was introduced in version 1.2 and made it to incorrectly interpret command line
    parameters. Fixed a bug that made XMail to not correctly recognize user type
    characters when lowercase. Fixed a bug that caused XMail to not start is the MAIL_ROOT
    environment variable had a final slash on Windows. Added a new filter return code ( 97 )
    to reject messages without notification and without frozen processing. Added two new
    command line options  -MR  and  -MS  to set the I/O socket buffers sizes in bytes
    ( do not use them if You don't know what You're doing ). Changed system library to have
    a better performace, expecially on the Windows platform. Users that are using XMail
    mainly inside their local LAN are strongly encouraged to switch to this version.
    Fixed a bug that enabled insertion of aliases that overlapped real accounts.

* Mon Nov 12 2001 Davide Libenzi <davidel@xmailserver.org>
    A problem with log file names generation has been fixed.
    Added a new CTRL command "userstat".
    Implemented Linux/SPARC port and relative makefile ( Makefile.slx ).
    Extended the XMail version of  sendmail  to support a filename as input ( both XMail format that
    raw email format ) and to accept a filename as recipient list.
    Added a new kind of aliases named "cmdaliases" that implements a sort of custom domains commands
    on a per-user basis ( look at the  CmdAliases  section ).
    ************************************************************************************************
    * You've to create the directory "cmdaliases" inside $MAIL_ROOT to have 1.2 working correctly
    ************************************************************************************************	
    Fixed a bug that had XMail to not check for the user variable SmtpPerms with CRAM-MD5 authetication.
    Fixed a bug in the XMail's  sendmail  implementation that made it unable to detect the "."
    end of message condition.
    Fixed a bug in the XMail's  sendmail  implementation that made it to skip cascaded command line
    parameters ( -Ooet ).
    Implemented a new XMail's  sendmail  switch -i to relax the <CR><LF>.<CR><LF> ond of message indicator.
* Tue Oct 10 2001 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug in the XMail version of  sendmail  that made messages to be double sent.
    The macro @@TMPFILE has been removed from filters coz it's useless.
    The command line parameter  -Lt NSEC  has been added to set the sleep timeout of LMAIL threads.
    Added domain aliasing ( see ALIASDOMAIN.TAB section ).
    ************************************************************************************************
    * You've to create the file ALIASDOMAIN.TAB inside $MAIL_ROOT ( even if empty )
    ************************************************************************************************	
    Added CTRL commands "aliasdomainadd", "aliasdomaindel" and "aliasdomainlist" to handle domain aliases
    through the CTRL protocol.
* Tue Sep 4 2001 Davide Libenzi <davidel@xmailserver.org>
    Added wildcard matching in the domain part of ALIASES.TAB ( see ALIASES.TAB section ).
    Changed the PSYNC scheduling behaviour to allow sync interval equal to zero ( disabled ) and
    let the file .psync-trigger to schedule syncs.
    Solaris on Intel support added.
    A new filter return code ( 98 ) has been added to give the ability to reject message without notify the sender.
    It's finally time, after about 70 releases, to go 1.0 !!
* Mon Jul 2 2001 Davide Libenzi <davidel@xmailserver.org>
    A stack shifting call method has been implemented to make virtually impossible for attackers
    to guess the stack frame pointer.
    With this new feature, even if buffer overflows are present, the worst thing that could happen
    is a server crash and not the attacker that execute root code on the server machine.
    Implemented the SIZE ESMTP extension and introduced a new SERVER.TAB variable "MaxMessageSize" that set
    the maximum message size that the server will accept ( in Kb ).
    If this variable is not set or if it's zero, any message will be accepted.
    A new SMTP authentication permission ( 'Z' ) has been added to allow authenticated users to bypass the check.
    The SMTP sender now check for the remote support of the SIZE ESMTP extension.
    A new SERVER.TAB variable has been added  "CustMapsList"  to enable the user to enter custom maps checking
    ( look at the section "SERVER.TAB variables" ).
    Fixed a bug in "frozdel" CTRL command.
* Sun Jun 10 2001 Davide Libenzi <davidel@xmailserver.org>
* Fri Jun 8 2001 Davide Libenzi <davidel@xmailserver.org>
    Fixed a possible buffer overflow bug inside the DNS resolver.
* Tue May 29 2001 Davide Libenzi <davidel@xmailserver.org>
	Fixed build errors in MkUsers.cpp and SendMail.cpp ( FreeBSD version ).
	Added the ability to specify a list of matching domains when using PSYNC with
	masquerading domains ( see POP3LINKS.TAB section ).
	The auxiliary program  sendmail  now read the MAIL_ROOT environment from
	registry ( Win32 version ) and if it fails it reads from the environment.
	Fixed a bug that made XMail to crash if the first line of ALIASES.TAB was empty.
	RPM packaging added.
    Added a new feature to the custom domain commands "redirect" and "lredirect"
    that will accept email addresses as redirection target.
    Fixed a bug in MkUsers.
    Added system resource checking before accepting SMTP connections
    (see "SmtpMinDiskSpace" and "SmtpMinVirtMemSpace" SERVER.TAB variables ).
    Added system resource checking before accepting POP3 connections
    ( see "Pop3MinVirtMemSpace" SERVER.TAB variable ).
    A new command line param -t has been implemented in sendmail.
    A new USER.TAB variable "SmtpPerms"  has been added	to enable account based SMTP permissions.
    If "SmtpPerms" is not found the SERVER.TAB variable "DefaultSmtpPerms" is checked.
    A new USER.TAB variable "ReceiveEnable" has been added to enable/disable the account from receiving emails.
    A new USER.TAB variable "PopEnable" has been added to enable/disable the account from fetching emails.

