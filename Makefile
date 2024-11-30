.PHONY: switch switch-p4 switch-ctrl clean


switch: switch-p4 switch-ctrl

switch-p4: dataplane/themis.p4 dataplane/header.p4 dataplane/util.p4
	$(SDE)/p4_build.sh $<

switch-ctrl: controlplane/ctrl.c controlplane/headers.h controlplane/switch_config.h config.h
	gcc -I$$SDE_INSTALL/include -g -O2 -std=gnu11  -L/usr/local/lib -L$$SDE_INSTALL/lib -Wl,-rpath=/opt/bf-sde-9.2.0/install/lib\
		$< -o contrl \
		-ldriver -lbfsys -lbfutils -lbf_switchd_lib -lm -lpthread  \




clean:
	-rm -f contrl bf_drivers.log* zlog-cfg-cur

# .DEFAULT_GOAL :=