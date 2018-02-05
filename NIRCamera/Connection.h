#pragma once

// Must define this before the #include <winsock2.h> since we are using many old stuffs (goodies...).
// See the discussion here: https://stackoverflow.com/questions/32234348/winsock-deprecated-no-warnings  
#define _WINSOCK_DEPRECATED_NO_WARNINGS 

#include <WinSock2.h>
#include <MSWSock.h>
#include <vector>
#include <assert.h>
#include <string>
#include "Common.h"
#include "XRayManager.h"

using namespace std;

class Connection : public OVERLAPPED {
	Connection(const Connection&);

	// The state machine of this class 
	enum STATE {
		WAIT_ACCEPT = 0,
		WAIT_READREQUEST = 1,
		WAIT_SENDDATA = 2,
		WAIT_RESET = 3,
	};

private:
	// Define constants for the Connection Class 
	enum {
		Accept_Address_Length = sizeof(struct sockaddr_in) + 16,
		READ_BUFFER_LEN = 1024,			// The read lenght everytime we read from client 
		MAX_HEADER_LEN = 1024,
	};

	string XRayImagePath = "../XRay";
	int XRayIndex;

	// Params for connection itself
	BYTE AcceptBuffer[Accept_Address_Length * 2];	// Accept buffer holds the remote address data of server and clients. Each of them needs Accept_Address_Length long. Therefore we need a size of 2*Accept_Address_Length 
	WSABUF Buffer_wsa;								// We need this because WSARecv needs it 

	string *buffer; 							// This buffer (pointer) holds all the data we read from client. (Note: if the lenght of header exceeds 
												// 1024 bytes we will treat it as invalid command)

	char tempReadBuffer[READ_BUFFER_LEN];		// This buffer holds the data as single read returns. 
												// In CompleteReadRequest, its data will be copied to string 'buffer'

	DWORD total_read_len;						// The total number of bytes we read from client. 
	DWORD Recv_flags;							// The flags for WSARecv, it must remain valid until the completion of WSARecv.

	SOCKET myListener;							// Linked to the server socket 
	SOCKET myClientSocket;

	int state;
	int Connection_ID;							// The connection ID for each instance

	// Params for client profile
	char *command;								// Command of the client
	string clientIP;							// client's IP

public:
	// Constructor
	Connection(SOCKET Listener, HANDLE IoPort, int Conn_ID);

	// Destructor
	~Connection();

	void IssueAccept();
	void CompleteAccept();

	void IssueReadRequest();
	void CompleteReadRequest(WSABUF DataToSend);

	void IssueSendData(WSABUF DataToSend);
	void CompleteSendData(WSABUF DataToSend);

	void IssueReset();
	void CompleteReset();

	/*
	The main handler for this specific connection
	Will be called by the worker thread when the iocp tells the thread that
	this connection's I/O operation is compelte
	*/
	void OnIoComplete(WSABUF DataToSend);

private:
	// ---------- Helper Functions ----------
	BOOL parseHeader(string *buf);
	
	WSABUF BuildResponseMsg(WSABUF input);

	void cleanBuffers();

	void printBuffer(string buf);

	void printConnectionID();

	void printClientProfile();

	void appendData(string *buffer, char *tempBuffer, int tempBufferLen);
};