/*
    fastHandler.c -- FastCGI handler

    This handler supports the full spec: https://github.com/fast-cgi/spec/blob/master/spec.md

    It supports launching FastCGI applications and connecting to pre-existing FastCGI applications.
    It will multiplex multiple simultaneous requests to one or more FastCGI apps.

    <Route /fast>
        LoadModule fastHandler libmod_fast
        Action application/x-php /usr/local/bin/php-cgi
        AddHandler fastHandler php
        FastConnect 127.0.0.1:9991 launch min=1 max=2 count=500 timeout=5mins multiplex=1
    </Route>

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/*********************************** Includes *********************************/

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

/*
    Pseudo types
 */
 #define FAST_COMMS_ERROR       12          //  Communications error

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
#define FAST_MAX_REQUESTS       MAXINT64    //  Max number of requests per proxy instance
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
#define FAST_PROXY_TIMEOUT      (300 * TPS) //  Default inactivity time to preserve idle proxy
#define FAST_REAP_TIMEOUT       (10 * TPS)  //  Time to wait for kill proxy to take effect
#define FAST_WATCHDOG_TIMEOUT   (60 * TPS)  //  Frequence to check on idle proxies

/*
    Top level FastCGI structure per route
 */
typedef struct Fast {
    cchar           *endpoint;              //  Proxy listening endpoint
    int             launch;                 //  Launch proxy
    int             multiplex;              //  Maximum number of requests to send to each FastCGI proxy
    int             minProxies;             //  Minumum number of proxies to maintain
    int             maxProxies;             //  Maximum number of proxies to spawn
    uint64          maxRequests;            //  Maximum number of requests for launched proxies before respawning
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
    HttpTrace       *trace;                 // Default tracing configuration
    MprTicks        lastActive;             // When last active
    MprSignal       *signal;                // Mpr signal handler for child death
    bool            destroy;                // Must destroy proxy
    int             inUse;                  // In use counter
    int             pid;                    // Process ID of the FastCGI proxy app
    uint64          nextID;                 // Next request ID for this proxy
    MprList         *comms;                 // Connectors for each request
} FastProxy;

/*
    Per FastCGI comms instance. This is separate from the FastProxy properties because the
    FastComm executes on a different dispatcher.
 */
typedef struct FastComm {
    Fast            *fast;                  // Parent pointer
    FastProxy       *proxy;                 // Owning proxy
    MprSocket       *socket;                // I/O socket
    HttpStream      *stream;                // Owning client request stream
    HttpQueue       *writeq;                // Queue to write to the FastCGI app
    HttpQueue       *readq;                 // Queue to hold read data from the FastCGI app
    HttpTrace       *trace;                 // Default tracing configuration
    uint64          reqID;                  // FastCGI request ID - assigned from FastProxy.nextID
    bool            eof;                    // Socket is closed
    bool            parsedHeaders;          // Parsed the FastCGI app header response
    bool            writeBlocked;           // Socket is full of write data
} FastComm;

/*********************************** Forwards *********************************/

static void addFastPacket(HttpQueue *q, HttpPacket *packet);
static void addToFastVector(HttpQueue *q, char *ptr, ssize bytes);
static void adjustFastVec(HttpQueue *q, ssize written);
static Fast *allocFast(void);
static FastComm *allocFastComm(FastProxy *proxy, HttpStream *stream);
static FastProxy *allocFastProxy(Fast *fast, HttpStream *stream);
static cchar *buildProxyArgs(HttpStream *stream, int *argcp, cchar ***argvp);
static MprOff buildFastVec(HttpQueue *q);
static void closeFast(HttpQueue *q);
static FastComm *connectFastComm(FastProxy *proxy, HttpStream *stream);
static void copyFastInner(HttpPacket *packet, cchar *key, cchar *value, cchar *prefix);
static void copyFastParams(HttpPacket *packet, MprJson *params, cchar *prefix);
static void copyFastVars(HttpPacket *packet, MprHash *vars, cchar *prefix);
static HttpPacket *createFastPacket(HttpQueue *q, int type, HttpPacket *packet);
static MprSocket *createListener(FastProxy *proxy, HttpStream *stream);
static void enableFastCommEvents(FastComm *comm);
static void fastConnectorIO(FastComm *comm, MprEvent *event);
static void fastConnectorIncoming(HttpQueue *q, HttpPacket *packet);
static void fastConnectorIncomingService(HttpQueue *q);
static void fastConnectorOutgoingService(HttpQueue *q);
static void fastHandlerReapResponse(FastComm *comm);
static void fastHandlerResponse(FastComm *comm, int type, HttpPacket *packet);
static void fastIncoming(HttpQueue *q, HttpPacket *packet);
static int fastConnectDirective(MaState *state, cchar *key, cchar *value);
static void freeFastPackets(HttpQueue *q, ssize bytes);
static Fast *getFast(HttpRoute *route);
static char *getFastToken(MprBuf *buf, cchar *delim);
static FastProxy *getFastProxy(Fast *fast, HttpStream *stream);
static int getListenPort(MprSocket *socket);
static void killFastProxy(FastProxy *proxy);
static void manageFast(Fast *fast, int flags);
static void manageFastProxy(FastProxy *proxy, int flags);
static void manageFastComm(FastComm *fastConnector, int flags);
static int openFast(HttpQueue *q);
static bool parseFastHeaders(HttpPacket *packet);
static bool parseFastResponseLine(HttpPacket *packet);
static void prepFastRequestStart(HttpQueue *q);
static void prepFastRequestParams(HttpQueue *q);
static void reapSignalHandler(FastProxy *proxy, MprSignal *sp);
static FastProxy *startFastProxy(Fast *fast, HttpStream *stream);
static void terminateIdleFastProxies(Fast *fast);

/************************************* Code ***********************************/
/*
    Loadable module initialization
 */
PUBLIC int httpFastInit(Http *http, MprModule *module)
{
    HttpStage   *handler, *connector;

    /*
        Add configuration file directives
     */
    maAddDirective("FastConnect", fastConnectDirective);

    /*
        Create FastCGI handler to respond to client requests
     */
    if ((handler = httpCreateHandler("fastHandler", module)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->fastHandler = handler;
    handler->close = closeFast;
    handler->open = openFast;
    handler->incoming = fastIncoming;

    /*
        Create FastCGI connector. The connector manages communication to the FastCGI application.
        The Fast handler is the head of the pipeline while the Fast connector is
        after the Http protocol and tailFilter.
    */
    if ((connector = httpCreateConnector("fastConnector", NULL)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->fastConnector = connector;
    connector->incoming = fastConnectorIncoming;
    connector->incomingService = fastConnectorIncomingService;
    connector->outgoingService = fastConnectorOutgoingService;
    return 0;
}


/*
    Open the fastHandler for a new client request
 */
static int openFast(HttpQueue *q)
{
    Http        *http;
    HttpNet     *net;
    HttpStream  *stream;
    Fast        *fast;
    FastProxy   *proxy;
    FastComm    *comm;

    net = q->net;
    stream = q->stream;
    http = stream->http;

    httpTrimExtraPath(stream);
    httpMapFile(stream);
    httpCreateCGIParams(stream);

    /*
        Get a Fast instance for this route. First time, this will allocate a new Fast instance. Second and
        subsequent times, will reuse the existing instance.
     */
    fast = getFast(stream->rx->route);

    /*
        Get a FastProxy instance. This will reuse an existing FastCGI proxy app if possible. Otherwise,
        it will launch a new FastCGI proxy app if within limits. Otherwise it will wait until one becomes available.
     */
    if ((proxy = getFastProxy(fast, stream)) == 0) {
        httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot allocate FastCGI proxy for route %s", stream->rx->route->pattern);
        return MPR_ERR_CANT_OPEN;
    }

    /*
        Open a dedicated client socket to the FastCGI proxy app
     */
    if ((comm = connectFastComm(proxy, stream)) == NULL) {
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot connect to fast proxy: %d", errno);
        return MPR_ERR_CANT_CONNECT;
    }
    mprAddItem(proxy->comms, comm);
    q->queueData = q->pair->queueData = comm;

    /*
        Send a start request followed by the request parameters
     */
    prepFastRequestStart(q);
    prepFastRequestParams(q);
    enableFastCommEvents(comm);
    return 0;
}


/*
    Release a proxy and comm when the request completes. This closes the connection to the FastCGI app.
    It will destroy the FastCGI app on errors or if the number of requests exceeds the maxRequests limit.
 */
static void closeFast(HttpQueue *q)
{
    Fast        *fast;
    FastComm    *comm;
    FastProxy   *proxy;
    cchar       *msg;

    comm = q->queueData;
    fast = comm->fast;
    proxy = comm->proxy;

    lock(fast);

    if (comm->socket) {
        mprCloseSocket(comm->socket, 1);
        mprRemoveSocketHandler(comm->socket);
        comm->socket = 0;
    }
    mprRemoveItem(proxy->comms, comm);

    if (--proxy->inUse <= 0) {
        if (mprRemoveItem(fast->proxies, proxy) < 0) {
            httpLog(proxy->trace, "fast", "error", "msg:'Cannot find proxy in list'");
        }
        if (proxy->destroy || (fast->maxRequests < MAXINT64 && proxy->nextID >= fast->maxRequests) ||
                (mprGetListLength(fast->proxies) + mprGetListLength(fast->idleProxies) >= fast->minProxies)) {
            msg = "Destroy FastCGI proxy";
            killFastProxy(proxy);
        } else {
            msg = "Release FastCGI proxy";
            proxy->lastActive = mprGetTicks();
            mprAddItem(fast->idleProxies, proxy);
        }
        httpLog(proxy->trace, "fast", "context",
            "msg:'%s', pid:%d, idle:%d, active:%d, id:%lld, maxRequests:%lld, destroy:%d, nextId:%lld",
            msg, proxy->pid, mprGetListLength(fast->idleProxies), mprGetListLength(fast->proxies),
            proxy->nextID, fast->maxRequests, proxy->destroy, proxy->nextID);
        mprSignalCond(fast->cond);
    }
    unlock(fast);
}


static Fast *allocFast(void)
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
    fast->timer = mprCreateTimerEvent(NULL, "fast-watchdog", FAST_WATCHDOG_TIMEOUT,
        terminateIdleFastProxies, fast, MPR_EVENT_QUICK);
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


static void terminateIdleFastProxies(Fast *fast)
{
    FastProxy   *proxy;
    MprTicks    now;
    int         count, next;

    lock(fast);
    now = mprGetTicks();
    count = mprGetListLength(fast->proxies) + mprGetListLength(fast->idleProxies);
    for (ITERATE_ITEMS(fast->idleProxies, proxy, next)) {
        if (proxy->pid && ((now - proxy->lastActive) > fast->proxyTimeout)) {
            if (count-- > fast->minProxies) {
                killFastProxy(proxy);
            }
        }
    }
    unlock(fast);
}


/*
    Get the fast structure for a route and save in "eroute". Allocate if required.
    One Fast instance is shared by all using the route.
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
    POST/PUT incoming body data from the client destined for the CGI gateway. : For POST "form" requests,
    this will be called before the command is actually started.
 */
static void fastIncoming(HttpQueue *q, HttpPacket *packet)
{
    HttpStream  *stream;
    FastComm    *comm;

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
            packet = createFastPacket(q, FAST_ABORT_REQUEST, httpCreateDataPacket(0));

        } else {
            createFastPacket(q, FAST_STDIN, packet);
        }
    } else {
        createFastPacket(q, FAST_STDIN, packet);
    }
    httpPutForService(comm->writeq, packet, HTTP_SCHEDULE_QUEUE);
}


static void fastHandlerReapResponse(FastComm *comm)
{
    fastHandlerResponse(comm, FAST_REAP, NULL);
}


/*
    Handle response messages from the FastCGI app
 */
static void fastHandlerResponse(FastComm *comm, int type, HttpPacket *packet)
{
    FastProxy   *proxy;
    HttpStream  *stream;
    HttpRx      *rx;
    MprBuf      *buf;
    int         status, protoStatus;

    stream = comm->stream;
    proxy = comm->proxy;

    if (stream->state <= HTTP_STATE_BEGIN || stream->rx->route == NULL) {
        /* Request already complete and stream has been recycled (prepared for next request) */
        return;
    }

    if (type == FAST_COMMS_ERROR) {
        httpError(stream, HTTP_ABORT | HTTP_CODE_COMMS_ERROR, "FastComm: comms error");

    } else if (type == FAST_REAP) {
        //  Reap may happen before valid I/O has drained
        // httpError(stream, HTTP_ABORT | HTTP_CODE_COMMS_ERROR, "FastComm: proxy process killed error");

    } else if (type == FAST_END_REQUEST && packet) {
        if (httpGetPacketLength(packet) < 8) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "FastCGI bad request end packet");
            return;
        }
        buf = packet->content;
        rx = stream->rx;

        status = mprGetCharFromBuf(buf) << 24 || mprGetCharFromBuf(buf) << 16 ||
                 mprGetCharFromBuf(buf) << 8 || mprGetCharFromBuf(buf);
        protoStatus = mprGetCharFromBuf(buf);
        mprAdjustBufStart(buf, 3);

        if (protoStatus == FAST_REQUEST_COMPLETE) {
            httpLog(proxy->trace, "fast.rx", "context", "msg:'Request complete', id:%lld, status:%d", comm->reqID, status);

        } else if (protoStatus == FAST_CANT_MPX_CONN) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "FastCGI cannot multiplex requests %s", rx->uri);
            return;

        } else if (protoStatus == FAST_OVERLOADED) {
            httpError(stream, HTTP_CODE_SERVICE_UNAVAILABLE, "FastCGI overloaded %s", rx->uri);
            return;

        } else if (protoStatus == FAST_UNKNOWN_ROLE) {
            httpError(stream, HTTP_CODE_BAD_GATEWAY, "FastCGI unknown role %s", rx->uri);
            return;
        }
        httpLog(proxy->trace, "fast.rx.eof", "detail", "msg:'FastCGI end request', id:%lld", comm->reqID);
        httpFinalizeOutput(stream);

    } else if (type == FAST_STDOUT && packet) {
        if (!comm->parsedHeaders) {
            if (!parseFastHeaders(packet)) {
                return;
            }
            comm->parsedHeaders = 1;
        }
        if (httpGetPacketLength(packet) > 0) {
            httpLogPacket(proxy->trace, "fast.rx.data", "packet", 0, packet, "type:%d, id:%lld, len:%ld", type, comm->reqID,
                httpGetPacketLength(packet));
            httpPutPacketToNext(stream->writeq, packet);
        }
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
    FastComm    *comm;
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
                httpLog(comm->trace, "fast.rx", "detail", "msg:'FastCGI incomplete headers', id:%lld", comm->reqID);
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
                /* Now pass all other headers back to the client */
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
/*
    The FastProxy represents the connection to a single FastCGI app instance
 */
static FastProxy *allocFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;

    proxy = mprAllocObj(FastProxy, manageFastProxy);
    proxy->fast = fast;
    proxy->trace = stream->net->trace;
    proxy->comms = mprCreateList(0, 0);

    /*
        The requestID must start at 1 by spec
     */
    proxy->nextID = 1;
    return proxy;
}


static void manageFastProxy(FastProxy *proxy, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(proxy->comms);
        mprMark(proxy->fast);
        mprMark(proxy->signal);
        mprMark(proxy->trace);
    }
}


static FastProxy *getFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;
    MprTicks    timeout;
    int         idle, next;

    lock(fast);
    proxy = NULL;
    timeout = mprGetTicks() +  FAST_WAIT_TIMEOUT;

    /*
        Locate a FastProxy to serve the request. Use an idle proxy first. If none available, start a new proxy
        if under the limits. Otherwise, wait for one to become available.
     */
    while (!proxy && mprGetTicks() < timeout) {
        idle = mprGetListLength(fast->idleProxies);
        if (idle > 0) {
            proxy = mprGetFirstItem(fast->idleProxies);
            mprRemoveItemAtPos(fast->idleProxies, 0);
            mprAddItem(fast->proxies, proxy);
            break;

        } else if (mprGetListLength(fast->proxies) < fast->maxProxies) {
            if ((proxy = startFastProxy(fast, stream)) != 0) {
                mprAddItem(fast->proxies, proxy);
            }
            break;

        } else {
            for (ITERATE_ITEMS(fast->proxies, proxy, next)) {
                if (mprGetListLength(proxy->comms) < fast->multiplex) {
                    break;
                }
            }
            if (proxy) {
                break;
            }
            unlock(fast);
            //  TEST
            if (mprWaitForCond(fast->cond, TPS) < 0) {
                return NULL;
            }
            lock(fast);
        }
    }
    if (proxy) {
        proxy->lastActive = mprGetTicks();
        proxy->inUse++;
    }
    unlock(fast);
    return proxy;
}


/*
    Start a new FastCGI app process. Called with lock(fast)
 */
static FastProxy *startFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;
    HttpRoute   *route;
    MprSocket   *listen;
    cchar       **argv, *command;
    int         argc, i;

    route = stream->rx->route;
    proxy = allocFastProxy(fast, stream);

    if (fast->launch) {
        argc = 1;                                   /* argv[0] == programName */
        if ((command = buildProxyArgs(stream, &argc, &argv)) == 0) {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot find Fast proxy command");
            return NULL;
        }
        httpLog(stream->trace, "fast", "context", "msg:'Start FastCGI proxy', command:'%s'", command);

        if ((listen = createListener(proxy, stream)) == NULL) {
            return NULL;
        }
        if (!proxy->signal) {
            proxy->signal = mprAddSignalHandler(SIGCHLD, reapSignalHandler, proxy, NULL, MPR_SIGNAL_BEFORE);
        }
        if ((proxy->pid = fork()) < 0) {
            fprintf(stderr, "Fork failed for FastCGI");
            return NULL;

        } else if (proxy->pid == 0) {
            /* Child */
            dup2(listen->fd, 0);
            /*
                When debugging, keep stdout/stderr open so printf/fprintf from the FastCGI app will show in the console.
             */
            for (i = FAST_DEBUG ? 3 : 1; i < 128; i++) {
                close(i);
            }
            // FUTURE envp[0] = sfmt("FCGI_WEB_SERVER_ADDRS=%s", route->host->defaultEndpoint->ip);
            if (execve(command, (char**) argv, NULL /* (char**) &env->items[0] */) < 0) {
                printf("Cannot exec fast proxy: %s\n", command);
            }
            return NULL;
        } else {
            httpLog(proxy->trace, "fast", "context", "msg:'FastCGI started proxy', command:'%s', pid:%d", command, proxy->pid);
            mprCloseSocket(listen, 0);
        }
    }
    return proxy;
}


/*
    Build the command arguments for the FastCGI app
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
    Proxy process has died, so reap the status and inform relevant streams.
    WARNING: this may be called before all the data has been read from the socket, so we must not set eof = 1 here.
    WARNING: runs on the MPR dispatcher. Everyting must be "fast" locked.
 */
static void reapSignalHandler(FastProxy *proxy, MprSignal *sp)
{
    Fast        *fast;
    FastComm    *comm;
    int         next, status;

    fast = proxy->fast;

    lock(fast);
    if (proxy->pid && waitpid(proxy->pid, &status, WNOHANG) == proxy->pid) {
        httpLog(proxy->trace, "fast", WEXITSTATUS(status) == 0 ? "context" : "error",
            "msg:'FastCGI exited', pid:%d, status:%d", proxy->pid, WEXITSTATUS(status));
        if (proxy->signal) {
            mprRemoveSignalHandler(proxy->signal);
            proxy->signal = 0;
        }
        if (mprLookupItem(proxy->fast->idleProxies, proxy) >= 0) {
            mprRemoveItem(proxy->fast->idleProxies, proxy);
        }
        proxy->destroy = 1;
        proxy->pid = 0;

        /*
            Notify all comms on their relevant dispatcher
         */
        for (ITERATE_ITEMS(proxy->comms, comm, next)) {
            mprCreateEvent(comm->stream->dispatcher, "fast-reap", 0, fastHandlerReapResponse, comm, 0);
        }
    }
    unlock(fast);
}


/*
    Kill the FastCGI proxy app due to error or maxRequests limit being exceeded
 */
static void killFastProxy(FastProxy *proxy)
{
    lock(proxy->fast);
    if (proxy->pid) {
        httpLog(proxy->trace, "fast", "context", "msg: 'Kill FastCGI process', pid:%d", proxy->pid);
        if (proxy->pid) {
            kill(proxy->pid, SIGTERM);
        }
    }
    unlock(proxy->fast);
}


/*
    Create a socket connection to the FastCGI app. Retry if the FastCGI is not yet ready.
 */
static FastComm *connectFastComm(FastProxy *proxy, HttpStream *stream)
{
    Fast        *fast;
    FastComm    *comm;
    MprTicks    timeout;
    int         retries, connected;

    fast = proxy->fast;

    lock(fast);
    connected = 0;
    retries = 1;

    comm = allocFastComm(proxy, stream);

    timeout = mprGetTicks() +  FAST_CONNECT_TIMEOUT;
    while (1) {
        httpLog(stream->trace, "fast.rx", "request", "FastCGI try to connect to %s:%d", fast->ip, fast->port);
        comm->socket = mprCreateSocket();
        if (mprConnectSocket(comm->socket, fast->ip, fast->port, 0) == 0) {
            connected = 1;
            break;
        }
        if (mprGetTicks() >= timeout) {
            unlock(fast);
            return NULL;
        }
        mprSleep(50 * retries++);
    }

    unlock(fast);
    return comm;
}


/*
    Add the FastCGI spec packet header to the packet->prefix
    See the spec at https://github.com/fast-cgi/spec/blob/master/spec.md
 */
static HttpPacket *createFastPacket(HttpQueue *q, int type, HttpPacket *packet)
{
    FastComm    *comm;
    uchar       *buf;
    ssize       len, pad;

    comm = q->queueData;
    if (!packet) {
        packet = httpCreateDataPacket(0);
    }
    len = httpGetPacketLength(packet);

    packet->prefix = mprCreateBuf(16, 16);
    buf = (uchar*) packet->prefix->start;
    *buf++ = FAST_VERSION;
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

    httpLog(comm->trace, "fast.tx", "packet", "msg:FastCGI send packet', type:%d, id:%lld, lenth:%ld", type, comm->reqID, len);
    return packet;
}


static void prepFastRequestStart(HttpQueue *q)
{
    HttpPacket  *packet;
    FastComm    *comm;
    uchar       *buf;

    comm = q->queueData;
    packet = httpCreateDataPacket(16);
    buf = (uchar*) packet->content->start;
    *buf++= 0;
    *buf++= FAST_RESPONDER;
    *buf++ = FAST_KEEP_CONN;
    /* Reserved bytes */
    buf += 5;
    mprAdjustBufEnd(packet->content, 8);
    httpPutForService(comm->writeq, createFastPacket(q, FAST_BEGIN_REQUEST, packet), HTTP_SCHEDULE_QUEUE);
}


static void prepFastRequestParams(HttpQueue *q)
{
    FastComm    *comm;
    HttpStream  *stream;
    HttpPacket  *packet;
    HttpRx      *rx;

    comm = q->queueData;
    stream = q->stream;
    rx = stream->rx;

    packet = httpCreateDataPacket(stream->limits->headerSize);
    packet->data = comm;
    copyFastParams(packet, rx->params, rx->route->envPrefix);
    copyFastVars(packet, rx->svars, "");
    copyFastVars(packet, rx->headers, "HTTP_");

    httpPutForService(comm->writeq, createFastPacket(q, FAST_PARAMS, packet), HTTP_SCHEDULE_QUEUE);
    httpPutForService(comm->writeq, createFastPacket(q, FAST_PARAMS, 0), HTTP_SCHEDULE_QUEUE);
}


/************************************************ FastComm ***********************************************************/
/*
    Setup the proxy comm. Must be called locked
 */
static FastComm *allocFastComm(FastProxy *proxy, HttpStream *stream)
{
    FastComm    *comm;

    comm = mprAllocObj(FastComm, manageFastComm);
    comm->stream = stream;
    comm->trace = stream->trace;
    comm->reqID = proxy->nextID++;
    comm->fast = proxy->fast;
    comm->proxy = proxy;

    comm->readq = httpCreateQueue(stream->net, stream, HTTP->fastConnector, HTTP_QUEUE_RX, 0);
    comm->writeq = httpCreateQueue(stream->net, stream, HTTP->fastConnector, HTTP_QUEUE_TX, 0);

    comm->readq->max = FAST_Q_SIZE;
    comm->writeq->max = FAST_Q_SIZE;

    comm->readq->queueData = comm;
    comm->writeq->queueData = comm;
    comm->readq->pair = comm->writeq;
    comm->writeq->pair = comm->readq;
    return comm;
}


static void manageFastComm(FastComm *comm, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(comm->fast);
        mprMark(comm->proxy);
        mprMark(comm->readq);
        mprMark(comm->socket);
        mprMark(comm->stream);
        mprMark(comm->trace);
        mprMark(comm->writeq);
    }
}


static void fastConnectorIncoming(HttpQueue *q, HttpPacket *packet)
{
    FastComm    *comm;

    comm = q->queueData;
    httpPutForService(comm->writeq, packet, HTTP_SCHEDULE_QUEUE);
}


/*
    Parse an incoming response packet from the FastCGI app
 */
static void fastConnectorIncomingService(HttpQueue *q)
{
    FastComm        *comm;
    FastProxy       *proxy;
    HttpPacket      *packet, *tail;
    MprBuf          *buf;
    ssize           contentLength, len, padLength;
    int             requestID, type, version;

    comm = q->queueData;
    proxy = comm->proxy;
    proxy->lastActive = mprGetTicks();

    while ((packet = httpGetPacket(q)) != 0) {
        buf = packet->content;

        if (mprGetBufLength(buf) < FAST_PACKET_SIZE) {
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

        httpLog(proxy->trace, "fast", "packet", "msg:'FastCGI incoming packet', type:'%s' id:%d, length:%ld",
                fastTypes[type], requestID, contentLength);

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
        if (type == FAST_STDOUT || type == FAST_END_REQUEST) {
            fastHandlerResponse(comm, type, packet);

        } else if (type == FAST_STDERR) {
            /* Log and discard stderr */
            httpLog(proxy->trace, "fast", "error", "msg:'FastCGI stderr', uri:'%s', error:'%s'",
                comm->stream->rx->uri, mprBufToString(packet->content));

        } else {
            httpLog(proxy->trace, "fast", "error", "msg:'FastCGI invalid packet', command:'%s', type:%d",
                comm->stream->rx->uri, type);
            proxy->destroy = 1;
        }
    }
}


/*
    Handle IO events on the network
 */
static void fastConnectorIO(FastComm *comm, MprEvent *event)
{
    Fast        *fast;
    HttpPacket  *packet;
    ssize       nbytes;

    fast = comm->fast;
    if (comm->eof) {
        /* Network connection to client has been destroyed */
        return;
    }
    if (event->mask & MPR_WRITABLE) {
        httpServiceQueue(comm->writeq);
    }
    if (event->mask & MPR_READABLE) {
        lock(fast);
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
        unlock(fast);
    }
    httpServiceNetQueues(comm->stream->net, 0);
    //httpProcess(comm->stream->inputq)

    if (!comm->eof) {
        enableFastCommEvents(comm);
    }
}


static void enableFastCommEvents(FastComm *comm)
{
    MprSocket   *sp;
    int         eventMask;

    lock(comm->fast);
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
                mprAddSocketHandler(sp, eventMask, comm->stream->dispatcher, fastConnectorIO, comm, 0);
            } else {
                mprWaitOn(sp->handler, eventMask);
            }
        } else if (sp->handler) {
            mprWaitOn(sp->handler, eventMask);
        }
    }
    unlock(comm->fast);
}


/*
    Send request and post body data to the fastCGI app
 */
static void fastConnectorOutgoingService(HttpQueue *q)
{
    Fast            *fast;
    FastProxy       *proxy;
    FastComm        *comm, *cp;
    ssize           written;
    int             errCode, next;

    comm = q->queueData;
    proxy = comm->proxy;
    fast = proxy->fast;
    proxy->lastActive = mprGetTicks();

    lock(fast);
    if (comm->eof || comm->socket == 0) {
        return;
    }
    comm->writeBlocked = 0;

    while (q->first || q->ioIndex) {
        if (q->ioIndex == 0 && buildFastVec(q) <= 0) {
            freeFastPackets(q, 0);
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
            proxy->destroy = 1;
            httpLog(comm->proxy->trace, "fast", "error", "msg='Write error', errno:%d", errCode);

            for (ITERATE_ITEMS(proxy->comms, cp, next)) {
                fastHandlerResponse(cp, FAST_COMMS_ERROR, NULL);
            }
            break;

        } else if (written > 0) {
            freeFastPackets(q, written);
            adjustFastVec(q, written);

        } else {
            /* Socket full */
            break;
        }
    }
    enableFastCommEvents(comm);
    unlock(fast);
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
static void adjustFastVec(HttpQueue *q, ssize written)
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


/*
    FastCGI encoding of strings
 */
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


/*
    FastCGI encoding of names and values. Used to send params.
 */
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
    FastComm    *comm;

    comm = packet->data;
    if (prefix) {
        key = sjoin(prefix, key, NULL);
    }
    httpLog(comm->trace, "fast.tx", "detail", "msg:'FastCGI env', key:'%s', value:'%s'", key, value);
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
    cchar   *endpoint, *args, *ip;
    char    *option, *ovalue, *tok;
    int     port;

    fast = getFast(state->route);

    if (!maTokenize(state, value, "%S ?*", &endpoint, &args)) {
        return MPR_ERR_BAD_SYNTAX;
    }
    fast->endpoint = endpoint;

    for (option = stok(sclone(args), " \t", &tok); option; option = stok(0, " \t", &tok)) {
        option = ssplit(option, " =\t,", &ovalue);
        ovalue = strim(ovalue, "\"'", MPR_TRIM_BOTH);

        if (smatch(option, "count")) {
            fast->maxRequests = httpGetNumber(ovalue);
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
    /*
        Pre-test the endpoint
     */
    if (mprParseSocketAddress(fast->endpoint, &ip, &port, NULL, 9128) < 0) {
        mprLog("fast error", 0, "Cannot bind FastCGI proxy address: %s", fast->endpoint);
        return MPR_ERR_BAD_SYNTAX;
    }
    return 0;
}


/*
    Create listening socket that is passed to the FastCGI app (and then closed after forking)
 */
static MprSocket *createListener(FastProxy *proxy, HttpStream *stream)
{
    Fast        *fast;
    MprSocket   *listen;

    fast = proxy->fast;

    if (mprParseSocketAddress(fast->endpoint, &fast->ip, &fast->port, NULL, 0) < 0) {
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot parse listening endpoint");
        return NULL;
    }
    listen = mprCreateSocket();
    if (mprListenOnSocket(listen, fast->ip, fast->port, MPR_SOCKET_BLOCK | MPR_SOCKET_NODELAY) == SOCKET_ERROR) {
        if (mprGetError() == EADDRINUSE) {
            httpLog(proxy->trace, "fast.rx", "error",
                "msg:'Cannot open listening socket for FastCGI, already bound', address: '%s:%d'",
                fast->ip ? fast->ip : "*", fast->port);
        } else {
            httpLog(proxy->trace, "fast.rx", "error", "msg:'Cannot open listening socket for FastCGI', address: '%s:%d'",
                fast->ip ? fast->ip : "*", fast->port);
        }
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot create listening endpoint");
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
    return ntohs(sin.sin_port);
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
