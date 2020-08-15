/*
    proxyHandler.c -- Proxy handler

    MOB - put doc here

    - Maybe optional launch, try connect if already running
    - Perhaps use HTTP/2 on the backend?
    - Perhaps use SSL on the backend

    Add to DOC to say proxy and fast are Unix only

    Always pass actual Host header
    Headers?
        X-Forwarded-For     Client-IP
        X-Forwarded-Host    Original Host header
        X-Forwarded-Server  Hostname of the proxy server

    Questions
        - Use HTTP/2 for backend or only HTTP/1
        - WebSockets

    The proxy modules supports launching backend applications and connecting to pre-existing applications.
    It will multiplex multiple simultaneous requests to one or more apps.

    <Route /proxy>
        AddHandler proxyHandler
        Prefix /proxy
        ProxyConnect 127.0.0.1:9991 launch=/program min=1 max=2 count=500 timeout=5mins multiplex=1
    </Route>

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/*********************************** Includes *********************************/

#include    "appweb.h"

#if ME_COM_PROXY && ME_UNIX_LIKE
/************************************ Locals ***********************************/

#define PROXY_VERSION            1
#define PROXY_DEBUG              1           //  For debugging (keeps filedes open in Proxy for debug output)

/*
    MOB - remove?
    Proxy packet types
 */
#define PROXY_REAP               0           //  Proxy has been reaped (not part of spec)
#define PROXY_BEGIN_REQUEST      1           //  Start new request - sent to Proxy
#define PROXY_ABORT_REQUEST      2           //  Abort request - sent to Proxy
#define PROXY_END_REQUEST        3           //  End request - received from Proxy

#if UNUSED
#define PROXY_PARAMS             4           //  Send params to Proxy
#endif
#define PROXY_STDIN              5           //  Post body data
#define PROXY_STDOUT             6           //  Response body
#define PROXY_STDERR             7           //  Proxy app errors

#if UNUSED
//  MOB - not
#define PROXY_GET_VALUES         9           //  Query Proxy app (unused)
#define PROXY_GET_VALUES_RESULT  10          //  Query result
#define PROXY_UNKNOWN_TYPE       11          //  Unknown management request
#endif
//  MOB - revise
#define PROXY_MAX                11          //  Max type

/*
    Pseudo types
 */
 #define PROXY_COMMS_ERROR       12          //  Communications error

 // MOB - REVISE
static cchar *proxyTypes[PROXY_MAX + 1] = {
    "invalid", "begin", "abort", "end", "params", "stdin", "stdout", "stderr",
    "data", "get-values", "get-values-result", "unknown",
};

#if UNUSED
/*
    Proxy app types
 */
#define PROXY_RESPONDER          1           //  Supported web request responder
#endif

/*
    Default constants. WARNING: this code does not yet support multiple requests per proxy
 */
#define PROXY_MAX_PROXIES        1           //  Max of one proxy
#define PROXY_MIN_PROXIES        1           //  Min of one proxy (keep running after started)
#define PROXY_MAX_REQUESTS       MAXINT64    //  Max number of requests per proxy instance
#define PROXY_MAX_MULTIPLEX      1           //  Max number of concurrent requests per proxy instance

#define PROXY_PACKET_SIZE        8           //  Size of minimal Proxy packet
#define PROXY_KEEP_CONN          1           //  Flag to app to keep connection open

#define PROXY_Q_SIZE             ((PROXY_PACKET_SIZE + 65535 + 8) * 2)

#define PROXY_REQUEST_COMPLETE   0           //  End Request response status for request complete
#define PROXY_CANT_MPX_CONN      1           //  Request rejected -- Proxy app cannot multiplex requests
#define PROXY_OVERLOADED         2           //  Request rejected -- app server is overloaded
#define PROXY_UNKNOWN_ROLE       3           //  Request rejected -- unknown role

#define PROXY_WAIT_TIMEOUT       (30 * TPS)  //  Time to wait for a proxy
#define PROXY_CONNECT_TIMEOUT    (10 * TPS)  //  Time to wait for Proxy to respond to a connect
#define PROXY_PROXY_TIMEOUT      (300 * TPS) //  Default inactivity time to preserve idle proxy
#define PROXY_REAP_TIMEOUT       (10 * TPS)  //  Time to wait for kill proxy to take effect
#define PROXY_WATCHDOG_TIMEOUT   (60 * TPS)  //  Frequence to check on idle proxies

/*
    Top level Proxy structure per route
 */
typedef struct Proxy {
    cchar           *endpoint;              //  App listening endpoint
    cchar           *launch;                //  Launch path
    int             multiplex;              //  Maximum number of requests to send to each app
    int             minApps;                //  Minumum number of proxies to maintain
    int             maxApps;                //  Maximum number of proxies to spawn
    uint64          maxRequests;            //  Maximum number of requests for launched apps before respawning
    MprTicks        proxyTimeout;           //  Timeout for an idle proxy to be maintained
    MprList         *apps;                  //  List of active apps
    MprList         *idleApps;              //  Idle apps
    MprMutex        *mutex;                 //  Multithread sync
    MprCond         *cond;                  //  Condition to wait for available app
    MprEvent        *timer;                 //  Timer to check for idle apps
    cchar           *ip;                    //  Listening IP address
    int             port;                   //  Listening port
} Proxy;

/*
    Per app instance
 */
typedef struct ProxyApp {
    Proxy           *proxy;                 // Parent proxy pointer
    HttpTrace       *trace;                 // Default tracing configuration
    MprTicks        lastActive;             // When last active
    MprSignal       *signal;                // Mpr signal handler for child death
    bool            destroy;                // Must destroy app
    int             inUse;                  // In use counter
    int             pid;                    // Process ID of the app
    uint64          nextID;                 // Next request ID for this app
    MprList         *comms;                 // Connectors for each request
} ProxyApp;

/*
    Per Proxy comms instance. This is separate from the ProxyApp properties because the
    ProxyComm executes on a different dispatcher.
 */
typedef struct ProxyComm {
    Proxy           *proxy;                 // Parent proxy pointer
    ProxyApp        *app;                   // Owning app
    MprSocket       *socket;                // I/O socket
    HttpStream      *stream;                // Owning client request stream
    HttpQueue       *writeq;                // Queue to write to the app
    HttpQueue       *readq;                 // Queue to hold read data from the app
    HttpTrace       *trace;                 // Default tracing configuration
    uint64          reqID;                  // Request ID - assigned from ProxyApp.nextID
    bool            eof;                    // Socket is closed
    bool            parsedHeaders;          // Parsed the app header response
    bool            writeBlocked;           // Socket is full of write data
} ProxyComm;

/*********************************** Forwards *********************************/

static void addProxyPacket(HttpQueue *q, HttpPacket *packet);
static void addToProxyVector(HttpQueue *q, char *ptr, ssize bytes);
static void adjustProxyVec(HttpQueue *q, ssize written);
static Proxy *allocProxy(void);
static ProxyComm *allocProxyComm(ProxyApp *app, HttpStream *stream);
static ProxyApp *allocProxyApp(Proxy *proxy, HttpStream *stream);
static cchar *buildProxyArgs(HttpStream *stream, Proxy *proxy, int *argcp, cchar ***argvp);
static MprOff buildProxyVec(HttpQueue *q);
static void closeProxy(HttpQueue *q);
static ProxyComm *connectProxyComm(ProxyApp *app, HttpStream *stream);
#if UNUSED
static void copyProxyInner(HttpPacket *packet, cchar *key, cchar *value, cchar *prefix);
static void copyProxyParams(HttpPacket *packet, MprJson *params, cchar *prefix);
static void copyProxyVars(HttpPacket *packet, MprHash *vars, cchar *prefix);
#endif
static HttpPacket *createProxyPacket(HttpQueue *q, int type, HttpPacket *packet);
static MprSocket *createListener(ProxyApp *app, HttpStream *stream);
static void enableProxyCommEvents(ProxyComm *comm);
static void proxyConnectorIO(ProxyComm *comm, MprEvent *event);
static void proxyConnectorIncoming(HttpQueue *q, HttpPacket *packet);
static void proxyConnectorIncomingService(HttpQueue *q);
static void proxyConnectorOutgoingService(HttpQueue *q);
static void proxyHandlerReapResponse(ProxyComm *comm);
static void proxyHandlerResponse(ProxyComm *comm, int type, HttpPacket *packet);
static void proxyIncoming(HttpQueue *q, HttpPacket *packet);
static int proxyConnectDirective(MaState *state, cchar *key, cchar *value);
static void freeProxyPackets(HttpQueue *q, ssize bytes);
static Proxy *getProxy(HttpRoute *route);
static char *getProxyToken(MprBuf *buf, cchar *delim);
static ProxyApp *getProxyApp(Proxy *proxy, HttpStream *stream);
static int getListenPort(MprSocket *socket);
static void killProxyApp(ProxyApp *app);
static void manageProxy(Proxy *proxy, int flags);
static void manageProxyApp(ProxyApp *app, int flags);
static void manageProxyComm(ProxyComm *proxyConnector, int flags);
static int openProxy(HttpQueue *q);
static bool parseProxyHeaders(HttpPacket *packet);
static bool parseProxyResponseLine(HttpPacket *packet);
#if UNUSED
static void prepProxyRequestStart(HttpQueue *q);
static void prepProxyRequestParams(HttpQueue *q);
#endif
static void reapSignalHandler(ProxyApp *app, MprSignal *sp);
static ProxyApp *startProxyApp(Proxy *proxy, HttpStream *stream);
static void terminateIdleProxyApps(Proxy *proxy);

/************************************* Code ***********************************/
/*
    Loadable module initialization
 */
PUBLIC int httpProxyInit(Http *http, MprModule *module)
{
    HttpStage   *handler, *connector;

    /*
        Add configuration file directives
     */
    maAddDirective("ProxyConnect", proxyConnectDirective);

    /*
        Create Proxy handler to respond to client requests
     */
    if ((handler = httpCreateHandler("proxyHandler", module)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->proxyHandler = handler;
    handler->close = closeProxy;
    handler->open = openProxy;
    handler->incoming = proxyIncoming;

    /*
        Create Proxy connector. The connector manages communication to the Proxy application.
        The Proxy handler is the head of the pipeline while the Proxy connector is
        after the Http protocol and tailFilter.
    */
    if ((connector = httpCreateConnector("proxyConnector", NULL)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->proxyConnector = connector;
    connector->incoming = proxyConnectorIncoming;
    connector->incomingService = proxyConnectorIncomingService;
    connector->outgoingService = proxyConnectorOutgoingService;
    return 0;
}


/*
    Open the proxyHandler for a new client request
 */
static int openProxy(HttpQueue *q)
{
    Http        *http;
    HttpNet     *net;
    HttpStream  *stream;
    Proxy       *proxy;
    ProxyApp    *app;
    ProxyComm   *comm;

    net = q->net;
    stream = q->stream;
    http = stream->http;

#if UNUSED
    httpTrimExtraPath(stream);
    httpMapFile(stream);
    httpCreateCGIParams(stream);
#endif

    /*
        Get a Proxy instance for this route. First time, this will allocate a new Proxy instance. Second and
        subsequent times, will reuse the existing instance.
     */
    proxy = getProxy(stream->rx->route);

    /*
        Get a ProxyApp instance. This will reuse an existing Proxy app if possible. Otherwise,
        it will launch a new Proxy app if within limits. Otherwise it will wait until one becomes available.
     */
    if ((app = getProxyApp(proxy, stream)) == 0) {
        httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot allocate ProxyApp for route %s", stream->rx->route->pattern);
        return MPR_ERR_CANT_OPEN;
    }

    /*
        Open a dedicated client socket to the Proxy app
     */
    if ((comm = connectProxyComm(app, stream)) == NULL) {
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot connect to proxy app: %d", errno);
        return MPR_ERR_CANT_CONNECT;
    }
    mprAddItem(app->comms, comm);
    q->queueData = q->pair->queueData = comm;

#if UNUSED
    /*
        Send a start request followed by the request parameters
     */
    prepProxyRequestStart(q);
    prepProxyRequestParams(q);
#endif
    enableProxyCommEvents(comm);
    return 0;
}


/*
    Release a proxy app and comm when the request completes. This closes the connection to the Proxy app.
    It will destroy the Proxy app on errors or if the number of requests exceeds the maxRequests limit.
 */
static void closeProxy(HttpQueue *q)
{
    Proxy       *proxy;
    ProxyComm   *comm;
    ProxyApp    *app;
    cchar       *msg;

    comm = q->queueData;
    proxy = comm->proxy;
    app = comm->app;

    lock(proxy);

    if (comm->socket) {
        mprCloseSocket(comm->socket, 1);
        mprRemoveSocketHandler(comm->socket);
        comm->socket = 0;
    }
    mprRemoveItem(app->comms, comm);

    if (--app->inUse <= 0) {
        if (mprRemoveItem(proxy->apps, app) < 0) {
            httpLog(app->trace, "proxy", "error", "msg:'Cannot find proxy app in list'");
        }
        if (app->destroy || (proxy->maxRequests < MAXINT64 && app->nextID >= proxy->maxRequests) ||
                (mprGetListLength(proxy->apps) + mprGetListLength(proxy->idleApps) >= proxy->minApps)) {
            msg = "Destroy Proxy app";
            killProxyApp(app);
        } else {
            msg = "Release Proxy app";
            app->lastActive = mprGetTicks();
            mprAddItem(proxy->idleApps, app);
        }
        httpLog(app->trace, "proxy", "context",
            "msg:'%s', pid:%d, idle:%d, active:%d, id:%lld, maxRequests:%lld, destroy:%d, nextId:%lld",
            msg, app->pid, mprGetListLength(proxy->idleApps), mprGetListLength(proxy->apps),
            app->nextID, proxy->maxRequests, app->destroy, app->nextID);
        mprSignalCond(proxy->cond);
    }
    unlock(proxy);
}


static Proxy *allocProxy(void)
{
    Proxy    *proxy;

    proxy = mprAllocObj(Proxy, manageProxy);
    proxy->apps = mprCreateList(0, 0);
    proxy->idleApps = mprCreateList(0, 0);
    proxy->mutex = mprCreateLock();
    proxy->cond = mprCreateCond();
    proxy->multiplex = PROXY_MAX_MULTIPLEX;
    proxy->maxRequests = PROXY_MAX_REQUESTS;
    proxy->minApps = PROXY_MIN_PROXIES;
    proxy->maxApps = PROXY_MAX_PROXIES;
    proxy->ip = sclone("127.0.0.1");
    proxy->port = 0;
    proxy->proxyTimeout = PROXY_PROXY_TIMEOUT;
    proxy->timer = mprCreateTimerEvent(NULL, "proxy-watchdog", PROXY_WATCHDOG_TIMEOUT,
        terminateIdleProxyApps, proxy, MPR_EVENT_QUICK);
    return proxy;
}


static void manageProxy(Proxy *proxy, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(proxy->cond);
        mprMark(proxy->endpoint);
        mprMark(proxy->idleApps);
        mprMark(proxy->ip);
        mprMark(proxy->launch);
        mprMark(proxy->mutex);
        mprMark(proxy->apps);
        mprMark(proxy->timer);
    }
}


static void terminateIdleProxyApps(Proxy *proxy)
{
    ProxyApp    *app;
    MprTicks    now;
    int         count, next;

    lock(proxy);
    now = mprGetTicks();
    count = mprGetListLength(proxy->apps) + mprGetListLength(proxy->idleApps);
    for (ITERATE_ITEMS(proxy->idleApps, app, next)) {
        if (app->pid && ((now - app->lastActive) > proxy->proxyTimeout)) {
            if (count-- > proxy->minApps) {
                killProxyApp(app);
            }
        }
    }
    unlock(proxy);
}


/*
    Get the proxy structure for a route and save in "eroute". Allocate if required.
    One Proxy instance is shared by all using the route.
 */
static Proxy *getProxy(HttpRoute *route)
{
    Proxy        *proxy;

    if ((proxy = route->eroute) == 0) {
        mprGlobalLock();
        if ((proxy = route->eroute) == 0) {
            proxy = route->eroute = allocProxy();
        }
        mprGlobalUnlock();
    }
    return proxy;
}


/*
    POST/PUT incoming body data from the client destined for the CGI gateway. : For POST "form" requests,
    this will be called before the command is actually started.
 */
static void proxyIncoming(HttpQueue *q, HttpPacket *packet)
{
    HttpStream  *stream;
    ProxyComm    *comm;

    assert(q);
    assert(packet);
    stream = q->stream;

    if ((comm = q->queueData) == 0) {
        return;
    }
    if (httpGetPacketLength(packet) == 0) {
        /* End of input */
        httpFinalizeInput(stream);
        if (stream->rx->remainingContent > 0) {
            httpError(stream, HTTP_CODE_BAD_REQUEST, "Client supplied insufficient body data");
            packet = createProxyPacket(q, PROXY_ABORT_REQUEST, httpCreateDataPacket(0));

        } else {
            createProxyPacket(q, PROXY_STDIN, packet);
        }
    } else {
        createProxyPacket(q, PROXY_STDIN, packet);
    }
    httpPutForService(comm->writeq, packet, HTTP_SCHEDULE_QUEUE);
}


static void proxyHandlerReapResponse(ProxyComm *comm)
{
    proxyHandlerResponse(comm, PROXY_REAP, NULL);
}


/*
    Handle response messages from the Proxy app
    MOB - revise not relevant
 */
static void proxyHandlerResponse(ProxyComm *comm, int type, HttpPacket *packet)
{
    ProxyApp    *app;
    HttpStream  *stream;
    HttpRx      *rx;
    MprBuf      *buf;
    int         status, protoStatus;

    stream = comm->stream;
    app = comm->app;

    if (stream->state <= HTTP_STATE_BEGIN || stream->rx->route == NULL) {
        /* Request already complete and stream has been recycled (prepared for next request) */
        return;
    }

    if (type == PROXY_COMMS_ERROR) {
        httpError(stream, HTTP_ABORT | HTTP_CODE_COMMS_ERROR, "ProxyComm: comms error");

    } else if (type == PROXY_REAP) {
        //  Reap may happen before valid I/O has drained
        // httpError(stream, HTTP_ABORT | HTTP_CODE_COMMS_ERROR, "ProxyComm: proxy app killed error");

    } else if (type == PROXY_END_REQUEST && packet) {
        if (httpGetPacketLength(packet) < 8) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "Proxy bad request end packet");
            return;
        }
        buf = packet->content;
        rx = stream->rx;

        status = mprGetCharFromBuf(buf) << 24 || mprGetCharFromBuf(buf) << 16 ||
                 mprGetCharFromBuf(buf) << 8 || mprGetCharFromBuf(buf);
        protoStatus = mprGetCharFromBuf(buf);
        mprAdjustBufStart(buf, 3);

        //  MOB - these are not right
        if (protoStatus == PROXY_REQUEST_COMPLETE) {
            httpLog(app->trace, "proxy.rx", "context", "msg:'Request complete', id:%lld, status:%d", comm->reqID, status);

        } else if (protoStatus == PROXY_CANT_MPX_CONN) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "Proxy cannot multiplex requests %s", rx->uri);
            return;

        } else if (protoStatus == PROXY_OVERLOADED) {
            httpError(stream, HTTP_CODE_SERVICE_UNAVAILABLE, "Proxy overloaded %s", rx->uri);
            return;

        } else if (protoStatus == PROXY_UNKNOWN_ROLE) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "Proxy unknown role %s", rx->uri);
            return;
        }
        httpLog(app->trace, "proxy.rx.eof", "detail", "msg:'Proxy end request', id:%lld", comm->reqID);
        httpFinalizeOutput(stream);

    } else if (type == PROXY_STDOUT && packet) {
        if (!comm->parsedHeaders) {
            if (!parseProxyHeaders(packet)) {
                return;
            }
            comm->parsedHeaders = 1;
        }
        if (httpGetPacketLength(packet) > 0) {
            httpLogPacket(app->trace, "proxy.rx.data", "packet", 0, packet, "type:%d, id:%lld, len:%ld", type, comm->reqID,
                httpGetPacketLength(packet));
            httpPutPacketToNext(stream->writeq, packet);
        }
    }
    httpProcess(stream->inputq);
}


/*
    Parse the Proxy app output headers. Sample Proxy program output:
        Content-type: text/html
        <html.....
 */
static bool parseProxyHeaders(HttpPacket *packet)
{
    ProxyComm   *comm;
    HttpStream  *stream;
    MprBuf      *buf;
    char        *endHeaders, *headers, *key, *value;
    ssize       blen, len;

    stream = packet->stream;
    comm = packet->data;
    buf = packet->content;
    headers = mprGetBufStart(buf);
    value = 0;

    headers = mprGetBufStart(buf);
    blen = mprGetBufLength(buf);
    len = 0;

    if ((endHeaders = sncontains(headers, "\r\n\r\n", blen)) == NULL) {
        if ((endHeaders = sncontains(headers, "\n\n", blen)) == NULL) {
            if (slen(headers) < ME_MAX_HEADERS) {
                /* Not EOF and less than max headers and have not yet seen an end of headers delimiter */
                httpLog(comm->trace, "proxy.rx", "detail", "msg:'Proxy incomplete headers', id:%lld", comm->reqID);
                return 0;
            }
        }
        len = 2;
    } else {
        len = 4;
    }
    /*
        Split the headers from the body. Add null to ensure we can search for line terminators.
     */
    if (endHeaders) {
        endHeaders[len - 1] = '\0';
        endHeaders += len;
    }

    /*
        Want to be tolerant of Proxy programs that omit the status line.
        MOB - what about HTTP/2
     */
    if (strncmp((char*) buf->start, "HTTP/1.", 7) == 0) {
        //  MOB - the line must be passed onto the client
        if (!parseProxyResponseLine(packet)) {
            /* httpError already called */
            return 0;
        }
    }
    if (endHeaders && strchr(mprGetBufStart(buf), ':')) {
        while (mprGetBufLength(buf) > 0 && buf->start[0] && (buf->start[0] != '\r' && buf->start[0] != '\n')) {
            if ((key = getProxyToken(buf, ":")) == 0) {
                key = "Bad Header";
            }
            value = getProxyToken(buf, "\n");
            while (isspace((uchar) *value)) {
                value++;
            }
            len = (int) strlen(value);
            while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == '\n')) {
                value[len - 1] = '\0';
                len--;
            }
            httpLog(stream->trace, "proxy.rx", "detail", "key:'%s', value: '%s'", key, value);
            if (scaselesscmp(key, "location") == 0) {
                httpRedirect(stream, HTTP_CODE_MOVED_TEMPORARILY, value);

            } else if (scaselesscmp(key, "status") == 0) {
                httpSetStatus(stream, atoi(value));

            } else if (scaselesscmp(key, "content-type") == 0) {
                httpSetHeaderString(stream, "Content-Type", value);

            } else if (scaselesscmp(key, "content-length") == 0) {
                httpSetContentLength(stream, (MprOff) stoi(value));
                httpSetChunkSize(stream, 0);

            } else if (scaselesscmp(key, "location") == 0) {
                //  MOB - need to map the Location header
                key = ssplit(key, ":\r\n\t ", NULL);
                httpSetHeaderString(stream, key, value);

            } else {
                /* Now pass all other headers back to the client */
                //  MOB - do we need to validate any headers here?
                key = ssplit(key, ":\r\n\t ", NULL);
                httpSetHeaderString(stream, key, value);
            }
        }
        buf->start = endHeaders;
    }
    return 1;
}


/*
    Parse the first response line
 */
static bool parseProxyResponseLine(HttpPacket *packet)
{
    MprBuf      *buf;
    char        *protocol, *status, *msg;

    //  MOB - this must be passed onto the client
    buf = packet->content;
    protocol = getProxyToken(buf, " ");
    if (protocol == 0 || protocol[0] == '\0') {
        httpError(packet->stream, HTTP_CODE_BAD_GATEWAY, "Bad CGI HTTP protocol response");
        return 0;
    }
    if (strncmp(protocol, "HTTP/1.", 7) != 0) {
        httpError(packet->stream, HTTP_CODE_BAD_GATEWAY, "Unsupported CGI protocol");
        return 0;
    }
    status = getProxyToken(buf, " ");
    if (status == 0 || *status == '\0') {
        httpError(packet->stream, HTTP_CODE_BAD_GATEWAY, "Bad CGI header response");
        return 0;
    }
    msg = getProxyToken(buf, "\n");
    mprDebug("http cgi", 4, "CGI response status: %s %s %s", protocol, status, msg);
    return 1;
}


/*
    Get the next input token. The content buffer is advanced to the next token. This routine always returns a
    non-zero token. The empty string means the delimiter was not found.
 */
static char *getProxyToken(MprBuf *buf, cchar *delim)
{
    char    *token, *nextToken;
    ssize   len;

    len = mprGetBufLength(buf);
    if (len == 0) {
        return "";
    }
    token = mprGetBufStart(buf);
    nextToken = sncontains(mprGetBufStart(buf), delim, len);
    if (nextToken) {
        *nextToken = '\0';
        len = (int) strlen(delim);
        nextToken += len;
        buf->start = nextToken;

    } else {
        buf->start = mprGetBufEnd(buf);
    }
    return token;
}


/************************************************ ProxyApp ***************************************************************/
/*
    The ProxyApp represents the connection to a single Proxy app instance
 */
static ProxyApp *allocProxyApp(Proxy *proxy, HttpStream *stream)
{
    ProxyApp   *app;

    app = mprAllocObj(ProxyApp, manageProxyApp);
    app->proxy = proxy;
    app->trace = stream->net->trace;
    app->comms = mprCreateList(0, 0);

    /*
        The requestID must start at 1 by spec
     */
    app->nextID = 1;
    return app;
}


static void manageProxyApp(ProxyApp *app, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(app->comms);
        mprMark(app->proxy);
        mprMark(app->signal);
        mprMark(app->trace);
    }
}


static ProxyApp *getProxyApp(Proxy *proxy, HttpStream *stream)
{
    ProxyApp    *app;
    MprTicks    timeout;
    int         idle, next;

    lock(proxy);
    app = NULL;
    timeout = mprGetTicks() +  PROXY_WAIT_TIMEOUT;

    /*
        Locate a ProxyApp to serve the request. Use an idle proxy app first. If none available, start a new proxy app
        if under the limits. Otherwise, wait for one to become available.
     */
    while (!app && mprGetTicks() < timeout) {
        idle = mprGetListLength(proxy->idleApps);
        if (idle > 0) {
            app = mprGetFirstItem(proxy->idleApps);
            mprRemoveItemAtPos(proxy->idleApps, 0);
            mprAddItem(proxy->apps, app);
            break;

        } else if (mprGetListLength(proxy->apps) < proxy->maxApps) {
            if ((app = startProxyApp(proxy, stream)) != 0) {
                mprAddItem(proxy->apps, app);
            }
            break;

        } else {
            for (ITERATE_ITEMS(proxy->apps, app, next)) {
                if (mprGetListLength(app->comms) < proxy->multiplex) {
                    break;
                }
            }
            if (app) {
                break;
            }
            unlock(proxy);
            //  TEST
            if (mprWaitForCond(proxy->cond, TPS) < 0) {
                return NULL;
            }
            lock(proxy);
        }
    }
    if (app) {
        app->lastActive = mprGetTicks();
        app->inUse++;
    }
    unlock(proxy);
    return app;
}


/*
    Start a new Proxy app process. Called with lock(proxy)
 */
static ProxyApp *startProxyApp(Proxy *proxy, HttpStream *stream)
{
    ProxyApp    *app;
    HttpRoute   *route;
    MprSocket   *listen;
    cchar       **argv, *command;
    int         argc, i;

    route = stream->rx->route;
    app = allocProxyApp(proxy, stream);

    if (proxy->launch) {
        if ((command = buildProxyArgs(stream, proxy, &argc, &argv)) == 0) {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot find Proxy app command");
            return NULL;
        }
        httpLog(stream->trace, "proxy", "context", "msg:'Start Proxy app', command:'%s'", command);

        if ((listen = createListener(app, stream)) == NULL) {
            return NULL;
        }
        if (!app->signal) {
            app->signal = mprAddSignalHandler(SIGCHLD, reapSignalHandler, app, NULL, MPR_SIGNAL_BEFORE);
        }
        if ((app->pid = fork()) < 0) {
            fprintf(stderr, "Fork failed for Proxy");
            return NULL;

        } else if (app->pid == 0) {
            /* Child */
            dup2(listen->fd, 0);
            /*
                When debugging, keep stdout/stderr open so printf/fprintf from the Proxy app will show in the console.
             */
            for (i = PROXY_DEBUG ? 3 : 1; i < 128; i++) {
                close(i);
            }
            // FUTURE envp[0] = sfmt("FCGI_WEB_SERVER_ADDRS=%s", route->host->defaultEndpoint->ip);
            if (execve(command, (char**) argv, NULL /* (char**) &env->items[0] */) < 0) {
                printf("Cannot exec proxy app: %s\n", command);
            }
            return NULL;
        } else {
            httpLog(app->trace, "proxy", "context", "msg:'Proxy started', command:'%s', pid:%d", command, app->pid);
            mprCloseSocket(listen, 0);
        }
    }
    return app;
}


/*
    Build the command arguments for the Proxy app
 */
static cchar *buildProxyArgs(HttpStream *stream, Proxy *proxy, int *argcp, cchar ***argvp)
{
    HttpRx      *rx;
    HttpTx      *tx;
    cchar       *cp, *fileName, *query;
    char        **argv, *tok;
    ssize       len;
    int         argc, argind, i;

    rx = stream->rx;
    tx = stream->tx;
    fileName = tx->filename;

    argind = 0;
    argc = 2;
    /*
        Count the args for ISINDEX queries. Only valid if there is not a "=" in the query.
        If this is so, then we must not have these args in the query env also?
     */
    query = (char*) rx->parsedUri->query;
    if (query && !schr(query, '=')) {
        argc++;
        for (cp = query; *cp; cp++) {
            if (*cp == '+') {
                argc++;
            }
        }
    } else {
        query = 0;
    }
    len = (argc + 1) * sizeof(char*);
    argv = mprAlloc(len);

    argv[argind++] = (char*) proxy->launch;
    argv[argind++] = sclone(fileName);
    /*
        ISINDEX queries. Only valid if there is not a "=" in the query. If this is so, then we must not
        have these args in the query env also?
        FUTURE - should query vars be set in the env?
     */
    if (query) {
        cp = stok(sclone(query), "+", &tok);
        while (cp) {
            argv[argind++] = mprEscapeCmd(mprUriDecode(cp), 0);
            cp = stok(NULL, "+", &tok);
        }
    }
    assert(argind <= argc);
    argv[argind] = 0;
    *argcp = argc;
    *argvp = (cchar**) argv;

    mprDebug("http proxy", 6, "Proxy: command:");
    for (i = 0; i < argind; i++) {
        mprDebug("http proxy", 6, "   argv[%d] = %s", i, argv[i]);
    }
    return argv[0];
}


/*
    Proxy process has died, so reap the status and inform relevant streams.
    WARNING: this may be called before all the data has been read from the socket, so we must not set eof = 1 here.
    WARNING: runs on the MPR dispatcher. Everyting must be "proxy" locked.
 */
static void reapSignalHandler(ProxyApp *app, MprSignal *sp)
{
    Proxy       *proxy;
    ProxyComm   *comm;
    int         next, status;

    proxy = app->proxy;

    lock(proxy);
    if (app->pid && waitpid(app->pid, &status, WNOHANG) == app->pid) {
        httpLog(app->trace, "proxy", WEXITSTATUS(status) == 0 ? "context" : "error",
            "msg:'Proxy exited', pid:%d, status:%d", app->pid, WEXITSTATUS(status));
        if (app->signal) {
            mprRemoveSignalHandler(app->signal);
            app->signal = 0;
        }
        if (mprLookupItem(app->proxy->idleApps, app) >= 0) {
            mprRemoveItem(app->proxy->idleApps, app);
        }
        app->destroy = 1;
        app->pid = 0;

        /*
            Notify all comms on their relevant dispatcher
         */
        for (ITERATE_ITEMS(app->comms, comm, next)) {
            mprCreateEvent(comm->stream->dispatcher, "proxy-reap", 0, proxyHandlerReapResponse, comm, 0);
        }
    }
    unlock(proxy);
}


/*
    Kill the Proxy app due to error or maxRequests limit being exceeded
 */
static void killProxyApp(ProxyApp *app)
{
    lock(app->proxy);
    if (app->pid) {
        httpLog(app->trace, "proxy", "context", "msg: 'Kill Proxy process', pid:%d", app->pid);
        if (app->pid) {
            kill(app->pid, SIGTERM);
        }
    }
    unlock(app->proxy);
}


/*
    Create a socket connection to the Proxy app. Retry if the Proxy is not yet ready.
 */
static ProxyComm *connectProxyComm(ProxyApp *app, HttpStream *stream)
{
    Proxy       *proxy;
    ProxyComm   *comm;
    MprTicks    timeout;
    int         retries, connected;

    proxy = app->proxy;

    lock(proxy);
    connected = 0;
    retries = 1;

    comm = allocProxyComm(app, stream);

    timeout = mprGetTicks() +  PROXY_CONNECT_TIMEOUT;
    while (1) {
        httpLog(stream->trace, "proxy.rx", "request", "Proxy try to connect to %s:%d", proxy->ip, proxy->port);
        comm->socket = mprCreateSocket();
        if (mprConnectSocket(comm->socket, proxy->ip, proxy->port, 0) == 0) {
            connected = 1;
            break;
        }
        if (mprGetTicks() >= timeout) {
            unlock(proxy);
            return NULL;
        }
        mprSleep(50 * retries++);
    }

    unlock(proxy);
    return comm;
}


/*
    Add the Proxy spec packet header to the packet->prefix
 */
static HttpPacket *createProxyPacket(HttpQueue *q, int type, HttpPacket *packet)
{
    ProxyComm    *comm;
    uchar       *buf;
    ssize       len, pad;

    comm = q->queueData;
    if (!packet) {
        packet = httpCreateDataPacket(0);
    }
    len = httpGetPacketLength(packet);

    packet->prefix = mprCreateBuf(16, 16);
    buf = (uchar*) packet->prefix->start;
    *buf++ = PROXY_VERSION;
    *buf++ = type;

    *buf++ = (uchar) (comm->reqID >> 8);
    *buf++ = (uchar) (comm->reqID & 0xFF);

    *buf++ = (uchar) (len >> 8);
    *buf++ = (uchar) (len & 0xFF);

    /*
        Use 8 byte padding alignment
     */
    pad = (len % 8) ? (8 - (len % 8)) : 0;
    if (pad > 0) {
        if (mprGetBufSpace(packet->content) < pad) {
            mprGrowBuf(packet->content, pad);
        }
        mprAdjustBufEnd(packet->content, pad);
    }
    *buf++ = (uchar) pad;
    mprAdjustBufEnd(packet->prefix, 8);

    httpLog(comm->trace, "proxy.tx", "packet", "msg:Proxy send packet', type:%d, id:%lld, lenth:%ld", type, comm->reqID, len);
    return packet;
}


#if UNUSED
static void prepProxyRequestStart(HttpQueue *q)
{
    HttpPacket  *packet;
    ProxyComm    *comm;
    uchar       *buf;

    comm = q->queueData;
    packet = httpCreateDataPacket(16);
    buf = (uchar*) packet->content->start;
    *buf++= 0;
    *buf++= PROXY_RESPONDER;
    *buf++ = PROXY_KEEP_CONN;
    /* Reserved bytes */
    buf += 5;
    mprAdjustBufEnd(packet->content, 8);
    httpPutForService(comm->writeq, createProxyPacket(q, PROXY_BEGIN_REQUEST, packet), HTTP_SCHEDULE_QUEUE);
}


//  MOB - NO

static void prepProxyRequestParams(HttpQueue *q)
{
    ProxyComm    *comm;
    HttpStream  *stream;
    HttpPacket  *packet;
    HttpRx      *rx;

    comm = q->queueData;
    stream = q->stream;
    rx = stream->rx;

    packet = httpCreateDataPacket(stream->limits->headerSize);
    packet->data = comm;
    copyProxyParams(packet, rx->params, rx->route->envPrefix);
    copyProxyVars(packet, rx->svars, "");
    copyProxyVars(packet, rx->headers, "HTTP_");

    httpPutForService(comm->writeq, createProxyPacket(q, PROXY_PARAMS, packet), HTTP_SCHEDULE_QUEUE);
    httpPutForService(comm->writeq, createProxyPacket(q, PROXY_PARAMS, 0), HTTP_SCHEDULE_QUEUE);
}
#endif


/************************************************ ProxyComm ***********************************************************/
/*
    Setup the proxy comm. Must be called locked
 */
static ProxyComm *allocProxyComm(ProxyApp *app, HttpStream *stream)
{
    ProxyComm    *comm;

    comm = mprAllocObj(ProxyComm, manageProxyComm);
    comm->stream = stream;
    comm->trace = stream->trace;
    comm->reqID = app->nextID++;
    comm->proxy = app->proxy;
    comm->app = app;

    comm->readq = httpCreateQueue(stream->net, stream, HTTP->proxyConnector, HTTP_QUEUE_RX, 0);
    comm->writeq = httpCreateQueue(stream->net, stream, HTTP->proxyConnector, HTTP_QUEUE_TX, 0);

    comm->readq->max = PROXY_Q_SIZE;
    comm->writeq->max = PROXY_Q_SIZE;

    comm->readq->queueData = comm;
    comm->writeq->queueData = comm;
    comm->readq->pair = comm->writeq;
    comm->writeq->pair = comm->readq;
    return comm;
}


static void manageProxyComm(ProxyComm *comm, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(comm->proxy);
        mprMark(comm->app);
        mprMark(comm->readq);
        mprMark(comm->socket);
        mprMark(comm->stream);
        mprMark(comm->trace);
        mprMark(comm->writeq);
    }
}


static void proxyConnectorIncoming(HttpQueue *q, HttpPacket *packet)
{
    ProxyComm    *comm;

    comm = q->queueData;
    httpPutForService(comm->writeq, packet, HTTP_SCHEDULE_QUEUE);
}


/*
    Parse an incoming response packet from the Proxy app
 */
static void proxyConnectorIncomingService(HttpQueue *q)
{
    ProxyComm       *comm;
    ProxyApp        *app;
    HttpPacket      *packet, *tail;
    MprBuf          *buf;
    ssize           contentLength, len, padLength;
    int             requestID, type, version;

    comm = q->queueData;
    app = comm->app;
    app->lastActive = mprGetTicks();

    while ((packet = httpGetPacket(q)) != 0) {
        buf = packet->content;

        //  MOB - don't need to do all this. Just route the packet and be done

        if (mprGetBufLength(buf) < PROXY_PACKET_SIZE) {
            /* Insufficient data */
            httpPutBackPacket(q, packet);
            break;
        }
        version = mprGetCharFromBuf(buf);
        type = mprGetCharFromBuf(buf);
        requestID = (mprGetCharFromBuf(buf) << 8) | (mprGetCharFromBuf(buf) & 0xFF);
        contentLength = (mprGetCharFromBuf(buf) << 8) | (mprGetCharFromBuf(buf) & 0xFF);
        padLength = mprGetCharFromBuf(buf);
        /* reserved */ (void) mprGetCharFromBuf(buf);
        len = contentLength + padLength;

        if (version != PROXY_VERSION) {
            httpLog(app->trace, "proxy.rx", "error", "msg:'Bad Proxy response version'");
            break;
        }
        if (contentLength < 0 || contentLength > 65535) {
            httpLog(app->trace, "proxy", "error", "msg:'Bad Proxy content length', length:%ld", contentLength);
            break;
        }
        if (padLength < 0 || padLength > 255) {
            httpLog(app->trace, "proxy", "error", "msg:'Bad Proxy pad length', padding:%ld", padLength);
            break;
        }
        if (mprGetBufLength(buf) < len) {
            /* Insufficient data */
            mprAdjustBufStart(buf, -PROXY_PACKET_SIZE);
            httpPutBackPacket(q, packet);
            break;
        }
        packet->type = type;

        httpLog(app->trace, "proxy", "packet", "msg:'Proxy incoming packet', type:'%s' id:%d, length:%ld",
                proxyTypes[type], requestID, contentLength);

        /*
            Split extra data off this packet
         */
        if ((tail = httpSplitPacket(packet, len)) != 0) {
            httpPutBackPacket(q, tail);
        }
        if (padLength) {
            /* Discard padding */
            mprAdjustBufEnd(packet->content, -padLength);
        }
        if (type == PROXY_STDOUT || type == PROXY_END_REQUEST) {
            proxyHandlerResponse(comm, type, packet);

        } else if (type == PROXY_STDERR) {
            /* Log and discard stderr */
            httpLog(app->trace, "proxy", "error", "msg:'Proxy stderr', uri:'%s', error:'%s'",
                comm->stream->rx->uri, mprBufToString(packet->content));

        } else {
            httpLog(app->trace, "proxy", "error", "msg:'Proxy invalid packet', command:'%s', type:%d",
                comm->stream->rx->uri, type);
            app->destroy = 1;
        }
    }
}


/*
    Handle IO events on the network
 */
static void proxyConnectorIO(ProxyComm *comm, MprEvent *event)
{
    Proxy       *proxy;
    HttpPacket  *packet;
    ssize       nbytes;

    proxy = comm->proxy;
    if (comm->eof) {
        /* Network connection to client has been destroyed */
        return;
    }
    if (event->mask & MPR_WRITABLE) {
        httpServiceQueue(comm->writeq);
    }
    if (event->mask & MPR_READABLE) {
        lock(proxy);
        if (comm->socket) {
            packet = httpCreateDataPacket(ME_PACKET_SIZE);
            nbytes = mprReadSocket(comm->socket, mprGetBufEnd(packet->content), ME_PACKET_SIZE);
            comm->eof = mprIsSocketEof(comm->socket);
            if (nbytes > 0) {
                mprAdjustBufEnd(packet->content, nbytes);
                httpJoinPacketForService(comm->readq, packet, 0);
                httpServiceQueue(comm->readq);
            }
        }
        unlock(proxy);
    }
    httpServiceNetQueues(comm->stream->net, 0);
    //httpProcess(comm->stream->inputq)

    if (!comm->eof) {
        enableProxyCommEvents(comm);
    }
}


static void enableProxyCommEvents(ProxyComm *comm)
{
    MprSocket   *sp;
    int         eventMask;

    lock(comm->proxy);
    sp = comm->socket;

    if (sp && !comm->eof && !(sp->flags & MPR_SOCKET_CLOSED)) {
        eventMask = 0;
        if (comm->writeq->count > 0) {
            eventMask |= MPR_WRITABLE;
        }
        if (comm->readq->count < comm->readq->max) {
            eventMask |= MPR_READABLE;
        }
        if (eventMask) {
            if (sp->handler == 0) {
                mprAddSocketHandler(sp, eventMask, comm->stream->dispatcher, proxyConnectorIO, comm, 0);
            } else {
                mprWaitOn(sp->handler, eventMask);
            }
        } else if (sp->handler) {
            mprWaitOn(sp->handler, eventMask);
        }
    }
    unlock(comm->proxy);
}


/*
    Send request and post body data to the app
 */
static void proxyConnectorOutgoingService(HttpQueue *q)
{
    Proxy           *proxy;
    ProxyApp        *app;
    ProxyComm       *comm, *cp;
    ssize           written;
    int             errCode, next;

    comm = q->queueData;
    app = comm->app;
    proxy = app->proxy;
    app->lastActive = mprGetTicks();

    lock(proxy);
    if (comm->eof || comm->socket == 0) {
        return;
    }
    comm->writeBlocked = 0;

    while (q->first || q->ioIndex) {
        if (q->ioIndex == 0 && buildProxyVec(q) <= 0) {
            freeProxyPackets(q, 0);
            break;
        }
        written = mprWriteSocketVector(comm->socket, q->iovec, q->ioIndex);
        if (written < 0) {
            errCode = mprGetError();
            if (errCode == EAGAIN || errCode == EWOULDBLOCK) {
                /*  Socket full, wait for an I/O event */
                comm->writeBlocked = 1;
                break;
            }
            comm->eof = 1;
            app->destroy = 1;
            httpLog(comm->app->trace, "proxy", "error", "msg='Write error', errno:%d", errCode);

            for (ITERATE_ITEMS(app->comms, cp, next)) {
                proxyHandlerResponse(cp, PROXY_COMMS_ERROR, NULL);
            }
            break;

        } else if (written > 0) {
            freeProxyPackets(q, written);
            adjustProxyVec(q, written);

        } else {
            /* Socket full */
            break;
        }
    }
    enableProxyCommEvents(comm);
    unlock(proxy);
}


/*
    Build the IO vector. Return the count of bytes to be written. Return -1 for EOF.
 */
static MprOff buildProxyVec(HttpQueue *q)
{
    HttpPacket  *packet;

    /*
        Examine each packet and accumulate as many packets into the I/O vector as possible. Leave the packets on
        the queue for now, they are removed after the IO is complete for the entire packet.
     */
     for (packet = q->first; packet; packet = packet->next) {
        if (q->ioIndex >= (ME_MAX_IOVEC - 2)) {
            break;
        }
        if (httpGetPacketLength(packet) > 0 || packet->prefix) {
            addProxyPacket(q, packet);
        }
    }
    return q->ioCount;
}


/*
    Add a packet to the io vector. Return the number of bytes added to the vector.
 */
static void addProxyPacket(HttpQueue *q, HttpPacket *packet)
{
    assert(q->count >= 0);
    assert(q->ioIndex < (ME_MAX_IOVEC - 2));

    if (packet->prefix && mprGetBufLength(packet->prefix) > 0) {
        addToProxyVector(q, mprGetBufStart(packet->prefix), mprGetBufLength(packet->prefix));
    }
    if (packet->content && mprGetBufLength(packet->content) > 0) {
        addToProxyVector(q, mprGetBufStart(packet->content), mprGetBufLength(packet->content));
    }
}


/*
    Add one entry to the io vector
 */
static void addToProxyVector(HttpQueue *q, char *ptr, ssize bytes)
{
    assert(bytes > 0);

    q->iovec[q->ioIndex].start = ptr;
    q->iovec[q->ioIndex].len = bytes;
    q->ioCount += bytes;
    q->ioIndex++;
}


static void freeProxyPackets(HttpQueue *q, ssize bytes)
{
    HttpPacket  *packet;
    ssize       len;

    assert(q->count >= 0);
    assert(bytes >= 0);

    while ((packet = q->first) != 0) {
        if (packet->flags & HTTP_PACKET_END) {
            ;
        } else if (bytes > 0) {
            if (packet->prefix) {
                len = mprGetBufLength(packet->prefix);
                len = min(len, bytes);
                mprAdjustBufStart(packet->prefix, len);
                bytes -= len;
                /* Prefixes don't count in the q->count. No need to adjust */
                if (mprGetBufLength(packet->prefix) == 0) {
                    /* Ensure the prefix is not resent if all the content is not sent */
                    packet->prefix = 0;
                }
            }
            if (packet->content) {
                len = mprGetBufLength(packet->content);
                len = min(len, bytes);
                mprAdjustBufStart(packet->content, len);
                bytes -= len;
                q->count -= len;
                assert(q->count >= 0);
            }
        }
        if ((packet->flags & HTTP_PACKET_END) || (httpGetPacketLength(packet) == 0 && !packet->prefix)) {
            /* Done with this packet - consume it */
            httpGetPacket(q);
        } else {
            /* Packet still has data to be written */
            break;
        }
    }
}


/*
    Clear entries from the IO vector that have actually been transmitted. Support partial writes.
 */
static void adjustProxyVec(HttpQueue *q, ssize written)
{
    MprIOVec    *iovec;
    ssize       len;
    int         i, j;

    if (written == q->ioCount) {
        q->ioIndex = 0;
        q->ioCount = 0;
    } else {
        /*
            Partial write of an vector entry. Need to copy down the unwritten vector entries.
         */
        q->ioCount -= written;
        assert(q->ioCount >= 0);
        iovec = q->iovec;
        for (i = 0; i < q->ioIndex; i++) {
            len = iovec[i].len;
            if (written < len) {
                iovec[i].start += written;
                iovec[i].len -= written;
                break;
            } else {
                written -= len;
            }
        }
        /*
            Compact the vector
         */
        for (j = 0; i < q->ioIndex; ) {
            iovec[j++] = iovec[i++];
        }
        q->ioIndex = j;
    }
}


#if UNUSED
/*
    Proxy encoding of strings
 */
static void encodeProxyLen(MprBuf *buf, cchar *s)
{
    ssize   len;

    len = slen(s);
    if (len <= 127) {
        mprPutCharToBuf(buf, (uchar) len);
    } else {
        mprPutCharToBuf(buf, (uchar) (((len >> 24) & 0x7f) | 0x80));
        mprPutCharToBuf(buf, (uchar) ((len >> 16) & 0xff));
        mprPutCharToBuf(buf, (uchar) ((len >> 8) & 0xff));
        mprPutCharToBuf(buf, (uchar) (len & 0xff));
    }
}


/*
    Proxy encoding of names and values. Used to send params.
 */
static void encodeProxyName(HttpPacket *packet, cchar *name, cchar *value)
{
    MprBuf      *buf;

    buf = packet->content;
    encodeProxyLen(buf, name);
    encodeProxyLen(buf, value);
    mprPutStringToBuf(buf, name);
    mprPutStringToBuf(buf, value);
}


static void copyProxyInner(HttpPacket *packet, cchar *key, cchar *value, cchar *prefix)
{
    ProxyComm    *comm;

    comm = packet->data;
    if (prefix) {
        key = sjoin(prefix, key, NULL);
    }
    httpLog(comm->trace, "proxy.tx", "detail", "msg:'Proxy env', key:'%s', value:'%s'", key, value);
    encodeProxyName(packet, key, value);
}


static void copyProxyVars(HttpPacket *packet, MprHash *vars, cchar *prefix)
{
    MprKey  *kp;

    for (ITERATE_KEYS(vars, kp)) {
        if (kp->data) {
            copyProxyInner(packet, kp->key, kp->data, prefix);
        }
    }
}


static void copyProxyParams(HttpPacket *packet, MprJson *params, cchar *prefix)
{
    MprJson     *param;
    int         i;

    for (ITERATE_JSON(params, param, i)) {
        copyProxyInner(packet, param->name, param->value, prefix);
    }
}
#endif


static int proxyConnectDirective(MaState *state, cchar *key, cchar *value)
{
    Proxy   *proxy;
    cchar   *endpoint, *args, *ip;
    char    *option, *ovalue, *tok;
    int     port;

    proxy = getProxy(state->route);

    if (!maTokenize(state, value, "%S ?*", &endpoint, &args)) {
        return MPR_ERR_BAD_SYNTAX;
    }
    proxy->endpoint = endpoint;

    for (option = stok(sclone(args), " \t", &tok); option; option = stok(0, " \t", &tok)) {
        option = ssplit(option, " =\t,", &ovalue);
        ovalue = strim(ovalue, "\"'", MPR_TRIM_BOTH);

        if (smatch(option, "count")) {
            proxy->maxRequests = httpGetNumber(ovalue);
            if (proxy->maxRequests < 1) {
                proxy->maxRequests = 1;
            }

        } else if (smatch(option, "launch")) {
            proxy->launch = sclone(ovalue);

        } else if (smatch(option, "max")) {
            proxy->maxApps = httpGetInt(ovalue);
            if (proxy->maxApps < 1) {
                proxy->maxApps = 1;
            }

        } else if (smatch(option, "min")) {
            proxy->minApps = httpGetInt(ovalue);
            if (proxy->minApps < 1) {
                proxy->minApps = 1;
            }

        } else if (smatch(option, "multiplex")) {
            proxy->multiplex = httpGetInt(ovalue);
            if (proxy->multiplex < 1) {
                proxy->multiplex = 1;
            }

        } else if (smatch(option, "timeout")) {
            proxy->proxyTimeout = httpGetTicks(ovalue);
            if (proxy->proxyTimeout < (30 * TPS)) {
                proxy->proxyTimeout = 30 * TPS;
            }
        } else {
            mprLog("proxy error", 0, "Unknown Proxy option %s", option);
            return MPR_ERR_BAD_SYNTAX;
        }
    }
    /*
        Pre-test the endpoint
     */
    if (mprParseSocketAddress(proxy->endpoint, &ip, &port, NULL, 9128) < 0) {
        mprLog("proxy error", 0, "Cannot bind Proxy proxy address: %s", proxy->endpoint);
        return MPR_ERR_BAD_SYNTAX;
    }
    return 0;
}


/*
    Create listening socket that is passed to the Proxy app (and then closed after forking)
 */
static MprSocket *createListener(ProxyApp *app, HttpStream *stream)
{
    Proxy       *proxy;
    MprSocket   *listen;

    proxy = app->proxy;

    if (mprParseSocketAddress(proxy->endpoint, &proxy->ip, &proxy->port, NULL, 0) < 0) {
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot parse listening endpoint");
        return NULL;
    }
    listen = mprCreateSocket();
    if (mprListenOnSocket(listen, proxy->ip, proxy->port, MPR_SOCKET_BLOCK | MPR_SOCKET_NODELAY) == SOCKET_ERROR) {
        if (mprGetError() == EADDRINUSE) {
            httpLog(app->trace, "proxy.rx", "error",
                "msg:'Cannot open listening socket for Proxy, already bound', address: '%s:%d'",
                proxy->ip ? proxy->ip : "*", proxy->port);
        } else {
            httpLog(app->trace, "proxy.rx", "error", "msg:'Cannot open listening socket for Proxy', address: '%s:%d'",
                proxy->ip ? proxy->ip : "*", proxy->port);
        }
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot create listening endpoint");
        return NULL;
    }
    if (proxy->port == 0) {
        proxy->port = getListenPort(listen);
    }
    httpLog(app->trace, "proxy.rx", "context", "msg:'Listening for Proxy', endpoint: '%s:%d'",
        proxy->ip ? proxy->ip : "*", proxy->port);
    return listen;
}


static int getListenPort(MprSocket *socket)
{
    struct sockaddr_in sin;
    socklen_t len;

    len = sizeof(sin);
    if (getsockname(socket->fd, (struct sockaddr *)&sin, &len) < 0) {
        return MPR_ERR_CANT_FIND;
    }
    return ntohs(sin.sin_port);
}


#endif /* ME_COM_PROXY */

/*
    Copyright (c) Embedthis Software. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
