/*
    fastHandler.c -- Fast CGI handler

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.

    <Route /fast>
        LoadModule fastHandler libmod_fast
        AddHandler fastHandler php
        FastAction application/x-php /usr/local/bin/php-cgi
        FastMinProcesses 1
        FastMaxProcesses 2
        FastMaxRequests 500
        FastListen *:4000
        FastTimeout Ticks         ## Not implemented

        Action application/x-php /usr/local/bin/php-cgi
    </Route>

    MOB
        - Do we need CGI to be configured to get the Action directive -- should not?
        - need fast cgiProgram for testing
        - What about pipes vs sockets?

    FAST_LISTENSOCK_FILENO -- listening socket (on 0)
        getpeername(FAST_LISTENSOCK_FILENO) returns -1 & errno == ENOTCONN
    FAST_WEB_SERVER_ADDRS - list of validate addresses for the web server (comma separated)
        FAST_WEB_SERVER_ADDRS=199.170.183.28,199.170.183.71
    Stdout, stderr are closed
    NULL data packet indicates end of stream
 */

/*********************************** Includes *********************************/

#define ME_COM_FAST 1

#include    "appweb.h"

#if ME_COM_FAST && ME_UNIX_LIKE
/************************************ Locals ***********************************/

#define FAST_VERSION            1

#define FAST_BEGIN_REQUEST      1
#define FAST_ABORT_REQUEST      2
#define FAST_END_REQUEST        3
#define FAST_PARAMS             4
#define FAST_STDIN              5       //  Post body data
#define FAST_STDOUT             6       //  Response body
#define FAST_STDERR             7       //  FCGI app errors
#define FAST_DATA               8       //  MOB ??
#define FAST_GET_VALUES         9
#define FAST_GET_VALUES_RESULT  10
#define FAST_UNKNOWN_TYPE       11      //   Unknown management request
#define FAST_MAX                11

#define FAST_RESPONDER          1
#define FAST_AUTHORIZER         2       //  Not supported
#define FAST_FILTER             3       //  Not supported

#define FAST_KEEP_CONN          1       //  MOB??
#define FAST_HEADER_LEN         8       //  MOB??

#define FCGI_REQUEST_COMPLETE   0
#define FCGI_CANT_MPX_CONN      1
#define FCGI_OVERLOADED         2
#define FCGI_UNKNOWN_ROLE       3

#define FAST_MULTIPLEX          0       //  This code does not yet support multiplexing requests

/*
    FAST_MAX_CONNS  The maximum number of concurrent transport connections this application will accept
    FAST_MAX_REQS   The maximum number of concurrent requests this application will accept
    FAST_MPX_CONNS  Does the application support multiplexed connections
 */

typedef struct Fast {
    cchar           *endpoint;
    int             minProxies;
    int             maxProxies;
    int             maxRequests;
    MprTicks        proxyTimeout;
    MprList         *proxies;
    MprList         *idleProxies;
    MprMutex        *mutex;
    MprCond         *cond;
    cchar           *command;
    cchar           *ip;
    int             port;
} Fast;

typedef struct FastProxy {
    Fast            *fast;
    HttpStream      *stream;
    MprList         *streams;
    ssize           requestId;
    int             requestID;
    int             pid;
    struct FastConnector
                    *connector;
} FastProxy;

typedef struct FastConnector {
    MprSocket       *socket;
    MprSocket       *listen;
    MprDispatcher   *dispatcher;
    MprMutex        *mutex;
    HttpQueue       *writeq;                /**< Queue to write to the FastCGI app */
    HttpQueue       *readq;                 /**< Queue to hold read data from the FastCGI app */
    FastProxy       *proxy;
    bool            writeBlocked;
    int             eof;
} FastConnector;

#if 0
//  MOB - align on 8 byte boundaries

typedef struct FastRec {
    uchar version;                          /* 1 */
    uchar type;
    uchar requestIdB1;                      /* Request ID */
    uchar requestIdB0;
    uchar contentLengthB1;
    uchar contentLengthB0;
    uchar paddingLength;
    uchar reserved;
    uchar contentData[contentLength];
    uchar paddingData[paddingLength];
} FastRec;

typedef struct FastRec {
    uchar   version;                          /* 1 */
    uchar   type;
    ushort  requestIdB1;                      /* Request ID */
    ushort  contentLengthB1;
    uchar   paddingLength;
    uchar   reserved;
    uchar   contentData[contentLength];
    uchar   paddingData[paddingLength];
} FastRec;

typedef struct FastRequestStart {
    uchar roleB1;
    uchar roleB0;
    uchar flags;
    uchar reserved[5];
} FastRequestStart;

typedef struct FastRequestEnd {
    uchar appStatusB3;
    uchar appStatusB2;
    uchar appStatusB1;
    uchar appStatusB0;
    uchar protocolStatus;
    uchar reserved[3];
} FastRequestEnd;
#endif

/*********************************** Forwards *********************************/

static void addFastPacket(HttpQueue *q, HttpPacket *packet);
static void addToFastVector(HttpQueue *q, char *ptr, ssize bytes);
static void adjustNetVec(HttpQueue *q, ssize written);
static Fast *allocFast();
static FastConnector *allocFastConnector(FastProxy *proxy);
static FastProxy *allocFastProxy(Fast *fast, HttpStream *stream);
static MprOff buildFastVec(HttpQueue *q);
static void closeFast(HttpQueue *q);
static void destroyFastProxy(FastProxy *proxy);
static void enableFastConnectorEvents(FastConnector *connector);
static int fastAction(MaState *state, cchar *key, cchar *value);
static void fastConnectorIO(FastConnector *connector, MprEvent *event);
static void fastConnectorIncoming(HttpQueue *q, HttpPacket *packet);
static void fastConnectorIncomingService(HttpQueue *q);
static void fastConnectorOutgoing(HttpQueue *q, HttpPacket *packet);
static void fastConnectorOutgoingService(HttpQueue *q);
static void fastIncoming(HttpQueue *q, HttpPacket *packet);
static int fastListen(MaState *state, cchar *key, cchar *value);
static int fastMaxProcesses(MaState *state, cchar *key, cchar *value);
static int fastMinProcesses(MaState *state, cchar *key, cchar *value);
static int fastMaxRequests(MaState *state, cchar *key, cchar *value);
static void fastOutgoing(HttpQueue *q);
static int fastTimeout(MaState *state, cchar *key, cchar *value);
static void freeFastPackets(HttpQueue *q, ssize bytes);
static Fast *getFast(HttpRoute *route);
static char *getFastToken(MprBuf *buf, cchar *delim);
static FastProxy *getFastProxy(Fast *fast, HttpStream *stream);
static void manageFast(Fast *fast, int flags);
static void manageFastProxy(FastProxy *fastProxy, int flags);
static void manageFastConnector(FastConnector *fastConnector, int flags);
static int openFast(HttpQueue *q);
static bool parseFastHeaders(HttpQueue *q, HttpPacket *packet);
static HttpPacket *prepFastHeader(HttpQueue *q, int type, HttpPacket *packet);
static void prepFastPost(HttpQueue *q, HttpPacket *packet);
static void prepFastRequestStart(HttpQueue *q);
static void prepFastRequestEnd(HttpQueue *q, HttpPacket *packet);
static void prepFastRequestParams(HttpQueue *q);
static bool parseFastResponseLine(HttpQueue *q, HttpPacket *packet);
static void releaseFastProxy(Fast *fast, FastProxy *proxy);
static FastProxy *startFastProxy(Fast *fast, HttpStream *stream);

//  MOB - refactor args ... proxy vs q

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
    maAddDirective("FastAction", fastAction);
    maAddDirective("FastMaxProcesses", fastMaxProcesses);
    maAddDirective("FastMaxRequests", fastMaxRequests);
    maAddDirective("FastMinProcesses", fastMinProcesses);
    maAddDirective("FastTimeout", fastTimeout);
    maAddDirective("FastListen", fastListen);

    if ((handler = httpCreateHandler("fastandler", module)) == 0) {
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

    fast = getFast(stream->rx->route);

    if ((proxy = getFastProxy(fast, stream)) == 0) {
        httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot allocate Fast proxy process for route %s", stream->rx->route->pattern);
        return MPR_ERR_CANT_OPEN;
    }
    q->queueData = q->pair->queueData = proxy;
    proxy->stream = stream;

    prepFastRequestStart(q);
    prepFastRequestParams(q);
    return 0;
}


static void closeFast(HttpQueue *q)
{
    releaseFastProxy(q->stream->rx->route->eroute, q->queueData);
}


static Fast *getFast(HttpRoute *route)
{
    Fast        *fast;

    if ((fast = route->eroute) == 0) {
        mprGlobalLock();
        if ((fast = route->eroute) == 0) {
            fast = allocFast();
        }
        mprGlobalUnlock();
    }
    return fast;
}


static FastProxy *getFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;

    lock(fast);
    proxy = NULL;
    //  MOB - need timeout

    /*
        MOB - this does not support multiplexing yet
     */
    while (!proxy) {
        if (mprGetListLength(fast->idleProxies) >= 0) {
            proxy = mprGetFirstItem(fast->idleProxies);
            mprAddItem(fast->proxies, proxy);
            break;

        } else if (mprGetListLength(fast->idleProxies) < fast->maxProxies) {
            proxy = startFastProxy(fast, stream);
            mprAddItem(fast->proxies, proxy);
            break;

        } else {
            unlock(fast);
            //  MOB - timeout ahd
            if (mprWaitForCond(fast->cond, 60 * 1000) < 0) {
                return NULL;
            }
            lock(fast);
        }
    }
    unlock(fast);
    proxy->requestId++;
    return proxy;
}


static void releaseFastProxy(Fast *fast, FastProxy *proxy)
{
    lock(fast);
    if (mprRemoveItem(fast->proxies, proxy) < 0) {
        mprLog("fast error", 0, "Cannot find proxy in list");
    }
    if (proxy->connector->eof) {
        destroyFastProxy(proxy);

    } else if (mprGetListLength(fast->proxies) + mprGetListLength(fast->idleProxies) >= fast->minProxies) {
        destroyFastProxy(proxy);

    } else {
        mprAddItem(fast->idleProxies, proxy);
    }
    unlock(fast);
}


static FastProxy *startFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;
    HttpRoute   *route;
    cchar       *path;
    cchar       **argv;
    int         argc, i;

    route = stream->rx->route;
    path = mprGetMimeProgram(route->mimeTypes, stream->tx->ext);

    proxy = allocFastProxy(fast, stream);

    if ((argc = mprMakeArgv(fast->command, &argv, 0)) < 0 || argv == 0) {
        httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot make Fast proxy command: %s", fast->command);
        return NULL;
    }
    //  MOB - add trace

    if ((proxy->pid = fork()) < 0) {
        fprintf(stderr, "Fork failed for FastCGI");
        return NULL;

    } else if (proxy->pid == 0) {
        /* Child */
        for (i = 0; i < 128; i++) {
            close(i);
        }
        dup2(proxy->connector->socket->fd, 0);
        if (execve(fast->command, (char**) argv, NULL /* (char**) &env->items[0] */) < 0) {
            httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot exec fast proxy: %s", fast->command);
        }
        return NULL;

    } else {
        //  MOB- dynamic port
        //  MOB - how do we know when they have accepted the socket?
        //  MOB - retry
        if (mprConnectSocket(proxy->connector->socket, fast->ip, fast->port, 0) < 0) {
            httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "Cannot connect to fast proxy: %d", errno);
            return NULL;
        }
    }
    return proxy;
}


static void destroyFastProxy(FastProxy *proxy)
{
    int     status;

    if (proxy->pid == 0) {
        return;
    }
    while (waitpid(proxy->pid, &status, 0) != proxy->pid) {
        if (errno == EINTR) {
            mprSleep(100);
            continue;
        }
        mprLog("fast error", 0, "Cannot wait for fastCGI app.");
    }
    if (WEXITSTATUS(status) != 0) {
        mprLog("fast error", 0, "FastCGI app with bad exit status.");
    }
    proxy->pid = 0;
}


static Fast *allocFast()
{
    Fast    *fast;

    fast = mprAllocObj(Fast, manageFast);
    fast->proxies = mprCreateList(0, 0);
    fast->idleProxies = mprCreateList(0, 0);
    fast->mutex = mprCreateLock();
    fast->cond = mprCreateCond();
    fast->maxRequests = -1;
    fast->minProxies = 1;
    fast->maxProxies = 1;
    return fast;
}


static FastProxy *allocFastProxy(Fast *fast, HttpStream *stream)
{
    FastProxy   *proxy;

    proxy = mprAllocObj(FastProxy, manageFastProxy);
    proxy->fast = fast;
    proxy->requestId = 0;
    proxy->connector = allocFastConnector(proxy);
    return proxy;
}


/*
    THREAD - currently don't support multiplexing so there is one connector per proxy
 */
static FastConnector *allocFastConnector(FastProxy *proxy)
{
    Http            *http;
    Fast            *fast;
    FastConnector   *connector;

    /*
        Setup the proxy connector. This may run on a different thread.
     */
    http = proxy->stream->http;
    fast = proxy->fast;

    connector = mprAllocObj(FastConnector, manageFastConnector);
    connector->proxy = proxy;
    proxy->connector = connector;
    proxy->mutex = mprCreateLock();

    connector->readq = httpCreateQueue(NULL, NULL, http->fastConnector, HTTP_QUEUE_RX, 0);
    connector->readq->pair = connector->writeq;
    connector->writeq->pair = connector->readq;
    connector->readq->queueData = proxy;
    connector->writeq = httpCreateQueue(NULL, NULL, http->fastConnector, HTTP_QUEUE_TX, 0);
    connector->writeq->queueData = proxy;

    /*
        Create listening socket for the fastCGI app to accept -- passed to app via fork
     */
    connector->listen = mprCreateSocket();
    connector->socket = mprCreateSocket();

    connector->dispatcher = FAST_MULTIPLEX ? mprCreateDispatcher("fast", 0) : proxy->stream->net->dispatcher;

    //  MOB - need to get the real port number allocated here
    if (mprListenOnSocket(connector->listen, fast->ip, fast->port, MPR_SOCKET_NODELAY) == SOCKET_ERROR) {
        if (mprGetError() == EADDRINUSE) {
            mprLog("error http", 0, "Cannot open a socket on %s:%d, socket already bound.",
                fast->ip ? fast->ip : "*", fast->port);
        } else {
            mprLog("error http", 0, "Cannot open a socket on %s:%d", fast->ip ? fast->ip : "*", fast->port);
        }
        return NULL;
    }
    return connector;
}


static void manageFast(Fast *fast, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(fast->endpoint);
        mprMark(fast->proxies);
        mprMark(fast->idleProxies);
        mprMark(fast->ip);
        mprMark(fast->command);
        mprMark(fast->mutex);
        mprMark(fast->cond);
    }
}


static void manageFastProxy(FastProxy *fastProxy, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(fastProxy->fast);
        mprMark(fast->stream);
    }
}


static void manageFastConnector(FastConnector *connector, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(connector->dispatcher);
        mprMark(connector->socket);
        mprMark(connector->listen);
        mprMark(connector->writeq);
        mprMark(connector->readq);
        mprMark(connector->proxy);
        mprMark(connector->mutex);
    }
}

/*
    Handler incoming route.
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
        if (stream->rx->remainingContent > 0) {
            /* Short incoming body data. Must kill the proxy -- MOB or can we send a kill request ? */
            packet = prepFastHeader(q, FAST_ABORT_REQUEST, httpCreateDataPacket(0));
            // proxy->connector->eof = 1;
            httpError(stream, HTTP_CODE_BAD_REQUEST, "Client supplied insufficient body data");
        } else {
            prepFastRequestEnd(q, packet);
        }
    } else {
        prepFastPost(q, packet);
    }
    putToConnectorWriteq(proxy, packet);
}


static void putToConnectorWriteq(FastConnector *connector, HttpPacket *packet)
{
    lock(connector);
    httpPutForService(connector->writeq, packet, HTTP_SCHEDULE_QUEUE);
    unlock(connector);
}


#if UNUSED
static HttpPacket *putToConnectorReadq(FastConnector *connector, HttpPacket *packet)
{
    lock(connector);
    httpPutForService(connector->readq, packet, HTTP_SCHEDULE_QUEUE);
    unlock(connector);
}


static HttpPacket *readFromConnectorWriteq(FastConnector *connector, HttpPacket *packet)
{
    lock(connector);
    httpPutForService(connector->writeq, packet, HTTP_SCHEDULE_QUEUE);
    unlock(connector);
}


static void readFromConnectorReadq(FastConnector *connector)
{
    lock(connector);
    packet = httpPutForService(connector->readq);
    unlock(connector);
    return packet;
}
#endif


static void fastOutgoing(HttpQueue *q)
{
    FastProxy       *proxy;
    FastConnector   *connector;

    if ((proxy = q->queueData) == 0) {
        return;
    }
    connector = proxy->connector;

    /*
        This will copy outgoing packets downstream toward the network stream and on to the browser.
     */
    httpDefaultOutgoingServiceStage(q);
    if (q->count < q->low) {
        lock(connector);
        httpResumeQueue(connector->writeq);
        unlock(connector);

    } else if (q->count > q->max && connector->writeBlocked) {
        httpSuspendQueue(q->stream->writeq);
    }
}


static int fastAction(MaState *state, cchar *key, cchar *value)
{
    char    *mimeType, *program;

    if (!maTokenize(state, value, "%S %S", &mimeType, &program)) {
        return MPR_ERR_BAD_SYNTAX;
    }
    mprSetMimeProgram(state->route->mimeTypes, mimeType, program);
    return 0;
}


static int fastListen(MaState *state, cchar *key, cchar *value)
{
    Fast    *fast;

    fast = getFast(state->route);
    fast->endpoint = sclone(value);

    if (mprParseSocketAddress(fast->endpoint, &fast->ip, &fast->port, NULL, 9128) < 0) {
        mprLog("fast error", 0, "Cannot bind FastCGI proxy address: %s", fast->endpoint);
        return MPR_ERR_BAD_SYNTAX;
    }
    return 0;
}


static int fastMaxProcesses(MaState *state, cchar *key, cchar *value)
{
    Fast    *fast;

    //  MOB - should these be in limits so they can be inherited?
    fast = getFast(state->route);
    fast->maxProxies = httpGetInt(value);
    return 0;
}


static int fastMinProcesses(MaState *state, cchar *key, cchar *value)
{
    Fast    *fast;

    fast = getFast(state->route);
    fast->minProxies = httpGetInt(value);
    return 0;
}


static int fastMaxRequests(MaState *state, cchar *key, cchar *value)
{
    Fast    *fast;

    fast = getFast(state->route);
    fast->maxRequests = httpGetInt(value);
    return 0;
}


static int fastTimeout(MaState *state, cchar *key, cchar *value)
{
    Fast    *fast;

    fast = getFast(state->route);
    fast->proxyTimeout = httpGetTicks(value);
    return 0;
}


static HttpPacket *prepFastHeader(HttpQueue *q, int type, HttpPacket *packet)
{
    FastProxy   *proxy;
    uchar       *buf;
    ssize       len, pad;

    proxy = q->queueData;
    if (!packet) {
        packet = httpCreateDataPacket(0);
    }
    packet->prefix = mprCreateBuf(16, 16);

    buf = (uchar*) packet->prefix->start;
    *buf++ = FAST_VERSION;
    *buf++ = type;
    //  MOB - must limit requestID to ushort
    *buf++ = (uchar) (proxy->requestId >> 8);
    *buf++ = (uchar) (proxy->requestId & 0xFF);

    len = httpGetPacketLength(packet);
    *buf++ = (uchar) (len >> 8);
    *buf++ = (uchar) (len & 0xFF);

    pad = (len % 8) ? (8 - (len % 8)) : 0;
    *buf++ = pad;
    *buf++ = 0;
    mprAdjustBufEnd(packet->content, 8 + pad);
    return packet;
}


/*
    typedef struct {
        unsigned char roleB1;
        unsigned char roleB0;
        unsigned char flags;
        unsigned char reserved[5];
    } FCGI_RequestStart;
 */
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
    //  MOB - what is flags?
    *buf++ = 0;     //  MOB flags;
    //  MOB ??
    buf += 5;
    mprAdjustBufEnd(packet->content, 8);
    (void) prepFastHeader(q, FAST_BEGIN_REQUEST, packet);
    putToConnectorWriteq(proxy->connector, packet);
}


static void prepFastRequestEnd(HttpQueue *q, HttpPacket *packet)
{
    FastProxy   *proxy;

    proxy = q->queueData;
    (void) prepFastHeader(q, FAST_END_REQUEST, packet);
    putToConnectorWriteq(proxy->connector, packet);
}


static void prepFastRequestParams(HttpQueue *q)
{
    HttpPacket  *packet;
    FastProxy   *proxy;

    proxy = q->queueData;

    //  MOB - how to encode params
    //  MOB - what params
    packet = prepFastHeader(q, FAST_PARAMS, 0);
    putToConnectorWriteq(proxy->connector, packet);
}


static void prepFastPost(HttpQueue *q, HttpPacket *packet)
{
    (void) prepFastHeader(q, FAST_STDIN, packet);
}


static void fastConnectorIncoming(HttpQueue *q, HttpPacket *packet)
{
    FastProxy   *proxy;

    proxy = q->queueData;
    putToConnectorWriteq(proxy->connector, packet);
}


static void fastConnectorIncomingService(HttpQueue *q)
{
    FastProxy   *proxy;
    HttpPacket  *packet;
    int         type;

    proxy = q->queueData;

    while ((packet = httpGetPacket(q)) != 0) {
        type = mprGetCharFromBuf(packet->content);
        requestId = mprGetCharFromBuf(packet->content);

        /* KEEP
            stream = mprGetItemAt(proxy->streams, requestId);
            MOB - locking
            mprCreateEvent
         */
        if (type == FAST_STDOUT) {
            packet->stream = stream;
            packet->data = proxy;
            mprCreateEvent(stream->dispatcher, "fast", 0, xxxx, packet, 0);
        }
    }
}


static void fastHandlerResponse(HttpPacket *packet)
{
    HttpStream  *stream;
    HttpRx      *rx;

    stream = packet->stream;
    proxy =
    rx = stream->rx;

    if (!rx->seenFastHeaders) {
        if ((len = getHeadersLength(packet)) == 0) {
            return;
        }
        if (!parseFastHeaders(q, packet, len)) {
            return;
        }
        proxy->seenFastHeaders = 1;
    }
        if (httpGetPacketLength(packet) > 0) {
            httpPutPacketToNext(q, packet);
            }
        }
    }
}


/*
    Handle IO on the network. Initially the dispatcher will be set to the server->dispatcher and the first
    I/O event will be handled on the server thread (or main thread). A request handler may create a new
    net->dispatcher and transfer execution to a worker thread if required.
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
    if (!connector->eof) {
        enableFastConnectorEvents(connector);
    }
}


static void enableFastConnectorEvents(FastConnector *connector)
{
    MprSocket   *sp;
    int         eventMask;

    sp = connector->socket;

    if (!connector->eof) {
        eventMask = 0;
        if (connector->writeq->count > 0) {
            eventMask |= MPR_WRITABLE;
        }
        //  MOB - can't do this?
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


static void fastConnectorOutgoing(HttpQueue *q, HttpPacket *packet)
{
    httpPutForService(q, packet, HTTP_SCHEDULE_QUEUE);
}


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
            mprLog("fast error", 6, "fastConnector: Cannot write. errno %d", errCode);
            connector->eof = 1;
            break;

        } else if (written > 0) {
            freeFastPackets(q, written);
            adjustNetVec(q, written);

        } else {
            /* Socket full */
            break;
        }
    }
    if ((q->first || q->ioIndex) && connector->writeBlocked) {
        enableFastConnectorEvents(connector);
    }
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
    HttpStream  *stream;
    ssize       len;

    assert(q->count >= 0);
    assert(bytes >= 0);

    while ((packet = q->first) != 0) {
        if (packet->flags & HTTP_PACKET_END) {
            if ((stream = packet->stream) != 0) {
                httpFinalizeConnector(stream);
                httpProcess(stream->inputq);
            }
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


static int getHeadersLength(HttpPacket *packet)
{
    char    *endHeaders, *headers;
    int     len;

    buf = packet->content;
    headers = mprGetBufStart(buf);
    len = 0;

    if ((endHeaders = sncontains(headers, "\r\n\r\n", blen)) == NULL) {
        if ((endHeaders = sncontains(headers, "\n\n", blen)) == NULL) {
            if (slen(headers) < ME_MAX_HEADERS) {
                /* Not EOF and less than max headers and have not yet seen an end of headers delimiter */
                return 0;
            }
        }
        len = 2;
    } else {
        len = 4;
    }
    return len;
}


/*
    Parse the CGI output headers. Sample CGI program output:
        Content-type: text/html
        <html.....
 */
static bool parseFastHeaders(HttpQueue *q, HttpPacket *packet, int len)
{
    FastProxy   *proxy;
    HttpStream  *stream;
    MprBuf      *buf;
    char        *endHeaders, *headers, *key, *value;
    ssize       blen;

    proxy = q->queueData;
    stream = proxy->stream;
    buf = packet->content;
    headers = mprGetBufStart(buf);
    blen = mprGetBufLength(buf);
    value = 0;

    /*
        Split the headers from the body. Add null to ensure we can search for line terminators.
     */
    endHeaders = &buf[len];
    if (endHeaders > buf->end) {
        assert(endHeaders <= buf->end);
        return 0;
    }
    endHeaders[len - 1] = '\0';
    endHeaders += len;

    /*
        Want to be tolerant of CGI programs that omit the status line.
     */
    if (strncmp((char*) buf->start, "HTTP/1.", 7) == 0) {
        if (!parseFastResponseLine(q, packet)) {
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
            if (scaselesscmp(key, "location") == 0) {
                //  MOB - THREAD
                httpRedirect(proxy->stream, stream->tx->status, value);

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
static bool parseFastResponseLine(HttpQueue *q, HttpPacket *packet)
{
    MprBuf      *buf;
    char        *protocol, *status, *msg;

    buf = packet->content;
    protocol = getFastToken(buf, " ");
    if (protocol == 0 || protocol[0] == '\0') {
        httpError(q->stream, HTTP_CODE_BAD_GATEWAY, "Bad CGI HTTP protocol response");
        return 0;
    }
    if (strncmp(protocol, "HTTP/1.", 7) != 0) {
        httpError(q->stream, HTTP_CODE_BAD_GATEWAY, "Unsupported CGI protocol");
        return 0;
    }
    status = getFastToken(buf, " ");
    if (status == 0 || *status == '\0') {
        httpError(q->stream, HTTP_CODE_BAD_GATEWAY, "Bad CGI header response");
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

#endif /* ME_COM_FAST */

/*
    Copyright (c) Embedthis Software. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
