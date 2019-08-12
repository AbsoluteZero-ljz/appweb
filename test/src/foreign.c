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
    mprStartOsThread("foreign", foreignThread, LTOP(stream->seqno), NULL);
}


static void foreignThread(uint64 seqno)
{
    assert(mprGetCurrentThread() == NULL);
    httpCreateEvent(seqno, finalizeResponse, strdup("Hello World"));
}


static void finalizeResponse(HttpStream *stream, void *message)
{
    httpWrite(stream->writeq, "message: %s\n", message);

    httpFinalize(stream);
    httpProcess(stream->inputq);
    free(message);
}
