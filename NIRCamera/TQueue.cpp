#include "TQueue.h"

template<class T>
TQueue<T>::TQueue(){
	// Default capacity is 10, T_delete_fun = NULL
	general_constructor(10, NULL);
}

template<class T>
TQueue<T>::TQueue(int cap){
	general_constructor(cap, NULL);
}

template<class T>
TQueue<T>::TQueue(int cap, void(*delete_fun)(T)){
	general_constructor(cap, delete_fun);
}

template<class T>
TQueue<T>::TQueue(const TQueue& other){
	general_constructor(other.get_capacity(), other.get_delete_function());
	tolerance = other.get_tolerance();
}

template<class T>
TQueue<T>::~TQueue(){
	// Clean all the elements in the cell
	for (int i = 0; i < capacity; i++){
		Cell *curr_cell = cell_array[i].exchange(NULL);
		if (curr_cell != NULL){
			clean_cell(*curr_cell);
			delete curr_cell;
			curr_cell = NULL;
		}
	}

	delete[] cell_array;
	cell_array = NULL;

	T_delete_fun = NULL;
}

// Could do: copy constructor
// template<class T>
// TQueue<T>& TQueue<T>::operator=(const TQueue& other){
// 	// TODO: how does the copy operator works
// 	// delete?
// 	// more?
// 	copy_help(other);
// 	return *this;
// }

template<class T>
void TQueue<T>::general_constructor(int cap, void(*delete_fun)(T)){
	if (!(cap > 0)){
		capacity = 10;
	}
	else{
		capacity = cap;
	}

	T_delete_fun = delete_fun;

	// Initialize cell_array and set all its elements to be NULL
	cell_array = new atomic<Cell*>[capacity];
	for(int i = 0; i < capacity; i++){
		cell_array[i] = NULL; 
	}

	global_timeStamp = 0;	// Set the global_timeStamp to zero
	write_idx = 0;
	read_idx = 0; 
	tolerance = 0; 
}

template<class T>
void TQueue<T>::copy_help(const TQueue& other){
	int other_cap = other.get_capacity();
	capacity = other_cap;
	T_delete_fun = other.get_delete_function();
}

template<class T>
void TQueue<T>::push(T &input){

	global_timeStamp++;

	// Build a new cell
	Cell *new_cell = new Cell();
	new_cell->my_timeStamp = global_timeStamp;
	new_cell->my_t = input;

	// Push the new cell into the cell_array
	Cell *old_cell = cell_array[write_idx].exchange(new_cell);
	
	// Clean up the old_cell
	if (old_cell != NULL){
		clean_cell(*old_cell);
		delete old_cell;
		old_cell = NULL;
	}

	// write_idx points to the next writable cell
	write_idx = (write_idx + 1) % capacity;
}

template<class T>
T TQueue<T>::pop(const T &invalid_output){

	Cell *my_cell = NULL; 	
	while(true){
		// Read a cell from cell_array, replace it with NULL
		my_cell = cell_array[read_idx].exchange(NULL);

		// Check if this cell is acceptable
		// - if my_cell = NULL, pop() should terminate and return invalid_output
		// - if the time_stamp is not acceptable, delete it and read the next one
		if (my_cell == NULL){
			return invalid_output;
		}
		else {
			unsigned long curr_ts = global_timeStamp;	// Make a local reference to the global_timeStamp in case writer increments it
			if (curr_ts < tolerance) {					// Where global_timeStamp is smaller than the tolerance, my_cell is valid for sure
				break;
			}
			else if (my_cell->my_timeStamp >= (curr_ts - tolerance)){	// my_cell is within the tolerance time range, accept it
				break;
			}
			else{
				// current cell is not acceptable, delete it
				clean_cell(*my_cell);
				delete my_cell;
				my_cell = NULL;

				// Increment the read_idx
				read_idx = (read_idx + 1) % capacity;
			}
		}
	}

	// Next read will read the next available cell 
	read_idx = (read_idx + 1) % capacity;

	// my_cell is the valid data
	T retval = my_cell->my_t;
	delete my_cell;		// clean my_cell except my_t
	my_cell = NULL;
	return retval;
}

template<class T>
void TQueue<T>::clean_cell(Cell &input){
	if (T_delete_fun != NULL){
		T_delete_fun(input.my_t);
	}
}

template<class T>
int TQueue<T>::get_capacity() const {
	return capacity;
}

template<class T>
typename TQueue<T>::delete_fun TQueue<T>::get_delete_function() const {
	return T_delete_fun;
}

template<class T>
void TQueue<T>::set_tolerance(const unsigned long new_tolerance){
	tolerance = new_tolerance;
}

template<class T>
unsigned long TQueue<T>::get_tolerance() const {
	return tolerance;
}
