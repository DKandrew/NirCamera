#pragma once
#include "Connection.h"
#include "Common.h"
#include "TQueue.h"

#include <string>
#include <thread>
#include <vector>
#include <mutex>

using namespace std;

/*
A class that handle the communication between PC and HoloLens
*/
class HoloNetwork
{
public:
	HoloNetwork();

	/*
	A more detailed constructor
	IpAddr: the IP address of the server
	Port: the port that the server will listen to
	WorkerNum: the number of worker threads that will be created
	ClientNum: the maximum number of clients that this network can handle concurrently
	*/
	HoloNetwork(string IpAddr, int Port, int WorkerNum, int ClientNum);
	~HoloNetwork();

	void RunServer();

	/*
	Update the internal FramePtr to the one pointed by dataIn. (It will perform a deep copy. Hence, it is user's responsibility to clean up dataIn)
	dataIn: the data to be copied
	dataLen: the length of dataIn that will be copied
	*/
	void UpdateBuffer(char *dataIn, int dataLen);

	void CloseServer();

	/*
	thread function that handles the connections
	idx: use this index to get the corresponding TQueue in the worker_tqs
	*/
	void WorkerFunction(HANDLE IoPort, int idx);	// The worker function is put in public otherwise we cannot thread it. (Maybe?This is based on my memory.)

private:
	// A class for the data transportation between main thread and worker threads
	class Package {
	public:
		int length;
		char *content;
		Package() {
			length = 0; content = NULL;
		};
	};

	std::shared_ptr<spdlog::logger> _logger;

	string ServerIp; 
	int port;
	int MaxWorkerThreadNum;
	int MaxClientNum; 
	SOCKET ServerSocket;
	HANDLE IocpHandle;		// Handle for IOCP

	// Store all the worker threads
	vector<thread> WorkerThreads;
	// Store all the connections
	vector<Connection*> Connections;

	void constructor_helper(string IpAddr, int Port, int WorkerNum, int ClientNum);

	/*
	This function will Setup the Server socket, complete the bind and listen procedure and
	return the SOCKET back to caller. If there is any error occurs, INVALID_SOCKET will be returned
	Input:
	port: The port number that user want the socket to listen to
	*/
	SOCKET SetupServer();

	//Vector of TQueue
	vector<TQueue<Package>> worker_tqs; 

	//Delete function for TQueue
	//The function is declared as static so it does not rely on a object to create this function
	//See: https://stackoverflow.com/questions/12662891/passing-a-member-function-as-an-argument-in-c
	static void delete_fun_package(Package input);
};
