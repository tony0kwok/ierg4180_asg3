#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <algorithm>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

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
#define WSAEMSGSIZE 10040

#include "tinythread.h"
#include "es_timer.h"
#include "pipe.h"

using namespace std;
using namespace tthread;

#define SEND_MODE 1
#define RECV_MODE -1

// Function declaration
void statistics_display(void *input);
void args_parser(int argc, char **argv);
void socket_cleanup();
void socket_init();
bool sockaddr_equal(struct sockaddr_in *input1, struct sockaddr_in *input2);
void thread_collector(void *input);
void double_threadpool();
void halve_threadpool();
void tcp_connect_init(void *input);
void tcp_handler(void *input);
void udp_read_handler(void *input);
void udp_write_handler(void *input);
void connection_handler(void *input);
// ====================

// Configuration
char *hostname = NULL, protocol[4] = "UDP";
int port_number = 4180, stat_update = 0;
int sbufsize = 0, rbufsize = 0;
bool thread_mode = true;

struct incoming_client {
	// TCP = true, UDP = false
	bool connection_type = false;
	SOCKET socket_descriptor;
	struct sockaddr_in client_addr;

	// Server receive = true, Server send = false
	bool connection_mode = false;

	int packet_size = 0;
	int packet_rate = 0;
	long packet_number = 0;

	long next_packet_number = 0;
	long next_packet_transmission_time = 0;
	long time_interval = 0;

	// For UDP receive from client
	long last_packet_expected_time = 0;
};

struct operating_parameters {
	// TCP = true, UDP = false
	bool connection_type = false;
	// Server receive = true, Server send = false
	bool connection_mode = false;

	int packet_size = 0;
	int packet_rate = 0;
	long packet_number = 0;
};

// Socket variables
struct sockaddr_in sock_addr;
SOCKET tcp_socket, udp_socket;
fd_set socket_read_set, socket_write_set;

// Clients
tthread::mutex *client_lock, *cleanup_lock;
tthread::condition_variable cond;
struct incoming_client **connecting_client;
int next_client = 0, max_client = 8;
bool *close_connection;

// Client threads
tthread::thread **connection_thread;
tthread::thread *thread_lock;

// Thread pool variables
pipe_t* p;
pipe_producer_t* producers[2];
pipe_consumer_t** consumers;
int max_pool_size = 1024;
int min_pool_size = max_client;
ES_Timer counter = ES_Timer();

// Timer
ES_Timer timer = ES_Timer();

// Display thread
bool keep_running = true;
int tcp_client = 0, udp_client = 0;

void statistics_display(void *input) {
	long next_display_time = stat_update;

	if (stat_update == 0)
		keep_running = false;

	while (keep_running == true) {
		long current_time = timer.Elapsed();
		if (current_time >= next_display_time) {
			if (thread_mode == true)
				printf("Elapsed [%lds] ThreadPool [%d|%d] TCP Clients [%d] UDP Clients [%d]\n", current_time / 1000, max_client, next_client, tcp_client, udp_client);
			else
				printf("Elapsed [%lds] TCP Clients [%d] UDP Clients [%d]\n", current_time / 1000, tcp_client, udp_client);
			next_display_time += stat_update;
		}
		else {
			Sleep((next_display_time - current_time));
		}
	}

	return;
}

void args_parser(int argc, char **argv) {
	const char *short_opt = "b:c:d:i:j:m:n:";
	struct option long_opt[] =
	{
		{ "stat",     required_argument, NULL, 'b' },

		{ "lhost",    required_argument, NULL, 'c' },
		{ "lport",    required_argument, NULL, 'd' },

		{ "sbufsize", required_argument, NULL, 'i' },
		{ "rbufsize", required_argument, NULL, 'j' },

		{ "servermodel", required_argument, NULL, 'm' },
		{ "poolsize", required_argument, NULL, 'n'},

		{ NULL,       0,                 NULL,  0 }
	};

	int retstat;

	while ((retstat = getopt_long_only(argc, argv, short_opt, long_opt, NULL)) != -1) {
		switch (retstat) {
		case -1:       /* no more arguments */
		case 0:        /* long options toggles */
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

			// Buffer size
		case 'i':
			sbufsize = atoi(optarg);
			break;

		case 'j':
			rbufsize = atoi(optarg);
			break;

		case 'm':
			if (strcmp(optarg, "select") == 0)
				thread_mode = false;
			break;
		
		case 'n':
			max_client = atoi(optarg);
			min_pool_size = max_client;
			break;
		};
	}
}

void debug_args(struct incoming_client *client_info) {
	printf("Incoming Client Parameters: \n");
	printf("Client IP: %s\n", inet_ntoa(client_info->client_addr.sin_addr));
	printf("Client Port number: %d\n", ntohs(client_info->client_addr.sin_port));
	printf("Protocol: %s\n", (client_info->connection_type) ? "TCP" : "UDP");
	printf("Receive from client: %s\n", (client_info->connection_mode) ? "true" : "false");
	printf("Packet size: %d\n", client_info->packet_size);
	printf("Packet rate: %d\n", client_info->packet_rate);
	printf("Packet number: %ld\n", client_info->packet_number);
	printf("==================================\n\n");
}

void socket_cleanup() {
	// struct linger optval;
	// optval.l_onoff = 1;
	// optval.l_linger = 5;
	// setsockopt(tcp_socket, SOL_SOCKET, SO_LINGER, (char *)&optval, sizeof(optval));

	keep_running = false;

	if (tcp_socket != INVALID_SOCKET)
		closesocket(tcp_socket);

	if (udp_socket != INVALID_SOCKET)
		closesocket(udp_socket);

	#ifdef _WIN32
		WSACleanup();
	#endif
}

void socket_init() {
	int iResult;
	#ifdef _WIN32
		WSADATA wsaData;
		iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (iResult != 0) {
			printf("WSAStartup failed: %d\n", iResult);
			exit(1);
		}
	#endif

	memset(&sock_addr, 0, sizeof(struct sockaddr_in));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port_number);

	// Resolve hostname to address
	if (strcmp(hostname, "IN_ADDR_ANY") == 0) {
		sock_addr.sin_addr.s_addr = INADDR_ANY;
	}
	else {
		sock_addr.sin_addr.s_addr = inet_addr(hostname);
		if (sock_addr.sin_addr.s_addr == -1) {
			struct hostent *host_result = gethostbyname(hostname);
			if (host_result != NULL) {
				// Debug IP address
				struct in_addr addr = { 0, };
				addr.s_addr = *(u_long *)host_result->h_addr_list[0];
				printf("Resolved IP Address: %s\n", inet_ntoa(addr));

				sock_addr.sin_addr.s_addr = inet_addr(inet_ntoa(addr));
			}
		}
	}
	// ============================

	// Get socket descriptor
	tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tcp_socket == INVALID_SOCKET) {
		printf("TCP Socket failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}

	udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_socket == INVALID_SOCKET) {
		printf("UDP Socket failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}
	// ====================

	int reuse_address = 1;
	iResult = setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_address, sizeof(reuse_address));
	if (iResult == SOCKET_ERROR) {
		printf("TCP Socket setsockopt failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}

	// TCP bind and listen socket
	iResult = ::bind(tcp_socket, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_in));
	if (iResult == SOCKET_ERROR) {
		printf("Server: TCP bind failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}

	listen(tcp_socket, 128);
	if (iResult == SOCKET_ERROR) {
		printf("Server: TCP listen failed. Error code: %i\n", get_socket_error());
		socket_cleanup();
		exit(1);
	}

	// Set send and receive buffer size
	if (sbufsize > 0) {
		iResult = setsockopt(tcp_socket, SOL_SOCKET, SO_SNDBUF, (char *)&sbufsize, sizeof(sbufsize));
		if (iResult < 0) {
			printf("Server: set socket sbufsize error. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}
	}
	if (rbufsize > 0) {
		iResult = setsockopt(tcp_socket, SOL_SOCKET, SO_RCVBUF, (char *)&rbufsize, sizeof(rbufsize));
		if (iResult < 0) {
			printf("Server: set socket rbufsize error. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}
	}

	if (sbufsize > 0) {
		iResult = setsockopt(udp_socket, SOL_SOCKET, SO_SNDBUF, (char *)&sbufsize, sizeof(sbufsize));
		if (iResult < 0) {
			printf("Server: set socket sbufsize error. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}
	}
	if (rbufsize > 0) {
		iResult = setsockopt(udp_socket, SOL_SOCKET, SO_RCVBUF, (char *)&rbufsize, sizeof(rbufsize));
		if (iResult < 0) {
			printf("Server: set socket rbufsize error. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}
	}
	// ================================
}

bool sockaddr_equal(struct sockaddr_in *input1, struct sockaddr_in *input2) {
	if (ntohs(input1->sin_port) != ntohs(input2->sin_port))
		return false;
	if (strcmp(inet_ntoa(input1->sin_addr), inet_ntoa(input2->sin_addr)) != 0)
		return false;
	
	return true;
}

SOCKET max_socket() {
	SOCKET max_sd = max(tcp_socket, udp_socket);
	for (int i = 0; i < next_client; i++)
		max_sd = max(max_sd, connecting_client[i]->socket_descriptor);
	return (max_sd + 1);
}

void concurrent_client() {
	// Clear descriptor set
	FD_ZERO(&socket_read_set);
	FD_ZERO(&socket_write_set);

	SOCKET max_sd = max_socket();

	while(true) {
		// Set all sockets into socket read set
		FD_SET(tcp_socket, &socket_read_set);
		FD_SET(udp_socket, &socket_read_set);
		FD_SET(udp_socket, &socket_write_set);
		for (int i = 0; i < next_client; i++) {
			if (connecting_client[i]->connection_type == true) {
				if (connecting_client[i]->connection_mode == true)
					FD_SET(connecting_client[i]->socket_descriptor, &socket_read_set);
				else
					FD_SET(connecting_client[i]->socket_descriptor, &socket_write_set);
			}
		}
		max_sd = max_socket();

		// Check any available socket descriptor
		int retstat = select(max_sd, &socket_read_set, &socket_write_set, NULL, NULL);
		if (retstat < 0) {
			printf("Select socket failed. Error code: %i\n", get_socket_error());
			socket_cleanup();
			exit(1);
		}

		// TCP socket is available to read
		if (FD_ISSET(tcp_socket, &socket_read_set)) {
			struct incoming_client *new_client = (struct incoming_client *) malloc(sizeof(struct incoming_client));
			memset(&new_client->client_addr, 0, sizeof(struct sockaddr_in));
			socklen_t sockaddr_len = sizeof(new_client->client_addr);

			new_client->socket_descriptor = accept(tcp_socket, (struct sockaddr *) &new_client->client_addr, &sockaddr_len);

			if (new_client->socket_descriptor == INVALID_SOCKET) {
				printf("Server: TCP accept failed. Error code: %i\n", get_socket_error());
				socket_cleanup();
				exit(1);
			}

			// printf("Server: TCP accpeting incoming client\n");
			struct operating_parameters *params_buffer = (struct operating_parameters *) malloc(sizeof(struct operating_parameters));

			retstat = recv(new_client->socket_descriptor, (char *) params_buffer, sizeof(struct operating_parameters), 0);
			if (retstat > 0 && next_client < max_client) {
				new_client->connection_type = params_buffer->connection_type;
				new_client->connection_mode = params_buffer->connection_mode;
				new_client->packet_size = ntohl(params_buffer->packet_size);
				new_client->packet_rate = ntohl(params_buffer->packet_rate);
				new_client->packet_number = ntohl(params_buffer->packet_number);
				new_client->next_packet_number = 0;

				// debug_args(new_client);
				connecting_client[next_client] = new_client;
				next_client += 1;
				tcp_client += 1;
				// printf("Server: Current connecting client %d\n", next_client);
				
				// Receive from Client
				if (new_client->connection_mode == true) {
					FD_SET(new_client->socket_descriptor, &socket_read_set);
				}
				// Send to Client
				else {
					if (new_client->packet_rate > 0)
						new_client->time_interval = 1000 * new_client->packet_size / new_client->packet_rate;
					else
						new_client->time_interval = 0;
					new_client->next_packet_transmission_time = timer.Elapsed() + new_client->time_interval;

					FD_SET(new_client->socket_descriptor, &socket_write_set);
				}
			}
			else if (next_client >= max_client)
				printf("Server: maximum number of clients reached\n");
			else if (retstat != sizeof(params_buffer))
				printf("Server: operating parameters missing.\n");
			else
				printf("Server: recv failed. Error code: %i\n", get_socket_error());
			
			free(params_buffer);
		}

		// Check all TCP sockets
		for (int i = 0; i < next_client; i++) {
			if (connecting_client[i]->connection_type == true) {
				// Receive message from TCP client
				if (connecting_client[i]->connection_mode == true) {
					if (FD_ISSET(connecting_client[i]->socket_descriptor, &socket_read_set)) {
						bool close_connection = false;
						// printf("Server: Client %d keeps connecting\n", i);

						char *recv_buffer = (char *) calloc(sizeof(char), connecting_client[i]->packet_size);
						int retstat = recv(connecting_client[i]->socket_descriptor, recv_buffer, connecting_client[i]->packet_size, 0);

						if (retstat > 0) {
							connecting_client[i]->next_packet_number = atoi(recv_buffer) + 1;
							// printf("Server: Next packet number %ld\n", connecting_client[i]->next_packet_number);
						}
						else if (retstat == 0) {
							retstat = shutdown(connecting_client[i]->socket_descriptor, SD_SEND);
							if (retstat == SOCKET_ERROR) {
								printf("Server: shutdown failed. Error code: %i\n", get_socket_error());
							}
							close_connection = true;
						}
						free(recv_buffer);

						// Remove from connection since packets are all delivered
						if (connecting_client[i]->next_packet_number == connecting_client[i]->packet_number || close_connection == true) {
							FD_CLR(connecting_client[i]->socket_descriptor, &socket_read_set);

							free(connecting_client[i]);
							for (int j = i + 1; j < max_client; j++)
								connecting_client[j - 1] = connecting_client[j];
							next_client -= 1;
							tcp_client -= 1;
							
							// printf("Server: Current connecting client %d\n", next_client);
							break;
						}
					}
				}
				// Send message to TCP client
				else {
					if (FD_ISSET(connecting_client[i]->socket_descriptor, &socket_write_set)) {
						long current_time = timer.Elapsed();
						if (current_time >= connecting_client[i]->next_packet_transmission_time) {
							char *sendbuf = (char *)calloc(connecting_client[i]->packet_size, sizeof(char));
							memset(sendbuf, 0, connecting_client[i]->packet_size);
							sprintf(sendbuf, "%ld", connecting_client[i]->next_packet_number);

							int iResult = send(connecting_client[i]->socket_descriptor, sendbuf, connecting_client[i]->packet_size, 0);
							if (iResult == SOCKET_ERROR) {
								printf("Server: send failed. Error code: %i\n", get_socket_error());
							}
							// printf("Server: Send packet to client %d with packet number %ld\n", i, connecting_client[i]->next_packet_number);

							current_time = timer.Elapsed();
							long delay_sent = current_time - connecting_client[i]->next_packet_transmission_time;

							connecting_client[i]->next_packet_transmission_time += (connecting_client[i]->time_interval - delay_sent);
							connecting_client[i]->next_packet_number += 1;
							free(sendbuf);

							// Remove from connection since packets are all delivered
							if (connecting_client[i]->next_packet_number == connecting_client[i]->packet_number) {
								retstat = shutdown(connecting_client[i]->socket_descriptor, SD_SEND);
								if (retstat == SOCKET_ERROR) {
									printf("Server: shutdown failed. Error code: %i\n", get_socket_error());
								}

								FD_CLR(connecting_client[i]->socket_descriptor, &socket_write_set);
								
								free(connecting_client[i]);
								for (int j = i + 1; j < max_client; j++)
									connecting_client[j - 1] = connecting_client[j];
								next_client -= 1;
								tcp_client -= 1;

								// printf("Server: Current connecting client %d\n", next_client);
								break;
							}
						}
					}
				}
			}
		}

	}
}

void thread_collector(void *input) {
	while (keep_running == true) {
		for (int i = 0; i < next_client; i++) {
			// Check any thread already closed connection
			if (close_connection[i] == true) {
				client_lock->lock();
				if (connecting_client[i]->connection_type == true)
					tcp_client -= 1;
				else
					udp_client -= 1;

				free (connecting_client[i]);

				for (int j = i + 1; j < next_client; j++) {
					connecting_client[j - 1] = connecting_client[j];
					connection_thread[j - 1] = connection_thread[j];
					close_connection[j - 1] = close_connection[j];
				}

				next_client -= 1;
				close_connection[next_client] = false;

				client_lock->unlock();
				i = -1;
			}

		}

		if (next_client > max_client / 2) {
			counter.Start();
		}

		// Double or halve pool size
		if (max_client > min_pool_size && counter.Elapsed() >= 60 * 1000) {
			// Timeout and halve pool size
			client_lock->lock();
			halve_threadpool();
			client_lock->unlock();
		}
		// =========================

		// Check garbage every second
		Sleep(1000);
	}
}

void double_threadpool() {
	int original_size = max_client;
	max_client = (max_client * 2 <= max_pool_size) ? max_client * 2 : max_pool_size;
	connecting_client = (struct incoming_client **) realloc(connecting_client, sizeof(struct incoming_client *) * max_client);
	connection_thread = (tthread::thread **) realloc(connection_thread, sizeof(tthread::thread *) * max_client);
	close_connection = (bool *) realloc(close_connection, max_client);

	if (max_client > original_size) {
		consumers = (pipe_consumer_t **) realloc(consumers, sizeof(pipe_consumer_t *)* max_client);
		for (int i = original_size; i < max_client; i++) {
			consumers[i] = pipe_consumer_new(p);
			connection_thread[i] = new thread(connection_handler, consumers[i]);
		}
	}
}

void halve_threadpool() {
	int original_size = max_client;
	max_client = (max_client / 2 >= min_pool_size) ? max_client / 2 : min_pool_size;
	if (max_client < original_size) {
		consumers = (pipe_consumer_t **) realloc(consumers, sizeof(pipe_consumer_t *)* max_client);
		for (int i = max_client; i < original_size; i++) {
			pipe_consumer_free(consumers[i]);
			connection_thread[i]->join();
		}
	}

	connecting_client = (struct incoming_client **) realloc(connecting_client, sizeof(struct incoming_client *) * max_client);
	connection_thread = (tthread::thread **) realloc(connection_thread, sizeof(tthread::thread *) * max_client);
	close_connection = (bool *) realloc(close_connection, max_client);
}

// Keep running until crash
void tcp_connect_init(void *input) {
	while (keep_running == true) {
		struct incoming_client *new_client = (struct incoming_client *) malloc(sizeof(struct incoming_client));
		memset(&new_client->client_addr, 0, sizeof(struct sockaddr_in));
		socklen_t sockaddr_len = sizeof(new_client->client_addr);

		new_client->socket_descriptor = accept(tcp_socket, (struct sockaddr *) &new_client->client_addr, &sockaddr_len);

		if (new_client->socket_descriptor == INVALID_SOCKET) {
			printf("Server: TCP accept failed. Error code: %i\n", get_socket_error());
			closesocket(tcp_socket);
			return;
		}

		// printf("Server: TCP accpeting incoming client\n");
		struct operating_parameters *params_buffer = (struct operating_parameters *) malloc(sizeof(struct operating_parameters));

		int retstat = recv(new_client->socket_descriptor, (char *) params_buffer, sizeof(struct operating_parameters), 0);
		if (retstat > 0) {
			new_client->connection_type = params_buffer->connection_type;
			new_client->connection_mode = params_buffer->connection_mode;
			new_client->packet_size = ntohl(params_buffer->packet_size);
			new_client->packet_rate = ntohl(params_buffer->packet_rate);
			new_client->packet_number = ntohl(params_buffer->packet_number);
			new_client->next_packet_number = 0;

			// Calculate delay time
			if (new_client->connection_mode == false) {
				if (new_client->packet_rate > 0)
					new_client->time_interval = 1000 * new_client->packet_size / new_client->packet_rate;
				else
					new_client->time_interval = 0;
				new_client->next_packet_transmission_time = timer.Elapsed() + new_client->time_interval;
			}
			// ====================

			// debug_args(new_client);
			
			// Critical section for adding new client
			bool new_connection = false;
			client_lock->lock();
			if (next_client >= max_client) {
				double_threadpool();
			}

			if (next_client < max_client) {
				connecting_client[next_client] = new_client;
				pipe_push(producers[0], new_client, 1);
				// connection_thread[next_client] = new thread(connection_handler, new_client);
				next_client += 1;
				tcp_client += 1;
				new_connection = true;
			}
			client_lock->unlock();
			// ======================================

			if (new_connection == false)
				free(new_client);
		}
		else if (retstat != sizeof(params_buffer)) {
			printf("Server: operating parameters missing.\n");
			free(new_client);
		}
		else {
			printf("Server: recv failed. Error code: %i\n", get_socket_error());
			free(new_client);
		}
		
		free(params_buffer);
	}
}
// ========================

// TCP thread for handling send / recv
void tcp_handler(void *input) {
	struct incoming_client *thread_client = (struct incoming_client *) input;
	bool close_tcp_connection = false;

	while ((thread_client->next_packet_number < thread_client->packet_number || thread_client->packet_number == 0) && close_tcp_connection == false) {
		if (thread_client->connection_type == true) {
			// Receive message from TCP client
			if (thread_client->connection_mode == true) {
				// printf("Server: Client %d keeps connecting\n", i);

				char *recv_buffer = (char *) calloc(sizeof(char), thread_client->packet_size);
				int retstat = recv(thread_client->socket_descriptor, recv_buffer, thread_client->packet_size, 0);

				if (retstat > 0) {
					thread_client->next_packet_number = atoi(recv_buffer) + 1;
					// printf("Server: Next packet number %ld\n", thread_client->next_packet_number);
				}
				else if (retstat == 0) {
					retstat = shutdown(thread_client->socket_descriptor, SD_SEND);
					close_tcp_connection = true;
					if (retstat == SOCKET_ERROR) {
						printf("Server: shutdown failed. Error code: %i\n", get_socket_error());
						free(recv_buffer);
						break;
					}
				}
				free(recv_buffer);
			}
			// Send message to TCP client
			else {
				long current_time = timer.Elapsed();
				if (current_time >= thread_client->next_packet_transmission_time) {
					char *sendbuf = (char *)calloc(thread_client->packet_size, sizeof(char));
					memset(sendbuf, 0, thread_client->packet_size);
					sprintf(sendbuf, "%ld", thread_client->next_packet_number);

					int iResult = send(thread_client->socket_descriptor, sendbuf, thread_client->packet_size, 0);
					if (iResult == SOCKET_ERROR) {
						printf("Server: send failed. Error code: %i\n", get_socket_error());
						close_tcp_connection = true;
						free(sendbuf);
						break;
					}

					// printf("Server: Send packet to client %d with packet number %ld\n", i, thread_client->next_packet_number);

					current_time = timer.Elapsed();
					long delay_sent = current_time - thread_client->next_packet_transmission_time;

					thread_client->next_packet_transmission_time += (thread_client->time_interval - delay_sent);
					thread_client->next_packet_number += 1;
					free(sendbuf);
				}
			}
		}
	}

	// Remove from connection since packets are all delivered
	if (thread_client->next_packet_number == thread_client->packet_number || close_tcp_connection == true) {
		if (thread_client->connection_mode == false) {
			int retstat = shutdown(thread_client->socket_descriptor, SD_SEND);
			if (retstat == SOCKET_ERROR)
				printf("Server: shutdown failed. Error code: %i\n", get_socket_error());
		}
		closesocket(thread_client->socket_descriptor);

		// Free incoming_client structure element with mutex lock
		client_lock->lock();
		// Find client index
		int client_index = -1;
		for (int i = 0; i < max_client; i++) {
			if (strcmp((char *) &(connecting_client[i]->client_addr), (char *) &(thread_client->client_addr)) == 0) {
				client_index = i;
				close_connection[client_index] = true;
				break;
			}
		}
			
		client_lock->unlock();
	}
	// ======================================================
	return;
}
// ===================================

void connection_handler(void *input) {
	struct incoming_client *thread_client = (struct incoming_client *) malloc(sizeof(struct incoming_client));
	memset(&thread_client->client_addr, 0, sizeof(struct sockaddr_in));
	pipe_consumer_t *consumer = (pipe_consumer_t *) input;

	while (keep_running == true) {
		int retstat = pipe_pop(consumer, thread_client, 1);

		if (thread_client->connection_type == true)
			tcp_handler(thread_client);
		else
			udp_write_handler(thread_client);
	}
	
	pipe_consumer_free(consumer);
}

int main(int argc, char **argv) {
	timer.Start();

	#ifndef _WIN32
		signal(SIGPIPE, SIG_IGN);
	#endif

	args_parser(argc, argv);

	connecting_client = (struct incoming_client **) malloc(sizeof(struct incoming_client *) * max_client);
	thread *statistics_thread = new thread(statistics_display, NULL);

	if (!hostname) {
		hostname = (char *)calloc(strlen("IN_ADDR_ANY") + 1, sizeof(char));
		strcpy(hostname, "IN_ADDR_ANY");
	}
	socket_init();

	if (thread_mode == true) {
		connection_thread = (tthread::thread **) malloc(sizeof(tthread::thread *) * max_client);
		close_connection = (bool *) calloc(sizeof(bool), max_client);
		consumers = (pipe_consumer_t **) malloc(sizeof(pipe_consumer_t *) * max_client);

		client_lock = new tthread::mutex();
		cleanup_lock = new tthread::mutex();

		p = pipe_new(sizeof(struct incoming_client), 0);
		producers[0] = pipe_producer_new(p);
		producers[1] = pipe_producer_new(p);
		for (int i = 0; i < max_client; i++)
			consumers[i] = pipe_consumer_new(p);

		for (int i = 0; i < max_client; i++)
			connection_thread[i] = new thread(connection_handler, consumers[i]);
		
		thread *collector_thread = new thread(thread_collector, NULL);
		thread *tcp_thread = new thread(tcp_connect_init, NULL);
		thread *udp_thread = new thread(udp_read_handler, NULL);

		// Wait for the thread to finish
		tcp_thread->join();
		delete tcp_thread;
		udp_thread->join();
		delete udp_thread;
		collector_thread->join();
		delete collector_thread;

		pipe_free(p);
		pipe_producer_free(producers[0]);
		pipe_producer_free(producers[1]);
		for (int i = 0; i < max_client; i++)
			pipe_consumer_free(consumers[i]);

		delete client_lock;
		delete cleanup_lock;
		// ==============================
	}
	else {
		concurrent_client();
	}

	statistics_thread->join();
	delete statistics_thread;

	if (hostname != NULL)
		free(hostname);
	
	return 0;
}