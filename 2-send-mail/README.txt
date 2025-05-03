--------------------------------------------------------------------------------

test #1:
========

email.txt does not include header: "Message-ID"

result:
=======

XMail bounce: Rcpt=[xxxxx@gmail.com];Error=
  [75.xxx.xxx.xxx]
  Messages missing a valid Message-ID header are not accepted.
  For more information, review RFC 5322 specifications and go to:
    https://support.google.com/mail/?p=RfcMessageNonCompliant

--------------------------------------------------------------------------------

test #2:
========

email.txt does include the header:
Message-ID: <1@example.com>

result:
=======

XMail bounce: Rcpt=[xxxxx@gmail.com];Error=
  Your email has been blocked because the sender is unauthenticated.
  Gmail requires all senders to authenticate with either SPF or DKIM.

  Authentication results:
    DKIM = did not pass
    SPF [example.com] with ip: [75.xxx.xxx.xxx] = did not pass

  For instructions on setting up authentication, go to:
    https://support.google.com/mail/answer/81126#authentication

notes:
======

SPF (Sender Policy Framework) works by verifying that emails
claiming to be from a specific domain are actually sent from authorized servers.
Domain owners create an SPF record, a TXT record in their DNS,
listing the IP addresses or domains of servers allowed to send emails on their behalf.
When a receiving mail server gets an email,
it checks the SPF record for the sender's domain to ensure the sending server is authorized.

DKIM (DomainKeys Identified Mail) uses public/private key cryptography to authenticate emails
by verifying the sender's identity and ensuring the message hasn't been tampered with in transit.
It works by signing emails with the sender's private key,
which is then verified against a public key stored in the domain's DNS record.

--------------------------------------------------------------------------------
