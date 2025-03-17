#ifndef _SWITCH_CONFIG_H
#define _SWITCH_CONFIG_H

#if __TOFINO_MODE__ == 0
#include <tofino/pdfixed/pd_mirror.h>

const switch_port_t PORT_LIST[] = {
	{"1/0"}, {"1/1"}, {"1/2"}, {"1/3"},
    {"2/0"}, {"2/1"}, {"2/2"}, {"2/3"}
};

const act_l3_table_list_tuple_t act_l3_table_list[] = {
    {"192.168.201.1", 141}, {"192.168.202.1", 133},
    {"192.168.201.2", 135}, {"192.168.202.2", 140},
    {"192.168.201.3", 142}, {"192.168.202.3", 134},
    {"192.168.201.4", 132}, {"192.168.202.4", 143},
    {"192.168.201.5", 148}, 
    {"192.168.201.6", 156}
};

const act_arp_table_list_tuple_t act_arp_table_list[] = {
    {"192.168.201.1", 141}, {"192.168.202.1", 133},
    {"192.168.201.2", 135}, {"192.168.202.2", 140},
    {"192.168.201.3", 142}, {"192.168.202.3", 134},
    {"192.168.201.4", 132}, {"192.168.202.4", 143},
    {"192.168.201.5", 148}, 
    {"192.168.201.6", 156}
};

const detect_ecn_list_tuple_t detect_ecn_list[] = {
    {0},
    {1},
    {2},
    {3}
};

const count_cnp_list_tuple_t count_cnp_list[] = {
    {129}
};

p4_pd_mirror_session_info_t mirror_session_list[] = {
    {
    .type        = PD_MIRROR_TYPE_NORM, // Not sure
    .dir         = PD_DIR_INGRESS,
    .id          = ACT_196_PORT,
    .egr_port    = ACT_196_PORT,
    .egr_port_v  = true,
    .max_pkt_len = 16384 // Refer to example in Barefoot Academy	
    },

    {
    .type        = PD_MIRROR_TYPE_NORM, // Not sure
    .dir         = PD_DIR_INGRESS,
    .id          = ACT_197_PORT,
    .egr_port    = ACT_197_PORT,
    .egr_port_v  = true,
    .max_pkt_len = 16384 // Refer to example in Barefoot Academy	
    },

    {
    .type        = PD_MIRROR_TYPE_NORM, // Not sure
    .dir         = PD_DIR_INGRESS,
    .id          = ACT_198_PORT,
    .egr_port    = ACT_198_PORT,
    .egr_port_v  = true,
    .max_pkt_len = 16384 // Refer to example in Barefoot Academy	
    },

    {
    .type        = PD_MIRROR_TYPE_NORM, // Not sure
    .dir         = PD_DIR_INGRESS,
    .id          = ACT_199_PORT,
    .egr_port    = ACT_199_PORT,
    .egr_port_v  = true,
    .max_pkt_len = 16384 // Refer to example in Barefoot Academy	
    }
};
#else
#endif
#endif