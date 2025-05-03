Summary: Advanced, fast and reliable ESMTP/POP3 mail server
Name: xmail
Version: 0.73
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
POP3 mail fecthing of external POP3 accounts, aliases, custom mail processing,
direct mail files delivery, custom mail filters, mailing lists, remote administration,
custom mail exchangers, logging, and multi-platform code.
XMail sources compile under GNU/Linux, FreeBSD, Solaris and NT.


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

