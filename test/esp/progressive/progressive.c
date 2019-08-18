/*
    progressive.c - Emit progressive output 
 */
#include "appweb.h"
#include "esp.h"

static void callback(HttpConn *conn, MprEvent *event);
static void serviceRequest();
static void secondary(HttpConn *conn, void *data);


ESP_EXPORT int esp_controller_app_progressive(HttpRoute *route)
{
    espDefineAction(route, "request", serviceRequest);
    return 0;
}


static void serviceRequest(HttpConn *conn)
{
    httpWrite(conn->writeq, "starting\n");

    print("START");
    if (mprCreateTimerEvent(conn->dispatcher, "progrssive", 50, (MprEventProc) callback, conn, 0) < 0) {
        ;
    }
}


static void callback(HttpConn *conn, MprEvent *event)
{
    if (conn->error) {
        // print("DISCONNECT");
        mprRemoveEvent(event);
        httpError(conn, HTTP_CODE_COMMS_ERROR, "Disconnected");
    } else {
        // print("Call httpCreate Event %ld", conn->seqno);
        if (httpCreateEvent(conn->seqno, secondary, NULL) < 0) {
            mprRemoveEvent(event);
        }
    }
}

static void secondary(HttpConn *conn, void *data)
{
    static int count = 0;

    // print("IN SECONDARY");
    // print("WRITE %d", count);
    if (conn) {
        httpWrite(conn->writeq, "%s", mprGetDate(NULL));
        httpFlushQueue(conn->writeq, 0);
        if ((count++ % 500) == 0) {
            mprPrintMem(sfmt("Memory at %s", mprGetDate(NULL)), 0);
        }
    }
}
