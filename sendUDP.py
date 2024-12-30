import socket
import struct
from bcc import BPF


BROADCAST_IP = "255.255.255.255"
BROADCAST_PORT = 12345


bpf_program =
"""
#include <linux/ptrace.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

BPF_PERF_OUTPUT(events);

int trace_modify_qp(struct pt_regs *ctx) {
    struct ibv_qp *qp;
    u32 qpn = 0;
    bpf_probe_read(&qp, sizeof(qp), (void *)PT_REGS_PARM1(ctx));
    if(qp){
        qpn = qp->qp_num;
    }
    events.perf_submit(ctx, &qpn, sizeof(qpn));
    return 0;
}

int trace_destroy_qp(struct pt_regs *ctx) {
    struct ibv_qp *qp;
    u32 qpn = 0;
    bpf_probe_read(&qp, sizeof(qp), (void *)PT_REGS_PARM1(ctx));
    if (qp) {
        qpn = qp->qp_num;
    }
    events.perf_submit(ctx, &qpn, sizeof(qpn));
    return 0;
}
"""


b = BPF(text=bpf_program)
b.attach_kprobe(event="ibv_modify_qp", fn_name="trace_modify_qp")
b.attach_kprobe(event="ibv_destroy_qp", fn_name="trace_destroy_qp")


def send_udp_broadcast(qpn):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    server_address = (BROADCAST_IP, BROADCAST_PORT)
    message = f"QPN: {qpn}".encode()
    sock.sendto(message, server_address)
    sock.close()


def print_event(cpu, data, size):
    qpn = struct.unpack("I", data)[0]  # 解包数据
    print(f"Captured QPN: {qpn}")
    send_udp_broadcast(qpn)


b["events"].open_perf_buffer(print_event)


print("Tracing ibv_modify_qp and ibv_destroy_qp... Press Ctrl+C to exit")



try:
    while True:
        b.perf_buffer_poll()
except KeyboardInterrupt:
    print("Exiting...")
