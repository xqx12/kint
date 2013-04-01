#include "stdlib.h"

#define AF_MAX          40

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#ifdef __GNUC__
__extension__ typedef __signed__ long long __s64;
__extension__ typedef unsigned long long __u64;
#else
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif


#ifdef __CHECKER__
#define __bitwise__ __attribute__((bitwise))
#else
#define __bitwise__
#endif
#ifdef __CHECK_ENDIAN__
#define __bitwise __bitwise__
#else
#define __bitwise
#endif

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;

typedef __u16 __bitwise __sum16;
typedef __u32 __bitwise __wsum;

static const struct sock_diag_handler *sock_diag_handlers[AF_MAX];

struct sk_buff {
	/* These two members must be first. */
	struct sk_buff		*next;
	struct sk_buff		*prev;
};

struct nlmsghdr {
	__u32		nlmsg_len;	/* Length of message including header */
	__u16		nlmsg_type;	/* Message content */
	__u16		nlmsg_flags;	/* Additional flags */
	__u32		nlmsg_seq;	/* Sequence number */
	__u32		nlmsg_pid;	/* Sending process port ID */
};

struct sock_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
};

#define NLMSG_HDRLEN	 ((int) NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define	EINVAL		22
#define	ENOSYS		38
#define NETLINK_SOCK_DIAG	4
#define PF_NETLINK		16
#define	ENOENT		 2

void *nlmsg_data(const struct nlmsghdr *nlh)
{
	return (unsigned char *) nlh + NLMSG_HDRLEN;
}

int nlmsg_len(const struct nlmsghdr *nlh)
{
	return nlh->nlmsg_len - NLMSG_HDRLEN;
}

static inline int request_module(const char *name, ...) { return -ENOSYS; }

struct sock_diag_handler *sock_diag_lock_handler(int family)
{
	if (sock_diag_handlers[family] == NULL)
		request_module("net-pf-%d-proto-%d-type-%d", PF_NETLINK,
				NETLINK_SOCK_DIAG, family);

//	mutex_lock(&sock_diag_table_mutex);
	return sock_diag_handlers[family];
}

int __sock_diag_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int err;
	struct sock_diag_req *req = nlmsg_data(nlh);
	const struct sock_diag_handler *hndl;

	if (nlmsg_len(nlh) < sizeof(*req))
		return -EINVAL;

	hndl = sock_diag_lock_handler(req->sdiag_family);
	if (hndl == NULL)
		err = -ENOENT;
	else
		err = -1;
//	sock_diag_unlock_handler(hndl);

	return err;
}
