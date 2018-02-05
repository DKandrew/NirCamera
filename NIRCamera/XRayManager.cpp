#include "XRayManager.h"

XRayManager::XRayManager()
{
	constructor_helper(".");
}

XRayManager::XRayManager(string dirPath)
{
	constructor_helper(dirPath);
}

void XRayManager::constructor_helper(string dirPath) {
	ImageDir = dirPath;
	totalImageNum = GetTotalImageNum();
}

XRayManager::~XRayManager()
{
}

/*
Get the total number of images in the directory
*/
int XRayManager::GetTotalImageNum() {
	int counter = 0;
	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	// Local reference to the directory
	// Append '\\*' to the directory name
	string dir = ImageDir + "\\*";

	// Start iterating over the files in the path directory.
	hFind = FindFirstFile(dir.c_str(), &ffd);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do // Managed to locate and create an handle to that folder.
		{
			if (ffd.dwFileAttributes != FILE_ATTRIBUTE_DIRECTORY) {
				// Current file is not a directory
				counter++;
			}
		} while (FindNextFile(hFind, &ffd) == TRUE);
		FindClose(hFind);
	}
	else {
		printf("XRayManager::GetTotalImageNum - Failed to find directory: %s\n", ImageDir.c_str());
		counter = -1;
	}

	return counter;
}

/*
Given the index of image, read the image

Return: 
a WSABUF buffer with len = the length of the file, and buf = file content
If any error occurs, buffer len = 0 and buf is undefined
*/
WSABUF XRayManager::ReadImage(int idx) {
	WSABUF retval;

	// Check if the idx is less than the maximum image number (0<idx<maxNumber)
	if (idx < 1 || idx > totalImageNum) {
		cout << "XRayManager::ReadImage() - Invalid Input!\n";
		retval.buf = NULL; retval.len = 0;
		return retval;
	}

	// Set the image suffix (i.e format)
	string imageSuffix = ".jpg";
	string fileName = ImageDir + "/" + std::to_string(idx) + imageSuffix;
	
	// Open the file
	ifstream imageFile;
	// [Guess] Open the file in the binary mode, otherwise we may suffer some formatting transformations
	imageFile.open(fileName, ios::binary);

	// Check if the file open successfully
	if (!imageFile.is_open()) {
		retval.buf = NULL; retval.len = 0;
		return retval;
	}

	// Find the size of the file
	streampos fbegin, fend;
	fbegin = imageFile.tellg();
	imageFile.seekg(0, ios::end);		// seekg() moves the get pointer whereas seekp() moves the put pointer
	fend = imageFile.tellg();
	// Converting the streampos to ULONG then we get the size
	ULONG fsize = (ULONG)(fend - fbegin);
	// Reset the get pointer position to zero 
	imageFile.seekg(0, ios::beg);

	// Read the file
	char *fdata = new char[fsize];
	imageFile.read(fdata, fsize);
	if (imageFile) {
		//DebugLog("XRayManager::ReadImage():Read successfully");
	}
	else {
		cout << "XRayManager::ReadImage() - Read file failed! " << "Only read " << imageFile.gcount() << endl;
		delete[] fdata;
		fdata = NULL;
		fsize = 0;
	}
	
	// Close the file
	imageFile.close();	

	retval.buf = fdata;
	retval.len = fsize;
	return retval;
}

/*
Change the image dir
*/
void XRayManager::UpdateImageDir(string dirPath) {
	ImageDir = dirPath;
	totalImageNum = GetTotalImageNum();
}