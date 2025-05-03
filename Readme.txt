
            < XMail Server >

Version      : 1.10
Release type : Gnu Public License   http://www.gnu.org
Date         : 15-06-2002
Project by   : Davide Libenzi <davidel@xmailserver.org> http://www.xmailserver.org/
Credits      :
             : Michael Hartle <mhartle@hartle-klug.com>
             : Shawn Anderson <sanderson@eye-catcher.com>
             : Dick van der Kaaden <dick@netrex.nl>




***************************************************************************************
*                   WARNING
*   If You're upgrading an existing version of XMail it's strongly suggested
*   to read all the ChangeLog.txt notes that range from existing version to the new one
***************************************************************************************













Part 0          License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA



















Part 1          Overview

    This server born due to the need of having a free and stable Mail Server
    to be used inside my old company, which used a Windows Network.
    I don't like to reinvent the wheel but the need of some special features
    drive me to start a new project. Probably if I could use a Linux server
    on my net, I would be able to satisfy my needs without write code, but
    this is not my case. It should be also portable to other OSs, like Linux
    and other Unixes.
    Another reason that drove me to write XMail is the presence of the same steps
    in setting up a typical mail server, ie :
    sendmail + qpopper + fetchmail
    if one needs SMTP, POP3 and external syncronization, or :
    sendmail + qpopper
    for only SMTP and POP3 ( I've quoted sendmail, qpopper and fetchmail, but there
    are many other packages You can use to reach these needs ).
    With XMail You get an all-in-one package with a central administration that can
    simplify the above common steps.
    The first code of XMail Server is started on Windows NT and Linux, and
    now, the FreeBSD and Solaris version ready. The compilers supported are gcc for
    Linux, FreeBSD and Solaris and M$ Visual C++ for NT/2K.

















Part 2          Features

    1) ESMTP server
    2) POP3 server
    3) Finger server
    4) Multiple domains
    5) Users don't need a real system account
    6) SMTP relay checking
    7) SMTP RBL maps check (rbl.maps.vix.com)
    8) SMTP RSS maps check (relays.mail-abuse.org)
    9) SMTP ORBS relay check (relays.orbs.org)
    10) SMTP DUL map check (dialups.mail-abuse.org)
    11) SMTP protection over spammers ( IP based and address based )
    12) SMTP authentication ( PLAIN LOGIN CRAM-MD5 POP3/SMTP and custom )
    13) SMTP ETRN command support
    14) POP3 account syncronizer with external POP3 accounts
    15) Account aliasing
    16) Domain aliasing
    17) Mailing lists
    18) Custom mail processing
    19) Locally generated mail files delivery
    20) Remote administration
    21) Custom mail exchangers
    22) Logging
    23) Multi platform
    24) Domain message filters
    25) Custom ( external ) POP3 authentication












Part 3          Porting status

    Right now the Linux and NT ports are stable, while the Solaris and FreeBSD ones have
    not been tested like the previous OSs.



    















Part 4          Requirements

    Any version of Linux that has glibc.
    Windows NT with ws2_32.dll correctly installed.
    A working DNS and gateway to the internet ( if You plan to use it ).
    To build for Linux You need any version of gcc and glibc installed.
    To build for Windows You need MS Visual C++ ( for which I give the project ) 
    or any other working compiler that give support for Win32 SDK.


















Part 5          Getting sources

    Always get the latest sources at the XMail home page http://www.xmailserver.org/
    coz You're maybe using an old version.
    Use the correct distribution for Your system and don't mix Unix files with
    Windows ones coz this is one of the most common cause of XMail bad behaviour.
    When You unzip the package You've to check that the MailRoot directory contained inside
    the package itself is complete ( look at the directory tree listed below ) coz
    some unzippers don't restore empty directories. 





















Part 6          Build

    In Windows NT I give You a project that can be loaded from Visual C++ while in *nixes :

    # make -f Makefile.lnx      ( Linux )
    # make -f Makefile.slx      ( Linux on SPARC )
    # make -f Makefile.plx      ( Linux on PPC )
    # gmake -f Makefile.bsd     ( FreeBSD - You need GCC and GMAKE to build on FreeBSD )
    # make -f Makefile.sso      ( Sun/Solaris on SPARC - You need GCC to build on Solaris )
    # make -f Makefile.ssx      ( Sun/Solaris on Intel - You need GCC to build on Solaris )

    will build XMail and tools executables.
    As soon as the project reach a higher maturity I plan to supply a configure script.
    Under Linux an init.d startup script is supplied ( xmail ) to allow You to run
    XMail as a standard rc? daemon. You must put it into /etc/init.d ( it depends on which
    distro You're using ) directory and then create K??xmail - S??xmail links into the
    proper directories.
    Under Windows NT/2000/XP the XMail's executable is a Win32 service by default and if
    You want to have it built like a standard executable You've to comment the statement
    "#define SERVICE" in MainWin.cpp
    When it's built as a service ( default ) You can run :

    XMail --install

    to install XMail as a manual startup service or :

    XMail --install-auto

    to install XMail as an automatic startup service.
    If You run  --install  and You want XMail to run at NT boot You must go in
    ControlPanel->Services and edit the startup options of XMail.
    Once You have the service version of XMail You can run it in a "normal" way by
    executing :

    XMail --debug [options]














Part 7          Configuration

    [ Linux/Solaris/FreeBSD ]

    1) Build XMail
    2) Log as root
    3) Copy the supplied MailRoot directory where You want it resides ( I suppose  /var )
    4) Do a   # chmod 700 /var/MailRoot   to setup MailRoot directory access rights
    5) Strip XMail executables if You want to reduce its sizes ( strip filename )
    6) Copy XMail executables to  /var/MailRoot/bin
    7) If You've  inetd  installed You must comment out the lines of  /etc/inetd.conf
        that involves SMTP POP3 Finger and restart  inetd  ( kill -HUP ... )
    8) Since XMail use syslog to log messages, enable  syslogd  if it's not running
    9) Setup  SERVER.TAB  configuration option after a well read to the rest of this doc
    10) Add Your users and domains ( after a well read to the rest of this doc )
    11) Change or comment out ( # ) the example account in  ctrlaccounts.tab  by using not
        trivial username and password
    12) Copy  xmail  startup script to Your init.d directory ( it's position depends on Your distro ).
        If You've setup XMail to work in a subdirectory other then  /var/MailRoot  You
        must edit  xmail  startup script to customize its boot
    13) Use the  sysv_inst.sh  shell script ( from root user ) to create SysV boot script
    14) To start XMail without reboot You can run ( from root ):
            /etc/rc.d/init.d/xmail start
        otherwise reboot Your machine
    15) Setup the file  smtprelay.tab  ( and/or  smtp.ipmap.tab  if You need ) to restrict mail
        relaying of Your server


    [ NT ]

    1) Build XMail
    2) Copy the supplied MailRoot directory where You want it resides ( I suppose  C:\MailRoot )
    3) Setup MailRoot directory ( and subdirectories and files ) permissions to allow access
        only to System and Administrators. Doing this You can run XMail as console startup only
        if You're Administrator ( service startup as System )
    4) Copy XMail executables to  C:\MailRoot\bin
    5) With  regedit  create GNU key inside  HKEY_LOCAL_MACHINE\SOFTWARE\ and then  XMail
        key inside  HKEY_LOCAL_MACHINE\SOFTWARE\GNU
    6) Create a new string value named  MAIL_ROOT  inside  HKEY_LOCAL_MACHINE\SOFTWARE\GNU\XMail\
        with value  C:\MailRoot
    7) Optionally create a new string value named  MAIL_CMD_LINE  inside
        HKEY_LOCAL_MACHINE\SOFTWARE\GNU\XMail\ to store Your command line options
        ( read well the rest of this doc )
    8) Open an NT console ( command prompt )
    9) Go inside  C:\MailRoot\bin  and run :
            XMail --install
        for a manual startup, or :
            XMail --install-auto
        for an automatic startup
    10) If You've other services that gives the same functionalities of XMail, that is
        SMTP POP3 or Finger servers, You must stop these services
    11) Setup  SERVER.TAB  configuration option after a well read to the rest of this doc
    12) Add Your users and domains ( after a well read to the rest of this doc )
    13) Setup file permissions of  C:\MailRoot  directory to grant access only SYSTEM and
        Domain Adminis
    14) Change or comment out ( # ) the example account in  ctrlaccounts.tab  by using not
        trivial username and password
    15) To start XMail without reboot You can go to :
            ControlPanel -> Services -> XMail server
        and start the service, otherwise reboot Your machine
    16) Setup the file  smtprelay.tab  ( and/or  smtp.ipmap.tab  if You need ) to restrict mail
        relaying of Your server


    If You want to start XMail as a simple test You must setup an environment variable 
    MAIL_ROOT that point to the XMail Server root directory.
    Linux :

        export MAIL_ROOT=/var/XMailRoot

    Windows NT :

        set MAIL_ROOT=C:\MailRoot




    Mail root directory contain this files :

        aliases.tab <file>
        aliasdomain.tab <file>
        domains.tab <file>
        dnsroots    <file>
        extaliases.tab  <file>
        mailusers.tab   <file>
        message.id  <file>
        pop3links.tab   <file>
        server.tab  <file>
        smtpgw.tab  <file>
        smtpfwd.tab <file>
        smtprelay.tab   <file>
        smtpauth.tab    <file>
        smtpextauth.tab <file>
        userdef.tab <file>
        ctrlaccounts.tab    <file>
        spammers.tab    <file>
        spam-address.tab    <file>
        pop3.ipmap.tab  <file>
        smtp.ipmap.tab  <file>
        ctrl.ipmap.tab  <file>
        finger.ipmap.tab    <file>

    this directories :

        bin     <dir>
        cmdaliases  <dir>
        tabindex    <dir>
        dnscache    <dir>
            mx  <dir>
            ns  <dir>
        custdomains <dir>
        filters     <dir>
        logs        <dir>
        pop3locks   <dir>
        pop3linklocks   <dir>
        pop3links   <dir>
        spool       <dir>
            local       <dir>
            temp        <dir>
            0           <dir>
                0           <dir>
                    mess        <dir>
                    rsnd        <dir>
                    info        <dir>
                    temp        <dir>
                    slog        <dir>
                    lock        <dir>
                    cust        <dir>
                    froz        <dir>
                ...
            ...
        userauth    <dir>
            pop3    <dir>
            smtp    <dir>
        domains     <dir>

    and for each domain DOMAIN handled a directory ( inside  domains  ) :

            DOMAIN      <dir>
            userdef.tab <file>

    inside which reside, for each account ACCOUNT ( inside  domains/ACCOUNT ) :

                ACCOUNT         <dir>
                    user.tab    <file>
                    mlusers.tab <file>  [ mailing list case ]
                    mailproc.tab    <file>  [ optional ]
                    pop3.ipmap.tab  <file>  [ optional ]

    and

                    mailbox     <dir>

    for mailbox structure, while :

                    Maildir     <dir>
                        tmp <dir>
                        new <dir>
                        cur <dir>

    for Maildir structure.
    TAB files are text files ( in the sense meant by OS : <CR><LF> for NT and <CR> for Linux )
    with this format :

    "value1"[TAB]"value2"[TAB]...[NEWLINE]

    Let's examine now the means of this files.


    ALIASES.TAB :

    "domain"[TAB]"alias"[TAB]"realaccount"[NEWLINE]

    Ex :

    "home.bogus"    "davidel"   "dlibenzi"

    define "davidel" as alias for "dlibenzi" in "home.bogus" domain.

    "home.bogus"    "foo*bog"   "homer@internal-domain.org"

    define an alias for all users whose name start with  foo  and end with  bog
    that point to the locally handled account  homer@internal-domain.org.

    "home.bogus"    "??trips"   "travels"

    define an alias for all users whose name start with any two chars and end with trips.
    You can have widcard even in the domain field, like :
    
    "*" "postmaster"    "postmaster@domain.net"
    
    You __CANNOT__ edit this file while XMail is running due to the fact that is an indexed file.


    ALIASDOMAIN.TAB :

    "aliasdomain"[TAB]"realdomain"[NEWLINE]

    where  aliasdomain  can use wildcards :

    "simpson.org"   "simpson.com"
    "*.homer.net"   "homer.net"

    The first line define  simpson.org  as an alias of  simpson.com  while the second remap
    all subdomains of  homer.net  to  homer.net
    You __CANNOT__ edit this file while XMail is running due to the fact that is an indexed file.


    DOMAINS.TAB :

    "domain"[NEWLINE]

    define domains handled by the server.


    DNSROOTS :

    host

    this is a file that lists a root name server in each line ( this is not a TAB file ).
    This can be created from a query via nslookup for type=ns and host = "."


    EXTALIASES.TAB :

    "external-domain"[TAB]"external-account"[TAB]"local-domain"[TAB]"local-user"[NEWLINE]

    Ex :

    "xmailserver.org"   "dlibenzi"  "home.bogus"    "dlibenzi"

    This file is used in configutaions in which the server run not directly on internet
    ( like my case ) but act as internal mail exchanger and external mail gateway.
    This file define "Return-Path: <...>" mapping for internal mail delivery.
    If You are using a Mail client like Outlook, Eudora, KMail ... You have configured
    Your email address with the external account say "dlibenzi@xmailserver.org".
    When You post an inernal message to "foo@home.bogus" the mail client put Your external
    email address ( "dlibenzi@xmailserver.org" ) in the "MAIL FROM: <...>" SMTP request.
    Now if the user "foo" reply to this message, it'll reply to "dlibenzimaticad.it"
    then it'll be sent to the external mail server.
    With the entry above in EXTALIASES.TAB file the "Return-Path: <...>" field is filled
    with "dlibenzi@home.bogus" that lead to an internal mail reply.
    You __CANNOT__ edit this file while XMail is running due to the fact that is an indexed file.


    MAILUSERS.TAB :

    "domain"[TAB]"account"[TAB]"enc-passwd"[TAB]"account-id"[TAB]"account-dir"[TAB]"account-type"[NEWLINE]

    Ex :

    "home.bogus"    "dlibenzi"  "XYZ..."    1   "dlibenzi"  "U"

    define an account "dlibenzi" in domain "home.bogus" with the encrypted password "XYZ...",
    user id "1" and mail directory "dlibenzi" inside $MAIL_ROOT/domains/home.bogus.
    To allow multiple domains handling the POP3 client must use the entire email address
    for the POP3 user account, ex. if a user has email user@domain it must supply :

        user@domain

    as POP3 account login.
    The directory "account-dir" __must__ case match with the field "account-dir" of this file.
    Note that user id __must__ be unique for all users ( can't exist duplicated user ids ).
    The user id 0 is reserved by XMail and cannot be used.
    The last field "U" is the account type :

    "U" = User account
    "M" = Mailing list account

    The encrypted password is generated by  XMCrypt  whose source is in  XMCrypt.cpp.
    Even if external authentication is used ( see Part 8 External Authentication ) this
    file _must_ contain an entry for each user handled by XMail.
    You __CANNOT__ edit this file while XMail is running due to the fact that is an indexed file.


    MESSAGE.ID :

    Is a file storing a sequential message number.
    You set it at 1 when You install the server and leave it be handled by the software.


    POP3LINKS.TAB :

    "local-domain"[TAB]"local-account"[TAB]"external-domain"[TAB]"external-account"[TAB]"external-crypted-password"[TAB]"authtype"[NEWLINE]

    authtype    = authentication method ( CLR = USER/PASS auth    APOP = APOP auth )

    Ex :

    "home.bogus"    "dlibenzi"  "xmailserver.org"   "dlibenzi"  "XYZ..."    "APOP"

    This entry is used to syncronize the external account "dlibenzi@xmailserver.org" with encrypted
    password "XYZ..." with the local account "dlibenzi@home.bogus" using  APOP  authentication.
    It connect with the "xmailserver.org" POP3 server and download all messages for "dlibenzi@xmailserver.org" into
    the local account "dlibenzi@home.bogus".
    The remote server must support  APOP  authentication to specify APOP as authtype.
    Even if using APOP authentication is more secure coz clear usernames and password does not
    travel on the network, if You're not sure about it, specify  CLR  as authtype.
    For non local POP3 sync You've to specify a line like this one ( @ as the first domain char ) :

    "@home.bogus.com"   "dlibenzi"  "xmailserver.org:110"   "dlibenzi"  "XYZ..."    "CLR"

    This entry is used to syncronize the external account "dlibenzi@xmailserver.org" with encrypted
    password "XYZ..." with the account "dlibenzi@home.bogus.com" using  CLR  authentication.
    The message will be pushed into the spool having as destination  dlibenzi@home.bogus.com  ,
    so You've to have some kind of processing for that user or domain in Your XMail configuration
    ( for example custom domain processing ).
    You can also have the option to setup a line like this one :

    "?home.bogus.com,felins.net,pets.org"   "dlibenzi"  "xmailserver.org"   "dlibenzi"  "XYZ..."    "CLR"

    and messages are dropped inside the spool by following these rules :
    1) XMail parse the message headers by searching for To:, Cc: and Bcc: addresses
    2) Each address's domain is compared with the list of valid domains ( felins.net, pets.org )
    3) For each valid address the username part is taken and joined with the '@' and
        the masquerade domain name ( the name following '?' )
    4) The message is spooled with the above built destination address
    
    Obviously the masquerade domain ( 'home.bogus.com' ) MUST be handled by the server or MUST be
    a valid external mail domain.
    So if a message having as To: address  graycat@felins.net  is fetched by the previous line a
    message is pushed into the spool with address  graycat@home.bogus.com.
    Particular attention is to be taken about at not creating mail loops.
    Another otion is :

    "&.local,felins.net,pets.org"   "dlibenzi"  "xmailserver.org"   "dlibenzi"  "XYZ..."    "CLR"

    where a fetched message whose To: address is graycat@felins.net will be replaced with
    graycat@felins.net.local.
    You can avoid the matching domain list after the masquerading domain but, in that case,
    You may have bad destination addresses inside the spool.
    The list MUST be comma separated WITHOUT spaces.
    XMail will start PSYNC session with a delay that You can specify with the -Yi nsec
    command line parameter ( default 120 ).
    XMail will also check for the presence ( inside MAIL_ROOT ) of a file named ".psync-trigger" and,
    when this file is found, a PSYNC session will start and such file will be removed.


    SERVER.TAB :

    "varname"[TAB]"varvalue"[NEWLINE]

    This file contain server configuration variabiles.


    SMTPGW.TAB :

    "domain"[TAB]"smtp-gateway"[NEWLINE]

    Ex :

    "foo.example.com"   "@xmailserver.org"

    will send all mail for "foo.example.com" through the "xmailserver.org" SMTP server,
    while :

    "*.dummy.net"   "@relay.xmailserver.org"

    will send all mail for  "*.dummy.net"  through  "relay.xmailserver.org".
    The  smtp-gateway  can be a complex routing also, ex :

    "*.dummy.net"   "@relay.xmailserver.org,@mail.nowhere.org"

    will send all mail for  "*.dummy.net"  through  "@relay.xmailserver.org,@mail.nowhere.org",
    in this way  relay.xmailserver.org --> mail.nowhere.org --> @DESTINATION


    SMTPFWD.TAB :

    "domain"[TAB]"smtp-mx-list"[NEWLINE]

    Ex :

    "foo.example.com"   "mail.xmailserver.org:7001,192.168.1.1:6123,mx.xmailserver.org"

    will send all mail for "foo.example.com" using the provided list of mail exchangers,
    while :

    "*.dummy.net"   "mail.xmailserver.org,192.168.1.1,mx.xmailserver.org:6423"

    will send all mail for  "*.dummy.net"  through the provided list of mail exchangers.
    If the port ( :nn ) is not specified the default SMTP port ( 25 ) is assumed.
    You can also enable XMail to random-select the order of the gateway list by specifying :

    "*.dummy.net"   "#mail.xmailserver.org,192.168.1.1,mx.xmailserver.org:6423"

    using the character  #  as the first char of the gateway list.


    SMTPRELAY.TAB :

    "ipaddr"[TAB]"netmask"[NEWLINE]

    Ex :

    "212.131.173.0"[TAB]"255.255.255.0"[NEWLINE]

    allow all hosts of the class "C" network "212.131.173.XXX" to use the server as relay.


    SMTPAUTH.TAB :

    "username"[TAB]"password"[TAB]"permissions"[NEWLINE]

    is used to permit SMTP clients authentication with protocols PLAIN, LOGIN, CRAM-MD5
    and custom.
    With custom authentication a file containing all secrets ( username + ':' + password )
    is passed as parameter to the custom authentication program which will test all secrets
    to find the one matching ( if exist ).
    For this reason it's better to keep the number of entries in this file as low as possible.
    Permissions are a string that can contain :

    M   = open mailing features
    R   = open relay features ( bypass all other relay blocking traps )
    V   = VRFY command enabled ( bypass SERVER.TAB variable )
    T   = ETRN command enabled ( bypass SERVER.TAB variable )
    Z   = disable mail size checking ( bypass SERVER.TAB variable )

    When PLAIN, LOGIN or CRAM-MD5 authentication mode are used a first lookup in MAILUSERS.TAB
    accounts is performed to avoid duplicating informations with SMTPAUTH.TAB.
    So using these authentication modes a user must use as username the full email address
    ( the : separator is permitted instead @ ) and as password his POP3 password.
    If the lookup succeed the  SERVER.TAB  variable  "DefaultSmtpPerms"  is used to assign
    user SMTP permissions ( default MR ).
    If the lookup will fail then  SMTPAUTH.TAB  lookup is done.


    SMTPEXTAUTH.TAB :

    Besides internal SMTP authentication methods a user ( XMail administrator ) can define
    custom authentication procedures by setting up properly this file.
    In section  "SMTP Client Authentication"  We can see at the client part of custom
    authentication when We put an  "external"  line inside the configuration file.
    The file  SMTPEXTAUTH.TAB  is the server part of the custom authentication which has
    the given format :

    "auth-name"[TAB]"base-challenge"[TAB]"program-path"[TAB]"arg-or-macro"...[NEWLINE]

    This file can contain multiple lines whose  "auth-name"  will be listed during the
    EHLO command response.
    Where  "arg-or-macro" can be :

    @@CHALL     = server challenge given by  base-challenge + ':' + server-timestamp
    @@DGEST     = client response to server challenge ( @@CHALL )
    @@FSECRT    = a file containing all the lines ( username + ':' + password ) of SMTPAUTH.TAB

    Ie :

    "RSA-AUTH"  "foochallenge"  "/usr/bin/myrsa-authenticate"   "-c"    "@@CHALL"   "-f"    "@@FSECRT"  "-d"    "@@DGEST"

    The external program must test all lines of  @@FSECRT  to find the one ( if exist )
    that match the client digest ( @@DGEST ).
    If it find a match it must return zero and overwrite  @@FSECRT  with the matching secret
    ( username + ':' + password ).
    If a match is not found the program must return a value different from zero.


    USERDEF.TAB :

    "varname"[TAB]"varvalue"[NEWLINE]

    Ex :

    "RealName"  "??"
    "HomePage"  "??"
    "Address"   "??"
    "Telephone" "??"
    "MaxMBSize" "10000"

    contain user default values for new users that are not set during the new account creation.
    This file is looked up in two different places, first in $MAIL_ROOT/domains/DOMAIN then in $MAIL_ROOT,
    where DOMAIN is the name of the domain where We're going to create the new user.


    For each "domain" handled by the server We'll create a directory "domain" inside $MAIL_ROOT.
    Inside $MAIL_ROOT/"domain" will reside "domain" "account" directories ( $MAIL_ROOT/"domain"/"account" ).
    This folder contain a subfolder named  mailbox  ( or  Maildir/(tmp,new,cur) ) that store all "account" messages.
    It also contains a file named USER.TAB that store "account" variabiles, ex :


    "RealName"  "Davide Libenzi"
    "HomePage"  "http://www.xmailserver.org/davide.html"
    "MaxMBSize" "30000"


    CTRLACCOUNTS.TAB :

    "username"[TAB]"password"[NEWLINE]

    This file contain the accounts that are enable to remote administer XMail.
    The password is encrypted with  XMCrypt  program supplied with the source distro.
    REMEMBER THAT THIS HOLDS ADMIN ACCOUNTS, SO PLEASE CHOOSE COMPLEX USERNAMES AND PASSWORDS AND
    USE CTRL.IPMAP.TAB TO RESTRICT IP ACCESS !
    REMEMBER TO REMOVE THE EXAMPLE ACCOUNT FROM THIS FILE !


    SPAMMERS.TAB :

    "ipaddr"[TAB]"netmask"[NEWLINE]

    Ex :

    "212.131.173.0"[TAB]"255.255.255.0"[NEWLINE]

    register all hosts of the class "C" network "212.131.173.XXX" as spammers,
    and block them the use of XMail SMTP server.


    SPAM-ADDRESS.TAB :

    "spam-address"[NEWLINE]

    Ex :

    "*@rude.net"[NEWLINE]
    "*-admin@even.more.rude.net"[NEWLINE]

    will block mails coming from the entire domain  rude.net  and comig from all
    addresses that end with  -admin@even.more.rude.net.


    POP3.IPMAP.TAB :

    "ipaddr"[TAB]"netmask"[TAB]"permission"[TAB]"precedence"[NEWLINE]

    This file control the global IP access permission to POP3 server if located
    into MAIL_ROOT path, and user IP access to its POP3 mailbox if located inside
    the user directory.
    Ex :

    "0.0.0.0"[TAB]"0.0.0.0"[TAB]"DENY"[TAB]"1"[NEWLINE]
    "212.131.173.0"[TAB]"255.255.255.0"[TAB]"ALLOW"[TAB]"2"[NEWLINE]

    This configuration deny access to all IPs except the ones of the
    class "C" network "212.131.173.XXX".
    Higher precedences win over lower ones.


    SMTP.IPMAP.TAB :

    "ipaddr"[TAB]"netmask"[TAB]"permission"[TAB]"precedence"[NEWLINE]

    This file control IP access permission to SMTP server.
    Ex :

    "0.0.0.0"[TAB]"0.0.0.0"[TAB]"DENY"[TAB]"1"[NEWLINE]
    "212.131.173.0"[TAB]"255.255.255.0"[TAB]"ALLOW"[TAB]"2"[NEWLINE]

    This configuration deny access to all IPs except the ones of the
    class "C" network "212.131.173.XXX".
    Higher precedences win over lower ones.


    CTRL.IPMAP.TAB :

    "ipaddr"[TAB]"netmask"[TAB]"permission"[TAB]"precedence"[NEWLINE]

    This file control IP access permission to CTRL server.
    Ex :

    "0.0.0.0"[TAB]"0.0.0.0"[TAB]"DENY"[TAB]"1"[NEWLINE]
    "212.131.173.0"[TAB]"255.255.255.0"[TAB]"ALLOW"[TAB]"2"[NEWLINE]

    This configuration deny access to all IPs except the ones of the
    class "C" network "212.131.173.XXX".
    Higher precedences win over lower ones.


    FINGER.IPMAP.TAB :

    "ipaddr"[TAB]"netmask"[TAB]"permission"[TAB]"precedence"[NEWLINE]

    This file control IP access permission to FINGER server.
    Ex :

    "0.0.0.0"[TAB]"0.0.0.0"[TAB]"DENY"[TAB]"1"[NEWLINE]
    "212.131.173.0"[TAB]"255.255.255.0"[TAB]"ALLOW"[TAB]"2"[NEWLINE]

    This configuration deny access to all IPs except the ones of the
    class "C" network "212.131.173.XXX".
    Higher precedences win over lower ones.


    USER.TAB :

    "variable"[TAB]"value"[NEWLINE]

    store user informations like :

    "RealName"  "Davide Libenzi"
    "HomePage"  "http://www.xmailserver.org/davide.html"
    "MaxMBSize" "30000"
    "ClosedML"  "0"


    MLUSERS.TAB :

    If the user is a mailing list this file must exist inside user account subdirectory
    and contain a list of users subscribed to this list. The file format is :

    "user"[TAB]"perms"[NEWLINE]

    where :

    user        = subscriber email address
    perms       = subscriber permissions :
                  R = read
                  W = write ( check done using the "MAIL FROM:<...>" SMTP return path )
                  A = write ( check done using the email address used for SMTP authentication )


    Ex:

    "davidel@xmailserver.org"   "RW"
    "ghostuser@nightmare.net"   "R"
    "meawmeaw@kitty.cat"        "RA"

    If the  USER.TAB  file defines a "ClosedML" variable as 1 then a client can post
    to this mailing list only if It's listed in  MLUSERS.TAB with RW permissions.


    MAILPROC.TAB :

    "command"[TAB]"arg-or-macro"[TAB]...[NEWLINE]

    store commands ( internals or externals ) that has to be executed on message file.
    The presence of this file is optional an if it does not exist the default processing
    is to store the message in user mailbox.
    Each argument can be a macro also :

    @@FROM      will be substituted with the sender of the message
    @@RCPT      will be substituted with the recipient of the message
    @@RRCPT     will be substituted with the real recipient ( @@RCPT could be an alias ) of the message
    @@FILE      will be substituted with the message file path ( the external command _must_ only read the file )
    @@MSGID     will be substituted with the ( XMail unique ) message id
    @@MSGREF    will be substituted with the reference SMTP message id
    @@TMPFILE   will create a copy of the message file to a temporary one. It can be
            used with "external" command but in this case it's external program
            responsibility to delete the temporary file.

    Supported commands :

    [EXTERNAL]
    "external"[TAB]"priority"[TAB]"wait-timeout"[TAB]"command-path"[TAB]"arg-or-macro"[TAB]...[NEWLINE]

    where :

    external    = command keyword
    priority    = process priority - 0 = normal  -1 = below normal  +1 = above normal
    wait-timeout    = wait timeout for process execution in seconds - 0 = nowait

    if wait-timeout = 0 You must add a "wait" command at the end of MAILPROC.TAB to
    give the executed external commands the time to read the message file.
    This is coz such file is a temporary one that will be deleted when XMail exit
    from MAILPROC.TAB file processing.

    [MAILBOX]
    "mailbox"[NEWLINE]

    With this command the message will be push into local user mailbox.

    [REDIRECT]
    "redirect"[TAB]"address"[TAB]...[NEWLINE]

    Redirect message to internal or external addresses.

    [LREDIRECT]
    "lredirect"[TAB]"address"[TAB]...[NEWLINE]

    Redirect message to internal or external addresses impersonating local domain
    during messages delivery.

    [WAIT]
    "wait"[TAB]"timeout"[NEWLINE]

    Wait "timeout" seconds.
    This command is used to give external commands the time to read the temporary
    message file when such commands are lounched with wait-timeout = 0.














Part 8          External Authentication

    You can use external modules ( executables ) to perform user authentication instead
    of using XMail  mailusers.tab  lookups.
    Inside  userauth  directory You'll find one directory for each service whose
    authentication can be handled externally ( for now only POP3 ).
    Suppose We must authenticate  USERNAME  inside  DOMAIN , XMail first try
    to lookup ( inside  userauth/pop3  ) a file named :

    DOMAIN.tab

    else :

    .tab

    If one of this files is found, XMail authenticate  USERNAME - DOMAIN using such file.
    Authentication file is a TAB file ( see at the proper section in this doc ) which has
    the given structure :

    "auth-action"[TAB]"command"[TAB]"arg-or-macro"[TAB]...[NEWLINE]

    Each argument can be a macro also :

    @@USER      will be substituted with the USERNAME to authenticate
    @@DOMAIN    will be substituted with the DOMAIN to authenticate
    @@PASSWD    will be substituted with the user password
    @@PATH      will be substituted with the user path

    The values for  "auth-action"  can be :

    userauth    = executed when user authentication is required
    useradd     = executed when a user need to be added
    useredit    = executed when a user change is needed
    userdel     = executed when a user deletion is needed
    domaindrop  = executed when all domain users need to be deleted

    The first line that store the handling command for the requested action is executed as :

    command arg0 ... argN

    that in success case must return zero. Any other exit code will be interpreted as
    authentication operation failure, that in  userauth  case means that such user will be
    not authenticated.
    If the execution of the  command  will fail for system reasons ( command not found,
    access denied, etc ... ) then the user will be not authenticated.
    If noone of this files id found then usual authentication is performed ( mailusers.tab ).
    The use of external authentication does not avoid the presence of the user entry in
    mailusers.tab.



















Part 9          SMTP Client Authentication

    When a message is to be sent through an SMTP server that require authentication,
    XMail provides a way to handle this task by setting up properly the  userauth/smtp
    subdirectory.
    Suppose a mail is to be sent through the SMTP server  mail.foo.net , this makes XMail
    to search for a file named ( inside  userauth/smtp ) :

    mail.foo.net.tab

    then :

    foo.net.tab

    then :

    net.tab

    If one of this files is found its content is used to authenticate the SMTP client session.
    The structure of this file, as the extension say, is the TAB one used for most of config
    files inside XMail.
    Only the first valid line ( uncommented # ) is used to choose the authentication method
    and lines has this format :

    "auth-type"[TAB]"param1"...[TAB]"paramN"[NEWLINE]

    Valid lines are :

    "plain" "username"  "password"

    or

    "login" "username"  "password"

    or

    "cram-md5"  "username"  "password"

    or

    "external"  "auth-name" "secret"    "prog-path" "arg-or-macro"  ...

    Where  "auth-name"  can be any symbolic name and  "arg-or-macro"  can be a program
    argoument or one of this macros :

    @@CHALL     = server challenge string
    @@SECRT     = authentication secret
    @@RFILE     = output response file path

    Ie :

    "external"  "RSA-AUTH"  "mysecret"  "/usr/bin/myrsa-auth"   "-c"    "@@CHALL"   "-s"    "@@SECRT"   "-f"    "@@RFILE"

    XMail will send a line like :

    AUTH RSA-AUTH

    to the SMTP server, and wait for a line like :

    3?? base64-challenge

    Than XMail decode  base64-challenge  and invoke the external program to get the response
    to send to the SMTP server.
    The external program must return zero in success case and must put the response into
    the file  @@RFILE ( without new line termination ).

























Part 10         Custom domain mail processing

    If a message that has as target domain  sub1.sub2.domain.net  arrive onto the XMail server, XMail
    will decide if this domain will get a custom domain processing by trying to lookup :

    sub1.sub2.domain.net.tab
    .sub2.domain.net.tab
    .domain.net.tab
    .net.tab
    .tab

    inside the  custdomains  directory.
    If one of these files is found the incoming mail will get a custom domain processing by executing
    commands that are stored into such file.
    The format is :

    "command"[TAB]"arg-or-macro"[TAB]...[NEWLINE]

    store commands ( internals or externals ) that has to be executed on message file.
    The presence of this file is optional an if it does not exist the default processing
    is to send the message via SMTP.
    Each argument can be a macro also :

    @@FROM      will be substituted with the sender of the message
    @@RCPT      will be substituted with the target of the message
    @@FILE      will be substituted with the message file path ( the external command _must_ only read the file )
    @@MSGID     will be substituted with the ( XMail unique ) message id
    @@MSGREF    will be substituted with the reference SMTP message id
    @@TMPFILE   will create a copy of the message file to a temporary one. It can be
            used with "external" command but in this case it's external program
            responsibility to delete the temporary file.

    Supported commands :

    [EXTERNAL]
    "external"[TAB]"priority"[TAB]"wait-timeout"[TAB]"command-path"[TAB]"arg-or-macro"[TAB]...[NEWLINE]

    where :

    external    = command keyword
    priority    = process priority - 0 = normal  -1 = below normal  +1 = above normal
    wait-timeout    = wait timeout for process execution in seconds - 0 = nowait

    if wait-timeout = 0 You must add a "wait" command at the end of the file to
    give the executed external commands the time to read the message file.
    This is coz such file is a temporary one that will be deleted when XMail exit
    from file processing.

    [REDIRECT]
    "redirect"[TAB]"domain-or-emailaddress"[TAB]...[NEWLINE]

    Redirect message to internal or external domain or email address.
    If the message was for foo-user@custdomain.net and the file custdomain.net.tab
    contain a line :

    "redirect"  "target-domain.org"

    the message is delivered to  foo-user@target-domain.org
    While the line :
    
    "redirect"  "user@target-domain.org"
    
    will redirect the message to user@target-domain.org.
    

    [LREDIRECT]
    "lredirect"[TAB]"domain-or-emailaddress"[TAB]...[NEWLINE]

    Redirect message to internal or external domain ( or email address ) impersonating local domain
    during messages delivery.
    If the message was for foo-user@custdomain.net and the file custdomain.net.tab
    contain a line :

    "redirect"  "target-domain.org"

    the message is delivered to  foo-user@target-domain.org
    While the line :
    
    "redirect"  "user@target-domain.org"
    
    will redirect the message to user@target-domain.org.


    [WAIT]
    "wait"[TAB]"timeout"[NEWLINE]

    Wait "timeout" seconds.
    This command is used to give external commands the time to read the temporary
    message file when such commands are lounched with wait-timeout = 0.

    [SMTPRELAY]
    "smtprelay"[TAB]"server[:port],server[:port],..."[NEWLINE]

    Send mail to the specified SMTP server list by trying the first, if fails the second and so on.
    Otherwise You can use this syntax :
    "smtprelay"[TAB]"#server[:port],server[:port],..."[NEWLINE]
    To have XMail random-select the order the specified relays.

    [SMTP]
    "smtp"[NEWLINE]

    Do SMTP delivery.
















Part 11         CmdAliases

     CmdAliases implements aliases that are handled only through commands and can be thought like
     a user level implementation of custom domain processing commands.
     The command set is the same of the one that is described above about custom domain processing
     and it won't be explained again ( look at the "Custom domain mail processing" section above ).
     For every handled domain ( listed inside  domains.tab  ) a directory with the same domain name
     is created inside the  cmdaliases  subdirectory.
     This directory is automatically created and removed when You add/remove domains through the
     CTRL protocol ( or CtrlClnt ).
     When a mail from  USER@DOMAIN  is received by the server and the domain DOMAIN results to be
     handled locally, it the standard users/aliases lookup fails, a file named USER.tab is searched
     inside $MAIL_ROOT/cmdaliases/DOMAIN.
     If such file is found, commands listed inside the file ( whose format must follow the one
     described in the previous section ) are executed by the server as a matter of mail message processing.
     An important thing to remember is that all domain and user names, when applied to the file
     system, must be lower case.
     The use of the command [SMTP] must be considered with high attention because it could create
     mail loops within the server.
















Part 12         SERVER.TAB variables

    [RootDomain]
    Indicate the primary domain for the server.

    [POP3Domain]
    Set the default domain for POP3 client connections.

    [PostMaster]
    Set the postmaster address.
    
    [ErrorsAdmin]
    The email address that will receive notification messages for every message that has had delivery errors.
    It can be empty and in such case the notification message will be sent only to the sender.
    
    [TempErrorsAdmin]
    The email address that will receive notification for temporary delivery failures.
    In case it's empty the notification message will be sent only to the sender.

    [DefaultSMTPGateways]
    A comma separated list of SMTP servers XMail _must_ use to send its mails.
    This has the precedence over MX records.
    
    [HeloDomain]
    If this variable is specified and is not empty, its content will be sent as HELO domain.
    Otherwise the reverse lookup of the local IP will be sent as HELO domain.
    This will help to deal with remote SMTP servers that are set to check the reverse lookup
    of the incoming IP.
    
    [CheckMailerDomain]
    Enable validation of the sender domain ( "MAIL FROM:<...@xxx>" ) by looking up DNS/MX entries.

    [RemoveSpoolErrors]
    Indicate if mail has to be removed or stored in  froz  directory after a failure in
    delivery or filtering.
    
    [NotifyMsgLinesExtra]
    Number of lines of the bounced message that have to be listed inside the notify message ( lines after
    the headers section ). Default is zero.
    
    [NotifySendLogToSender]
    Enable/Disable the send of the message log file inside the notify message to the sender.
    Default is off ( zero ).
    
    [NotifyTryPattern]
    List of delivery attempts that require the system to send a notification to the sender
    ( and eventually to  TempErrorsAdmin ).
    The list is a comma separated list of number ( with no extra spaces ) like :

    "1,4,9"

    Default is empty that means that no notification is sent upon a delivery attempt failure.

    [AllowNullSender]
    Enable null sender ( "MAIL FROM:<>" ) messages to be accepted by XMail.
    
    [MaxMTAOps]
    Set the maximum number of MTA relay steps before to declare the message as looped ( default 16 ).
    
    [ReceivedHdrType]
    Set the verbosity of the Received: message headers tag.
    0   = Standard ( client IP shown , server IP not ). Default.
    1   = Verbose ( client IP shown , server IP shown )
    2   = Strict ( no IP shown )
    
    [FetchHdrTags]
    Set the list of headers tags to be used to extract addresses from POP3 fetched messages ( POP3LINKS.TAB ).
    This is a comma delimited list ( no extra space or TABs must be included inside the list ) like :
    
    "+X-Deliver-To,To,Cc"
    
    Tags preceded by a '+' character will make XMail to stop scanning when an address is found inside such header tag.
    Tags preceded by a '+' character must be listed before other tags.
    The string "+X-Deliver-To,To,Cc" is the default if nothing is specified.

    [CustomSMTPMessage]
    Set this to the message that you want to follow the standard SMTP error response sent by XMail, like :

    "Please open http://www.xmailserver.test/smtp_errors.html to get more informations about this error"

    Please be aware the RFC821 fix the maximum reply line length to 512 bytes.

    [AllowSmtpVRFY]
    Enable the use of VRFY SMTP command. This flag may be forced by SMTP authentication.
    
    [AllowSmtpETRN]
    Enable the use of ETRN SMTP command. This flag may be forced by SMTP authentication.

    [SmtpMinDiskSpace]
    Minimum disk space ( in Kb ) that is requested before accepting an SMTP connection.

    [SmtpMinVirtMemSpace]
    Minimum virtual memory ( in Kb ) that is requested before accepting an SMTP connection.

    [Pop3MinVirtMemSpace]
    Minimum virtual memory ( in Kb ) that is requested before accepting a POP3 connection.

    [Pop3SyncErrorAccount]
    This defines the email account ( MUST be handled locally ) that will receive all
    fetched email that XMail has not been able to deliver.
    
    [EnableAuthSMTP-POP3]
    Enable SMTP after POP3 authentication ( default on ).
    
    [MaxMessageSize]
    Set the maximum message size in Kb that is possible to send through the server.
    
    [DefaultSmtpPerms]
    This list SMTP permissions assigned to users looked up inside MAILUSERS.TAB during SMTP authentication.
    It also defines the permissions for users authenticated with SMTP after POP3.
    
    [CustMapsList]
    This is a list a user can use to set custom maps checking. The list has the given ( strict ) format :
    
    maps-root:code,maps-root:code...
    
    Where  maps-root  is the root for the dns query ( ie. dialups.mail-abuse.org. ) and the code can be :

    1   = the connection is drooped soon
    0   = the connection is kept alive but only authenticated users can send mail
    -S  = the peer can send messages but a delay of S seconds will be introduced between commands

    [SMTP-RDNSCheck]
    Indicate if XMail must do an RDNS lookup before accepting a incoming SMTP connection.
    If 0 the check is not performed; if 1 and the check fail, the user will receive a "server use forbidden"
    at MAIL_FROM time; if -S ( S > 0 ) and the check fail, a delay of S seconds between SMTP commands is used to prevent
    massive spamming.
    SMTP authentication will override the denial set by this option by giving authenticated users
    the ability to access the server from "mapped" IPs.

    [SmartDNSHost]
    Setup a list of smart DNS hosts to which are directed DNS queries with recursion
    bit set to true. Such DNS hosts must support DNS recursion in queries.
    The format is :

    dns.home.bogus.net:tcp,192.168.1.1:udp,...

    [DisableEmitAuthUser]
    Enable/disable the emission the the "X-Auth-User:" mail header for authenticated users.
    Valid values are "0" or "1", default is "0" ( emission enabled ).

    [DynDnsSetup]
    Give the possibility to handle dynamic IP domain registration to dynamic IP servers.
    One of these service providers is "www.dyndns.org" whose site You can watch for
    registrations and more info.
    The string has the format :

    server,port,HTTP-GET-String[,username,password]

    ie. :

    members.dyndns.org,80,/nic/dyndns?action=edit&started=1&hostname=YES&host_id=yourhost.ourdomain.ext&myip=%s&wildcard=OFF&mx=mail.exchanger.ext&backmx=NO,foouser,foopasswd

    or

    www.dns4ever.com,80,/sys/u.cgi?d=DOMAIN&u=USERNAME&p=PASSWORD&i=%s

    where :

    DOMAIN      = domain You've registered
    USERNAME    = username You get from service provider
    PASSWORD    = password You get from service provider

    The %s in HTTP-GET-String will be replaced with the IP address to register.

    [SmtpConfig]

    Default SMTP server config loaded if specific server IP config is not found.

    [SmtpConfig-XXX.YYY.ZZZ.WWW]

    Specific IP SMTP server config.
    The variable value is a comma separated sequence of configuration tokens whose
    meaning is :

    mail-auth   = authentication required to send mail to the server.
                  Please note that by setting this value will require authentication
                  even for sending to local domains, and this is not what you're
                  probably wishing
                  
                  
















Part 13         Domain message filters

    This feature offer the way to filter out messages by providing the ability to
    execute external programs, such as scripts or real executables, that examines
    ( modify or both ) the message and gives its response with its return value.
    This feature offer the ability to inspect and modify messages, giving a way
    to reject messages based on its content, alter messages ( address rewriting )
    and so on.
    If this filters returns  97, 98 or 99  means that the message is rejected and
    must be stopped in its travel.
    If the filter modify the message it must return  100  as its result.
    When a message is received by the SMTP server for user  foo@xyzw.abc  then XMail
    search inside the  filters  subdirectory to find a file named ( user processing ) :

    foo@xyzw.abc.tab

    If this file is not found then XMail search for ( domain processing ) :

    xyzw.abc.tab

    If this file is not found then XMail search for  .tab  inside  filters
    subdirectory, which offers a way to specify a default mail filtering.
    If this file is not found then the message can continue its travel, otherwise this
    file is processed by submitting the message to all filters stored into the file.

    The syntax of the file is :

    "command"[TAB]"arg-or-macro"[TAB]...[NEWLINE]

    Each argument can be a macro also :

    @@FROM      will be substituted with the sender of the message
    @@RCPT      will be substituted with the target of the message
    @@FILE      will be substituted with the message file path ( the external command may modify
                the file if it's going to return 100 as command exit value )
    @@MSGID     will be substituted with the ( XMail unique ) message id
    @@MSGREF    will be substituted with the reference SMTP message id

    Here  "command"  is the name of an external program that must process the message and
    return its processing result. If it return  99  the message is rejected and a notification
    message is sent to the sender. By returning  98  the message will be rejected without notification
    while by returning 97 the message is rejected without notification and without being frozen
    ( a 98 response could lead to a frozen message is the SERVER.TAB configuration enable this ).
    If all filters return values different from 99, 98 and 97 the message can continue its trip.
    The filter command may also modify the file and return 100, having in this way the ability
    to change the file content ( AV scanning, content filter, message rewriting, etc ).
    If the filter will change the message file it MUST keep the message structure and
    it MUST terminate all line with <CR><LF>.
    The spool files has this structure :

    SmtpDomain      [ 1st line ]
    SmtpMessageID       [ 2nd line ]
    MAIL FROM:<...>     [ 3th line ]
    RCPT TO:<...>       [ 4th line ]
    <<MAIL-DATA>>       [ 5th line ]
    ...

    After the  "<<MAIL-DATA>>"  tag ( 5th line ) the message follow. The message is composed
    by headers section and, after the first empty line, the message body.
















Part 14         USER.TAB variables

    [RealName]
    Full user name, ie. :

    "RealName"  "Davide Libenzi"

    [HomePage]
    User home page, ie. :

    "HomePage"  "http://www.xmailserver.org/davide.html"

    [MaxMBSize]
    Max user mailbox size in Kb, ie. :

    "MaxMBSize" "30000"

    [ClosedML]
    Specify if the mailing list is closed only to subscribed users, ie. :

    "ClosedML"  "1"

    [ListSender]
    Specify the mailing list sender or administrator :

    "ListSender"    "ml-admin@xmailserver.org"

    This variable should be set to avoid delivery error notifications to reach the
    original message senders.

    [SmtpPerms]
    User SMTP permissions ( see SMTPAUTH.TAB for info ).
    
    [ReceiveEnable]
    It's "1" if the account can receive email, "0" if You want to disable the account from receiving messages.

    [PopEnable]
    It's "1" if You want to enable the account to fetch POP3 messages, "0" otherwise.

    [UseReplyTo]
    Enable/Disable the emission of the Reply-To: header for mailing list's messages ( default 1 ).

    [MaxMessageSize]
    Set the maximum message size ( in Kb ) that the user will be able to send through the server.
    Overrides the SERVER.TAB variable.
    
    [DisableEmitAuthUser]
    Enable/disable the emission the the "X-Auth-User:" mail header for authenticated users.
    Valid values are "0" or "1", default is "0" ( emission enabled ). This variable overides
    the SERVER.TAB one when present.













Part 15         Mail Routing Through Addresses

    A full implementation of SMTP protocol comprise the ability to perform mail routing
    bypassing DNS MX records by means of setting in a ruled way the "RCPT TO: <>" request.
    A mail from  xuser@hostz  directed to  @hosta,@hostb:foouser@hostc  is received
    by  @hosta  then this will be sent to  @hostb  using  "MAIL FROM: <@hosta:xuser@hostz>"
    and  "RCPT TO: <@hostb:foouser@hostc>".
    Then will be sent to  @hostc  using  "MAIL FROM: <@hostb,@hosta:xuser@hostz>"
    and  "RCPT TO: <foouser@hostc>".














Part 16         XMail spool design

    The new spool fs tree format has been designed to enable XMail to handle very
    large queues.
    Instead of having a single spool directory ( like versions older than 0.61 )
    a two layer deep splitting has been introduced so that its structure is :

    0   <dir>
        0   <dir>
            mess    <dir>
            rsnd    <dir>
            info    <dir>
            temp    <dir>
            slog    <dir>
            cust    <dir>
            froz    <dir>
        ...
    ...

    When XMail needs to create a new spool file a spool path is choosen in a random way and
    a new file with the format :

    mstime.pid.hostname

    is created inside the temp subdirectory.
    When the spool file is ready to be committed, it's moved in the mess subdirectory that holds
    newer spool files.
    If XMail fails sending a new message ( the ones in  mess  subdirectory ) it creates a log file
    ( with the same name of the message file ) inside the  slog  subdirectory and move the file
    from  mess  to  rsnd.
    During the message sending the message itself is locked by creating a file inside the  lock
    subdirectory ( with the same name of the message file ).
    If the message has permanent delivery errors or is expired and if the option  RemoveSpoolErrors
    of the  SERVER.TAB  file is off, the message file is moved inside the  froz  subdirectory.















Part 17         SMTP commands

    These are commands understood by ESMTP server :

    MAIL FROM:<>
    RCPT TO:<>
    DATA
    HELO
    EHLO
    AUTH
    RSET
    VRFY
    ETRN
    NOOP
    HELP
    QUIT

















Part 18         POP3 commands

    These are commands understood by POP3 server :

    USER
    PASS
    APOP
    STAT
    LIST
    UIDL
    QUIT
    RETR
    TOP
    DELE
    NOOP
    LAST
    RSET





















Part 19         Command line

    Most of XMail configuration settings are command line tunables.
    These are command line switches organized by server.

    [XMAIL]
    -Ms pathname    = Mail root path also settable with MAIL_ROOT environment
    -Md     = Activate debug ( verbose ) mode
    -Mr hours   = Set log rotate hours step
    -Mx split-level = Set the queue split level. The value You set here is rounded to the lower
                prime number higher or equal than the value You've set
    -MR bytes   = Set the size of the socket's receive buffer in bytes ( rounded up to 1024 )
    -MS bytes   = Set the size of the socket's send buffer in bytes ( rounded up to 1024 )
    -MM         = Setup XMail to use "Maildir" delivery ( default on Unix )
    -Mm         = Setup XMail to use "mailbox" delivery ( default on Windows )

    [POP3]
    -Pp port    = Set POP3 server port ( if You change this You must know what You're doing )
    -Pt timeout = Set POP3 session timeout ( seconds ) after which the server will close
                the connection if not receive any commands
    -Pl     = Enable POP3 logging
    -Pw timeout = Set the delay timeout in response to a bad POP3 login. Such time will be
                doubled at the next bad login
    -Ph     = Hang the connection in bad login response
    -PI ip[:port]   = Bind server to the specified ip address and ( optional ) port ( can be multiple )
    -PX nthreads    = Set the maximum number of threads for POP3 server

    [SMTP]
    -Sp port    = Set SMTP server port ( if You change this You must know what You're doing )
    -St timeout = Set SMTP session timeout ( seconds ) after which the server will close
                        the connection if not receive any commands
    -Sl             = Enable SMTP logging
    -SI ip[:port]   = Bind server to the specified ip address and ( optional ) port ( can be multiple )
    -SX nthreads    = Set the maximum number of threads for SMTP server
    -Sr maxrcpts    = Set the maximu number of recipients for a single SMTP message ( default 100 )
    -Se nsecs       = Set the expire timeout for a POP3 authentication IP ( default 900 )

    [SMAIL]
    -Qn nthreads    = Set the number of mailer threads
    -Qt timeout = Set the timeout to be waited for a next try after send failure. Default 480
    -Qi ratio   = Set the increment ratio of the reschedule time in sending a messages.
                At every failure in delivery a message, reschedule time T is incremented
                by ( T / ratio ), therefore  T(i) = T(i-1) + T(i-1)/ratio.
                If You set this ratio to zero, T remain unchanged over delivery tentatives.
                Default 16
    -Qr nretries    = Set the maximum number of times to try to send the message. Default 32
    -Ql     = Enable SMAIL logging

    [PSYNC]
    -Yi interval    = Set external POP3 accounts sync interval. Default 120
    -Yt nthreads    = Set the number of POP3 sync threads

    [FINGER]
    -Fp port    = Set FINGER server port ( if You change this You must know what You're doing )
    -Fl     = Enable FINGER logging
    -FI ip[:port]   = Bind server to the specified ip address and ( optional ) port ( can be multiple )

    [CTRL]
    -Cp port    = Set CTRL server port ( if You change this You must know what You're doing )
    -Ct timeout = Set CTRL session timeout ( seconds ) after which the server will close
                        the connection if not receive any commands
    -Cl             = Enable CTRL logging
    -CI ip[:port]   = Bind server to the specified ip address and ( optional ) port ( can be multiple )
    -CX nthreads    = Set the maximum number of threads for CTRL server

    [LMAIL]
    -Ln nthreads    = Set the number of local mailer threads
    -Lt timeout = Set the sleep timeout for LMAIL threads ( in seconds, default 2 )
    -Ll     = Enable local mail logging













Part 20         XMail admin protocol

    It's possible to remote admin XMail due to the existence of a "controller server"
    that run with XMail and that wait for TCP/IP connections on a port ( 6017 or tunable
    via a "-Cp nport" ) command line option.
    The XMail admin server "speak" a given protocol that can be used by external GUI
    utilities written with the more disparate scripting languages, to remote administer
    the mail server.
    The protocol is based on sending formatted strings of command and waiting for formatted
    server responses and error codes.
    All the lines, commands and responses, are delimited by a <CR><LF> pair.
    The error code string ( I'll call it RESSTRING ) has the given format :

    "+DDDDD OK"<CR><LF>

    if the command execution is successful while :

    "-DDDDD ErrorString"<CR><LF>

    if failed.
    The " character is not included in responses.
    DDDDD is a numeric error code while ErrorString is a description of the error.
    If DDDDD equals  00100  then a lines list terminated by a line with a single point
    ( <CR><LF>.<CR><LF> ) will follow the response.
    The input format for commands is similar to the one used in TAB files :

    "cmdstring"[TAB]"param1"[TAB]..."paramN"<CR><LF>

    where "cmdstring" is the command string identifying the action to be performed,
    and param1,... are the parameters of the command.

    Immediately after the connection with XMail controller server is established
    the client will receive a RESSTRING that will be :

    +00000 <TimeStamp> XMail ...

    if the server is ready, while :

    -DDDDD ...

    ( where DDDDDD is an error code ) if not.
    The  TimeStamp  string has the format :

    currtime.pid@ipaddress

    and will be used in MD5 authentication procedure.
    As the first action immediately after the connection the client must send an
    authentication string with this format :

    "user"[TAB]"password"<CR><LF>

    where user must be enabled to remote admin XMail. This in case of clear text
    authentication that should not be used due server security.
    Using MD5 authentication instead, the client must perform an MD5 checksum on the
    string composed by ( <> included ) :

    <TimeStamp>password

    and then send to the server :

    "user"[TAB]"#md5chksum"<CR><LF>

    where  md5chksum  is the MD5 checksum ( note  #  as first char of sent digest ).
    The result of the authentication send will be a RESSTRING.
    If the user does not receive a positive authentication response, the connection will
    be closed by the server.

            XMail controller commands

    *) Adding a user

    "useradd"[TAB]"domain"[TAB]"username"[TAB]"password"[TAB]"usertype"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    username    = username to add
    password    = user password
    usertype    = "U" for normal user and "M" for mailing list

    The result will be a RESSTRING.


    *) Deleting a user

    "userdel"[TAB]"domain"[TAB]"username"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    username    = username to delete

    The result will be a RESSTRING.


    *) Changing user password

    "userpasswd"[TAB]"domain"[TAB]"username"[TAB]"password"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    username    = username ( must exist )
    password    = new password

    The result will be a RESSTRING.


    *) Authenticate user

    "userauth"[TAB]"domain"[TAB]"username"[TAB]"password"<CR><LF>

    where :

    domain      = domain name
    username    = username
    password    = password

    The result will be a RESSTRING.


    *) Retrieve user statistics

    "userstat"[TAB]"domain"[TAB]"username"<CR><LF>

    where :

    domain      = domain name
    username    = username/alias

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted matching users list will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    This is the format of the listing :

    "variable"[TAB]"value"<CR><LF>

    Where valid variables are :

    RealAddress           = real address ( maybe different is the supplied username is an alias )
    MailboxSize           = total size of the mailbox in bytes
    MailboxMessages       = total number of messages
    LastLoginIP           = last user login IP address


    *) Adding an alias

    "aliasadd"[TAB]"domain"[TAB]"alias"[TAB]"username"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    alias       = alias to add
    username    = real email account ( locally handled )

    The result will be a RESSTRING.


    *) Deleting an alias

    "aliasdel"[TAB]"domain"[TAB]"alias"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    alias       = alias to delete

    The result will be a RESSTRING.


    *) Listing aliases

    "aliaslist"[TAB]"domain"[TAB]"alias"[TAB]"username"<CR><LF>
    or
    "aliaslist"[TAB]"domain"[TAB]"alias"<CR><LF>
    or
    "aliaslist"[TAB]"domain"<CR><LF>
    or
    "aliaslist"<CR><LF>

    where :

    domain      = domain name, optional ( can contain wildcards )
    alias       = alias name, optional  ( can contain wildcards )
    username    = username, optional  ( can contain wildcards )

    Ex :

    "aliaslist"[TAB]"foo.bar"[TAB]"*"[TAB]"mickey"<CR><LF>

    will list all aliases of user  "mickey"  in domain  "foo.bar".

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted matching users list will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    This is the format of the listing :

    "domain"[TAB]"alias"[TAB]"username"<CR><LF>


    *) Listing user vars

    "uservars"[TAB]"domain"[TAB]"username"<CR><LF>

    where :

    domain      = domain name
    username    = username

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted list of user vars will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    This is the format of the listing :

    "varname"[TAB]"varvalue"<CR><LF>


    *) Setting user vars

    "uservarsset"[TAB]"domain"[TAB]"username"[TAB]"varname"[TAB]"varvalue" ... <CR><LF>

    where :

    domain      = domain name
    username    = username
    varname     = variable name
    varvalue    = variable value

    There can be multiple variable assignments with a single call.
    If  varvalue  is the string ".|rm" the variable  varname  is deleted.
    The result will be a RESSTRING.


    *) Listing users

    "userlist"[TAB]"domain"[TAB]"username"<CR><LF>
    or
    "userlist"[TAB]"domain"<CR><LF>
    or
    "userlist"<CR><LF>

    where :

    domain      = domain name, optional ( can contain wildcards )
    username    = username, optional  ( can contain wildcards )

    Ex :

    "userlist"[TAB]"spacejam.foo"[TAB]"*admin"<CR><LF>

    will list all users of domain  "spacejam.foo"  that end with the word  "admin".

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted matching users list will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    This is the format of the listing :

    "domain"[TAB]"username"[TAB]"password"[TAB]"usertype"<CR><LF>


    *) Getting mailproc.tab file

    "usergetmproc"[TAB]"domain"[TAB]"username"<CR><LF>

    where :

    domain      = domain name
    username    = username

    Ex :

    "usergetmproc"[TAB]"spacejam.foo"[TAB]"admin"<CR><LF>

    will get mailproc.tab file for user  "admin"  in domain  "spacejam.foo".

    The result will be a RESSTRING.
    In success case ( 00100 ) the mailproc.tab file is listed line by line, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Setting mailproc.tab file

    "usersetmproc"[TAB]"domain"[TAB]"username"<CR><LF>

    where :

    domain      = domain name
    username    = username

    Ex :

    "usersetmproc"[TAB]"spacejam.foo"[TAB]"admin"<CR><LF>

    will set mailproc.tab file for user  "admin"  in domain  "spacejam.foo".

    The result will be a RESSTRING.
    In success case ( 00101 ) the client must list the mailproc.tab file line by line,
    ending with a line containing a single dot ( <CR><LF>.<CR><LF> ).
    If a line of the file begin with a dot, another dot must be added at the begin of the line.
    If the file has zero length the mailproc.tab will be deleted.
    The client must then get another RESSTRING indicating the final command result.


    *) Adding a mailing list user

    "mluseradd"[TAB]"domain"[TAB]"mlusername"[TAB]"mailaddress"[TAB]"perms"<CR><LF>

    or

    "mluseradd"[TAB]"domain"[TAB]"mlusername"[TAB]"mailaddress"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    mlusername  = mailing list username
    mailaddress = mail address to add to the mailing list "mlusername@domain"
    perms       = user permissions ( R or RW or RA )

    When  perms  is not specified the default is RW.
    The result will be a RESSTRING.


    *) Deleting a mailing list user

    "mluserdel"[TAB]"domain"[TAB]"mlusername"[TAB]"mailaddress"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    mlusername  = mailing list username
    mailaddress = mail address to delete from the mailing list "mlusername@domain"

    The result will be a RESSTRING.


    *) Listing mailing list users

    "mluserlist"[TAB]"domain"[TAB]"mlusername"<CR><LF>

    where :

    domain      = domain name ( must be handled by the server )
    mlusername  = mailing list username

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted list of mailing list users will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Adding a domain

    "domainadd"[TAB]"domain"<CR><LF>

    where :

    domain      = domain name to add

    The result will be a RESSTRING.


    *) Deleting a domain

    "domaindel"[TAB]"domain"<CR><LF>

    where :

    domain      = domain name to delete

    The result will be a RESSTRING.
    This is not always a safe operation.


    *) Listing handled domains

    "domainlist"<CR><LF>
    
    or :
    
    "domainlist"[TAB]"wildmatch0"[TAB]...[TAB]"wildmatchN"<CR><LF>

    The result will be a RESSTRING.
    The wild match versions simply return a filtered list of domains.
    In success case ( 00100 ) a formatted list of handled domains will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Adding a domain alias

    "aliasdomainadd"[TAB]"realdomain"[TAB]"aliasdomain"<CR><LF>

    Ex :

    "aliasdomainadd"[TAB]"xmailserver.org"[TAB]"xmailserver.com"<CR><LF>

    define  xmailserver.com  as an alias of  xmailserver.org , or :

    "aliasdomainadd"[TAB]"xmailserver.org"[TAB]"*.xmailserver.org"<CR><LF>

    define all subdomains of  xmailserver.org  as alises of  xmailserver.org.


    *) Deleting a domain alias

    "aliasdomaindel"[TAB]"aliasdomain"<CR><LF>

    Ex :

    "aliasdomaindel"[TAB]"*.xmailserver.org"<CR><LF>

    remove the  *.xmailserver.org  domain alias.


    *) Listing alias domains

    "aliasdomainlist"<CR><LF>
    
    or :
    
    "aliasdomainlist"[TAB]"wildmatch0"[TAB]...[TAB]"wildmatchN"<CR><LF>

    The result will be a RESSTRING.
    The wild match version simply returns a filtered list of alias domains.
    In success case ( 00100 ) a formatted list of alias domains will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Getting custom domain file

    "custdomget"[TAB]"domain"<CR><LF>

    where :

    domain      = domain name

    Ex :

    "custdomget"[TAB]"spacejam.foo"<CR><LF>

    will get custom domain file for domain  "spacejam.foo".

    The result will be a RESSTRING.
    In success case ( 00100 ) the custom domain file is listed line by line, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Setting custom domain file

    "custdomset"[TAB]"domain"<CR><LF>

    where :

    domain      = domain name

    Ex :

    "custdomset"[TAB]"spacejam.foo"<CR><LF>

    will set custom domain file for domain  "spacejam.foo".

    The result will be a RESSTRING.
    In success case ( 00101 ) the client must list the custom domain file line by line,
    ending with a line containing a single dot ( <CR><LF>.<CR><LF> ).
    If a line of the file begin with a dot, another dot must be added at the begin of the line.
    If the file has zero length the custom domain file will be deleted.
    The client must then get another RESSTRING indicating the final command result.


    *) Listing custom domains

    "custdomlist"<CR><LF>

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted list of custom domains will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Adding a POP3 external link

    "poplnkadd"[TAB]"loc-domain"[TAB]"loc-username"[TAB]"extrn-domain"[TAB]"extrn-username"[TAB]"extrn-password"[TAB]"authtype"<CR><LF>

    where :

    loc-domain  = local domain name ( must be handled by the server )
    loc-username    = local username which will receive mails
    extrn-domain    = external domain
    extrn-username  = external username
    extrn-password  = external user password
    authtype    = authentication method ( CLR = USER/PASS auth   APOP = APOP auth )

    The remote server must support  APOP  authentication to specify APOP as authtype.
    Even if using APOP authentication is more secure coz clear usernames and password does not
    travel on the network, if You're not sure about it, specify  CLR  as authtype.

    The result will be a RESSTRING.


    *) Deleting a POP3 external link

    "poplnkdel"[TAB]"loc-domain"[TAB]"loc-username"[TAB]"extrn-domain"[TAB]"extrn-username"<CR><LF>

    where :

    loc-domain  = local domain name ( must be handled by the server )
    loc-username    = local username which will receive mails
    extrn-domain    = external domain
    extrn-username  = external username

    The result will be a RESSTRING.


    *) Listing POP3 external links

    "poplnklist"[TAB]"loc-domain"[TAB]"loc-username"<CR><LF>
    or
    "poplnklist"[TAB]"loc-domain"<CR><LF>
    or
    "poplnklist"<CR><LF>

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted list of handled domains will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    The format of the listing is :

    "loc-domain"[TAB]"loc-username"[TAB]"extrn-domain"[TAB]"extrn-username"[TAB]"extrn-password"[TAB]"authtype"[TAB]"on-off"<CR><LF>


    *) Enabling a POP3 external link

    "poplnkenable"[TAB]"enable"[TAB]"loc-domain"[TAB]"loc-username"[TAB]"extrn-domain"[TAB]"extrn-username"<CR><LF>
    or
    "poplnkenable"[TAB]"enable"[TAB]"loc-domain"[TAB]"loc-username"<CR><LF>

    where :

    enable      = 1 for enabling - 0 for disabling
    loc-domain  = local domain name
    loc-username    = local username which will receive mails
    extrn-domain    = external domain
    extrn-username  = external username

    In the second format all users links are affected by the enable operation.

    The result will be a RESSTRING.


    *) Listing files
    
    "filelist"[TAB]"relative-dir-path"[TAB]"match-string"<CR><LF>
    
    where :
    
    relative-dir-path   = path relative to MAIL_ROOT path
    match-string        = wildcard match string for file list selection

    The result will be a RESSTRING.
    In success case ( 00100 ) the directory is listed line by line, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    

    *) Getting configuration file

    "cfgfileget"[TAB]"relative-file-path"<CR><LF>

    where :

    relative-file-path  = path relative to MAIL_ROOT path

    Ex :

    "cfgfileget"[TAB]"ctrlaccounts.tab"<CR><LF>

    The result will be a RESSTRING.
    In success case ( 00100 ) the file is listed line by line, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    You CANNOT use this command with indexed files !


    *) Setting configuration file

    "cfgfileset"[TAB]"relative-file-path"<CR><LF>

    where :

    relative-file-path  = path relative to MAIL_ROOT path

    Ex :

    "cfgfileset"[TAB]"ctrlaccounts.tab"<CR><LF>

    The result will be a RESSTRING.
    In success case ( 00101 ) the client must list the configuration file line by line,
    ending with a line containing a single dot ( <CR><LF>.<CR><LF> ).
    If a line of the file begin with a dot, another dot must be added at the begin of the line.
    If the file has zero length the configuration file will be deleted.
    The client must then get another RESSTRING indicating the final command result.
    Remember that configuration files has a strict syntax and that pushing a bad one
    You can make XMail to not work properly.
    You CANNOT use this command with indexed files !


    *) Listing frozen messages

    "frozlist"<CR><LF>

    The result will be a RESSTRING.
    In success case ( 00100 ) a formatted list of frozen messages will follow, until a line
    containing a single dot ( <CR><LF>.<CR><LF> ).
    The format of the listing is :

    "msgfile"[tab]"lev0"[TAB]"lev1"[TAB]"from"[TAB]"to"[TAB]"time"[TAB]"size"<CR><LF>

    Where :

    msgfile     = message name or id
    lev0        = queue fs level 0 ( first level directory index )
    lev1        = queue fs level 1 ( second level directory index )
    from        = message sender
    to      = message destination
    time        = message time ( "YYYY-MM-DD HH:MM:SS" )
    size        = message size in bytes


    *) Rescheduling frozen message
    
    "frozsubmit"[TAB]"lev0"[TAB]"lev1"[TAB]"msgfile"<CR><LF>
    
    Where :
    
    msgfile     = message name or id
    lev0        = queue fs level 0 ( first level directory index )
    lev1        = queue fs level 1 ( second level directory index )
    
    You can get these info from the "frozlist" command.
    After a message has been successfully rescheduled it'll be deleted from the frozen fs path.
    The result will be a RESSTRING.


    *) Deleting frozen message
    
    "frozdel"[TAB]"lev0"[TAB]"lev1"[TAB]"msgfile"<CR><LF>
    
    Where :
    
    msgfile     = message name or id
    lev0        = queue fs level 0 ( first level directory index )
    lev1        = queue fs level 1 ( second level directory index )
    
    You can get these info from the "frozlist" command.
    The result will be a RESSTRING.


    *) Getting frozen message log file
    
    "frozgetlog"[TAB]"lev0"[TAB]"lev1"[TAB]"msgfile"<CR><LF>
    
    Where :
    
    msgfile     = message name or id
    lev0        = queue fs level 0 ( first level directory index )
    lev1        = queue fs level 1 ( second level directory index )
    
    You can get these info from the "frozlist" command.
    The result will be a RESSTRING.
    In success case ( 00100 ) the frozen message log file will follow, until a line containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Getting frozen message

    "frozgetmsg"[TAB]"lev0"[TAB]"lev1"[TAB]"msgfile"<CR><LF>
    
    Where :
    
    msgfile     = message name or id
    lev0        = queue fs level 0 ( first level directory index )
    lev1        = queue fs level 1 ( second level directory index )
    
    You can get these info from the "frozlist" command.
    The result will be a RESSTRING.
    In success case ( 00100 ) the frozen message file will follow, until a line containing a single dot ( <CR><LF>.<CR><LF> ).


    *) Starting a queue flush
    
    "etrn"[TAB]"email-match0"...<CR><LF>
    
    Where :
    
    email-match0    = wildcard email matching for destination address

    Ex :
    
    "etrn"  "*@*.mydomain.com"  "your-domain.org"
    
    will start queueing all messages with a matching destination address.
    

    *) Do nothing command

    "noop"<CR><LF>

    The result will be a RESSTRING.


    *) Quit the connection

    "quit"<CR><LF>

    The result will be a RESSTRING.




    Are there guys that want to build GUI configuration tools using common scripting
    languages ( Java, TCL/Tk, etc ) and XMail controller protocol ?
    Are there guys that want to build Web configuration tools ?
    Let me know <davidel@xmailserver.org>.


















Part 21         XMail local mailer

    XMail has the ability to deliver locally prepared mail files that if founds
    inside the  spool/local  directory.
    The format of these files is strict :

    mail from:<...>[CR][LF]
    rcpt to:<...>[CR][LF]
    ...
    [CR][LF]
    message text with [CR][LF] line termination

    All lines must be [CR][LF] terminated, with one mail-from statement, one or more
    rcpt-to statements, an empty line and the message text.
    Mail files must not be created directly inside the  /spool/local  directory but
    instead inside  /spool/temp  directory.
    When the file is prepared it has to be moved inside  /spool/local.
    The file name format is :

    stime-seqnr.pid.hostname

    where :

    stime       = system time in sec from 01/01/1970
    seqnr       = sequence number for the current file
    pid         = process or thread id
    hostname    = creator process host name

    Example :

    97456928-001.7892.home.bogus

    XMail has a number of LMAIL threads that periodically scans the  /spool/local
    directory watching for locally generated mail files.
    You can tune this number of threads with the "-Ln nthreads" command line option.
    The suggested number ranges from three to seven.




















Part 22         CtrlClnt ( XMail administration )

    You can use CtrlClnt to send administration commands to XMail.
    These commands are defined in the previous section.
    The syntax of CtrlClnt is :

    CtrlClnt  [-snuptf]  ...

    -s server        = set server address
    -n port          = set server port [6017]
    -u user          = set username
    -p pass          = set password
    -t timeout       = set timeout [60]
    -f filename      = set dump filename [stdout]

    with the command and parameters that follow adhering to the command syntax, ie :

    CtrlClnt  -s mail.foo.org -u davide.libenzi -p ciao   useradd home.bogus foouser foopasswd U

    will execute the command  useradd  with parameters  "home.bogus foouser foopasswd U".
    CtrlClnt  will return 0 if the command is successful and != 0 if not.
    If the command is a query one, then the result will be printed to  stdout.














Part 23         Server Shutdown

    [Linux]
    Under Linux XMail creates a file named XMail.pid under /var/run that contain the PID
    on the main XMail thread.
    By issuing a :

    kill -INT `cat /var/run/XMail.pid`

    a system administrator can initiate the shutdown process ( can take several seconds ).
    You can use the supplied  xmail  startup script to start / stop XMail :

    xmail start / stop


    [NT as console service]
    Under NT console service ( XMail --debug ... ) You can hit Ctrl-C to initiate the
    shutdown process.


    [NT as service]
    Using [Control Panel]->[Services] You can start and stop XMail as You want.


    [All]
    XMail detect a shutdown condition by checking the presence of a file named
    .shutdown  inside its main directory ( MAIL_ROOT ).
    You can initiate XMail shutdown process by creating ( or copying ) a file
    with that name.















Part 24         MkUsers

    This command line utility enable You to create user accounts structure by giving
    it a formatted list of users parameters ( or a formatted text file ).
    The syntax of the list ( or file ) is :

    domain;username;password;real-name;homepage[NEWLINE]

    where a character # as the very first one in a line is used to comment the entire line.
    This utility can be also used to create a random number users, that is useful for me
    for testing the server performance.
    These are MkUsers command line parameters :

    -a numusers     = number of users to create in auto-mode
    -d domain       = domain name in auto-mode
    -f inputFile        = input file name {stdin}
    -u username     = radix user name in auto-mode
    -r rootdir      = mail root path {./}
    -s mboxsize     = mailbox maximum size {10000}
    -i useridbase       = base user id {1}
    -m          = create Maildir boxes
    -h          = show this message

    MkUsers create, under the specified root directory the given structure :

    rootdir                     <dir>
        mailusers.tab               <file>
        domains                 <dir>
            domainXXX           <dir>
                userXXX         <dir>
                    user.tab    <file>
                    mailbox     <dir>
                ...
            ...

    for mailbox structure, while :

    rootdir                     <dir>
        mailusers.tab               <file>
        domains                 <dir>
            domainXXX           <dir>
                userXXX         <dir>
                    user.tab    <file>
                    Maildir     <dir>
                        tmp <dir>
                        new <dir>
                        cur <dir>
                ...
            ...

    for Maildir structure.
    If a file mailusers.tab already exist in mail root path MkUsers exit without
    overwriting the existing copy.
    This protect You by accidental overwriting of Your file when playing inside
    the real MAIL_ROOT directory.
    So if You want to setup the root directory ( -r ... ) as MAIL_ROOT You must delete
    by hand the existing file ( You must know what You're doing ).
    If You setup the root directory ( -r ... ) as MAIL_ROOT You MUST have XMail stopped
    before running MkUsers.
    Existing files and directories will be not overwrited by MkUsers so You can keep
    Your users db into the formatted text file ( or generate it by a database dump for example )
    and run MkUsers to create the structure.
    Remeber that You've to add new domains in  domains.tab  file by hand.
    MkUsers is intended as a bulk-mode utility, not to create single users coz for this need
    CtrlClnt ( or other GUI/Web configuration utilities ) is better suited.
















Part 25         sendmail

    When building XMail an executable called sendmail is also created.
    This is a replacement of the sendmail program used mostly on Unix systems and that use the local mail delivery
    of XMail to send email generated onto the server machine.
    The only sendmail options that are supported are ( other options are simply ignored ) :
    
    -f{mail from}       = Set the sender of the email
    -F{ext mail from}   = Set the extended sender of the email
    -t                  = Extract recipients from the "To:"/"Cc:"/"Bcc:" header tags
    
    The syntax is :
    
    sendmail [-t] [-f...] [-F...] [--input-file fname] [--xinput-file fname] [--rcpt-file fname] [--] recipient ...
    
    the message content is read from the standard input and must be RFC compliant.
    The following parameters are XMail extensions meant to be used with mailing lists managers ( using
    sendmail as a mail list exploder ) :

    --input-file fname      = take the message from the specified file instead from stdin ( RFC format )
    --xinput-file fname     = take the message from the specified file instead from stdin ( XMail format )
    --rcpt-file fname       = add recipients listed inside the specified file ( list exploder )

    To be RFC compliant means that the message MUST be :
    
    [Headers]
    NewLine
    Body
    
    So, suppose You've Your message inside the file 'msg.txt', that You're 'xmailuser@smartdomain' and
    that You want to send the message to 'user1@dom1' and 'user2@dom2' the syntax is :
    
    sendmail -fxmailuser@smartdomain user1@dom1 user2@dom2 < msg.txt
    
    or

    sendmail -fxmailuser@smartdomain --input-file msg.txt user1@dom1 user2@dom2

    
    
    
    
    
    











Part 26         Miscellaneous

    [1]
    To handle multiple POP3 domains the server makes a reverse lookup of the IP address
    upon which it receives the connection.
    Suppose the reverse lookup will result in  xxxx.yyyy.zzzz  then XMail will check if
    xxxx.yyyy.zzzz  is handled, then it'll check  yyyy.zzzz  and then  zzzz.
    The first ( in the given order ) that will result handled will be the POP3 domain.
    To avoid the above behaviour it's sufficient that the POP3 client supply the entire
    email address as POP3 login username :

    foo@foodomain.net   ==> foo@foodomain.net

    and not :

    foo@foodomain.net   ==> foo

    This enable XMail to handle multiple domains in cases where more nic-names are mapped
    over a single IP address.
    To run finger queries You must specify :

    foo@foodomain.net@foodomain.net

    or as general rule :

    username@pop3domain@hostname

    You can use the optional configuration variable "POP3Domain" to set the default
    domain for POP3 clients connections.
    This means that users of "POP3Domain" can use only the name part of their email address
    as POP3 login, while users of others hosted domains must use their entire email as
    POP3 login.

    [2]
    a) REMEMBER TO REMOVE THE EXAMPLE ACCOUNT FROM CTRLACCOUNTS.TAB FILE !
    b) Use ctrl.ipmap.tab to restrict CTRL server access.
    c) Use long password ( mixed upper/lower case with digits ) for ctrlaccounts.tab.

    [3]
    The main cause of bugs with XMail is due a bad line termination of configuration
    files, so check that these files being correctly line terminated for Your OS.
    Linux uses the standard <CR> while M$ uses <CR><LF>.

    [4]
    In Linux if You get a bind error You must comment pop3, smtp and finger entries
    in Your /etc/inetd.conf

    [5]
    Remeber to compile the file CTRL.IPMAP.TAB to restrict the access to the IPs You
    use to remote administer XMail server.

    [6]
    If You've an heavy loaded server remember to setup the best number of SMAIL threads
    by specifying the "-Qn nthreads" option ( You must do some tentatives to find the best
    value for Your needs ).
    Also You can limit the number of SMTP, POP3 and CTRL service threads by specifying
    the options "-SX maxthreads", "-PX maxthreads" and "-CX maxthreads".

    [7]
    If You've enabled logging remember to setup the "-Mr ndays" option depending on the
    traffic You get in Your server.
    This avoid XMail to work with very big log files and can speedup server performances.
    
    [8]
    If You're unable to start XMail even if You followed this document instructions check
    the MailRoot directory with the one listed above.
    More than one unzipper does not restore empty directories by default.

    [-]
    Please report me errors about XMail itself and about this document.
    If You successfully build and run XMail please let me know at davidel@xmailserver.org ,
    I don't want money ;)















Part 27         Known bugs

    Version 0.1 ( Alpha-1 ) :

    0.1-001     Linux   ( FIXED )
    SMail threads don't wake up upon a release semaphore by SMTP threads.

    0.27-001    Windows
    Using XMail inside a net that use MS Proxy Server 2.0 cause XMail to fail
    in sending UDP packets for DNS MX queries ( due to a bug of WS2_32.DLL linked
    to MS Proxy 2.0 ). I don't know if greater versions of MS Proxy fixes this bug.
    To makes XMail work in such environment You can use "DefaultSMTPGateways" option
    in SERVER.TAB to use smart SMTP hosts. Or better strip away MS Proxy server and
    setup a cheap PC running Linux + IP-Masquerading that cost exactly 0.0 $ and works great.
    Or use "SmartDNSHost" configuration to redirect recursion queries to a DNS smart host
    that support TCP and recursion.
















Part 28         Thanks

    My mother Adelisa, to give me the light.
    My cat Grace, for her patience to wait for food while I'm coding.
    All free source community, to give me code and knowledge.
    My company, Network Associates, to give me my wage.






