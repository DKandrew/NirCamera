#pragma once

#include "Common.h"
#include <Windows.h>
#include <string>
#include <fstream>		

using namespace std;

class XRayManager
{
public:
	XRayManager();
	/*
	string dirPath: The path to the image folder
	*/
	XRayManager(string dirPath); 
	~XRayManager();
	int GetTotalImageNum();
	WSABUF ReadImage(int idx);
	void UpdateImageDir(string dirPath);

private:
	// The directroy to the XRay Images
	// By default the directory is the current directory of the program
	string ImageDir;

	/*
	A general helper function for any kind of constructor
	*/
	void constructor_helper(string dirPath);

	int totalImageNum = 0;

};

