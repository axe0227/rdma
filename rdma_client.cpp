#include <iostream>
#include "utils.h"
#include <fcntl.h> 

using namespace std;

void postReceiveRequest(struct rdma_cm_id *id);

static void writeRemote(struct rdma_cm_id *id, uint32_t len)
{
  struct client_context *ctx = (struct client_context *)id->context;

  struct ibv_send_wr wr, *bad_wr = NULL;
  struct ibv_sge sge;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = (uintptr_t)id;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.imm_data = htonl(len);
  wr.wr.rdma.remote_addr = ctx->peer_addr;
  wr.wr.rdma.rkey = ctx->peer_rkey;

  if (len) {
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)ctx->conn->buffer;
    sge.length = len;
    sge.lkey = ctx->conn->buffer_mr->lkey;
  }

  ibv_post_send(id->qp, &wr, &bad_wr);
}


static void sendNextChunk(struct rdma_cm_id *id)
{
  struct client_context *ctx = (struct client_context *)id->context;
  ssize_t size = 0;

  size = read(ctx->conn->fd, ctx->conn->buffer, BUFFER_SIZE);

  if (size == -1)
    die("read() failed\n");

  writeRemote(id, size);
}

static void sendFileName(struct rdma_cm_id *id)
{
  struct client_context *ctx = (struct client_context *)id->context;

  strcpy(ctx->conn->buffer, ctx->conn->file_name);

  writeRemote(id, strlen(ctx->conn->file_name) + 1);
}

static void onCompletion(struct ibv_wc *wc)
{
  struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
  struct client_context *ctx = (struct client_context *)id->context;
  struct connection *conn = ctx->conn;

  if (wc->opcode & IBV_WC_RECV) {
    if (conn->msg->type == MSG_MR) {
      ctx->peer_addr = conn->msg->data.addr;
      ctx->peer_rkey = conn->msg->data.rkey;

      cout << "received MR, sending file name\n" << endl;
      sendFileName(id);
    } else if (conn->msg->type == MSG_READY) {
      cout << "received READY, sending chunk\n" << endl;
      sendNextChunk(id);
    } else if (conn->msg->type == MSG_DONE) {
      cout << "received DONE, disconnecting\n" << endl;
      rdma_disconnect(id);
      return;
    }

    postReceiveRequest(id);
  }
}

void postReceiveRequest(struct rdma_cm_id *id){

    struct client_context *c_ctx = (struct client_context *)id->context;
    struct connection *conn = c_ctx->conn;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)conn->msg;
    sge.length = sizeof(*conn->msg);
    sge.lkey = conn->msg_mr->lkey;

    ibv_post_recv(id->qp, &wr, &bad_wr);
}

void registerMemory(struct rdma_cm_id *id){

    struct client_context *c_ctx = (struct client_context *)id->context;
    struct connection *conn = c_ctx->conn;

    posix_memalign((void **)&conn->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
    conn->buffer_mr = ibv_reg_mr(id->pd, conn->buffer, BUFFER_SIZE, 0);

    posix_memalign((void **)&conn->msg, sysconf(_SC_PAGESIZE), sizeof(struct message));
    conn->msg_mr = ibv_reg_mr(id->pd, conn->msg, sizeof(struct message), IBV_ACCESS_LOCAL_WRITE);

    postReceiveRequest(id);
}



int onConnectionRequest(struct rdma_cm_id *id){
    
    struct client_context *c_ctx;
    cout << "------------- received a connection request --------------" << endl;

    createConnection(id);
    c_ctx = (struct client_context *)calloc(1, sizeof(struct client_context));
    id->context = c_ctx;

    c_ctx->conn->file_name[0] = '\0';
    registerMemory(id);
    postReceiveRequest(id);
    // sprintf(get_local_message_region(id->context), "message from passive/server side with pid %d", getpid());
    // memset(&cm_param, 0, sizeof(struct rdma_conn_param));

    /*Connection request events give the user a newly created rdma_cm_id, similar to a
        new socket, but the rdma_cm_id is bound to a specific RDMA device. rdma_accept is called on
        the new rdma_cm_id*/
    cout << " -------------- successfullly created a connection  -------------------" << endl;
    
    return 0;
}

int clientEventLoop(struct rdma_event_channel *ec, int exit_on_disconnect){
    struct rdma_cm_event *event = NULL;
    struct rdma_conn_param cm_params;

    setCmParam(&cm_params);

    while(rdma_get_cm_event(ec, &event) == 0){
        struct rdma_cm_event *eventCopy;

        memcpy(eventCopy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        /*All events that are reported must be acknowledged by calling rdma_ack_cm_event. 
        rdma_cm_event is released by the rdma_ack_cm_event routine. 
        Destruction of an rdma_cm_id will block until related events have been acknowledged. */

         if (eventCopy->event == RDMA_CM_EVENT_ADDR_RESOLVED) {
            onConnectionRequest(eventCopy->id);
            rdma_resolve_route(eventCopy->id, TIME_OUT_IN_MS);
        } else if(eventCopy->event == RDMA_CM_EVENT_ROUTE_RESOLVED){
            rdma_connect(eventCopy->id, &cm_params);
        } else if (eventCopy->event == RDMA_CM_EVENT_DISCONNECTED) {
            rdma_destroy_qp(eventCopy->id);
            rdma_destroy_id(eventCopy->id);

            if (exit_on_disconnect)
                break;
        }else{
            die("Event failure : unknown event");
        }
    }
}

int main(int argc, char **argv){

    struct addrinfo *addr;
    struct rdma_cm_event *event = nullptr;
    struct rdma_cm_id *id = nullptr;
    struct rdma_event_channel *ec = nullptr;

    struct client_context *ctx;
    struct connection *conn = ctx->conn;
    char * fn = conn->file_name;

    if(argc != 4){
        cerr <<  "usage: " << argv[0] << " <server-address> <port> <file_name> "  << endl;
        exit(1);
    }

    fn = basename(argv[3]);
    conn->fd = open(argv[3], O_RDONLY);

    if(conn->fd == -1){
        cerr << "unable to open file %s " << argv[2] << endl;
        exit(1);
    }

    init(onCompletion);
    getaddrinfo(argv[1], argv[2], NULL, &addr);

    ec = rdma_create_event_channel();
    rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
    rdma_resolve_addr(id, NULL, addr->ai_addr, TIME_OUT_IN_MS);
    /*resolves destination and optional source addresses from IP addresses to an RDMA address. 
       If successful, the specified rdma_cm_id will be bound to a local device.*/
    freeaddrinfo(addr);    

    id->context = conn;

    /*client exits on disconnnect*/
    clientEventLoop(ec, 1);

    rdma_destroy_event_channel(ec);

    close(conn->fd);
    return 0;
}