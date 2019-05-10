#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <getopt.h>

// Function rename
#define get_socket_error() (errno)
#define closesocket(s) close(s)
#define Sleep(x) usleep(x * 1000)

// Variable rename
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define SD_SEND SHUT_WR


#include "tinythread.h"
#include "es_timer.h"

using namespace std;
using namespace tthread;

#define RESPONSE_MODE 2
#define SEND_MODE 1
#define RECV_MODE 0

// Configuration
char *hostname = NULL, protocol[4] = "UDP";
int port_number = 4180, stat_update = 500, mode = SEND_MODE;
int pktsize = 1000, pktrate = 1000, pktnum = 0;
int rbufsize = 0, sbufsize = 0;

// Socket variables
struct sockaddr_in sock_addr;
SOCKET socket_descriptor;

// Thread variables
int keep_running = 1;

// Statistics Display
long elapsed_time, packet_received = 0, packet_lost = 0;
double throughput = 0, jitter = 0;
long accumulate_filesize[3] = { 0, 0, 0 };
double jitter_old = 0, jitter_new = 0;
ES_Timer program_timer = ES_Timer();

// Configuration struct
struct operating_parameters {
	// TCP = true, UDP = false
	bool connection_type = false;
	// Server receive = true, Server send = false
	bool connection_mode = false;

	int packet_size = 0;
	int packet_rate = 0;
	long packet_number = 0;
};
struct operating_parameters *client_params;

// Response mode
double min_response = 1000000.0, max_response = 0.0;
long total_response = 0;
double average_response = 0.0;

void args_parser(int argc, char **argv) {
	const char *short_opt = "ab:c:d:e:f:g:h:i:jk:m";
	struct option long_opt[] =
	{
		{ "send",     no_argument,       NULL, 'a' },
		{ "recv",     no_argument,       NULL, 'j' },
		{ "response", no_argument,       NULL, 'm' },

		{ "stat",     required_argument, NULL, 'b' },

		{ "rhost",    required_argument, NULL, 'c' },

		{ "rport",    required_argument, NULL, 'd' },

		{ "proto",    required_argument, NULL, 'e' },

		{ "pktsize",  required_argument, NULL, 'f' },

		{ "pktrate",  required_argument, NULL, 'g' },

		{ "pktnum",   required_argument, NULL, 'h' },

		{ "sbufsize", required_argument, NULL, 'i' },
		{ "rbufsize", required_argument, NULL, 'k' },

		{ NULL,       0,                 NULL,  0 }
	};

	int retstat;

	while ((retstat = getopt_long_only(argc, argv, short_opt, long_opt, NULL)) != -1) {
		switch (retstat) {
		case -1:       /* no more arguments */
		case 0:        /* long options toggles */
			break;

			// Sending mode
		case 'a':
			mode = SEND_MODE;
			break;

			// Receiving mode
		case 'j':
			mode = RECV_MODE;
			break;
		
			// Response mode
		case 'm':
			mode = RESPONSE_MODE;
			break;

			// Update of statistics display
		case 'b':
			stat_update = atoi(optarg);
			break;

			// Hostname
		case 'c':
			hostname = (char *)calloc(strlen(optarg) + 1, sizeof(char));
			strcpy(hostname, optarg);
			break;

			// Port number
		case 'd':
			port_number = atoi(optarg);
			break;

			// Protocol
		case 'e':
			if (strcmp(optarg, "TCP") == 0 || strcmp(optarg, "tcp") == 0)
				strcpy(protocol, "TCP");
			break;

			// Packet size
		case 'f':
			pktsize = atoi(optarg);
			break;

			// Packet rate
		case 'g':
			pktrate = atoi(optarg);
			break;

			// Packet number
		case 'h':
			pktnum = atoi(optarg);
			break;

			// Buffer size
		case 'i':
			sbufsize = atoi(optarg);
			break;

		case 'k':
			rbufsize = atoi(optarg);
			break;
		};
	}
}

void debug_args() {
	printf("Input Parameters: \n");
	printf("Statistics update: %d\n", stat_update);
	printf("Hostname: %s\n", hostname);
	printf("Port number: %d\n", port_number);
	printf("Protocol: %s\n", protocol);
	printf("Packet size: %d\n", pktsize);
	printf("Packet rate: %d\n", pktrate);
	printf("Packet number: %d\n", pktnum);
	printf("==================================\n\n");
}

// Later for statistics display in multi-threading
void displayThread(void *data) {
	long duration = 0;
	ES_Timer timer = ES_Timer();
	timer.Start();

	while (keep_running != 0) {
		Sleep(stat_update);
		duration = timer.Elapsed();

		double txrate = accumulate_filesize[0] * 8000 / duration;
		txrate += accumulate_filesize[1] * 8 / duration;
		txrate += accumulate_filesize[2] * 8 / (1000 * duration);

		// printf("Accumulated Filesize: %ldMB, %ldKB, %ldB\n", accumulate_filesize[0], accumulate_filesize[1], accumulate_filesize[2]);

		if (mode == RECV_MODE) {
			double lost_percentage = packet_lost * 100.0 / packet_received;
			if (packet_received == 0)
				lost_percentage = 0;

			printf("Elapsed [%ldms] Pkts [%ld] Lost [%ld, %.2f%%] Rate [%.2fMbps] Jitter [%.2fms]\n",
				duration,
				packet_received,
				packet_lost,
				lost_percentage,
				txrate,
				jitter_new);
		}
		else if (mode == SEND_MODE) {
			printf("Elapsed [%ldms] Rate [%.2fMbps]\n",
				duration,
				txrate);
		}
		else if (mode == RESPONSE_MODE) {
			printf("Elapsed [%ldms] Replies [%ld] Min [%.4fms] Max [%.4fms] Avg [%.4fms] Jitter [%.4fms]\n",
				duration,
				total_response,
				min_response,
				max_response,
				average_response,
				jitter_new
			);
		}
	}
	return;
}
// ===============================================

void socket_cleanup() {
	// struct linger optval;
	// optval.l_onoff = 1;
	// optval.l_linger = 5;
	// setsockopt(socket_descriptor, SOL_SOCKET, SO_LINGER, (char *)&optval, sizeof(optval));
	if (mode != RESPONSE_MODE)
		keep_running = 0;

	if (socket_descriptor != INVALID_SOCKET) {
		closesocket(socket_descriptor);
	}
}

void socket_init() {
	int iResult;

	memset(&sock_addr, 0, sizeof(struct sockaddr_in));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port_number);

	// Resolve hostname to address
	sock_addr.sin_addr.s_addr = inet_addr(hostname);
	if (sock_addr.sin_addr.s_addr == -1) {
		struct hostent *host_result = gethostbyname(hostname);
		if (host_result != NULL) {
			// Debug IP address
			struct in_addr addr = { 0, };
			addr.s_addr = *(u_long *)host_result->h_addr_list[0];
			// printf("Resolved IP Address: %s\n", inet_ntoa(addr));

			sock_addr.sin_addr.s_addr = inet_addr(inet_ntoa(addr));
		}
	}
	// ============================

	// Create socket descriptor
	socket_descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	if (socket_descriptor == INVALID_SOCKET) {
		printf("Client: socket failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}
	// ========================
	
	// Connect to server
	if (connect(socket_descriptor, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
		printf("Client: connect() failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}
	// =================

	// Set send and receive buffer size
	if (sbufsize > 0) {
		iResult = setsockopt(socket_descriptor, SOL_SOCKET, SO_SNDBUF, (char *)&sbufsize, sizeof(sbufsize));
		if (iResult < 0) {
			printf("Server: set socket sbufsize error. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}
	}
	if (rbufsize > 0) {
		iResult = setsockopt(socket_descriptor, SOL_SOCKET, SO_RCVBUF, (char *)&rbufsize, sizeof(rbufsize));
		if (iResult < 0) {
			printf("Server: set socket rbufsize error. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}
	}
	// ==================================
}

void operating_parameters() {
	client_params = (struct operating_parameters *) malloc(sizeof(struct operating_parameters));

	// Put congiuration into structure
	client_params->connection_type = (strcmp(protocol, "TCP") == 0);
	if (mode == RESPONSE_MODE)
		client_params->connection_mode = RECV_MODE;
	else
		client_params->connection_mode = mode;

	client_params->packet_size = htonl(pktsize);
	if (mode == RESPONSE_MODE)
		client_params->packet_rate = htonl(0);
	else
		client_params->packet_rate = htonl(pktrate);
	if (mode == RESPONSE_MODE)
		client_params->packet_number = htonl(1);
	else
		client_params->packet_number = htonl(pktnum);
	// ===============================

	// Send to server and initialize connection
	int iResult;
	iResult = send(socket_descriptor, (char *) client_params, sizeof(struct operating_parameters), 0);

	// ========================================

	if (iResult == SOCKET_ERROR) {
		printf("Client: send failed. Error code: %i\n", get_socket_error());
	}

	free(client_params);
}

void sending_mode() {
	// printf("Sending Mode\n");
	// debug_args();

	int iResult;
	long sent_count = 0;
	char *sendbuf = (char *)calloc(pktsize, sizeof(char));
	ES_Timer timer = ES_Timer();

	while (sent_count < pktnum || pktnum == 0) {
		// printf("Packet number: %ld\n", sent_count);
		memset(sendbuf, 0, pktsize);
		sprintf(sendbuf, "%ld", sent_count);

		timer.Start();
		iResult = send(socket_descriptor, sendbuf, pktsize, 0);

		double duration = timer.Elapsed();

		if (iResult == SOCKET_ERROR) {
			printf("Client: send failed. Error code: %i\n", get_socket_error());
			break;
		}
		else {
			// printf("Client: send packet with size %d and packet number %ld\n", iResult, sent_count);
			accumulate_filesize[2] += (iResult);
			if (accumulate_filesize[2] > 1024) {
				accumulate_filesize[1] += (accumulate_filesize[2] / 1024);
				accumulate_filesize[2] = accumulate_filesize[2] % 1024;
			}
			if (accumulate_filesize[1] > 1024) {
				accumulate_filesize[0] += (accumulate_filesize[1] / 1024);
				accumulate_filesize[1] = accumulate_filesize[1] % 1024;
			}
		}

		if (pktrate > 0) {
			double sleep_time = (1000.0 / pktrate) * iResult - duration;
			if (sleep_time > 0)
				Sleep(sleep_time);
		}

		sent_count += 1;
	}

	free(sendbuf);
	socket_cleanup();
}

void receiving_mode() {
	// printf("Receiving Mode\n");
	// debug_args();

	int iResult, buflen = pktsize;
	char *recvbuf;
	struct sockaddr sender_addr;
	recvbuf = (char *)calloc(buflen, sizeof(char));
	// memset(recvbuf, 0, buflen);

	memset(&sender_addr, 0, sizeof(struct sockaddr_in));
	int sockaddr_len = sizeof(sender_addr);

	long expected_sequence = 0, incoming_time, previous_time = 0;
	do {
		iResult = recv(socket_descriptor, recvbuf, buflen, 0);

		iResult = recvfrom(socket_descriptor, recvbuf, buflen, 0, (struct sockaddr *) &sender_addr, (socklen_t *) &sockaddr_len);


		if (iResult == 0) {
			printf("Client: connection closing...\n");
			break;
		}
		else if (iResult == SOCKET_ERROR || iResult < 0) {
			printf("Client: recv failed. Error code: %i\n", get_socket_error());
			break;
		}
		else {
			incoming_time = program_timer.Elapsed();
			packet_received += 1;
			accumulate_filesize[2] += (iResult);
			if (accumulate_filesize[2] > 1024) {
				accumulate_filesize[1] += (accumulate_filesize[2] / 1024);
				accumulate_filesize[2] = accumulate_filesize[2] % 1024;
			}
			if (accumulate_filesize[1] > 1024) {
				accumulate_filesize[0] += (accumulate_filesize[1] / 1024);
				accumulate_filesize[1] = accumulate_filesize[1] % 1024;
			}

			double time_T = (double) (incoming_time - previous_time) / packet_received;
			jitter_new = (jitter_old * (packet_received - 1) + (incoming_time - previous_time - time_T)) / packet_received;

			jitter_old = jitter_new;
			previous_time = incoming_time;

			long sequence_number = atoi(recvbuf);
			// printf("Client: received packet with sequence number %ld and packet size %d\n", sequence_number, iResult);

			if (sequence_number > expected_sequence) {
				packet_lost += (sequence_number - expected_sequence);
				expected_sequence = sequence_number + 1;
			}
			
			expected_sequence += 1;

			if (expected_sequence == pktnum)
				break;
		}
	} while (iResult > 0);

	iResult = shutdown(socket_descriptor, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("Client: shutdown failed. Error code: %i\n", get_socket_error());
	}

	free(recvbuf);
	socket_cleanup();
}

void response_mode() {
	if (strcmp(protocol, "TCP") != 0) {
		printf("Client: Response mode is only available for TCP.\n");
		exit(1);
	}
	if (pktrate == 1000)
		pktrate = 10;

	ES_Timer timer = ES_Timer();
	int iResult, buflen = pktsize;
	double response_time;
	char *recvbuf;

	while (total_response < pktnum || pktnum == 0) {
		// Create new socket
		socket_init();

		operating_parameters();
		response_time = timer.ElapseduSec();
		long duration = timer.Elapsed();

		recvbuf = (char *)calloc(buflen, sizeof(char));
		iResult = recv(socket_descriptor, recvbuf, buflen, 0);

		if (iResult == 0) {
			printf("Client: connection closing...\n");
		}
		else if (iResult == SOCKET_ERROR || iResult < 0) {
			printf("Client: recv failed. Error code: %i\n", get_socket_error());
			break;
		}
		else {
			response_time = timer.ElapseduSec() - response_time;
			response_time *= 0.001;

			max_response = (response_time > max_response) ? response_time : max_response;
			min_response = (response_time < min_response) ? response_time : min_response;
			average_response = (average_response * total_response + response_time) / (total_response);

			double jitter_average = fabs(response_time - average_response);
			if (total_response == 0)
				jitter_new = response_time;
			else
				jitter_new = (jitter_new * total_response + jitter_average) / (total_response);

			total_response += 1;

			if (pktrate > 0) {
				double sleep_time = (1000.0 / pktrate) * total_response - duration;
				if (sleep_time > 0)
					Sleep(sleep_time);
			}
		}
		
		socket_cleanup();
	}
	keep_running = 0;
}

int main(int argc, char **argv) {
	args_parser(argc, argv);

	if (!hostname) {
		hostname = (char *)calloc(strlen("localhost") + 1, sizeof(char));
		strcpy(hostname, "localhost");
	}

	if (mode != RESPONSE_MODE) {
		socket_init();
		operating_parameters();

		// Start the child thread
		thread thread_id(displayThread, NULL);

		if (mode == SEND_MODE)
			sending_mode();
		else
			receiving_mode();
		
		// Wait for the thread to finish
		thread_id.join();
	}
	else {
		// Start the child thread
		thread thread_id(displayThread, NULL);

		response_mode();
		
		// Wait for the thread to finish
		thread_id.join();
	}

	if (hostname != NULL)
		free(hostname);

	return 0;
}