#include "HoloNetwork.h"

HoloNetwork::HoloNetwork()
{
	string IpAddr = "192.168.1.2";
	int Port = 27015;
	int WorkerNum = 4;
	int ClientNum = 10;
	constructor_helper(IpAddr, Port, WorkerNum, ClientNum);
}

HoloNetwork::HoloNetwork(string IpAddr, int Port, int WorkerNum, int ClientNum) {
	constructor_helper(IpAddr, Port, WorkerNum, ClientNum);
}

void HoloNetwork::constructor_helper(string IpAddr, int Port, int WorkerNum, int ClientNum) {
	ServerIp = IpAddr;
	port = Port;
	MaxWorkerThreadNum = WorkerNum;
	MaxClientNum = ClientNum;
	ServerSocket = INVALID_SOCKET;
	IocpHandle = NULL;
	_logger = spdlog::stdout_color_mt("HoloNetwork");
}

HoloNetwork::~HoloNetwork()
{
}

void HoloNetwork::delete_fun_package(Package input) {
	delete[] input.content;
	input.content = NULL;
}

void HoloNetwork::RunServer() {
	DWORD WORKER_THREAD_COUNT = (DWORD)MaxWorkerThreadNum;		// This is equal to the MaxWorkerThreadNum. But becasue WinAPI requires DWORD type, we just give them a one.

	// Initialize the Microsoft Windows Sockets Library
	WSADATA wsaData;
	int err;	// variable to catch the error

	// Initialize Winsock
	err = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (err != 0) {
		printf("WSAStartup failed: %d\n", err);
		return;
	}

	// Create a I/O Completion port
	IocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, WORKER_THREAD_COUNT);
	if (IocpHandle == NULL) {
		//Error
		WSACleanup();
		return;
	}

	// Set up server socket
	SOCKET ServerSocket = SetupServer();
	if (ServerSocket == INVALID_SOCKET) {
		// Error
		WSACleanup();
		return;
	}

	// Associate the server socket with the I/O Completion Port
	CreateIoCompletionPort((HANDLE)ServerSocket, IocpHandle, COMPLETION_KEY_IO, 0);

	// Create worker threads
	TQueue<Package> tq_reference(1, delete_fun_package);		// By experiment, I find out that the smaller the capacity is, the less likely will the video have glitch. This behavior is coherent with the original mutex-lock design as the original design only have one frame buffer. I set the capacity to 1 here so that we will have the best video quality. Of cause, you can just use c++ <atomic> to achieve similar result without using TQueue, and that may have less overhead. It is a future work for anyone who is interested in. 
	for (int i = 0; i < MaxWorkerThreadNum; i++) {
		// Each worker thread will have a unique TQueue
		worker_tqs.push_back(tq_reference);		
		WorkerThreads.push_back(thread(&HoloNetwork::WorkerFunction, this, IocpHandle, i));
	}

	// Allocate an vector of connections pointer
	for (int i = 0; i < MaxClientNum; i++) {
		Connections.push_back(new Connection(ServerSocket, IocpHandle, i));
	}
}

void HoloNetwork::CloseServer() {
	// Post a 'quit' completion message for each worker thread
	for (size_t i = 0; i < MaxWorkerThreadNum; i++) {
		PostQueuedCompletionStatus(IocpHandle, 0, COMPLETION_KEY_SHUTDOWN, 0);
	}

	// Wait for all the worker threads
	for (auto &th : WorkerThreads) {
		th.join();
	}

	// Shut down the listener socket and close the I/O port
	shutdown(ServerSocket, SD_BOTH);
	closesocket(ServerSocket);
	CloseHandle(IocpHandle);

	// Delete connections
	for (size_t i = 0; i < MaxClientNum; i++) {
		delete Connections.at(i);
		Connections.at(i) = NULL;
	}

	// Clean up WSA
	WSACleanup();

	return;
}

void HoloNetwork::UpdateBuffer(char *dataIn, int dataLen) {
	if (dataLen < 0) {
		return;
	}
	
	// Send the latest data to each worker's TQueue
	for (int i = 0; i < MaxWorkerThreadNum; i++) {
		// Copy the dataIn
		Package new_pkg;
		new_pkg.content = new char[dataLen];
		for (int i = 0; i < dataLen; i++) {
			new_pkg.content[i] = dataIn[i];
		}
		new_pkg.length = dataLen;

		worker_tqs[i].push(new_pkg);
	}
}

SOCKET HoloNetwork::SetupServer() {
	int err;
	SOCKET Listener;

	// Set up Server socket
	int af = AF_INET;
	int socketType = SOCK_STREAM;
	Listener = WSASocket(af, socketType, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);		// Remember to close the socket
	if (Listener == INVALID_SOCKET) {
		// Error
		PRINT_WSAERROR("WSASOCKET failed with error");
		return Listener;
	}

	// Set socket port to be reuseable
	BOOL optval = TRUE;
	int optlen = sizeof(BOOL);
	err = setsockopt(Listener, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, optlen);
	if (err != 0) {
		PRINT_WSAERROR("setsockopt failed with error");
		closesocket(Listener);
		return INVALID_SOCKET;
	}

	// Bind the Server socket 
	sockaddr_in Addr;
	Addr.sin_family = af;
	Addr.sin_addr.s_addr = inet_addr(ServerIp.c_str());
	Addr.sin_port = htons(port);
	err = ::bind(Listener, (SOCKADDR *)&Addr, sizeof(Addr));
	if (err == SOCKET_ERROR) {
		// Error
		PRINT_WSAERROR("bind failed with error");

		int ErrorCode = WSAGetLastError();
		if (ErrorCode == 10049) {
			_logger->warn("Cannot assign the requested IP address: {0} \nDid you connect the PC to router?", ServerIp);
		}

		closesocket(Listener);
		return INVALID_SOCKET;
	}

	// Start listening
	err = listen(Listener, MaxClientNum);
	if (err == SOCKET_ERROR) {
		// Error
		PRINT_WSAERROR("listen failed with error");
		closesocket(Listener);
		return INVALID_SOCKET;
	}

	_logger->info("Server IP: {0}; Waiting for connection...", ServerIp);

	return Listener;
}

void HoloNetwork::WorkerFunction(HANDLE IoPort, int idx) {
	// Create a variable to hold the most recent frame data
	char *LocalData = NULL;
	int LocalDataLen = 0; 

	// Run the loop
	while (TRUE) {
		// Create variables to store the result of the iocp
		BOOL Status = 0;
		DWORD NumTransferred = 0;
		ULONG_PTR CompletionKey = 0;
		LPOVERLAPPED Overlapped_ptr = 0;

		Status = GetQueuedCompletionStatus(reinterpret_cast<HANDLE>(IoPort),
			&NumTransferred, &CompletionKey, &Overlapped_ptr, INFINITE);

		// Try to get the latest data of the NIR image
		Package empty_pkg;
		Package new_pkg = worker_tqs[idx].pop(empty_pkg);
		if (new_pkg.content != NULL) {
			// Replace the old data 
			delete[] LocalData;
			LocalData = new_pkg.content;
			LocalDataLen = new_pkg.length;
		}

		// Convert the overlapped pointer to Connection
		Connection *Conn_ptr = reinterpret_cast<Connection*>(Overlapped_ptr);

		if (FALSE == Status) {
			// An error occurred; reset connection
			if (Conn_ptr) {
				PRINT_WSAERROR("GetQueuedCompletionStatus() returns FALSE with error");
				//DebugLog("Restarting the connection.");
				// Reset connection
				Conn_ptr->IssueReset();
			}
		}
		else if (CompletionKey == COMPLETION_KEY_IO) {
			WSABUF DataToSend;
			if (LocalData == NULL) {
				DataToSend.len = 0;
				DataToSend.buf = (CHAR*)LocalData;
			}
			else {
				DataToSend.len = LocalDataLen;
				DataToSend.buf = (CHAR*)LocalData;
			}

			Conn_ptr->OnIoComplete(DataToSend);
		}
		else if (CompletionKey == COMPLETION_KEY_SHUTDOWN) {
			// Clean up and terminate the thread
			break;
		}
	}

	delete[] LocalData;
	LocalData = NULL;
}
