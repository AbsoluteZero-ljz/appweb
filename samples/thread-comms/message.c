/*
    message.c - Sample to demonstrate how to communicate from a non-Appweb thread into Appweb.

    Appweb APIs are in general not thread-safe for performance. So when sending messages from
    non-Appweb threads into Appweb, we need to send the message via httpCreateEvent sends a
    message to the request event dispatcher in a thread-safe manner.

    ESP is used to load this test module and it not required to use this design pattern.
 */
#include "appweb.h"
#include "esp.h"

static void finalizeResponse(HttpStream *stream, void *message);
static void response();
static void threadMain(HttpStream *stream, MprThread *tp);

/*
    We use an ESP module just to make it easier to dynamically load this test module.
 */
ESP_EXPORT int esp_controller_app_message(HttpRoute *route)
{
    /*
        Create a URL action to respond to HTTP requests.
     */
    espDefineAction(route, "response", response);
    mprLog("info", 0, "Loaded");
    return 0;
}

/*
    Start servicing a HTTP request
 */
static void response(HttpStream *stream)
{
    /*
        Simulate a foreign thread by creating an MPR thread that will respond to the request.
        We pass the current HttpStream sequence number which will be storead in MprThread.data.
     */
    mprStartThread(mprCreateThread("test", threadMain, LTOP(stream->seqno), 0));

    /* Tell ESP to not finalize the request here. Only required for ESP */
    espSetAutoFinalizing(stream, 0);
}

static void threadMain(HttpStream *stream, MprThread *tp)
{
    int     streamSeqno = PTOL(tp->data);

    /*
        Convert this appweb thread to simulate a foreign thread by yielding to the garbage collector permanently.
        This thread now behaves just like a foreign thread would. Only required for this sample and not when you
        have a real foreign thread. From here on, do not use any Appweb or MPR APIs here except for httpCreateEvent() and
        palloc/pfree.
    */
    mprYield(MPR_YIELD_STICKY);

    /*
        Invoke the finalizeResponse callback on the Stream event dispatcher and pass in an allocated string to write.
        The first argument is the stream sequence number captured earlier and passed into this thread.
     */
    httpCreateEvent(streamSeqno, finalizeResponse, strdup("Hello World"));
}

/*
    Finalize the response. Invoked indirectly from the foreign thread via httpInvoke.
 */
static void finalizeResponse(HttpStream *stream, void *message)
{
    mprLog("info", 0, "Writing message \"%s\"", message);
    httpWrite(stream->writeq, "message %s\n", message);

    httpFinalize(stream);
    httpProcess(stream->inputq);

    free(message);
}
