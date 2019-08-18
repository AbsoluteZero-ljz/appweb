/*
    foreign.c - Test httpCreateEvent from a foreign thread
 */
#include "esp.h"

static void finalizeResponse(HttpStream *stream, void *message);
static void serviceRequest();
static void foreignThread(uint64 seqno);

ESP_EXPORT int esp_controller_app_foreign(HttpRoute *route)
{
    espDefineAction(route, "request", serviceRequest);
    return 0;
}

static void serviceRequest(HttpStream *stream)
{
    uint64      seqno;

    seqno = stream->seqno;
    if (mprStartOsThread("foreign", foreignThread, LTOP(stream->seqno), NULL) < 0) {
        print("NO THREAD");
    }
}


static void foreignThread(uint64 streamSeqno)
{
    char    *message;

    assert(mprGetCurrentThread() == NULL);

    message = strdup("Hello World");
    if (httpCreateEvent(streamSeqno, finalizeResponse, message) < 0) {
        print("NO EVENT");
        free(message);
    }
}


static void finalizeResponse(HttpStream *stream, void *message)
{
    MprTicks ticks = mprGetTicks();
    assert(message);
    if (stream) {
        httpWrite(stream->writeq, "message: %ld\n", ticks);
        httpFinalize(stream);
        httpServiceNetQueues(stream->net, 0);
        httpProtocol(stream);
        // print("%ld seqno %ld state %d", ticks, stream->seqno, stream->state);
    } else {
        print("NO STREAM");
    }
    free(message);
}
