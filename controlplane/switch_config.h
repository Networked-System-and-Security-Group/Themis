#ifndef _SWITCH_CONFIG_H
#define _SWITCH_CONFIG_H

#if __TOFINO_MODE__ == 0
const switch_port_t PORT_LIST[] = {
	{"1/0"}, {"1/1"}, {"1/2"}, {"1/3"},
    {"2/0"}, {"2/1"}, {"2/2"}, {"2/3"}
};
#else
#endif
#endif