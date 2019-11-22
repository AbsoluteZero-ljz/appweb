Foreign Thread Communications
===

This samples demonstrates how to interact from a foreign thread with Appweb.

This sample shows how to send a message from a foreign thread into Appweb in a
thread-safe manner.

Requirements
---

* [Appweb](https://www.embedthis.com/appweb/download.html)
* [MakeMe Build Tool](https://www.embedthis.com/makeme/download.html)

To build:
---
    Nothing to build. The source will be dynamically compiled.

To run:
---
    me run

The server listens on port 8080. Browse to:

     http://localhost:8080/

Code:
---
* [cache](cache) - Directory for compiled code
* [appweb.conf](appweb.conf) - Appweb server configuration file
* [message.c](chat.c) - WebSockets chat server code
* [start.me](start.me) - MakeMe build instructions

Documentation:
---
* [Appweb Documentation](https://www.embedthis.com/appweb/doc/index.html)

See Also:
---
* [secure-server - Secure server](../secure-server/README.md)
* [simple-server - Simple server and embedding API](../simple-server/README.md)
* [typical-server - Fully featured server and embedding API](../typical-server/README.md)
* [websockets-chat - Fully featured server and embedding API](../typical-server/README.md)
