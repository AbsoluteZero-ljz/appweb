/*
    switchboard.c - WebSockets chat server
 */
#include "esp.h"

/*
    List of clients. Stores the conn IDs.
 */
static MprList  *clients;

/*
    Example of a structure to pass to each client.
 */
typedef struct Msg {
    HttpPacket  *packet;
} Msg;


static void chat(HttpConn *conn, Msg *msg);
static void chat_action();
static void chat_callback(HttpConn *conn, int event, int arg);
static void manageMsg(Msg *msg, int flags);


/*
    Initialize the "chat" loadable module
 */
ESP_EXPORT int esp_controller_chat_switchboard(HttpRoute *route)
{
    /*
        Create a list of clients holding the conn sequence number. Preserve the list from GC by adding as route data.
     */
    clients = mprCreateList(0, MPR_LIST_STATIC_VALUES);
    httpSetRouteData(route, "clients", clients);

    /*
        Define the "chat" action that will run when the "test/chat" URI is invoked
     */
    espDefineAction(route, "test/chat", chat_action);
    return 0;
}


/*
    Action to run in response to the "test/chat" URI
 */
static void chat_action(HttpConn *conn)
{
    mprAddItem(clients, LTOP(conn->seqno));

    /*
        Establish the event callback that will be called for I/O events of interest for all clients.
     */
    espSetNotifier(conn, chat_callback);
}


/*
    Event callback. Invoked for incoming web socket messages and other events of interest.
    Running on the conn event dispatcher using an Mpr worker thread.
 */
static void chat_callback(HttpConn *conn, int event, int arg)
{
    HttpPacket  *packet;
    void        *client;
    Msg         *msg;
    int         next;

    if (event == HTTP_EVENT_READABLE) {
        packet = httpGetPacket(conn->readq);
        if (packet->type == WS_MSG_TEXT || packet->type == WS_MSG_BINARY) {
            for (ITERATE_ITEMS(clients, client, next)) {
                /*
                    Send the message to each connections using the conn sequence number captured earlier.
                    This must be done using each connection's event dispatcher to ensure we don't conflict with
                    other activity on the conn that may happen on another worker thread at the same time.
                    The "chat" callback will be invoked on the releveant connections's event dispatcher.

                    We allocate the message object here just to demonstrate how it is done, despite only having one 
                    field "packet".  We could have just passed the packet without allocating a Msg. Keep the reference 
                    in conn->data to ensure it is retained by the GC.
                 */
                conn->data = msg = mprAllocObj(Msg, manageMsg);
                msg->packet = packet;
                httpCreateEvent(PTOL(client), (HttpEventProc) chat, msg);
            }
        }

    } else if (event == HTTP_EVENT_APP_CLOSE) {
        /*
            This event is in response to a web sockets close event
         */
        mprLog("chat info", 0, "Close event. Status status %d, orderly closed %d, reason %s", arg,
        httpWebSocketOrderlyClosed(conn), httpGetWebSocketCloseReason(conn));

    } else if (event == HTTP_EVENT_DESTROY) {
        /*
            This is invoked when the client is closed. This API is thread safe.
         */
        mprRemoveItem(clients, LTOP(conn->seqno));
    }
}


/*
    Send message to a client
 */
static void chat(HttpConn *conn, Msg *msg)
{
    HttpPacket  *packet;

    if (conn) {
        packet = msg->packet;
        httpSendBlock(conn, packet->type, httpGetPacketStart(packet), httpGetPacketLength(packet), 0);
    } else {
        /* Stream destroyed. Release any custom Msg resources if required here */
    }
}

/*
    Garbage collection rention callback.
 */
static void manageMsg(Msg *msg, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(msg->packet);
    }
}
