SimpleHandler Sample
===

This sample shows how to create an Appweb handler module. Handlers receive to client requests and
generate responses.

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
* [simpleHandler.c](simpleHandler.c) - Simple handler
* [appweb.conf](appweb.conf) - Appweb server configuration file
* [start.me](start.me) - MakeMe build instructions

Documentation:
---
* [Appweb Documentation](https://www.embedthis.com/appweb/doc/index.html)
* [Creating Handlers](https://www.embedthis.com/appweb/doc/developers/handlers.html)
* [Creating Modules](https://www.embedthis.com/appweb/doc/developers/modules.html)
* [API Library](https://www.embedthis.com/appweb/doc/ref/native.html)
* [Configuration Directives](https://www.embedthis.com/appweb/doc/users/configuration.html#directives)

See Also:
---
* [simple-server - Simple one-line embedding API](../simple-server/README.md)
