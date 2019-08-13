/*
    message.c - Sample to demonstrate how to communicate from a non-Appweb thread into Appweb.

    Appweb APIs are in general not thread-safe for performance. So when sending messages from
    non-Appweb threads into Appweb, we need to send the message via httpCreateEvent sends a
    message to the request event dispatcher in a thread-safe manner.

    ESP is used to load this test module and it not required to use this design pattern.
 */
#include "esp.h"

static void finalizeResponse(HttpConn *conn, void *message);
static void serviceRequest();
static void foreignThread(uint64 seqno);


/*
    Create a URL action to respond to HTTP requests.
    We use an ESP module just to make it easier to dynamically load this test module.
 */
ESP_EXPORT int esp_controller_app_message(HttpRoute *route)
{
    espDefineAction(route, "request", serviceRequest);
    return 0;
}


/*
    Start servicing a HTTP request
 */
static void serviceRequest(HttpConn *conn)
{
    uint64      seqno;

    /*
        Create an O/S (non-mpr) thread and pass the current HttpConn sequence number.
     */
    seqno = conn->seqno;
    mprStartOsThread("foreign", foreignThread, LTOP(conn->seqno), NULL);
}


static void foreignThread(uint64 connSeqno)
{
    char    *message;

    assert(mprGetCurrentThread() == NULL);

    /*
        Invoke the finalizeResponse callback on the Stream identified by the sequence number and pass in a message to write.
     */
    message = strdup("Hello World");
    if (httpCreateEvent(connSeqno, finalizeResponse, message) < 0) {
        /* Stream destroyed and create event failed */
        free(message);
    }
}


/*
    Finalize a response to the Http request. This runs on the conn's dispatcher, thread-safe inside Appweb.
    If the conn has already been destroyed, conn will be NULL.
 */
static void finalizeResponse(HttpConn *conn, void *message)
{
    if (conn) {
        httpWrite(conn->writeq, "message: %s\n", message);
        httpFinalize(conn);
        httpProtocol(conn);
    }
    /*
        Free the "hello World" memory allocated via strdup in foreignThread
     */
    free(message);
}
