#include "headers.h"
#include "switch_config.h"

switch_t iswitch;
bf_switchd_context_t *switchd_ctx;
bf_rt_target_t *dev_tgt = &iswitch.dev_tgt;
const bf_rt_info_hdl *bfrt_info = NULL;
bf_rt_session_hdl **session = &iswitch.session;

// ibv_destroy_qp中只有cQPN，计算不出来udp_sport，
// 所以为了找到要删哪一个table entry，需要在controlPlane维护这么一个映射
// C里面没有unordered_map，追求完美就要把代码重构为C++然后用unordered_map，懒得重构了
uint16_t sIP_and_cQPN_to_udp_sport_mapping[100][1000];

uint32_t ipv4AddrToUint32(const char* ip_str) {
  uint32_t ip_addr;
  if (inet_pton(AF_INET, ip_str, &ip_addr) != 1) {
      fprintf(stderr, "Error: Invalid IP address format.\n");
      return EXIT_FAILURE;
  }

  return ntohl(ip_addr);
}

const bf_rt_table_hdl *set_cnp_dstQPN = NULL;
set_cnp_dstQPN_info_t set_cnp_dstQPN_info;
static void set_cnp_dstQPN_setup(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_table_hdl **set_cnp_dstQPN,
                                set_cnp_dstQPN_info_t *set_cnp_dstQPN_info) {
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Egress.set_cnp_dstQPN",
                                          set_cnp_dstQPN);
    assert(bf_status == BF_SUCCESS);

    // Allocate key and data once, and use reset across different uses
    bf_status = bf_rt_table_key_allocate(*set_cnp_dstQPN,
                                         &set_cnp_dstQPN_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_table_data_allocate(*set_cnp_dstQPN,
                                          &set_cnp_dstQPN_info->data);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for key field
	bf_status = bf_rt_key_field_id_get(*set_cnp_dstQPN, "hdr.ipv4.dst_addr",
										&set_cnp_dstQPN_info->kid_client_ip);
    assert(bf_status == BF_SUCCESS);

	bf_status = bf_rt_key_field_id_get(*set_cnp_dstQPN, "hdr.udp.src_port",
										&set_cnp_dstQPN_info->kid_client_udp_port);
    assert(bf_status == BF_SUCCESS);

    // Get action Ids for action forward
    bf_status = bf_rt_action_name_to_id(*set_cnp_dstQPN,
                                        "Egress.set_dstQPN",
                                        &set_cnp_dstQPN_info->aid_set_dstQPN);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for data field
    bf_status = bf_rt_data_field_id_with_action_get(
        *set_cnp_dstQPN, "clientQPN",
        set_cnp_dstQPN_info->aid_set_dstQPN, &set_cnp_dstQPN_info->did_clientQPN
    );
    assert(bf_status == BF_SUCCESS);
}

static void set_cnp_dstQPN_entry_add(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *set_cnp_dstQPN,
                                    set_cnp_dstQPN_info_t *set_cnp_dstQPN_info,
                                    set_cnp_dstQPN_entry_t *set_cnp_dstQPN_entry) {
    bf_status_t bf_status;

    // Reset key before use
    bf_rt_table_key_reset(set_cnp_dstQPN, &set_cnp_dstQPN_info->key);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_client_ip,
										  set_cnp_dstQPN_entry->client_ip); 
    assert(bf_status == BF_SUCCESS);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_client_udp_port,
										  set_cnp_dstQPN_entry->client_udp_port); 
    assert(bf_status == BF_SUCCESS);

    if (strcmp(set_cnp_dstQPN_entry->action, "set_dstQPN") == 0) {
        // Reset data before use
        bf_rt_table_action_data_reset(set_cnp_dstQPN,
                                      set_cnp_dstQPN_info->aid_set_dstQPN,
                                      &set_cnp_dstQPN_info->data);
        // Fill in the Data object
        bf_status = bf_rt_data_field_set_value(set_cnp_dstQPN_info->data,
                                               set_cnp_dstQPN_info->did_clientQPN,
                                               set_cnp_dstQPN_entry->clientQPN);
        assert(bf_status == BF_SUCCESS);
    }
    // Call table entry add API
    bf_status = bf_rt_table_entry_add(set_cnp_dstQPN, session, dev_tgt,
                                      set_cnp_dstQPN_info->key,
                                      set_cnp_dstQPN_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

static void set_cnp_dstQPN_entry_del(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *set_cnp_dstQPN,
                                    set_cnp_dstQPN_info_t *set_cnp_dstQPN_info,
                                    set_cnp_dstQPN_entry_t *set_cnp_dstQPN_entry) {
    bf_status_t bf_status;

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_client_ip,
										  set_cnp_dstQPN_entry->client_ip); 
    assert(bf_status == BF_SUCCESS);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_client_udp_port,
										  set_cnp_dstQPN_entry->client_udp_port); 
    assert(bf_status == BF_SUCCESS);

    // Call table entry del API
    bf_status = bf_rt_table_entry_del(set_cnp_dstQPN, session, dev_tgt,
                                      set_cnp_dstQPN_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

// C-style using bf_pm
static void port_setup(const bf_rt_target_t *dev_tgt,
                       const switch_port_t *port_list,
                       const uint8_t port_count) {
    bf_status_t bf_status;
    
    for (unsigned int idx = 0; idx < port_count; idx++) {
        bf_pal_front_port_handle_t port_hdl;
        bf_status = bf_pm_port_str_to_hdl_get(dev_tgt->dev_id,
                                              port_list[idx].fp_port,
                                              &port_hdl);
        assert(bf_status == BF_SUCCESS);
        bf_status = bf_pm_port_add(dev_tgt->dev_id, &port_hdl,
                                   BF_SPEED_25G, BF_FEC_TYP_NONE);
        assert(bf_status == BF_SUCCESS);
        bf_status = bf_pm_port_enable(dev_tgt->dev_id, &port_hdl);
        assert(bf_status == BF_SUCCESS);
        printf("Port %s is enabled successfully!\n", port_list[idx].fp_port);
    }
}

static void switchd_setup(bf_switchd_context_t *switchd_ctx, const char *prog) {
	char conf_file[256];
	char bf_sysfs_fname[128] = "/sys/class/bf/bf0/device";
    FILE *fd;

	switchd_ctx->install_dir = strdup(getenv("SDE_INSTALL"));
	sprintf(conf_file, "%s%s%s%s", \
	        getenv("SDE_INSTALL"), "/share/p4/targets/tofino/", prog, ".conf");
	switchd_ctx->conf_file = conf_file;
	switchd_ctx->running_in_background = true;
	switchd_ctx->dev_sts_thread = true;
	switchd_ctx->dev_sts_port = 7777;
	
    strncat(bf_sysfs_fname, "/dev_add",
            sizeof(bf_sysfs_fname) - 1 - strlen(bf_sysfs_fname));
    printf("bf_sysfs_fname %s\n", bf_sysfs_fname);
    fd = fopen(bf_sysfs_fname, "r");
    if (fd != NULL) {
        // override previous parsing if bf_kpkt KLM was loaded 
        printf("kernel mode packet driver present, forcing kpkt option!\n");
        switchd_ctx->kernel_pkt = true;
        fclose(fd);
    }

    assert(bf_switchd_lib_init(switchd_ctx) == BF_SUCCESS);
    printf("\nbf_switchd is initialized correctly!\n");
}

static void bfrt_setup( const bf_rt_target_t *dev_tgt,
                        const bf_rt_info_hdl **bfrt_info,
                        const char* prog,
                        bf_rt_session_hdl **session) {
    bf_status_t bf_status;

    // Get bfrtInfo object from dev_id and p4 program name
    bf_status = bf_rt_info_get(dev_tgt->dev_id, prog, bfrt_info);
    assert(bf_status == BF_SUCCESS);
    // Create a session object
    bf_status = bf_rt_session_create(session);
    assert(bf_status == BF_SUCCESS);
    printf("bfrt_info is got and session is created correctly!\n");
}

// 当前不考虑batch（不过主机端上batch发Daemon Pkt的话应该弊远大于利）
void* poll_daemon_pkt(void* arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[DAEMON_PKT_BUFFER_SIZE];
    struct pollfd fds[1];
    socklen_t addr_len = sizeof(client_addr);

    // 创建 UDP 套接字
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        return NULL;
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有本地 IP
    server_addr.sin_port = htons(GET_DAEMON_PKT_UDP_PORT); // 绑定端口

    // 绑定套接字到指定端口
    if (bind(sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        return NULL;
    }

    printf("GetcQPN Thread: Listening on UDP port %d...\n", GET_DAEMON_PKT_UDP_PORT);

    // 设置 poll 文件描述符
    fds[0].fd = sockfd;
    fds[0].events = POLLIN; // 监听可读事件

    while (1) {
        // 调用 poll 阻塞等待事件
        int poll_result = poll(fds, 1, -1); // -1 表示无限阻塞等待事件

        if (poll_result == -1) {
            perror("Poll failed");
            break;
        }

        // 检查是否有数据可读
        // 当前方案是来一个包就处理一个，并没有batch操作
        if (fds[0].revents & POLLIN) {
            memset(buffer, 0, DAEMON_PKT_BUFFER_SIZE);
            int len = recvfrom(sockfd, buffer, DAEMON_PKT_BUFFER_SIZE, 0,
                               (struct sockaddr*)&client_addr, &addr_len);
            if (len < 0) {
                perror("recvfrom failed");
            } else {
                printf("GetcQPN Thread: Received %d bytes from %s:%d\n", len,
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                printf("Polled Daemon Pkt here:\n");
                for (int i = 0; i < len; ++i) {
                    printf("%02x ", (unsigned char)buffer[i]);
                    if ((i + 1) % 16 == 0)
                        printf("\n");
                }
                printf("\n");
                // 解析数据包
                uint16_t udp_source_port = (uint8_t)buffer[0] << 8 | (uint8_t)buffer[1];
                uint32_t cQPN = ((uint8_t)buffer[3] << 16) + ((uint8_t)buffer[4] << 8) + (uint8_t)buffer[5];
                uint32_t sQPN = ((uint8_t)buffer[7] << 16) + ((uint8_t)buffer[8] << 8) + (uint8_t)buffer[9];
                uint8_t flag = (uint8_t)buffer[10];
                // 下发流表
                if (flag == 1) {
                    uint32_t sIP = ipv4AddrToUint32(inet_ntoa(client_addr.sin_addr));
                    set_cnp_dstQPN_entry_t set_cnp_dstQPN_entry =  {
                        .client_ip = sIP,
                        .client_udp_port = udp_source_port,
                        .action = "set_cnp_dstQPN",
                        .clientQPN = cQPN,
                    }; 
                    set_cnp_dstQPN_entry_add(dev_tgt, *session, set_cnp_dstQPN, 
                                            &set_cnp_dstQPN_info, &set_cnp_dstQPN_entry);
                    // 将映射写入 sIP_and_cQPN_to_udp_sport_mapping
                    sIP_and_cQPN_to_udp_sport_mapping[sIP][cQPN] = udp_source_port;
                }
                else {
                    uint32_t cQPN = ((uint8_t)buffer[1] << 16) + ((uint8_t)buffer[2] << 8) + (uint8_t)buffer[3];
                    uint32_t sIP = ipv4AddrToUint32(inet_ntoa(client_addr.sin_addr));
                    set_cnp_dstQPN_entry_t set_cnp_dstQPN_entry =  {
                        .client_ip = sIP,
                        .client_udp_port = sIP_and_cQPN_to_udp_sport_mapping[sIP][cQPN],
                        .action = "set_cnp_dstQPN",
                        .clientQPN = cQPN,
                    }; 
                    set_cnp_dstQPN_entry_del(dev_tgt, *session, set_cnp_dstQPN, 
                                            &set_cnp_dstQPN_info, &set_cnp_dstQPN_entry);
                }
            }
        }
    }

    // 关闭套接字
    close(sockfd);
    return NULL;
}

int main(void) {
    dev_tgt->dev_id = 0;
    dev_tgt->pipe_id = BF_DEV_PIPE_ALL;

    // Initialize and set the bf_switchd
    switchd_ctx = (bf_switchd_context_t *)
                  calloc(1, sizeof(bf_switchd_context_t));
    if (switchd_ctx == NULL) {
        printf("Cannot allocate switchd context\n");
        return -1;
    }

    switchd_setup(switchd_ctx, P4_PROG_NAME);
    printf("\nbf_switchd is initialized successfully!\n");

    // Get BfRtInfo and create the bf_runtime session
    bfrt_setup(dev_tgt, &bfrt_info, P4_PROG_NAME, session);
    printf("bfrtInfo is got and session is created successfully!\n");

    // Set up the portable using C bf_pm api, instead of BF_RT CPP
	port_setup(dev_tgt, PORT_LIST, ARRLEN(PORT_LIST));	
    printf("$PORT table is set up successfully!\n");
    
    // Set up the set_cnp_dstQPN
    set_cnp_dstQPN_setup(dev_tgt, bfrt_info, &set_cnp_dstQPN,
                        &set_cnp_dstQPN_info);

    pthread_t poll_thread;

    // 创建线程
    if (pthread_create(&poll_thread, NULL, poll_daemon_pkt, NULL) != 0) {
        perror("Failed to create thread");
        return EXIT_FAILURE;
    }

    printf("Main: Poll thread started\n");

    // 等待线程结束（如果需要，主线程可以继续执行其他任务）
    pthread_join(poll_thread, NULL);

    printf("Main: Poll thread finished\n");

    while(1){}

    return 0;
}