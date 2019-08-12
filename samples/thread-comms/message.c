/*
    message.c - Sample to demonstrate how to communicate from a non-Appweb thread into Appweb.

    Appweb APIs are in general not thread-safe for performance. So when sending messages from
    non-Appweb threads into Appweb, we need to send the message via httpCreateEvent sends a
    message to the request event dispatcher in a thread-safe manner.

    ESP is used to load this test module and it not required to use this design pattern.
 */
#include "esp.h"

static void finalizeResponse(HttpStream *stream, void *message);
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
static void serviceRequest(HttpStream *stream)
{
    uint64      seqno;

    /*
        Create an O/S (non-mpr) thread and pass the current HttpStream sequence number.
     */
    seqno = stream->seqno;
    mprStartOsThread("foreign", foreignThread, LTOP(stream->seqno), NULL);
}


static void foreignThread(uint64 seqno)
{
    assert(mprGetCurrentThread() == NULL);

    /*
        Invoke the finalizeResponse callback on the Stream event dispatcher and pass in an allocated string to write.
        The first argument is the stream sequence number captured earlier and passed into this thread.
     */
    httpCreateEvent(seqno, finalizeResponse, strdup("Hello World"));
}


/*
    Finalize a response to the Http request. This runs on the stream's dispatcher, thread-safe inside Appweb.
 */
static void finalizeResponse(HttpStream *stream, void *message)
{
    httpWrite(stream->writeq, "message: %s\n", message);

    httpFinalize(stream);
    httpProcess(stream->inputq);

    /*
        Free the "hello World" memory allocated via strdup in foreignThread
     */
    free(message);
}
