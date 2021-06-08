/*
    testHandler.c -- Test handler to assist when developing handlers and modules

    This handler is a basic file handler without the frills for GET requests.

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

#include    "appweb.h"

#if ME_COM_TEST

static int prepPacket(HttpQueue *q, HttpPacket *packet);
static ssize readFileData(HttpQueue *q, HttpPacket *packet, ssize size);

static int test_open(HttpQueue* q)
{
    HttpStream  *stream;

    stream = q->stream;
    httpMapFile(stream);
	if ((q->pair->staticData = fopen(stream->tx->filename, "rb")) == NULL) {
        httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot find document");
    }
    return 0;
}

static void test_close(HttpQueue *q)
{
    fclose(q->pair->staticData);
}

static void test_incoming(HttpQueue* q, HttpPacket* packet)
{
	if (packet->flags & HTTP_PACKET_END) {
		httpFinalizeInput(q->stream);
        if (q->net->protocol >= 2) {
            HttpQueue *tail = q->stream->outputq;
            //  Test code to catch condition when tailFilter stops with data pending
            assert(tail->count == 0 || (tail->scheduleNext && tail->scheduleNext != tail) || tail->flags & HTTP_QUEUE_SUSPENDED);
            HttpQueue *h2 = q->net->outputq;
            print("HTTP2 count %d, flags %x, window %d", (int) h2->count, h2->flags, (int) h2->window);
        }
	}
}

static void test_ready(HttpQueue* q)
{
    HttpPacket  *packet;

	packet = httpCreateDataPacket(0);
	packet->esize = q->stream->tx->fileInfo.size;
	httpSetContentLength(q->stream, packet->esize);
	httpPutForService(q, packet, 1);
}

static void test_outgoing(HttpQueue *q)
{
    HttpStream	*stream = q->stream;
    HttpPacket  *packet;
	int         rc;

    for (packet = httpGetPacket(q); packet; packet = httpGetPacket(q)) {
		if (packet->esize) {
			if ((rc = prepPacket(q, packet)) < 0) {
                assert(0);
                return;
            } else if (rc == 0) {
                httpPutBackPacket(q, packet);
                return;
            }
        }
        httpPutPacketToNext(q, packet);
		if (q->first == NULL) {
			httpFinalizeOutput(stream);
		}
    }
    HttpQueue *tail = q->stream->outputq;
    //  Test code to catch condition when tailFilter stops with data pending
    assert(tail->count == 0 || (tail->scheduleNext && tail->scheduleNext != tail) || tail->flags & HTTP_QUEUE_SUSPENDED);
}

static int prepPacket(HttpQueue *q, HttpPacket *packet)
{
    HttpQueue   *nextQ;
    ssize       size, nbytes;

    nextQ = q->nextQ;
    if (packet->esize > nextQ->packetSize) {
        httpPutBackPacket(q, httpSplitPacket(packet, nextQ->packetSize));
        size = nextQ->packetSize;
    } else {
        size = (ssize) packet->esize;
    }
    if ((size + nextQ->count) > nextQ->max) {
        httpSuspendQueue(q);
        if (!(nextQ->flags & HTTP_QUEUE_SUSPENDED)) {
            httpScheduleQueue(nextQ);
        }
        return 0;
    }
    if ((nbytes = readFileData(q, packet, size)) != size) {
        return MPR_ERR_CANT_READ;
    }
    return 1;
}

static ssize readFileData(HttpQueue *q, HttpPacket *packet, ssize size)
{
    FILE       *fp;
    ssize      bytesRead;

    fp = q->staticData;

    if (packet->content == 0) {
        packet->content = mprCreateBuf(size, -1);
    }
	if (mprGetBufSpace(packet->content) < size) {
		size = mprGetBufSpace(packet->content);
	}
	bytesRead = fread(mprGetBufStart(packet->content), sizeof(char), size, fp);
	if ((size != bytesRead) && ferror(fp)) {
        assert(0);
		return -1;
	}
	if (bytesRead >= 0) {
		mprAdjustBufEnd(packet->content, bytesRead);
		packet->esize -= bytesRead;

	} else {
		assert(0);
		return -1;
	}
	return bytesRead;
}

PUBLIC int httpTestInit(Http* http, MprModule *module)
{
	HttpStage  *handler;

	handler = httpCreateHandler("test", module);

    handler->open = test_open;
	handler->incoming = test_incoming;
	handler->ready = test_ready;
	handler->outgoingService = test_outgoing;
	handler->close = test_close;
	return 0;
}
#endif

/*
    Copyright (c) Embedthis Software. All Rights Reserved.
    This software is distributed under a commercial license. Consult the LICENSE.md
    distributed with this software for full details and copyrights.
 */
