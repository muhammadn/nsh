/*
 * Copyright (c) 2002
 *      Chris Cappuccio.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <tzfile.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if_vlan_var.h>
#include <net/if_ieee80211.h>
#include <arpa/inet.h>
#include <limits.h>
#include "ip.h"
#include "externs.h"
#include "bridge.h"

char *iftype(int int_type);
char *get_hwdaddr(int ifs, char *ifname);

static const struct {
	char *name;
	u_int8_t type;
} iftypes[] = {
	/* OpenBSD-specific types */
	{ "Packet Filter Logging",	IFT_PFLOG },
	{ "IPsec Loopback",		IFT_ENC },      
	{ "Generic Tunnel",		IFT_GIF },
	{ "IPv6-IPv4 TCP relay",	IFT_FAITH },
	{ "Ethernet Bridge",		IFT_BRIDGE },
	/* IANA-assigned types */
	{ "Token Ring",			IFT_ISO88025 },
	{ "ISO over IP",		IFT_EON },
	{ "XNS over IP",		IFT_NSIP },
	{ "X.25 to IMP",		IFT_X25DDN },
	{ "ATM Data Exchange Interface", IFT_ATMDXI },
	{ "ATM Logical",		IFT_ATMLOGICAL },
	{ "ATM Virtual",		IFT_ATMVIRTUAL },
	{ "ATM",			IFT_ATM },
	{ "Ethernet",			IFT_ETHER },
	{ "ARCNET",			IFT_ARCNET },
	{ "HDLC",			IFT_HDLC },
	{ "IEEE 802.1Q",		IFT_L2VLAN },
	{ "Virtual",			IFT_PROPVIRTUAL },
	{ "PPP",			IFT_PPP },
	{ "SLIP",			IFT_SLIP },
	{ "Loopback",			IFT_LOOP },
	{ "ISDN S",			IFT_ISDNS },
	{ "ISDN U",			IFT_ISDNU },
	{ "ISDN BRI",			IFT_ISDNBASIC },
	{ "ISDN PRI",			IFT_ISDNPRIMARY },
	{ "V.35",			IFT_V35 },
	{ "HSSI",			IFT_HSSI },
	{ "Network Tunnel",		IFT_TUNNEL },
	{ "Coffee Pot",			IFT_COFFEE },
	{ "IEEE 802.11",		IFT_IEEE80211 },
	{ "Unspecified",		IFT_OTHER },
};

int
show_int(char *ifname)
{
	struct if_nameindex *ifn_list, *ifnp;
	struct ifreq ifr;
	struct if_data if_data;
	struct sockaddr_in sin, sin2;
	struct timeval tv;
	struct vlanreq vreq;

	short tmp;
	int ifs, br, flags, days, hours, mins, pntd;
	int noaddr = 0;
	time_t c;
	char *type, *lladdr;

	u_long rate, bucket;
	char rate_str[64], bucket_str[64], tmp_str[4096], tmp_str2[1024];

	/*
	 * Show all interfaces when no ifname specified.
	 */
	if (ifname == 0) {
		if ((ifn_list = if_nameindex()) == NULL) {
			printf("%% show_int: if_nameindex failed\n");
			return 1;
		}
		for (ifnp = ifn_list; ifnp->if_name != NULL; ifnp++) {
			show_int(ifnp->if_name);
		}
		if_freenameindex(ifn_list);
		return(0);
	} else if (!is_valid_ifname(ifname)) {
		printf("%% interface %s not found\n", ifname);
		return(1);
	}

	if ((ifs = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("%% show_int: %s\n", strerror(errno));
		return(1);
	}

	if (!(br = is_bridge(ifs, (char *)ifname)))
		br = 0;

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	/*
	 * Show up/down status and last change time
	 */
	flags = get_ifflags(ifname, ifs);

	ifr.ifr_data = (caddr_t)&if_data;
	if (ioctl(ifs, SIOCGIFDATA, (caddr_t)&ifr) < 0) {
		printf("%% show_int: SIOCGIFDATA: %s\n", strerror(errno));
		close(ifs);
		return(1);
	}

	printf("%% %s\n", ifname);
	printf("  %s is %s", br ? "Bridge" : "Interface",
	    flags & IFF_UP ? "up" : "down");

	if (if_lastchange.tv_sec) {
		gettimeofday(&tv, (struct timezone *)0);
		c = difftime(tv.tv_sec, if_lastchange.tv_sec);
		days = c / SECSPERDAY;
		c %= SECSPERDAY;
		hours = c / SECSPERHOUR;
		c %= SECSPERHOUR;
		mins = c / SECSPERMIN;
		c %= SECSPERMIN;
		printf(" (last change ");
		if (days)
			printf("%id ", days);
		printf("%02i:%02i:%02i)", hours, mins, c);
	}

	printf(", protocol is %s", flags & IFF_RUNNING ? "up" : "down");
	printf("\n");

	type = iftype(if_type);

	printf("  Interface type %s", type);
	if (flags & IFF_BROADCAST)
		printf(" (Broadcast)");
	else if (flags & IFF_POINTOPOINT)
		printf(" (PointToPoint)");

	if ((lladdr = get_hwdaddr(ifs, ifname)) != NULL)
		printf(", hardware address %s", lladdr);
	printf("\n");

	media_status(ifs, ifname, "  Media type ");

	/*
	 * Display IP address and CIDR netmask
	 */
	if (ioctl(ifs, SIOCGIFADDR, (caddr_t)&ifr) < 0) {
		if (errno == EADDRNOTAVAIL) {
			noaddr = 1;
		} else {
			printf("%% show_int: SIOCGIFADDR: %s\n",
			    strerror(errno));
			close(ifs);
			return(1);
		}
	}
 
	if (!noaddr) {
		sin.sin_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;

		if (ioctl(ifs, SIOCGIFNETMASK, (caddr_t)&ifr) < 0)
			if (errno != EADDRNOTAVAIL) {
				printf("%% show_int: SIOCGIFNETMASK: %s\n",
				    strerror(errno));
				close(ifs);
				return(1);
			}
		sin2.sin_addr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;

		printf("  Internet address is %s\n",
		    (char *)netname(sin.sin_addr.s_addr, sin2.sin_addr.s_addr));
	}

	if (!br) {
		if (phys_status(ifs, ifname, tmp_str, tmp_str2, sizeof(tmp_str),
		    sizeof(tmp_str2)) > 0)
			printf("  Tunnel source %s destination %s\n",
			    tmp_str, tmp_str2);
		/*
		 * Display MTU, line rate, and ALTQ token rate info
		 * (if available)
		 */
		printf("  MTU %li bytes", if_mtu);
		if (if_baudrate)
			printf(", Line Rate %li %s\n",
			    MBPS(if_baudrate) ? MBPS(if_baudrate) :
			    if_baudrate / 1000,
			    MBPS(if_baudrate) ? "Mbps" : "Kbps");
		else
			printf("\n");
 
		rate = get_tbr(ifname, TBR_RATE);
		bucket = get_tbr(ifname, TBR_BUCKET);

		if(rate && bucket) {
			if (MBPS(rate))
				snprintf(rate_str, sizeof(rate_str),
				    "%.2f Mbps",
				    (double)rate/1000.0/1000.0);
			else
				snprintf(rate_str, sizeof(rate_str),
				    "%.2f Kbps",
				    (double)rate/1000.0);

			if (bucket < 10240)
				snprintf(bucket_str, sizeof(bucket_str),
				    "%lu bytes",
				    bucket);
			else
				snprintf(bucket_str, sizeof(bucket_str),
				    "%.2f Kbytes",
				    (double)bucket/1024.0);

			printf("  Token Rate %s, Bucket %s\n", rate_str,
			    bucket_str);
		}

		memset(&vreq, 0, sizeof(struct vlanreq));
		ifr.ifr_data = (caddr_t)&vreq;

		if (ioctl(ifs, SIOCGETVLAN, (caddr_t)&ifr) != -1)
			if(vreq.vlr_tag || (vreq.vlr_parent[0] != '\0'))
				printf("  802.1Q vlan tag %d, parent %s\n",
				    vreq.vlr_tag, vreq.vlr_parent[0] == '\0' ?
				    "<none>" : vreq.vlr_parent);
	}

	/*
	 * Display remaining info from if_data structure
	 */
	printf("  %lu packets input, %lu bytes, %lu errors, %lu drops\n",
	    if_ipackets, if_ibytes, if_ierrors, if_iqdrops);
	printf("  %lu packets output, %lu bytes, %lu errors, %lu unsupported\n",
	    if_opackets, if_obytes, if_oerrors, if_noproto);
	if (if_ibytes && if_ipackets && (if_ibytes / if_ipackets) >= ETHERMIN) {
		/* < ETHERMIN means byte counter probably rolled over */
		printf("  %lu avg input size", if_ibytes / if_ipackets);
		pntd = 1;
	} else
		pntd = 0;
	if (if_obytes && if_opackets && (if_obytes / if_opackets) >= ETHERMIN) {
		/* < ETHERMIN means byte counter probably rolled over */
		printf("%s%lu avg output size", pntd ? ", " : "  ",
		    if_obytes / if_opackets);
		pntd = 1;
	}
	if (pntd)
		printf("\n");

	switch(if_type) {
	/*
	 * These appear to be the only interface types to increase collision
	 * count in the OpenBSD 3.2 kernel.
	 */
	case IFT_ETHER:
	case IFT_SLIP:
	case IFT_PROPVIRTUAL:
	case IFT_IEEE80211:
		printf("  %lu collisions\n", if_collisions);
		break;
	default:
		break;
	}

	if(verbose) {
		if (flags) {
			printf("  Flags:\n    ");
			bprintf(stdout, flags, ifnetflags);
			printf("\n");
		}
		if (br) {
			if ((tmp = bridge_list(ifs, ifname, "    ", tmp_str,
			    sizeof(tmp_str), SHOW_STPSTATE))) {
				printf("  STP member state%s:\n", tmp > 1 ?
				    "s" : "");
				printf("%s", tmp_str);
			}
			bridge_addrs(ifs, ifname, "  ", "    ");
		}
		if (get_nwinfo(ifname, tmp_str, sizeof(tmp_str), NWID) != NULL)
		{
			printf("  IEEE 802.11:\n");
			printf("    network id %s\n", tmp_str);
			if (get_nwinfo(ifname, tmp_str, sizeof(tmp_str), NWKEY)
			    != NULL)
				printf("    network key %s\n", tmp_str);
			if ((tmp = get_nwpowersave(ifs, (char *)ifname))
			    != NULL)
				printf("    powersaving (%d ms)\n", tmp);
		}
		media_supported(ifs, ifname, "  ", "    ");
	}

	close(ifs);
	return(0);
}

u_int32_t
in4_netaddr(u_int32_t addr, u_int32_t mask)
{
	u_int32_t net;

	net = ntohl(addr) & ntohl(mask);

	return (net);
}

u_int32_t
in4_brdaddr(u_int32_t addr, u_int32_t mask)
{
	u_int32_t net, bcast;

	net = in4_netaddr(addr, mask);
	bcast = net | ~ntohl(mask);

	return(bcast);
}

char *
get_hwdaddr(int ifs, char *ifname)
{
	int i, found = 0;
	char *val = NULL;
	struct ifaddrs *ifap, *ifa;
	struct ether_addr *ea;
	struct sockaddr_dl *sdl;

	if (getifaddrs(&ifap) != 0) {
		printf("%% get_hwdaddr: getifaddrs: %s\n", strerror(errno));
		return(NULL);
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
		if (ifa->ifa_addr->sa_family == AF_LINK &&
		    (strcmp(ifname, ifa->ifa_name) == 0)) {
			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			found++;
			break;
		}

	if (found && sdl && sdl->sdl_alen)
		switch(sdl->sdl_type) {
		case IFT_ETHER:
		case IFT_IEEE80211:
			ea = (struct ether_addr *)LLADDR(sdl);
			val = ether_ntoa(ea);
			for (found = 0, i = 0; i < ETHER_ADDR_LEN; i++)
				if (ea->ether_addr_octet[i] == 0)
					found++;
			if (found == ETHER_ADDR_LEN)
				val = NULL;
			break;
		default:
			val = NULL;
			break;
		}

	freeifaddrs(ifap);

	return(val);
}

char *
iftype(int int_type)
{
	int i;

	for (i = 0; i < sizeof(iftypes) / sizeof(iftypes[0]); i++)
		if (int_type == iftypes[i].type)
			return(iftypes[i].name);

	return("Unknown");
}

int 
get_ifdata(char *ifname, int type)
{
	int ifs, value = 0;
	struct ifreq ifr;
	struct if_data if_data;

	if (type == IFDATA_MTU)
		value = 576;			 /* default MTU */
	/*
	 * We don't set a default for IFDATA_BAUDRATE because we detect
	 * a failure at 0
	 */

	if ((ifs = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return (value);
	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)&if_data;
	if (ioctl(ifs, SIOCGIFDATA, (caddr_t)&ifr) == 0) {
		if (type == IFDATA_MTU)
			value = if_mtu;
		else if (type == IFDATA_BAUDRATE)
			value = if_baudrate;
	}
	close(ifs);
	return (value);
}

/*
 * returns 1 if one valid, matching interface name is found
 * returns 0 for no valid or failure
 */
int
is_valid_ifname(char *ifname)
{
	struct if_nameindex *ifn_list, *ifnp;
	int count = 0;

	if ((ifn_list = if_nameindex()) == NULL) {
		printf("%% is_valid_ifname: if_nameindex failed\n");
		return(0);
	}
	for (ifnp = ifn_list; ifnp->if_name != NULL; ifnp++) {
		if (strcasecmp(ifname, ifnp->if_name) == 0)
			count++;
	}
	if_freenameindex(ifn_list);

	if (count == 1)
		return(1);
	else
		return(0);
}

int
get_ifflags(char *ifname, int ifs)
{
	int flags;
	struct ifreq ifr;

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	if (ioctl(ifs, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
		printf("%% get_ifflags: SIOCGIFFLAGS: %s\n", strerror(errno));
		flags = 0;
	} else
		flags = ifr.ifr_flags;
	return(flags);
}

int
set_ifflags(char *ifname, int ifs, int flags)
{
	struct ifreq ifr;

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	ifr.ifr_flags = flags;

	if (ioctl(ifs, SIOCSIFFLAGS, (caddr_t)&ifr) < 0) {
		printf("%% get_ifflags: SIOCSIFFLAGS: %s\n", strerror(errno));
	}

        return(0);
}

int
intip(char *ifname, int ifs, int argc, char **argv)
{
	int set, alias, flags, argcmax;
	ip_t ip;
	struct in_addr destbcast;
	struct ifaliasreq addreq, ridreq;
	struct sockaddr_in *sin;
	char  *msg, *cmdname;

	memset(&addreq, 0, sizeof(addreq));
	memset(&ridreq, 0, sizeof(ridreq));

	if (NO_ARG(argv[0])) {
		set = 0;
		argc--;
		argv++;
	} else
		set = 1;

	/*
	 * We use this function for ip and alias setup since they are
	 * the same thing.
	 */
	if (CMP_ARG(argv[0], "a")) {
		alias = 1;
		cmdname = "alias";
	} else if (CMP_ARG(argv[0], "i")) {
		alias = 0;
		cmdname = "ip";
	} else {
		printf("%% intip: Internal error\n");
		return 0;
	}

	argc--;
	argv++;

	flags = get_ifflags(ifname, ifs);
	if (flags & IFF_POINTOPOINT) {
		argcmax = 2;
		msg = "destination";
	} else if (flags & IFF_BROADCAST) {
		argcmax = 2;
		msg = "broadcast";
	} else {
		argcmax = 1;
		msg = NULL;
	}

	if (argc < 1 || argc > argcmax) {
		printf("%% %s <address>/<bits> %s%s%s\n", cmdname,
		    msg ? "[" : "", msg ? msg : "", msg ? "]" : "");
		printf("%% %s <address>/<netmask> %s%s%s\n", cmdname,
		    msg ? "[" : "", msg ? msg : "", msg ? "]" : "");
		printf("%% no %s <address>[/bits]\n", cmdname);
		printf("%% no %s <address>[/netmask]\n", cmdname);
		return(0);
	}

	ip = parse_ip(argv[0], NO_NETMASK);
	if (ip.bitlen == -1) {
		printf("%% Netmask not specified\n");
		return(0);
	}
	
	if (argc == 2)
		if (!inet_aton(argv[1], &destbcast)) {
			printf("%% Invalid %s address\n", msg);
			return(0);
		}
	
	strlcpy(addreq.ifra_name, ifname, sizeof(addreq.ifra_name));
	strlcpy(ridreq.ifra_name, ifname, sizeof(ridreq.ifra_name));

	if (!set) {
		sin = (struct sockaddr_in *)&ridreq.ifra_addr;
		sin->sin_len = sizeof(ridreq.ifra_addr);
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = ip.addr.sin.s_addr;
	}

	if (!alias || !set) {
		/*
		 * Here we remove the top IP on the interface before we
		 * might add another one, or we delete the specified IP.
		 */
		if (ioctl(ifs, SIOCDIFADDR, &ridreq) < 0)
			if (!set)
				printf("%% intip: SIOCDIFADDR: %s\n",
				    strerror(errno));
	}

	if (set) {
		sin = (struct sockaddr_in *)&addreq.ifra_addr;
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(addreq.ifra_addr);
		sin->sin_addr.s_addr = ip.addr.sin.s_addr;
		sin = (struct sockaddr_in *)&addreq.ifra_mask;
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(addreq.ifra_mask);
		sin->sin_addr.s_addr = htonl(0xffffffff << (32 - ip.bitlen));
		if (argc == 2) {
			sin = (struct sockaddr_in *)&addreq.ifra_dstaddr;
			sin->sin_family = AF_INET;
			sin->sin_len = sizeof(addreq.ifra_dstaddr);
			sin->sin_addr.s_addr = destbcast.s_addr;
		}
		if (ioctl(ifs, SIOCAIFADDR, &addreq) < 0)
			printf("%% intip: SIOCAIFADDR: %s\n", strerror(errno));
	}

	return(0);
}

int
intmtu(char *ifname, int ifs, int argc, char **argv)
{
	struct ifreq ifr;
	int set;
	char *ep;

	if (NO_ARG(argv[0])) {
		set = 0;
		argc--;
		argv++;
	} else
		set = 1;

	argc--;
	argv++;

	if ((!set && argc > 1) || (set && argc != 1)) {
		printf("%% mtu <mtu>\n");
		printf("%% no mtu [mtu]\n");
		return(0);
	}

	if (set)
		ifr.ifr_mtu = strtoul(argv[0], &ep, 10);
	else
		ifr.ifr_mtu = default_mtu(ifname);

	if (!ep || *ep) {
		printf("%% Invalid MTU\n");
		return(0);
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(ifs, SIOCSIFMTU, (caddr_t)&ifr) < 0)
		printf("%% intmtu: SIOCSIFMTU: %s\n", strerror(errno));

	return(0);
}

int
intmetric(char *ifname, int ifs, int argc, char **argv)
{
	struct ifreq ifr;
	int set;
	char *ep = NULL;

	if (NO_ARG(argv[0])) {
		set = 0;
		argc--;
		argv++;
	} else
		set = 1;

	argc--;
	argv++;

	if ((!set && argc > 1) || (set && argc != 1)) {
		printf("%% metric <metric>\n");
		printf("%% no metric [metric]\n");
		return(0);
	}

	if (set)
		ifr.ifr_metric = strtoul(argv[0], &ep, 10);
	else
		ifr.ifr_metric = 0;

	if (!ep || *ep) {
		printf("%% Invalid metric\n");
		return(0);
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(ifs, SIOCSIFMETRIC, (caddr_t)&ifr) < 0)
		printf("%% intmetric: SIOCSIFMETRIC: %s\n", strerror(errno));

	return(0);
}

int
intvlan(char *ifname, int ifs, int argc, char **argv)
{
	struct ifreq ifr;
	struct vlanreq vreq;
	int set;

	if (NO_ARG(argv[0])) {
		set = 0;
		argc--;
		argv++;
	} else
		set = 1;

	argc--;
	argv++;

	memset(&vreq, 0, sizeof(vreq));

	if ((set && argc != 2) || (!set && argc > 2)) {
		printf("%% vlan <tag> <parent interface>\n");
		printf("%% no vlan [tag] [parent interface]\n");
		return(0);
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t)&vreq;

	if (ioctl(ifs, SIOCGETVLAN, (caddr_t)&ifr) == -1) {
		if (errno == EINVAL)
			printf("%% This interface does not support vlan"
			    " tagging\n");
		else
			printf("%% intvlan: SIOCGETVLAN: %s\n",
			    strerror(errno));
		return(0);
	}

	if (set) {
		if (!is_valid_ifname(argv[1]) || is_bridge(ifs, argv[1])) {
			printf("%% Invalid vlan parent %s\n", argv[1]);
			return(0);
		}
		strlcpy(vreq.vlr_parent, argv[1], sizeof(vreq.vlr_parent));
		vreq.vlr_tag = atoi(argv[0]);
		if (vreq.vlr_tag != EVL_VLANOFTAG(vreq.vlr_tag)) {
			printf("%% Invalid vlan tag %s\n", argv[0]);
			return(0);
		}
	} else {
		memset(&vreq.vlr_parent, 0, sizeof(vreq.vlr_parent));
		vreq.vlr_tag = 0;
	}

	if (ioctl(ifs, SIOCSETVLAN, (caddr_t)&ifr) == -1) {
		if (errno == EBUSY) {
			printf("%% Please disconnect the current vlan parent"
			    " before setting a new one\n");
		} else {
			printf("%% intvlan: SIOCSETVLAN: %s\n",
			    strerror(errno));
		}
	}

	return(0);
}

int
intflags(char *ifname, int ifs, int argc, char **argv)
{
	int set, value, flags;

	if (NO_ARG(argv[0])) {
		set = 0;
		argv++;
		argc--;
	} else
		set = 1;

	if (CMP_ARG(argv[0], "d")) {
		/* debug */
		value = IFF_DEBUG;
	} else if (CMP_ARG(argv[0], "s")) {
		/* shutdown */
		value = -IFF_UP;
	} else if (CMP_ARG(argv[0], "a")) {
		/* arp */
		value = -IFF_NOARP;
	} else {
		printf("%% intflags: Internal error\n");
		return(0);
	}

	flags = get_ifflags(ifname, ifs);
	if (value < 0) {
		/*
		 * Idea from ifconfig.  If value is negative then
		 * we just reverse the operation. (e.g. 'shutdown' is
		 * the opposite of the IFF_UP flag)
		 */
		if (set) {
			value = -value;
			flags &= ~value;
		} else {
			value = -value;
			flags |= value;
		}
	} else if (value > 0) {
		if (set)
			flags |= value;
		else
			flags &= ~value;
	} else {
		printf("%% intflags: value internal error\n");
	}
	set_ifflags(ifname, ifs, flags);
	return(0);
}

int
intlink(char *ifname, int ifs, int argc, char **argv)
{
	int set, i, flags, value;

	if (NO_ARG(argv[0])) {
		set = 0;
		argv++;
		argc--;
	} else
		set = 1;

	argv++;
	argc--;

	if ((set && argc < 1) || argc > 3) {
		printf("%% link <012>\n");
		printf("%% no link [012]\n");
		return(0);
	}

	flags = get_ifflags(ifname, ifs);

	if (!set && argc == 0) {
		/*
		 * just 'no link' was specified.  so we remove all flags
		 */
		flags &= ~IFF_LINK0 & ~IFF_LINK1 & ~IFF_LINK2;
	} else 
	for (i = 0; i < argc; i++) {
		int a;

		a = strlen(argv[i]);
		if (a > 1 || a != strspn(argv[i], "012")) {
			printf("%% Invalid argument: %s\n", argv[i]);
			return(0);
		}

		a = atoi(argv[i]);
		switch(a) {
		case 0:
			value = IFF_LINK0;
			break;
		case 1:
			value = IFF_LINK1;
			break;
		case 2:
			value = IFF_LINK2;
			break;
		}

		if (set)
			flags |= value;
		else
			flags &= ~value;
	}

	set_ifflags(ifname, ifs, flags);

	return(0);
}

int
intnwid(char *ifname, int ifs, int argc, char **argv)
{
	struct ieee80211_nwid nwid;
	struct ifreq ifr;
	int set, len;

	if (NO_ARG(argv[0])) {
		set = 0;
		argv++;
		argc--;
	} else
		set = 1;

	argv++;
	argc--;

	if ((set && argc != 1) || (!set && argc > 1)) {
		printf("%% nwid <nwid>\n");
		printf("%% no nwid [nwid]\n");
		return(0);
	}

	len = sizeof(nwid.i_nwid);

	if (set) {
		if (get_string(argv[0], NULL, nwid.i_nwid, &len) == NULL) {
			printf("%% intnwid: bad input\n");
			return(0);
		}
	} else
		len = 0; /* nwid "" */

	nwid.i_len = len;
	ifr.ifr_data = (caddr_t)&nwid;
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	if (ioctl(ifs, SIOCS80211NWID, (caddr_t)&ifr) < 0)
		printf("%% intnwid: SIOCS80211NWID: %s\n", strerror(errno));

	return(0);
}

int
intpowersave(char *ifname, int ifs, int argc, char **argv)
{
	struct ieee80211_power power;
	int  set;

	if (NO_ARG(argv[0])) {
		set = 0;
		argv++;
		argc--;
	} else
		set = 1;

	argv++;
	argc--;

 	if (argc > 1) {
		printf("%% powersave [milisec]\n");
		printf("%% no powersave [milisec]\n");
	}

	strlcpy(power.i_name, ifname, sizeof(power.i_name));

	if (ioctl(ifs, SIOCG80211POWER, (caddr_t)&power) == -1) {
		printf("%% intpowersave: SIOCG80211POWER: %s\n",
		    strerror(errno));
		return(0);
	}

	if (argc == 1)
		power.i_maxsleep = atoi(argv[0]);
	else
		power.i_maxsleep = DEFAULT_POWERSAVE;
	power.i_enabled = set;

	if (ioctl(ifs, SIOCS80211POWER, (caddr_t)&power) == -1) {
		printf("%% intpowersave: SIOCS80211POWER: %s\n",
		    strerror(errno));
		return(0);
	}

	return(0);
}
