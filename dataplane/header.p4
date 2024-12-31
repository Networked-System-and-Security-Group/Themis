#ifndef _HEADERS_
#define _HEADERS_

/*************************************************************************
 ************* C O N S T A N T S    A N D   T Y P E S  *******************
 *************************************************************************/

typedef bit<48> mac_addr_t;
typedef bit<32> ipv4_addr_t;
typedef bit<16> l4_port_t;

typedef bit<16> ether_type_t;
const ether_type_t ETHERTYPE_IPV4 = 16w0x0800;
const bit<16> ETHERTYPE_ARP = 16w0x0806;

typedef bit<8> ip_protocol_t;
const ip_protocol_t IP_PROTOCOLS_TCP = 6;
const ip_protocol_t IP_PROTOCOLS_UDP = 17;

const l4_port_t UDP_ROCE_DST_PORT = 4791;

// rixin: 由于使用了mirror，所以需要区分
typedef bit<8>  pkt_type_t;
const pkt_type_t PKT_TYPE_NORMAL = 6;
const pkt_type_t PKT_TYPE_MIRROR = 9;

#if __TARGET_TOFINO__ == 1
typedef bit<3> mirror_type_t;
#else
typedef bit<4> mirror_type_t;
#endif
const mirror_type_t MIRROR_TYPE_I2E = 1;
const mirror_type_t MIRROR_TYPE_E2E = 2;

// rixin: the num of host interfaces, real num at ACT is 12.
const int HOST_IF_NUM = 32;
// rixin: session id 0被保留了，使用1
const int CNP_SES_ID = 1;
// rixin: 造成拥塞的跨DC流的最大个数(concurrent)
const int MAX_NUM_CONGESTION_FLOW = 10000;

/*************************************************************************
 ***********************  H E A D E R S  *********************************
 *************************************************************************/

header ethernet_h {
    mac_addr_t dst_addr;
    mac_addr_t src_addr;
    bit<16> ether_type;
}

header ipv4_h {
    bit<4> version;
    bit<4> ihl;
    bit<6> diffserv;
    bit<2> ecn;
    bit<16> total_len;
    bit<16> identification;
    bit<3> flags;
    bit<13> frag_offset;
    bit<8> ttl;
    bit<8> protocol;
    bit<16> hdr_checksum;
    ipv4_addr_t src_addr;
    ipv4_addr_t dst_addr;
}

header tcp_h {
    l4_port_t src_port;
    l4_port_t dst_port;
    bit<32> seq_no;
    bit<32> ack_no;
    bit<4> data_offset;
    bit<4> res;
    bit<8> flags;
    bit<16> window;
    bit<16> checksum;
    bit<16> urgent_ptr;
}

header udp_h {
    l4_port_t src_port;
    l4_port_t dst_port;
    bit<16> hdr_length;
    bit<16> checksum;
}

header bth_h {
    bit<8> opcode;
    bit<1> solicited_event;
    bit<1> mig_req;
    bit<2> pad_count;
    bit<4> header_v;
    bit<16> parti_key;
    bit<8> reserved1;
    bit<24> dst_qpn;
    bit<1> ack_req;
    bit<7> reserved2;
    bit<24> psn;
}

header arp_h {
    bit<16> hw_type;
    bit<16> proto_type;
    bit<8> hw_sz;
    bit<8> proto_sz;
    bit<16> opcode;
    mac_addr_t srcMacAddr;
    bit<32> srcIpv4Addr;
    mac_addr_t dstMacAddr;
    bit<32> dstIpv4Addr;
}

header mirror_bridged_metadata_h {
    pkt_type_t pkt_type;
}

header mirror_h {
    pkt_type_t  pkt_type;
}

struct my_ingress_headers_t {
    mirror_bridged_metadata_h bridged_md;
    ethernet_h ethernet;
    ipv4_h ipv4;
    arp_h arp;
    tcp_h tcp;
    udp_h udp;
}

struct my_egress_headers_t {
    mirror_bridged_metadata_h bridged_md;
    mirror_h mirror_md;
    ethernet_h ethernet;
    ipv4_h ipv4;
    tcp_h tcp;
    udp_h udp;
    bth_h bth;
}

struct empty_header_t {}

struct empty_metadata_t {}

#endif /* _HEADERS_ */
