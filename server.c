// #include <iostream>
#include "utils.h"
#include <fcntl.h> 

// #define _GLIBCXX_USE_CXX11_ABI 0
// using namespace std;

const size_t BUFFER_SIZE = 10 * 1024 * 1024;

static struct static_context *s_ctx = NULL;
void sendMessage(struct rdma_cm_id *id);
void postReceiveRequest(struct rdma_cm_id *id);

void onCompletion(struct ibv_wc *wc){

    struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
    struct connection *conn = (struct connection *)id->context;
    if(wc->status != IBV_WC_SUCCESS)
        die("completion failed");

    if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
    uint32_t size = ntohl(wc->imm_data);

    if (size == 0) {
      conn->msg->type = MSG_DONE;
      sendMessage(id);

      // don't need post_receive() since we're done with this connection

    } else if (conn->file_name[0]) {
      ssize_t ret;

      // cout << "received %i bytes." << size << endl;

      ret = write(conn->fd, conn->buffer, size);

      if (ret != size)
        die("write() failed");

      postReceiveRequest(id);

      conn->msg->type = MSG_READY;
      sendMessage(id);

    } else {
      size = (size > MAX_FILE_NAME_LENGTH) ? MAX_FILE_NAME_LENGTH : size;
      memcpy(conn->file_name, conn->buffer, size);
      conn->file_name[size - 1] = '\0';

      // cout << "opening file %s."<< conn->file_name << endl;

      conn->fd = open(conn->file_name, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

      if (conn->fd == -1)
        die("open() failed");

      postReceiveRequest(id);

      conn->msg->type = MSG_READY;
      sendMessage(id);
    }
  }
    
}

void registerMemory(struct connection *conn){

     posix_memalign((void **)&conn->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
    conn->buffer_mr = ibv_reg_mr(s_ctx->pd, conn->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    /*buffer memory reion is used for remote write for the peer machine*/

    posix_memalign((void **)&conn->msg, sysconf(_SC_PAGESIZE), sizeof(*conn->msg));
    conn->msg_mr = ibv_reg_mr(s_ctx->pd, conn->msg, sizeof(*conn->msg), 0);
    /*mag memory region is used to store and send the message and rkey info to the remote side*/

}


void postReceiveRequest(struct rdma_cm_id *id){
    struct ibv_recv_wr rr, *bad_rr = NULL; 
    // struct ibv_sge *sge

    memset(&rr, 0, sizeof(struct ibv_recv_wr));
    rr.wr_id = (uintptr_t)id;
    // rr->next = NULL;
    rr.sg_list = NULL;
    rr.num_sge = 0;

    // sge.addr = (uintptr_t)conn->recv_msg;
    // sge.length = sizeof(struct message);
    // sge.lkey = conn->recv_mr->lkey;
        
    ibv_post_recv(id->qp, &rr, &bad_rr);
}


int onConnectRequest(struct rdma_cm_id *id){
    
    struct connection *conn;
    // cout << "------------- received a connection request --------------" << endl;

    createConnection(id);

    conn = (struct connection *)calloc(1, sizeof(struct connection));
    id->context = conn;

    conn->file_name[0] = '\0';

    registerMemory(conn);
    postReceiveRequest(id);
    // sprintf(get_local_message_region(id->context), "message from passive/server side with pid %d", getpid());
    // memset(&cm_param, 0, sizeof(struct rdma_conn_param));

    // cout << " -------------- successfullly created a connection  -------------------" << endl;
    
    return 0;
}

/*in  sendMessage, data are gathered from sge, whose address are set to conn->msg_mr, where contains rkey for the client */
void sendMessage(struct rdma_cm_id *id){
    struct connection *conn = (struct connection *)id->context;

    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    // snprintf(conn->send_region, RDMA_BUFFER_SIZE, "message from passive/server side with pid %d", getpid());

    // cout << " -------- connected, sending rkey to client ------------\n";

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;

    sge.addr = (uintptr_t)conn->msg;
    sge.length = sizeof(*conn->msg);
    sge.lkey = conn->msg_mr->lkey;

    ibv_post_send(id->qp, &wr, &bad_wr);
}


int onConnectionEstablished(struct rdma_cm_id *id)
{
  struct connection *conn = (struct connection *)id->context;

  conn->msg->type =  MSG_MR; /*type MSG_MR means sending rkey info to the remote side*/
  conn->msg->data.addr = (uintptr_t)conn->buffer_mr->addr;
  /*mr_buffer are used by the peer(client) to write data, so we should send its remote key to client*/
  conn->msg->data.rkey = conn->buffer_mr->rkey;

  return 0;
}

static void onDisconnect(struct rdma_cm_id *id)
{
  struct connection *conn = (struct connection *)id->context;

  close(conn->fd);

  ibv_dereg_mr(conn->buffer_mr);
  ibv_dereg_mr(conn->msg_mr);

  free(conn->buffer);
  free(conn->msg);

  // cout << "finished transferring %s " << conn->file_name << endl;

  free(conn);
}


void serverEventLoop(struct rdma_event_channel *ec, int exit_on_disconnect){
    
    struct rdma_cm_event *event = NULL;
    // struct rdma_conn_param *cm_param;
    // cout << "---------- entering event loop, waiting for event ------------- " << endl;
    // setCmParam(cm_param);

    while(rdma_get_cm_event(ec, &event) == 0){
        struct rdma_cm_event eventCopy;

        memcpy(&eventCopy, event, sizeof(*event));
        rdma_ack_cm_event(event);
        /*All events that are reported must be acknowledged by calling rdma_ack_cm_event. 
         rdma_cm_event is released by the rdma_ack_cm_event routine. 
         Destruction of an rdma_cm_id will block until related events have been acknowledged. */

        if (eventCopy.event == RDMA_CM_EVENT_CONNECT_REQUEST){
                onConnectRequest(eventCopy.id);
                /*Connection request events give the user a newly created rdma_cm_id, similar to a
                  new socket, but the rdma_cm_id is bound to a specific RDMA device. rdma_accept is called on
                  the new rdma_cm_id*/
                rdma_accept(eventCopy.id, NULL);
            }
        else if (eventCopy.event == RDMA_CM_EVENT_ESTABLISHED)
            onConnectionEstablished(eventCopy.id);
        else if (eventCopy.event == RDMA_CM_EVENT_DISCONNECTED){
            rdma_destroy_qp(eventCopy.id);
            onDisconnect(eventCopy.id);
            rdma_destroy_id(eventCopy.id);
        }
        else 
            die("Event failure : unknown event");
    }

}

int main(int argc, char **argv){

    init(onCompletion);

    struct sockaddr_in addr;
    struct rdma_cm_id *listener = NULL;
    struct rdma_event_channel *ec = NULL;
    uint16_t port = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    ec = rdma_create_event_channel();
    rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP);/* RDMA_PS_TCP Provides reliable, connection-oriented QP communication.*/
    rdma_bind_addr(listener, (struct sockaddr *)&addr); /*bind cm_id to a local address, addr.sin_port is set to 0, rdma_cm will select an avaliable port*/
    rdma_listen(listener, 10); /*10 is an arbitrary backlog value*/

    port = ntohs(rdma_get_src_port(listener));
    // cout << "server listening on port : " << port << endl;
    printf("server listening on port : %d\n", port);

    /*sit in a loop to retrieve event in the rdma event channel. 
      If no events are pending, by default, the call will block until an event is received*/ 
    serverEventLoop(ec, 0);
    
    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);

    return 0;
}







