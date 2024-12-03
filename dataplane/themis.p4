/* P4_16 */

#include <core.p4>
#include <tna.p4>

#include "header.p4"
#include "util.p4"

/*************************************************************************
 **************  I N G R E S S   P R O C E S S I N G   *******************
 *************************************************************************/

    /******  G L O B A L   M E T A D A T A  *********/
struct my_ingress_metadata_t {
    MirrorId_t ing_mir_ses; // rixin: mirror session id
    pkt_type_t pkt_type; // rixin: 用于让ingress pipeline告诉ingress deparser是什么类型
}
    /***********************  P A R S E R  **************************/
parser IngressParser(packet_in        pkt,
    /* User */    
    out my_ingress_headers_t          hdr,
    out my_ingress_metadata_t         meta,
    /* Intrinsic */
    out ingress_intrinsic_metadata_t  ig_intr_md)
{
    TofinoIngressParser() tofino_parser;

    state start {
        tofino_parser.apply(pkt, ig_intr_md);
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition parse_ipv4;
    }

    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition accept;
    }
}

    /***********************  M A U  **************************/
control Ingress(
    /* User */
    inout my_ingress_headers_t                       hdr,
    inout my_ingress_metadata_t                      meta,
    /* Intrinsic */
    in    ingress_intrinsic_metadata_t               ig_intr_md,
    in    ingress_intrinsic_metadata_from_parser_t   ig_prsr_md,
    inout ingress_intrinsic_metadata_for_deparser_t  ig_dprsr_md,
    inout ingress_intrinsic_metadata_for_tm_t        ig_tm_md) {

    // rixin: 转发ipv4包的动作
	action normal_forward(PortId_t egress_port) {
	    ig_tm_md.ucast_egress_port = egress_port;
	}

	table ipv4_table {
	    key = {
	    	hdr.ipv4.dst_addr : exact;
	    }
	    actions = {
            normal_forward;
            @defaultonly NoAction;
	    }
	    size = HOST_IF_NUM;
	}

    // rixin: 采用ing-to-eg mirror机制，mirrored pkt作cnp
    // 下面是对origin pkt的处理
    action set_mirror_md() {
        // rixin: 设置一些信息，用于deparser进行mirror操作
        meta.ing_mir_ses = CNP_SES_ID; // rixin: 这个session id应该按照ingress_port去选择的，但是目前我先不管
        meta.pkt_type = PKT_TYPE_MIRROR; // rixin: 这个包需要被mirror
        ig_dprsr_md.mirror_type = MIRROR_TYPE_I2E; // rixin: mirror类型告知deparser
    }

    // rixin: 检测ecn
    table detect_ecn {
        key = {
            hdr.ipv4.ecn :exact;
        }
        actions = {
            set_mirror_md;
            @defaultonly NoAction;
        }
	    size = HOST_IF_NUM;
    }

	apply {
        // rixin: 下面的table只会设定出端口
	    if (hdr.ethernet.ether_type == ETHERTYPE_IPV4) {
		    ipv4_table.apply();
	    }
        // rixin: 使用resubmit_flag（自带的），从而第二个模块不会对第一个模块产生影响
        if (ig_intr_md.resubmit_flag == 0) {
            // rixin: 如果检测到ecn
            detect_ecn.apply();
        }
        // rixin: 设置mirrored pkt的bridge header (相关信息见Figure3 from "P4_16 Tofino Native Architecture")
        // rixin: setValid + hdr.emit <==> 让bridged_md加入头部
        hdr.bridged_md.setValid();
        hdr.bridged_md.pkt_type = PKT_TYPE_NORMAL;
	}
}

    /*********************  D E P A R S E R  ************************/
control IngressDeparser(packet_out pkt,
    /* User */
    inout my_ingress_headers_t                       hdr,
    in    my_ingress_metadata_t                      meta,
    /* Intrinsic */
    in    ingress_intrinsic_metadata_for_deparser_t  ig_dprsr_md)
{
    Mirror() mirror;

    apply {
        if (ig_dprsr_md.mirror_type == MIRROR_TYPE_I2E) {
            // rixin: 此处，是真正的创建mirror包的地方，把meta.pkt_type作为头部加进去
            // NOTE: 我只用了一个字节，还有31个字节可以使用
            mirror.emit<mirror_h>(meta.ing_mir_ses, {meta.pkt_type});
        }
        // rixin: 此处是对original pkt的deparse
        pkt.emit(hdr);
    }
}

/*************************************************************************
 ****************  E G R E S S   P R O C E S S I N G   *******************
 *************************************************************************/
    
    /********  G L O B A L   E G R E S S   M E T A D A T A  *********/
struct my_egress_metadata_t {
    bit<1> is_mirrored; // rixin: parser告知pipeline处理的是一个cnp
}

    /***********************  P A R S E R  **************************/

parser EgressParser(packet_in        pkt,
    /* User */
    out my_egress_headers_t          hdr,
    out my_egress_metadata_t         meta,
    /* Intrinsic */
    out egress_intrinsic_metadata_t  eg_intr_md)
{
    TofinoEgressParser() tofino_parser;

    state start {
        tofino_parser.apply(pkt, eg_intr_md);
        transition parse_metadata;
    }

    state parse_metadata {
        // rixin(tips): 读这块代码的reader可以自行查一查lookahead和extract的区别
        mirror_h mirror_md = pkt.lookahead<mirror_h>();
        transition select(mirror_md.pkt_type) {
            PKT_TYPE_MIRROR : parse_mirror_md;
            PKT_TYPE_NORMAL : parse_bridged_md;
            default : accept;
        }
    }

    state parse_bridged_md {
        pkt.extract(hdr.bridged_md);
        transition parse_ethernet;
    }

    state parse_mirror_md {
        mirror_h mirror_md;
        pkt.extract(mirror_md);
        meta.is_mirrored = 1;
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition accept;
    }

    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            IP_PROTOCOLS_TCP:   parse_tcp;
	        IP_PROTOCOLS_UDP:   parse_udp;
            default: accept;
        }
    }

    state parse_tcp {
        pkt.extract(hdr.tcp);
        transition parse_bth;
    }

    state parse_udp {
        pkt.extract(hdr.udp);
        transition parse_bth;
    }

    state parse_bth {
        pkt.extract(hdr.bth);
        transition accept;
    }
}

    /***************** M A T C H - A C T I O N  *********************/

control Egress(
    /* User */
    inout my_egress_headers_t                          hdr,
    inout my_egress_metadata_t                         meta,
    /* Intrinsic */    
    in    egress_intrinsic_metadata_t                  eg_intr_md,
    in    egress_intrinsic_metadata_from_parser_t      eg_prsr_md,
    inout egress_intrinsic_metadata_for_deparser_t     eg_dprsr_md,
    inout egress_intrinsic_metadata_for_output_port_t  eg_oport_md)
{
    action set_dstQPN (bit<24> clientQPN) {
        hdr.bth.dest_qp = clientQPN;
    }

    table set_cnp_dstQPN {
        key = {
            // rixin: 下面两个字段用来match需要修改dstQPN字段的CNP
            hdr.ipv4.dst_addr: exact;
            hdr.udp.src_port: exact;
        }
        actions = {
            set_dstQPN;
            @defaultonly NoAction;
        }
        size = MAX_NUM_CONGESTION_FLOW;
    }

    apply {
        // rixin: 判断出当前是个mirrored pkt即cnp
        if (meta.is_mirrored == 1)
        {
            // rixin: 设置好cnp的mac地址
            mac_addr_t tmp1 = hdr.ethernet.dst_addr;
            hdr.ethernet.dst_addr = hdr.ethernet.src_addr;
            hdr.ethernet.src_addr = tmp1;
            // rixin: 设置好cnp的ip地址
            ipv4_addr_t tmp2 = hdr.ipv4.dst_addr;
            hdr.ipv4.dst_addr = hdr.ipv4.src_addr;
            hdr.ipv4.src_addr = tmp2;
            // rixin: udp port不需要改变
            // rixin: BTH中的dstQPN字段应该等到后续的MAT修改
            set_cnp_dstQPN.apply();
        }

    }
}

    /*********************  D E P A R S E R  ************************/

control EgressDeparser(packet_out pkt,
    /* User */
    inout my_egress_headers_t                       hdr,
    in    my_egress_metadata_t                      meta,
    /* Intrinsic */
    in    egress_intrinsic_metadata_for_deparser_t  eg_dprsr_md)
{
    apply {
        pkt.emit(hdr);
    }
}


/************ F I N A L   P A C K A G E ******************************/
Pipeline(
    IngressParser(),
    Ingress(),
    IngressDeparser(),
    EgressParser(),
    Egress(),
    EgressDeparser()
) pipe;

Switch(pipe) main;

