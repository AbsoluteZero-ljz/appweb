{
    title:  'Migrating',
    crumbs: [
        { "Developer's Guide": '../developers/' },
    ],
}


# Migrating to Appweb 8

## Architectural Changes

Appweb 8 includes some major architectural changes to support the [HTTP/2](https://en.wikipedia.org/wiki/HTTP/2) protocol.

HTTP/2 is a higher performance binary protocol that supports multiplexing multiple request streams over a single network connection and offers decreased latency and improved efficiency. This necessitates some API changes and so applications, handlers and filters may need some minor refactoring to work with Appweb 8.

The architectural changes in Appweb 8 are:

* HTTP/2 protocol support and filter
* Improved pipeline handling to support HTTP/2 multiplexing
* New HttpNet structure defines the network socket.
* The existing HttpConn structure is renamed HttpStream.
* The Upload filter is enabled by default and always part of pipeline
* Upload directory now not per route — needs to be defined before routing
* New Auth Type "app"
* Removed the Send Connector as all I/O must now go through the Net Connector
* Improved OpenSSL support for either 1.0 or 1.2
* ESP improvements to make compiling more flexible

## Changed APIs

### Networks and Connections

The previous HttpConn structure contained information regarding the network connection and the current request. With HTTP/2 a single network connection must support multiple simultaneous requests. To support this, the HttpNet structure is introduced and it assumes the management of the network connection. The HttpConn structure is renamed HttpStream and it retains responsibility for a single request and response.

Some HttpConn APIs have been migrated to HttpNet and consequently now take a HttpNet* parameter as their argument.

For compatibility, a CPP define is provided for HttpConn which maps to HttpStream. Similarly, defines are provided which map legacy HttpConn APIs to their HttpStream equivalents. However, you should refactor your applications and rename all HttpConn references to HttpStream. You should also change your "conn" variable declarations to be "stream" for clarity.

Here is a list of the API changes:

- Added HttpNet structure and httpCreateNet, httpDestroyNet APIs.
- httpRequest - gains a protocol argument to nominate HTTP/1 or HTTP/2 for client requests.
- httpTrace -- the first argument is changed from HttpConn to HttpTrace so that this API can be used for both HttpNet and HttpStream related trace.
- The httpCreateConn API is renamed to httpCreateStream and its arguments are changed to a single HttpNet argument. An HttpNet instance must be created prior to creating a HttpStream.
- The httpSteal APIs are changed to work with HttpNet instances.
- Several httpGetConn*, httpSetConn*, http..Conn... APIs are replaced with "Stream" equivalents.
- These APIs which previously operated on HttpConn/HttpStream objects now operating on HttpNet instances: httpSetAsync, httpServiceQueues, httpEnableConnEvents, httpIOEvent
- Web sockets ESP callbacks must now be prepared to handle multiple packets on their READABLE events.
- The "AddConnector" configuration directive is now a NOP as the net connector is the only supported connector.
- New httpSetAuthStoreVerify API for globally setting the auth verify callback

## ESP Changes

Here is a list of ESP changes:

- The esp.json "esp.app.source" configuration directive can specify a list of source files to build. Previously, ESP would only compile app.c.
- Support "esp.app.tokens" to specify CFLAGS, DFLAGS, LDFLAGS for compiler definitions and libraries.
- Handle source file names with "-" in the filename. 
