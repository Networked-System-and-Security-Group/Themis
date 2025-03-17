# Themis
## NS3 Simulation
## P4 Testbed
Themis's main concern is whether the daemon packet sent via eBPF can be received by the switch CPU in time and successfully install the flow rule before the first RDMA data packet arrives. Therefore, we build a testbed to verify its validity. The topology is described below:

```
Host A --- Switch --- Host B
```

### Quickstart
**Start the switch:**

```
cd P4
$SDE_INSTALL/bin/bf_kdrv_mod_load $SDE_INSTALL
ls /dev/bf0
make switch
./control
```

**Start the eBPF daemon:**

```
cd eBPF
sudo python daemon.py
```