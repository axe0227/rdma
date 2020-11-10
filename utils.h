
#ifndef UTILS_H
#define UTILS_H

// #include <iostream>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

#include "rdma.h"

typedef void (*completion_fp)(struct ibv_wc *wc);

void init(completion_fp);

// void build_connection(struct rdma_cm_id *id);
// struct ibv_pd *rc_get_pd();
void die(char *str);
void setCmParam(struct rdma_conn_param *param);
void *pollCompletionQueue(void *cq_context);
// void createStaticContext(struct rmda_cm_id *id);
void setQueuePairAttr(struct ibv_qp_init_attr *qp_init_attr);
void createConnection(struct rdma_cm_id *id);

#endif