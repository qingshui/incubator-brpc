// Copyright (c) 2014 baidu-rpc authors.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifdef BRPC_RDMA
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#endif
#include <arpa/inet.h>
#include <butil/fd_utility.h>                     // make_non_blocking
#include <butil/logging.h>
#include <butil/unique_ptr.h>
#include <gflags/gflags.h>
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_communication_manager.h"

namespace brpc {
namespace rdma {

DEFINE_int32(rdma_backlog, 1024, "The backlog for rdma connection.");
DEFINE_int32(rdma_conn_timeout_ms, 500, "The timeout (ms) for RDMA connection"
                                        "establishment.");

static const int FLOW_CONTROL = 1;          // for creating QP
static const int RETRY_COUNT = 1;           // for creating QP
static const int RNR_RETRY_COUNT = 0;       // for creating QP

RdmaCommunicationManager::RdmaCommunicationManager(void* cm_id)
    : _cm_id(cm_id)
{
}

RdmaCommunicationManager::~RdmaCommunicationManager() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
#else
    ReleaseQP();
    if (_cm_id) {
        rdma_destroy_id((rdma_cm_id*)_cm_id);
    }
#endif
}

RdmaCommunicationManager* RdmaCommunicationManager::Create() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    RdmaCommunicationManager* rcm = new (std::nothrow) RdmaCommunicationManager;
    if (!rcm) {
        return NULL;
    }
    if (rdma_create_id(NULL, (rdma_cm_id**)(&rcm->_cm_id),
                       NULL, RDMA_PS_TCP)) {
        PLOG(WARNING) << "Fail to rdma_create_id";
        delete rcm;
        return NULL;
    }
    rdma_cm_id* cm_id = (rdma_cm_id*)rcm->_cm_id;
    butil::make_close_on_exec(cm_id->channel->fd);
    if (butil::make_non_blocking(cm_id->channel->fd) < 0) {
        PLOG(WARNING) << "Fail to set rdmacm fd to nonblocking";
        delete rcm;
        return NULL;
    }
    return rcm;
#endif
}

RdmaCommunicationManager* RdmaCommunicationManager::Listen(
        butil::EndPoint& listen_ep) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    std::unique_ptr<RdmaCommunicationManager> rcm(Create());
    if (rcm == NULL) {
        return NULL;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_ep.port);
    addr.sin_addr = listen_ep.ip;

    rdma_cm_id* cm_id = (rdma_cm_id*)rcm->_cm_id;
    if (rdma_bind_addr(cm_id, (sockaddr*)&addr) < 0) {
        PLOG(WARNING) << "Fail to rdma_bind_addr";
        return NULL;
    }

    if (rdma_listen(cm_id, FLAGS_rdma_backlog) < 0) {
        PLOG(WARNING) << "Fail to rdma_listen";
        return NULL;
    }

    return rcm.release();
#endif
}

RdmaCommunicationManager* RdmaCommunicationManager::GetRequest(
        char** data, size_t* len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(_cm_id != NULL);

    rdma_cm_id* cm_id = NULL;
    if (rdma_get_request((rdma_cm_id*)_cm_id, &cm_id) < 0 || cm_id == NULL) {
        if (errno != EAGAIN) {
            PLOG(WARNING) << "Fail to rdma_get_request";
        }
        return NULL;
    }

    std::unique_ptr<RdmaCommunicationManager> rcm(
            new (std::nothrow) RdmaCommunicationManager(cm_id));
    if (rcm == NULL) {
        PLOG(WARNING) << "Fail to create RdmaCommunicationManager";
        rdma_destroy_id(cm_id);
        return NULL;
    }

    butil::make_close_on_exec(cm_id->channel->fd);
    if (butil::make_non_blocking(cm_id->channel->fd) < 0) {
        PLOG(WARNING) << "Fail to set rdmacm fd to nonblocking";
        return NULL;
    }

    CHECK(cm_id->event != NULL);
    *data = (char*)cm_id->event->param.conn.private_data;
    *len = (size_t)cm_id->event->param.conn.private_data_len;

    return rcm.release();
#endif
}

// Used by UT, do not set it static
#ifdef BRPC_RDMA
void InitRdmaConnParam(rdma_conn_param* p, const char* data, size_t len) {
    CHECK(p != NULL);

    memset(p, 0, sizeof(*p));
    if (data) {
        p->private_data = data;
        p->private_data_len = len;
    }
    p->flow_control = FLOW_CONTROL;
    p->retry_count = RETRY_COUNT;
    p->rnr_retry_count = RNR_RETRY_COUNT;
}
#endif

int RdmaCommunicationManager::Accept(char* data, size_t len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);

    rdma_conn_param param;
    InitRdmaConnParam(&param, data, len);
    return rdma_accept((rdma_cm_id*)_cm_id, &param);
#endif
}

int RdmaCommunicationManager::Connect(char* data, size_t len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);

    rdma_conn_param param;
    InitRdmaConnParam(&param, data, len);
    return rdma_connect((rdma_cm_id*)_cm_id, &param);
#endif
}

int RdmaCommunicationManager::ResolveAddr(butil::EndPoint& remote_ep) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);
    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;

    sockaddr_in* addr = &cm_id->route.addr.dst_sin;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(remote_ep.port);
    // Automatically find local RDMA address
    // We cannot use 127.0.0.1 or 0.0.0.0 for RDMA directly, because
    // the resources used are bound to a specific RDMA NIC.
    if (IsLocalIP(remote_ep.ip)) {
        addr->sin_addr = GetRdmaIP();
    } else {
        addr->sin_addr = remote_ep.ip;
    }
    cm_id->route.addr.src_addr.sa_family = addr->sin_family;

    return rdma_resolve_addr(cm_id, NULL, (sockaddr*)addr,
                             FLAGS_rdma_conn_timeout_ms / 2);
#endif
}

int RdmaCommunicationManager::ResolveRoute() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);

    return rdma_resolve_route((rdma_cm_id*)_cm_id,
                              FLAGS_rdma_conn_timeout_ms / 2);
#endif
}

RdmaCMEvent RdmaCommunicationManager::GetCMEvent() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return RDMACM_EVENT_ERROR;
#else
    CHECK(_cm_id != NULL);
    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;

    if (cm_id->event && rdma_ack_cm_event(cm_id->event) < 0) {
        PLOG(WARNING) << "Fail to rdma_ack_cm_event";
        return RDMACM_EVENT_ERROR;
    }
    cm_id->event = NULL;

    if (rdma_get_cm_event(cm_id->channel, &cm_id->event) < 0) {
        if (errno != EAGAIN) {
            PLOG(WARNING) << "Fail to rdma_get_cm_event";
            return RDMACM_EVENT_ERROR;
        }
        return RDMACM_EVENT_NONE;
    }

    switch (cm_id->event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED: {
        return RDMACM_EVENT_ADDR_RESOLVED;
    }
    case RDMA_CM_EVENT_ROUTE_RESOLVED: {
        return RDMACM_EVENT_ROUTE_RESOLVED;
    }
    case RDMA_CM_EVENT_ESTABLISHED: {
        return RDMACM_EVENT_ESTABLISHED;
    }
    case RDMA_CM_EVENT_DISCONNECTED: {
        return RDMACM_EVENT_DISCONNECT;
    }
    default:
        break;
    }

    return RDMACM_EVENT_OTHER;
#endif
}

void* RdmaCommunicationManager::CreateQP(
        uint32_t sq_size, uint32_t rq_size, void* cq, uint64_t id) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(_cm_id != NULL);
    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;
    
    // Create QP
    ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_context = (void*)id;
    qp_attr.send_cq = (ibv_cq*)cq;
    qp_attr.recv_cq = (ibv_cq*)cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.sq_sig_all = 0;
    qp_attr.cap.max_send_wr = sq_size;
    qp_attr.cap.max_recv_wr = rq_size;
    qp_attr.cap.max_send_sge = GetRdmaMaxSge();
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.cap.max_inline_data = 64;
    if (rdma_create_qp(cm_id, (ibv_pd*)GetRdmaProtectionDomain(),
                       &qp_attr) < 0) {
        PLOG(WARNING) << "Fail to rdma_create_qp";
        return NULL;
    }

    return cm_id->qp;
#endif
}

void RdmaCommunicationManager::ReleaseQP() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
#else
    if (_cm_id) {
        rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;

        if (cm_id->qp) {
            // Do not use rdma_destroy_qp, which will release CQ as well
            ibv_destroy_qp(cm_id->qp);
            cm_id->qp = NULL;
        }
    }
#endif
}

int RdmaCommunicationManager::GetFD() const {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    if (!_cm_id) {
        return -1;
    }
    return ((rdma_cm_id*)_cm_id)->channel->fd;
#endif
}

char* RdmaCommunicationManager::GetConnData() const {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(_cm_id != NULL);

    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;
    if (!cm_id->event) {
        return NULL;
    }
    return (char*)cm_id->event->param.conn.private_data;
#endif
}

size_t RdmaCommunicationManager::GetConnDataLen() const {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return 0;
#else
    CHECK(_cm_id != NULL);

    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;
    if (!cm_id->event) {
        return 0;
    }
    return cm_id->event->param.conn.private_data_len;
#endif
}

}  // namespace rdma
}  // namespace brpc

