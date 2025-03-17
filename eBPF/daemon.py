import ctypes
import socket
import struct
from bcc import BPF
from ctypes import string_at


BROADCAST_IP = "255.255.255.255"
BROADCAST_PORT = 12345


bpf_program = """
#include <linux/ptrace.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

// Use the value of attr->dest_qp_num
#define IBV_QP_AV (1 << 7)
// Use the value of attr->dest_qp_num
#define IBV_QP_DEST_QPN (1 << 20)

BPF_PERF_OUTPUT(events);

struct ibv_qp {
        void                 *context;
        void                   *qp_context;
        void         *pd;
        void         *send_cq;
        void         *recv_cq;
        void           *srq;
        uint32_t                handle;
        uint32_t                qp_num;
};
struct ibv_qp_cap {
	uint32_t		max_send_wr;
	uint32_t		max_recv_wr;
	uint32_t		max_send_sge;
	uint32_t		max_recv_sge;
	uint32_t		max_inline_data;
};
union ibv_gid {
	uint8_t			raw[16];
	struct {
		__be64	subnet_prefix;
		__be64	interface_id;
	} global;
};
struct ibv_global_route {
	union ibv_gid		dgid;
	uint32_t		flow_label;
	uint8_t			sgid_index;
	uint8_t			hop_limit;
	uint8_t			traffic_class;
};
struct ibv_ah_attr {
	struct ibv_global_route	grh;
	uint16_t		dlid;
	uint8_t			sl;
	uint8_t			src_path_bits;
	uint8_t			static_rate;
	uint8_t			is_global;
	uint8_t			port_num;
};
struct ibv_qp_attr {
	int                     qp_state;
	int                     cur_qp_state;
	int                     path_mtu;
	int               	path_mig_state;
	uint32_t		qkey;
	uint32_t		rq_psn;
	uint32_t		sq_psn;
	uint32_t		dest_qp_num;
	unsigned int		qp_access_flags;
	struct ibv_qp_cap	cap;
	struct ibv_ah_attr	ah_attr;
	// struct ibv_ah_attr	alt_ah_attr;
	// uint16_t		pkey_index;
	// uint16_t		alt_pkey_index;
	// uint8_t			en_sqd_async_notify;
	// uint8_t			sq_draining;
	// uint8_t			max_rd_atomic;
	// uint8_t			max_dest_rd_atomic;
	// uint8_t			min_rnr_timer;
	// uint8_t			port_num;
	// uint8_t			timeout;
	// uint8_t			retry_cnt;
	// uint8_t			rnr_retry;
	// uint8_t			alt_port_num;
	// uint8_t			alt_timeout;
	// uint32_t		rate_limit;
};

struct event_data {
    uint32_t dIP;
    uint32_t cqpn;
    uint32_t sqpn;
    uint32_t flag;
};

int trace_modify_qp(struct pt_regs *ctx) {
    struct ibv_qp qp;
    struct ibv_qp_attr attr = {0};
    long eBPF_status;
    uint32_t cqpn = 0;
    uint32_t sqpn = 0;
    uint64_t tmp_dIP = 0;
    uint32_t dIP = 0;

    int attr_mask = PT_REGS_PARM3(ctx);
    if (!(attr_mask & IBV_QP_AV) || !(attr_mask & IBV_QP_AV)) {
		return 0;
	}

    eBPF_status = bpf_probe_read_user(&qp, sizeof(qp), (void *)PT_REGS_PARM1(ctx));
    bpf_trace_printk("Status of reading PT_REGS_PARM1 in ibv_modify_qp: %d\\n", eBPF_status);
    
    eBPF_status = bpf_probe_read_user(&attr, sizeof(attr), (void *)PT_REGS_PARM2(ctx));
    bpf_trace_printk("Status of reading PT_REGS_PARM2 in ibv_destroy_qp: %d\\n", eBPF_status);
	
    bpf_trace_printk(\"interface_id: %lx\\n\",
        attr.ah_attr.grh.dgid.global.interface_id);
        
    dIP = (attr.ah_attr.grh.dgid.global.interface_id >> 32);
    dIP = bpf_ntohl(dIP);
    bpf_trace_printk("dIP: %u\\n", dIP);

    bpf_trace_printk("get cqpn from trace_modify_qp\\n");
    cqpn = qp.qp_num;
    bpf_trace_printk("cqpn: %d\\n", cqpn);
    
    bpf_trace_printk("get sqpn from trace_modify_qp\\n");
    sqpn = attr.dest_qp_num;
    bpf_trace_printk("sqpn: %d\\n", sqpn);
    
    struct event_data data = {
        .dIP = dIP,
        .cqpn = cqpn,
        .sqpn = sqpn,
        .flag = 1,
    };
    events.perf_submit(ctx, &data, sizeof(data));
    bpf_trace_printk("sizeof(event_data): %lu\\n", sizeof(struct event_data));
    
    return 0;
}

int trace_destroy_qp(struct pt_regs *ctx) {
    struct ibv_qp *qp;
    long eBPF_status;
    uint32_t cqpn = 0;

    eBPF_status = bpf_probe_read_user(&qp, sizeof(qp), (void *)PT_REGS_PARM1(ctx));
    bpf_trace_printk("Status of reading PT_REGS_PARM1 of ibv_destroy_qp: %d\\n", eBPF_status);

    bpf_trace_printk("get cqpn from trace_destroy_qp\\n");
    cqpn = qp.qp_num;
    bpf_trace_printk("cqpn: %d\\n", cqpn);

    // Rixin: there is no dIP and sQPN in ibv_destroy_qp
    struct event_data data = {
        .dIP = 0,
        .cqpn = cqpn,
        .sqpn = 0,
        .flag = 0,
    };
    events.perf_submit(ctx, &data, sizeof(data));
    bpf_trace_printk("sizeof(event_data): %lu\\n", sizeof(struct event_data));

    return 0;
}
"""


b = BPF(
    text=bpf_program,
)
b.attach_uprobe(name="/lib/x86_64-linux-gnu/libibverbs.so", sym="ibv_modify_qp", fn_name="trace_modify_qp")
b.attach_uprobe(name="/lib/x86_64-linux-gnu/libibverbs.so", sym="ibv_destroy_qp", fn_name="trace_destroy_qp")

class EventData(ctypes.Structure):
    _fields_ = [
        ("dIP", ctypes.c_uint32),
        ("cqpn", ctypes.c_uint32),
        ("sqpn", ctypes.c_uint32),
        ("flag", ctypes.c_uint32),
    ]

def send_daemon_pkt(dIP, cqpn, sqpn, flag):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target_ip = "192.168.5.220" # IP of switch
    server_address = (target_ip, BROADCAST_PORT)

    # message = f"QPN: {qpn}, dIP: {dIP}".encode()
    message = struct.pack("!IIII", dIP, cqpn, sqpn, flag)

    # 发送消息
    sock.sendto(message, server_address)
    sock.close()


def print_event(cpu, data, size):
    # 解析数据
    event = ctypes.cast(data, ctypes.POINTER(EventData)).contents
    cqpn = event.cqpn
    dIP = event.dIP
    sqpn = event.sqpn
    flag = event.flag

    # 打印信息
    print(f"dIP: {dIP}, cQPN: {cqpn}, sQPN: {sqpn}, flag: {flag}")

    # 发送 UDP 广播
    send_daemon_pkt(dIP, cqpn, sqpn, flag)


b["events"].open_perf_buffer(print_event)


print("Tracing ibv_modify_qp and ibv_destroy_qp... Press Ctrl+C to exit")



try:
    while True:
        b.perf_buffer_poll()
except KeyboardInterrupt:
    print("Exiting...")
