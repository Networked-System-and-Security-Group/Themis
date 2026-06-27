#ifndef _SWITCH_CONFIG_H
#define _SWITCH_CONFIG_H

#if __TOFINO_MODE__ == 0

const switch_port_t PORT_LIST[] = {
    {"1/0"}, {"2/0"}, {"3/0"}, {"4/0"}, {"5/0"},
    {"6/0"}, {"7/0"}, {"8/0"}, {"9/0"}
};

const act_l3_table_list_tuple_t act_l3_table_list[] = {
    {"10.99.1.1", "1/0"},
    {"10.99.2.1", "2/0"},
    {"10.99.1.2", "3/0"},
    {"10.99.2.2", "4/0"},
    {"10.99.1.3", "5/0"},
    {"10.99.2.3", "6/0"},
    {"10.99.1.4", "7/0"},
    {"10.99.2.4", "8/0"},
    {"10.99.5.1", "9/0"}
};

const act_arp_table_list_tuple_t act_arp_table_list[] = {
    {"10.99.1.1", "1/0"},
    {"10.99.2.1", "2/0"},
    {"10.99.1.2", "3/0"},
    {"10.99.2.2", "4/0"},
    {"10.99.1.3", "5/0"},
    {"10.99.2.3", "6/0"},
    {"10.99.1.4", "7/0"},
    {"10.99.2.4", "8/0"},
    {"10.99.5.1", "9/0"}
};

const detect_ecn_list_tuple_t detect_ecn_list[] = {
    {3}
};

const count_cnp_list_tuple_t count_cnp_list[] = {
    {129}
};

static const char *DEFAULT_MIRROR_FP_PORT = "1/0";

#else
#endif

#endif
