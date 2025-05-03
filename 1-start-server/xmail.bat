@echo off

set XMAIL_HOME=C:\PortableApps\XMail\1.27

set MAIL_ROOT=%XMAIL_HOME%\MailRoot

set options=
set options=%options% --debug
set options=%options% -P- -B- -X- -Y- -F- -C- -W-
set options=%options% -Sp "25" -SI "0.0.0.0:25"
set options=%options% -Sl

cd /D "%XMAIL_HOME%"

XMail.exe %options%

echo.
pause
