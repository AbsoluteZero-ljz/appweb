/*
    foreign.c - Test httpCreateEvent from a foreign thread
 */
#include "esp.h"

static void finalizeResponse(HttpConn *conn, void *message);
static void serviceRequest();
static void foreignThread(uint64 seqno);

ESP_EXPORT int esp_controller_app_foreign(HttpRoute *route)
{
    espDefineAction(route, "request", serviceRequest);
    return 0;
}

static void serviceRequest(HttpConn *conn)
{
    uint64      seqno;

    seqno = conn->seqno;
    mprStartOsThread("foreign", foreignThread, LTOP(conn->seqno), NULL);
}


static void foreignThread(uint64 connSeqno)
{
    char    *message;

    assert(mprGetCurrentThread() == NULL);

    message = strdup("Hello World");
    if (httpCreateEvent(connSeqno, finalizeResponse, message) < 0) {
        free(message);
    }
}


static void finalizeResponse(HttpConn *conn, void *message)
{
    assert(message);
    if (conn) {
        httpWrite(conn->writeq, "message: %s\n", message);
        httpFinalize(conn);
        httpProtocol(conn);
    }
    free(message);
}
