// #include <iostream>
#include "utils.h"
#include <fcntl.h> 
#include <libgen.h>

// char mapping[10] = {"a","b"};

// using namespace std;
struct client_context{
    char *buffer;
    struct ibv_mr *buffer_mr;

    struct message *msg;
    struct ibv_mr *msg_mr;

    int fd;
    const char *file_name;
    uint64_t peer_addr;
    uint32_t peer_rkey;
};

void mapping(enum rdma_cm_event_type type){
    if(type == RDMA_CM_EVENT_CONNECT_RESPONSE)
      printf("connect response\n");
    if(type == RDMA_CM_EVENT_CONNECT_ERROR)
      printf("connect error\n");
    if(type == RDMA_CM_EVENT_UNREACHABLE)
  	  printf("unreachable\n");
    if(type == RDMA_CM_EVENT_REJECTED)
      printf("rejected\n");
    if(type == RDMA_CM_EVENT_ESTABLISHED)
      printf("established\n");
    if(type == RDMA_CM_EVENT_DISCONNECTED)
      printf("disconnected\n");
}

const size_t BUFFER_SIZE = 10 * 1024 * 1024;


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

    sge.addr = (uintptr_t)ctx->buffer;
    sge.length = len;
    sge.lkey = ctx->buffer_mr->lkey;
  }

  ibv_post_send(id->qp, &wr, &bad_wr);
}


static void sendNextChunk(struct rdma_cm_id *id)
{
  struct client_context *ctx = (struct client_context *)id->context;
  ssize_t size = 0;

  size = read(ctx->fd, ctx->buffer, BUFFER_SIZE);

  if (size == -1)
    die("read() failed\n");

  writeRemote(id, size);
}

static void sendFileName(struct rdma_cm_id *id)
{
  struct client_context *ctx = (struct client_context *)id->context;

  strcpy(ctx->buffer, ctx->file_name);

  writeRemote(id, strlen(ctx->file_name) + 1);
}

static void onCompletion(struct ibv_wc *wc)
{
  struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
  struct client_context *ctx = (struct client_context *)id->context;
  // struct connection *conn = ctx->conn;
   printf("------------- calling onCompletion -------------- \n");
  if(wc->status == IBV_WC_SUCCESS){
    printf(" wc success\n");
  }else{
    die("status is not success\n");
  }

  if (wc->opcode & IBV_WC_RECV) {
    if (ctx->msg->type == MSG_MR) {
      ctx->peer_addr = ctx->msg->data.addr;
      ctx->peer_rkey = ctx->msg->data.rkey;

      printf("received MR, sending file name\n");
      sendFileName(id);
    } else if (ctx->msg->type == MSG_READY) {
      printf ("received READY, sending chunk\n" );
      sendNextChunk(id);
    } else if (ctx->msg->type == MSG_DONE) {
      printf("received DONE, disconnecting\n");
      rdma_disconnect(id);
      return;
    }

    postReceiveRequest(id);
  }
}

void postReceiveRequest(struct rdma_cm_id *id){

    printf("------------------------------ calling post rr -------------- \n");
    struct client_context *c_ctx = (struct client_context *)id->context;
    // struct connection *conn = c_ctx->conn;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)c_ctx->msg;
    sge.length = sizeof(*c_ctx->msg);
    sge.lkey = c_ctx->msg_mr->lkey;

    // printf("------------- calling ibv_post_recv -------------- \n");
    ibv_post_recv(id->qp, &wr, &bad_wr);
    // printf("------------- posted recv ------------- \n");
}

void registerMemory(struct rdma_cm_id *id){
  // printf("-------------  callin register memory -------------- \n");
    struct client_context *c_ctx = (struct client_context *)id->context;
    // struct connection *conn = c_ctx->conn;

    posix_memalign((void **)&c_ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
    c_ctx->buffer_mr = ibv_reg_mr(id->pd, c_ctx->buffer, BUFFER_SIZE, 0);

    posix_memalign((void **)&c_ctx->msg, sysconf(_SC_PAGESIZE), sizeof(struct message));
    c_ctx->msg_mr = ibv_reg_mr(id->pd, c_ctx->msg, sizeof(struct message), IBV_ACCESS_LOCAL_WRITE);

    // printf("------------- memory registered -------------- \n");
}


int onConnectionRequest(struct rdma_cm_id *id){
  
    // cout << "------------- received a connection request --------------" << endl;
    printf("------------- received a connection request -------------- \n");
    createConnection(id);
    registerMemory(id);
    postReceiveRequest(id);
    return 0;
}

void clientEventLoop(struct rdma_event_channel *ec, int exit_on_disconnect){
  printf("----------- calling client event loop ----------------\n");
    struct rdma_cm_event *event = NULL;
    // struct rdma_conn_param cm_params;

    // setCmParam(&cm_params);

    while(rdma_get_cm_event(ec, &event) == 0){
        struct rdma_cm_event eventCopy;
        printf("got an cm event\n");
        memcpy(&eventCopy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        printf("cm event acked\n");
        /*All events that are reported must be acknowledged by calling rdma_ack_cm_event. 
        rdma_cm_event is released by the rdma_ack_cm_event routine. 
        Destruction of an rdma_cm_id will block until related events have been acknowledged. */
        mapping(eventCopy.event);
         if (eventCopy.event == RDMA_CM_EVENT_ADDR_RESOLVED) {
            printf("--------event: addr resolved--------\n");
            onConnectionRequest(eventCopy.id);
            printf("------------- returned from onConnection Request, calling resolve_route -------------- \n");
            rdma_resolve_route(eventCopy.id, TIME_OUT_IN_MS);
            printf("-------------route resolved  -------------- \n");
        } else if(eventCopy.event == RDMA_CM_EVENT_ROUTE_RESOLVED){
            printf("----------- get route resolved event, calling connect -------------\n");
            rdma_connect(eventCopy.id, NULL);
            printf("-----------calling connect ------------ \n");
        } else if(eventCopy.event == RDMA_CM_EVENT_ESTABLISHED){
            
        } else if (eventCopy.event == RDMA_CM_EVENT_DISCONNECTED) {
            printf("----------- event disconnected ---------------\n");
            rdma_destroy_qp(eventCopy.id);
            rdma_destroy_id(eventCopy.id);

            if (exit_on_disconnect)
                break;
        }else{
            die("Event failure : unknown event");
        }
    }
}

int main(int argc, char **argv){

    printf("calling main\n");
    struct addrinfo *addr;
    struct rdma_cm_id *id = NULL;
    struct rdma_event_channel *ec = NULL;

    struct client_context ctx;
    // struct connection conn;
    printf("checking argc\n");
    if(argc != 4){
        // cerr <<  "usage: " << argv[0] << " <server-address> <port> <file_name> "  << endl;
        fprintf(stderr, "usage: %s <server-address> <port> <file name> \n", argv[0]);
        exit(1);
    }

    ctx.file_name = basename(argv[3]);

    printf("opening file: %s \n", argv[3]);
    // cout << "file name : " << fn << endl;

    ctx.fd = open(argv[3], O_RDONLY);

    // cout << "---------------- successfully opened file -----------------" << endl;
    if(ctx.fd == -1){
        // cout << "unable to open file %s " << argv[2] << endl;
        fprintf(stderr, "uable to open file\n");
        exit(1);
    }

    printf("successfully opened file\n");

    init(onCompletion);
    getaddrinfo(argv[1], argv[2], NULL, &addr);

    printf("successfully got addr info\n");
    ec = rdma_create_event_channel();
    rdma_create_id(ec, &id, NULL, RDMA_PS_TCP);
    rdma_resolve_addr(id, NULL, addr->ai_addr, TIME_OUT_IN_MS);
    /*resolves destination and optional source addresses from IP addresses to an RDMA address. 
       If successful, the specified rdma_cm_id will be bound to a local device.*/
       printf("successfully resolved addr\n");
    freeaddrinfo(addr);    

    id->context = &ctx;

    printf("about to enter eventloop\n");
    /*client exits on disconnnect*/
    clientEventLoop(ec, 1);

    rdma_destroy_event_channel(ec);

    close(ctx.fd);
    return 0;
}