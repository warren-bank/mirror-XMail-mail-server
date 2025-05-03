@echo off

set XMAIL_HOME=C:\PortableApps\XMail\1.27

set MAIL_ROOT=%XMAIL_HOME%\MailRoot

set from=user@example.com
set RFC_format_input_file="%~dp0.\email.txt"

set options=
set options=%options% "-f%from%"
set options=%options% -t --input-file %RFC_format_input_file%

cd /D "%XMAIL_HOME%"

SendMail.exe %options%

echo.
pause
