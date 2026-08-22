#ifndef _SWITCH_CONFIG_H
#define _SWITCH_CONFIG_H

#if __TOFINO_MODE__ == 0

const switch_port_t PORT_LIST[] = {
    {"1/0"}, {"2/0"}, {"3/0"}, {"4/0"}, {"5/0"},
    {"6/0"}, {"7/0"}, {"8/0"}, {"9/0"}
};

const act_l3_table_list_tuple_t act_l3_table_list[] = {
    /* Verified 2026-08-21 RoCE links between 111/112 and Tofino2 220. */
    {"10.99.1.1", "1/0"}, /* 111 mlx5_0 / ens7f0np0 */
    {"10.99.2.1", "2/0"}, /* 111 mlx5_2 / ens10f0np0 */
    {"10.99.1.2", "3/0"}, /* 112 mlx5_0 / ens7f0np0 */
    {"10.99.2.2", "4/0"}, /* 112 mlx5_2 / ens10f0np0 */

    /* Additional lab aliases retained for compatibility. */
    {"10.99.1.3", "5/0"},
    {"10.99.2.3", "6/0"},
    {"10.99.1.4", "7/0"},
    {"10.99.2.4", "8/0"},
    {"10.99.5.1", "9/0"}
};

const act_arp_table_list_tuple_t act_arp_table_list[] = {
    /* Verified 2026-08-21 RoCE links between 111/112 and Tofino2 220. */
    {"10.99.1.1", "1/0"}, /* 111 mlx5_0 / ens7f0np0 */
    {"10.99.2.1", "2/0"}, /* 111 mlx5_2 / ens10f0np0 */
    {"10.99.1.2", "3/0"}, /* 112 mlx5_0 / ens7f0np0 */
    {"10.99.2.2", "4/0"}, /* 112 mlx5_2 / ens10f0np0 */

    /* Additional lab aliases retained for compatibility. */
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

/* Fixed mirrored CNP return port for the primary 111 mlx5_0/ens7f0np0 -> 112 mlx5_0/ens7f0np0 test. */
static const char *DEFAULT_MIRROR_FP_PORT = "1/0";

#else
#endif

#endif
