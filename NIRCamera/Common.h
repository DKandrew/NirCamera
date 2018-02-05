/*
This .h file contains common functions for the following feature
1. Networking
2. Debugging
*/
#pragma once

// Must define this before the #include <winsock2.h> since we are using many old stuffs (goodies...).
// See the discussion here: https://stackoverflow.com/questions/32234348/winsock-deprecated-no-warnings  
#define _WINSOCK_DEPRECATED_NO_WARNINGS 

#include <iostream>
#include <WinSock2.h>			// Alarm: (Short answer, #include "Common.h" before any other #include) winsock2.h should be included before other #include. Otherwise you will experience linkage problem in the compilor time.
#include <WS2tcpip.h>
#include <MSWSock.h>

#include "spdlog/spdlog.h"

#pragma comment(lib,"ws2_32")	// Need this for the WinSock2 to work
#pragma comment(lib,"mswsock")

// List of command
#define COMM_STREAM "STREAM"
#define COMM_GET_XRAY_TOTALNUM "GET XRAY TOTALNUM"
#define COMM_GET_XRAY "GET XRAY"
#define COMM_EOF	"\n" 
#define COMM_OK		"OK"
#define COMM_ERROR	"ERROR"
#define COMM_INDEX_LEN sizeof(int)		// sizeof(int) == 4 bytes
#define COMM_ERR_INVALID_INDEX "The required index is not available."

// Completion Key for the IOCPs
enum COMPLETION_KEY {
	COMPLETION_KEY_IO = 0,
	COMPLETION_KEY_SHUTDOWN = 1,
};

void PRINT_WSAERROR(char *msg);

void print_connection_info(SOCKET socket);

void DebugLog(char *msg);

void CoutPrint(char *msg);