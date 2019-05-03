/*
    client.c - Http Client using non-blocking I/O.

    This sample demonstrates retrieving content using HTTP GET|POST method with non-blocking I/O.
    User threads will not be created, but the MPR will utilize worker threads for the callback.

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */
/***************************** Includes *******************************/

#include    "appweb.h"

#define MAX_REQUESTS    1          /* Number of test requests to issue (in parallel) */

/*
    Please change this test URL to a local url
 */
#define TEST_URL        "http://example.com/index.html"
#define TEST_METHOD     "GET"
#define TEST_DATA       ""

/*
    Number of requests outstanding
 */
static int outstanding = 0;

typedef struct Context {
    cchar   *buf;               /* Post data to write */
    ssize   len;                /* Length of post data */
    ssize   written;            /* Length of data written so far */
} Context;

/******************************* Forwards *****************************/

static void callback(HttpConn *conn, int event, int arg);
static int startRequest(HttpConn *conn, cchar *method, cchar *uri, cchar *data);
static void manageContext(Context *context, int flags);
static int setupRequest(MprDispatcher *dispatcher, cchar *method, cchar *uri, cchar *data);

/********************************* Code *******************************/

MAIN(simpleClient, int argc, char **argv, char **envp)
{
    MprDispatcher   *dispatcher;
    int             i;

    /*
       Create the Multithreaded Portable Runtime and start it.
       Initialize the HTTP subsystem for client requests
     */
    mprCreate(argc, argv, MPR_USER_EVENTS_THREAD);
    httpCreate(HTTP_CLIENT_SIDE);

    /*
        Enable if you would like logging and request tracing

        mprStartLogging("stdout:6", 0);
        httpStartTracing("stdout:6");
     */

    mprStart();

    /*
        Create a single event dispatcher and start it.
        All HTTP requests will be processed non-blocking on this event dispatcher.
        Note: requests will run in parallel using different network connections.
     */
    dispatcher = mprCreateDispatcher("httpRequest", 0);

    for (i = 0; i < MAX_REQUESTS; i++) {
        if (setupRequest(dispatcher, TEST_METHOD, TEST_URL, TEST_DATA) < 0) {
            mprError("Cannot start request");
            break;
        }
    }

    /*
        Service requests until instructed to exit by the call to mprShutdown when all requests are done
     */
    mprServiceEvents(-1, 0);

    mprDestroyDispatcher(dispatcher);
    mprDestroy();
    return 0;
}


/*
    Setup a HTTP request. This will create a network object and conn. Events will be serialized on the dispatcher.
    But requests will be run in parallel without blocking.
 */
static int setupRequest(MprDispatcher *dispatcher, cchar *method, cchar *uri, cchar *data)
{
    HttpConn    *conn;

    /*
        This sample creates a new network connection for each request. NOTE: HTTP/2 can issue
        multiple overlapping requests on multiple straems using a single HTTP connection (net).
        However, here we could create multiple network connections so it will work over HTTP/1.
     */
    if ((conn = httpCreateConn(NULL, dispatcher)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    /*
        Disable timeouts incase you are debugging. Otherwise, you should utilize the standard timeouts
     */
    httpSetTimeout(conn, 0, 0);
    httpSetAsync(conn, 1);
    
    /*
        Setup the callback to be notified on readable, writable and state change events.
        We write the post data, if any, in the callback. If no post data, the request will be finalized when the
        first WRITABLE event is triggered.
     */
    httpSetConnNotifier(conn, callback);

    if (startRequest(conn, method, uri, data) < 0) {
        httpDestroyConn(conn);
        return MPR_ERR_CANT_WRITE;
    }
    HTTP_NOTIFY(conn, HTTP_EVENT_WRITABLE, 0);
    httpServiceQueues(conn, 0);
    httpEnableConnEvents(conn);
    return 0;
}


/*
    Issue a client http request. This will not block or wait for the request to complete.
 */
static int startRequest(HttpConn *conn, cchar *method, cchar *uri, cchar *postData)
{
    Context *context;

    /*
        Pass some context information into the callback. You may have more context information. Add to the Context
        structure. If memory is allocated by the MPR, ensure it is marked in manageContext.
        Read: https://www.embedthis.com/appweb/doc/ref/memory.html for more details.
     */
    context = conn->data = mprAllocObj(Context, manageContext);
    if (context) {
        context->buf = postData;
        context->len = slen(postData);
        context->written = 0;
    }
    outstanding++;

    /*
        Open a connection to issue the request. This will not block. It won't even wait for the connection to be
        established to the server.
     */
    if (httpConnect(conn, method, uri, NULL) < 0) {
        mprError("Cannot connect to %s", uri);
        return MPR_ERR_CANT_CONNECT;
    }
    return 0;
}


/*
    HTTP event callback notifier. The callback notifier is invoked for READABLE, WRITABLE and
    state change events. This routine must never block.
 */
static void callback(HttpConn *conn, int event, int arg)
{
    HttpPacket  *packet;
    Context     *context;
    ssize       len;
    int         status;

    context = conn->data;

    if (event == HTTP_EVENT_STATE) {
        /*
            The request state has changed. Only interested in the "parsed" and "complete" states.
         */
        if (conn->state == HTTP_STATE_PARSED) {
            status = httpGetStatus(conn);
            if (status != 200) {
                httpError(conn, 0, "Got bad status %d", status);
            }

        } else if (conn->state == HTTP_STATE_COMPLETE) {
            mprPrintf("Request complete\n");
            httpDestroyConn(conn);
            if (--outstanding <= 0) {
                mprShutdown(MPR_EXIT_NORMAL, 0, 0);
            }

        }

    } else if (event == HTTP_EVENT_READABLE) {
        /*
            The server has responded with some data. Just print it out.
         */
        packet = httpGetPacket(conn->readq);
        if (packet->flags & HTTP_PACKET_DATA) {
            print("Got data: %s", packet->content->start);
        }

    } else if (event == HTTP_EVENT_WRITABLE) {
        /*
            The server can accept some post data. Write it if we have some.
            Note: this will not block.
         */
        while (context->written < context->len) {
            len = httpWriteBlock(conn->writeq,
                &context->buf[context->written], context->len - context->written, HTTP_NON_BLOCK);
            if (len < 0) {
                httpError(conn, 0, "Cannot write request body postData");
            } else if (len == 0) {
                break;
            }
            context->written += len;
        }
        if (context->written >= context->len) {
            httpFinalizeOutput(conn);
        }

    } else if (event == HTTP_EVENT_ERROR) {
        mprError("conn encountered an error: %s", conn->errorMsg);

    } else if (event == HTTP_EVENT_DESTROY) {
        /* conn destroyed */
    }
}

/*
    Manage the request context. If you have other fields that are allocated by the MPR, then you
    must "mark" them here as being required.
 */
static void manageContext(Context *context, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        //  mprMark(context->field);
    }
}

/*
    Copyright (c) Embedthis Software. All Rights Reserved.
    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
