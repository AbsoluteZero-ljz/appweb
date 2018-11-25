Tyical Server Sample
===

This sample shows how to embed Appweb using the full embedding API.

Requirements
---
* [Appweb](https://www.embedthis.com/appweb/download.html)
* [MakeMe Build Tool](https://www.embedthis.com/makeme/download.html)

To build:
---
    me 

To run:
---
    me run

The server listens on port 8080. Browse to: 
 
     http://localhost:8080/

Code:
---
* [server.c](server.c) - Main program
* [appweb.conf](appweb.conf) - Appweb server configuration file
* [auth.conf](auth.conf) - User/Password/Role authorization file
* [cgi-bin](cgi-bin) - Directory for CGI programs
* [web](web) - Web content to serve
* [start.me](start.me) - MakeMe build instructions

Documentation:
---
* [Appweb Documentation](https://www.embedthis.com/appweb/doc/index.html)
* [Configuration Directives](https://www.embedthis.com/appweb/doc/users/configuration.html#directives)
* [Sandbox Limits](https://www.embedthis.com/appweb/doc/users/dir/sandbox.html)
* [Security Considerations](https://www.embedthis.com/appweb/doc/users/security.html)
* [SSL in Appweb](https://www.embedthis.com/appweb/doc/users/ssl.html)
* [User Authentication](https://www.embedthis.com/appweb/doc/users/authentication.html)

See Also:
---
* [min-server - Minimal server configuration](../min-server/README.md)
* [simple-server - Simple one-line embedding API](../simple-server/README.md)
