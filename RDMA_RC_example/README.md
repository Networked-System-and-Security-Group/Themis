# RDMA example code

This code demonstrates how to use several typical RDMA Verbs APIs, including Send, Receive, RDMA Read and RDMA Write. After establishing a QP between client and server, the code has 4 tests.

```
Test 1: server use 'Send' operation to put 'MSG_S' into client's buffer.
Test 2: client use 'Send' operation to put 'MSG_C' into server's buffer.
Test 3: client use 'RDMA Read' operation to read server's buffer 'RDMAMSGR'.
Test 4: client use 'RDMA Write' operation to put 'RDMAMSGW' into server's buffer.
```

## Run the code

Compile:

`gcc -Wall -O2 -o RDMA_RC_example RDMA_RC_example.c -lmlx5 -libverbs`

Client side:

`RDMA_RC_example -g <gid_idx> -d <ib-dev> <server_ip>`

Server side:

`RDMA_RC_example -g <gid_idx> -d <ib-dev>`

## QP connection updates

The example maintains the local RC QP connection after exchanging connection
data. When `--qp-notify-host` is set, it sends a UDP update after the QP reaches
RTS and before the QP is destroyed.

Packet format is compatible with the old eBPF daemon path:

```
uint32_t dIP   // remote IPv4 address, network byte order
uint32_t cQPN  // local QP number, network byte order
uint32_t sQPN  // remote QP number, network byte order
uint32_t flag  // 1 = add/update, 0 = delete
```

Example:

```
# server
./RDMA_RC_example -g 3 -d mlx5_0 --qp-notify-host <switch_or_collector_ip>

# client
./RDMA_RC_example -g 3 -d mlx5_0 --qp-notify-host <switch_or_collector_ip> <server_ip>
```

For the THEMIS PNP UDP-source-port encoding path, add `--udp-sport-from-qpn`.
It calls `mlx5dv_modify_qp_udp_sport()` after QP creation and sets the actual
RoCE UDP source port to `0xC000 | ((local_qpn >> 3) & 0x3fff)`. The P4 data
plane reads the low 14 bits as `sQPN[16:3]`.
