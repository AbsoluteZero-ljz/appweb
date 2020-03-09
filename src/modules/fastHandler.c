/*
    fastHandler.c -- Fast CGI handler

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.

    https://github.com/fast-cgi/spec/blob/master/spec.md

    <Route /fast>
        LoadModule fastHandler libmod_fast
        Action application/x-php /usr/local/bin/php-cgi
        AddHandler fastHandler php
        FastConnect 127.0.0.1:9991 launch min=1 max=2 count=500 timeout=5mins multiplex=1
    </Route>
 */

/*********************************** Includes *********************************/

#define ME_COM_FAST 1

#include    "appweb.h"

#if ME_COM_FAST && ME_UNIX_LIKE
/************************************ Locals ***********************************/

#define FAST_VERSION            1
#define FAST_DEBUG              1           //  For debugging (keeps filedes open in FastCGI for debug output)

/*
    FastCGI spec packet types
 */
#define FAST_REAP               0           //  Proxy has been reaped (not part of spec)
#define FAST_BEGIN_REQUEST      1           //  Start new request - sent to FastCGI
#define FAST_ABORT_REQUEST      2           //  Abort request - sent to FastCGI
#define FAST_END_REQUEST        3           //  End request - received from FastCGI
#define FAST_PARAMS             4           //  Send params to FastCGI
#define FAST_STDIN              5           //  Post body data
#define FAST_STDOUT             6           //  Response body
#define FAST_STDERR             7           //  FastCGI app errors
#define FAST_DATA               8           //  Additional data to application (unused)
#define FAST_GET_VALUES         9           //  Query FastCGI app (unused)
#define FAST_GET_VALUES_RESULT  10          //  Query result
#define FAST_UNKNOWN_TYPE       11          //  Unknown management request
#define FAST_MAX                11          //  Max type

static cchar *fastTypes[FAST_MAX + 1] = {
    "invalid", "begin", "abort", "end", "params", "stdin", "stdout", "stderr",
    "data", "get-values", "get-values-result", "unknown",
};

/*
    FastCGI app types
 */
#define FAST_RESPONDER          1           //  Supported web request responder
#define FAST_AUTHORIZER         2           //  Not supported
#define FAST_FILTER             3           //  Not supported

/*
    Default constants. WARNING: this code does not yet support multiple requests per proxy
 */
#define FAST_MAX_PROXIES        1           //  Max of one proxy
#define FAST_MIN_PROXIES        1           //  Min of one proxy (keep running after started)
#define FAST_MAX_REQUESTS       500         //  Max number of requests per proxy instance
#define FAST_MAX_MULTIPLEX      1           //  Max number of concurrent requests per proxy instance

#define FAST_PACKET_SIZE        8           //  Size of minimal FastCGI packet
#define FAST_KEEP_CONN          1           //  Flag to app to keep connection open

#define FAST_Q_SIZE             ((FAST_PACKET_SIZE + 65535 + 8) * 2)

#define FAST_REQUEST_COMPLETE   0           //  End Request response status for request complete
#define FAST_CANT_MPX_CONN      1           //  Request rejected -- FastCGI app cannot multiplex requests
#define FAST_OVERLOADED         2           //  Request rejected -- app server is overloaded
#define FAST_UNKNOWN_ROLE       3           //  Request rejected -- unknown role

#define FAST_WAIT_TIMEOUT       (30 * TPS)  //  Time to wait for a proxy
#define FAST_CONNECT_TIMEOUT    (10 * TPS)  //  Time to wait for FastCGI to respond to a connect
#define FAST_PROXY_TIMEOUT      (300 * TPS) //  Time to wait destroy idle proxies

/*
    Top level FastCGI structure per route
 */
typedef struct Fast {
    cchar           *endpoint;              //  Proxy listening endpoint
    int             launch;                 //  Launch proxy
    int             multiplex;              //  Maximum number of requests to send to each FastCGI proxy
    int             minProxies;             //  Minumum number of proxies to maintain
    int             maxProxies;             //  Maximum number of proxies to spawn
    int             maxRequests;            //  Maximum number of requests per proxy before respawning
    MprTicks        proxyTimeout;           //  Timeout for an idle proxy to be maintained
    MprList         *proxies;               //  List of active proxies
    MprList         *idleProxies;           //  Idle proxies
    MprMutex        *mutex;                 //  Multithread sync
    MprCond         *cond;                  //  Condition to wait for available proxy
    MprEvent        *timer;                 //  Timer to check for idle proxies
    cchar           *ip;                    //  Listening IP address
    int             port;                   //  Listening port
} Fast;

/*
    Per FastCGI app instance
 */
typedef struct FastProxy {
    Fast            *fast;                  // Parent pointer
    MprList         *streams;               // List of concurrent stream requests
    HttpTrace       *trace;                 // Default tracing configuration
    MprTicks        lastActive;             // When last active
    int             nextID;                 // Next request ID
    struct FastConnector *connector;        // Connector runs on a different dispatcher
} FastProxy;

typedef struct FastConnector {
    MprSocket       *socket;                // I/O socket
    MprDispatcher   *dispatcher;            // Dispatcher for connector. Different to handler if multiplexing
    HttpQueue       *writeq;                // Queue to write to the FastCGI app
    HttpQueue       *readq;                 // Queue to hold read data from the FastCGI app
    FastProxy       *proxy;                 // Owning proxy
    HttpTrace       *trace;                 // Default tracing configuration
    MprSignal       *signal;                // Mpr signal handler for child death
    int             pid;                    // Process ID of the FastCGI proxy app
    bool            destroy;                // Must destroy proxy
    bool            eof;                    // Socket is closed
    bool            writeBlocked;           // Socket is full of write data
} FastConnector;

/*********************************** Forwards *********************************/

static void addFastPacket(HttpQueue *q, HttpPacket *packet);
static void addToFastVector(HttpQueue *q, char *ptr, ssize bytes);
static void adjustNetVec(HttpQueue *q, ssize written);
static Fast *allocFast();
static FastConnector *allocFastConnector(FastProxy *proxy, MprDispatcher *dispatcher, HttpStream *stream);
static FastProxy *allocFastProxy(Fast *fast, HttpStream *stream);
static cchar *buildProxyArgs(HttpStream *stream, int *argcp, cchar ***argvp);
static MprOff buildFastVec(HttpQueue *q);
static void checkIdleProxies(Fast *fast);
static void closeFast(HttpQueue *q);
static int connectFastProxy(FastProxy *proxy);
static void copyFastInner(HttpPacket *packet, cchar *key, cchar *value, cchar *prefix);
static void copyFastParams(HttpPacket *packet, MprJson *params, cchar *prefix);
static void copyFastVars(HttpPacket *packet, MprHash *vars, cchar *prefix);
static HttpPacket *createFastPacket(HttpQueue *q, int type, HttpPacket *packet);
static MprSocket *createListener(FastConnector *connector);
static void destroyFastProxy(FastProxy *proxy);
static void enableFastConnectorEvents(FastConnector *connector);
static void fastConnectorData(HttpPacket *packet);
static void fastConnectorIO(FastConnector *connector, MprEvent *event);
static void fastConnectorIncoming(HttpQueue *q, HttpPacket *packet);
static void fastConnectorIncomingService(HttpQueue *q);
static void fastConnectorOutgoing(HttpQueue *q, HttpPacket *packet);
static void fastConnectorOutgoingService(HttpQueue *q);
static void fastHandlerResponse(HttpPacket *packet);
static void fastIncoming(HttpQueue *q, HttpPacket *packet);
static int fastConnectDirective(MaState *state, cchar *key, cchar *value);
static void fastOutgoing(HttpQueue *q);
static void freeFastPackets(HttpQueue *q, ssize bytes);
static Fast *getFast(HttpRoute *route);
static char *getFastToken(MprBuf *buf, cchar *delim);
static FastProxy *getFastProxy(Fast *fast, HttpStream *stream);
static int getListenPort(MprSocket *socket);
static HttpStream *getStream(FastProxy *proxy, int requestID);
static void manageFast(Fast *fast, int flags);
static void manageFastProxy(FastProxy *proxy, int flags);
static void manageFastConnector(FastConnector *fastConnector, int flags);
static int openFast(HttpQueue *q);
static bool parseFastHeaders(HttpPacket *packet);
static bool parseFastResponseLine(HttpPacket *packet);
static void prepFastRequestStart(HttpQueue *q);
static void prepFastRequestParams(HttpQueue *q);
static void putToConnectorWriteq(FastConnector *connector, HttpPacket *packet);
static void reapProxyProcess(FastConnector *connector, MprSignal *sp);
static void releaseFastProxy(Fast *fast, FastProxy *proxy);
static FastProxy *startFastProxy(Fast *fast, HttpStream *stream);

/************************************* Code ***********************************/
/*
    Loadable module initialization
 */
PUBLIC int httpFastHandlerInit(Http *http, MprModule *module)
{
    HttpStage   *handler, *connector;

    /*
        Add configuration file directives
     */
    maAddDirective("FastConnect", fastConnectDirective);

    if ((handler = httpCreateHandler("fastHandler", module)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->fastHandler = handler;
    handler->close = closeFast;
    handler->open = openFast;
    handler->incoming = fastIncoming;
    handler->outgoingService = fastOutgoing;

    /*
        The Fast handler is the head of the pipeline. The Fast connector is a clone of the net
        connector and is after the Http protocol and tailFilter.
    */
    if ((connector = httpCreateStreamector("fastConnector", NULL)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->fastConnector = connector;
    connector->incoming = fastConnectorIncoming;
    connector->incomingService = fastConnectorIncomingService;
    connector->outgoing = fastConnectorOutgoing;
    connector->outgoingService = fastConnectorOutgoingService;

    return 0;
}

/*
    Open for a new request
 */
static int openFast(HttpQueue *q)
{
    Http        *http;
    HttpNet     *net;
    HttpStream  *stream;
    Fast        *fast;
    FastProxy   *proxy;

    net = q->net;
    stream = q->stream;
    http = stream->http;

    httpTrimExtraPath(stream);
    httpMapFile(stream);
    httpCreateCGIParams(stream);

    fast = getFast(stream->rx->route);

    if ((proxy = getFastProxy(fast, stream)) == 0) {
        httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot allocate FastCGI proxy for route %s", stream->rx->route->pattern);
        return MPR_ERR_CANT_OPEN;
    }
    q->queueData = q->pair->queueData = proxy;

    if (connectFastProxy(proxy)) {
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot connect to fast proxy: %d", errno);
        return MPR_ERR_CANT_CONNECT;
    }

    prepFastRequestStart(q);
    prepFastRequestParams(q);
    enableFastConnectorEvents(proxy->connector);
    return 0;
}


/*
    Request is closed, so release proxy
 */
static void closeFast(HttpQueue *q)
{
    HttpRoute   *route;
    FastProxy   *proxy;

    route = q->stream->rx->route;
    proxy = q->queueData;
    if (proxy) {
        mprRemoveItem(proxy->streams, q->stream);
        releaseFastProxy(proxy->fast, proxy);
    }
}


static Fast *allocFast()
{
    Fast    *fast;

    fast = mprAllocObj(Fast, manageFast);
    fast->proxies = mprCreateList(0, 0);
    fast->idleProxies = mprCreateList(0, 0);
    fast->mutex = mprCreateLock();
    fast->cond = mprCreateCond();
    fast->multiplex = FAST_MAX_MULTIPLEX;
    fast->maxRequests = FAST_MAX_REQUESTS;
    fast->minProxies = FAST_MIN_PROXIES;
    fast->maxProxies = FAST_MAX_PROXIES;
    fast->ip = sclone("127.0.0.1");
    fast->port = 0;
    fast->proxyTimeout = FAST_PROXY_TIMEOUT;
    fast->timer = mprCreateTimerEvent(NULL, "fast-watchdog", fast->proxyTimeout, checkIdleProxies, fast, MPR_EVENT_QUICK);
    return fast;
}


static void manageFast(Fast *fast, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(fast->cond);
        mprMark(fast->endpoint);
        mprMark(fast->idleProxies);
        mprMark(fast->ip);
        mprMark(fast->mutex);
        mprMark(fast->proxies);
        mprMark(fast->timer);
    }
}


static void checkIdleProxies(Fast *fast)
{
    FastProxy   *proxy;
    MprTicks    now;
    int         next;

    lock(fast);
    now = mprGetTicks();
    for (ITERATE_ITEMS(fast->idleProxies, proxy, next)) {
        if ((now - proxy->lastActive) > fast->proxyTimeout) {
            destroyFastProxy(proxy);
            break;
        }
    }
    unlock(fast);
}


/*
    Get the fast structure for a route and save in "eroute"
 */
static Fast *getFast(HttpRoute *route)
{
    Fast        *fast;

    if ((fast = route->eroute) == 0) {
        mprGlobalLock();
        if ((fast = route->eroute) == 0) {
            fast = route->eroute = allocFast();
        }
        mprGlobalUnlock();
    }
    return fast;
}


/*
    Accept incoming body data from the client destined for the CGI gateway. This is typically POST or PUT data.
    Note: For POST "form" requests, this will be called before the command is actually started.
 */
static void fastIncoming(HttpQueue *q, HttpPacket *packet)
{
    HttpStream  *stream;
    FastProxy   *proxy;

    assert(q);
    assert(packet);
    stream = q->stream;

    if ((proxy = q->queueData) == 0) {
        return;
    }
    if (httpGetPacketLength(packet) == 0) {
        /* End of input */
        httpFinalizeInput(stream);
        if (stream->rx->remainingContent > 0) {
            httpError(stream, HTTP_CODE_BAD_REQUEST, "Client supplied insufficient body data");
            packet = createFastPacket(q, FAST_ABORT_REQUEST, httpCreateDataPacket(0));

        } else {
            createFastPacket(q, FAST_STDIN, packet);
        }
    } else {
        createFastPacket(q, FAST_STDIN, packet);
    }
    putToConnectorWriteq(proxy->connector, packet);
}


static void fastOutgoing(HttpQueue *q)
{
    /*
        This will copy outgoing packets downstream toward the network stream and on to the browser.
     */
    httpDefaultOutgoingServiceStage(q);

#if TODO
    FastProxy       *proxy;
    FastConnector   *connector;

    if ((proxy = q->queueData) == 0) {
        return;
    }
    connector = proxy->connector;
    if (q->count < q->low) {
        httpResumeQueue(connector->writeq);

    } else if (q->count > q->max && connector->writeBlocked) {
        httpSuspendQueue(q->stream->writeq);
    }
#endif
}


/*
    Handle a FastCGI response. Called from the connector via mprCreateEvent().
 */
static void fastHandlerResponse(HttpPacket *packet)
{
    FastProxy   *proxy;
    HttpStream  *stream;
    HttpRx      *rx;
    MprBuf      *buf;
    int         status, protoStatus, type;

    proxy = packet->data;
    stream = packet->stream;
    type = packet->type;
    rx = stream->rx;
    buf = packet->content;

    if (stream->state <= HTTP_STATE_BEGIN || stream->rx->route == NULL) {
        /* Request already complete */
        return;
    }

    if (type == FAST_END_REQUEST) {
        if (httpGetPacketLength(packet) < 8) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "FastCGI bad request end packet");
            return;
        }
        status = mprGetCharFromBuf(buf) << 24 || mprGetCharFromBuf(buf) << 16 ||
                 mprGetCharFromBuf(buf) << 8 || mprGetCharFromBuf(buf);
        protoStatus = mprGetCharFromBuf(buf);
        mprAdjustBufStart(buf, 3);

        if (protoStatus == FAST_REQUEST_COMPLETE) {
            httpLog(proxy->trace, "fast.rx", "context", "msg:'Request complete', id:%d, status:%d", stream->reqID, status);

        } else if (protoStatus == FAST_CANT_MPX_CONN) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "FastCGI cannot multiplex requests %s", stream->rx->uri);
            return;

        } else if (protoStatus == FAST_OVERLOADED) {
            httpError(stream, HTTP_CODE_SERVICE_UNAVAILABLE, "FastCGI overloaded %s", stream->rx->uri);
            return;

        } else if (protoStatus == FAST_UNKNOWN_ROLE) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "FastCGI unknown role %s", stream->rx->uri);
            return;
        }
        httpLog(proxy->trace, "fast.rx.eof", "detail", "msg:'FastCGI end request', id:%d", stream->reqID);
        httpFinalizeOutput(stream);

    } else if (type == FAST_STDOUT) {
        if (!rx->seenFastHeaders) {
            if (!parseFastHeaders(packet)) {
                return;
            }
            rx->seenFastHeaders = 1;
        }
    }
    if (type == FAST_REAP) {
        /* Nothing to do -- just call httpProcess below */
        ;

    } else {
        httpLogPacket(proxy->trace, "fast.rx.data", "packet", 0, packet, "id:%d, len:%ld", stream->reqID,
            httpGetPacketLength(packet));
        httpPutPacketToNext(stream->writeq, packet);
    }
    httpProcess(stream->inputq);
}


/*
    Parse the FastCGI app output headers. Sample FastCGI program output:
        Content-type: text/html
        <html.....
 */
static bool parseFastHeaders(HttpPacket *packet)
{
    FastProxy   *proxy;
    HttpStream  *stream;
    MprBuf      *buf;
    char        *endHeaders, *headers, *key, *value;
    ssize       blen, len;

    stream = packet->stream;
    proxy = packet->data;
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
                httpLog(proxy->trace, "fast.rx", "detail", "msg:'FastCGI incomplete headers', id:%d", stream->reqID);
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
        Want to be tolerant of FastCGI programs that omit the status line.
     */
    if (strncmp((char*) buf->start, "HTTP/1.", 7) == 0) {
        if (!parseFastResponseLine(packet)) {
            /* httpError already called */
            return 0;
        }
    }
    if (endHeaders && strchr(mprGetBufStart(buf), ':')) {
        while (mprGetBufLength(buf) > 0 && buf->start[0] && (buf->start[0] != '\r' && buf->start[0] != '\n')) {
            if ((key = getFastToken(buf, ":")) == 0) {
                key = "Bad Header";
            }
            value = getFastToken(buf, "\n");
            while (isspace((uchar) *value)) {
                value++;
            }
            len = (int) strlen(value);
            while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == '\n')) {
                value[len - 1] = '\0';
                len--;
            }
            httpLog(stream->trace, "fast.rx", "detail", "key:'%s', value: '%s'", key, value);
            if (scaselesscmp(key, "location") == 0) {
                httpRedirect(stream, HTTP_CODE_MOVED_TEMPORARILY, value);

            } else if (scaselesscmp(key, "status") == 0) {
                httpSetStatus(stream, atoi(value));

            } else if (scaselesscmp(key, "content-type") == 0) {
                httpSetHeaderString(stream, "Content-Type", value);

            } else if (scaselesscmp(key, "content-length") == 0) {
                httpSetContentLength(stream, (MprOff) stoi(value));
                httpSetChunkSize(stream, 0);

            } else {
                /*
                    Now pass all other headers back to the client
                 */
                key = ssplit(key, ":\r\n\t ", NULL);
                httpSetHeaderString(stream, key, value);
            }
        }
        buf->start = endHeaders;
    }
    return 1;
}


/*
    Parse the CGI output first line
 */
static bool parseFastResponseLine(HttpPacket *packet)
{
    MprBuf      *buf;
    char        *protocol, *status, *msg;

    buf = packet->content;
    protocol = getFastToken(buf, " ");
    if (protocol == 0 || protocol[0] == '\0') {
        httpError(packet->stream, HTTP_CODE_BAD_GATEWAY, "Bad CGI HTTP protocol response");
        return 0;
    }
    if (strncmp(protocol, "HTTP/1.", 7) != 0) {
        httpError(packet->stream, HTTP_CODE_BAD_GATEWAY, "Unsupported CGI protocol");
        return 0;
    }
    status = getFastToken(buf, " ");
    if (status == 0 || *status == '\0') {
        httpError(packet->stream, HTTP_CODE_BAD_GATEWAY, "Bad CGI header response");
        return 0;
    }
    msg = getFastToken(buf, "\n");
    mprDebug("http cgi", 4, "CGI response status: %s %s %s", protocol, status, msg);
    return 1;
}


/*
    Get the next input token. The content buffer is advanced to the next token. This routine always returns a
    non-zero token. The empty string means the delimiter was not found.
 */
static char *getFastToken(MprBuf *buf, cchar *delim)
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


/************************************************ FastProxy ***************************************************************/

static FastProxy *allocFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;

    proxy = mprAllocObj(FastProxy, manageFastProxy);
    proxy->fast = fast;
    proxy->trace = stream->net->trace;
    /*
        The requestID must start at 1
     */
    proxy->nextID = 1;
    proxy->streams = mprCreateList(0, 0);
    proxy->connector = allocFastConnector(proxy, fast->multiplex > 1 ? NULL : stream->net->dispatcher, stream);
    if (proxy->connector == NULL) {
        return NULL;
    }
    return proxy;
}


static void manageFastProxy(FastProxy *proxy, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(proxy->fast);
        mprMark(proxy->streams);
        mprMark(proxy->connector);
        mprMark(proxy->trace);
    }
}


static FastProxy *getFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;
    MprTicks    timeout;
    int         next;

    lock(fast);
    proxy = NULL;
    timeout = mprGetTicks() +  FAST_WAIT_TIMEOUT;

    while (!proxy && mprGetTicks() < timeout) {
        if (mprGetListLength(fast->idleProxies) > 0) {
            proxy = mprGetFirstItem(fast->idleProxies);
            mprRemoveItemAtPos(fast->idleProxies, 0);
            mprAddItem(fast->proxies, proxy);
            break;

        } else if (mprGetListLength(fast->idleProxies) < fast->maxProxies) {
            if ((proxy = startFastProxy(fast, stream)) != 0) {
                mprAddItem(fast->proxies, proxy);
            }
            break;

        } else {
            for (ITERATE_ITEMS(fast->proxies, proxy, next)) {
                if (mprGetListLength(proxy->streams) < fast->multiplex) {
                    break;
                }
            }
            if (proxy) {
                break;
            }
            unlock(fast);
            if (mprWaitForCond(fast->cond, TPS) < 0) {
                return NULL;
            }
            lock(fast);
        }
    }
    if (proxy) {
        mprAddItem(proxy->streams, stream);
        stream->reqID = proxy->nextID++;
    }
    proxy->lastActive = mprGetTicks();
    unlock(fast);
    return proxy;
}


static void releaseFastProxy(Fast *fast, FastProxy *proxy)
{
    FastConnector   *connector;
    cchar           *msg;
    bool            destroyProxy;

    lock(fast);
    connector = proxy->connector;

    if (connector->socket) {
        mprCloseSocket(connector->socket, 1);
    }
    if (mprRemoveItem(fast->proxies, proxy) < 0) {
        httpLog(proxy->trace, "fast", "error", "msg:'Cannot find proxy in list'");
    }
    destroyProxy = 0;
    if (connector->destroy || proxy->nextID >= fast->maxRequests ||
            (mprGetListLength(fast->proxies) + mprGetListLength(fast->idleProxies) >= fast->minProxies)) {
        destroyProxy = 1;
    }
    if (destroyProxy) {
        msg = "Destroy FastCGI proxy";
        destroyFastProxy(proxy);

    } else {
        msg = "Release FastCGI proxy";
        proxy->lastActive = mprGetTicks();
        mprAddItem(fast->idleProxies, proxy);
        assert(!connector->eof);
        assert(!connector->destroy);
    }
    httpLog(proxy->trace, "fast", "context",
        "msg:'%s', pid:%d, idle:%d, active:%d, id:%d, maxRequests:%d, destroy:%d, nextId:%d",
        msg, connector->pid, mprGetListLength(fast->idleProxies), mprGetListLength(fast->proxies),
        proxy->nextID, fast->maxRequests, connector->destroy, proxy->nextID);
    mprSignalCond(fast->cond);
    unlock(fast);
}


static FastProxy *startFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy       *proxy;
    FastConnector   *connector;
    HttpRoute       *route;
    MprSocket       *listen;
    cchar           **argv, *command;
    int             argc;

    route = stream->rx->route;

    proxy = allocFastProxy(fast, stream);
    assert(proxy->connector);
    connector = proxy->connector;

    if (fast->launch) {
        argc = 1;                                   /* argv[0] == programName */
        if ((command = buildProxyArgs(stream, &argc, &argv)) == 0) {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot find Fast proxy command");
            return NULL;
        }
#if UNUSED
        fast->command = mprGetMimeProgram(route->mimeTypes, stream->tx->ext);
        if ((argc = mprMakeArgv(mprGetPathBase(fast->command), &argv, 0)) < 0 || argv == 0) {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot make Fast proxy command: %s", fast->command);
            return NULL;
        }
#endif
        httpLog(stream->trace, "fast", "context", "msg:'Start FastCGI proxy', command:'%s'", command);

        if ((listen = createListener(connector)) < 0) {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot create proxy listening endpoint for %s", command);
            return NULL;
        }
        if (!connector->signal) {
            connector->signal = mprAddSignalHandler(SIGCHLD, reapProxyProcess, connector, connector->dispatcher, MPR_SIGNAL_BEFORE);
        }
        if ((connector->pid = fork()) < 0) {
            fprintf(stderr, "Fork failed for FastCGI");
            return NULL;

        } else if (connector->pid == 0) {
            /* Child */
            dup2(listen->fd, 0);
    #if FAST_DEBUG
            int     i;
            for (i = 3; i < 128; i++) {
                close(i);
            }
    #else
            int     i;
            for (i = 1; i < 128; i++) {
                close(i);
            }
    #endif
            // TODO envp[0] = sfmt("FCGI_WEB_SERVER_ADDRS=%s", route->host->defaultEndpoint->ip);

            if (execve(command, (char**) argv, NULL /* (char**) &env->items[0] */) < 0) {
                printf("Cannot exec fast proxy: %s\n", command);
            }
            return NULL;
        } else {
            httpLog(proxy->trace, "fast", "context", "msg:'FastCGI started proxy', command:'%s', pid:%d",
                command, connector->pid);
            mprCloseSocket(listen, 0);
        }
    }
    return proxy;
}



/*
    Build the command arguments. NOTE: argv is untrusted input.
 */
static cchar *buildProxyArgs(HttpStream *stream, int *argcp, cchar ***argvp)
{
    HttpRx      *rx;
    HttpTx      *tx;
    cchar       *actionProgram, *cp, *fileName, *query;
    char        **argv, *tok;
    ssize       len;
    int         argc, argind, i;

    rx = stream->rx;
    tx = stream->tx;
    fileName = tx->filename;

    actionProgram = 0;
    argind = 0;
    argc = *argcp;

    if (tx->ext) {
        actionProgram = mprGetMimeProgram(rx->route->mimeTypes, tx->ext);
        if (actionProgram != 0) {
            argc++;
        }
        /* This is an Apache compatible hack for PHP 5.3 */
        mprAddKey(rx->headers, "REDIRECT_STATUS", itos(HTTP_CODE_MOVED_TEMPORARILY));
    }
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

    if (actionProgram) {
        argv[argind++] = sclone(actionProgram);
    }
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

    mprDebug("http fast", 6, "Fast: command:");
    for (i = 0; i < argind; i++) {
        mprDebug("http fast", 6, "   argv[%d] = %s", i, argv[i]);
    }
    return argv[0];
}


/*
    Proxy process has died.
    WARNING: this may be called before all the data has been read from the socket, so we must not set eof = 1 here.
    WARNING: this runs on the connectors dispatcher.
 */
static void reapProxyProcess(FastConnector *connector, MprSignal *sp)
{
    FastProxy   *proxy;
    HttpPacket  *packet;
    HttpStream  *stream;
    int         next, status, retry;

    proxy = connector->proxy;

    lock(proxy->fast);
    if (connector->pid == 0) {
        unlock(proxy->fast);
        return;
    }
    retry = 20;
    while (waitpid(connector->pid, &status, WNOHANG) != connector->pid && retry-- > 0) {
        if (connector->pid) {
            kill(connector->pid, SIGTERM);
        }
        mprSleep(100);
    }
    if (WEXITSTATUS(status) != 0) {
        httpLog(connector->trace, "fast", "error", "msg:'FastCGI exited with status', status:%d", WEXITSTATUS(status));
    } else {
        httpLog(connector->trace, "fast", "context", "msg:'FastCGI exited with status', status:%d", WEXITSTATUS(status));
    }
    if (connector->signal) {
        mprRemoveSignalHandler(connector->signal);
        connector->signal = 0;
    }
    if (mprLookupItem(proxy->fast->idleProxies, proxy) >= 0) {
        mprRemoveItem(proxy->fast->idleProxies, proxy);
    }
    httpLog(connector->trace, "fast", "info", "Zero pid %d", connector->pid);
    connector->destroy = 1;
    connector->pid = 0;

    for (ITERATE_ITEMS(connector->proxy->streams, stream, next)) {
        if (HTTP_STATE_BEGIN < stream->state && stream->state <= HTTP_STATE_RUNNING) {
            if (!stream->tx->finalized) {
                httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "FastCGI app closed prematurely");
            }
            packet = httpCreateDataPacket(0);
            packet->data = connector->proxy;
            packet->stream = stream;
            packet->type = FAST_REAP;
            mprCreateEvent(stream->dispatcher, "fast.reap", 0, fastHandlerResponse, packet, 0);
        }
    }
    unlock(proxy->fast);
}


static void destroyFastProxy(FastProxy *proxy)
{
    FastConnector   *connector;

    connector = proxy->connector;
    if (connector->pid) {
        lock(proxy->fast);
        httpLog(proxy->trace, "fast", "context", "msg: 'Kill FastCGI process', pid:%d", connector->pid);
        mprSleep(10);
        if (connector->pid) {
            kill(connector->pid, SIGTERM);
        }
        reapProxyProcess(connector, NULL);
        unlock(proxy->fast);
    }
}


static int connectFastProxy(FastProxy *proxy)
{
    Fast            *fast;
    FastConnector   *connector;
    MprTicks        timeout;
    int             tries, connected;

    fast = proxy->fast;
    connector = proxy->connector;

    timeout = mprGetTicks() +  FAST_CONNECT_TIMEOUT;
    tries = 1;
    do {
        httpLog(proxy->trace, "fast.rx", "request", "FastCGI try to connect to %s:%d pid %d",
            fast->ip, fast->port, connector->pid);

        connector->socket = mprCreateSocket();
        if ((connected = mprConnectSocket(connector->socket, fast->ip, fast->port, 0)) == 0) {
            mprLog("fast info", 0, "FastCGI connected to %s:%d", fast->ip, fast->port);
            return 0;
        }
        mprSleep(50 * tries++);
    } while (mprGetTicks() < timeout);

    return MPR_ERR_CANT_CONNECT;
}


/*
    Add the FastCGI spec packet header to the packet->prefix
 */
static HttpPacket *createFastPacket(HttpQueue *q, int type, HttpPacket *packet)
{
    FastProxy   *proxy;
    uchar       *buf;
    ssize       len, pad;

    proxy = q->queueData;
    if (!packet) {
        packet = httpCreateDataPacket(0);
    }
    len = httpGetPacketLength(packet);

    packet->prefix = mprCreateBuf(16, 16);

    buf = (uchar*) packet->prefix->start;
    *buf++ = FAST_VERSION;
    *buf++ = type;

    *buf++ = (uchar) (q->stream->reqID >> 8);
    *buf++ = (uchar) (q->stream->reqID & 0xFF);

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

    httpLog(proxy->trace, "fast.tx", "context", "msg:FastCGI send packet', type:%d, id:%d, lenth:%ld",
        type, q->stream->reqID, len);
    return packet;
}


static void prepFastRequestStart(HttpQueue *q)
{
    HttpPacket  *packet;
    FastProxy   *proxy;
    uchar       *buf;

    proxy = q->queueData;
    packet = httpCreateDataPacket(16);
    buf = (uchar*) packet->content->start;
    *buf++= 0;
    *buf++= FAST_RESPONDER;
    *buf++ = FAST_KEEP_CONN;
    /* Reserved bytes */
    buf += 5;
    mprAdjustBufEnd(packet->content, 8);
    putToConnectorWriteq(proxy->connector, createFastPacket(q, FAST_BEGIN_REQUEST, packet));
}


static void prepFastRequestParams(HttpQueue *q)
{
    FastProxy   *proxy;
    HttpStream  *stream;
    HttpPacket  *packet;
    HttpRx      *rx;

    proxy = q->queueData;
    stream = q->stream;
    rx = stream->rx;

    packet = httpCreateDataPacket(stream->limits->headerSize);
    packet->data = proxy;
    copyFastParams(packet, rx->params, rx->route->envPrefix);
    copyFastVars(packet, rx->svars, "");
    copyFastVars(packet, rx->headers, "HTTP_");

    putToConnectorWriteq(proxy->connector, createFastPacket(q, FAST_PARAMS, packet));
    putToConnectorWriteq(proxy->connector, createFastPacket(q, FAST_PARAMS, 0));
}


/************************************************ FastConnector ***********************************************************/
/*
    Setup the proxy connector. This may run on a different thread.
 */
static FastConnector *allocFastConnector(FastProxy *proxy, MprDispatcher *dispatcher, HttpStream *stream)
{
    Fast            *fast;
    FastConnector   *connector;

    fast = proxy->fast;

    connector = mprAllocObj(FastConnector, manageFastConnector);
    connector->proxy = proxy;
    proxy->connector = connector;
    connector->trace = proxy->trace;

    connector->readq = httpCreateQueue(stream->net, stream, HTTP->fastConnector, HTTP_QUEUE_RX, 0);
    connector->writeq = httpCreateQueue(stream->net, stream, HTTP->fastConnector, HTTP_QUEUE_TX, 0);

    connector->readq->max = FAST_Q_SIZE;
    connector->writeq->max = FAST_Q_SIZE;

    connector->readq->queueData = connector;
    connector->writeq->queueData = connector;
    connector->readq->pair = connector->writeq;
    connector->writeq->pair = connector->readq;

    /*
        Create listening socket for the fastCGI app to accept -- passed to app via fork
     */
    connector->dispatcher = mprCreateDispatcher("fast-connector", 0);
    return connector;
}


static void manageFastConnector(FastConnector *connector, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(connector->dispatcher);
        mprMark(connector->socket);
        mprMark(connector->writeq);
        mprMark(connector->readq);
        mprMark(connector->proxy);
        mprMark(connector->signal);
        mprMark(connector->trace);
    }
}


static void fastConnectorIncoming(HttpQueue *q, HttpPacket *packet)
{
    FastProxy   *proxy;

    proxy = q->queueData;
    putToConnectorWriteq(proxy->connector, packet);
}


/*
    Safe routine to put a packet to the FastCGI connector. Can be called from the stream's dispatcher.
    This must be thread safe if multiplexing as the connector will run on a different dispatcher.
 */
static void putToConnectorWriteq(FastConnector *connector, HttpPacket *packet)
{
    packet->data = connector->proxy;
    mprCreateEvent(connector->dispatcher, "fast", 0, fastConnectorData, packet, 0);
}


/*
    Called via CreateEvent to transfer packets from the handler's dispatcher to the connector's dispatcher
 */
static void fastConnectorData(HttpPacket *packet)
{
    FastProxy   *proxy;

    proxy = packet->data;
    httpPutForService(proxy->connector->writeq, packet, HTTP_SCHEDULE_QUEUE);
    /*
        Must explicitly service queue. NetConnector queues are scheduled via httpProcess. This private queue must be serviced here.
     */
    httpServiceQueue(proxy->connector->writeq);
}


/*
    Parse an incoming packet (response) from the FastCGI app
 */
static void fastConnectorIncomingService(HttpQueue *q)
{
    FastConnector   *connector;
    FastProxy       *proxy;
    HttpPacket      *packet, *tail;
    HttpStream      *stream;
    MprBuf          *buf;
    ssize           contentLength, len, padLength;
    int             requestID, type, version;

    connector = q->queueData;
    proxy = connector->proxy;

    while ((packet = httpGetPacket(q)) != 0) {
        buf = packet->content;

        if (mprGetBufLength(buf) < FAST_PACKET_SIZE) {
            /* Insufficient data */
            httpPutBackPacket(q, packet);
            // httpLog(proxy->trace, "fast", "context", "msg: 'FastCGI waiting for rest of header frame'");
            break;
        }
        version = mprGetCharFromBuf(buf);
        type = mprGetCharFromBuf(buf);
        requestID = (mprGetCharFromBuf(buf) << 8) | (mprGetCharFromBuf(buf) & 0xFF);
        contentLength = (mprGetCharFromBuf(buf) << 8) | (mprGetCharFromBuf(buf) & 0xFF);
        padLength = mprGetCharFromBuf(buf);
        /* reserved */ (void) mprGetCharFromBuf(buf);
        len = contentLength + padLength;

        if (version != FAST_VERSION) {
            httpLog(proxy->trace, "fast.rx", "error", "msg:'Bad FastCGI response version'");
            break;
        }
        if (contentLength < 0 || contentLength > 65535) {
            httpLog(proxy->trace, "fast", "error", "msg:'Bad FastCGI content length', length:%ld", contentLength);
            break;
        }
        if (padLength < 0 || padLength > 255) {
            httpLog(proxy->trace, "fast", "error", "msg:'Bad FastCGI pad length', padding:%ld", padLength);
            break;
        }
        if (mprGetBufLength(buf) < len) {
            /* Insufficient data */
            mprAdjustBufStart(buf, -FAST_PACKET_SIZE);
            httpPutBackPacket(q, packet);
            break;
        }
        packet->type = type;

        httpLog(proxy->trace, "fast", "context", "msg:'FastCGI incoming packet', type:'%s' id:%d, length:%ld",
                fastTypes[type], requestID, contentLength);

        if ((tail = httpSplitPacket(packet, len)) != 0) {
            httpPutBackPacket(q, tail);
        }
        if (padLength) {
            mprAdjustBufEnd(packet->content, -padLength);
        }
        if ((stream = getStream(proxy, requestID)) != 0) {
            if (type == FAST_STDOUT || type == FAST_END_REQUEST) {
                packet->stream = stream;
                packet->data = proxy;
                mprCreateEvent(stream->dispatcher, "fast", 0, fastHandlerResponse, packet, 0);

            } else if (type == FAST_STDERR) {
                httpLog(proxy->trace, "fast", "error", "msg:'FastCGI stderr', uri:'%s', error:'%s'",
                    stream->rx->uri, mprBufToString(packet->content));

            } else {
                httpLog(proxy->trace, "fast", "error", "msg:'FastCGI invalid packet', command:'%s', type:%d",
                    stream->rx->uri, type);
                connector->destroy = 1;
            }
        }
    }
}


/*
    Handle IO on the network
 */
static void fastConnectorIO(FastConnector *connector, MprEvent *event)
{
    HttpPacket  *packet;
    ssize       nbytes;

    if (connector->eof) {
        /* Network connection to client has been destroyed */
        return;
    }
    if (event->mask & MPR_WRITABLE) {
        httpServiceQueue(connector->writeq);
    }
    if (event->mask & MPR_READABLE) {
        packet = httpCreateDataPacket(ME_PACKET_SIZE);
        nbytes = mprReadSocket(connector->socket, mprGetBufEnd(packet->content), ME_PACKET_SIZE);
        connector->eof = mprIsSocketEof(connector->socket);
        if (nbytes > 0) {
            mprAdjustBufEnd(packet->content, nbytes);
            httpJoinPacketForService(connector->readq, packet, 0);
            httpServiceQueue(connector->readq);
        }
    }
    if (connector->eof) {
        connector->destroy = 1;
    } else {
        enableFastConnectorEvents(connector);
    }
}


static void enableFastConnectorEvents(FastConnector *connector)
{
    MprSocket   *sp;
    int         eventMask;

    sp = connector->socket;

    if (!connector->eof && !(sp->flags & MPR_SOCKET_CLOSED)) {
        eventMask = 0;
        if (connector->writeq->count > 0) {
            eventMask |= MPR_WRITABLE;
        }
        if (connector->readq->count < connector->readq->max) {
            eventMask |= MPR_READABLE;
        }
        if (eventMask) {
            if (sp->handler == 0) {
                mprAddSocketHandler(sp, eventMask, connector->dispatcher, fastConnectorIO, connector, 0);
            } else {
                mprWaitOn(sp->handler, eventMask);
            }
        } else if (sp->handler) {
            mprWaitOn(sp->handler, eventMask);
        }
    }
}


//  MOB UNUSED?
static void fastConnectorOutgoing(HttpQueue *q, HttpPacket *packet)
{
    assert(0);
    httpPutForService(q, packet, HTTP_SCHEDULE_QUEUE);
}


/*
    Send requset and post body data to the fastCGI app
 */
static void fastConnectorOutgoingService(HttpQueue *q)
{
    FastConnector   *connector;
    ssize           written;
    int             errCode;

    connector = q->queueData;
    connector->writeBlocked = 0;

    while (q->first || q->ioIndex) {
        if (q->ioIndex == 0 && buildFastVec(q) <= 0) {
            freeFastPackets(q, 0);
            break;
        }
        written = mprWriteSocketVector(connector->socket, q->iovec, q->ioIndex);
        if (written < 0) {
            errCode = mprGetError();
            if (errCode == EAGAIN || errCode == EWOULDBLOCK) {
                /*  Socket full, wait for an I/O event */
                connector->writeBlocked = 1;
                break;
            }
            httpLog(connector->proxy->trace, "fast", "error", "msg='Write error', errno:%d", errCode);
            connector->eof = 1;
            connector->destroy = 1;
            break;

        } else if (written > 0) {
            freeFastPackets(q, written);
            adjustNetVec(q, written);

        } else {
            /* Socket full */
            break;
        }
    }
    enableFastConnectorEvents(connector);
}


/*
    Build the IO vector. Return the count of bytes to be written. Return -1 for EOF.
 */
static MprOff buildFastVec(HttpQueue *q)
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
            addFastPacket(q, packet);
        }
    }
    return q->ioCount;
}


/*
    Add a packet to the io vector. Return the number of bytes added to the vector.
 */
static void addFastPacket(HttpQueue *q, HttpPacket *packet)
{
    assert(q->count >= 0);
    assert(q->ioIndex < (ME_MAX_IOVEC - 2));

    if (packet->prefix && mprGetBufLength(packet->prefix) > 0) {
        addToFastVector(q, mprGetBufStart(packet->prefix), mprGetBufLength(packet->prefix));
    }
    if (packet->content && mprGetBufLength(packet->content) > 0) {
        addToFastVector(q, mprGetBufStart(packet->content), mprGetBufLength(packet->content));
    }
}


/*
    Add one entry to the io vector
 */
static void addToFastVector(HttpQueue *q, char *ptr, ssize bytes)
{
    assert(bytes > 0);

    q->iovec[q->ioIndex].start = ptr;
    q->iovec[q->ioIndex].len = bytes;
    q->ioCount += bytes;
    q->ioIndex++;
}


static void freeFastPackets(HttpQueue *q, ssize bytes)
{
    HttpPacket  *packet;
    ssize       len;

    assert(q->count >= 0);
    assert(bytes >= 0);

    while ((packet = q->first) != 0) {
        if (packet->flags & HTTP_PACKET_END) {
            /* MOB
            if ((stream = packet->stream) != 0) {
                // httpFinalizeConnector(stream);
                httpProcess(stream->inputq);
            } */
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
static void adjustNetVec(HttpQueue *q, ssize written)
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


static void encodeFastLen(MprBuf *buf, cchar *s)
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


static void encodeFastName(HttpPacket *packet, cchar *name, cchar *value)
{
    MprBuf      *buf;

    buf = packet->content;
    encodeFastLen(buf, name);
    encodeFastLen(buf, value);
    mprPutStringToBuf(buf, name);
    mprPutStringToBuf(buf, value);
}


static void copyFastInner(HttpPacket *packet, cchar *key, cchar *value, cchar *prefix)
{
    FastProxy   *proxy;

    proxy = packet->data;
    if (prefix) {
        key = sjoin(prefix, key, NULL);
    }
    httpLog(proxy->trace, "fast.tx", "info", "msg:'FastCGI env', key:'%s', value:'%s'", key, value);
    encodeFastName(packet, key, value);
}


static void copyFastVars(HttpPacket *packet, MprHash *vars, cchar *prefix)
{
    MprKey  *kp;

    for (ITERATE_KEYS(vars, kp)) {
        if (kp->data) {
            copyFastInner(packet, kp->key, kp->data, prefix);
        }
    }
}


static void copyFastParams(HttpPacket *packet, MprJson *params, cchar *prefix)
{
    MprJson     *param;
    int         i;

    for (ITERATE_JSON(params, param, i)) {
        copyFastInner(packet, param->name, param->value, prefix);
    }
}


static int fastConnectDirective(MaState *state, cchar *key, cchar *value)
{
    Fast    *fast;
    cchar   *endpoint, *args;
    char    *option, *ovalue, *tok;

    fast = getFast(state->route);

    if (!maTokenize(state, value, "%S ?*", &endpoint, &args)) {
        return MPR_ERR_BAD_SYNTAX;
    }
    fast->endpoint = endpoint;

    for (option = stok(sclone(args), " \t", &tok); option; option = stok(0, " \t", &tok)) {
        option = ssplit(option, " =\t,", &ovalue);
        ovalue = strim(ovalue, "\"'", MPR_TRIM_BOTH);

        if (smatch(option, "count")) {
            fast->maxRequests = httpGetInt(ovalue);
            if (fast->maxRequests < 1) {
                fast->maxRequests = 1;
            }

        } else if (smatch(option, "launch")) {
            fast->launch = 1;

        } else if (smatch(option, "max")) {
            fast->maxProxies = httpGetInt(ovalue);
            if (fast->maxProxies < 1) {
                fast->maxProxies = 1;
            }

        } else if (smatch(option, "min")) {
            fast->minProxies = httpGetInt(ovalue);
            if (fast->minProxies < 1) {
                fast->minProxies = 1;
            }

        } else if (smatch(option, "multiplex")) {
            fast->multiplex = httpGetInt(ovalue);
            if (fast->multiplex < 1) {
                fast->multiplex = 1;
            }

        } else if (smatch(option, "timeout")) {
            fast->proxyTimeout = httpGetTicks(ovalue);
            if (fast->proxyTimeout < (30 * TPS)) {
                fast->proxyTimeout = 30 * TPS;
            }
        } else {
            mprLog("fast error", 0, "Unknown FastCGI option %s", option);
            return MPR_ERR_BAD_SYNTAX;
        }
    }
    if (mprParseSocketAddress(fast->endpoint, &fast->ip, &fast->port, NULL, 9128) < 0) {
        mprLog("fast error", 0, "Cannot bind FastCGI proxy address: %s", fast->endpoint);
        return MPR_ERR_BAD_SYNTAX;
    }
    return 0;
}


static MprSocket *createListener(FastConnector *connector)
{
    Fast        *fast;
    FastProxy   *proxy;
    MprSocket   *listen;

    proxy = connector->proxy;
    fast = proxy->fast;

    listen = mprCreateSocket();
    if (mprListenOnSocket(listen, fast->ip, fast->port, MPR_SOCKET_BLOCK | MPR_SOCKET_NODELAY) == SOCKET_ERROR) {
        if (mprGetError() == EADDRINUSE) {
            httpLog(proxy->trace, "fast.rx", "error", "msg:'Cannot open a socket, already bound', address: '%s:%d'",
                fast->ip ? fast->ip : "*", fast->port);
        } else {
            httpLog(proxy->trace, "fast.rx", "error", "msg:'Cannot open a socket', address: '%s:%d'",
                fast->ip ? fast->ip : "*", fast->port);
        }
        return NULL;
    }
    if (fast->port == 0) {
        fast->port = getListenPort(listen);
    }
    httpLog(proxy->trace, "fast.rx", "context", "msg:'Listening for FastCGI', endpoint: '%s:%d'",
        fast->ip ? fast->ip : "*", fast->port);
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
#if KEEP
    struct sockaddr *sa;
    sa = (struct sockaddr *)&sin;
    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];
    int rc = getnameinfo(sa, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
#endif
    return ntohs(sin.sin_port);
}


static HttpStream *getStream(FastProxy *proxy, int requestID)
{
    HttpStream  *stream;
    int         next;

    for (ITERATE_ITEMS(proxy->streams, stream, next)) {
        if ((int) stream->reqID == requestID) {
            return stream;
        }
    }
    return NULL;
}

#endif /* ME_COM_FAST */

/*
    Copyright (c) Embedthis Software. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
