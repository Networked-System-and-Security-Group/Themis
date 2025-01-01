#include "headers.h"
#include "switch_config.h"

switch_t iswitch;
bf_switchd_context_t *switchd_ctx;
bf_rt_target_t *dev_tgt = &iswitch.dev_tgt;
const bf_rt_info_hdl *bfrt_info = NULL;
bf_rt_session_hdl **session = &iswitch.session;

// ibv_destroy_qp中只有cQPN，没有dIP
// 所以为了找到要删哪一个table entry，需要在controlPlane维护这么一个映射
// C里面没有unordered_map，追求完美就要把代码重构为C++然后用unordered_map，懒得重构了
uint16_t sIP_and_cQPN_to_dIP_mapping[100][1000];

uint32_t ipv4AddrToUint32(const char* ip_str) {
  uint32_t ip_addr;
  if (inet_pton(AF_INET, ip_str, &ip_addr) != 1) {
      fprintf(stderr, "Error: Invalid IP address format.\n");
      return EXIT_FAILURE;
  }

  return ntohl(ip_addr);
}

// table operation for act_l3_table
static void act_l3_table_setup(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_table_hdl **act_l3_table,
                                act_l3_table_info_t *act_l3_table_info,
                                const act_l3_table_list_tuple_t *act_l3_table_list,
                                const uint8_t list_size) {
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Ingress.l3_table",
                                          act_l3_table);
    assert(bf_status == BF_SUCCESS);

    // Allocate key and data once, and use reset across different uses
    bf_status = bf_rt_table_key_allocate(*act_l3_table,
                                         &act_l3_table_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_table_data_allocate(*act_l3_table,
                                          &act_l3_table_info->data);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for key field

	bf_status = bf_rt_key_field_id_get(*act_l3_table, "hdr.ipv4.dst_addr",
										&act_l3_table_info->kid_ipv4_dst_ip);
    assert(bf_status == BF_SUCCESS);

    // Get action Ids for action forward
    bf_status = bf_rt_action_name_to_id(*act_l3_table,
                                        "Ingress.forward",
                                        &act_l3_table_info->aid_forward);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for data field
    bf_status = bf_rt_data_field_id_with_action_get(
        *act_l3_table, "egress_port",
        act_l3_table_info->aid_forward, &act_l3_table_info->did_egress_port
    );
    assert(bf_status == BF_SUCCESS);
}

static void act_l3_table_entry_add(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *act_l3_table,
                                    act_l3_table_info_t *act_l3_table_info,
                                    act_l3_table_entry_t *act_l3_table_entry) {
    bf_status_t bf_status;

    // Reset key before use
    bf_rt_table_key_reset(act_l3_table, &act_l3_table_info->key);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(act_l3_table_info->key,
										  act_l3_table_info->kid_ipv4_dst_ip,
										  act_l3_table_entry->ipv4_addr); 
    assert(bf_status == BF_SUCCESS);

    if (strcmp(act_l3_table_entry->action, "forward") == 0) {
        // Reset data before use
        bf_rt_table_action_data_reset(act_l3_table,
                                      act_l3_table_info->aid_forward,
                                      &act_l3_table_info->data);
        // Fill in the Data object
        bf_status = bf_rt_data_field_set_value(act_l3_table_info->data,
                                               act_l3_table_info->did_egress_port,
                                               act_l3_table_entry->egress_port);
        assert(bf_status == BF_SUCCESS);
    }
    // Call table entry add API
    bf_status = bf_rt_table_entry_add(act_l3_table, session, dev_tgt,
                                      act_l3_table_info->key,
                                      act_l3_table_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

// prefix: act_l3_table
static void act_l3_table_deploy(const bf_rt_target_t *dev_tgt,
                                 const bf_rt_info_hdl *bfrt_info,
                                 const bf_rt_session_hdl *session,
                                 const act_l3_table_list_tuple_t *act_l3_table_list,
                                 const uint8_t list_size) {
    bf_status_t bf_status;
    const bf_rt_table_hdl *act_l3_table = NULL;
    act_l3_table_info_t act_l3_table_info;

    // Set up the act_l3_table
    act_l3_table_setup(dev_tgt, bfrt_info, &act_l3_table,
                        &act_l3_table_info, act_l3_table_list, list_size);
    printf("act_l3_table is set up correctly!\n");

    // Add act_l3_table entries
    for (unsigned int idx = 0; idx < list_size; idx++) {

		act_l3_table_entry_t act_l3_table_entry =  {
			.ipv4_addr = ipv4AddrToUint32(act_l3_table_list[idx].ipv4_addr),
			.action = "forward",
			.egress_port = act_l3_table_list[idx].egress_port,
		}; 
        act_l3_table_entry_add(dev_tgt, session, act_l3_table,
                                &act_l3_table_info, &act_l3_table_entry);
    }
    printf("act_l3_table is deployed correctly!\n");

}

static void act_arp_table_setup(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_table_hdl **act_arp_table,
                                act_arp_table_info_t *act_arp_table_info,
                                const act_arp_table_list_tuple_t *act_arp_table_list,
                                const uint8_t list_size) {
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Ingress.arp_table",
                                          act_arp_table);
    assert(bf_status == BF_SUCCESS);

    // Allocate key and data once, and use reset across different uses
    bf_status = bf_rt_table_key_allocate(*act_arp_table,
                                         &act_arp_table_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_table_data_allocate(*act_arp_table,
                                          &act_arp_table_info->data);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for key field

	bf_status = bf_rt_key_field_id_get(*act_arp_table, "hdr.arp.dstIpv4Addr",
										&act_arp_table_info->kid_ipv4_dst_ip);
    assert(bf_status == BF_SUCCESS);

    // Get action Ids for action forward
    bf_status = bf_rt_action_name_to_id(*act_arp_table,
                                        "Ingress.forward",
                                        &act_arp_table_info->aid_forward);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for data field
    bf_status = bf_rt_data_field_id_with_action_get(
        *act_arp_table, "egress_port",
        act_arp_table_info->aid_forward, &act_arp_table_info->did_egress_port
    );
    assert(bf_status == BF_SUCCESS);
}

static void act_arp_table_entry_add(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *act_arp_table,
                                    act_arp_table_info_t *act_arp_table_info,
                                    act_arp_table_entry_t *act_arp_table_entry) {
    bf_status_t bf_status;

    // Reset key before use
    bf_rt_table_key_reset(act_arp_table, &act_arp_table_info->key);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(act_arp_table_info->key,
										  act_arp_table_info->kid_ipv4_dst_ip,
										  act_arp_table_entry->ipv4_addr); 
    assert(bf_status == BF_SUCCESS);

    if (strcmp(act_arp_table_entry->action, "forward") == 0) {
        // Reset data before use
        bf_rt_table_action_data_reset(act_arp_table,
                                      act_arp_table_info->aid_forward,
                                      &act_arp_table_info->data);
        // Fill in the Data object
        bf_status = bf_rt_data_field_set_value(act_arp_table_info->data,
                                               act_arp_table_info->did_egress_port,
                                               act_arp_table_entry->egress_port);
        assert(bf_status == BF_SUCCESS);
    }
    // Call table entry add API
    bf_status = bf_rt_table_entry_add(act_arp_table, session, dev_tgt,
                                      act_arp_table_info->key,
                                      act_arp_table_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

// prefix: act_arp_table
static void act_arp_table_deploy(const bf_rt_target_t *dev_tgt,
                                 const bf_rt_info_hdl *bfrt_info,
                                 const bf_rt_session_hdl *session,
                                 const act_arp_table_list_tuple_t *act_arp_table_list,
                                 const uint8_t list_size) {
    bf_status_t bf_status;
    const bf_rt_table_hdl *act_arp_table = NULL;
    act_arp_table_info_t act_arp_table_info;

    // Set up the act_arp_table
    act_arp_table_setup(dev_tgt, bfrt_info, &act_arp_table,
                        &act_arp_table_info, act_arp_table_list, list_size);
    printf("act_arp_table is set up correctly!\n");

    // Add act_arp_table entries
    for (unsigned int idx = 0; idx < list_size; idx++) {

		act_arp_table_entry_t act_arp_table_entry =  {
			.ipv4_addr = ipv4AddrToUint32(act_arp_table_list[idx].ipv4_addr),
			.action = "forward",
			.egress_port = act_arp_table_list[idx].egress_port,
		}; 
        act_arp_table_entry_add(dev_tgt, session, act_arp_table,
                                &act_arp_table_info, &act_arp_table_entry);
    }
    printf("act_arp_table is deployed correctly!\n");

}

// table operation for detect_ecn
static void detect_ecn_setup(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_table_hdl **detect_ecn,
                                detect_ecn_info_t *detect_ecn_info,
                                const detect_ecn_list_tuple_t *detect_ecn_list,
                                const uint8_t list_size) {
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Ingress.detect_ecn",
                                          detect_ecn);
    assert(bf_status == BF_SUCCESS);

    // Allocate key and data once, and use reset across different uses
    bf_status = bf_rt_table_key_allocate(*detect_ecn,
                                         &detect_ecn_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_table_data_allocate(*detect_ecn,
                                          &detect_ecn_info->data);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for key field

	bf_status = bf_rt_key_field_id_get(*detect_ecn, "hdr.ipv4.ecn",
										&detect_ecn_info->kid_ecn_value);
    assert(bf_status == BF_SUCCESS);

    // Get action Ids for action set_mirror_md
    bf_status = bf_rt_action_name_to_id(*detect_ecn,
                                        "Ingress.set_mirror_md",
                                        &detect_ecn_info->aid_set_mirror_md);
    assert(bf_status == BF_SUCCESS);

    // No data field needed to set
}

static void detect_ecn_entry_add(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *detect_ecn,
                                    detect_ecn_info_t *detect_ecn_info,
                                    detect_ecn_entry_t *detect_ecn_entry) {
    bf_status_t bf_status;

    // Reset key before use
    bf_rt_table_key_reset(detect_ecn, &detect_ecn_info->key);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(detect_ecn_info->key,
										  detect_ecn_info->kid_ecn_value,
										  detect_ecn_entry->ecn_value); 
    assert(bf_status == BF_SUCCESS);

    if (strcmp(detect_ecn_entry->action, "set_mirror_md") == 0) {
        // Reset data before use
        bf_rt_table_action_data_reset(detect_ecn,
                                      detect_ecn_info->aid_set_mirror_md,
                                      &detect_ecn_info->data);
        // No Data object needs to be filled
    }
    // Call table entry add API
    bf_status = bf_rt_table_entry_add(detect_ecn, session, dev_tgt,
                                      detect_ecn_info->key,
                                      detect_ecn_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

// prefix: detect_ecn
static void detect_ecn_deploy(const bf_rt_target_t *dev_tgt,
                                 const bf_rt_info_hdl *bfrt_info,
                                 const bf_rt_session_hdl *session,
                                 const detect_ecn_list_tuple_t *detect_ecn_list,
                                 const uint8_t list_size) {
    bf_status_t bf_status;
    const bf_rt_table_hdl *detect_ecn = NULL;
    detect_ecn_info_t detect_ecn_info;

    // Set up the detect_ecn
    detect_ecn_setup(dev_tgt, bfrt_info, &detect_ecn,
                        &detect_ecn_info, detect_ecn_list, list_size);
    printf("detect_ecn is set up correctly!\n");

    // Add detect_ecn entries
    for (unsigned int idx = 0; idx < list_size; idx++) {

		detect_ecn_entry_t detect_ecn_entry =  {
			.ecn_value = detect_ecn_list[idx].ecn_value,
			.action = "set_mirror_md"
		}; 
        detect_ecn_entry_add(dev_tgt, session, detect_ecn,
                                &detect_ecn_info, &detect_ecn_entry);
    }
    printf("detect_ecn is deployed correctly!\n");

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
    
	bf_status = bf_rt_key_field_id_get(*set_cnp_dstQPN, "hdr.ipv4.src_addr",
										&set_cnp_dstQPN_info->kid_server_ip);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for key field
	bf_status = bf_rt_key_field_id_get(*set_cnp_dstQPN, "hdr.ipv4.dst_addr",
										&set_cnp_dstQPN_info->kid_client_ip);
    assert(bf_status == BF_SUCCESS);

	bf_status = bf_rt_key_field_id_get(*set_cnp_dstQPN, "hdr.bth.dst_qpn",
										&set_cnp_dstQPN_info->kid_serverQPN);
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
    
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_server_ip,
										  set_cnp_dstQPN_entry->server_ip); 
    assert(bf_status == BF_SUCCESS);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_client_ip,
										  set_cnp_dstQPN_entry->client_ip); 
    assert(bf_status == BF_SUCCESS);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_serverQPN,
										  set_cnp_dstQPN_entry->serverQPN); 
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

	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_server_ip,
										  set_cnp_dstQPN_entry->server_ip); 
    assert(bf_status == BF_SUCCESS);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_client_ip,
										  set_cnp_dstQPN_entry->client_ip); 
    assert(bf_status == BF_SUCCESS);

    // Fill in the Key object
	bf_status = bf_rt_key_field_set_value(set_cnp_dstQPN_info->key,
										  set_cnp_dstQPN_info->kid_serverQPN,
										  set_cnp_dstQPN_entry->serverQPN); 
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
                printf("------------------------------------------------\n");
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
                uint32_t dIP = ((uint8_t)buffer[0] << 24) + ((uint8_t)buffer[1] << 16) + ((uint8_t)buffer[2] << 8) + (uint8_t)buffer[3];
                uint32_t cQPN = ((uint8_t)buffer[5] << 16) + ((uint8_t)buffer[6] << 8) + (uint8_t)buffer[7];
                uint32_t sQPN = ((uint8_t)buffer[9] << 16) + ((uint8_t)buffer[10] << 8) + (uint8_t)buffer[11];
                uint8_t flag = (uint8_t)buffer[12];
                uint32_t sIP = ipv4AddrToUint32(inet_ntoa(client_addr.sin_addr));
                printf("dIP = %u\n", dIP);
                printf("cQPN = %u\n", cQPN);
                printf("sQPN = %u\n", sQPN);
                // 下发流表
                if (flag == 1) {
                    printf("Add a new entry\n");
                    set_cnp_dstQPN_entry_t set_cnp_dstQPN_entry =  {
                        .server_ip = dIP,
                        .client_ip = sIP,
                        .serverQPN = sQPN,
                        .action = "set_cnp_dstQPN",
                        .clientQPN = cQPN,
                    }; 
                    set_cnp_dstQPN_entry_add(dev_tgt, *session, set_cnp_dstQPN, 
                                            &set_cnp_dstQPN_info, &set_cnp_dstQPN_entry);
                    // 将映射写入 sIP_and_cQPN_to_udp_sport_mapping
                    sIP_and_cQPN_to_dIP_mapping[sIP][cQPN] = dIP;
                }
                else {
                    printf("Delete a new entry\n");
                    set_cnp_dstQPN_entry_t set_cnp_dstQPN_entry =  {
                        .server_ip = sIP_and_cQPN_to_dIP_mapping[sIP][cQPN],
                        .client_ip = sIP,
                        .action = "set_cnp_dstQPN",
                        .clientQPN = cQPN,
                    }; 
                    set_cnp_dstQPN_entry_del(dev_tgt, *session, set_cnp_dstQPN, 
                                            &set_cnp_dstQPN_info, &set_cnp_dstQPN_entry);
                }
                printf("------------------------------------------------\n");
            }
        }
    }

    // 关闭套接字
    close(sockfd);
    return NULL;
}

static void mirrorSetup(const bf_rt_target_t *dev_tgt) {
	p4_pd_status_t pd_status;
	p4_pd_sess_hdl_t mirror_session;
	p4_pd_dev_target_t pd_dev_tgt = {dev_tgt->dev_id, dev_tgt->pipe_id};

	pd_status = p4_pd_client_init(&mirror_session);
    assert(pd_status == BF_SUCCESS);

    p4_pd_mirror_session_info_t mirror_session_info = {
        .type        = PD_MIRROR_TYPE_NORM, // Not sure
        .dir         = PD_DIR_INGRESS,
        .id          = CNP_SES_ID,
        .egr_port    = ACT_196_PORT,
        .egr_port_v  = true,
        .max_pkt_len = 16384 // Refer to example in Barefoot Academy	
    };

    pd_status = p4_pd_mirror_session_create(mirror_session, pd_dev_tgt,
                                        &mirror_session_info);
    assert(pd_status == BF_SUCCESS);	
    printf("Config I2E mirrror to PORT: %d, Session ID: %d\n", mirror_session_info.egr_port, mirror_session_info.id);
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

    // Setup and install entries for act_l3_table (C-style)
	act_l3_table_deploy(dev_tgt, bfrt_info, *session,
                        act_l3_table_list, ARRLEN(act_l3_table_list));

    // Setup and install entries for act_l3_table (C-style)
	act_arp_table_deploy(dev_tgt, bfrt_info, *session,
                        act_arp_table_list, ARRLEN(act_l3_table_list));
    
    // Set up the set_cnp_dstQPN table in Egress
    set_cnp_dstQPN_setup(dev_tgt, bfrt_info, &set_cnp_dstQPN,
                        &set_cnp_dstQPN_info);

    // Set up detect_ecn table in Ingress
    detect_ecn_deploy(dev_tgt, bfrt_info, *session,
                        detect_ecn_list, ARRLEN(detect_ecn_list));

    // Set up I2E Mirror
    mirrorSetup(dev_tgt);
	printf("Mirror Setup finished\n");

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