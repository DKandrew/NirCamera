#include "Connection.h"

Connection::Connection(SOCKET Listener, HANDLE IoPort, int Conn_ID) {	
	// Assign Conn_ID to this instance
	Connection_ID = Conn_ID;

	// Clean up all the buffers
	cleanBuffers();

	// Set up params
	buffer = new string();
	Buffer_wsa.len = 0;
	Buffer_wsa.buf = NULL;
	total_read_len = 0;
	command = NULL;

	// AcceptEx requires the the client socket must be created beforehand.
	// This minor annoyance can help server handle many short-lived connections. 
	myClientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	// Associate Listener to myListener 
	myListener = Listener;

	// Associate the client socket with the I/O Completion Port.
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(myClientSocket), IoPort, COMPLETION_KEY_IO, 0);

	IssueAccept();
}


Connection::~Connection() {
	shutdown(myClientSocket, SD_BOTH);
	closesocket(myClientSocket);

	// Clean dynamic memory
	delete buffer;
	buffer = NULL;
}

/*
This function will issue an asynchronous accept
*/
void Connection::IssueAccept() {
	state = WAIT_ACCEPT;
	DWORD ReceiveLen = 0; // This gets thrown away, but must be passed.
	// The forth input of AcceptEx should be set to zero so acceptEx completes as soon as a connection arrives
	AcceptEx(myListener, myClientSocket, AcceptBuffer, 0, Accept_Address_Length,
		Accept_Address_Length, &ReceiveLen, this);
}

/*
Complete the accept
*/
void Connection::CompleteAccept() {
	// Find the remote client's IP address
	sockaddr *pLocal = NULL, *pRemote = NULL;
	int nLocal = 0, nRemote = 0;
	GetAcceptExSockaddrs(AcceptBuffer, 0, Accept_Address_Length, Accept_Address_Length,
		&pLocal, &nLocal, &pRemote, &nRemote);

	// Record client's IP address
	char s[INET_ADDRSTRLEN];		// Required by inet_ntop, its size should be at least INET_ADDRSTRLEN, here we give a size of INET6_ADDRSTRLEN which is way sufficiently enough
	sockaddr_in *apple = (sockaddr_in*)pRemote;
	inet_ntop(AF_INET, &(apple->sin_addr), s, sizeof(s));
	clientIP = string(s);

	//printf("Connection %d accepts a connection from %s\n", Connection_ID, s);

	IssueReadRequest(); 
}

/*
Issue an asynchronous read for the client request
*/
void Connection::IssueReadRequest() {
	state = WAIT_READREQUEST;
	cleanBuffers();

	Buffer_wsa.len = READ_BUFFER_LEN;
	Buffer_wsa.buf = tempReadBuffer;
	DWORD BufferCount = 1;
	Recv_flags = 0;

	// WSARecv will not show complete until 
	// 1, incoming data is placed into the buffers 
	// 2, or the connection is closed, 
	// 3, or the internally buffered data is exhausted
	// Note: IpNumberOfBytesRecvd should be NULL if the lpOverlapped is not NULL
	int err = WSARecv(myClientSocket, &Buffer_wsa, BufferCount, NULL, &Recv_flags, this, NULL);
	if (err == SOCKET_ERROR) {
		if (WSAGetLastError() != WSA_IO_PENDING) {
			PRINT_WSAERROR("WSARecv error");
			// Reset
			IssueReset();
		}
	}
}

/*
Complete the Read Request
*/
void Connection::CompleteReadRequest(WSABUF DataToSend) {
	// Get the total number of bytes read from this I/O
	DWORD cbTransfer = 0;
	DWORD dwFlags = 0;
	
	BOOL err = WSAGetOverlappedResult(myClientSocket, this, &cbTransfer, TRUE, &dwFlags);
	if (err == FALSE) {
		PRINT_WSAERROR("WSAGetOverlappedResult failed with error");
		IssueReset();
		return;
	}

	// Append Read Data
	total_read_len += cbTransfer;
	appendData(buffer, Buffer_wsa.buf, cbTransfer);

	// Check whether the client request is valid
	BOOL validRequest = parseHeader(buffer);

	// Print Debug info
	if (validRequest) {
		printConnectionID();
		printf("Client[%s] request is %s\n", clientIP.c_str(), command);
	}
	else {
		printConnectionID();
		printf("Client[%s] request is invalid\n", clientIP.c_str());
	}

	// Because TCP protocol will gaurantee the message from client is comprehensive,
	// we here can just assume that client complete sending the request (or reach the MAX_HEADER_LEN)
	IssueSendData(DataToSend);
}

void Connection::IssueSendData(WSABUF DataToSend) {
	// You can consider using stringstream if the data you send include non-char data like int or other stuffs
	state = WAIT_SENDDATA;
	cleanBuffers();

	// Build the sending message (Remember to free the buffer after use)
	Buffer_wsa = BuildResponseMsg(DataToSend);
	DWORD BufferCount = 1;
	DWORD Flag = 0;

	int err = WSASend(myClientSocket, &Buffer_wsa, BufferCount, NULL, Flag, this, NULL);
	if (err == SOCKET_ERROR) {
		if (WSAGetLastError() != WSA_IO_PENDING) {
			PRINT_WSAERROR("WSASend error");
			IssueReset();
		}
	}
}

void Connection::CompleteSendData(WSABUF DataToSend) {
	// Delete the local send buffer
	delete[] Buffer_wsa.buf;
	Buffer_wsa.len = 0;
	Buffer_wsa.buf = NULL;

	if (command == COMM_STREAM) {
		// Keep sending stream data to client
		IssueSendData(DataToSend);
	}
	else {
		IssueReset();
	}
}

void Connection::IssueReset() {
	state = WAIT_RESET;

	// Disconnect the client socket and mark it as reuse for the new connection
	TransmitFile(myClientSocket, 0, 0, 0, this, 0,
		TF_DISCONNECT | TF_REUSE_SOCKET);
}

/*
This function will clean up everything and issue a new accept()
*/
void Connection::CompleteReset() {
	cleanBuffers();

	// Clean up the buffer memory
	buffer->clear();
	total_read_len = 0;

	// Clean up client profile
	command = NULL;

	IssueAccept();

	printf("========Connection %d Over========\n", Connection_ID);
}

/*
The main handler for this specific connection
Will be called by the worker thread when the iocp tells the thread that
this connection's I/O operation is compelte

This function must make a deep copy of the DataToSend
*/
void Connection::OnIoComplete(WSABUF DataToSend) {
	switch (state)
	{
	case WAIT_ACCEPT:
		CompleteAccept();
		break;

	case WAIT_READREQUEST:
		CompleteReadRequest(DataToSend);
		break;

	case WAIT_SENDDATA:
		CompleteSendData(DataToSend);
		break;

	case WAIT_RESET:
		CompleteReset();
		break;
	}
}

// ---------- Helper Functions ----------

/*
This function will check whether the client request is valid.
After the function is call, the client profile will be updated
Input
string *buf: the data we want to check if it is valid or not
Return
BOOL:Return true if client's request is valid and false otherwise
*/
BOOL Connection::parseHeader(string *buf) {
	// Command format:
	// [Command]\n[fileSize][Content]

	string refStr;

	// Check if the command is STREAM
	refStr.clear();
	refStr += COMM_STREAM;
	refStr += COMM_EOF;

	if (buf->length() >= refStr.length() &&
		buf->compare(0, refStr.length(), refStr.c_str()) == 0) {
		command = COMM_STREAM;
		return TRUE;
	}

	// Check if the command is GET XRAY TOTALNUM
	// Valid request: "GET XRAY TOTALNUM\n"
	refStr.clear();
	refStr += COMM_GET_XRAY_TOTALNUM;
	refStr += COMM_EOF;

	if (buf->length() >= refStr.length() &&
		buf->compare(0, refStr.length(), refStr.c_str()) == 0) {
		command = COMM_GET_XRAY_TOTALNUM;
		return TRUE;
	}

	// Check if the command is GET XRAY
	// Valid request: "GET XRAY\n[Index]\n"
	refStr.clear();
	refStr += COMM_GET_XRAY;
	refStr += COMM_EOF;

	int requireLen = refStr.length() + sizeof(UINT) + 1;
	if (buf->length() >= requireLen &&
		buf->compare(0, refStr.length(), refStr.c_str()) == 0 &&		// Check the header 
		buf->compare(requireLen - 1, 1, COMM_EOF) == 0					// Check the ending "\n"
		)
	{
		// Get the index
		int *p = (int*)(buf->c_str() + refStr.length());
		XRayIndex = *p;

		// Build the command
		command = COMM_GET_XRAY;
		return TRUE;
	}

	return FALSE;
}

/*
This function will build up the response message server send to client.
If the client request is invalid, it will generate error message.
If the client request is valid, it will generate ok message with appropriate data client wants

Input
	string *buf: a string pointer will the message will be stored
*/
WSABUF Connection::BuildResponseMsg(WSABUF input) {
	// Use buf to hold the header message
	string buf;
	WSABUF msg;

	if (command == NULL) {
		// Invalid client request, send error info
		buf += COMM_ERROR;
		buf += COMM_EOF;

		char *result = new char[buf.length()];
		for (int i = 0; i < buf.length(); i++) {
			result[i] = buf[i];
		}

		msg.len = ULONG(buf.length());
		msg.buf = (CHAR*)result;
	}
	else if (command == COMM_STREAM) {
		// For STREAM, don't send the header OK\n
		char *result = new char[input.len];
		for (int i = 0; i < int(input.len); i++) {
			result[i] = input.buf[i];
		}

		msg.len = ULONG(input.len);
		msg.buf = (CHAR*)result;
	}
	else if (command == COMM_GET_XRAY_TOTALNUM) {
		// Valid request
		buf += COMM_OK;
		buf += COMM_EOF;

		// Build message "OK\n[sizeof(int)]\n"
		XRayManager xrm(XRayImagePath);
		int totalNum = xrm.GetTotalImageNum();
		buf.append((char*)&totalNum, sizeof(int));

		buf += COMM_EOF;

		// Copy data into result
		char *result = new char[buf.length()];
		for (int i = 0; i < buf.length(); i++) {
			result[i] = buf[i];
		}
		
		msg.len = ULONG(buf.length());
		msg.buf = (CHAR*)result;
	}
	else if (command == COMM_GET_XRAY) {
		// Check if the required idx is valid
		// sizeof(int) == sizeof(ULONG); sizeof(size_t) == 8;
		XRayManager xrm(XRayImagePath);
		WSABUF imgData = xrm.ReadImage(XRayIndex);
		ULONG fileSize = imgData.len;
		if (imgData.len == 0) {
			// Invalid idx/Image is not available
			// Build message "ERROR\n[ERROR_INVALID_INDEX]\n
			buf += COMM_ERROR;
			buf += COMM_EOF;
			buf += COMM_ERR_INVALID_INDEX;
			buf += COMM_EOF;
		}
		else {
			// Valid request
			// Build message "OK\n[sizeof(ULONG)][fileData]"
			buf += COMM_OK;
			buf += COMM_EOF;
			// Append the ULONG into the buf
			buf.append((char*)&fileSize, sizeof(fileSize));
		}

		// Copy data into result
		char *result = new char[buf.length() + fileSize];
		for (int i = 0; i < buf.length(); i++) {
			result[i] = buf[i];
		}
		int offset = buf.length();
		for (int i = 0; i < int(imgData.len); i++) {
			result[i + offset] = imgData.buf[i];
		}

		msg.len = ULONG(buf.length() + fileSize);
		msg.buf = (CHAR*)result;

		// Delete the XRay image
		delete[] imgData.buf;
		imgData.buf = NULL;
		imgData.len = 0;
	}
	else {
		// Valid request
		buf += COMM_OK;
		buf += COMM_EOF;

		char *result = new char[buf.length() + input.len];

		for (int i = 0; i < buf.length(); i++) {
			result[i] = buf[i];
		}
		for (int i = 0; i < int(input.len); i++) {
			result[i + buf.length()] = input.buf[i];
		}

		msg.len = ULONG(buf.length() + input.len);
		msg.buf = (CHAR*)result;
	}

	return msg;
}

/*
Clean up all the buffers in this class 
*/
void Connection::cleanBuffers() {
	ZeroMemory(AcceptBuffer, sizeof(AcceptBuffer));
	ZeroMemory(tempReadBuffer, sizeof(tempReadBuffer));
}

/*
Print out the content of the buffer
*/
void Connection::printBuffer(string buf) {
	printf("-----Buffer Content-----\n");
	for (int i = 0; i < buf.length(); i++) {
		printf("%c ", buf.at(i));
	}
	printf("\n------------------------\n");
}

void Connection::printConnectionID() {
	printf("Connection %d::", Connection_ID);
}

/*
Print out the client's profile
*/
void Connection::printClientProfile() {
	printf("Connection ID: %d\n", Connection_ID);
	printf("Client request is %s\n", command);
}

/*
This function appends [tempBufferLen] number of data in the tempBuffer and push them into the
vector<char> buffer. Assuming that the size of tempBuffer is greater than tempBufferLen.
Input:
	string *buffer: a string buffer that store all the data in the lifetime of this connection.
		It must be a pointer instead of a string otherwise C++ will make a local copy of the string.
*/
void Connection::appendData(string *buffer, char *tempBuffer, int tempBufferLen) {
	buffer->append(tempBuffer, tempBufferLen);
	
	if (buffer->length() != total_read_len) {
		DebugLog("appendData Error!");
		printf("buffer length: %zu. total_read_len: %d\n", buffer->length(), total_read_len);
	}
}
