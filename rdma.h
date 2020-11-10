#ifndef RDMA_H
#define RDMA_H

#define CQ_SIZE 0x10 /*completion queue size */
#define MAX_SEND_WR 10/* maximum send work requests capacity */
#define MAX_RECV_WR 10
#define MAX_SEND_SGE 1/* scatter gather entry (SGE) size for send request.*/
#define MAX_RECV_SGE 1

#define TIME_OUT_IN_MS 500
#define MAX_FILE_NAME_LENGTH 256

 enum message_type {
        MSG_MR,
        MSG_READY,
        MSG_DONE,
    };

struct message {
    enum message_type type;

    struct key_data{
        uint64_t addr;
        uint32_t rkey;
    } data;
};

/*struct describing a connection */
struct connection {

  char *buffer;
  struct ibv_mr *buffer_mr;

  struct message *msg;
  struct ibv_mr *msg_mr;

  int fd;
  char file_name[MAX_FILE_NAME_LENGTH];
};



struct static_context {
    struct ibv_context *ctx; 
    struct ibv_pd *pd;/*protection domain*/
    struct ibv_cq *cq; /*completion  queue*/
    struct ibv_comp_channel *comp_channel; /*completion channel*/

    pthread_t cq_poller_thread; /*separate thread for polling completion queue*/
};

#endif

