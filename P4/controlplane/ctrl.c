#ifndef BFRT_GENERIC_FLAGS
#define BFRT_GENERIC_FLAGS
#endif

#include "headers.h"
#include "switch_config.h"
#include <execinfo.h>
#include <signal.h>

switch_t iswitch;
bf_switchd_context_t *switchd_ctx;
bf_rt_target_t *dev_tgt = &iswitch.dev_tgt;
const bf_rt_info_hdl *bfrt_info = NULL;
bf_rt_session_hdl **session = &iswitch.session;

static void check_bf_status(bf_status_t status, const char *operation)
{
    if (status != BF_SUCCESS)
    {
        fprintf(stderr, "%s failed, status=%d\n", operation, status);
        exit(EXIT_FAILURE);
    }
}

static bf_status_t table_entry_add_compat(const bf_rt_table_hdl *table_hdl,
                                          const bf_rt_session_hdl *session_hdl,
                                          const bf_rt_target_t *target,
                                          const bf_rt_table_key_hdl *key,
                                          const bf_rt_table_data_hdl *data)
{
#ifdef BFRT_GENERIC_FLAGS
    printf("  bf_rt_table_entry_add: generic-flags ABI\n");
    return bf_rt_table_entry_add(table_hdl, session_hdl, target, 0, key, data);
#else
    printf("  bf_rt_table_entry_add: legacy ABI\n");
    return bf_rt_table_entry_add(table_hdl, session_hdl, target, key, data);
#endif
}

static void segv_handler(int sig)
{
    void *frames[32];
    int frame_count;

    fprintf(stderr, "\nCaught signal %d (SIGSEGV). Backtrace:\n", sig);
    frame_count = backtrace(frames, (int)ARRLEN(frames));
    backtrace_symbols_fd(frames, frame_count, fileno(stderr));
    fflush(stderr);
    _exit(128 + sig);
}

static void print_build_mode(void)
{
    printf("control build: %s %s\n", __DATE__, __TIME__);
#ifdef BFRT_GENERIC_FLAGS
    printf("BFRT_GENERIC_FLAGS: enabled\n");
#else
    printf("BFRT_GENERIC_FLAGS: disabled\n");
#endif
    fflush(stdout);
}

static void log_table_entry_context(const char *table_name,
                                    const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session_hdl,
                                    const bf_rt_table_hdl *table_hdl,
                                    const bf_rt_id_t key_field_id,
                                    const bf_rt_id_t action_id,
                                    const bf_rt_id_t data_field_id,
                                    const uint32_t ipv4_addr,
                                    const char *ipv4_str,
                                    const bf_dev_port_t egress_port)
{
    printf("  [%s] dev_id=%d pipe_id=%d table=%p session=%p key_field_id=%d action_id=%d data_field_id=%d\n",
           table_name,
           dev_tgt ? dev_tgt->dev_id : -1,
           dev_tgt ? dev_tgt->pipe_id : -1,
           (const void *)table_hdl,
           (const void *)session_hdl,
           key_field_id,
           action_id,
           data_field_id);
    printf("  [%s] ipv4_addr_u32=%" PRIu32 " ipv4_addr_str=%s egress_port=%u\n",
           table_name, ipv4_addr, ipv4_str ? ipv4_str : "(null)", egress_port);
    fflush(stdout);
}

// Variables of monitoring counter
const bf_rt_table_hdl *counter_table = NULL;
bf_rt_table_key_hdl *counter_table_key;
bf_rt_table_data_hdl *counter_table_data;
bf_rt_id_t counter_pkts_field_id = 0;

uint32_t ipv4AddrToUint32(const char *ip_str)
{
    uint32_t ip_addr;
    if (inet_pton(AF_INET, ip_str, &ip_addr) != 1)
    {
        fprintf(stderr, "Error: Invalid IP address format.\n");
        return EXIT_FAILURE;
    }

    return ntohl(ip_addr);
}

static bf_dev_port_t fp_port_to_dev_port(bf_dev_id_t dev_id, const char *fp_port)
{
    bf_status_t bf_status;
    bf_dev_port_t dev_port = 0;
    char port_str[32];

    if (snprintf(port_str, sizeof(port_str), "%s", fp_port) >= (int)sizeof(port_str))
    {
        fprintf(stderr, "Front-panel port string is too long: %s\n", fp_port);
        exit(EXIT_FAILURE);
    }

    bf_status = bf_pm_port_str_to_dev_port_get(dev_id, port_str, &dev_port);
    if (bf_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "Failed to map front-panel port %s to dev port, status=%d\n",
                fp_port, bf_status);
        exit(EXIT_FAILURE);
    }

    return dev_port;
}

static uint32_t dev_port_pipe(uint32_t dev_port)
{
    return (dev_port >> 7) & 0x7;
}

static uint32_t dev_port_local_port(uint32_t dev_port)
{
    return dev_port & 0x7f;
}

static bool dev_port_in_front_panel_list(bf_dev_id_t dev_id,
                                         uint32_t dev_port,
                                         const switch_port_t *port_list,
                                         const uint8_t port_count,
                                         char *matched_fp_port,
                                         size_t matched_fp_port_len)
{
    for (uint8_t idx = 0; idx < port_count; idx++)
    {
        bf_dev_port_t fp_dev_port =
            fp_port_to_dev_port(dev_id, port_list[idx].fp_port);
        if ((uint32_t)fp_dev_port == dev_port)
        {
            if (matched_fp_port != NULL && matched_fp_port_len > 0)
            {
                strncpy(matched_fp_port, port_list[idx].fp_port,
                        matched_fp_port_len - 1);
                matched_fp_port[matched_fp_port_len - 1] = '\0';
            }
            return true;
        }
    }
    return false;
}

static void print_recirc_port_inventory(bf_dev_id_t dev_id,
                                        const switch_port_t *port_list,
                                        const uint8_t port_count)
{
    uint32_t recirc_devports[30] = {0};
    uint32_t recirc_count;
    bool configured_is_recirc = false;
    bool configured_is_front_panel = false;
    char configured_fp_port[32] = "";
    bf_status_t bf_status;
    uint32_t start_recirc_port = 0;
    uint32_t end_recirc_port = 0;

    printf("[recirc-port] configured RECIRC_PORT=%u "
           "(pipe=%u local_port=%u). This is a Tofino dev_port, not an IP address.\n",
           (uint32_t)RECIRC_PORT,
           dev_port_pipe((uint32_t)RECIRC_PORT),
           dev_port_local_port((uint32_t)RECIRC_PORT));
    printf("[recirc-port] header.p4 also has const PortId_t RECIRC_PORT; "
           "keep header.p4 and config.h synchronized when changing it.\n");

    printf("[recirc-port] front-panel ports currently added by this control plane:\n");
    for (uint8_t idx = 0; idx < port_count; idx++)
    {
        bf_dev_port_t fp_dev_port =
            fp_port_to_dev_port(dev_id, port_list[idx].fp_port);
        printf("[recirc-port]   fp_port=%s dev_port=%u pipe=%u local_port=%u%s\n",
               port_list[idx].fp_port,
               (uint32_t)fp_dev_port,
               dev_port_pipe((uint32_t)fp_dev_port),
               dev_port_local_port((uint32_t)fp_dev_port),
               (uint32_t)fp_dev_port == (uint32_t)RECIRC_PORT
                   ? "  <-- WARNING: matches RECIRC_PORT"
                   : "");
    }

    recirc_count = bf_pm_recirc_devports_get(dev_id, recirc_devports);
    printf("[recirc-port] bf_pm_recirc_devports_get(dev_id=%d) returned %u ports\n",
           dev_id, recirc_count);
    for (uint32_t idx = 0; idx < recirc_count && idx < ARRLEN(recirc_devports); idx++)
    {
        bool is_current = recirc_devports[idx] == (uint32_t)RECIRC_PORT;
        char fp_port[32] = "";
        bool is_front_panel = dev_port_in_front_panel_list(dev_id,
                                                           recirc_devports[idx],
                                                           port_list,
                                                           port_count,
                                                           fp_port,
                                                           sizeof(fp_port));
        printf("[recirc-port]   recirc[%u] dev_port=%u pipe=%u local_port=%u%s%s%s\n",
               idx,
               recirc_devports[idx],
               dev_port_pipe(recirc_devports[idx]),
               dev_port_local_port(recirc_devports[idx]),
               is_current ? "  <-- configured RECIRC_PORT" : "",
               is_front_panel ? "  <-- WARNING: also in PORT_LIST as " : "",
               is_front_panel ? fp_port : "");
        configured_is_recirc = configured_is_recirc || is_current;
    }

    configured_is_front_panel = dev_port_in_front_panel_list(dev_id,
                                                            (uint32_t)RECIRC_PORT,
                                                            port_list,
                                                            port_count,
                                                            configured_fp_port,
                                                            sizeof(configured_fp_port));
    if (!configured_is_recirc)
    {
        printf("[recirc-port] WARNING: configured RECIRC_PORT=%u was NOT returned by "
               "bf_pm_recirc_devports_get(). Packets sent there may not recirculate.\n",
               (uint32_t)RECIRC_PORT);
        if (recirc_count > 0)
        {
            printf("[recirc-port] suggested next test: set RECIRC_PORT to one returned "
                   "dev_port, for example %u, in both dataplane/header.p4 and config.h, "
                   "then rebuild/reinstall P4 and control.\n",
                   recirc_devports[0]);
        }
    }
    else
    {
        printf("[recirc-port] OK: configured RECIRC_PORT=%u is in the SDE recirc "
               "dev_port list.\n",
               (uint32_t)RECIRC_PORT);
    }

    if (configured_is_front_panel)
    {
        printf("[recirc-port] WARNING: configured RECIRC_PORT=%u also maps to "
               "front-panel port %s in PORT_LIST. Do not use a normal host-facing "
               "front-panel port as the recirculation egress port.\n",
               (uint32_t)RECIRC_PORT, configured_fp_port);
    }

    bf_status = bf_pm_recirc_port_range_get(dev_id,
                                            &start_recirc_port,
                                            &end_recirc_port);
    if (bf_status == BF_SUCCESS)
    {
        printf("[recirc-port] recirc index range: start=%u end=%u\n",
               start_recirc_port, end_recirc_port);
        if (end_recirc_port >= start_recirc_port &&
            end_recirc_port - start_recirc_port < 64)
        {
            for (uint32_t recirc_port = start_recirc_port;
                 recirc_port <= end_recirc_port;
                 recirc_port++)
            {
                bf_dev_port_t dev_port = 0;
                bf_status = bf_pm_recirc_port_to_dev_port_get(dev_id,
                                                              recirc_port,
                                                              &dev_port);
                printf("[recirc-port]   recirc_index=%u map_status=%d dev_port=%u "
                       "pipe=%u local_port=%u%s\n",
                       recirc_port,
                       bf_status,
                       (uint32_t)dev_port,
                       dev_port_pipe((uint32_t)dev_port),
                       dev_port_local_port((uint32_t)dev_port),
                       (uint32_t)dev_port == (uint32_t)RECIRC_PORT
                           ? "  <-- configured RECIRC_PORT"
                           : "");
            }
        }
    }
    else
    {
        printf("[recirc-port] bf_pm_recirc_port_range_get failed, status=%d\n",
               bf_status);
    }

    printf("[recirc-port] note: front-panel route targets in act_l3_table "
           "cannot be used as RECIRC_PORT.\n");
    fflush(stdout);
}

static void recirculation_setup(const bf_rt_target_t *dev_tgt)
{
    p4_pd_status_t pd_status;
    p4_pd_sess_hdl_t pd_session;
    bf_status_t bf_status;

    bf_status = bf_port_add(dev_tgt->dev_id,
                            (bf_dev_port_t)RECIRC_PORT,
                            BF_SPEED_50G,
                            BF_FEC_TYP_NONE);
    printf("[recirc-port] bf_port_add(dev_id=%d, dev_port=%u, speed=50G) "
           "status=%d%s\n",
           dev_tgt->dev_id,
           (uint32_t)RECIRC_PORT,
           bf_status,
           bf_status == BF_SUCCESS ? " (BF_SUCCESS)" :
           bf_status == BF_ALREADY_EXISTS ? " (BF_ALREADY_EXISTS)" : "");
    if (bf_status != BF_SUCCESS && bf_status != BF_ALREADY_EXISTS)
    {
        fprintf(stderr,
                "[recirc-port] failed to add internal recirculation dev_port=%u. "
                "This means the switch/SDE did not accept this recirc port as an "
                "added PGR port.\n",
                (uint32_t)RECIRC_PORT);
        exit(EXIT_FAILURE);
    }

    bf_status = bf_port_enable(dev_tgt->dev_id,
                               (bf_dev_port_t)RECIRC_PORT,
                               true);
    printf("[recirc-port] bf_port_enable(dev_id=%d, dev_port=%u) status=%d%s\n",
           dev_tgt->dev_id,
           (uint32_t)RECIRC_PORT,
           bf_status,
           bf_status == BF_SUCCESS ? " (BF_SUCCESS)" : "");
    if (bf_status != BF_SUCCESS)
    {
        printf("[recirc-port] WARNING: bf_port_enable failed for internal "
               "recirculation dev_port=%u. This is expected on some Tofino2/SDE "
               "stacks because recirc/PGR ports are not normal front-panel ports. "
               "Continuing because bf_port_add succeeded.\n",
               (uint32_t)RECIRC_PORT);
    }

    pd_status = p4_pd_client_init(&pd_session);
    if (pd_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "[recirc-port] p4_pd_client_init for recirculation failed, status=%d\n",
                pd_status);
        exit(EXIT_FAILURE);
    }

    pd_status = p4_pd_recirculation_enable(pd_session,
                                           dev_tgt->dev_id,
                                           (uint32_t)RECIRC_PORT);
    printf("[recirc-port] p4_pd_recirculation_enable(dev_id=%d, port=%u) "
           "status=%d%s\n",
           dev_tgt->dev_id,
           (uint32_t)RECIRC_PORT,
           pd_status,
           pd_status == BF_SUCCESS ? " (BF_SUCCESS)" : "");
    if (pd_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "[recirc-port] failed to enable recirculation on RECIRC_PORT=%u. "
                "Use one of the dev_ports returned by bf_pm_recirc_devports_get().\n",
                (uint32_t)RECIRC_PORT);
        exit(EXIT_FAILURE);
    }

    pd_status = p4_pd_complete_operations(pd_session);
    if (pd_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "[recirc-port] p4_pd_complete_operations after recirculation enable "
                "failed, status=%d\n",
                pd_status);
        exit(EXIT_FAILURE);
    }

    pd_status = p4_pd_client_cleanup(pd_session);
    if (pd_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "[recirc-port] p4_pd_client_cleanup after recirculation enable "
                "failed, status=%d\n",
                pd_status);
        exit(EXIT_FAILURE);
    }
}

// table operation for act_l3_table
static void act_l3_table_setup(const bf_rt_target_t *dev_tgt,
                               const bf_rt_info_hdl *bfrt_info,
                               const bf_rt_table_hdl **act_l3_table,
                               act_l3_table_info_t *act_l3_table_info,
                               const act_l3_table_list_tuple_t *act_l3_table_list,
                               const uint8_t list_size)
{
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Ingress.l3_table",
                                          act_l3_table);
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
        act_l3_table_info->aid_forward, &act_l3_table_info->did_egress_port);
    assert(bf_status == BF_SUCCESS);
}

static void act_l3_table_entry_add(const bf_rt_target_t *dev_tgt,
                                   const bf_rt_session_hdl *session,
                                   const bf_rt_table_hdl *act_l3_table,
                                   act_l3_table_info_t *act_l3_table_info,
                                   act_l3_table_entry_t *act_l3_table_entry,
                                   const char *ipv4_str)
{
    bf_status_t bf_status;
    bf_rt_table_key_hdl *key = NULL;
    bf_rt_table_data_hdl *data = NULL;

    printf("  act_l3_table: allocating key\n");
    fflush(stdout);
    bf_status = bf_rt_table_key_allocate(act_l3_table, &key);
    check_bf_status(bf_status, "act_l3_table key allocate");

    // Fill in the Key object
    printf("  act_l3_table: setting dst key\n");
    fflush(stdout);
    bf_status = bf_rt_key_field_set_value(key,
                                          act_l3_table_info->kid_ipv4_dst_ip,
                                          act_l3_table_entry->ipv4_addr);
    check_bf_status(bf_status, "act_l3_table set dst_addr key");

    if (strcmp(act_l3_table_entry->action, "forward") == 0)
    {
        printf("  act_l3_table: allocating action data\n");
        fflush(stdout);
        bf_status = bf_rt_table_action_data_allocate(
            act_l3_table, act_l3_table_info->aid_forward, &data);
        check_bf_status(bf_status, "act_l3_table action data allocate");

        // Fill in the Data object
    printf("  act_l3_table: setting egress_port data\n");
    fflush(stdout);
    bf_status = bf_rt_data_field_set_value(data,
                                           act_l3_table_info->did_egress_port,
                                           act_l3_table_entry->egress_port);
    check_bf_status(bf_status, "act_l3_table set egress_port data");
    }
    else
    {
        fprintf(stderr, "Unsupported act_l3_table action: %s\n",
                act_l3_table_entry->action);
        exit(EXIT_FAILURE);
    }
    log_table_entry_context("act_l3_table", dev_tgt, session, act_l3_table,
                            act_l3_table_info->kid_ipv4_dst_ip,
                            act_l3_table_info->aid_forward,
                            act_l3_table_info->did_egress_port,
                            act_l3_table_entry->ipv4_addr,
                            ipv4_str,
                            act_l3_table_entry->egress_port);
    // Call table entry add API
    printf("  act_l3_table: adding entry\n");
    fflush(stdout);
    bf_status = table_entry_add_compat(act_l3_table, session, dev_tgt,
                                       key, data);
    check_bf_status(bf_status, "act_l3_table entry add");
    printf("  act_l3_table: completing operations\n");
    fflush(stdout);
    bf_status = bf_rt_session_complete_operations(session);
    check_bf_status(bf_status, "act_l3_table complete operations");

    bf_status = bf_rt_table_key_deallocate(key);
    check_bf_status(bf_status, "act_l3_table key deallocate");
    bf_status = bf_rt_table_data_deallocate(data);
    check_bf_status(bf_status, "act_l3_table data deallocate");
}

// prefix: act_l3_table
static void act_l3_table_deploy(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_session_hdl *session,
                                const act_l3_table_list_tuple_t *act_l3_table_list,
                                const uint8_t list_size)
{
    bf_status_t bf_status;
    const bf_rt_table_hdl *act_l3_table = NULL;
    act_l3_table_info_t act_l3_table_info;
    memset(&act_l3_table_info, 0, sizeof(act_l3_table_info));

    // Set up the act_l3_table
    act_l3_table_setup(dev_tgt, bfrt_info, &act_l3_table,
                       &act_l3_table_info, act_l3_table_list, list_size);
    printf("act_l3_table is set up correctly!\n");

    // Add act_l3_table entries
    for (unsigned int idx = 0; idx < list_size; idx++)
    {

        act_l3_table_entry_t act_l3_table_entry = {
            .ipv4_addr = ipv4AddrToUint32(act_l3_table_list[idx].ipv4_addr),
            .action = "forward",
            .egress_port = fp_port_to_dev_port(dev_tgt->dev_id,
                                               act_l3_table_list[idx].egress_fp_port),
        };
        printf("Adding act_l3_table entry[%u]: dst=%s, fp_port=%s, dev_port=%d\n",
               idx, act_l3_table_list[idx].ipv4_addr,
               act_l3_table_list[idx].egress_fp_port,
               act_l3_table_entry.egress_port);
        fflush(stdout);
        act_l3_table_entry_add(dev_tgt, session, act_l3_table,
                               &act_l3_table_info, &act_l3_table_entry,
                               act_l3_table_list[idx].ipv4_addr);
    }
    printf("act_l3_table is deployed correctly!\n");
}

static void act_arp_table_setup(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_table_hdl **act_arp_table,
                                act_arp_table_info_t *act_arp_table_info,
                                const act_arp_table_list_tuple_t *act_arp_table_list,
                                const uint8_t list_size)
{
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Ingress.arp_table",
                                          act_arp_table);
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
        act_arp_table_info->aid_forward, &act_arp_table_info->did_egress_port);
    assert(bf_status == BF_SUCCESS);
}

static void act_arp_table_entry_add(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *act_arp_table,
                                    act_arp_table_info_t *act_arp_table_info,
                                    act_arp_table_entry_t *act_arp_table_entry,
                                    const char *ipv4_str)
{
    bf_status_t bf_status;
    bf_rt_table_key_hdl *key = NULL;
    bf_rt_table_data_hdl *data = NULL;

    printf("  act_arp_table: allocating key\n");
    fflush(stdout);
    bf_status = bf_rt_table_key_allocate(act_arp_table, &key);
    check_bf_status(bf_status, "act_arp_table key allocate");

    // Fill in the Key object
    printf("  act_arp_table: setting dst key\n");
    fflush(stdout);
    bf_status = bf_rt_key_field_set_value(key,
                                          act_arp_table_info->kid_ipv4_dst_ip,
                                          act_arp_table_entry->ipv4_addr);
    check_bf_status(bf_status, "act_arp_table set dst IPv4 key");

    if (strcmp(act_arp_table_entry->action, "forward") == 0)
    {
        printf("  act_arp_table: allocating action data\n");
        fflush(stdout);
        bf_status = bf_rt_table_action_data_allocate(
            act_arp_table, act_arp_table_info->aid_forward, &data);
        check_bf_status(bf_status, "act_arp_table action data allocate");

        // Fill in the Data object
        printf("  act_arp_table: setting egress_port data\n");
        fflush(stdout);
        bf_status = bf_rt_data_field_set_value(data,
                                               act_arp_table_info->did_egress_port,
                                               act_arp_table_entry->egress_port);
        check_bf_status(bf_status, "act_arp_table set egress_port data");
    }
    else
    {
        fprintf(stderr, "Unsupported act_arp_table action: %s\n",
                act_arp_table_entry->action);
        exit(EXIT_FAILURE);
    }
    log_table_entry_context("act_arp_table", dev_tgt, session, act_arp_table,
                            act_arp_table_info->kid_ipv4_dst_ip,
                            act_arp_table_info->aid_forward,
                            act_arp_table_info->did_egress_port,
                            act_arp_table_entry->ipv4_addr,
                            ipv4_str,
                            act_arp_table_entry->egress_port);
    // Call table entry add API
    printf("  act_arp_table: adding entry\n");
    fflush(stdout);
    bf_status = table_entry_add_compat(act_arp_table, session, dev_tgt,
                                       key, data);
    check_bf_status(bf_status, "act_arp_table entry add");
    printf("  act_arp_table: completing operations\n");
    fflush(stdout);
    bf_status = bf_rt_session_complete_operations(session);
    check_bf_status(bf_status, "act_arp_table complete operations");

    bf_status = bf_rt_table_key_deallocate(key);
    check_bf_status(bf_status, "act_arp_table key deallocate");
    bf_status = bf_rt_table_data_deallocate(data);
    check_bf_status(bf_status, "act_arp_table data deallocate");
}

// prefix: act_arp_table
static void act_arp_table_deploy(const bf_rt_target_t *dev_tgt,
                                 const bf_rt_info_hdl *bfrt_info,
                                 const bf_rt_session_hdl *session,
                                 const act_arp_table_list_tuple_t *act_arp_table_list,
                                 const uint8_t list_size)
{
    bf_status_t bf_status;
    const bf_rt_table_hdl *act_arp_table = NULL;
    act_arp_table_info_t act_arp_table_info;
    memset(&act_arp_table_info, 0, sizeof(act_arp_table_info));

    // Set up the act_arp_table
    act_arp_table_setup(dev_tgt, bfrt_info, &act_arp_table,
                        &act_arp_table_info, act_arp_table_list, list_size);
    printf("act_arp_table is set up correctly!\n");

    // Add act_arp_table entries
    for (unsigned int idx = 0; idx < list_size; idx++)
    {

        act_arp_table_entry_t act_arp_table_entry = {
            .ipv4_addr = ipv4AddrToUint32(act_arp_table_list[idx].ipv4_addr),
            .action = "forward",
            .egress_port = fp_port_to_dev_port(dev_tgt->dev_id,
                                               act_arp_table_list[idx].egress_fp_port),
        };
        printf("Adding act_arp_table entry[%u]: dst=%s, fp_port=%s, dev_port=%d\n",
               idx, act_arp_table_list[idx].ipv4_addr,
               act_arp_table_list[idx].egress_fp_port,
               act_arp_table_entry.egress_port);
        fflush(stdout);
        act_arp_table_entry_add(dev_tgt, session, act_arp_table,
                                &act_arp_table_info, &act_arp_table_entry,
                                act_arp_table_list[idx].ipv4_addr);
    }
    printf("act_arp_table is deployed correctly!\n");
}

// table operation for detect_ecn
static void detect_ecn_setup(const bf_rt_target_t *dev_tgt,
                             const bf_rt_info_hdl *bfrt_info,
                             const bf_rt_table_hdl **detect_ecn,
                             detect_ecn_info_t *detect_ecn_info,
                             const detect_ecn_list_tuple_t *detect_ecn_list,
                             const uint8_t list_size)
{
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
                                 detect_ecn_entry_t *detect_ecn_entry)
{
    bf_status_t bf_status;

    // Reset key before use
    bf_rt_table_key_reset(detect_ecn, &detect_ecn_info->key);

    // Fill in the Key object
    bf_status = bf_rt_key_field_set_value(detect_ecn_info->key,
                                          detect_ecn_info->kid_ecn_value,
                                          detect_ecn_entry->ecn_value);
    assert(bf_status == BF_SUCCESS);

    if (strcmp(detect_ecn_entry->action, "set_mirror_md") == 0)
    {
        // Reset data before use
        bf_rt_table_action_data_reset(detect_ecn,
                                      detect_ecn_info->aid_set_mirror_md,
                                      &detect_ecn_info->data);
        // No Data object needs to be filled
    }
    // Call table entry add API
    bf_status = table_entry_add_compat(detect_ecn, session, dev_tgt,
                                       detect_ecn_info->key,
                                       detect_ecn_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

// prefix: detect_ecn
// LEGACY HELPER RETAINED BUT UNUSED:
// The active P4 program no longer exposes/uses Ingress.detect_ecn for CNP
// generation. This helper is intentionally left in source for reference to the
// old ECN-triggered deployment path, but main() does not call it.
static void detect_ecn_deploy(const bf_rt_target_t *dev_tgt,
                              const bf_rt_info_hdl *bfrt_info,
                              const bf_rt_session_hdl *session,
                              const detect_ecn_list_tuple_t *detect_ecn_list,
                              const uint8_t list_size)
{
    bf_status_t bf_status;
    const bf_rt_table_hdl *detect_ecn = NULL;
    detect_ecn_info_t detect_ecn_info;

    // Set up the detect_ecn
    detect_ecn_setup(dev_tgt, bfrt_info, &detect_ecn,
                     &detect_ecn_info, detect_ecn_list, list_size);
    printf("detect_ecn is set up correctly!\n");

    // Add detect_ecn entries
    for (unsigned int idx = 0; idx < list_size; idx++)
    {

        detect_ecn_entry_t detect_ecn_entry = {
            .ecn_value = detect_ecn_list[idx].ecn_value,
            .action = "set_mirror_md"};
        detect_ecn_entry_add(dev_tgt, session, detect_ecn,
                             &detect_ecn_info, &detect_ecn_entry);
    }
    printf("detect_ecn is deployed correctly!\n");
}

const bf_rt_table_hdl *trp_cnp_store = NULL;
trp_cnp_store_info_t trp_cnp_store_info;

// LEGACY HELPER RETAINED BUT UNUSED:
// The active P4 program no longer uses Ingress.trp_cnp_store. These helpers are
// kept as reference for the old control-plane-selected recirculation path, but
// main() does not call them.
static void trp_cnp_store_setup(const bf_rt_target_t *dev_tgt,
                                const bf_rt_info_hdl *bfrt_info,
                                const bf_rt_table_hdl **trp_cnp_store,
                                trp_cnp_store_info_t *trp_cnp_store_info)
{
    bf_status_t bf_status;

    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Ingress.trp_cnp_store",
                                          trp_cnp_store);
    assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_table_key_allocate(*trp_cnp_store,
                                         &trp_cnp_store_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_table_data_allocate(*trp_cnp_store,
                                          &trp_cnp_store_info->data);
    assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_key_field_id_get(*trp_cnp_store, "hdr.ipv4.dst_addr",
                                       &trp_cnp_store_info->kid_dst_ip);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_key_field_id_get(*trp_cnp_store, "hdr.bth.dst_qpn",
                                       &trp_cnp_store_info->kid_dst_qpn);
    assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_action_name_to_id(*trp_cnp_store,
                                        "Ingress.trp_set_loop",
                                        &trp_cnp_store_info->aid_trp_set_loop);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_data_field_id_with_action_get(
        *trp_cnp_store, "loop_num",
        trp_cnp_store_info->aid_trp_set_loop,
        &trp_cnp_store_info->did_loop_num);
    assert(bf_status == BF_SUCCESS);
}

static void trp_cnp_store_entry_add(const bf_rt_target_t *dev_tgt,
                                    const bf_rt_session_hdl *session,
                                    const bf_rt_table_hdl *trp_cnp_store,
                                    trp_cnp_store_info_t *trp_cnp_store_info,
                                    trp_cnp_store_entry_t *entry)
{
    bf_status_t bf_status;

    bf_rt_table_key_reset(trp_cnp_store, &trp_cnp_store_info->key);
    bf_status = bf_rt_key_field_set_value(trp_cnp_store_info->key,
                                          trp_cnp_store_info->kid_dst_ip,
                                          entry->dst_ip);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_key_field_set_value(trp_cnp_store_info->key,
                                          trp_cnp_store_info->kid_dst_qpn,
                                          entry->dst_qpn);
    assert(bf_status == BF_SUCCESS);

    bf_status = bf_rt_table_action_data_reset(trp_cnp_store,
                                              trp_cnp_store_info->aid_trp_set_loop,
                                              &trp_cnp_store_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_data_field_set_value(trp_cnp_store_info->data,
                                           trp_cnp_store_info->did_loop_num,
                                           entry->loop_num);
    assert(bf_status == BF_SUCCESS);

    bf_status = table_entry_add_compat(trp_cnp_store, session, dev_tgt,
                                       trp_cnp_store_info->key,
                                       trp_cnp_store_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

static void count_cnp_setup(const bf_rt_target_t *dev_tgt,
                            const bf_rt_info_hdl *bfrt_info,
                            const bf_rt_table_hdl **count_cnp,
                            count_cnp_info_t *count_cnp_info,
                            const count_cnp_list_tuple_t *count_cnp_list,
                            const uint8_t list_size)
{
    bf_status_t bf_status;

    // Get table object from name
    bf_status = bf_rt_table_from_name_get(bfrt_info,
                                          "Egress.count_cnp",
                                          count_cnp);
    assert(bf_status == BF_SUCCESS);

    // Allocate key and data once, and use reset across different uses
    bf_status = bf_rt_table_key_allocate(*count_cnp,
                                         &count_cnp_info->key);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_table_data_allocate(*count_cnp,
                                          &count_cnp_info->data);
    assert(bf_status == BF_SUCCESS);

    // Get field-ids for key field

    bf_status = bf_rt_key_field_id_get(*count_cnp, "hdr.bth.opcode",
                                       &count_cnp_info->kid_opcode);
    assert(bf_status == BF_SUCCESS);

    // Get action Ids for action forward
    bf_status = bf_rt_action_name_to_id(*count_cnp,
                                        "Egress.inc_cnp_counter",
                                        &count_cnp_info->aid_inc_cnp_counter);
    assert(bf_status == BF_SUCCESS);
}

static void count_cnp_entry_add(const bf_rt_target_t *dev_tgt,
                                const bf_rt_session_hdl *session,
                                const bf_rt_table_hdl *count_cnp,
                                count_cnp_info_t *count_cnp_info,
                                count_cnp_entry_t *count_cnp_entry)
{
    bf_status_t bf_status;

    // Reset key before use
    bf_rt_table_key_reset(count_cnp, &count_cnp_info->key);

    // Fill in the Key object
    bf_status = bf_rt_key_field_set_value(count_cnp_info->key,
                                          count_cnp_info->kid_opcode,
                                          count_cnp_entry->opcode);
    assert(bf_status == BF_SUCCESS);

    if (strcmp(count_cnp_entry->action, "inc_cnp_counter") == 0)
    {
        // Reset data before use
        bf_rt_table_action_data_reset(count_cnp,
                                      count_cnp_info->aid_inc_cnp_counter,
                                      &count_cnp_info->data);
    }
    // Call table entry add API
    bf_status = table_entry_add_compat(count_cnp, session, dev_tgt,
                                       count_cnp_info->key,
                                       count_cnp_info->data);
    assert(bf_status == BF_SUCCESS);
    bf_status = bf_rt_session_complete_operations(session);
    assert(bf_status == BF_SUCCESS);
}

// prefix: count_cnp
static void count_cnp_deploy(const bf_rt_target_t *dev_tgt,
                             const bf_rt_info_hdl *bfrt_info,
                             const bf_rt_session_hdl *session,
                             const count_cnp_list_tuple_t *count_cnp_list,
                             const uint8_t list_size)
{

    bf_status_t bf_status;
    const bf_rt_table_hdl *count_cnp = NULL;
    count_cnp_info_t count_cnp_info;

    // Set up the count_cnp
    count_cnp_setup(dev_tgt, bfrt_info, &count_cnp,
                    &count_cnp_info, count_cnp_list, list_size);
    printf("count_cnp is set up correctly!\n");

    // Add count_cnp entries
    for (unsigned int idx = 0; idx < list_size; idx++)
    {
        count_cnp_entry_t count_cnp_entry = {
            .opcode = count_cnp_list[idx].opcode,
            .action = "inc_cnp_counter"};
        count_cnp_entry_add(dev_tgt, session, count_cnp,
                            &count_cnp_info, &count_cnp_entry);
    }
    printf("count_cnp is deployed correctly!\n");
}

static bf_status_t counter_table_sync(const bf_rt_table_hdl *table)
{
    bf_status_t bf_status = BF_SUCCESS;
    bf_rt_table_operations_hdl *operation_hdl;
    bf_status = bf_rt_table_operations_allocate(
        table, BFRT_COUNTER_SYNC, &operation_hdl);
    if (bf_status != BF_SUCCESS)
    {
        return bf_status;
    }

    bf_status = bf_rt_operations_counter_sync_set(
        operation_hdl, *session, dev_tgt, NULL, NULL);
    if (bf_status != BF_SUCCESS)
    {
        return bf_status;
    }

    bf_status = bf_rt_table_operations_execute(table, operation_hdl);
    return bf_status;
}

static bf_status_t find_counter_table(const char *const *names,
                                      size_t name_count,
                                      const bf_rt_table_hdl **table,
                                      const char **matched_name)
{
    bf_status_t status = BF_OBJECT_NOT_FOUND;

    for (size_t i = 0; i < name_count; i++)
    {
        status = bf_rt_table_from_name_get(bfrt_info, names[i], table);
        if (status == BF_SUCCESS)
        {
            *matched_name = names[i];
            return BF_SUCCESS;
        }
    }
    return status;
}

static bf_status_t read_counter_index(const bf_rt_table_hdl *table,
                                      uint32_t index,
                                      uint64_t *value)
{
    bf_status_t bf_status;
    bf_rt_table_key_hdl *key = NULL;
    bf_rt_table_data_hdl *data = NULL;
    bf_rt_id_t counter_index_field_id = 0;
    bf_rt_id_t counter_pkts_field_id_local = 0;

    *value = 0;

    bf_status = bf_rt_key_field_id_get(table, "$COUNTER_INDEX",
                                       &counter_index_field_id);
    if (bf_status != BF_SUCCESS)
    {
        return bf_status;
    }

    bf_status = bf_rt_data_field_id_get(table, "$COUNTER_SPEC_PKTS",
                                        &counter_pkts_field_id_local);
    if (bf_status != BF_SUCCESS)
    {
        return bf_status;
    }

    bf_status = bf_rt_table_key_allocate(table, &key);
    if (bf_status != BF_SUCCESS)
    {
        return bf_status;
    }

    bf_status = bf_rt_table_data_allocate(table, &data);
    if (bf_status != BF_SUCCESS)
    {
        bf_rt_table_key_deallocate(key);
        return bf_status;
    }

    bf_status = bf_rt_key_field_set_value(key, counter_index_field_id, index);
    if (bf_status == BF_SUCCESS)
    {
        bf_status = bf_rt_table_entry_get(table, *session, dev_tgt, 0, key, data);
    }
    if (bf_status == BF_SUCCESS)
    {
        bf_status = bf_rt_data_field_get_value(data, counter_pkts_field_id_local,
                                               value);
    }

    bf_rt_table_data_deallocate(data);
    bf_rt_table_key_deallocate(key);
    return bf_status;
}

static void monitor_debug_counters(void)
{
    static const bf_rt_table_hdl *recir_debug_counter = NULL;
    static const bf_rt_table_hdl *recir_return_counter = NULL;
    static const bf_rt_table_hdl *recir_return_roce_counter = NULL;
    static const bf_rt_table_hdl *cnp_debug_counter = NULL;
    static const bf_rt_table_hdl *egress_cnp_counter = NULL;
    static const bf_rt_table_hdl *ingress_port_debug_counter = NULL;
    static const bf_rt_table_hdl *egress_mirror_seen_counter = NULL;
    static const bf_rt_table_hdl *egress_mirror_bth_counter = NULL;
    static const bf_rt_table_hdl *egress_mirror_cnp_set_counter = NULL;
    static const bf_rt_table_hdl *egress_roce_data_counter = NULL;
    static const char *recir_debug_name = NULL;
    static const char *recir_return_name = NULL;
    static const char *recir_return_roce_name = NULL;
    static const char *cnp_debug_name = NULL;
    static const char *egress_cnp_name = NULL;
    static const char *ingress_port_debug_name = NULL;
    static const char *egress_mirror_seen_name = NULL;
    static const char *egress_mirror_bth_name = NULL;
    static const char *egress_mirror_cnp_set_name = NULL;
    static const char *egress_roce_data_name = NULL;
    static int initialized = 0;
    static uint64_t last_recir_start = 0;
    static uint64_t last_recir_return = 0;
    static uint64_t last_recir_return_roce = 0;
    static uint64_t last_cnp_mirror = 0;
    static uint64_t last_egress_cnp = 0;
    static uint64_t last_egress_mirror_seen = 0;
    static uint64_t last_egress_mirror_bth = 0;
    static uint64_t last_egress_mirror_cnp_set = 0;
    static uint64_t last_egress_roce_data = 0;
    static uint64_t last_ingress_port_counts[512] = {0};
    static uint64_t last_mac_rx_counts[ARRLEN(PORT_LIST)] = {0};
    uint64_t recir_start = 0;
    uint64_t recir_return = 0;
    uint64_t recir_return_roce = 0;
    uint64_t cnp_mirror = 0;
    uint64_t egress_cnp = 0;
    uint64_t egress_mirror_seen = 0;
    uint64_t egress_mirror_bth = 0;
    uint64_t egress_mirror_cnp_set = 0;
    uint64_t egress_roce_data = 0;
    uint64_t configured_recirc_ingress = 0;
    uint64_t configured_recirc_ingress_delta = 0;
    bf_status_t status;

    if (!initialized)
    {
        const char *recir_names[] = {
            "Ingress.recir_debug_counter",
            "recir_debug_counter"
        };
        const char *recir_return_names[] = {
            "Ingress.recir_return_counter",
            "recir_return_counter"
        };
        const char *recir_return_roce_names[] = {
            "Ingress.recir_return_roce_counter",
            "recir_return_roce_counter"
        };
        const char *cnp_debug_names[] = {
            "Ingress.cnp_debug_counter",
            "cnp_debug_counter"
        };
        const char *cnp_names[] = {
            "Egress.indirect_counter",
            "indirect_counter"
        };
        const char *ingress_port_names[] = {
            "Ingress.ingress_port_debug_counter",
            "ingress_port_debug_counter"
        };
        const char *egress_mirror_seen_names[] = {
            "Egress.egress_mirror_seen_counter",
            "egress_mirror_seen_counter"
        };
        const char *egress_mirror_bth_names[] = {
            "Egress.egress_mirror_bth_counter",
            "egress_mirror_bth_counter"
        };
        const char *egress_mirror_cnp_set_names[] = {
            "Egress.egress_mirror_cnp_set_counter",
            "egress_mirror_cnp_set_counter"
        };
        const char *egress_roce_data_names[] = {
            "Egress.egress_roce_data_counter",
            "egress_roce_data_counter"
        };

        status = find_counter_table(recir_names, ARRLEN(recir_names),
                                    &recir_debug_counter,
                                    &recir_debug_name);
        if (status == BF_SUCCESS)
        {
            printf("[debug-counter] using recir debug counter table: %s\n",
                   recir_debug_name);
        }
        else
        {
            printf("[debug-counter] recir debug counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(recir_return_names, ARRLEN(recir_return_names),
                                    &recir_return_counter,
                                    &recir_return_name);
        if (status == BF_SUCCESS)
        {
            printf("[debug-counter] using recir return counter table: %s\n",
                   recir_return_name);
        }
        else
        {
            printf("[debug-counter] recir return counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(recir_return_roce_names, ARRLEN(recir_return_roce_names),
                                    &recir_return_roce_counter,
                                    &recir_return_roce_name);
        if (status == BF_SUCCESS)
        {
            printf("[debug-counter] using recir return RoCE counter table: %s\n",
                   recir_return_roce_name);
        }
        else
        {
            printf("[debug-counter] recir return RoCE counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(cnp_debug_names, ARRLEN(cnp_debug_names),
                                    &cnp_debug_counter,
                                    &cnp_debug_name);
        if (status == BF_SUCCESS)
        {
            printf("[debug-counter] using CNP mirror debug counter table: %s\n",
                   cnp_debug_name);
        }
        else
        {
            printf("[debug-counter] CNP mirror debug counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(cnp_names, ARRLEN(cnp_names),
                                    &egress_cnp_counter,
                                    &egress_cnp_name);
        if (status == BF_SUCCESS)
        {
            printf("[debug-counter] using egress CNP counter table: %s\n",
                   egress_cnp_name);
        }
        else
        {
            printf("[debug-counter] egress CNP counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(ingress_port_names, ARRLEN(ingress_port_names),
                                    &ingress_port_debug_counter,
                                    &ingress_port_debug_name);
        if (status == BF_SUCCESS)
        {
            printf("[debug-counter] using ingress port debug counter table: %s\n",
                   ingress_port_debug_name);
        }
        else
        {
            printf("[debug-counter] ingress port debug counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(egress_mirror_seen_names,
                                    ARRLEN(egress_mirror_seen_names),
                                    &egress_mirror_seen_counter,
                                    &egress_mirror_seen_name);
        if (status == BF_SUCCESS) {
            printf("[debug-counter] using egress mirror seen counter table: %s\n",
                   egress_mirror_seen_name);
        } else {
            printf("[debug-counter] egress mirror seen counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(egress_mirror_bth_names,
                                    ARRLEN(egress_mirror_bth_names),
                                    &egress_mirror_bth_counter,
                                    &egress_mirror_bth_name);
        if (status == BF_SUCCESS) {
            printf("[debug-counter] using egress mirror BTH counter table: %s\n",
                   egress_mirror_bth_name);
        } else {
            printf("[debug-counter] egress mirror BTH counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(egress_mirror_cnp_set_names,
                                    ARRLEN(egress_mirror_cnp_set_names),
                                    &egress_mirror_cnp_set_counter,
                                    &egress_mirror_cnp_set_name);
        if (status == BF_SUCCESS) {
            printf("[debug-counter] using egress mirror CNP set counter table: %s\n",
                   egress_mirror_cnp_set_name);
        } else {
            printf("[debug-counter] egress mirror CNP set counter table not found yet, status=%d\n",
                   status);
        }

        status = find_counter_table(egress_roce_data_names,
                                    ARRLEN(egress_roce_data_names),
                                    &egress_roce_data_counter,
                                    &egress_roce_data_name);
        if (status == BF_SUCCESS) {
            printf("[debug-counter] using egress RoCE data counter table: %s\n",
                   egress_roce_data_name);
        } else {
            printf("[debug-counter] egress RoCE data counter table not found yet, status=%d\n",
                   status);
        }
        initialized = 1;
    }

    if (recir_debug_counter)
    {
        status = counter_table_sync(recir_debug_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   recir_debug_name, status);
        }
        status = read_counter_index(recir_debug_counter, 0, &recir_start);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read recir_start failed, status=%d\n", status);
        }
    }

    if (recir_return_counter)
    {
        status = counter_table_sync(recir_return_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   recir_return_name, status);
        }
        status = read_counter_index(recir_return_counter, 0, &recir_return);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read recir_return failed, status=%d\n", status);
        }
    }

    if (recir_return_roce_counter)
    {
        status = counter_table_sync(recir_return_roce_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   recir_return_roce_name, status);
        }
        status = read_counter_index(recir_return_roce_counter, 0, &recir_return_roce);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read recir_return_roce failed, status=%d\n", status);
        }
    }

    if (cnp_debug_counter)
    {
        status = counter_table_sync(cnp_debug_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   cnp_debug_name, status);
        }
        status = read_counter_index(cnp_debug_counter, 0, &cnp_mirror);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read cnp_mirror failed, status=%d\n", status);
        }
    }

    if (egress_cnp_counter)
    {
        status = counter_table_sync(egress_cnp_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   egress_cnp_name, status);
        }
        status = read_counter_index(egress_cnp_counter, 0, &egress_cnp);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read egress_cnp failed, status=%d\n", status);
        }
    }

    if (egress_mirror_seen_counter)
    {
        status = counter_table_sync(egress_mirror_seen_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   egress_mirror_seen_name, status);
        }
        status = read_counter_index(egress_mirror_seen_counter, 0,
                                    &egress_mirror_seen);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read egress_mirror_seen failed, status=%d\n",
                   status);
        }
    }

    if (egress_mirror_bth_counter)
    {
        status = counter_table_sync(egress_mirror_bth_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   egress_mirror_bth_name, status);
        }
        status = read_counter_index(egress_mirror_bth_counter, 0,
                                    &egress_mirror_bth);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read egress_mirror_bth failed, status=%d\n",
                   status);
        }
    }

    if (egress_mirror_cnp_set_counter)
    {
        status = counter_table_sync(egress_mirror_cnp_set_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   egress_mirror_cnp_set_name, status);
        }
        status = read_counter_index(egress_mirror_cnp_set_counter, 0,
                                    &egress_mirror_cnp_set);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read egress_mirror_cnp_set failed, status=%d\n",
                   status);
        }
    }

    if (egress_roce_data_counter)
    {
        status = counter_table_sync(egress_roce_data_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   egress_roce_data_name, status);
        }
        status = read_counter_index(egress_roce_data_counter, 0,
                                    &egress_roce_data);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] read egress_roce_data failed, status=%d\n",
                   status);
        }
    }

    if (ingress_port_debug_counter)
    {
        uint32_t recirc_devports[30] = {0};
        uint32_t recirc_count = 0;

        status = counter_table_sync(ingress_port_debug_counter);
        if (status != BF_SUCCESS)
        {
            printf("[debug-counter] sync failed for %s, status=%d\n",
                   ingress_port_debug_name, status);
        }

        if ((uint32_t)RECIRC_PORT < ARRLEN(last_ingress_port_counts))
        {
            status = read_counter_index(ingress_port_debug_counter,
                                        (uint32_t)RECIRC_PORT,
                                        &configured_recirc_ingress);
            if (status != BF_SUCCESS)
            {
                printf("[debug-counter] read ingress_port[%u] failed, status=%d\n",
                       (uint32_t)RECIRC_PORT, status);
            }
            configured_recirc_ingress_delta =
                configured_recirc_ingress -
                last_ingress_port_counts[(uint32_t)RECIRC_PORT];
        }

        recirc_count = bf_pm_recirc_devports_get(dev_tgt->dev_id, recirc_devports);
        for (uint32_t idx = 0; idx < recirc_count && idx < ARRLEN(recirc_devports); idx++)
        {
            uint32_t port = recirc_devports[idx];
            uint64_t value = 0;
            uint64_t delta = 0;

            if (port >= ARRLEN(last_ingress_port_counts))
            {
                continue;
            }

            status = read_counter_index(ingress_port_debug_counter, port, &value);
            if (status != BF_SUCCESS)
            {
                continue;
            }

            delta = value - last_ingress_port_counts[port];
            last_ingress_port_counts[port] = value;
        }
        {
            uint32_t start_recirc_port = 0;
            uint32_t end_recirc_port = 0;

            status = bf_pm_recirc_port_range_get(dev_tgt->dev_id,
                                                 &start_recirc_port,
                                                 &end_recirc_port);
            if (status == BF_SUCCESS &&
                end_recirc_port >= start_recirc_port &&
                end_recirc_port - start_recirc_port < 64)
            {
                for (uint32_t recirc_port = start_recirc_port;
                     recirc_port <= end_recirc_port;
                     recirc_port++)
                {
                    uint64_t value = 0;
                    uint64_t delta = 0;

                    if (recirc_port >= ARRLEN(last_ingress_port_counts))
                    {
                        continue;
                    }

                    status = read_counter_index(ingress_port_debug_counter,
                                                recirc_port,
                                                &value);
                    if (status != BF_SUCCESS)
                    {
                        continue;
                    }

                    delta = value - last_ingress_port_counts[recirc_port];
                    last_ingress_port_counts[recirc_port] = value;
                }
            }
        }

        for (uint32_t idx = 0; idx < ARRLEN(PORT_LIST); idx++)
        {
            bf_dev_port_t dev_port = fp_port_to_dev_port(dev_tgt->dev_id,
                                                         PORT_LIST[idx].fp_port);
            uint64_t value = 0;
            uint64_t delta = 0;

            if ((uint32_t)dev_port >= ARRLEN(last_ingress_port_counts))
            {
                continue;
            }

            status = read_counter_index(ingress_port_debug_counter,
                                        (uint32_t)dev_port,
                                        &value);
            if (status != BF_SUCCESS)
            {
                printf("[debug-counter] read ingress fp_port=%s dev_port=%u failed, status=%d\n",
                       PORT_LIST[idx].fp_port,
                       (uint32_t)dev_port,
                       status);
                continue;
            }

            delta = value - last_ingress_port_counts[(uint32_t)dev_port];
            last_ingress_port_counts[(uint32_t)dev_port] = value;
            if (delta != 0)
            {
                printf("ingress_fp=%s dev_port=%u pkts=%" PRIu64 " (+%" PRIu64 ")\n",
                       PORT_LIST[idx].fp_port,
                       (uint32_t)dev_port,
                       value,
                       delta);
            }
        }

        for (uint32_t idx = 0; idx < ARRLEN(PORT_LIST); idx++)
        {
            bf_pal_front_port_handle_t port_hdl;
            bf_rmon_counter_t ctr = bf_mac_stat_FramesReceivedAll;
            uint64_t rx_all = 0;
            uint64_t delta = 0;

            status = bf_pm_port_str_to_hdl_get(dev_tgt->dev_id,
                                               PORT_LIST[idx].fp_port,
                                               &port_hdl);
            if (status != BF_SUCCESS)
            {
                printf("[debug-counter] map fp_port=%s to handle failed, status=%d\n",
                       PORT_LIST[idx].fp_port,
                       status);
                continue;
            }

            status = bf_pm_port_stat_direct_get(dev_tgt->dev_id,
                                                &port_hdl,
                                                &ctr,
                                                &rx_all,
                                                1);
            if (status != BF_SUCCESS)
            {
                status = bf_pm_port_all_stats_update_sync(dev_tgt->dev_id,
                                                          &port_hdl);
                if (status == BF_SUCCESS)
                {
                    status = bf_pm_port_this_stat_get(dev_tgt->dev_id,
                                                      &port_hdl,
                                                      bf_mac_stat_FramesReceivedAll,
                                                      &rx_all);
                }
            }
            if (status != BF_SUCCESS)
            {
                printf("[debug-counter] read MAC RX fp_port=%s failed, status=%d\n",
                       PORT_LIST[idx].fp_port,
                       status);
                continue;
            }

            delta = rx_all - last_mac_rx_counts[idx];
            last_mac_rx_counts[idx] = rx_all;
            if (delta != 0)
            {
                printf("mac_rx_fp=%s rx_all=%" PRIu64 " (+%" PRIu64 ")\n",
                       PORT_LIST[idx].fp_port,
                       rx_all,
                       delta);
            }
        }
    }

    printf("recir_start=%" PRIu64 " (+%" PRIu64 ") recir_return=%" PRIu64 " (+%" PRIu64 ") recir_return_roce=%" PRIu64 " (+%" PRIu64 ") cnp_mirror=%" PRIu64 " (+%" PRIu64 ")\n",
           recir_start,
           recir_start - last_recir_start,
           recir_return,
           recir_return - last_recir_return,
           recir_return_roce,
           recir_return_roce - last_recir_return_roce,
           cnp_mirror,
           cnp_mirror - last_cnp_mirror);
    fflush(stdout);

    last_recir_start = recir_start;
    last_recir_return = recir_return;
    last_recir_return_roce = recir_return_roce;
    last_cnp_mirror = cnp_mirror;
    last_egress_cnp = egress_cnp;
    last_egress_mirror_seen = egress_mirror_seen;
    last_egress_mirror_bth = egress_mirror_bth;
    last_egress_mirror_cnp_set = egress_mirror_cnp_set;
    last_egress_roce_data = egress_roce_data;
}

void *monitor_thread_func(void *arg)
{
    while (1)
    {
        monitor_debug_counters();
        sleep(1);
    }
    return NULL;
}

// C-style using bf_pm
static void port_setup(const bf_rt_target_t *dev_tgt,
                       const switch_port_t *port_list,
                       const uint8_t port_count)
{
    bf_status_t bf_status;

    for (unsigned int idx = 0; idx < port_count; idx++)
    {
        bf_pal_front_port_handle_t port_hdl;
        bool is_added = false;
        bf_status = bf_pm_port_str_to_hdl_get(dev_tgt->dev_id,
                                              port_list[idx].fp_port,
                                              &port_hdl);
        check_bf_status(bf_status, "bf_pm_port_str_to_hdl_get");

        bf_status = bf_pm_is_port_added(dev_tgt->dev_id, &port_hdl,
                                        &is_added);
        check_bf_status(bf_status, "bf_pm_is_port_added");

        if (!is_added)
        {
            bf_status = bf_pm_port_add(dev_tgt->dev_id, &port_hdl,
                                       BF_SPEED_100G, BF_FEC_TYP_RS);
            check_bf_status(bf_status, "bf_pm_port_add");
        }
        else
        {
            printf("Port %s is already added; skip bf_pm_port_add.\n",
                   port_list[idx].fp_port);
        }

        bf_status = bf_pm_port_enable(dev_tgt->dev_id, &port_hdl);
        check_bf_status(bf_status, "bf_pm_port_enable");
        printf("Port %s is enabled successfully!\n", port_list[idx].fp_port);
    }
}

static void switchd_setup(bf_switchd_context_t *switchd_ctx, const char *prog)
{
    char conf_file[256];
    char bf_sysfs_fname[128] = "/sys/class/bf/bf0/device";
    const char *sde_install = getenv("SDE_INSTALL");
    FILE *fd;
    int written;

    assert(sde_install != NULL);
    switchd_ctx->install_dir = strdup(sde_install);
    assert(switchd_ctx->install_dir != NULL);
    written = snprintf(conf_file, sizeof(conf_file), "%s%s%s%s",
                       sde_install, "/share/p4/targets/tofino2/", prog, ".conf");
    assert(written > 0 && (size_t)written < sizeof(conf_file));
    switchd_ctx->conf_file = strdup(conf_file);
    assert(switchd_ctx->conf_file != NULL);
    printf("Using SDE_INSTALL: %s\n", switchd_ctx->install_dir);
    printf("Using switchd conf: %s\n", switchd_ctx->conf_file);
    if (access(switchd_ctx->conf_file, R_OK) != 0)
    {
        fprintf(stderr,
                "Cannot read switchd conf: %s\n"
                "Install the P4 artifacts under $SDE_INSTALL/share/p4/targets/tofino2 "
                "and $SDE_INSTALL/share/tofino2pd/themis before starting control.\n",
                switchd_ctx->conf_file);
        exit(EXIT_FAILURE);
    }
    switchd_ctx->running_in_background = true;
    switchd_ctx->skip_port_add = true;
    switchd_ctx->dev_sts_thread = true;
    switchd_ctx->dev_sts_port = 7777;

    strncat(bf_sysfs_fname, "/dev_add",
            sizeof(bf_sysfs_fname) - 1 - strlen(bf_sysfs_fname));
    printf("bf_sysfs_fname %s\n", bf_sysfs_fname);
    fd = fopen(bf_sysfs_fname, "r");
    if (fd != NULL)
    {
        // override previous parsing if bf_kpkt KLM was loaded
        printf("kernel mode packet driver present, forcing kpkt option!\n");
        switchd_ctx->kernel_pkt = true;
        fclose(fd);
    }
    //bf_sys_trace_level_set(BF_MOD_SWITCHD, BF_LOG_DBG);
    bf_status_t st = bf_switchd_lib_init(switchd_ctx);
    fprintf(stderr, "bf_switchd_lib_init=%d (%s)\n", st, bf_err_str(st));
    fflush(stderr);

    printf("bf_switchd_lib_init=%d\n",st);

    assert(st == BF_SUCCESS);
    const char *msg = bf_err_str(st);
    printf("err=%s\n", msg);
    printf(
        "conf=%s\n",
        switchd_ctx->conf_file);
       
    printf(
        "exist=%d\n",
        access(
          switchd_ctx->conf_file,
          F_OK));
    printf("\nbf_switchd is initialized correctly!\n");
}

static void bfrt_dump_registered_programs(bf_dev_id_t requested_dev_id)
{
    bf_status_t bf_status;
    uint32_t num_devs = 0;

    bf_status = bf_rt_num_device_id_list_get(&num_devs);
    printf("BFRT device inventory: status=%d, num_devs=%u\n",
           bf_status, num_devs);
    if (bf_status != BF_SUCCESS || num_devs == 0)
    {
        return;
    }

    bf_dev_id_t *dev_ids = (bf_dev_id_t *)calloc(num_devs, sizeof(*dev_ids));
    assert(dev_ids != NULL);

    bf_status = bf_rt_device_id_list_get(dev_ids);
    printf("bf_rt_device_id_list_get status=%d\n", bf_status);
    if (bf_status != BF_SUCCESS)
    {
        free(dev_ids);
        return;
    }

    for (uint32_t idx = 0; idx < num_devs; idx++)
    {
        int num_names = 0;
        printf("BFRT dev_id[%u]=%d%s\n",
               idx, dev_ids[idx],
               dev_ids[idx] == requested_dev_id ? " (requested)" : "");

        bf_status = bf_rt_num_p4_names_get(dev_ids[idx], &num_names);
        printf("bf_rt_num_p4_names_get(dev_id=%d) status=%d, num_names=%d\n",
               dev_ids[idx], bf_status, num_names);
        if (bf_status != BF_SUCCESS || num_names <= 0)
        {
            continue;
        }

        const char **p4_names = (const char **)calloc(num_names, sizeof(*p4_names));
        assert(p4_names != NULL);

        bf_status = bf_rt_p4_names_get(dev_ids[idx], p4_names);
        printf("bf_rt_p4_names_get(dev_id=%d) status=%d\n",
               dev_ids[idx], bf_status);
        if (bf_status == BF_SUCCESS)
        {
            for (int name_idx = 0; name_idx < num_names; name_idx++)
            {
                printf("BFRT registered P4 name[%d]=%s\n",
                       name_idx, p4_names[name_idx] ? p4_names[name_idx] : "(null)");
            }
        }
        free(p4_names);
    }

    free(dev_ids);
}

static bf_status_t bfrt_try_registered_programs(const bf_rt_target_t *dev_tgt,
                                                const bf_rt_info_hdl **bfrt_info)
{
    bf_status_t bf_status;
    int num_names = 0;

    bf_status = bf_rt_num_p4_names_get(dev_tgt->dev_id, &num_names);
    if (bf_status != BF_SUCCESS || num_names <= 0)
    {
        return bf_status;
    }

    const char **p4_names = (const char **)calloc(num_names, sizeof(*p4_names));
    assert(p4_names != NULL);

    bf_status = bf_rt_p4_names_get(dev_tgt->dev_id, p4_names);
    if (bf_status != BF_SUCCESS)
    {
        free(p4_names);
        return bf_status;
    }

    for (int name_idx = 0; name_idx < num_names; name_idx++)
    {
        if (p4_names[name_idx] == NULL)
        {
            continue;
        }

        printf("Trying BFRT registered P4 name[%d]=%s\n",
               name_idx, p4_names[name_idx]);
        bf_status = bf_rt_info_get(dev_tgt->dev_id,
                                   p4_names[name_idx],
                                   bfrt_info);
        printf("bf_rt_info_get(dev_id=%d, prog=%s) status=%d, bfrt_info=%p\n",
               dev_tgt->dev_id, p4_names[name_idx],
               bf_status, (void *)*bfrt_info);
        if (bf_status == BF_SUCCESS)
        {
            printf("Using BFRT registered P4 program name: %s\n",
                   p4_names[name_idx]);
            free(p4_names);
            return BF_SUCCESS;
        }
    }

    free(p4_names);
    return BF_OBJECT_NOT_FOUND;
}

static bf_status_t bfrt_info_get_logged(const bf_rt_target_t *dev_tgt,
                                        const char *prog,
                                        const bf_rt_info_hdl **bfrt_info,
                                        int retry)
{
    bf_status_t bf_status;

    printf("Getting BFRT info: dev_id=%d, prog=%s, retry=%d\n",
           dev_tgt->dev_id, prog, retry);
    bf_status = bf_rt_info_get(dev_tgt->dev_id, prog, bfrt_info);
    printf("bf_rt_info_get(dev_id=%d, prog=%s) status=%d, bfrt_info=%p\n",
           dev_tgt->dev_id, prog, bf_status, (void *)*bfrt_info);

    return bf_status;
}

static void bfrt_setup(const bf_rt_target_t *dev_tgt,
                       const bf_rt_info_hdl **bfrt_info,
                       const char *prog,
                       bf_rt_session_hdl **session)
{
    bf_status_t bf_status = BF_OBJECT_NOT_FOUND;
    bool dumped_inventory = false;
    char p4_suffix_prog[128];
    bool try_p4_suffix_prog = false;

    if (strstr(prog, ".p4") == NULL)
    {
        int written = snprintf(p4_suffix_prog, sizeof(p4_suffix_prog),
                               "%s.p4", prog);
        try_p4_suffix_prog = written > 0 &&
                             (size_t)written < sizeof(p4_suffix_prog);
    }

    // Get bfrtInfo object from dev_id and p4 program name
    for (int retry = 0; retry < 20; retry++)
    {
        bf_status = bfrt_info_get_logged(dev_tgt, prog, bfrt_info, retry);
        if (bf_status == BF_SUCCESS)
        {
            break;
        }

        if (try_p4_suffix_prog)
        {
            bf_status = bfrt_info_get_logged(dev_tgt, p4_suffix_prog,
                                             bfrt_info, retry);
            if (bf_status == BF_SUCCESS)
            {
                printf("Using BFRT P4 program name with .p4 suffix: %s\n",
                       p4_suffix_prog);
                break;
            }
        }

        if (!dumped_inventory)
        {
            bfrt_dump_registered_programs(dev_tgt->dev_id);
            dumped_inventory = true;
        }

        bf_status = bfrt_try_registered_programs(dev_tgt, bfrt_info);
        if (bf_status == BF_SUCCESS)
        {
            break;
        }

        sleep(1);
    }
    if (bf_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "Failed to get BFRT info for dev_id=%d, requested prog=%s, status=%d\n",
                dev_tgt->dev_id, prog, bf_status);
        bfrt_dump_registered_programs(dev_tgt->dev_id);
    }
    assert(bf_status == BF_SUCCESS);
    // Create a session object
    bf_status = bf_rt_session_create(session);
    assert(bf_status == BF_SUCCESS);
    printf("bfrt_info is got and session is created correctly!\n");
}

static void mirrorSetup(const bf_rt_target_t *dev_tgt)
{
    p4_pd_status_t pd_status;
    p4_pd_sess_hdl_t mirror_session;
    p4_pd_dev_target_t pd_dev_tgt = {dev_tgt->dev_id, dev_tgt->pipe_id};
    bf_dev_port_t mirror_dev_port =
        fp_port_to_dev_port(dev_tgt->dev_id, DEFAULT_MIRROR_FP_PORT);

    pd_status = p4_pd_client_init(&mirror_session);
    assert(pd_status == BF_SUCCESS);

    p4_pd_mirror_session_info_t mirror_session_info = {
        .type = PD_MIRROR_TYPE_NORM,
        .dir = PD_DIR_INGRESS,
        .id = CNP_SES_ID,
        .egr_port = mirror_dev_port,
        .egr_port_v = true,
        .max_pkt_len = CNP_MIRROR_MAX_PKT_LEN
    };

    pd_status = p4_pd_mirror_session_create(mirror_session, pd_dev_tgt,
                                            &mirror_session_info);
    if (pd_status != BF_SUCCESS)
    {
        printf("Config fixed I2E mirror session create failed for fp_port=%s "
               "dev_port=%u session_id=%u status=%d; trying update\n",
               DEFAULT_MIRROR_FP_PORT,
               (uint32_t)mirror_dev_port,
               (uint32_t)CNP_SES_ID,
               pd_status);
        pd_status = p4_pd_mirror_session_update(mirror_session,
                                                pd_dev_tgt,
                                                &mirror_session_info,
                                                true);
    }
    if (pd_status != BF_SUCCESS)
    {
        fprintf(stderr,
                "Config fixed I2E mirror session failed for fp_port=%s "
                "dev_port=%u session_id=%u status=%d\n",
                DEFAULT_MIRROR_FP_PORT,
                (uint32_t)mirror_dev_port,
                (uint32_t)CNP_SES_ID,
                pd_status);
        exit(EXIT_FAILURE);
    }
    printf("Config fixed I2E mirror to fp_port=%s dev_port=%u session_id=%u\n",
           DEFAULT_MIRROR_FP_PORT,
           (uint32_t)mirror_dev_port,
           (uint32_t)CNP_SES_ID);

    // OLD LOGIC DISABLED:
    // This temporarily created one mirror session per front-panel port and used
    // original ingress dev_port low 8 bits as the MirrorId_t. It is disabled
    // because the user wants fixed-port CNP again.

    pd_status = p4_pd_complete_operations(mirror_session);
    assert(pd_status == BF_SUCCESS);
    pd_status = p4_pd_client_cleanup(mirror_session);
    assert(pd_status == BF_SUCCESS);
}

int main(void)
{
    signal(SIGSEGV, segv_handler);
    print_build_mode();

    dev_tgt->dev_id = 0;
    dev_tgt->pipe_id = BF_DEV_PIPE_ALL;

    // Initialize and set the bf_switchd
    switchd_ctx = (bf_switchd_context_t *)
        calloc(1, sizeof(bf_switchd_context_t));
    if (switchd_ctx == NULL)
    {
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
    print_recirc_port_inventory(dev_tgt->dev_id, PORT_LIST, ARRLEN(PORT_LIST));
    recirculation_setup(dev_tgt);

    // Setup and install entries for act_l3_table (C-style)
    act_l3_table_deploy(dev_tgt, bfrt_info, *session,
                        act_l3_table_list, ARRLEN(act_l3_table_list));

    // Setup and install entries for act_l3_table (C-style)
    act_arp_table_deploy(dev_tgt, bfrt_info, *session,
                         act_arp_table_list, ARRLEN(act_arp_table_list));

    // PNP reconstructs sQPN in the data plane from UDP source port and dIP hash.
    // Host QP Pool encoding replaces the previous control-plane QPN mapping path.

    // CNP generation now happens for every RoCE data packet in the data plane,
    // after one recirculation pass. The old ECN-triggered and TRP-triggered
    // tables are no longer deployed.
    //
    // OLD LOGIC DISABLED:
    // This used to install ECN match entries so only ECN-marked RoCE packets
    // generated CNP. It is commented out because the active P4 path now calls
    // set_mirror_md() directly for every RoCE data packet after one recir pass.
    //
    // detect_ecn_deploy(dev_tgt, bfrt_info, *session,
    //                   detect_ecn_list, ARRLEN(detect_ecn_list));
    //
    // OLD LOGIC DISABLED:
    // This used to prepare the TRP recirculation table; a daemon/control-plane
    // path could then insert selected (dst_ip, dst_qpn) flows with custom loop
    // counts. It is commented out because recirculation is now unconditional
    // for RoCE data packets and always exactly one pass.
    //
    // trp_cnp_store_setup(dev_tgt, bfrt_info, &trp_cnp_store,
    //                     &trp_cnp_store_info);
    // printf("trp_cnp_store is set up correctly!\n");

    // Set up count_cnp table in Egress
    count_cnp_deploy(dev_tgt, bfrt_info, *session,
                     count_cnp_list, ARRLEN(count_cnp_list));

    // Set up I2E Mirror
    mirrorSetup(dev_tgt);
    printf("Mirror Setup finished\n");

    printf("PNP QPN daemon polling is disabled; host QP Pool must encode sQPN into UDP source port.\n");

    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, monitor_thread_func, NULL) != 0) {
        perror("Failed to create debug counter monitor thread");
        return EXIT_FAILURE;
    }
    printf("Main: Debug counter monitor thread started\n");

    // pthread_join(monitor_thread, NULL);

    while (1)
    {
        sleep(1);
    }

    return 0;
}
