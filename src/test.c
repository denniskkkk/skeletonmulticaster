/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>
#include <event2/event.h>
#include <arpa/inet.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

typedef void (*t_sighup_handler)(void);
static t_sighup_handler user_sighup_handler = NULL;
static int got_sighup = 0;
int pid;
int daemonized = 0;

pthread_t thread[2];
pthread_attr_t pra_attr[2];
pthread_mutex_t prd_mutex[2];
pthread_cond_t prd_cond[2];

#define MAX_LINE 16384

#define MAX_HOSTNAME_LEN 1024

struct in_addr localip;
int pid;

struct fd_state {
	char buffer[MAX_LINE];
	struct in_addr mcast_ip;
	u_int16_t mcast_port;
	struct event *read_event;
};

void do_read(evutil_socket_t fd, short events, void *arg);

void getlocalip() {
	char hostname[MAX_HOSTNAME_LEN];
	struct hostent* hostinfo;

	/* lookup local hostname */
	gethostname(hostname, MAX_HOSTNAME_LEN);

	printf("Localhost is %s\n", hostname);

	/* use gethostbyname to get host's IP address */
	if ((hostinfo = gethostbyname(hostname)) == NULL) {
		perror("gethostbyname() failed");
	}
	localip.s_addr = *((unsigned long *) hostinfo->h_addr_list[0]);
	printf("interface# %d\n", sizeof(hostinfo->h_addr_list) / sizeof(hostinfo));
	printf("address in hex 0x%08x\n", localip.s_addr);
	printf("ip %s\n", inet_ntoa(localip));

	//pid = getpid();
	//printf("pid %d\n");
}

struct fd_state * alloc_fd_state(struct event_base *base, evutil_socket_t fd) {
	struct fd_state *state = malloc(sizeof(struct fd_state));
	if (!state)
		return NULL;
	state->read_event = event_new(base, fd, EV_READ | EV_PERSIST, do_read,
			state);
	if (!state->read_event) {
		free(state);
		return NULL;
	}
	return state;
}

void printhex(unsigned char *tmp2, unsigned int readed) {
	unsigned int z;
	if (readed > 0) {
		printf("readed %d, ", readed);
		for (z = 0; z < readed; z++) {
			printf(":%02X:", tmp2[z]);
		}
		printf("\n");
	}
}

void free_fd_state(struct fd_state *state) {
	event_free(state->read_event);
	free(state);
}

int mcast_channel_fd_new(struct event_base *base, struct in_addr mcast,
		struct in_addr local, u_int16_t mcast_port) {
	evutil_socket_t nsock;
	struct sockaddr_in sin;
	int loop = 1;
	int flag = 1;
	struct ip_mreq mreq;
	struct fd_state *state;
	mreq.imr_multiaddr = mcast;
	mreq.imr_interface = local;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0;
	sin.sin_port = mcast_port;
	nsock = socket(AF_INET, SOCK_DGRAM, 0); // or IPPROTO_UDP = 0
	if ((setsockopt(nsock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)))
			< 0) {
		perror("setsockopt() failed");
		exit(1);
	}

	evutil_make_socket_nonblocking(nsock);

	if (bind(nsock, (struct sockaddr*) &sin, sizeof(sin)) < 0) {
		perror("bind");
		return -1;
	}
	if (setsockopt(nsock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop))
			< 0) {
		perror("setsocket():IP MULTICAST_LOOP");
		return -1;
	}
	if (setsockopt(nsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))
			< 0) {
		printf("%s setsockopt():IP ADD MEMBURSHIP\n", strerror(errno));
		return -1;
	}
	state = alloc_fd_state(base, nsock);
	assert(state);
	/*XXX err*/
	state->mcast_ip = mreq.imr_multiaddr;
	state->mcast_port = mcast_port;
	event_add(state->read_event, NULL);
	return 0;
}

void do_read(evutil_socket_t fd, short events, void *arg) {
	struct fd_state *state = arg;
	char buf[65536];
	int i;
	struct sockaddr_in src_addr;
	socklen_t len;
	ssize_t result;
	result = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*) &src_addr,
			&len);
	fprintf(stderr, "multicast packet received ");
	fprintf(stderr, "recv %zd from %s:%d", result, inet_ntoa(src_addr.sin_addr),
			ntohs(src_addr.sin_port));
	fprintf(stderr, " with mcast_channel %s:%d\n", inet_ntoa(state->mcast_ip),
			ntohs(state->mcast_port));
	printf("local address in hex 0x%08x\n", localip.s_addr);
//	printf ("address size %d\n", sizeof (src_addr.sin_addr.s_addr));
	printf("trans address in hex 0x%08x\n", src_addr.sin_addr.s_addr);

	if (localip.s_addr == src_addr.sin_addr.s_addr) {
		printf(">> this is the multicast data transmitter!!! \n");
	} else
		printf(">> data transmit by others ...\n");
	//printhex(buf, result);

	if (result == 0) {
		free_fd_state(state);
	} else if (result < 0) {
		if (errno == EAGAIN) // XXXX use evutil macro
		{
			return;
		}
		perror("recv");
		free_fd_state(state);
	}
}

void *mtransmitter() {
	int sock;
	struct sockaddr_in multicastAddr;
	char *multicastIP;
	unsigned short multicastPort;
	char sendString[60000];
	unsigned char multicastTTL;
	unsigned int sendStringLen;
	struct sockaddr_in echoServAddr;
	unsigned int i;

	multicastIP = "231.1.1.20";
	multicastPort = 5566;
	multicastTTL = 1;
	sendStringLen = sizeof(sendString) / sizeof(char);
	for (i = 0; i < sendStringLen; i++) {
		sendString[i] = i % 256;
	}

	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		perror("socket() failed");

   /* memset(&echoServAddr, 0, sizeof(echoServAddr));
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    echoServAddr.sin_port = htons(echoServPort);

    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");
	*/

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &multicastTTL,
			sizeof(multicastTTL)) < 0)
		perror("setsockopt() failed");

	memset(&multicastAddr, 0, sizeof(multicastAddr));
	multicastAddr.sin_family = AF_INET;
	multicastAddr.sin_addr.s_addr = inet_addr(multicastIP);
	multicastAddr.sin_port = htons(multicastPort);
	for (;;) {

		if (sendto(sock, sendString, sendStringLen, 0,
				(struct sockaddr *) &multicastAddr, sizeof(multicastAddr))
				!= sendStringLen)
			perror("sendto() sent a different number of bytes than expected");
	      //	usleep(50); // 10ms delay
	}
	return 0;
}

void *mreceive() {
	struct in_addr mcast; // remote address
	//struct in_addr local; // local interface address
	struct event_base *base;
	getlocalip();
	printf("---\n");
	//localip.s_addr = inet_addr("192.168.1.20");
	base = event_base_new();
	if (!base)
		return; /*XXXerr*/
	mcast.s_addr = inet_addr("231.1.1.20"); // 230.1.1.1:5405
	if (mcast_channel_fd_new(base, mcast, localip, htons(5566)) < 0)
		return;
	//   mcast.s_addr = inet_addr("224.0.0.252");
	//   if (mcast_channel_fd_new(base, mcast, local, htons(5252)) < 0)
	//       return;
	event_base_dispatch(base);
	return;
}

void set_sighup_handler(t_sighup_handler handler) {
	user_sighup_handler = handler;
	printf("exit daemon\n");
	exit(0);
}

void sighup_handler(int sig) {
	got_sighup = 1;
	printf("server stop !!!\n");

}

void setup_sighup(void) {
	struct sigaction act;
	int err;

	act.sa_handler = sighup_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	err = sigaction(SIGHUP, &act, NULL);
	if (err) {
		perror("sigaction");
	}
}

int main(int c, char **v) {
	int err;
	int threadgroup;
	setvbuf(stdout, NULL, _IONBF, 0);
	setup_sighup();
	printf("start threads!!!\n");
	pthread_attr_init(&pra_attr[0]);
	pthread_attr_init(&pra_attr[1]);
	if ((err = pthread_create(&thread[0], &pra_attr[0], mreceive, NULL))) {
		printf("create thread 1 without scheduling\n");
	}
	if ((err = pthread_create(&thread[1], &pra_attr[1], mtransmitter, NULL))) {
		printf("create thread 1 without scheduling\n");
	}
	printf("thread create !!!\n");
	pthread_join(thread[0], &threadgroup);
	pthread_join(thread[1], &threadgroup);
	pthread_attr_destroy(&pra_attr[0]);
	pthread_attr_destroy(&pra_attr[1]);
	printf("thread destory !!!\n");
	pthread_exit(NULL);
	printf("exit\n");
	return 0;
}

