/*
    message.c - Sample to demonstrate how to communicate from a non-Appweb thread into Appweb.

    Appweb APIs are in general not thread-safe. So when sending messages from non-Appweb threads into Appweb,
    we need to send the message via the mprCreateEvent API which is thread-safe.

    We use ESP just because it makes creating and loading samples easy. You don't have to use ESP to communicate
    from your threads to Appweb.
 */
#include "esp.h"

static void eventCallback(cchar *message)
{
    /*
        This code runs inside Appweb on an Appweb/MPR worker thread using the MPR event dispatcher.
        From here, you can safely use any Appweb/MPR API
    */
    mprLog("info", 0, "Message \"%s\"", message);
    free(message);
}


static void threadMain(void *data, MprThread *tp)
{
    char    *message;

    /*
        Simulate a foreign thread by yielding to the garbage collector permanently. This simulates a foreign thread for the MPR.
        Don't do this when running in a real foreign thread.
     */
    mprYield(MPR_YIELD_STICKY);

    /*
        This thread now behaves just like a foreign thread would. Other Appweb threads are running and we must be careful
        when interacting with Appweb APIs, memory and objects.

        We can send a message from a foreign thread to an Appweb thread by calling mprCreateEvent with the MPR_EVENT_FOREIGN flag.
        MPR_EVENT_STATIC_DATA means the message data is not allocated by the Appweb allocator via mprAlloc.
        The message data must continue to exist until the event has run.
     */
    message = strdup("Hello World");
    mprCreateEvent(NULL, "outside", 0, eventCallback, message, MPR_EVENT_FOREIGN | MPR_EVENT_STATIC_DATA);
    mprLog("info", 0, "After event has completed");
}

/*
    This action is simply so we can respond to the client HTTP test request that triggered the module.
 */
static void response() {
    render("done\n");
}

/*
    Initialize the "thread_communication" module
 */
ESP_EXPORT int esp_controller_app_message(HttpRoute *route)
{
    MprThread   *tp;

    /*
        Simulate a foreign thread by creating an MPR thread and then yield so GC can run without the
        consent of this thread.
     */
    tp = mprCreateThread("test", threadMain, NULL, 0);
    mprStartThread(tp);

    /*
        Just for the sample, create a URL action so we can respond to the HTTP request that loaded the module.
     */
    espDefineAction(route, "response", response);
    return 0;
}
