// In the .h file for template, because we want to include the .cpp
// (So we can put the defintion into cpp), it will cause double inlcude of 
// header file. Adding the ifndef ... define is a must here
// NOTE: when using this template, you only need to include this .h file. Don't include to the .cpp file
#ifndef TQUEUE_H
#define TQUEUE_H

#include <atomic>

using namespace std;

// The idea of this class is to design a data structure that provides the minimum latency
// TQueue has an internal data structure to keep track of the time stampe of the data
// and when pop, it will try to fetch the latest data. 
// Although it is designed to be be thread-safe, but it assumes that there are only one writer
// and one reader(they can be different threads).
template<class T>
class TQueue
{
public:	// public parameter
	typedef void (*delete_fun)(T);		// Define the function pointer of delete function 

public:
	// TODO
	// Add an operator[] (will compromise this class's multithreading feature)
	
	TQueue();

	// If T is not a pointer, or user doesn't want the TQueue to delete the data, he/she can just passing the capacity
	// Input:
	// --cap: the capacity, must be an integer greater than 0
	TQueue(int cap);

	// This consturctor asks the caller to provide the delete function for T 
	// so that if the time stampe check finds out the read data is out-of-data
	// it can delete the T internally by calling the delete function
	// Input:
	// --delete_fun: the format of delete function: void delete_fun(T input);
	TQueue(int cap, delete_fun);

	// Copied item: capacity, delete function pointer 
	TQueue(const TQueue& other);	
	
	~TQueue();

	// COULD DO:
	// TQueue& operator=(const TQueue& other);

	// Push input into the queue
	// Every time it push a new element into the queue, it will atomically swap out the old data (and delete it if needed)
	void push(T &input);

	// Pop out the latest data (as long as the data time frame is inside the tolerance, the data will be returned.)
	// In the tolerance range, the sequence of the data is not guaranteed;
	// For example, assume current we have data 995, 996, ..., 1001; and tolerance = 5 pop() may return 1001 first, then return 996, 997, ... 
	// or it could return 997 first, then 998, 999, ..., 1001, 996, ...
	// If the TQueue is empty (or any error occurs) pop() will return the invalid_output provided by user
	T pop(const T &invalid_output);

	// Immediately read the next available block in the TQueue
	// Return NULL means the TQueue is empty
	// (If it is not empty, should I report the current time stamp?)
	// T immediate_read();

	// Return the capacity of this TQueue
	int get_capacity() const;

	// Return the function pointer of delete function
	delete_fun get_delete_function() const;

	// Adjust the tolerance of TQueue
	void set_tolerance(const unsigned long new_tolerance);
	unsigned long get_tolerance() const;

private:
	// Cell is the unit of the internal array, each of them stores a data T and a timestamp
	class Cell{
	public:
		unsigned long my_timeStamp;
		T my_t;
	};

	atomic<Cell*> *cell_array;					// An array of atomic<Cell*>, it should be atomic<Cell*> so that writer and reader cannot access to the same data at same time. Using Cell* then every operation of atomic.exchange is just 8 bytes, a very small and constant number
	int capacity = 10;	// The capacity of the internal array
	unsigned long global_timeStamp = 0; 		// unsigned long is guarantee to be no less than the sizeof(int), however, the exact size is determined by the hardware architecture
												// Ideally, global_timeStampe is the maximum time stamp read thread can see
	int write_idx = 0;	// The index of cell_array where write should store
	int read_idx = 0; 	// The index of cell_array where we should read
	unsigned long tolerance = 0; 				// The max amount of delayed frame that pop() can tolerate
												// For example, if frame = 3 and current frame = 11. Then the acceptable frames are 11, 10, 9, 8

	// Create the delete function of T, by default it is NULL
	// The required format is:
	// void function_name(T input){...}
	void (*T_delete_fun)(T);

	// A general constructor that will initialize the internal parameters for this class
	void general_constructor(int cap, delete_fun);

	// A helper function for copy constructor
	void copy_help(const TQueue& other);

	// Clean the cell
	void clean_cell(Cell &input);

};

#include "TQueue.cpp"

#endif
