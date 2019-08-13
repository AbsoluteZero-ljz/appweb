/*
    event.c - Test mprCreateEvent from foreign threads
 */
#include "appweb.h"
#include "esp.h"

static void callback(char *message, MprEvent *event);
static void serviceRequest();
static void foreignThread();


ESP_EXPORT int esp_controller_app_event(HttpRoute *route)
{
    espDefineAction(route, "request", serviceRequest);
    return 0;
}


static void serviceRequest(HttpConn *conn)
{
    httpWrite(conn->writeq, "done\n");
    mprStartOsThread("foreign", foreignThread, NULL, NULL);
}


static void foreignThread()
{
    char    *message;

    assert(mprGetCurrentThread() == NULL);

    message = strdup("Hello World");
    if (mprCreateEvent(NULL, "foreign", 0, (MprEventProc) callback, message, MPR_EVENT_STATIC_DATA) < 0) {
        free(message);
    }
}


static void callback(char *message, MprEvent *event)
{
    assert(message && *message);

    if (event) {
        assert(event->proc);
        assert(event->timestamp);
        assert(event->data == message);
        assert(event->dispatcher);
        assert(event->sock == NULL);
        assert(event->proc == (MprEventProc) callback);
        assert(smatch(message, "Hello World"));
    }
    // printf("Got \"%s\" from event \"%s\"\n", message, event->name);
    free(message);
}
