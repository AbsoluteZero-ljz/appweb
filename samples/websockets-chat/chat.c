/*
    chat.c - WebSockets chat server
 */
#include "esp.h"

static MprList  *clients;

/*
    Hold a message destined for a connection
 */
typedef struct Msg {
    HttpConn    *conn;
    HttpPacket  *packet;
} Msg;

static void manageMsg(Msg *msg, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(msg->conn);
        mprMark(msg->packet);
    }
}

/*
    Send message to a connection
 */
static void chat(Msg *msg)
{
    HttpConn    *conn;
    HttpPacket  *packet;

    conn = msg->conn;
    packet = msg->packet;
    print("Sending %s", httpGetPacketStart(packet));
    httpSendBlock(conn, packet->type, httpGetPacketStart(packet), httpGetPacketLength(packet), 0);
}

/*
    Event callback. Invoked for incoming web socket messages and other events of interest.
 */
static void chat_callback(HttpConn *conn, int event, int arg)
{
    HttpPacket  *packet;
    HttpConn    *client;
    Msg         *msg;
    int         next;

    if (event == HTTP_EVENT_READABLE) {
        packet = httpGetPacket(conn->readq);
        if (packet->type == WS_MSG_TEXT || packet->type == WS_MSG_BINARY) {
            for (ITERATE_ITEMS(clients, client, next)) {
                msg = mprAllocObj(Msg, manageMsg);
                msg->conn = client;
                msg->packet = packet;
                mprCreateEvent(client->dispatcher, "chat", 0, chat, msg, 0);
            }
        }
    } else if (event == HTTP_EVENT_APP_CLOSE) {
        mprLog(0, "chat.c: close event. Status status %d, orderly closed %d, reason %s", arg,
        httpWebSocketOrderlyClosed(conn), httpGetWebSocketCloseReason(conn));
        mprRemoveItem(clients, conn);
    }
}

/*
    Action to run in response to the "test/chat" URI
 */
static void chat_action() 
{
    HttpConn    *conn;

    conn = getConn();
    mprAddItem(clients, conn);

    /*
        Don't automatically finalize (complete) the request when this routine returns. This keeps the connection open.
     */
    dontAutoFinalize();

    /*
        Establish the event callback
     */
    espSetNotifier(getConn(), chat_callback);
}


/*
    Initialize the "chat" loadable module
 */
ESP_EXPORT int esp_controller_app_chat(HttpRoute *route) 
{
    clients = mprCreateList(0, 0);
    mprAddRoot(clients);

    /*
        Define the "chat" action that will run when the "test/chat" URI is invoked
     */
    espDefineAction(route, "test/chat", chat_action);
    return 0;
}
