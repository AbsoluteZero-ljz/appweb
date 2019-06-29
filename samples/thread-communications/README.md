ESP WebSockets Thread Sample
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
    me

To run:
---
    me run

The server listens on port 8080. Browse to:

     http://localhost:8080/

Code:
---
* [cache](cache) - Directory for compiled ESP modules
* [appweb.conf](appweb.conf) - Appweb server configuration file
* [chat.c](chat.c) - WebSockets chat server code
* [start.me](start.me) - MakeMe build instructions
* [web](web) - Directory containing the index.html web page

Documentation:
---
* [Appweb Documentation](https://www.embedthis.com/appweb/doc/index.html)
* [ESP Directives](https://www.embedthis.com/appweb/doc/users/dir/esp.html)
* [ESP Tour](https://www.embedthis.com/esp/doc/start/tour.html)
* [ESP Controllers](https://www.embedthis.com/esp/doc/users/controllers.html)
* [ESP APIs](https://www.embedthis.com/esp/doc/ref/native.html)
* [ESP Guide](https://www.embedthis.com/esp/doc/users/index.html)
* [ESP Overview](https://www.embedthis.com/esp/doc/index.html)

See Also:
---
* [esp-angular-mvc - ESP Angular MVC Application](../esp-angular-mvc/README.md)
* [esp-controller - Serving ESP controllers](../esp-controller/README.md)
* [esp-html-mvc - ESP MVC Application](../esp-html-mvc/README.md)
* [esp-page - Serving ESP pages](../esp-page/README.md)
* [secure-server - Secure server](../secure-server/README.md)
* [simple-server - Simple server and embedding API](../simple-server/README.md)
* [typical-server - Fully featured server and embedding API](../typical-server/README.md)
* [websockets-echo - WebSockets echo server](../websockets-echo/README.md)
