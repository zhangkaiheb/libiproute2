/*
 * iplink_bridge_slave.c	Bridge slave device support
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:     Jiri Pirko <jiri@resnulli.us>
 */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_link.h>
#include <linux/if_bridge.h>

#include "rt_names.h"
#include "utils.h"
#include "ip_common.h"

static void print_explain(FILE *f)
{
	fprintf(f,
		"Usage: ... bridge_slave [ fdb_flush ]\n"
		"                        [ state STATE ]\n"
		"                        [ priority PRIO ]\n"
		"                        [ cost COST ]\n"
		"                        [ guard {on | off} ]\n"
		"                        [ hairpin {on | off} ]\n"
		"                        [ fastleave {on | off} ]\n"
		"                        [ root_block {on | off} ]\n"
		"                        [ learning {on | off} ]\n"
		"                        [ flood {on | off} ]\n"
		"                        [ proxy_arp {on | off} ]\n"
		"                        [ proxy_arp_wifi {on | off} ]\n"
		"                        [ mcast_router MULTICAST_ROUTER ]\n"
		"                        [ mcast_fast_leave {on | off} ]\n"
		"                        [ mcast_flood {on | off} ]\n"
		"                        [ group_fwd_mask MASK ]\n"
		"                        [ neigh_suppress {on | off} ]\n"
		"                        [ vlan_tunnel {on | off} ]\n"
	);
}

static void explain(void)
{
	print_explain(stderr);
}

static const char *port_states[] = {
	[BR_STATE_DISABLED] = "disabled",
	[BR_STATE_LISTENING] = "listening",
	[BR_STATE_LEARNING] = "learning",
	[BR_STATE_FORWARDING] = "forwarding",
	[BR_STATE_BLOCKING] = "blocking",
};

static const char *fwd_mask_tbl[16] = {
	[0]	= "stp",
	[2]	= "lacp",
	[14]	= "lldp"
};

static void print_portstate(FILE *f, __u8 state)
{
	if (state <= BR_STATE_BLOCKING)
		print_string(PRINT_ANY,
			     "state",
			     "state %s ",
			     port_states[state]);
	else
		print_int(PRINT_ANY, "state_index", "state (%d) ", state);
}

static void _print_onoff(FILE *f, char *json_flag, char *flag, __u8 val)
{
	if (is_json_context())
		print_bool(PRINT_JSON, flag, NULL, val);
	else
		fprintf(f, "%s %s ", flag, val ? "on" : "off");
}

static void _print_timer(FILE *f, const char *attr, struct rtattr *timer)
{
	struct timeval tv;

	__jiffies_to_tv(&tv, rta_getattr_u64(timer));
	if (is_json_context()) {
		json_writer_t *jw = get_json_writer();

		jsonw_name(jw, attr);
		jsonw_printf(jw, "%i.%.2i",
			     (int)tv.tv_sec, (int)tv.tv_usec / 10000);
	} else {
		fprintf(f, "%s %4i.%.2i ", attr, (int)tv.tv_sec,
			(int)tv.tv_usec / 10000);
	}
}

static void _bitmask2str(__u16 bitmask, char *dst, size_t dst_size,
			 const char **tbl)
{
	int len, i;

	for (i = 0, len = 0; bitmask; i++, bitmask >>= 1) {
		if (bitmask & 0x1) {
			if (tbl[i])
				len += snprintf(dst + len, dst_size - len, "%s,",
						tbl[i]);
			else
				len += snprintf(dst + len, dst_size - len, "0x%x,",
						(1 << i));
		}
	}

	if (!len)
		snprintf(dst, dst_size, "0x0");
	else
		dst[len - 1] = 0;
}

static void bridge_slave_print_opt(struct link_util *lu, FILE *f,
				   struct rtattr *tb[])
{
	if (!tb)
		return;

	if (tb[IFLA_BRPORT_STATE])
		print_portstate(f, rta_getattr_u8(tb[IFLA_BRPORT_STATE]));

	if (tb[IFLA_BRPORT_PRIORITY])
		print_int(PRINT_ANY,
			  "priority",
			  "priority %d ",
			  rta_getattr_u16(tb[IFLA_BRPORT_PRIORITY]));

	if (tb[IFLA_BRPORT_COST])
		print_int(PRINT_ANY,
			  "cost",
			  "cost %d ",
			  rta_getattr_u32(tb[IFLA_BRPORT_COST]));

	if (tb[IFLA_BRPORT_MODE])
		_print_onoff(f, "mode", "hairpin",
			     rta_getattr_u8(tb[IFLA_BRPORT_MODE]));

	if (tb[IFLA_BRPORT_GUARD])
		_print_onoff(f, "guard", "guard",
			     rta_getattr_u8(tb[IFLA_BRPORT_GUARD]));

	if (tb[IFLA_BRPORT_PROTECT])
		_print_onoff(f, "protect", "root_block",
			     rta_getattr_u8(tb[IFLA_BRPORT_PROTECT]));

	if (tb[IFLA_BRPORT_FAST_LEAVE])
		_print_onoff(f, "fast_leave", "fastleave",
			     rta_getattr_u8(tb[IFLA_BRPORT_FAST_LEAVE]));

	if (tb[IFLA_BRPORT_LEARNING])
		_print_onoff(f, "learning", "learning",
			     rta_getattr_u8(tb[IFLA_BRPORT_LEARNING]));

	if (tb[IFLA_BRPORT_UNICAST_FLOOD])
		_print_onoff(f, "unicast_flood", "flood",
			     rta_getattr_u8(tb[IFLA_BRPORT_UNICAST_FLOOD]));

	if (tb[IFLA_BRPORT_ID])
		print_0xhex(PRINT_ANY, "id", "port_id 0x%x ",
			    rta_getattr_u16(tb[IFLA_BRPORT_ID]));

	if (tb[IFLA_BRPORT_NO])
		print_0xhex(PRINT_ANY, "no", "port_no 0x%x ",
			   rta_getattr_u16(tb[IFLA_BRPORT_NO]));

	if (tb[IFLA_BRPORT_DESIGNATED_PORT])
		print_uint(PRINT_ANY,
			   "designated_port",
			   "designated_port %u ",
			   rta_getattr_u16(tb[IFLA_BRPORT_DESIGNATED_PORT]));

	if (tb[IFLA_BRPORT_DESIGNATED_COST])
		print_uint(PRINT_ANY,
			   "designated_cost",
			   "designated_cost %u ",
			   rta_getattr_u16(tb[IFLA_BRPORT_DESIGNATED_COST]));

	if (tb[IFLA_BRPORT_BRIDGE_ID]) {
		char bridge_id[32];

		br_dump_bridge_id(RTA_DATA(tb[IFLA_BRPORT_BRIDGE_ID]),
				  bridge_id, sizeof(bridge_id));
		print_string(PRINT_ANY,
			     "bridge_id",
			     "designated_bridge %s ",
			     bridge_id);
	}

	if (tb[IFLA_BRPORT_ROOT_ID]) {
		char root_id[32];

		br_dump_bridge_id(RTA_DATA(tb[IFLA_BRPORT_ROOT_ID]),
				  root_id, sizeof(root_id));
		print_string(PRINT_ANY,
			     "root_id",
			     "designated_root %s ", root_id);
	}

	if (tb[IFLA_BRPORT_HOLD_TIMER])
		_print_timer(f, "hold_timer", tb[IFLA_BRPORT_HOLD_TIMER]);

	if (tb[IFLA_BRPORT_MESSAGE_AGE_TIMER])
		_print_timer(f, "message_age_timer",
			     tb[IFLA_BRPORT_MESSAGE_AGE_TIMER]);

	if (tb[IFLA_BRPORT_FORWARD_DELAY_TIMER])
		_print_timer(f, "forward_delay_timer",
			     tb[IFLA_BRPORT_FORWARD_DELAY_TIMER]);

	if (tb[IFLA_BRPORT_TOPOLOGY_CHANGE_ACK])
		print_uint(PRINT_ANY,
			   "topology_change_ack",
			   "topology_change_ack %u ",
			   rta_getattr_u8(tb[IFLA_BRPORT_TOPOLOGY_CHANGE_ACK]));

	if (tb[IFLA_BRPORT_CONFIG_PENDING])
		print_uint(PRINT_ANY,
			   "config_pending",
			   "config_pending %u ",
			   rta_getattr_u8(tb[IFLA_BRPORT_CONFIG_PENDING]));

	if (tb[IFLA_BRPORT_PROXYARP])
		_print_onoff(f, "proxyarp", "proxy_arp",
			     rta_getattr_u8(tb[IFLA_BRPORT_PROXYARP]));

	if (tb[IFLA_BRPORT_PROXYARP_WIFI])
		_print_onoff(f, "proxyarp_wifi", "proxy_arp_wifi",
			     rta_getattr_u8(tb[IFLA_BRPORT_PROXYARP_WIFI]));

	if (tb[IFLA_BRPORT_MULTICAST_ROUTER])
		print_uint(PRINT_ANY,
			   "multicast_router",
			   "mcast_router %u ",
			   rta_getattr_u8(tb[IFLA_BRPORT_MULTICAST_ROUTER]));

	if (tb[IFLA_BRPORT_FAST_LEAVE])
		// not printing any json here because
		// we already printed fast_leave before
		print_string(PRINT_FP,
			     NULL,
			     "mcast_fast_leave %s ",
			     rta_getattr_u8(tb[IFLA_BRPORT_FAST_LEAVE]) ? "on" : "off");

	if (tb[IFLA_BRPORT_MCAST_FLOOD])
		_print_onoff(f, "mcast_flood", "mcast_flood",
			     rta_getattr_u8(tb[IFLA_BRPORT_MCAST_FLOOD]));

	if (tb[IFLA_BRPORT_NEIGH_SUPPRESS])
		_print_onoff(f, "neigh_suppress", "neigh_suppress",
			     rta_getattr_u8(tb[IFLA_BRPORT_NEIGH_SUPPRESS]));

	if (tb[IFLA_BRPORT_GROUP_FWD_MASK]) {
		char convbuf[256];
		__u16 fwd_mask;

		fwd_mask = rta_getattr_u16(tb[IFLA_BRPORT_GROUP_FWD_MASK]);
		print_0xhex(PRINT_ANY, "group_fwd_mask",
			    "group_fwd_mask 0x%x ", fwd_mask);
		_bitmask2str(fwd_mask, convbuf, sizeof(convbuf), fwd_mask_tbl);
		print_string(PRINT_ANY, "group_fwd_mask_str",
			     "group_fwd_mask_str %s ", convbuf);
	}

	if (tb[IFLA_BRPORT_VLAN_TUNNEL])
		_print_onoff(f, "vlan_tunnel", "vlan_tunnel",
			     rta_getattr_u8(tb[IFLA_BRPORT_VLAN_TUNNEL]));
}

static int bridge_slave_parse_on_off(char *arg_name, char *arg_val,
				      struct nlmsghdr *n, int type)
{
	__u8 val;

	if (strcmp(arg_val, "on") == 0)
		val = 1;
	else if (strcmp(arg_val, "off") == 0)
		val = 0;
	else
		return invarg("should be \"on\" or \"off\"", arg_name);

	addattr8(n, 1024, type, val);

	return 0;
}

static int bridge_slave_parse_opt(struct link_util *lu, int argc, char **argv,
				  struct nlmsghdr *n)
{
	__u8 state;
	__u16 priority;
	__u32 cost;

	while (argc > 0) {
		if (matches(*argv, "fdb_flush") == 0) {
			addattr(n, 1024, IFLA_BRPORT_FLUSH);
		} else if (matches(*argv, "state") == 0) {
			NEXT_ARG();
			if (get_u8(&state, *argv, 0))
				return invarg("state is invalid", *argv);
			addattr8(n, 1024, IFLA_BRPORT_STATE, state);
		} else if (matches(*argv, "priority") == 0) {
			NEXT_ARG();
			if (get_u16(&priority, *argv, 0))
				return invarg("priority is invalid", *argv);
			addattr16(n, 1024, IFLA_BRPORT_PRIORITY, priority);
		} else if (matches(*argv, "cost") == 0) {
			NEXT_ARG();
			if (get_u32(&cost, *argv, 0))
				return invarg("cost is invalid", *argv);
			addattr32(n, 1024, IFLA_BRPORT_COST, cost);
		} else if (matches(*argv, "hairpin") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("hairpin", *argv, n,
						  IFLA_BRPORT_MODE))
				return -1;
		} else if (matches(*argv, "guard") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("guard", *argv, n,
						  IFLA_BRPORT_GUARD))
				return -1;
		} else if (matches(*argv, "root_block") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("root_block", *argv, n,
						  IFLA_BRPORT_PROTECT))
				return -1;
		} else if (matches(*argv, "fastleave") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("fastleave", *argv, n,
						  IFLA_BRPORT_FAST_LEAVE))
				return -1;
		} else if (matches(*argv, "learning") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("learning", *argv, n,
						  IFLA_BRPORT_LEARNING))
				return -1;
		} else if (matches(*argv, "flood") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("flood", *argv, n,
						  IFLA_BRPORT_UNICAST_FLOOD))
				return -1;
		} else if (matches(*argv, "mcast_flood") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("mcast_flood", *argv, n,
						  IFLA_BRPORT_MCAST_FLOOD))
				return -1;
		} else if (matches(*argv, "proxy_arp") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("proxy_arp", *argv, n,
						  IFLA_BRPORT_PROXYARP))
				return -1;
		} else if (matches(*argv, "proxy_arp_wifi") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("proxy_arp_wifi", *argv, n,
						  IFLA_BRPORT_PROXYARP_WIFI))
				return -1;
		} else if (matches(*argv, "mcast_router") == 0) {
			__u8 mcast_router;

			NEXT_ARG();
			if (get_u8(&mcast_router, *argv, 0))
				return invarg("invalid mcast_router", *argv);
			addattr8(n, 1024, IFLA_BRPORT_MULTICAST_ROUTER,
				 mcast_router);
		} else if (matches(*argv, "mcast_fast_leave") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("mcast_fast_leave", *argv, n,
						  IFLA_BRPORT_FAST_LEAVE))
				return -1;
		} else if (matches(*argv, "neigh_suppress") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("neigh_suppress", *argv, n,
						  IFLA_BRPORT_NEIGH_SUPPRESS))
				return -1;
		} else if (matches(*argv, "group_fwd_mask") == 0) {
			__u16 mask;

			NEXT_ARG();
			if (get_u16(&mask, *argv, 0))
				return invarg("invalid group_fwd_mask", *argv);
			addattr16(n, 1024, IFLA_BRPORT_GROUP_FWD_MASK, mask);
		} else if (matches(*argv, "vlan_tunnel") == 0) {
			NEXT_ARG();
			if (bridge_slave_parse_on_off("vlan_tunnel", *argv, n,
						  IFLA_BRPORT_VLAN_TUNNEL))
				return -1;
		} else if (matches(*argv, "help") == 0) {
			explain();
			return -1;
		} else {
			fprintf(stderr, "bridge_slave: unknown option \"%s\"?\n",
				*argv);
			explain();
			return -1;
		}
		argc--, argv++;
	}

	return 0;
}

static void bridge_slave_print_help(struct link_util *lu, int argc, char **argv,
		FILE *f)
{
	print_explain(f);
}

struct link_util bridge_slave_link_util = {
	.id		= "bridge_slave",
	.maxattr	= IFLA_BRPORT_MAX,
	.print_opt	= bridge_slave_print_opt,
	.parse_opt	= bridge_slave_parse_opt,
	.print_help     = bridge_slave_print_help,
	.parse_ifla_xstats = bridge_parse_xstats,
	.print_ifla_xstats = bridge_print_xstats,
};
