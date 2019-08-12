/*
    event.c - Test httpCreateEvent
 */
#include "appweb.h"
#include "esp.h"

static void callback(char *message, MprEvent *event);
static void serviceRequest();
static void foreignThread(uint64 seqno);


/*
    Create a URL action to respond to HTTP requests.
    We use an ESP module just to make it easier to dynamically load this test module.
 */
ESP_EXPORT int esp_controller_app_event(HttpRoute *route)
{
    espDefineAction(route, "request", serviceRequest);
    return 0;
}


static void serviceRequest(HttpStream *stream)
{
    httpWrite(stream->writeq, "done\n");
    mprStartOsThread("foreign", foreignThread, strdup("hello world"), NULL);
}


static void foreignThread(uint64 seqno)
{
    assert(mprGetCurrentThread() == NULL);

    mprCreateEvent(NULL, "foreign", 0, (MprEventProc) callback, strdup("Hello World"), MPR_EVENT_STATIC_DATA);
}


/*
    Finalize a response to the Http request. This runs on the stream's dispatcher, thread-safe inside Appweb.
 */
static void callback(char *message, MprEvent *event)
{
    assert(message && *message);
    assert(event);
    assert(event->proc);
    assert(event->timestamp);
    assert(event->data == message);
    assert(event->dispatcher);
    assert(event->sock == NULL);
    assert(event->proc == (MprEventProc) callback);

    // printf("Got \"%s\" from event \"%s\"\n", message, event->name);
    free(message);
}
