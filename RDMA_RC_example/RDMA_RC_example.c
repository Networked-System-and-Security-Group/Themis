/*
* BUILD COMMAND:
* gcc -Wall -O2 -o RDMA_RC_example RDMA_RC_example.c -lmlx5 -libverbs
*/
/******************************************************************************
*
* RDMA Aware Networks Programming Example
*
* This code demonstrates how to perform the following operations using 
* the * VPI Verbs API:
* Send
* Receive
* RDMA Read
* RDMA Write
*
*****************************************************************************/
/*
 * 程序总览
 * ----------
 * 1. 用普通 TCP socket 交换 RDMA 建链所需的控制面信息:
 *    本端/对端缓冲区地址、rkey、QP number、LID/GID。
 * 2. 再按 RC QP 的标准状态机推进:
 *    RESET -> INIT -> RTR -> RTS。
 * 3. 建链完成后，数据面不再走 TCP，而是直接通过 verbs 发起
 *    SEND / RECV / RDMA READ / RDMA WRITE。
 * 4. 这个 Themis 版本额外支持:
 *    - 通过 mlx5 扩展接口显式设置 RoCE 外层 UDP 源端口。
 *    - 通过单独的 UDP 通知报文把 QP 连接状态同步给外部控制逻辑。
 *
 * 阅读时可以把代码分成三层:
 * - TCP 控制面: sock_connect / sock_sync_data
 * - RDMA 资源管理: resources_create / resources_destroy
 * - QP 状态推进与数据操作: modify_qp_* / post_* / main
 */
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <stdint.h>
 #include <inttypes.h>
 #include <errno.h>
 #include <endian.h>
 #include <byteswap.h>
 #include <getopt.h>
 #include <sys/time.h>
 #include <arpa/inet.h>
 #include <infiniband/verbs.h>
 #include <infiniband/mlx5dv.h>
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <netdb.h>
 #include <time.h>
 #include <rdma/ib_user_verbs.h>
 
 /* poll CQ timeout in millisec (2 seconds) */
 #define MAX_POLL_CQ_TIMEOUT 2000
 #define MSG_S "SEND operation from server"
 #define MSG_C "SEND operation from client"
 #define RDMAMSGR "RDMA read operation "
 #define RDMAMSGW "RDMA write operation"
 #define MSG_SIZE 20000
 #define BUF_SIZE 3000000
 #define QP_NOTIFY_DEFAULT_PORT 12345
 #define ROCE_UDP_SPORT_BASE 0xC000
 #define ROCE_UDP_SPORT_QPN_MASK 0x3FFF
 #if __BYTE_ORDER == __LITTLE_ENDIAN
 static inline uint64_t htonll(uint64_t x)
 {
     return bswap_64(x);
 }
 static inline uint64_t ntohll(uint64_t x)
 {
     return bswap_64(x);
 }
 #elif __BYTE_ORDER == __BIG_ENDIAN
 static inline uint64_t htonll(uint64_t x)
 {
     return x;
 }
 static inline uint64_t ntohll(uint64_t x)
 {
     return x;
 }
 #else
 #error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
 #endif
 
 struct timespec start, end;
 
 /*
  * 运行参数。
  * 注意:
  * - server_name == NULL 表示当前进程以“服务端”身份启动，负责被动等待 TCP 连接。
  * - server_name != NULL 表示当前进程以“客户端”身份启动，会主动连到远端。
  * 这里的“客户端/服务端”只描述控制面的 TCP 角色，不等价于谁执行 RDMA read/write。
  */
 struct config_t
 {
     const char *dev_name; /* IB device name */
     char *server_name;    /* server host name */
     uint32_t tcp_port;    /* server TCP port */
     int ib_port;          /* local IB port to work with */
     int gid_idx;          /* gid index to use */
     int udp_sport;        /* udp src port */
     int udp_sport_from_qpn; /* derive RoCE UDP source port from local QPN */
     int udp_sport_via_flow_label; /* fallback: derive RoCE UDP sport from GRH flow_label */
     const char *qp_notify_host; /* host that receives QP connection updates */
     uint32_t qp_notify_port;    /* UDP port for QP connection updates */
     uint32_t poll_timeout_ms;   /* CQ poll timeout in milliseconds */
     size_t msg_size;            /* RDMA operation length in bytes */
     size_t buf_size;            /* registered MR buffer size in bytes */
 };
 
 /*
  * TCP 控制面交换的最小建链信息。
  * 有了这些字段，本端就能把 QP 从 INIT 推到 RTR/RTS，并在 RDMA read/write
  * 时填好远端地址和 rkey。
  */
 struct cm_con_data_t
 {
     uint64_t addr;        /* Buffer address */
     uint32_t rkey;        /* Remote key */
     uint32_t qp_num;      /* QP number */
     uint16_t lid;         /* LID of the IB port */
     uint8_t gid[16];      /* gid */
 } __attribute__((packed));
 
 /*
  * 这是软件侧维护的一份“连接视图”，用于日志和外部通知。
  * 它不影响硬件真正的 QP 状态，只是帮助我们理解当前代码推进到了哪一步。
  */
 enum qp_connection_state
 {
     QP_CONN_EMPTY = 0,
     QP_CONN_CREATED,
     QP_CONN_REMOTE_KNOWN,
     QP_CONN_INIT,
     QP_CONN_RTR,
     QP_CONN_RTS,
     QP_CONN_CLOSED
 };
 
 /*
  * 单条 RC 连接的软件侧缓存。
  * 这里把后续会反复用到的远端属性缓存下来，避免每次发 WR 时再到别处查。
  */
 struct qp_connection
 {
     uint32_t local_qpn;
     uint32_t remote_qpn;
     uint32_t remote_ipv4;
     uint64_t remote_addr;
     uint32_t remote_rkey;
     uint16_t remote_lid;
     uint8_t remote_gid[16];
     enum qp_connection_state state;
     int notified_up;
 };
 
 struct qp_notify_msg
 {
     uint32_t dIP;
     uint32_t cQPN;
     uint32_t sQPN;
     uint32_t flag;
 } __attribute__((packed));
 
 /*
  * 一个 RDMA 会话实际占用的核心资源。
  * 生命周期基本一致，所以集中放到一个结构体里，便于创建、出错清理和最终释放。
  */
 struct resources
 {
     struct ibv_device_attr device_attr; /* Device attributes */
     struct ibv_port_attr port_attr;     /* IB port attributes */
     struct cm_con_data_t remote_props;  /* values to connect to remote side */
     struct qp_connection qp_conn;       /* maintained QP connection state */
     struct ibv_context *ib_ctx;         /* device handle */
     struct ibv_pd *pd;                  /* PD handle */
     struct ibv_cq *cq;                  /* CQ handle */
     struct ibv_qp *qp;                  /* QP handle */
     struct ibv_mr *mr;                  /* MR handle for buf */
     char *buf;                          /* memory buffer pointer, used for RDMA and send ops */
     int sock;                           /* TCP socket file descriptor */
 };
 
 struct config_t config =
 {
     NULL,  /* dev_name */
     NULL,  /* server_name */
     19875, /* tcp_port */
     1,     /* ib_port */
     -1,    /* gid_idx */
     0,     /* udp_sport */
     0,     /* udp_sport_from_qpn */
     0,     /* udp_sport_via_flow_label */
     NULL,  /* qp_notify_host */
     QP_NOTIFY_DEFAULT_PORT, /* qp_notify_port */
     MAX_POLL_CQ_TIMEOUT, /* poll_timeout_ms */
     MSG_SIZE, /* msg_size */
     BUF_SIZE /* buf_size */
 };
 
 /* 仅用于把内部枚举打印成人类可读字符串，方便跟踪状态推进。 */
 static const char *qp_connection_state_str(enum qp_connection_state state)
 {
     switch(state)
     {
     case QP_CONN_EMPTY:
         return "EMPTY";
     case QP_CONN_CREATED:
         return "CREATED";
     case QP_CONN_REMOTE_KNOWN:
         return "REMOTE_KNOWN";
     case QP_CONN_INIT:
         return "INIT";
     case QP_CONN_RTR:
         return "RTR";
     case QP_CONN_RTS:
         return "RTS";
     case QP_CONN_CLOSED:
         return "CLOSED";
     default:
         return "UNKNOWN";
     }
 }
 
 static const char *role_str(void)
 {
     return config.server_name ? "client" : "server";
 }
 
 static const char *wr_opcode_str(int opcode)
 {
     switch(opcode)
     {
     case IBV_WR_SEND:
         return "IBV_WR_SEND";
     case IBV_WR_RDMA_READ:
         return "IBV_WR_RDMA_READ";
     case IBV_WR_RDMA_WRITE:
         return "IBV_WR_RDMA_WRITE";
     default:
         return "UNKNOWN_WR_OPCODE";
     }
 }
 
 static const char *wc_opcode_str(enum ibv_wc_opcode opcode)
 {
     switch(opcode)
     {
     case IBV_WC_SEND:
         return "IBV_WC_SEND";
     case IBV_WC_RDMA_WRITE:
         return "IBV_WC_RDMA_WRITE";
     case IBV_WC_RDMA_READ:
         return "IBV_WC_RDMA_READ";
     case IBV_WC_RECV:
         return "IBV_WC_RECV";
     case IBV_WC_RECV_RDMA_WITH_IMM:
         return "IBV_WC_RECV_RDMA_WITH_IMM";
     default:
         return "UNKNOWN_WC_OPCODE";
     }
 }
 
 static const char *qp_state_str(enum ibv_qp_state state)
 {
     switch(state)
     {
     case IBV_QPS_RESET:
         return "RESET";
     case IBV_QPS_INIT:
         return "INIT";
     case IBV_QPS_RTR:
         return "RTR";
     case IBV_QPS_RTS:
         return "RTS";
     case IBV_QPS_SQD:
         return "SQD";
     case IBV_QPS_SQE:
         return "SQE";
     case IBV_QPS_ERR:
         return "ERR";
     default:
         return "UNKNOWN";
     }
 }
 
 static void print_gid_bytes(const char *label, const uint8_t gid[16])
 {
     fprintf(stdout,
             "%s%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
             label,
             gid[0], gid[1], gid[2], gid[3],
             gid[4], gid[5], gid[6], gid[7],
             gid[8], gid[9], gid[10], gid[11],
             gid[12], gid[13], gid[14], gid[15]);
 }
 
 static void dump_qp_attr(struct resources *res, const char *where)
 {
     struct ibv_qp_attr attr;
     struct ibv_qp_init_attr init_attr;
     int flags = IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                 IBV_QP_RQ_PSN | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
                 IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                 IBV_QP_MAX_QP_RD_ATOMIC | IBV_QP_MAX_DEST_RD_ATOMIC |
                 IBV_QP_MIN_RNR_TIMER | IBV_QP_AV;
     int rc;
 
     if(!res || !res->qp)
     {
         fprintf(stdout, "[%s][%s] QP dump skipped: QP is not available\n",
                 role_str(), where);
         return;
     }
 
     memset(&attr, 0, sizeof(attr));
     memset(&init_attr, 0, sizeof(init_attr));
     rc = ibv_query_qp(res->qp, &attr, flags, &init_attr);
     if(rc)
     {
         fprintf(stderr, "[%s][%s] ibv_query_qp failed: rc=%d errno=%d (%s)\n",
                 role_str(), where, rc, errno, strerror(errno));
         return;
     }
 
     fprintf(stdout,
             "[%s][%s] QP attr: qpn=0x%x state=%s path_mtu=%d dest_qpn=0x%x sq_psn=%u rq_psn=%u timeout=0x%x retry_cnt=%u rnr_retry=%u min_rnr_timer=0x%x max_rd_atomic=%u max_dest_rd_atomic=%u port=%u is_global=%u\n",
             role_str(),
             where,
             res->qp->qp_num,
             qp_state_str(attr.qp_state),
             attr.path_mtu,
             attr.dest_qp_num,
             attr.sq_psn,
             attr.rq_psn,
             attr.timeout,
             attr.retry_cnt,
             attr.rnr_retry,
             attr.min_rnr_timer,
             attr.max_rd_atomic,
             attr.max_dest_rd_atomic,
             attr.ah_attr.port_num,
             attr.ah_attr.is_global);
     if(attr.ah_attr.is_global)
     {
         print_gid_bytes("  QP dgid=", (const uint8_t *)&attr.ah_attr.grh.dgid);
         fprintf(stdout,
                 "  QP grh: sgid_index=%u flow_label=0x%x hop_limit=%u traffic_class=0x%x\n",
                 attr.ah_attr.grh.sgid_index,
                 attr.ah_attr.grh.flow_label,
                 attr.ah_attr.grh.hop_limit,
                 attr.ah_attr.grh.traffic_class);
     }
 }
 
 static void qp_connection_reset(struct qp_connection *conn)
 {
     /* 逻辑上的“断连/未初始化”状态，与硬件 QP 是否存在是两个概念。 */
     memset(conn, 0, sizeof(*conn));
     conn->state = QP_CONN_EMPTY;
 }
 
 static uint32_t ipv4_from_gid(const uint8_t gid[16])
 {
     uint64_t interface_id;
     uint32_t encoded_ipv4;
 
     /*
      * 这里默认使用当前实验环境里的 RoCEv2 GID 编码方式:
      * 从 GID 后 64 bit 中提取出带有 IPv4 信息的部分，再转回主机字节序。
      * 这不是通用的 IB/RoCE 规范解析器，而是面向当前实验拓扑的辅助函数。
      */
     memcpy(&interface_id, gid + 8, sizeof(interface_id));
     encoded_ipv4 = (uint32_t)(interface_id >> 32);
     return ntohl(encoded_ipv4);
 }
 
 static const char *ipv4_to_string(uint32_t ipv4, char *buf, size_t len)
 {
     struct in_addr addr;
 
     addr.s_addr = htonl(ipv4);
     if(!inet_ntop(AF_INET, &addr, buf, len))
     {
         snprintf(buf, len, "0x%08x", ipv4);
     }
     return buf;
 }
 
 static uint16_t udp_sport_from_qpn(uint32_t qpn)
 {
     /*
      * 在当前实验里，用 RoCE UDP 源端口的低 14 bit 编码本地 QPN[16:3]，
      * 这样交换机/P4 侧可以不解析 BTH 就恢复出发送端 QP 标识。
      */
     return (uint16_t)(ROCE_UDP_SPORT_BASE |
                       ((qpn >> 3) & ROCE_UDP_SPORT_QPN_MASK));
 }
 
 static int apply_qp_udp_sport(struct ibv_qp *qp, uint16_t udp_sport)
 {
     int rc;
     int saved_errno;
 
     /*
      * 这是 Mellanox/NVIDIA mlx5 的厂商扩展，不属于通用 verbs。
      * 它修改的是 RoCE 封装时外层 UDP 源端口，常用于与网内观测/转发逻辑配合。
      */
     errno = 0;
     rc = mlx5dv_modify_qp_udp_sport(qp, udp_sport);
     saved_errno = errno;
     if(rc)
     {
         fprintf(stderr,
                 "mlx5dv_modify_qp_udp_sport failed: qpn=0x%x udp_sport=%u low14=%u rc=%d (%s) errno=%d (%s)\n",
                 qp->qp_num,
                 udp_sport,
                 udp_sport & ROCE_UDP_SPORT_QPN_MASK,
                 rc,
                 strerror(rc),
                 saved_errno,
                 saved_errno ? strerror(saved_errno) : "not set");
         if(rc == EOPNOTSUPP)
         {
             fprintf(stderr,
                     "mlx5dv_modify_qp_udp_sport is not supported by this rdma-core/provider/kernel/firmware path; falling back to GRH flow_label encoding if RoCE uses ibv_flow_label_to_udp_sport().\n");
         }
         return rc;
     }
 
     fprintf(stdout,
             "RoCE UDP source port was set by mlx5dv_modify_qp_udp_sport: qpn=0x%x udp_sport=%u low14=%u\n",
             qp->qp_num,
             udp_sport,
             udp_sport & ROCE_UDP_SPORT_QPN_MASK);
     return 0;
 }
 
 static void qp_connection_set_state(struct resources *res, enum qp_connection_state state)
 {
     res->qp_conn.state = state;
 }
 
 static void qp_connection_set_created(struct resources *res)
 {
     /* QP 创建成功后，先记录本地 QPN；远端属性要等 TCP 交换完成后才知道。 */
     qp_connection_reset(&res->qp_conn);
     res->qp_conn.local_qpn = res->qp->qp_num;
     res->qp_conn.state = QP_CONN_CREATED;
 }
 
 static void qp_connection_set_remote(struct resources *res, const struct cm_con_data_t *remote_con_data)
 {
     /* 这份缓存既服务于 RDMA WR，也服务于外部 QP 通知报文。 */
     res->qp_conn.remote_addr = remote_con_data->addr;
     res->qp_conn.remote_rkey = remote_con_data->rkey;
     res->qp_conn.remote_qpn = remote_con_data->qp_num;
     res->qp_conn.remote_lid = remote_con_data->lid;
     memcpy(res->qp_conn.remote_gid, remote_con_data->gid, sizeof(res->qp_conn.remote_gid));
     res->qp_conn.remote_ipv4 = ipv4_from_gid(remote_con_data->gid);
     res->qp_conn.state = QP_CONN_REMOTE_KNOWN;
 }
 
 static void qp_connection_print(const struct resources *res, const char *prefix)
 {
     char ip_buf[INET_ADDRSTRLEN];
 
     fprintf(stdout,
             "%s QP connection: state=%s local_qpn=0x%x remote_qpn=0x%x remote_ipv4=%s remote_lid=0x%x\n",
             prefix,
             qp_connection_state_str(res->qp_conn.state),
             res->qp_conn.local_qpn,
             res->qp_conn.remote_qpn,
             ipv4_to_string(res->qp_conn.remote_ipv4, ip_buf, sizeof(ip_buf)),
             res->qp_conn.remote_lid);
 }
 
 static int send_qp_connection_update(struct resources *res, uint32_t flag)
 {
     struct addrinfo hints;
     struct addrinfo *resolved_addr = NULL;
     struct addrinfo *iterator;
     struct qp_notify_msg msg;
     char service[16];
     int sockfd = -1;
     int rc = 0;
 
     /*
      * 这是“旁路通知”，不是 RDMA 建链的一部分。
      * 即使通知失败，也不会自动回滚前面已经成功创建的 QP。
      */
     if(!config.qp_notify_host)
     {
         return 0;
     }
 
     snprintf(service, sizeof(service), "%u", config.qp_notify_port);
     memset(&hints, 0, sizeof(hints));
     hints.ai_family = AF_INET;
     hints.ai_socktype = SOCK_DGRAM;
 
     rc = getaddrinfo(config.qp_notify_host, service, &hints, &resolved_addr);
     if(rc)
     {
         fprintf(stderr, "failed to resolve QP notify target %s:%s: %s\n",
                 config.qp_notify_host, service, gai_strerror(rc));
         return -1;
     }
 
     msg.dIP = htonl(res->qp_conn.remote_ipv4);
     msg.cQPN = htonl(res->qp_conn.local_qpn);
     msg.sQPN = htonl(res->qp_conn.remote_qpn);
     msg.flag = htonl(flag);
 
     rc = -1;
     for(iterator = resolved_addr; iterator; iterator = iterator->ai_next)
     {
         sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
         if(sockfd < 0)
         {
             continue;
         }
 
         if(sendto(sockfd, &msg, sizeof(msg), 0, iterator->ai_addr, iterator->ai_addrlen) == (ssize_t)sizeof(msg))
         {
             rc = 0;
             break;
         }
 
         close(sockfd);
         sockfd = -1;
     }
 
     if(sockfd >= 0)
     {
         close(sockfd);
     }
     freeaddrinfo(resolved_addr);
 
     if(rc)
     {
         fprintf(stderr, "failed to send QP notify packet to %s:%u\n",
                 config.qp_notify_host, config.qp_notify_port);
     }
     else
     {
         char ip_buf[INET_ADDRSTRLEN];
         fprintf(stdout,
                 "sent QP notify: dIP=%s cQPN=%u sQPN=%u flag=%u to %s:%u\n",
                 ipv4_to_string(res->qp_conn.remote_ipv4, ip_buf, sizeof(ip_buf)),
                 res->qp_conn.local_qpn,
                 res->qp_conn.remote_qpn,
                 flag,
                 config.qp_notify_host,
                 config.qp_notify_port);
     }
     return rc;
 }
 
 /******************************************************************************
 Socket operations:
 For simplicity, the example program uses TCP sockets to exchange control
 information. If a TCP/IP stack/connection is not available, connection manager
 (CM) may be used to pass this information. Use of CM is beyond the scope of
 this example
 ******************************************************************************/
 /******************************************************************************
 * Function: sock_connect
 * Input:
 * servername: URL of server to connect to (NULL for server mode)
 * port: port of service
 *
 * Output:none
 *
 * Returns: socket (fd) on success, negative error code on failure
 *
 * Description:
 * Connect a socket. If servername is specified a client connection will be
 * initiated to the indicated server and port. Otherwise listen on the
 * indicated port for an incoming connection.
 *
 ******************************************************************************/
 static int sock_connect(const char *servername, int port)
 {
     struct addrinfo *resolved_addr = NULL;
     struct addrinfo *iterator;
     char service[6];
     int sockfd = -1;
     int listenfd = 0;
     int tmp;
     struct addrinfo hints =
     {
         .ai_flags    = AI_PASSIVE,
         .ai_family   = AF_INET,
         .ai_socktype = SOCK_STREAM
     };
 
     if(sprintf(service, "%d", port) < 0)
     {
         goto sock_connect_exit;
     }
 
     /* 控制面只使用 IPv4/TCP；真正的数据面仍走 RDMA。 */
     /* Resolve DNS address, use sockfd as temp storage */
     sockfd = getaddrinfo(servername, service, &hints, &resolved_addr);
     if(sockfd < 0)
     {
         fprintf(stderr, "%s for %s:%d\n", gai_strerror(sockfd), servername, port);
         goto sock_connect_exit;
     }
 
     /*
      * 逐个尝试解析结果。
      * 客户端模式下主动 connect；服务端模式下 bind/listen/accept。
      */
     /* Search through results and find the one we want */
     for(iterator = resolved_addr; iterator ; iterator = iterator->ai_next)
     {
         sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
         if(sockfd >= 0)
         {
             if(servername)
             {
                 /* Client mode. Initiate connection to remote */
                 if((tmp=connect(sockfd, iterator->ai_addr, iterator->ai_addrlen)))
                 {
                     fprintf(stdout, "failed connect \n");
                     close(sockfd);
                     sockfd = -1;
                 }
             }
             else
             {
                 /* Server mode. Set up listening socket an accept a connection */
                 listenfd = sockfd;
                 sockfd = -1;
                 if(bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
                 {
                     goto sock_connect_exit;
                 }
                 listen(listenfd, 1);
                 sockfd = accept(listenfd, NULL, 0);
             }
         }
     }
 
 sock_connect_exit:
     if(listenfd)
     {
         close(listenfd);
     }
 
     if(resolved_addr)
     {
         freeaddrinfo(resolved_addr);
     }
 
     if(sockfd < 0)
     {
         if(servername)
         {
             fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
         }
         else
         {
             perror("server accept");
             fprintf(stderr, "accept() failed\n");
         }
     }
 
     return sockfd;
 }
 
 /******************************************************************************
 * Function: sock_sync_data
 * Input:
 * sock: socket to transfer data on
 * xfer_size: size of data to transfer
 * local_data: pointer to data to be sent to remote
 *
 * Output: remote_data pointer to buffer to receive remote data
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Description:
 * Sync data across a socket. The indicated local data will be sent to the
 * remote. It will then wait for the remote to send its data back. It is
 * assumed that the two sides are in sync and call this function in the proper
 * order. Chaos will ensue if they are not. :)
 *
 * Also note this is a blocking function and will wait for the full data to be
 * received from the remote.
 *
 ******************************************************************************/
 int sock_sync_data(int sock, int xfer_size, char *local_data, char *remote_data)
 {
     int rc;
     int read_bytes = 0;
     int total_read_bytes = 0;
     /*
      * 两端都必须在同一个逻辑检查点调用本函数，否则很容易互相阻塞。
      * 它本质上是一个“先写后读”的简易 barrier。
      */
     fprintf(stdout,
             "[%s][tcp-sync] begin: sock=%d xfer_size=%d local_first_byte=0x%02x\n",
             role_str(),
             sock,
             xfer_size,
             xfer_size > 0 ? (unsigned char)local_data[0] : 0);
     rc = write(sock, local_data, xfer_size);
 
     if(rc < xfer_size)
     {
         fprintf(stderr,
                 "[%s][tcp-sync] write failed/short: requested=%d written=%d errno=%d (%s)\n",
                 role_str(), xfer_size, rc, errno, strerror(errno));
     }
     else
     {
         fprintf(stdout, "[%s][tcp-sync] wrote %d bytes, waiting for peer\n",
                 role_str(), rc);
         rc = 0;
     }
 
     while(!rc && total_read_bytes < xfer_size)
     {
         read_bytes = read(sock, remote_data, xfer_size);
         if(read_bytes > 0)
         {
             total_read_bytes += read_bytes;
             fprintf(stdout,
                     "[%s][tcp-sync] read chunk=%d total=%d/%d first_remote_byte=0x%02x\n",
                     role_str(),
                     read_bytes,
                     total_read_bytes,
                     xfer_size,
                     (unsigned char)remote_data[0]);
         }
         else
         {
             fprintf(stderr,
                     "[%s][tcp-sync] read failed/closed: read_bytes=%d total=%d/%d errno=%d (%s)\n",
                     role_str(),
                     read_bytes,
                     total_read_bytes,
                     xfer_size,
                     errno,
                     strerror(errno));
             rc = read_bytes;
         }
     }
     if(!rc)
     {
         fprintf(stdout, "[%s][tcp-sync] complete: exchanged %d bytes\n",
                 role_str(), xfer_size);
     }
     return rc;
 }
 /******************************************************************************
 End of socket operations
 ******************************************************************************/
 
 /* poll_completion */
 /******************************************************************************
 * Function: poll_completion
 *
 * Input:
 * res: pointer to resources structure
 *
 * Output: none
 *
 * Returns: 0 on success, 1 on failure
 *
 * Description:
 * Poll the completion queue for a single event. This function will continue to
 * poll the queue until MAX_POLL_CQ_TIMEOUT milliseconds have passed.
 *
 ******************************************************************************/
 static int poll_completion(struct resources *res)
 {
     struct ibv_wc wc;
     unsigned long start_time_msec;
     unsigned long cur_time_msec;
     unsigned long last_log_msec;
     struct timeval cur_time;
     int poll_result;
     int rc = 0;
 
     /*
      * 这个示例里所有 send WR 都带 IBV_SEND_SIGNALED，CQ 容量也按最小示例配置。
      * 因此这里每次只轮询一个 CQE，足够覆盖当前程序的单步演示流程。
      */
     /* poll the completion for a while before giving up of doing it .. */
     gettimeofday(&cur_time, NULL);
     start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
     last_log_msec = start_time_msec;
     fprintf(stdout,
             "[%s][cq] polling CQ: timeout=%u ms cq=%p qpn=0x%x\n",
             role_str(),
             config.poll_timeout_ms,
             (void *)res->cq,
             res->qp ? res->qp->qp_num : 0);
     do
     {
         poll_result = ibv_poll_cq(res->cq, 1, &wc);
         gettimeofday(&cur_time, NULL);
         cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
         if(poll_result == 0 && cur_time_msec - last_log_msec >= 1000)
         {
             fprintf(stdout,
                     "[%s][cq] still waiting: elapsed=%lu/%u ms\n",
                     role_str(),
                     cur_time_msec - start_time_msec,
                     config.poll_timeout_ms);
             last_log_msec = cur_time_msec;
         }
     }
     while((poll_result == 0) && ((cur_time_msec - start_time_msec) < config.poll_timeout_ms));
 
     if(poll_result < 0)
     {
         /* poll CQ failed */
         fprintf(stderr, "[%s][cq] ibv_poll_cq failed: rc=%d errno=%d (%s)\n",
                 role_str(), poll_result, errno, strerror(errno));
         dump_qp_attr(res, "poll-error");
         rc = 1;
     }
     else if(poll_result == 0)
     {
         /* the CQ is empty */
         fprintf(stderr,
                 "[%s][cq] completion wasn't found after timeout: elapsed=%lu ms timeout=%u ms\n",
                 role_str(),
                 cur_time_msec - start_time_msec,
                 config.poll_timeout_ms);
         fprintf(stderr,
                 "[%s][cq] hint: if this is RDMA WRITE, no CQE usually means no ACK/completion yet. Check switch drops, reverse path, PFC/ECN behavior, GID/VLAN, and try --poll-timeout-ms 15000 or 30000 to wait for RC retry exhaustion.\n",
                 role_str());
         dump_qp_attr(res, "poll-timeout");
         rc = 1;
     }
     else
     {
         /* CQE found */
         fprintf(stdout,
                 "[%s][cq] CQE found: status=0x%x (%s) opcode=%s wr_id=%" PRIu64 " vendor_err=0x%x byte_len=%u qp_num=0x%x src_qp=0x%x wc_flags=0x%x pkey_index=%u slid=0x%x sl=%u dlid_path_bits=%u\n",
                 role_str(),
                 wc.status,
                 ibv_wc_status_str(wc.status),
                 wc_opcode_str(wc.opcode),
                 wc.wr_id,
                 wc.vendor_err,
                 wc.byte_len,
                 wc.qp_num,
                 wc.src_qp,
                 wc.wc_flags,
                 wc.pkey_index,
                 wc.slid,
                 wc.sl,
                 wc.dlid_path_bits);
         /* check the completion status (here we don't care about the completion opcode */
         if(wc.status != IBV_WC_SUCCESS)
         {
             fprintf(stderr,
                     "[%s][cq] bad completion: status=0x%x (%s), vendor syndrome=0x%x\n",
                     role_str(),
                     wc.status,
                     ibv_wc_status_str(wc.status),
                     wc.vendor_err);
             dump_qp_attr(res, "bad-completion");
             rc = 1;
         }
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: post_send
 *
 * Input:
 * res: pointer to resources structure
 * opcode: IBV_WR_SEND, IBV_WR_RDMA_READ or IBV_WR_RDMA_WRITE
 *
 * Output: none
 *
 * Returns: 0 on success, error code on failure
 *
 * Description: This function will create and post a send work request
 ******************************************************************************/
 static int post_send(struct resources *res, int opcode)
 {
     struct ibv_send_wr sr;
     struct ibv_sge sge;
     struct ibv_send_wr *bad_wr = NULL;
     int rc;
 
     /*
      * verbs 层真正描述“本地缓冲区”的是 SGE:
      * - addr: 本地虚拟地址
      * - length: 本次操作访问的字节数
      * - lkey: 本地 MR 的权限令牌
      *
      * 对 SEND 和 RDMA WRITE 来说，这块缓冲区是“源”。
      * 对 RDMA READ 来说，这块缓冲区是“目的地”。
      */
     /* prepare the scatter/gather entry */
     memset(&sge, 0, sizeof(sge));
     sge.addr = (uintptr_t)res->buf;
     sge.length = (uint32_t)config.msg_size;
     sge.lkey = res->mr->lkey;
 
     /* prepare the send work request */
     memset(&sr, 0, sizeof(sr));
     sr.next = NULL;
     sr.wr_id = 0;
     sr.sg_list = &sge;
     sr.num_sge = 1;
     sr.opcode = opcode;
     sr.send_flags = IBV_SEND_SIGNALED;
     if(opcode != IBV_WR_SEND)
     {
         /*
          * RDMA READ/WRITE 不依赖远端 CPU 配合，但必须知道远端 MR 的地址和 rkey。
          * 这两个字段正是前面通过 TCP 交换得到并缓存到 remote_props 里的。
          */
         sr.wr.rdma.remote_addr = res->remote_props.addr;
         sr.wr.rdma.rkey = res->remote_props.rkey;
     }
 
     fprintf(stdout,
             "[%s][post-send] opcode=%s local_qpn=0x%x remote_qpn=0x%x sge_addr=0x%" PRIx64 " sge_len=%u lkey=0x%x send_flags=0x%x\n",
             role_str(),
             wr_opcode_str(opcode),
             res->qp ? res->qp->qp_num : 0,
             res->remote_props.qp_num,
             (uint64_t)sge.addr,
             sge.length,
             sge.lkey,
             sr.send_flags);
     if(opcode != IBV_WR_SEND)
     {
         fprintf(stdout,
                 "[%s][post-send] RDMA target: remote_addr=0x%" PRIx64 " rkey=0x%x remote_lid=0x%x\n",
                 role_str(),
                 sr.wr.rdma.remote_addr,
                 sr.wr.rdma.rkey,
                 res->remote_props.lid);
         print_gid_bytes("  remote_gid=", res->remote_props.gid);
     }
     dump_qp_attr(res, "before-post-send");
 
     /* there is a Receive Request in the responder side, so we won't get any into RNR flow */
     rc = ibv_post_send(res->qp, &sr, &bad_wr);
     if(rc)
     {
         fprintf(stderr,
                 "[%s][post-send] ibv_post_send failed: rc=%d errno=%d (%s) bad_wr=%p\n",
                 role_str(), rc, errno, strerror(errno), (void *)bad_wr);
     }
     else
     {
         switch(opcode)
         {
         case IBV_WR_SEND:
             fprintf(stdout, "Send Request was posted\n");
             break;
         case IBV_WR_RDMA_READ:
             fprintf(stdout, "RDMA Read Request was posted\n");
             break;
         case IBV_WR_RDMA_WRITE:
             fprintf(stdout, "RDMA Write Request was posted\n");
             break;
         default:
             fprintf(stdout, "Unknown Request was posted\n");
             break;
         }
         fprintf(stdout, "[%s][post-send] posted successfully: opcode=%s wr_id=%" PRIu64 "\n",
                 role_str(), wr_opcode_str(opcode), sr.wr_id);
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: post_receive
 *
 * Input:
 * res: pointer to resources structure
 *
 * Output: none
 *
 * Returns: 0 on success, error code on failure
 *
 * Description: post RR to be prepared for incoming messages
 *
 ******************************************************************************/
 static int post_receive(struct resources *res)
 {
     struct ibv_recv_wr rr;
     struct ibv_sge sge;
     struct ibv_recv_wr *bad_wr;
     int rc;
 
     /* prepare the scatter/gather entry */
     memset(&sge, 0, sizeof(sge));
     sge.addr = (uintptr_t)res->buf;
     sge.length = (uint32_t)config.msg_size;
     sge.lkey = res->mr->lkey;
 
     /*
      * 只有 SEND/RECV 语义需要提前在 RQ 上挂 RR。
      * 纯 RDMA READ/WRITE 直接命中远端 MR，不消耗远端 RQ。
      */
     /* prepare the receive work request */
     memset(&rr, 0, sizeof(rr));
     rr.next = NULL;
     rr.wr_id = 0;
     rr.sg_list = &sge;
     rr.num_sge = 1;
 
     /* post the Receive Request to the RQ */
     rc = ibv_post_recv(res->qp, &rr, &bad_wr);
     if(rc)
     {
         fprintf(stderr,
                 "[%s][post-recv] ibv_post_recv failed: rc=%d errno=%d (%s) bad_wr=%p\n",
                 role_str(), rc, errno, strerror(errno), (void *)bad_wr);
     }
     else
     {
         fprintf(stdout,
                 "[%s][post-recv] Receive Request was posted: qpn=0x%x addr=0x%" PRIx64 " len=%u lkey=0x%x\n",
                 role_str(),
                 res->qp ? res->qp->qp_num : 0,
                 (uint64_t)sge.addr,
                 sge.length,
                 sge.lkey);
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: resources_init
 *
 * Input:
 * res: pointer to resources structure
 *
 * Output: res is initialized
 *
 * Returns: none
 *
 * Description: res is initialized to default values
 ******************************************************************************/
 static void resources_init(struct resources *res)
 {
     /* 统一清零后，析构函数就能安全地按“是否为 NULL/-1”判断资源是否需要释放。 */
     memset(res, 0, sizeof *res);
     qp_connection_reset(&res->qp_conn);
     res->sock = -1;
 }
 
 /******************************************************************************
 * Function: resources_create
 *
 * Input: res pointer to resources structure to be filled in
 *
 * Output: res filled in with resources
 *
 * Returns: 0 on success, 1 on failure
 *
 * Description:
 * This function creates and allocates all necessary system resources. These
 * are stored in res.
 *****************************************************************************/
 static int resources_create(struct resources *res)
 {
     struct ibv_device **dev_list = NULL;
     struct ibv_qp_init_attr qp_init_attr;
     struct ibv_device *ib_dev = NULL;
     size_t size;
     int i;
     int mr_flags = 0;
     int cq_size = 0;
     int num_devices;
     int rc = 0;
 
     /*
      * 创建顺序基本遵循依赖关系:
      * 1. 先建立 TCP 控制面连接
      * 2. 打开 RDMA 设备并查询属性
      * 3. 创建 PD / CQ / 用户缓冲区 / MR
      * 4. 创建 RC QP
      * 5. 如有需要，再应用 RoCE UDP 源端口策略
      */
 
     /* if client side */
     if(config.server_name)
     {
         res->sock = sock_connect(config.server_name, config.tcp_port);
         if(res->sock < 0)
         {
             fprintf(stderr, "failed to establish TCP connection to server %s, port %d\n",
                     config.server_name, config.tcp_port);
             rc = -1;
             goto resources_create_exit;
         }
     }
     else
     {
         fprintf(stdout, "waiting on port %d for TCP connection\n", config.tcp_port);
         res->sock = sock_connect(NULL, config.tcp_port);
         if(res->sock < 0)
         {
             fprintf(stderr, "failed to establish TCP connection with client on port %d\n",
                     config.tcp_port);
             rc = -1;
             goto resources_create_exit;
         }
     }
     fprintf(stdout, "TCP connection was established\n");
     fprintf(stdout, "searching for IB devices in host\n");
 
     /* get device names in the system */
     dev_list = ibv_get_device_list(&num_devices);
     if(!dev_list)
     {
         fprintf(stderr, "failed to get IB devices list\n");
         rc = 1;
         goto resources_create_exit;
     }
 
     /* if there isn't any IB device in host */
     if(!num_devices)
     {
         fprintf(stderr, "found %d device(s)\n", num_devices);
         rc = 1;
         goto resources_create_exit;
     }
     fprintf(stdout, "found %d device(s)\n", num_devices);
 
     /* search for the specific device we want to work with */
     for(i = 0; i < num_devices; i ++)
     {
         if(!config.dev_name)
         {
             config.dev_name = strdup(ibv_get_device_name(dev_list[i]));
             fprintf(stdout, "device not specified, using first one found: %s\n", config.dev_name);
         }
         /* find the specific device */
         if(!strcmp(ibv_get_device_name(dev_list[i]), config.dev_name))
         {
             ib_dev = dev_list[i];
             break;
         }
     }
 
     /* if the device wasn't found in host */
     if(!ib_dev)
     {
         fprintf(stderr, "IB device %s wasn't found\n", config.dev_name);
         rc = 1;
         goto resources_create_exit;
     }
 
     /* get device handle */
     res->ib_ctx = ibv_open_device(ib_dev);
     if(!res->ib_ctx)
     {
         fprintf(stderr, "failed to open device %s\n", config.dev_name);
         rc = 1;
         goto resources_create_exit;
     }
 
     /* We are now done with device list, free it */
     ibv_free_device_list(dev_list);
     dev_list = NULL;
     ib_dev = NULL;
 
     /* query device attributions */
     if (ibv_query_device(res->ib_ctx, &res->device_attr))
     {
         fprintf(stderr, "ibv_query_device on device %s failed\n", config.dev_name);
         rc = 1;
         goto resources_create_exit;
     }
     fprintf(stdout, "dev.max_qp = %d\n", res->device_attr.max_qp);
 
     /* query port properties */
     if(ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr))
     {
         fprintf(stderr, "ibv_query_port on port %u failed\n", config.ib_port);
         rc = 1;
         goto resources_create_exit;
     }
     fprintf(stdout,
             "[%s][resources] port attr: port=%u state=%u phys_state=%u lid=0x%x link_layer=%u active_mtu=%u max_mtu=%u gid_tbl_len=%u active_width=%u active_speed=%u\n",
             role_str(),
             config.ib_port,
             res->port_attr.state,
             res->port_attr.phys_state,
             res->port_attr.lid,
             res->port_attr.link_layer,
             res->port_attr.active_mtu,
             res->port_attr.max_mtu,
             res->port_attr.gid_tbl_len,
             res->port_attr.active_width,
             res->port_attr.active_speed);
 
     /* allocate Protection Domain */
     res->pd = ibv_alloc_pd(res->ib_ctx);
     if(!res->pd)
     {
         fprintf(stderr, "ibv_alloc_pd failed\n");
         rc = 1;
         goto resources_create_exit;
     }
 
     /* 演示程序一次只推进一步，所以 CQ 深度配到 1 即可。 */
     /* each side will send only one WR, so Completion Queue with 1 entry is enough */
     cq_size = 1;
     res->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
     if(!res->cq)
     {
         fprintf(stderr, "failed to create CQ with %u entries\n", cq_size);
         rc = 1;
         goto resources_create_exit;
     }
 
     /*
      * 这块用户态缓冲区同时承担三种角色:
      * - SEND/RECV 的消息体
      * - RDMA READ 的本地落地区
      * - RDMA WRITE 的本地源数据
      */
     /* allocate the memory buffer that will hold the data */
     size = config.buf_size;
     res->buf = (char *) malloc(size);
     if(!res->buf)
     {
         fprintf(stderr, "failed to malloc %zu bytes to memory buffer\n", size);
         rc = 1;
         goto resources_create_exit;
     }
     memset(res->buf, 0 , size);
 
     /* only in the server side put the message in the memory buffer */
     if(!config.server_name)
     {
         fprintf(stdout, "fill the buffer with '%s'\n", MSG_S);
         strcpy(res->buf, MSG_S);
         fprintf(stdout, "going to send the message: '%s'\n", res->buf);
     }
     else
     {
         memset(res->buf, 0, size);
     }
 
     /*
      * 访问权限决定远端能对这块 MR 做什么:
      * - LOCAL_WRITE: 本地 CPU / HCA 可以写
      * - REMOTE_READ: 允许远端发起 RDMA READ
      * - REMOTE_WRITE: 允许远端发起 RDMA WRITE
      */
     /* register the memory buffer */
     mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE ;
     res->mr = ibv_reg_mr(res->pd, res->buf, size, mr_flags);
     if(!res->mr)
     {
         fprintf(stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
         rc = 1;
         goto resources_create_exit;
     }
     fprintf(stdout, "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
             res->buf, res->mr->lkey, res->mr->rkey, mr_flags);
 
     /*
      * 这里只是“创建”QPN 和发送/接收队列对象，还没有网络可达性。
      * 真正可通信，要等后面的 INIT/RTR/RTS 状态推进完成。
      */
     /* create the Queue Pair */
     memset(&qp_init_attr, 0, sizeof(qp_init_attr));
     qp_init_attr.qp_type = IBV_QPT_RC;
     qp_init_attr.sq_sig_all = 1;
     qp_init_attr.send_cq = res->cq;
     qp_init_attr.recv_cq = res->cq;
     qp_init_attr.cap.max_send_wr = 1;
     qp_init_attr.cap.max_recv_wr = 1;
     qp_init_attr.cap.max_send_sge = 1;
     qp_init_attr.cap.max_recv_sge = 1;
     res->qp = ibv_create_qp(res->pd, &qp_init_attr);
     if(!res->qp)
     {
         fprintf(stderr, "failed to create QP\n");
         rc = 1;
         goto resources_create_exit;
     }
     fprintf(stdout, "QP was created, QP number=0x%x\n", res->qp->qp_num);
     qp_connection_set_created(res);
     dump_qp_attr(res, "after-create");
     if(config.udp_sport_from_qpn)
     {
         /* 必须在拿到本地 QPN 之后，才能按约定把 QPN 编进 UDP 源端口。 */
         config.udp_sport = udp_sport_from_qpn(res->qp->qp_num);
         fprintf(stdout,
                 "UDP source port was derived from local QPN: 0x%x -> 0x%x, P4 encoded sQPN[16:3]=%u\n",
                 res->qp->qp_num,
                 config.udp_sport,
                 config.udp_sport & ROCE_UDP_SPORT_QPN_MASK);
     }
     if(config.udp_sport)
     {
         fprintf(stdout,
                 "RoCE UDP source port target is %u (0x%x); it will be encoded in QP path attributes during QP connection.\n",
                 config.udp_sport,
                 config.udp_sport);
     }
 
 resources_create_exit:
     if(rc)
     {
         /* Error encountered, cleanup */
         if(res->qp)
         {
             ibv_destroy_qp(res->qp);
             res->qp = NULL;
         }
         if(res->mr)
         {
             ibv_dereg_mr(res->mr);
             res->mr = NULL;
         }
         if(res->buf)
         {
             free(res->buf);
             res->buf = NULL;
         }
         if(res->cq)
         {
             ibv_destroy_cq(res->cq);
             res->cq = NULL;
         }
         if(res->pd)
         {
             ibv_dealloc_pd(res->pd);
             res->pd = NULL;
         }
         if(res->ib_ctx)
         {
             ibv_close_device(res->ib_ctx);
             res->ib_ctx = NULL;
         }
         if(dev_list)
         {
             ibv_free_device_list(dev_list);
             dev_list = NULL;
         }
         if(res->sock >= 0)
         {
             if(close(res->sock))
             {
                 fprintf(stderr, "failed to close socket\n");
             }
             res->sock = -1;
         }
     }
     return rc;
 }
 
 
 /******************************************************************************
 * Function: modify_qp_to_init
 *
 * Input:
 * qp: QP to transition
 *
 * Output: none
 *
 * Returns: 0 on success, ibv_modify_qp failure code on failure
 *
 * Description: Transition a QP from the RESET to INIT state
 ******************************************************************************/
 static int modify_qp_to_init(struct ibv_qp *qp)
 {
     struct ibv_qp_attr attr;
     int flags;
     int rc;
 
     /*
      * INIT 阶段主要声明“这个 QP 允许哪些访问语义”。
      * 后面如果远端要对本端 MR 做 RDMA READ/WRITE，本地 QP 侧必须先允许。
      */
     memset(&attr, 0, sizeof(attr));
     attr.qp_state = IBV_QPS_INIT;
     attr.port_num = config.ib_port;
     attr.pkey_index = 0;
     attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
     flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
     rc = ibv_modify_qp(qp, &attr, flags);
     if(rc)
     {
         fprintf(stderr, "failed to modify QP state to INIT\n");
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: modify_qp_to_rtr
 *
 * Input:
 * qp: QP to transition
 * remote_qpn: remote QP number
 * dlid: destination LID
 * dgid: destination GID (mandatory for RoCEE)
 *
 * Output: none
 *
 * Returns: 0 on success, ibv_modify_qp failure code on failure
 *
 * Description: 
 * Transition a QP from the INIT to RTR state, using the specified QP number
 ******************************************************************************/
 static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *dgid)
 {
     struct ibv_qp_attr attr;
     int flags;
     int rc;
 
     /*
      * RTR 阶段要把“对面是谁、路径怎么走”说清楚:
      * - dest_qp_num: 对端 QPN
      * - ah_attr: 如何把包送到对端
      * - max_dest_rd_atomic: 允许对端并发向我发起多少个 RDMA READ/原子操作
      */
     memset(&attr, 0, sizeof(attr));
     attr.qp_state = IBV_QPS_RTR;
     attr.path_mtu = IBV_MTU_256; /* this field specifies the MTU from source code*/
     attr.dest_qp_num = remote_qpn;
     attr.rq_psn = 0;
     attr.max_dest_rd_atomic = 1;
     attr.min_rnr_timer = 0x12;
     attr.ah_attr.is_global = 0;
     attr.ah_attr.dlid = dlid;
     attr.ah_attr.sl = 0;
     attr.ah_attr.src_path_bits = 0;
     attr.ah_attr.port_num = config.ib_port;
     if(config.gid_idx >= 0)
     {
         attr.ah_attr.is_global = 1;
         attr.ah_attr.port_num = 1;
         memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
         /*
          * rdma-core 的通用 RoCE 路径会用 ibv_flow_label_to_udp_sport()
          * 从 GRH flow_label 派生外层 UDP source port。设置低 14 bit 即可
          * 得到 0xC000 | low14，和当前 P4 的 sQPN 编码一致。
          */
         if(config.udp_sport)
         {
             attr.ah_attr.grh.flow_label = config.udp_sport & ROCE_UDP_SPORT_QPN_MASK;
             config.udp_sport_via_flow_label = 1;
             fprintf(stdout,
                     "[%s][rtr] GRH flow_label=0x%x expects RoCE UDP sport=0x%x via ibv_flow_label_to_udp_sport(), desired=0x%x\n",
                     role_str(),
                     attr.ah_attr.grh.flow_label,
                     ibv_flow_label_to_udp_sport(attr.ah_attr.grh.flow_label),
                     config.udp_sport);
         }
         else
         {
             attr.ah_attr.grh.flow_label = 0;
         }
         attr.ah_attr.grh.hop_limit = 1;
         attr.ah_attr.grh.sgid_index = config.gid_idx;
         attr.ah_attr.grh.traffic_class = 0;
     }
 
     flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
             IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
     rc = ibv_modify_qp(qp, &attr, flags);
 
     if(rc)
     {
         fprintf(stderr, "failed to modify QP state to RTR\n");
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: modify_qp_to_rts
 *
 * Input:
 * qp: QP to transition
 *
 * Output: none
 *
 * Returns: 0 on success, ibv_modify_qp failure code on failure
 *
 * Description: Transition a QP from the RTR to RTS state
 ******************************************************************************/
 static int modify_qp_to_rts(struct ibv_qp *qp)
 {
     struct ibv_qp_attr attr;
     int flags;
     int rc;
 
     /*
      * RTS 阶段补齐发送侧参数:
      * - timeout/retry_cnt/rnr_retry: 链路与接收方未就绪时的重试策略
      * - sq_psn: 发送包序号起点
      * - max_rd_atomic: 本端最多可同时向远端发起多少个 RDMA READ/原子操作
      */
     memset(&attr, 0, sizeof(attr));
     attr.qp_state = IBV_QPS_RTS;
     attr.timeout = 0x12;
     attr.retry_cnt = 6;
     attr.rnr_retry = 0;
     attr.sq_psn = 0;
     attr.max_rd_atomic = 1;
     flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
             IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
     rc = ibv_modify_qp(qp, &attr, flags);
     if(rc)
     {
         fprintf(stderr, "failed to modify QP state to RTS\n");
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: connect_qp
 *
 * Input:
 * res: pointer to resources structure
 *
 * Output: none
 *
 * Returns: 0 on success, error code on failure
 *
 * Description: 
 * Connect the QP. Transition the server side to RTR, sender side to RTS
 ******************************************************************************/
 static int connect_qp(struct resources *res)
 {
     struct cm_con_data_t local_con_data;
     struct cm_con_data_t remote_con_data;
     struct cm_con_data_t tmp_con_data;
     int rc = 0;
     union ibv_gid my_gid;
 
     /*
      * 这一步完成两件事:
      * 1. 通过 TCP 交换远端建链信息。
      * 2. 推进本地 QP 状态机直到 RTS。
      */
     if(config.gid_idx >= 0)
     {
         rc = ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid);
         if(rc)
         {
             fprintf(stderr, "could not get gid for port %d, index %d\n", config.ib_port, config.gid_idx);
             return rc;
         }
         print_gid_bytes("[connect] Local GID = ", my_gid.raw);
         {
             char ip_buf[INET_ADDRSTRLEN];
             fprintf(stdout,
                     "[%s][connect] local GID decoded IPv4 guess=%s gid_idx=%d ib_port=%d\n",
                     role_str(),
                     ipv4_to_string(ipv4_from_gid(my_gid.raw), ip_buf, sizeof(ip_buf)),
                     config.gid_idx,
                     config.ib_port);
         }
     }
     else
     {
         memset(&my_gid, 0, sizeof my_gid);
     }
 
     /* 先把“远端如何访问我”所需的最小信息发给对端。 */
     /* exchange using TCP sockets info required to connect QPs */
     local_con_data.addr = htonll((uintptr_t)res->buf);
     local_con_data.rkey = htonl(res->mr->rkey);
     local_con_data.qp_num = htonl(res->qp->qp_num);
     local_con_data.lid = htons(res->port_attr.lid);
     memcpy(local_con_data.gid, &my_gid, 16);
     fprintf(stdout, "\nLocal LID = 0x%x\n", res->port_attr.lid);
     if(sock_sync_data(res->sock, sizeof(struct cm_con_data_t), (char *) &local_con_data, (char *) &tmp_con_data) < 0)
     {
         fprintf(stderr, "failed to exchange connection data between sides\n");
         rc = 1;
         goto connect_qp_exit;
     }
 
     remote_con_data.addr = ntohll(tmp_con_data.addr);
     remote_con_data.rkey = ntohl(tmp_con_data.rkey);
     remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
     remote_con_data.lid = ntohs(tmp_con_data.lid);
     memcpy(remote_con_data.gid, tmp_con_data.gid, 16);
 
     /* save the remote side attributes, we will need it for the post SR */
     res->remote_props = remote_con_data;
     qp_connection_set_remote(res, &remote_con_data);
     fprintf(stdout, "Remote address = 0x%"PRIx64"\n", remote_con_data.addr);
     fprintf(stdout, "Remote rkey = 0x%x\n", remote_con_data.rkey);
     fprintf(stdout, "Remote QP number = 0x%x\n", remote_con_data.qp_num);
     fprintf(stdout, "Remote LID = 0x%x\n", remote_con_data.lid);
     if(config.gid_idx >= 0)
     {
         print_gid_bytes("Remote GID = ", remote_con_data.gid);
     }
     qp_connection_print(res, "After connection data exchange");
 
     /* modify the QP to init */
     rc = modify_qp_to_init(res->qp);
     if(rc)
     {
         fprintf(stderr, "change QP state to INIT failed\n");
         goto connect_qp_exit;
     }
     qp_connection_set_state(res, QP_CONN_INIT);
     dump_qp_attr(res, "after-init");
 
     /*
      * RR 只服务于前面保留的 SEND/RECV 示例。
      * 当前实际启用的 Test4 是 RDMA WRITE，本身不要求远端预贴 RR。
      */
     /* let the client post RR to be prepared for incoming messages */
     if(config.server_name)
     {
         rc = post_receive(res);
         if(rc)
         {
             fprintf(stderr, "failed to post RR\n");
             goto connect_qp_exit;
         }
     }
 
     /* modify the QP to RTR (ready to receive) */
     rc = modify_qp_to_rtr(res->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid);
     if(rc)
     {
         fprintf(stderr, "failed to modify QP state to RTR\n");
         goto connect_qp_exit;
     }
     qp_connection_set_state(res, QP_CONN_RTR);
     dump_qp_attr(res, "after-rtr");
 
     /* modify the QP to RTS (ready to send) */
     rc = modify_qp_to_rts(res->qp);
     if(rc)
     {
         fprintf(stderr, "failed to modify QP state to RTS\n");
         goto connect_qp_exit;
     }
     qp_connection_set_state(res, QP_CONN_RTS);
     fprintf(stdout, "QP state was change to RTS\n");
     qp_connection_print(res, "QP connected");
     dump_qp_attr(res, "after-rts");
 
     if(config.udp_sport)
     {
         rc = apply_qp_udp_sport(res->qp, (uint16_t)config.udp_sport);
         if(rc)
         {
             if(rc == EOPNOTSUPP && config.udp_sport_via_flow_label)
             {
                 fprintf(stderr,
                         "[%s][udp-sport] continuing with GRH flow_label fallback; verify with tcpdump/switch counter that RoCE UDP source port is actually 0x%x.\n",
                         role_str(),
                         config.udp_sport);
                 rc = 0;
             }
             else
             {
                 fprintf(stderr,
                         "[%s][udp-sport] failed to configure RoCE UDP source port and no fallback is available\n",
                         role_str());
                 goto connect_qp_exit;
             }
         }
     }
 
     /* 可选地通知外部控制平面“这条 QP 已经可用”。 */
     if(send_qp_connection_update(res, 1) == 0)
     {
         res->qp_conn.notified_up = config.qp_notify_host != NULL;
     }
 
     /* sync to make sure that both sides are in states that they can connect to prevent packet loose */
     // if(sock_sync_data(res->sock, 1, "Q", &temp_char))  /* just send a dummy char back and forth */
     // {
     //     fprintf(stderr, "sync error after QPs are were moved to RTS\n");
     //     rc = 1;
     // }
 
 connect_qp_exit:
     return rc;
 }
 
 /******************************************************************************
 * Function: resources_destroy
 *
 * Input:
 * res: pointer to resources structure
 *
 * Output: none
 *
 * Returns: 0 on success, 1 on failure
 *
 * Description: Cleanup and deallocate all resources used
 ******************************************************************************/
 static int resources_destroy(struct resources *res)
 {
     int rc = 0;
     if(res->qp)
     {
         if(res->qp_conn.notified_up)
         {
             /* 在硬件 QP 被销毁前，先告诉外部观察者这条连接即将下线。 */
             send_qp_connection_update(res, 0);
         }
         if(ibv_destroy_qp(res->qp))
         {
             fprintf(stderr, "failed to destroy QP\n");
             rc = 1;
         }
         else
         {
             qp_connection_set_state(res, QP_CONN_CLOSED);
         }
     }
 
     if(res->mr)
     {
         if(ibv_dereg_mr(res->mr))
         {
             fprintf(stderr, "failed to deregister MR\n");
             rc = 1;
         }
     }
 
     if(res->buf)
     {
         free(res->buf);
     }
 
     if(res->cq)
     {
         if(ibv_destroy_cq(res->cq))
         {
             fprintf(stderr, "failed to destroy CQ\n");
             rc = 1;
         }
     }
 
     if(res->pd)
     {
         if(ibv_dealloc_pd(res->pd))
         {
             fprintf(stderr, "failed to deallocate PD\n");
             rc = 1;
         }
     }
 
     if(res->ib_ctx)
     {
         if(ibv_close_device(res->ib_ctx))
         {
             fprintf(stderr, "failed to close device context\n");
             rc = 1;
         }
     }
 
     if(res->sock >= 0)
     {
         if(close(res->sock))
         {
             fprintf(stderr, "failed to close socket\n");
             rc = 1;
         }
     }
     return rc;
 }
 
 /******************************************************************************
 * Function: print_config
 *
 * Input: none
 *
 * Output: none
 *
 * Returns: none
 *
 * Description: Print out config information
 ******************************************************************************/
 static void print_config(void)
 {
     /* 把最终生效的配置打印出来，便于和抓包/日志对照。 */
     fprintf(stdout, " ------------------------------------------------\n");
     fprintf(stdout, " Device name : \"%s\"\n", config.dev_name);
     fprintf(stdout, " IB port : %u\n", config.ib_port);
     if(config.server_name)
     {
         fprintf(stdout, " IP : %s\n", config.server_name);
     }
     fprintf(stdout, " TCP port : %u\n", config.tcp_port);
     if(config.gid_idx >= 0)
     {
         fprintf(stdout, " GID index : %u\n", config.gid_idx);
     }
     if(config.udp_sport_from_qpn)
     {
         fprintf(stdout, " UDP source port : derived from local QPN; mlx5dv is tried after RTS, GRH flow_label fallback is used if unsupported\n");
     }
     else
     {
         fprintf(stdout, " UDP source port : %u\n", config.udp_sport);
     }
     if(config.qp_notify_host)
     {
         fprintf(stdout, " QP notify target : %s:%u\n", config.qp_notify_host, config.qp_notify_port);
     }
     fprintf(stdout, " CQ poll timeout : %u ms\n", config.poll_timeout_ms);
     fprintf(stdout, " RDMA op size : %zu bytes\n", config.msg_size);
     fprintf(stdout, " MR buffer size : %zu bytes\n", config.buf_size);
     fprintf(stdout, " ------------------------------------------------\n\n");
 }
 
 /******************************************************************************
 * Function: usage
 *
 * Input:
 * argv0: command line arguments
 *
 * Output: none
 *
 * Returns: none
 *
 * Description: print a description of command line syntax
 ******************************************************************************/
 static void usage(const char *argv0)
 {
     fprintf(stdout, "Usage:\n");
     fprintf(stdout, " %s start a server and wait for connection\n", argv0);
     fprintf(stdout, " %s <host> connect to server at <host>\n", argv0);
     fprintf(stdout, "\n");
     fprintf(stdout, "Options:\n");
     fprintf(stdout, " -p, --port <port> listen on/connect to port <port> (default 18515)\n");
     fprintf(stdout, " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
     fprintf(stdout, " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
     fprintf(stdout, " -g, --gid_idx <git index> gid index to be used in GRH (default not used)\n");
     fprintf(stdout, " -u, --udp_sport <udp_sport> set RoCE UDP source port with mlx5dv_modify_qp_udp_sport\n");
     fprintf(stdout, " -U, --udp-sport-from-qpn encode local QPN into RoCE UDP source port with mlx5dv_modify_qp_udp_sport\n");
     fprintf(stdout, " -q, --qp-notify-host <host> UDP target for QP connection updates\n");
     fprintf(stdout, " -Q, --qp-notify-port <port> UDP port for QP connection updates (default %u)\n",
             QP_NOTIFY_DEFAULT_PORT);
     fprintf(stdout, " -P, --poll-timeout-ms <ms> CQ poll timeout for debugging (default %u)\n",
             MAX_POLL_CQ_TIMEOUT);
     fprintf(stdout, " -m, --msg-size <bytes> RDMA WRITE/READ/SEND length in bytes (default %u)\n",
             MSG_SIZE);
     fprintf(stdout, " -B, --buf-size <bytes> registered MR buffer size in bytes (default %u)\n",
             BUF_SIZE);
 }
 
 /******************************************************************************
 * Function: main
 *
 * Input:
 * argc: number of items in argv
 * argv: command line parameters
 *
 * Output: none
 *
 * Returns: 0 on success, 1 on failure
 *
 * Description: Main program code
 ******************************************************************************/
 int main(int argc, char *argv[])
 {
     struct resources res;
     int rc = 1;
     char temp_char;
 
     /*
      * main 的结构很直接:
      * 1. 解析参数，决定自己扮演 TCP 控制面的哪一侧。
      * 2. 创建 RDMA 资源并把 QP 连到 RTS。
      * 3. 执行一个被启用的示例操作。
      *
      * 当前真正启用的是 Test4:
      * - “客户端”(server_name != NULL) 构造本地数据并发起 RDMA WRITE
      * - “服务端”(server_name == NULL) 不主动发 WR，只在最后通过 TCP barrier
      *   确认对端写完后，直接读取自己那块已经被远端 NIC 改写的本地缓冲区
      *
      * 前面的 SEND / RECV / RDMA READ 测试代码被保留下来，方便切换实验，
      * 但默认都是注释掉的。
      */
 
     /* parse the command line parameters */
     while(1)
     {
         int c;
         /* Designated Initializer */
         static struct option long_options[] =
         {
             {.name = "port", .has_arg = 1, .val = 'p' },
             {.name = "ib-dev", .has_arg = 1, .val = 'd' },
             {.name = "ib-port", .has_arg = 1, .val = 'i' },
             {.name = "gid-idx", .has_arg = 1, .val = 'g' },
             {.name = "udp-sport", .has_arg = 1, .val = 'u' },
             {.name = "udp-sport-from-qpn", .has_arg = 0, .val = 'U' },
             {.name = "qp-notify-host", .has_arg = 1, .val = 'q' },
             {.name = "qp-notify-port", .has_arg = 1, .val = 'Q' },
             {.name = "poll-timeout-ms", .has_arg = 1, .val = 'P' },
             {.name = "msg-size", .has_arg = 1, .val = 'm' },
             {.name = "buf-size", .has_arg = 1, .val = 'B' },
             {.name = NULL, .has_arg = 0, .val = '\0'}
         };
 
         c = getopt_long(argc, argv, "p:d:i:g:u:Uq:Q:P:m:B:", long_options, NULL);
         if(c == -1)
         {
             break;
         }
         switch(c)
         {
         case 'p':
             config.tcp_port = strtoul(optarg, NULL, 0);
             break;
         case 'd':
             config.dev_name = strdup(optarg);
             break;
         case 'i':
             config.ib_port = strtoul(optarg, NULL, 0);
             if(config.ib_port < 0)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         case 'g':
             config.gid_idx = strtoul(optarg, NULL, 0);
             if(config.gid_idx < 0)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         case 'u':
             config.udp_sport = strtoul(optarg, NULL, 0);
             if(config.udp_sport < 0 || config.udp_sport > 65535)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         case 'U':
             config.udp_sport_from_qpn = 1;
             break;
         case 'q':
             config.qp_notify_host = optarg;
             break;
         case 'Q':
             config.qp_notify_port = strtoul(optarg, NULL, 0);
             if(config.qp_notify_port == 0 || config.qp_notify_port > 65535)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         case 'P':
             config.poll_timeout_ms = strtoul(optarg, NULL, 0);
             if(config.poll_timeout_ms == 0)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         case 'm':
             config.msg_size = strtoull(optarg, NULL, 0);
             if(config.msg_size == 0 || config.msg_size > UINT32_MAX)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         case 'B':
             config.buf_size = strtoull(optarg, NULL, 0);
             if(config.buf_size == 0 || config.buf_size > UINT32_MAX)
             {
                 usage(argv[0]);
                 return 1;
             }
             break;
         default:
             usage(argv[0]);
             return 1;
         }
     }
 
     /*
      * 命令行最后一个非 option 参数被视为远端主机名。
      * 有这个参数 => 当前进程是主动 connect 的一侧；
      * 没有这个参数 => 当前进程作为被动监听的一侧。
      */
     /* parse the last parameter (if exists) as the server name */
     /* 
      * server_name is null means this node is a server,
      * otherwise this node is a client which need to connect to 
      * the specific server
      */
     if(optind == argc - 1)
     {
         config.server_name = argv[optind];
     }
     else if(optind < argc)
     {
         usage(argv[0]);
         return 1;
     }
 
     if(config.msg_size > config.buf_size)
     {
         fprintf(stderr,
                 "invalid size configuration: --msg-size=%zu is larger than --buf-size=%zu\n",
                 config.msg_size,
                 config.buf_size);
         return 1;
     }
 
     /* print the used parameters for info*/
     print_config();
     /* init all of the resources, so cleanup will be easy */
     resources_init(&res);
     /* create resources before using them */
     if(resources_create(&res))
     {
         fprintf(stderr, "failed to create resources\n");
         goto main_exit;
     }
     /* connect the QPs */
     if(connect_qp(&res))
     {
         fprintf(stderr, "failed to connect QPs\n");
         goto main_exit;
     }
     printf("Time from `modify_qp` to `post_wqe`: %ld ns\n",
         (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec));
 
 
     // fprintf(stdout, "\nTest 1: server use 'RDMA send' to client\n");
     // /* let the server post the sr */
     // if(!config.server_name)
     // {
     //     if(post_send(&res, IBV_WR_SEND))
     //     {
     //         fprintf(stderr, "failed to post SR from server side\n");
     //         goto main_exit;
     //     }
     // }
     // /* in both sides we expect to get a completion */
     // if(poll_completion(&res))
     // {
     //     fprintf(stderr, "poll completion failed\n");
     //     goto main_exit;
     // }
     // /* after polling the completion we have the message in the client buffer too */
     // if(config.server_name)
     // {
     //     fprintf(stdout, "Message is: '%s'\n", res.buf);
     // }
     
     // /* Sync so we are sure server side has data ready before client tries to read it */
     // if(sock_sync_data(res.sock, 1, "R", &temp_char))  /* just send a dummy char back and forth */
     // {
     //     fprintf(stderr, "sync error before RDMA ops\n");
     //     rc = 1;
     //     goto main_exit;
     // }
 
 
     // fprintf(stdout, "\nTest 2: client use 'RDMA send' to server\n");
     // // let the client post the Send Request
     // if (config.server_name){
     //     fprintf(stdout, "fill the buffer with '%s'\n", MSG_C);
     //     strcpy(res.buf, MSG_C);
     //     fprintf(stdout, "going to send the message: '%s'\n", res.buf);
     //     if (post_send(&res, IBV_WR_SEND)){
     //         fprintf(stderr, "failed to post SR from client side\n");
     //         goto main_exit;
     //     }
     // }
     // // let the server post the Receive Request
     // else{
     //     if (post_receive(&res)){
     //         fprintf(stderr, "failed to post RR from server side\n");
     //         goto main_exit;
     //     }
     // }
     // if(poll_completion(&res))
     // {
     //     fprintf(stderr, "poll completion failed\n");
     //     goto main_exit;
     // }
     // /* after polling the completion we have the message in the server buffer too */
     // if(!config.server_name)
     // {
     //     fprintf(stdout, "Message is: '%s'\n", res.buf);
     // }
 
     // /* Sync so we are sure server side has data ready before client tries to read it */
     // if(sock_sync_data(res.sock, 1, "S", &temp_char))  /* just send a dummy char back and forth */
     // {
     //     fprintf(stderr, "sync error before RDMA ops\n");
     //     rc = 1;
     //     goto main_exit;
     // }
 
 
     // fprintf(stdout, "\nTest 3: client use 'RDMA read' to server\n");
     // if(!config.server_name)
     // {
     //     fprintf(stdout, "fill the buffer with '%s'\n", RDMAMSGR);
     //     /* setup server buffer with read message */
     //     strcpy(res.buf, RDMAMSGR);
     //     for (int i = 0; i < 10000; i++) 
     //     {
     //         strcat(res.buf, RDMAMSGR);
     //     }
     // }
     // if(sock_sync_data(res.sock, 1, "S", &temp_char))  /* just send a dummy char back and forth */
     // {
     //     fprintf(stderr, "sync error before RDMA ops\n");
     //     rc = 1;
     //     goto main_exit;
     // }
     // /* 
     //  * Now the client performs an RDMA read and then write on server.
     //  * Note that the server has no idea these events have occured 
     //  */
     // if(config.server_name)
     // {
     //     /* First we read contens of server's buffer */
     //     if(post_send(&res, IBV_WR_RDMA_READ))
     //     {
     //         fprintf(stderr, "failed to post SR 2\n");
     //         rc = 1;
     //         goto main_exit;
     //     }
     //     if(poll_completion(&res))
     //     {
     //         fprintf(stderr, "poll completion failed 2\n");
     //         rc = 1;
     //         goto main_exit;
     //     }
     //     fprintf(stdout, "Contents of server's buffer: '%s'\n", res.buf);
     // }
 
     // /* Sync so we are sure server side has data ready before client tries to read it */
     // if(sock_sync_data(res.sock, 1, "R", &temp_char))  /* just send a dummy char back and forth */
     // {
     //     fprintf(stderr, "sync error before RDMA ops\n");
     //     rc = 1;
     //     goto main_exit;
     // }
 
 
     /*
      * Test4: 由“客户端”向“服务端”的 MR 发起 RDMA WRITE。
      * 注意这里不会触发服务端 CPU 执行任何接收逻辑，数据直接由远端 NIC 写入本地内存。
      */
     fprintf(stdout, "\nTest4: client use 'RDMA write' to server\n");
     fprintf(stdout,
             "[%s][test4] local_qpn=0x%x remote_qpn=0x%x remote_addr=0x%" PRIx64 " remote_rkey=0x%x msg_size=%u buf_size=%u\n",
             role_str(),
             res.qp ? res.qp->qp_num : 0,
             res.remote_props.qp_num,
             res.remote_props.addr,
             res.remote_props.rkey,
             (unsigned)config.msg_size,
             (unsigned)config.buf_size);
     if(config.server_name)
     {
         /*
          * 当前进程是主动发起 RDMA WRITE 的一侧。
          * 这里构造的是“要写到远端 MR 里去”的本地 payload。
          */
         /* Now we replace what's in the server's buffer */
         const size_t msgLen = strlen(RDMAMSGW);
         size_t payload_len = 0;
         size_t repetitions = 0;
         // for (int i = 0; i < 10000; i++) 
         // {
         //     strcat(res.buf, RDMAMSGW);
         // }
         /*
          * 用 memcpy 循环填充本次实际 RDMA WRITE 的长度，避免反复 strcat 的 O(n^2)
          * 开销。旧逻辑固定 repetitions=100000，只准备约 2MB payload，但 WR
          * 实际长度仍由 MSG_SIZE 决定，日志容易误导；现在让 payload_len 等于
          * config.msg_size，和 post_send() 里的 sge.length 保持一致。
          */
         char *p = res.buf;
         while(payload_len < config.msg_size)
         {
             size_t copy_len = msgLen;
             if(copy_len > config.msg_size - payload_len)
             {
                 copy_len = config.msg_size - payload_len;
             }
             memcpy(p, RDMAMSGW, copy_len);
             p += copy_len;
             payload_len += copy_len;
             repetitions++;
         }
         if(config.msg_size < config.buf_size)
         {
             *p = '\0';
         }
         fprintf(stdout,
                 "[%s][test4] prepared RDMA WRITE payload: repetitions=%zu unit_len=%zu total_payload_len=%zu first_32_bytes='%.32s'\n",
                 role_str(),
                 repetitions,
                 msgLen,
                 payload_len,
                 res.buf);
         // fprintf(stdout, "Now replacing it with: '%s'\n", res.buf);
         if(post_send(&res, IBV_WR_RDMA_WRITE))
         {
             fprintf(stderr, "failed to post SR 3\n");
             rc = 1;
             goto main_exit;
         }
         if(poll_completion(&res))
         {
             fprintf(stderr, "poll completion failed 3\n");
             rc = 1;
             goto main_exit;
         }
     }
 
     /*
      * RDMA WRITE 完成后，再用一次 TCP barrier 告诉对端:
      * “现在可以安全地读你自己的本地缓冲区了”。
      * 这一步只是为了示例输出更稳定，不是 RDMA 协议本身要求的握手。
      */
     /* Sync so server will know that client is done mucking with its memory */
     fprintf(stdout, "[%s][test4] entering post-RDMA TCP barrier\n", role_str());
     if(sock_sync_data(res.sock, 1, "W", &temp_char))  /* just send a dummy char back and forth */
     {
         fprintf(stderr,
                 "[%s][test4] sync error after RDMA ops; if peer is client, check whether client exited after CQ timeout/bad completion before reaching barrier\n",
                 role_str());
         rc = 1;
         goto main_exit;
     }
     fprintf(stdout,
             "[%s][test4] post-RDMA TCP barrier completed, remote byte=0x%02x\n",
             role_str(),
             (unsigned char)temp_char);
 
     if(!config.server_name)
     {
         /* 服务端并没有收到 SEND/RECV 事件，而是直接看到本地内存内容被远端改写后的结果。 */
         fprintf(stdout, "Contents of server buffer first 128 bytes: '%.128s'\n", res.buf);
     }
     rc = 0;
 
 main_exit:
     if(resources_destroy(&res))
     {
         fprintf(stderr, "failed to destroy resources\n");
         rc = 1;
     }
     if(config.dev_name)
     {
         free((char *) config.dev_name);
     }
     //fprintf(stdout, "\ntest result is %d\n", rc);
     return rc;
 }
 