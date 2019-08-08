/*
    chat.c - WebSockets chat server
 */
#include "esp.h"

/*
    List of stream IDs for clients
 */
static MprList  *clients;

/*
    Hold a message destined for clients
 */
typedef struct Msg {
    HttpPacket  *packet;
} Msg;

/*
    Garbage collection rention callback.
 */
static void manageMsg(Msg *msg, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(msg->packet);
    }
}

/*
    Send message to a client
 */
static void chat(HttpStream *stream, Msg *msg)
{
    HttpPacket  *packet;

    packet = msg->packet;
    httpSendBlock(stream, packet->type, httpGetPacketStart(packet), httpGetPacketLength(packet), 0);
}

/*
    Event callback. Invoked for incoming web socket messages and other events of interest.
    Running on the stream event dispatcher using an Mpr worker thread.
 */
static void chat_callback(HttpStream *stream, int event, int arg)
{
    HttpPacket  *packet;
    void        *client;
    Msg         *msg;
    int         next;

    if (event == HTTP_EVENT_READABLE) {
        packet = httpGetPacket(stream->readq);
        if (packet->type == WS_MSG_TEXT || packet->type == WS_MSG_BINARY) {
            for (ITERATE_ITEMS(clients, client, next)) {
                /*
                    Send the message to each stream using the stream sequence number captured earlier.
                    The "chat" callback will be invoked on the releveant stream's event dispatcher.
                    This must be done using each stream event dispatcher to ensure we don't conflict with
                    other activity on the stream that may happen on another worker thread at the same time.
                 */
                msg = mprAllocObj(Msg, manageMsg);
                msg->packet = packet;
                httpCreateEvent(PTOL(client), (HttpEventProc) chat, msg);
            }
        }

    } else if (event == HTTP_EVENT_APP_CLOSE) {
        /*
            This event is in response to a web sockets close event
         */
        mprLog("chat info", 0, "Close event. Status status %d, orderly closed %d, reason %s", arg,
        httpWebSocketOrderlyClosed(stream), httpGetWebSocketCloseReason(stream));

    } else if (event == HTTP_EVENT_DESTROY) {
        /*
            This is invoked when the client is closed. This API is thread safe.
         */
        mprRemoveItem(clients, LTOP(stream->seqno));
    }
}

/*
    Action to run in response to the "test/chat" URI
 */
static void chat_action()
{
    HttpStream  *stream;

    /*
        Add the client to the list of clients. This API is thread-safe.
     */
    stream = getConn();
    mprAddItem(clients, LTOP(stream->seqno));

    /*
        Don't automatically finalize (complete) the request when this routine returns. This keeps the stream open.
     */
    dontAutoFinalize();

    /*
        Establish the event callback that will be called for I/O events of interest for all clients.
     */
    espSetNotifier(stream, chat_callback);
}


/*
    Initialize the "chat" loadable module
 */
ESP_EXPORT int esp_controller_websockets_chat(HttpRoute *route)
{
    /*
        Create a list of clients. Preserve from GC by adding as route data.
     */
    clients = mprCreateList(0, MPR_LIST_STATIC_VALUES);
    httpSetRouteData(route, "clients", clients);

    /*
        Define the "chat" action that will run when the "test/chat" URI is invoked
     */
    espDefineAction(route, "test/chat", chat_action);
    return 0;
}
