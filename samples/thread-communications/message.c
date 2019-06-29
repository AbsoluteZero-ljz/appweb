/*
    message.c - Sample to demonstrate how to communicate from a non-Appweb thread into Appweb.

    Appweb APIs are in general not thread-safe for performance. So when sending messages from 
    non-Appweb threads into Appweb, we need to send the message via httpInvoke which calls the 
    mprCreateEvent API which is thread-safe.

    We use ESP for this sample, just because it makes creating and loading samples easy.
    You don't have to use ESP to communicate from your threads to Appweb.
 */
#include "esp.h"

static void finalizeResponse(HttpStream *stream, void *message);
static void response();
static void simulateForeignThread();
static void testMain(HttpStream *stream);

/*
    Initialize this sample module module when loaded by Appweb.
 */
ESP_EXPORT int esp_controller_app_message(HttpRoute *route)
{
    /*
        Create a URL action to respond to HTTP requests.
     */
    espDefineAction(route, "response", response);
    return 0;
}


static void threadMain(HttpStream *stream)
{
    char    *message;

    simulateForeignThread();

    /*
        From here on, do not use any Appweb or MPR APIs here except for httpInvoke()
     */
    message = strdup("Hello World");

    /*
        Call back into Appweb. This will safely create an event on the stream dispatcher that will be run on an
        Appweb/MPR thread. The message passed is owned by this thread and should be freed in the finalizeResponse.
     */
    httpInvoke(stream, finalizeResponse, message);
}


/*
    Service a request and generate a response
 */
static void response(HttpStream *stream)
{
    /*
        Hold the stream so it won't be garbage collected if the client disconnects. We release in finalizeResponse()
     */
    mprAddRoot(stream);

    /*
        Simulate a foreign thread by creating an MPR thread that will respond to the request.
        We pass the current HttpStream object returned from getStream().
     */
    mprStartThread(mprCreateThread("test", threadMain, stream, 0));

    /*
        For ESP, don't auto finalize this request. Wait for finalizeResponse to actually finalize.
     */
    dontAutoFinalize();
}


/*
    Finalize the response. Invoked indirectly from the foreign thread via httpInvoke.
 */
static void finalizeResponse(HttpStream *stream, void *message)
{
    /*
        For ESP only, re-establish the current stream for this thread
     */
    setStream(stream);
    
    mprLog("info", 0, "Writing message \"%s\"", message);
    httpWrite(stream->writeq, "message %s\n", message);

    /*
        When complete, call finalize (httpFinalizeOutput) and the invoke the HTTP protocol engine
     */
    finalize();
    httpProcess(stream->inputq);

    /* Free the message and invoke structure allocated in the foreign thread */
    free(message);

    /* Release the hold on the stream */
    mprRemoveRoot(stream);

}


/*
    Simulate a foreign thread by yielding to the garbage collector permanently.
    This simulates a foreign thread for the MPR.  Don't do this when running in a real foreign thread.
    Only required for this sample and not when you have a real foreign thread.
 */
static void simulateForeignThread()
{
    mprYield(MPR_YIELD_STICKY);
    /*
        This thread now behaves just like a foreign thread would. Other Appweb threads are running and we must be careful
        when interacting with Appweb APIs, memory and objects.
     */
}