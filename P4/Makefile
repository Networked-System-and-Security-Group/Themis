.PHONY: switch switch-p4 switch-ctrl runtime-libs clean

CONTROL := control
CONTROL_BIN := control-bin
SDE_INSTALL ?= /sde/bf-sde-9.7.1/install
RUNTIME_LIB_DIR := lib-runtime

switch: switch-p4 switch-ctrl

switch-p4: dataplane/themis.p4 dataplane/header.p4 dataplane/util.p4
	./p4_build.sh --with-tofino2 $< P4_VERSION=p4_16 P4_ARCHITECTURE=tna

switch-ctrl: controlplane/ctrl.c controlplane/headers.h controlplane/switch_config.h config.h runtime-libs
	gcc -DBFRT_GENERIC_FLAGS -I$(SDE_INSTALL)/include -g -O2 -std=gnu11 \
		-L/usr/local/lib -L$(SDE_INSTALL)/lib \
		-Wl,--disable-new-dtags \
		-Wl,-rpath,'$$ORIGIN/$(RUNTIME_LIB_DIR)' -Wl,-rpath,$(SDE_INSTALL)/lib \
		-Wl,-rpath-link,$(SDE_INSTALL)/lib \
		$< -o $(CONTROL_BIN) \
		-ldriver -lbf_switchd_lib -lbfsys -lbfutils -lbf_switch -lm -lpthread
	printf '%s\n' '#!/usr/bin/env sh' \
		'DIR=$$(CDPATH= cd -- "$$(dirname -- "$$0")" && pwd)' \
		'export LD_LIBRARY_PATH="$$DIR/$(RUNTIME_LIB_DIR):$(SDE_INSTALL)/lib$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}"' \
		'exec "$$DIR/$(CONTROL_BIN)" "$$@"' > $(CONTROL)
	chmod +x $(CONTROL)

runtime-libs:
	mkdir -p $(RUNTIME_LIB_DIR)
	ln -sfn $(SDE_INSTALL)/lib/libbf_switchd_lib.so $(RUNTIME_LIB_DIR)/libbf_switchd_lib.so.0
	ln -sfn $(SDE_INSTALL)/lib/libbfsys.so $(RUNTIME_LIB_DIR)/libbfsys.so.0
	ln -sfn $(SDE_INSTALL)/lib/libbfutils.so $(RUNTIME_LIB_DIR)/libbfutils.so.0
	ln -sfn $(SDE_INSTALL)/lib/libdriver.so $(RUNTIME_LIB_DIR)/libdriver.so.0
	ln -sfn $(SDE_INSTALL)/lib/libthrift-0.13.0.so $(RUNTIME_LIB_DIR)/libthrift-0.11.0.so
	if [ -f $(SDE_INSTALL)/lib/libtofinopdfixed_thrift.so ]; then \
		ln -sfn $(SDE_INSTALL)/lib/libtofinopdfixed_thrift.so $(RUNTIME_LIB_DIR)/libtofinopdfixed_thrift.so.0; \
	else \
		printf '%s\n' 'void themis_tofinopdfixed_thrift_stub(void) {}' | \
			gcc -x c -shared -fPIC -Wl,-soname,libtofinopdfixed_thrift.so.0 \
				-o $(RUNTIME_LIB_DIR)/libtofinopdfixed_thrift.so.0 -; \
	fi


clean:
	-rm -f $(CONTROL) $(CONTROL_BIN) bf_drivers.log* zlog-cfg-cur
	-rm -rf $(P4_BUILD_DIR) $(RUNTIME_LIB_DIR)

# .DEFAULT_GOAL :=
