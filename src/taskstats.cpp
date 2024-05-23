
/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2023  Boian Bonev
Copyright (C) 2023 Costis Contopoulos

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/taskstats.h>
#include <linux/genetlink.h>

#include "taskstats.h"

/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */
#define GENLMSG_DATA(glh)	   ((void *)((char*)NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)	(NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)			((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)		(len - NLA_HDRLEN)

#define MAX_MSG_SIZE 1024
#define MAX_SEND_FAILURES 5

struct msgtemplate {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char buf[MAX_MSG_SIZE];
};

static int nl_sock=-1;
static int nl_fam_id=0;

static int send_cmd(int sock_fd,__u16 nlmsg_type,__u32 nlmsg_pid,__u8 genl_cmd,__u16 nla_type,void *nla_data,int nla_len) {
	struct nlattr *na;
	struct sockaddr_nl nladdr;
	int r,buflen;
	char *buf;

	struct msgtemplate msg;

	memset(&msg,0,sizeof msg);
	// make cppcheck happier; hopefully the optimizer should remove this
	memset(msg.buf,0,sizeof msg.buf);

	msg.n.nlmsg_len=NLMSG_LENGTH(GENL_HDRLEN);
	msg.n.nlmsg_type=nlmsg_type;
	msg.n.nlmsg_flags=NLM_F_REQUEST;
	msg.n.nlmsg_seq=0;
	msg.n.nlmsg_pid=nlmsg_pid;
	msg.g.cmd=genl_cmd;
	msg.g.version=TASKSTATS_GENL_VERSION;

	na=(struct nlattr *)GENLMSG_DATA(&msg);
	na->nla_type=nla_type;
	na->nla_len=nla_len+NLA_HDRLEN;

	memcpy(NLA_DATA(na),nla_data,nla_len);
	msg.n.nlmsg_len+=NLMSG_ALIGN(na->nla_len);

	buf=(char *)&msg;
	buflen=msg.n.nlmsg_len;
	memset(&nladdr,0,sizeof nladdr);
	nladdr.nl_family=AF_NETLINK;
	while ((r=sendto(sock_fd,buf,buflen,0,(struct sockaddr *)&nladdr,sizeof nladdr))<buflen) {
		if (r>0) {
			buf+=r;
			buflen-=r;
		} else
			if (errno!=EAGAIN)
				return -1;
	}
	return 0;
}

static int get_family_id(int sock_fd) {
	struct msgtemplate answ;
	static char name[256];
	struct nlattr *na;
	ssize_t rep_len;
	int id=0;

	strcpy(name,TASKSTATS_GENL_NAME);
	if (send_cmd(sock_fd,GENL_ID_CTRL,getpid(),CTRL_CMD_GETFAMILY,CTRL_ATTR_FAMILY_NAME,(void *)name,strlen(TASKSTATS_GENL_NAME)+1))
		return 0;

	rep_len=recv(sock_fd,&answ,sizeof answ,0);
	if (rep_len<0||!NLMSG_OK((&answ.n),(size_t)rep_len)||answ.n.nlmsg_type==NLMSG_ERROR)
		return 0;

	na=(struct nlattr *)GENLMSG_DATA(&answ);
	na=(struct nlattr *)((char *)na+NLA_ALIGN(na->nla_len));
	if (na->nla_type==CTRL_ATTR_FAMILY_ID)
		id=*(__u16 *)NLA_DATA(na);

	return id;
}

nl_rc taskstat::nl_init(void) {
	struct sockaddr_nl addr;
	int sock_fd=socket(PF_NETLINK,SOCK_RAW,NETLINK_GENERIC);

	atexit(nl_fini);

	if (sock_fd<0)
		goto error;

	memset(&addr,0,sizeof addr);
	addr.nl_family=AF_NETLINK;

	if (bind(sock_fd,(struct sockaddr *)&addr,sizeof addr)<0)
		goto error;

	nl_sock=sock_fd;
	nl_fam_id=get_family_id(sock_fd);
	if (!nl_fam_id) {
		fprintf(stderr,"nl_init: couldn't get netlink family id\n");
        return CRITICAL_FAIL;
	}

	return SUCCESS;

error:
    nl_fini();

	fprintf(stderr,"nl_init: %s\n",strerror(errno));
    return CRITICAL_FAIL;
}

nl_rc taskstat::nl_taskstats_info(pid_t tid, taskstats* ts_response) {
    static short send_failures_counter = 0;

	if (nl_sock<0) {
		fprintf(stderr,"nl_taskstats_info: nl_sock is %d",nl_sock);
        nl_fini();
        return CRITICAL_FAIL;
	}
	if (nl_fam_id==0) { // this will cause recv to wait forever
		fprintf(stderr,"nl_taskstats_info: nl_fam_id is 0");
        nl_fini();
        return CRITICAL_FAIL;
	}

	if (send_cmd(nl_sock,nl_fam_id,tid,TASKSTATS_CMD_GET,TASKSTATS_CMD_ATTR_PID,&tid,sizeof tid)) {
		fprintf(stderr,"nl_taskstats_info: %s\n",strerror(errno));
        if (++send_failures_counter > MAX_SEND_FAILURES) {
            send_failures_counter = 0;
		    fprintf(stderr,"Successive send failures");
            nl_fini();
            return CRITICAL_FAIL;
	    }
        else
		    return FAIL;
    }


	struct msgtemplate msg;
	ssize_t rv=recv(nl_sock,&msg,sizeof msg,0);

	if (rv<0||!NLMSG_OK((&msg.n),(size_t)rv)||msg.n.nlmsg_type==NLMSG_ERROR) {
		struct nlmsgerr *err = static_cast<nlmsgerr*>NLMSG_DATA(&msg);

		if (err->error!=-ESRCH)
			fprintf(stderr,"fatal reply error, %d\n",err->error);
		return FAIL;
	}

	rv=GENLMSG_PAYLOAD(&msg.n);

	struct nlattr *na=(struct nlattr *)GENLMSG_DATA(&msg);
	int len=0;

	while (len<rv) {
		len+=NLA_ALIGN(na->nla_len);

		if (na->nla_type==TASKSTATS_TYPE_AGGR_TGID||na->nla_type==TASKSTATS_TYPE_AGGR_PID) {
			int aggr_len=NLA_PAYLOAD(na->nla_len);
			int len2=0;

			na=(struct nlattr *)NLA_DATA(na);
			while (len2<aggr_len) {
				if (na->nla_type==TASKSTATS_TYPE_STATS) {
					memcpy(ts_response,
                        static_cast<taskstats*>NLA_DATA(na),
                        sizeof (taskstats));

				}
				len2+=NLA_ALIGN(na->nla_len);
				na=(struct nlattr *)((char *)na+len2);
			}
		}
		na=(struct nlattr *)((char *)GENLMSG_DATA(&msg)+len);
	}

	return SUCCESS;

}

bool taskstat::is_socket_alive() { return nl_sock > -1; }

void taskstat::dump_ts(taskstats& ts) {

    #define PRT(field) fprintf(stderr, #field ": %lld\n", ts.field);
    #define DELAY(field) fprintf(stderr, #field ": %.3f ms\n", ts.field/(float)1e6);
    PRT(read_bytes);
    PRT(write_bytes);
    DELAY(swapin_delay_total);
    DELAY(blkio_delay_total)
    DELAY(cpu_delay_total)
    //PRT(ac_uid);
    #undef PRT
    #undef DELAY
}

void taskstat::nl_fini(void) {
	if (nl_sock>-1)
		close(nl_sock);
    nl_sock=-1;
}
