#include "utils.h"

static struct static_context *s_ctx = NULL;

static completion_fp onCompletion = NULL;

void setQueuePairAttr(struct ibv_qp_init_attr *qp_init_attr);
void *pollCompletionQueue(void *cq_context);
void createStaticContext(struct rdma_cm_id *id);

void build_connection(struct rdma_cm_id *id)
{
  struct ibv_qp_init_attr qp_attr;

  createStaticContext(id);
  setQueuePairAttr(&qp_attr);
  rdma_create_qp(id, s_ctx->pd, &qp_attr);
}


void die(char *str){
    // cerr << str << endl;
    fprintf(stderr, "%s\n", str);
    exit (EXIT_FAILURE);
}

void setCmParam(struct rdma_conn_param *params){
    // cout << "-------------- setting connection manager parameter --------------- " << endl;
    memset(params, 0, sizeof(struct rdma_conn_param));
    params->initiator_depth = 1;
    params->rnr_retry_count = 7; /* infinite retry */
    // cout << "-------------- finish setting connection manager parameter --------------- " << endl;
}

void *pollCompletionQueue(void *cq_context){
    struct ibv_cq *cq;
    struct ibv_wc wc; /* CQE array*/
    int ret;

    while(1){
        /*waits for a notification to be sent on the indicated completion channel (CC).blocking operation */
        ibv_get_cq_event(s_ctx->comp_channel, &cq, &cq_context);
        // cout << "------------ completion channel gets notified ------------------" << endl;
        /*Each notification sent MUST be acknowledged with the ibv_ack_cq_events operation*/
        ibv_ack_cq_events(cq, 1);
        
        /*Once a notification for a completion queue (CQ) is sent on a CC, that CQ is now “disarmed” and
          will not send any more notifications to the CC until it is rearmed again with a new call to the
          ibv_req_notify_cq operation*/
        ibv_req_notify_cq(cq, 0);

        /*ibv_get_cq_event only informs the user that a CQ has completion queue entries (CQE) to be processed, it does not actually process the CQEs. 
          The user should use the ibv_poll_cq operation to process the CQEs. ibv_poll_cq retrieves CQEs from a completion queue (CQ).*/
          while( (ret = ibv_poll_cq(cq, 1, &wc))){
              if(ret == -1)
                die("ibv_cq_poll failed");

              /*process the CQEs in the wc list*/
                 onCompletion(&wc);
          }
    }
    return NULL;
}

/* create a static context for one connection request*/
void createStaticContext(struct rdma_cm_id *id){

    struct ibv_context *ctx = id->verbs;
    if(s_ctx && s_ctx->ctx != ctx)
        die("static context already created");

    s_ctx = (struct static_context *)malloc(sizeof(struct static_context));
    s_ctx->ctx = ctx;

    s_ctx->pd = ibv_alloc_pd(ctx);
    s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx); /*used to notify the cq polling thread*/
    s_ctx->cq = ibv_create_cq(ctx, CQ_SIZE, NULL, s_ctx->comp_channel, 0);

    ibv_req_notify_cq(s_ctx->cq, 0);
    /*arms the notification mechanism for the indicated completion queue (CQ).
      When a completion queue entry (CQE) is placed on the CQ, a completion event will be sent to
      the completion channel (CC) associated with the CQ*/

    pthread_create(&s_ctx->cq_poller_thread, NULL, pollCompletionQueue, NULL);
    printf("------------- static context created -------------- \n");
}

 void setQueuePairAttr(struct ibv_qp_init_attr *qp_init_attr){

    memset(qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_init_attr->qp_type = IBV_QPT_RC; /*reliable connection*/
    // qp_init_attr.sq_sig_all = 0;  /*If this value is set to 1, all send requests (WR) will generate completion queue events (CQE)*/
    qp_init_attr->send_cq = s_ctx->cq;         
    qp_init_attr->recv_cq = s_ctx->cq; 
           
    qp_init_attr->cap.max_send_wr = MAX_SEND_WR;  
    qp_init_attr->cap.max_recv_wr = MAX_RECV_WR;  
    qp_init_attr->cap.max_send_sge = MAX_SEND_SGE; 
    qp_init_attr->cap.max_recv_sge = MAX_RECV_SGE; 
    printf("------------- qp attr set -------------- \n");

}

void createConnection(struct rdma_cm_id *id){

    printf("------------- calling createConnection --------------\n");
    struct ibv_qp_init_attr qp_init_attr;
    // memset(qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));

    createStaticContext(id);
    setQueuePairAttr(&qp_init_attr);

    rdma_create_qp(id, s_ctx->pd, &qp_init_attr);
}

struct ibv_pd * rc_get_pd()
{
  return s_ctx->pd;
}

void init(completion_fp comp){
  onCompletion = comp;
}