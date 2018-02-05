#include "Common.h"

void PRINT_WSAERROR(char *msg) {
	int error = WSAGetLastError();
	fprintf(stderr, "%s = %d", msg, error);

	// More explanation for the error
	// Format: ": [message]"
	switch (error)
	{
	case 64:
		fprintf(stderr, ": The connection is no longer available.");
		break;
	default:
		break;
	}

	fprintf(stderr, "\n");
}

// Print out the connection information
void print_connection_info(SOCKET socket) {
	struct sockaddr_storage conn_info;
	socklen_t size = sizeof(conn_info);
	char IpStr[INET6_ADDRSTRLEN];

	getpeername(socket, (struct sockaddr*)&conn_info, &size);

	struct sockaddr_in *s = (struct sockaddr_in*)&conn_info;
	int port = ntohs(s->sin_port);
	inet_ntop(AF_INET, &s->sin_addr, IpStr, sizeof(IpStr));

	printf("Receive connection from IP: %s, connected on Port: %d\n", IpStr, port);
}

// A handly debug function that prompts msg to the stderr
void DebugLog(char *msg) {
	fprintf(stderr, "%s\n", msg);
}

/*
Replace the cout << .. << endl format
Because won't use 'using namespace std'
*/
void CoutPrint(char *msg) {
	std::cout << msg << std::endl;
}
