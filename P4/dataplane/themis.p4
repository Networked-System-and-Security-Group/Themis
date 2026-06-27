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
    bit<8> is_last_recir_pkt; // rixin: 对于最后一次recirculate的包，recir_h 头部还在hdr里，所以mirror出来的包也会包含recir_h，此变量是为了告知egress需要extract recir_h header
    MirrorId_t ing_mir_ses; // rixin: mirror session id
    pkt_type_t pkt_type; // rixin: 用于让ingress pipeline告诉ingress deparser是什么类型
    bit<16> pnp_udp_src_port; // carries encoded sQPN[16:3] in low 14 bits
    bit<8> pnp_dip_hash; // egress uses low 3 bits as sQPN[2:0]
    // OLD LOGIC DISABLED:
    // This metadata was used by the temporary dynamic mirror-session path that
    // returned CNP to the original ingress dev_port. The active path uses one
    // fixed CNP mirror session/egress port again.
    //
    // bit<16> original_ingress_port;
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
        meta.is_last_recir_pkt = 0;
        meta.ing_mir_ses = 0;
        meta.pkt_type = PKT_TYPE_NORMAL;
        meta.pnp_udp_src_port = 0;
        meta.pnp_dip_hash = 0;
        tofino_parser.apply(pkt, ig_intr_md);
        transition select(ig_intr_md.ingress_port) {
            // rixin: 当前正在parse一个已循环的包
            RECIRC_PORT: parse_recir;
            default: parse_ethernet;
        }
    }

    state parse_recir {
        pkt.extract(hdr.recir);
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            ETHERTYPE_IPV4:  parse_ipv4;
	        ETHERTYPE_ARP:   parse_arp;
            default: accept;
        }
    }

    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            IP_PROTOCOLS_TCP:   parse_tcp;
	        IP_PROTOCOLS_UDP:   parse_udp;
            default: accept;
        }
    }
    
    state parse_arp {
        pkt.extract(hdr.arp);
        transition accept;
    }

    state parse_tcp {
        pkt.extract(hdr.tcp);
        transition accept;
    }

    state parse_udp {
        pkt.extract(hdr.udp);
        transition select(hdr.udp.dst_port) {
            UDP_ROCE_DST_PORT: parse_bth;
            default: accept;
        }
    }

    state parse_bth {
        pkt.extract(hdr.bth);
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
        
    // rixin: P4中，一个table只能被apply一次
    bool apply_l3 = false;
    Hash<bit<8>>(HashAlgorithm_t.CRC8) pnp_dip_hash_calc;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) recir_debug_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) recir_return_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) recir_return_roce_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) cnp_debug_counter;
    Counter<bit<32>, bit<32>>(512, CounterType_t.PACKETS) ingress_port_debug_counter;

    // rixin: 确保可通信
    action forward(PortId_t egress_port) {
	    ig_tm_md.ucast_egress_port = egress_port;
	}

	table l3_table {
	    key = {
	    	hdr.ipv4.dst_addr : exact;
	    }
	    actions = {
            forward;
            @defaultonly NoAction;
	    }
	    size = HOST_IF_NUM;
	}

	table arp_table {
	    key = {
	    	hdr.arp.dstIpv4Addr : exact;
	    }
	    actions = {
            forward;
            @defaultonly NoAction;
	    }
	    size = HOST_IF_NUM;
	}

    // rixin: 采用ing-to-eg mirror机制，mirrored pkt作cnp
    // 下面是对origin pkt的处理
    action reconstruct_pnp_sqpn() {
        meta.pnp_udp_src_port = hdr.udp.src_port;
        meta.pnp_dip_hash = pnp_dip_hash_calc.get({hdr.ipv4.dst_addr});
    }

    action set_mirror_md() {
        // rixin: 设置一些信息，用于deparser进行mirror操作
        meta.ing_mir_ses =  CNP_SES_ID; // rixin: fixed CNP mirror session
        // OLD LOGIC DISABLED:
        // This temporarily selected a mirror session from the original ingress
        // dev_port low 8 bits. It is disabled because the user wants CNP to go
        // back through one fixed mirror port again.
        //
        // meta.ing_mir_ses = (MirrorId_t)meta.original_ingress_port[7:0];
        meta.pkt_type = PKT_TYPE_MIRROR; // rixin: 这个包需要被mirror
        ig_dprsr_md.mirror_type = MIRROR_TYPE_I2E; // rixin: mirror类型告知deparser

        // OLD LOGIC DISABLED:
        // This used to modify the original RoCE data packet while requesting
        // the CNP mirror. That can break RC delivery because the RNIC validates
        // RoCE packet integrity; changing fields such as UDP source port after
        // the packet leaves the sender can make the receiver drop the packet
        // and therefore never send the ACK needed for RDMA WRITE completion.
        // The active path keeps the original packet untouched; the mirrored
        // CNP path reconstructs sQPN from the mirrored packet's original UDP
        // source port and destination IP.
        //
        // hdr.ipv4.ecn = ECN_NOT_ECT;
        // hdr.udp.src_port = UDP_ROCE_DST_PORT;
        cnp_debug_counter.count(0);
    }

    action start_one_recir() {
        hdr.recir.setValid();
        hdr.recir.cnt = 0;
        apply_l3 = false;
        ig_tm_md.ucast_egress_port = RECIRC_PORT;
        ig_tm_md.bypass_egress = 1;
        recir_debug_counter.count(0);
    }

    // OLD LOGIC DISABLED:
    // The previous TRP path recirculated only flows installed in trp_cnp_store,
    // with the loop count supplied by control plane. The active path below no
    // longer uses this table; it splits original RoCE data packets by PSN low
    // bit, and only one half enters start_one_recir().
    //
    // action trp_set_loop(bit<32> loop_num) {
    //     hdr.recir.setValid();
    //     hdr.recir.cnt = loop_num - 1;
    //     apply_l3 = false;
    //     ig_tm_md.ucast_egress_port = RECIRC_PORT;
    //     ig_tm_md.bypass_egress = 1;
    // }
    //
    // table trp_cnp_store {
    //     key = {
    //         hdr.ipv4.dst_addr: exact;
    //         hdr.bth.dst_qpn: exact;
    //     }
    //     actions = {
    //         trp_set_loop;
    //         @defaultonly NoAction;
    //     }
    //     size = MAX_NUM_TRP_FLOW;
    // }

    // OLD LOGIC DISABLED:
    // init_recir() used INIT_RECIR_CNT and therefore could loop packets multiple
    // times. The active path now uses start_one_recir() only for the
    // recirculation half of original RoCE data packets.
    //
    // action init_recir() {
    //     apply_l3 = false;
    //
    //     hdr.recir.setValid();
    //     hdr.recir.cnt = INIT_RECIR_CNT;
    //     hdr.recir.cnt = hdr.recir.cnt - 1;
    //
    //     ig_tm_md.ucast_egress_port = RECIRC_PORT;
    //     ig_tm_md.bypass_egress = 1;
    // }

    // OLD LOGIC DISABLED:
    // This helper was kept out of the active path because conditional control
    // inside actions has caused Tofino compiler issues in this codebase. The
    // active apply block performs the recirculation counter handling directly.
    //
    // action decrease_recir(bit<32> recir_cnt) {
    //     if (recir_cnt == 0) {
    //         apply_l3 = true;
    //         hdr.recir.setInvalid();
    //     }
    //     else {
    //         hdr.recir.cnt = hdr.recir.cnt - 1;
    //         apply_l3 = false;
    //         ig_tm_md.ucast_egress_port = RECIRC_PORT;
    //         ig_tm_md.bypass_egress = 1;
    //     }
    // }

	apply {
        ingress_port_debug_counter.count((bit<32>)ig_intr_md.ingress_port);

        // rixin: 确保可通信
	    if (hdr.ethernet.ether_type == ETHERTYPE_IPV4) {
		    apply_l3 = true;
	    }
	    else {
	   	    arp_table.apply();
	    }
        
        // rixin: 开始循环
        if (hdr.recir.isValid()) {
            recir_return_counter.count(0);
            if (hdr.udp.isValid() && hdr.bth.isValid() &&
                hdr.udp.dst_port == UDP_ROCE_DST_PORT &&
                hdr.bth.opcode != CNP_OPCODE) {
                recir_return_roce_counter.count(0);
            }
            if (hdr.recir.cnt == 0) {
                apply_l3 = true;
                meta.is_last_recir_pkt = 0;
                hdr.recir.setInvalid();
                // rixin: 这里不能bypass egress，不然bridged_md会被emit到包头里
                // ig_tm_md.bypass_egress = 1;
            }
            else {
                hdr.recir.cnt = hdr.recir.cnt - 1;
                apply_l3 = false;
                ig_tm_md.ucast_egress_port = RECIRC_PORT;
                ig_tm_md.bypass_egress = 1;
            }
        }
        else {
            // OLD LOGIC DISABLED:
            // The previous behavior recirculated every non-recirculated packet
            // immediately, regardless of whether it was RoCE. That also used
            // INIT_RECIR_CNT and could create multiple loops. The active logic
            // below restricts this to RoCE data packets and loops them once.
            //
            // init_recir();
            // apply_l3 = false;
            //
            // hdr.recir.setValid();
            // hdr.recir.cnt = INIT_RECIR_CNT;
            // hdr.recir.cnt = hdr.recir.cnt - 1;
            //
            // ig_tm_md.ucast_egress_port = RECIRC_PORT;
            // ig_tm_md.bypass_egress = 1;

            if (hdr.ethernet.ether_type == ETHERTYPE_IPV4) {
                apply_l3 = true;
            }
            else {
                apply_l3 = false;
            }
        }

        // Split RoCE data packets into two mutually exclusive groups:
        // PSN low bit 0 -> recirculation once; PSN low bit 1 -> CNP mirror.
        if (!hdr.recir.isValid() &&
            ig_intr_md.ingress_port != RECIRC_PORT &&
            hdr.udp.isValid() && hdr.bth.isValid() &&
            hdr.udp.dst_port == UDP_ROCE_DST_PORT &&
            hdr.bth.opcode != CNP_OPCODE &&
            hdr.bth.psn[0:0] == 1w0) {
            start_one_recir();
        }

        // rixin: 只调用一次 l3_table.apply()
        if (apply_l3) {
            l3_table.apply();
        }

        // Generate CNP only for the other half of original RoCE data packets.
        // Returned recirculation packets are excluded by ingress_port check, so
        // no packet takes both recirculation and CNP paths.
        if (ig_tm_md.ucast_egress_port != RECIRC_PORT &&
            ig_intr_md.ingress_port != RECIRC_PORT &&
            hdr.udp.isValid() && hdr.bth.isValid() &&
            hdr.udp.dst_port == UDP_ROCE_DST_PORT &&
            hdr.bth.opcode != CNP_OPCODE &&
            hdr.bth.psn[0:0] == 1w1) {
            reconstruct_pnp_sqpn();
            set_mirror_md();
        }

        // rixin: 设置mirrored pkt的bridge header (相关信息见Figure3 from "P4_16 Tofino Native Architecture")
        // rixin: setValid + pkt.emit(hdr.bridged_md) <==> 让bridged_md加入头部
        // rixin: 循环包直接bypass egress，所以不要加bridged_md
        if (ig_tm_md.ucast_egress_port != RECIRC_PORT) {
            hdr.bridged_md.setValid();
            hdr.bridged_md.pkt_type = PKT_TYPE_NORMAL;
        }
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
            mirror_h mirror_md;
            mirror_md.setValid();
            mirror_md.pkt_type = meta.pkt_type;
            // OLD LOGIC DISABLED:
            // This used to emit extra fields in mirror_h:
            // {pkt_type, is_last_recir_pkt, pnp_udp_src_port, pnp_dip_hash}.
            // The resulting mirrored packet reached egress, but parsing did not
            // line up with BTH on this Tofino2/SDE stack. The active path matches
            // the SDE tna_mirror pattern and emits only pkt_type; egress
            // reconstructs sQPN from the mirrored packet itself.
            //
            // mirror.emit<mirror_h>(meta.ing_mir_ses, {meta.pkt_type, meta.is_last_recir_pkt, meta.pnp_udp_src_port, meta.pnp_dip_hash});
            //
            // OLD LOGIC DISABLED:
            // This single-field list expression matches the SDE tna_mirror
            // example, but this SDE 9.7.1 build rejected it with T bound to
            // header mirror_h. The active path passes an explicitly typed
            // mirror_h variable instead.
            //
            // mirror.emit<mirror_h>(meta.ing_mir_ses, {meta.pkt_type});
            mirror.emit<mirror_h>(meta.ing_mir_ses, mirror_md);
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
    bit<16> udp_tmp_checksum;
}

    /***********************  P A R S E R  **************************/

parser EgressParser(packet_in        pkt,
    /* User */
    out my_egress_headers_t          hdr,
    out my_egress_metadata_t         meta,
    /* Intrinsic */
    out egress_intrinsic_metadata_t  eg_intr_md)
{
    Checksum() udp_csum;
   
    TofinoEgressParser() tofino_parser;

    state start {
        meta.is_mirrored = 0;
        meta.udp_tmp_checksum = 0;
        tofino_parser.apply(pkt, eg_intr_md);
        transition parse_metadata;
    }

    state parse_metadata {
        // rixin: lookahead不移动指针，extract移动指针
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
        pkt.extract(hdr.mirror_md);
        meta.is_mirrored = 1;
        // OLD LOGIC DISABLED:
        // This previously selected between parse_recir and parse_bridged_md
        // using hdr.mirror_md.is_last_recir_pkt. The active mirror metadata now
        // follows the SDE tna_mirror example and carries only pkt_type, so the
        // mirrored payload begins at ethernet after mirror_h.
        //
        // transition select(hdr.mirror_md.is_last_recir_pkt) {
        //     1: parse_recir;
        //     default: parse_bridged_md;
        // }
        transition parse_ethernet;
    }

    state parse_recir {
        recir_h tmp;
        // rixin: 丢了就好
        pkt.extract(tmp);
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            ETHERTYPE_IPV4: parse_ipv4;
            ETHERTYPE_ARP:  parse_arp;
            default: accept;
        }
    }

    state parse_arp {
        pkt.extract(hdr.arp);
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
        transition accept;
    }

    state parse_udp {
        pkt.extract(hdr.udp);
        // rixin: udp和tcp都是把payload算进checksum里了，下面是增量式的checksum更新
        udp_csum.subtract({hdr.udp.checksum});
        udp_csum.subtract({hdr.udp.src_port, hdr.udp.dst_port});
        meta.udp_tmp_checksum = udp_csum.get();
        transition select(hdr.udp.dst_port) {
            UDP_ROCE_DST_PORT:   parse_bth;
            default: accept;
        }
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
    Hash<bit<8>>(HashAlgorithm_t.CRC8) egress_pnp_dip_hash_calc;
    // Create indirect counter
    Counter<bit<32>, bit<32>>(
        512, CounterType_t.PACKETS) indirect_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) egress_mirror_seen_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) egress_mirror_bth_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) egress_mirror_cnp_set_counter;
    Counter<bit<32>, bit<32>>(1, CounterType_t.PACKETS) egress_roce_data_counter;

    action inc_cnp_counter () {
        indirect_counter.count(0);
    }

    table count_cnp {
        key = {
            hdr.bth.opcode: exact;
        }
        actions = {
            inc_cnp_counter;
            @defaultonly NoAction;
        }
        size = 1;
    }

    apply {
        bit<8> pnp_dip_hash = egress_pnp_dip_hash_calc.get({hdr.ipv4.dst_addr});
        mac_addr_t tmp1 = hdr.ethernet.dst_addr;
        ipv4_addr_t tmp2 = hdr.ipv4.dst_addr;

        // rixin: 判断出当前是个mirrored pkt即cnp
        if (meta.is_mirrored == 1)
        {
            egress_mirror_seen_counter.count(0);
            // rixin: 设置好cnp的mac地址
            hdr.ethernet.dst_addr = hdr.ethernet.src_addr;
            hdr.ethernet.src_addr = tmp1;
            // rixin: 设置好cnp的ip地址
            hdr.ipv4.dst_addr = hdr.ipv4.src_addr;
            hdr.ipv4.src_addr = tmp2;
            hdr.ipv4.ecn = ECN_NOT_ECT;
            // PNP reconstructs sQPN from the mirrored original data packet.
            // Before the IP swap above, hdr.ipv4.dst_addr is the original server
            // IP and hdr.udp.src_port still carries encoded sQPN[16:3].
            hdr.bth.dst_qpn[23:17] = 0;
            hdr.bth.dst_qpn[16:3] = hdr.udp.src_port[13:0];
            hdr.bth.dst_qpn[2:0] = pnp_dip_hash[2:0];
            hdr.bth.opcode = CNP_OPCODE;
            if (hdr.bth.isValid()) {
                egress_mirror_bth_counter.count(0);
                egress_mirror_cnp_set_counter.count(0);
            }
        }
        else if (hdr.udp.isValid() && hdr.bth.isValid() &&
                 hdr.udp.dst_port == UDP_ROCE_DST_PORT &&
                 hdr.bth.opcode != CNP_OPCODE) {
            egress_roce_data_counter.count(0);
        }
        if (hdr.bth.isValid()) {
            count_cnp.apply();
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
    Checksum() ipv4_csum;
    Checksum() udp_csum;
    apply {
        if (hdr.ipv4.isValid()) {
            hdr.ipv4.hdr_checksum = ipv4_csum.update({
            hdr.ipv4.version, hdr.ipv4.ihl, hdr.ipv4.diffserv, hdr.ipv4.ecn,
            hdr.ipv4.total_len,
            hdr.ipv4.identification,
            hdr.ipv4.flags, hdr.ipv4.frag_offset,
            hdr.ipv4.ttl, hdr.ipv4.protocol,
            /* skip hdr.ipv4.hdr_checksum, */
            hdr.ipv4.src_addr,
            hdr.ipv4.dst_addr});
        }
        if (hdr.udp.isValid()) {
            hdr.udp.checksum = udp_csum.update({
            hdr.udp.src_port,
            hdr.udp.dst_port, meta.udp_tmp_checksum}, true);
        }
        pkt.emit(hdr.ethernet);
        pkt.emit(hdr.ipv4);
        pkt.emit(hdr.arp);
        pkt.emit(hdr.tcp);
        pkt.emit(hdr.udp);
        pkt.emit(hdr.bth);
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
