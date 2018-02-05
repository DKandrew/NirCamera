#include "Common.h"
#include "TQueue.h"
#include "NirImager.h"
#include "HoloNetwork.h"

// Include the OpenCV library  
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/core.hpp"
#include "opencv2/cudaimgproc.hpp"
#include "opencv2/cudawarping.hpp"
#include "opencv2/core/cuda.hpp"
#include "opencv2/cudaarithm.hpp"

// Include for Saving the data
#include "cpp/H5Cpp.h"
#include <time.h>
#include <ctime>

// Include parallel programming library
#include <mutex>
#include <thread>

#define FRAMES_PER_TRANSFER 4
#define NUMBER_BUFFER 5
#define BUFFER_SIZE 648*488*FRAMES_PER_TRANSFER
#define IMAGE_HEIGHT 488
#define IMAGE_WIDTH 648
#define IMAGE_SATURATION_THRESHOLD 0.5

using namespace std;
using namespace cv;

// Interthread Image Buffer
UINT16 *ImageBuffer[NUMBER_BUFFER];
UINT16 *SaveBuffer[NUMBER_BUFFER];
UINT16 *NetworkBuffer[NUMBER_BUFFER];

// TQueue for different thread
TQueue<UINT16*> *ProcessDatatq;
TQueue<UINT16*> *Savetq;
TQueue<cuda::GpuMat*> *Displaytq;
TQueue<cuda::GpuMat*> *Networktq;

static volatile int stop_running;

// States for read thread
// Connect: Connecting to the FPGA
// Working: the FPGA is connected and works normally
enum ReadThreadState { Connect, Working };
ReadThreadState readState = Connect;

// States for saving the data
enum SaveDataState { Idle, Setup, Saving, Complete };
SaveDataState saveState = Idle;

// Event signal for reconnecting the FPGA
HANDLE connect_FPGA_event = INVALID_HANDLE_VALUE;
// Will be true if user click buttom to change the exposure
bool request_change_exposure = false;

// Global variable for Slider GUI
int exposure_slider = 30;
int rgba_alpha_slider = 15;
const int exposure_slider_max = 100;
const int threshold_slider_max = 255;
const int rgba_alpha_slider_max = 15;

// The logger for out top-level process
std::shared_ptr<spdlog::logger> _logger;

// ----------- Read Thread -----------

/*
Thread function for reading data from Imager
Reading from FPGA is usually 1/120 s. Its slowness can be utilized for designing the TQueue.
*/
void ReadData() {
	NirImager imager;

	while (!stop_running) {
		switch (readState) {
		case Connect: {
			double exposure = 0.03;
			BOOL setup_success = imager.SetupImager(exposure);
			if (setup_success) {
				readState = Working;
			}
			else {
				_logger->warn("Set up Imager failed. \nPlease check the connection between PC and FPGA. Click \"Connect FPGA Imager\" button to reconnect the imager.");
				WaitForSingleObject(connect_FPGA_event, INFINITE);
				readState = Connect;
			}
			break;
		}
		case Working: {
			if (request_change_exposure) {
				// The minimum value of exposure slider is 5, it will not be visually shown in the GUI tho 
				if (exposure_slider < 5) {
					exposure_slider = 5;
				}
				double expValue = (double)exposure_slider / 1000.0;
				imager.changeExposure(expValue); // Make sure the ReadThread is not reading from FPGA, then set the NirImager exposure value

				request_change_exposure = false;
				_logger->info("Imager's exposure has adjusted.");
			}
			else {
				unsigned char *rdata = NULL;
				//rdata = generate_dummy_data(BUFFER_SIZE*2);	// get dummy data
				rdata = imager.readImagerData();

				UINT16 *ProcessDataBuf = NULL;
				UINT16 *SaveBuf = NULL;

				if (rdata != NULL) {
					// Data read is valid
					ProcessDataBuf = new UINT16[BUFFER_SIZE];
					SaveBuf = new UINT16[BUFFER_SIZE];
					for (int PixCount = 0; PixCount < BUFFER_SIZE; PixCount++) {
						int index = PixCount * 2;

						int lowBit = rdata[index];
						int highBit = rdata[index + 1];
						int origBit = rdata[index];

						ProcessDataBuf[PixCount] = origBit + (highBit << 8);
						SaveBuf[PixCount] = (highBit << 8) + lowBit;
					}

					// Feed the data to TQueue
					ProcessDatatq->push(ProcessDataBuf);
					Savetq->push(SaveBuf);

					delete[] rdata;
				}
				else {
					_logger->warn("ReadData(): Read from imager failed. Please reconnect the FPGA imager.");
					readState = Connect;
					WaitForSingleObject(connect_FPGA_event, INFINITE);
				}
			}
			break;
		}
		}
	}
}

// ----------- Process Data Thread -----------

int threshold_low_slider = 0;
int threshold_high_slider = 255;
BOOL RequestCalibration = FALSE;		// Variable indicates whether the user want to calibration  
BOOL RestoreCalibration = FALSE;

/*
Save the perspective transformation matrix into a file
*/
void SavePersTranMat(Mat PersTranMat) {
	FileStorage file("calibrationMat.xml", cv::FileStorage::WRITE);

	// Write to file!
	file << "PersTransMat" << PersTranMat;
	file.release();
}

/*
Load the perspective transformation matrix from a file
*/
bool LoadPersTranMat(Mat &PersTranMat) {
	FileStorage file;
	if (file.open("calibrationMat.xml", cv::FileStorage::READ)) {
		file["PersTransMat"] >> PersTranMat;
		file.release();
		return TRUE;
	}
	else {
		return FALSE;
	}
}

/*
Comparator for Circle Detection
*/
bool SortbyXaxis(const Point & a, const Point &b)
{
	return a.x < b.x;
}

/*
Perform dectection on the four calibration circles in the image
Return TRUE if calibration success, FALSE otherwise

Bug: what if the image is empty, first pointer is null
*/
bool CircleDetection(cuda::GpuMat OrigDisplay, Mat &PersTransMat) {
	Mat SrcGray;
	OrigDisplay.download(SrcGray);

	// Blur the image in order to reduce the noice from original image
	Mat BlurredDisplay;
	blur(SrcGray, BlurredDisplay, cv::Size(10, 10));	// blur matrix (10,10) is picked by experiments

														// thresh == 200 is picked by experiments
	int thresh = 200;
	const double MAX_BINARY_VALUE = 255;
	Mat ThresholdDisplay;

	// Perform type 0 threshold (Binary threshold)
	threshold(BlurredDisplay, ThresholdDisplay, thresh, MAX_BINARY_VALUE, 0);

	// Edge detection with canny 
	Mat CannyOutput;
	Canny(ThresholdDisplay, CannyOutput, thresh, thresh * 3, 3);	// threshold1 = thresh; threshold2 = thresh * 3 are picked by experiments

																	// Find contours
	std::vector<std::vector<cv::Point>> contours;
	std::vector<cv::Vec4i> hierarchy;
	findContours(CannyOutput, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, Point(0, 0));

	// If the detected contour points are less than 4, then this detection is invalid. Return Identity matrix 
	if (contours.size() < 4) {
		PersTransMat = Mat::eye(3, 3, CV_64FC1);
		return false;
	}

	std::vector<Moments> mu(contours.size());
	for (int i = 0; i < contours.size(); i++) {

		mu[i] = moments(contours[i], false);
	}

	// Find the mass centers of each circle (point); Note the getPerspectiveTransform accept Point2f but not Point2d
	std::vector<Point2f> mc(contours.size());
	for (int i = 0; i < contours.size(); i++) {
		mc[i] = Point2f(float(mu[i].m10 / mu[i].m00), float(mu[i].m01 / mu[i].m00));
	}

	std::vector<Point2f> OrigCoordiate(mc.begin(), next(mc.begin(), 4));
	std::sort(OrigCoordiate.begin(), OrigCoordiate.end(), SortbyXaxis);

	// Generate Perspective adjusted image
	std::vector<Point2f> AdjustCoordiate(4);
	AdjustCoordiate[0] = Point2f(0, IMAGE_HEIGHT);
	AdjustCoordiate[1] = Point2f(IMAGE_WIDTH / 4, 0);
	AdjustCoordiate[2] = Point2f(IMAGE_WIDTH / 4 * 3, 0);
	AdjustCoordiate[3] = Point2f(IMAGE_WIDTH, IMAGE_HEIGHT);

	// Calculate the transformation matrix
	PersTransMat = getPerspectiveTransform(OrigCoordiate, AdjustCoordiate);

	return true;
}

/*
Detect whether the image is saturated.
return true if the image is saturated.
*/
bool SaturationDetection(cuda::GpuMat Image) {
	Scalar PixelSum = cuda::sum(Image);
	if (PixelSum[0] > 255 * IMAGE_HEIGHT*IMAGE_WIDTH*IMAGE_SATURATION_THRESHOLD) {
		return true;
	}
	else {
		return false;
	}
}

/*
Process the image, calibrate it if necessary
The process image will send to other thread via a TQueue<cv::GpuMat>
*/
void ProcessImage() {
	Mat DisplayMat(IMAGE_HEIGHT, IMAGE_WIDTH, CV_16UC1);						// (16 bit per element)
	cuda::GpuMat DisplayMatGpu(IMAGE_HEIGHT, IMAGE_WIDTH, CV_16UC1);

	// ScaleDisplayGpu will have the calibrated image information
	cuda::GpuMat ScaleDisplayGpu(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1); 			// Scaled Image will have the customized max/min pixel value (8 bit per element)

	Mat PersTranMat;						// Perspective Transformation Matrix
	bool PersTranMatConstructed = false;	// If true then we need to calibrate the image

	Mat ThresholdImage(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1);						// Threshold image (8 bit per element)
	cuda::GpuMat ThresholdLowImageGpu(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1);
	cuda::GpuMat ThresholdHighImageGpu(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1);		// ThresholdHigh image will be the output image

	while (!stop_running) {
		// Get the image from readData thread
		UINT16 *ImageData = ProcessDatatq->pop(NULL);
		if (ImageData != NULL) {
			// ImageData contains FRAMES_PER_TRANSFER frames of picture, go through each of them
			for (int i = 0; i < FRAMES_PER_TRANSFER; i++) {
				int offset = i * IMAGE_HEIGHT * IMAGE_WIDTH;
				DisplayMat.data = (uchar*)(ImageData + offset);

				// Load the image into GPU
				DisplayMatGpu.upload(DisplayMat);

				// Adjust the pixel value in the image
				// The number 256 means we want to restain the pixel value in the range of (0,255). The nuber 24 is picked by experiment.
				DisplayMatGpu.convertTo(ScaleDisplayGpu, CV_8UC1, 24.0 / 256.0);

				if (RestoreCalibration) {
					RestoreCalibration = FALSE;		// Reset this variable
					RequestCalibration = TRUE;
					PersTranMatConstructed = LoadPersTranMat(PersTranMat);
					if (!PersTranMatConstructed) {
						cout << "cannot restore calibration!" << endl;
					}
				}
				else {
					// Check whether we need to do a calibration
					if (RequestCalibration && !PersTranMatConstructed) {
						// To understand this if ... else if ... statement, think about PersTranMatConstructed as a state
						// If it is in FALSE state and user RequestCalibration, then do (1)
						// If it is in TRUE state and user cancel RequestCalibration, the do (2)
						PersTranMatConstructed = CircleDetection(ScaleDisplayGpu, PersTranMat);
						SavePersTranMat(PersTranMat);	// Save new perspective transformation matrix into file
						if (!PersTranMatConstructed) {
							RequestCalibration = FALSE;
						}
					}
					else if (!RequestCalibration && PersTranMatConstructed) {
						PersTranMatConstructed = false;
					}
				}

				// Generate the threshold image
				if (!PersTranMatConstructed) {
					// Adjust the threshold in the scaled image, use type 3 threshold (threshold to zero)
					cuda::threshold(ScaleDisplayGpu, ThresholdLowImageGpu, threshold_low_slider, 255.0, 3);
					ThresholdLowImageGpu.convertTo(ThresholdHighImageGpu, CV_8UC1, 255.0 / threshold_high_slider);
				}
				else {
					cuda::GpuMat TransformDisplayGpu(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1);
					// Change the perspective of image based on calculated disparity
					cuda::warpPerspective(ScaleDisplayGpu, TransformDisplayGpu, PersTranMat, ScaleDisplayGpu.size());

					// Ajust the threshold based on the perspective-adjusted image, use type 3 threshold
					cuda::threshold(TransformDisplayGpu, ThresholdLowImageGpu, threshold_low_slider, 255.0, 3);
					ThresholdLowImageGpu.convertTo(ThresholdHighImageGpu, CV_8UC1, 255.0 / threshold_high_slider);
				}

				// Filter out the saturated images
				if (SaturationDetection(ThresholdHighImageGpu)) {
					continue;
				}

				cuda::GpuMat *OutputImage_display = new cuda::GpuMat(ThresholdHighImageGpu);
				cuda::GpuMat *OutputImage_network = new cuda::GpuMat(ThresholdHighImageGpu);

				// Output the process image
				Displaytq->push(OutputImage_display);
				Networktq->push(OutputImage_network);
			}
		}

		delete[] ImageData;
	}
}

// ----------- Display GUI Thread -----------

/*
This event (function) will be triggered when the user presses SetExposure button.
It will set the imager's exposure to the exposure_slider's value
*/
void SetExposureClick(int state, void* userdata) {
	CoutPrint("Set exposure button clicked");
	request_change_exposure = true;
}

/*
The callback function when "Save Data" button is clicked
*/
void SaveDataClick(int state, void* userdata) {
	CoutPrint("Saving button clicked");
	switch (saveState) {
	case Idle: {
		saveState = Setup;
		break;
	}
	case Saving: {
		saveState = Complete;
		break;
	}
	default:
		break;
	}
}

/*
The callback function that is called when the "Start Calibration" button is clicked
*/
void CalibrationClick(int state, void* userdata) {
	CoutPrint("calibration button clicked");
	RequestCalibration = TRUE;
}

/*
The callback function that is called when the "Reset Calibration" button is clicked
*/
void ResetCalibrationClick(int state, void* userdata) {
	CoutPrint("Reset calibration button clicked");
	RequestCalibration = FALSE;
	RestoreCalibration = FALSE;
}

/*
Called when the "Restore Calibration" button is clicked
*/
void RestoreCalibrationClick(int state, void* userdata) {
	CoutPrint("Load calibration button clicked");
	RestoreCalibration = TRUE;
}

/*
Called when user wants to connect the FPGA Imager
*/
void FPGAConnectClick(int state, void* userdata) {
	if (readState == Connect && connect_FPGA_event != INVALID_HANDLE_VALUE) {
		SetEvent(connect_FPGA_event);
	}
}

/*
Adjust the Jet colormap based on src Matrix and store the result in the dst Matrix
*/
void AdjustJet(Mat &src, Mat &dst) {
	applyColorMap(src, dst, COLORMAP_JET);
	for (int i = 0; i < IMAGE_HEIGHT; i++) {
		for (int j = 0; j < IMAGE_WIDTH; j++) {
			if (src.at<uchar>(i, j) == 0) {
				dst.at<Vec3b>(i, j) = Vec3b(0.0, 0.0, 0.0);
			}
		}
	}
}

/*
Create a GUI in the PC by using openCV's library and display the imager data
The GUI is created based on opencv's HighGUI
*/
void DisplayData() {
	// Using OpenCV window
	cv::String windowName("NIR Camera");

	// Create a threshold windows
	namedWindow(windowName, CV_WINDOW_AUTOSIZE);

	// Create buttons related to the camera
	cv::createButton("Set Exposure", SetExposureClick, NULL, CV_PUSH_BUTTON, 0);
	cv::createButton("Start Calibration", CalibrationClick, NULL, CV_PUSH_BUTTON, 0);
	cv::createButton("Restore Calibration", RestoreCalibrationClick, NULL, CV_PUSH_BUTTON, 0);
	cv::createButton("Reset Calibration", ResetCalibrationClick, NULL, CV_PUSH_BUTTON, 0);

	// Create Trackbars
	cv::String emptyStr;		// Use an empty string to help creating the trackbar (Otherwise by using "", createTrackbar() will segfault occuasionally)
	cv::createTrackbar("Exposure(ms)", emptyStr, &exposure_slider, exposure_slider_max);		// Experiment has shown that the 2nd input parameters of the cv::createTrackbar should not be a "". Otherwise it will cause segfault. So I use emptyStr here.
	cv::createTrackbar("Thres_low", emptyStr, &threshold_low_slider, threshold_slider_max);
	cv::createTrackbar("Thres_high", emptyStr, &threshold_high_slider, threshold_slider_max);
	cv::createTrackbar("Transparency", emptyStr, &rgba_alpha_slider, rgba_alpha_slider_max);

	cv::createButton("Save Data", SaveDataClick, NULL, CV_PUSH_BUTTON, 0);
	cv::createButton("Connect FPGA Imager", FPGAConnectClick, NULL, CV_PUSH_BUTTON, 0);

	bool closeWindow = false;
	while (!closeWindow) {
		cuda::GpuMat *InputImage = Displaytq->pop(NULL);

		if (InputImage != NULL) {
			// Image to be displayed (8 bit per element)
			Mat DisplayImage(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1);

			// Load the threshold image from GPU to CPU 
			InputImage->download(DisplayImage);

			// Get the jet image
			Mat jetImage;
			AdjustJet(DisplayImage, jetImage);

			// Display the jet image
			imshow(windowName, jetImage);
		}

		// After imshow is called, you have to call waitkey for at least some time (e.g. 5)
		char key_pressed = waitKey(5);

		// If User press q, then exist
		if (key_pressed == 'q') {
			closeWindow = true;
		}

		delete InputImage;
	}
}

// ----------- Network Thread -----------

/*
* Breaking i into sections and getting AR value (8 Bits in total)
*/
unsigned char GetColorAR(unsigned char i) {
	if (i == 0) {
		return 0;
	}

	unsigned char ret = 0xf0;
	//if pixel(8 bits) value is less than 96, then R = 0x0
	if (i < 96) {
		return ret;
	}

	if (i < 128) {
		ret += (unsigned char)((i - 95) * 4) / 16;
		return ret;
	}

	if (i < 159) {
		unsigned char x = 131;
		x += (i - 128) * 4;
		x /= 16;
		return (unsigned char)(ret + x);
	}

	if (i < 224) {
		return (unsigned char)0xff;
	}
	return 0;
}

/*
An overrided function for GetColorAR
This function accepts an input of new_alpha based on which it will construct the AR value
*/
unsigned char GetColorAR(unsigned char i, int new_alpha) {
	char a = (new_alpha & 0xff) << 4;
	return GetColorAR(i) & (0x0f) + a;
}

/*
* Breaking i into sections and getting G 8 bit
*/
unsigned char getG(unsigned char i)
{
	if (i < 32 || i > 222) {
		return 0;
	}
	if (i < 64) {
		return ((unsigned char)(i - 31) * 4);
	}
	if (i < 95) {
		return ((unsigned char)((i - 64) * 4) + 131);
	}
	if (i < 160) {
		return (unsigned char)(0xff);
	}

	unsigned char tmp = 0;
	if (i < 191) {
		tmp = 0xff;
		tmp -= (i - 159) * 4;
		return tmp;
	}
	if (i < 223) {
		tmp = 128;
		tmp -= (i - 191) * 4;
		return tmp;
	}

	return 0;
}

/*
* Breaking i into sections and getting B 8 bit
*/
unsigned char getB(unsigned char i)
{
	if (i > 158) {
		return 0;
	}

	unsigned char tmp = 0;
	if (i <= 30) {
		tmp = 131;
		tmp += (i) * 4;
		return tmp;
	}
	if (i < 96) {
		return 0xff;
	}
	if (i < 127) {
		tmp = 0xff;
		tmp -= (i - 95) * 4;
		return tmp;
	}

	if (i < 159) {
		tmp = 128;
		tmp -= (i - 127) * 4;
		return tmp;
	}
	return 0;
}

/*
* Breaking i into sections and getting GB value (8 Bits in total)
* Use 2 helper functions
*/
unsigned char GetColorGB(unsigned char i)
{
	unsigned char gr = getG(i);
	unsigned char bl = getB(i);
	gr /= 16;
	bl /= 16;
	unsigned char temp = gr << 4;
	temp += bl;
	return temp;
}

/*
Get data from ProcessImage thread and send it to HoloLens
*/
void RunNetwork() {
	// Constant parameters for this thread
	int DOWN_FACTOR = 2;
	int NETWORK_DATA_LEN = IMAGE_HEIGHT / DOWN_FACTOR * IMAGE_WIDTH / DOWN_FACTOR * 2;

	// Create a network object
	string ip_addr = "192.168.1.2";
	int port = 27015;
	int worker_thread_num = 4;
	int client_num = 10;
	HoloNetwork holo_network(ip_addr, port, worker_thread_num, client_num);

	// Run the server and update the data
	holo_network.RunServer();
	while (!stop_running) {
		// If new image is available, update the network's buffer
		cuda::GpuMat *InputImage = Networktq->pop(NULL);

		if (InputImage != NULL) {
			// Down Sample Image Variable (8 bit per element)
			Mat DownSample(IMAGE_HEIGHT / DOWN_FACTOR, IMAGE_WIDTH / DOWN_FACTOR, CV_8UC1);
			cuda::GpuMat DownSampleGpu(IMAGE_HEIGHT / DOWN_FACTOR, IMAGE_WIDTH / DOWN_FACTOR, CV_8UC1);
			cuda::resize(*InputImage, DownSampleGpu, cv::Size(0, 0), 1.0 / DOWN_FACTOR, 1.0 / DOWN_FACTOR, INTER_NEAREST);
			DownSampleGpu.download(DownSample);

			char *SendData = new char[NETWORK_DATA_LEN];
			for (int i = 0; i < (IMAGE_HEIGHT / DOWN_FACTOR * IMAGE_WIDTH / DOWN_FACTOR); i++) {
				int index = i * 2;
				SendData[index] = (char)GetColorGB(DownSample.data[i]);
				SendData[index + 1] = (char)GetColorAR(DownSample.data[i], rgba_alpha_slider);
			}

			// Update the data
			holo_network.UpdateBuffer(SendData, NETWORK_DATA_LEN);

			delete[] SendData;
		}

		delete InputImage;
	}

	holo_network.CloseServer();
}

// ----------- Save Image Thread -----------

//global variables for HDF5
#define SAVE_VIDEO_LENGTH 255
#define OutRank 3

char VideoName[SAVE_VIDEO_LENGTH];
hsize_t h5offset[3];
hsize_t h5size[3];

// vidFile and vidDset must be global variables (by experimentsssss)
hid_t vidFile;
hid_t vidDset;

/*
Save data into HDF5
This function will run the (saveState) state machine
*/
void SaveHDF5(UINT16 *buffer) {
	hsize_t nDims[OutRank] = { 0, IMAGE_HEIGHT, IMAGE_WIDTH };
	hsize_t maxDims[OutRank] = { H5S_UNLIMITED, IMAGE_HEIGHT, IMAGE_WIDTH };
	hsize_t chunkDims[3] = { 1, IMAGE_HEIGHT, IMAGE_WIDTH };
	hsize_t dimsExt[3] = { 1, IMAGE_HEIGHT, IMAGE_WIDTH };

	// General error checking status
	hid_t status;

	switch (saveState)
	{
	case Setup: {
		// Create a directory if it doesn't exist
		string VideoDir = "Video";
		if (!CreateDirectory(VideoDir.c_str(), NULL)) {
			if (ERROR_ALREADY_EXISTS != GetLastError()) {
				PRINT_WSAERROR("Create save data directory fails");
			}
		}

		// Generate video name based on current time 
		time_t t = time(0);
		struct tm now;
		localtime_s(&now, &t);
		strftime(VideoName, SAVE_VIDEO_LENGTH, string(VideoDir + "\\" + "video_%Y_%m_%e_%H_%M_%S.h5").c_str(), &now);

		// Clean offset and h5size
		h5offset[0] = 0;
		h5offset[1] = 0;
		h5offset[2] = 0;
		h5size[0] = 0;
		h5size[1] = IMAGE_HEIGHT;
		h5size[2] = IMAGE_WIDTH;

		hid_t prop = H5Pcreate(H5P_DATASET_CREATE);
		status = H5Pset_chunk(prop, OutRank, chunkDims);

		hid_t vidDspace = H5Screate_simple(OutRank, nDims, maxDims);
		vidFile = H5Fcreate(VideoName, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
		vidDset = H5Dcreate(vidFile, "data", H5T_STD_U16LE, vidDspace, H5P_DEFAULT, prop, H5P_DEFAULT);

		H5Pclose(prop);
		H5Sclose(vidDspace);

		CoutPrint("Now start saving...\n");
		saveState = Saving;
		break;
	}
	case Saving: {
		h5size[0]++;
		status = H5Dextend(vidDset, h5size);
		hid_t vidFspace = H5Dget_space(vidDset);
		hid_t vidMspace = H5Screate_simple(OutRank, dimsExt, NULL);
		status = H5Sselect_hyperslab(vidFspace, H5S_SELECT_SET, h5offset, NULL, dimsExt, NULL);
		status = H5Dwrite(vidDset, H5T_STD_U16LE, vidMspace, vidFspace, H5P_DEFAULT, (void *)buffer);

		H5Sclose(vidMspace);
		H5Sclose(vidFspace);		// Although vidFspace is created by H5Dget_space, it is closed by H5Sclose instead of H5Dclose (by experiments..)

		h5offset[0]++;
		break;
	}
	case Complete: {
		hsize_t attDims[] = { 1 };
		hsize_t attMaxDims[] = { 1 };
		hid_t bdfspace = H5Screate_simple(1, attDims, attMaxDims);
		hid_t bdmspace = H5Screate_simple(1, attDims, attMaxDims);

		hid_t vidAttrs = H5Gcreate(vidFile, "/attributes", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

		hid_t attSet = H5Dcreate(vidFile, "/attributes/exposure_time", H5T_STD_I32LE, bdfspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		status = H5Dwrite(attSet, H5T_STD_I32LE, bdmspace, bdfspace, H5P_DEFAULT, &exposure_slider);
		H5Dclose(attSet);

		attSet = H5Dcreate(vidFile, "/attributes/threshold_low_slider", H5T_STD_I32LE, bdfspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		status = H5Dwrite(attSet, H5T_STD_I32LE, bdmspace, bdfspace, H5P_DEFAULT, &threshold_low_slider);
		H5Dclose(attSet);

		attSet = H5Dcreate(vidFile, "/attributes/threshold_high_slider", H5T_STD_I32LE, bdfspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		status = H5Dwrite(attSet, H5T_STD_I32LE, bdmspace, bdfspace, H5P_DEFAULT, &threshold_high_slider);
		H5Dclose(attSet);

		H5Sclose(bdfspace);
		H5Sclose(bdmspace);
		H5Gclose(vidAttrs);
		H5Dclose(vidDset);

		// The file "vidFile" must be closed when the saving data completes otherwise you can't open the .h5 file
		H5Fclose(vidFile);

		printf("Save video %s complete!\n", VideoName);
		saveState = Idle;
		break;
	}
	default:
		break;
	}
}

/*
The thread function for saving the data into HDF5
*/
void SaveData() {
	while (!stop_running) {
		UINT16 *ImageData = Savetq->pop(NULL);

		if (ImageData != NULL) {
			// ImageData contains FRAMES_PER_TRANSFER frames of picture, go through each of them
			for (int i = 0; i < FRAMES_PER_TRANSFER; i++) {
				int offset = i * IMAGE_HEIGHT * IMAGE_WIDTH;
				UINT16 *SingleFrameData = ImageData + offset;
				SaveHDF5(SingleFrameData);
			}
			delete[] ImageData;
		}
	}
}

// ----------- Main Thread -----------

/*
TQueue delete function for UINT16 array
*/
void delete_fun_UINT16_ptr(UINT16* input) {
	delete[] input;
	input = NULL;
}

/*
TQueue delete function for GpuMat
*/
void delete_fun_GpuMat_ptr(cuda::GpuMat* input) {
	delete input;
}

int main() {
	_logger = spdlog::stdout_color_mt("Main");

	// Set the delay time before exit the program (in ms)
	int ExitDelay = 1500;

	// A event that connects with the read thread 
	// This event is set to be auto-reset (i.e. 2nd input is set as false). Therefore, every time a waitSingleObject() catch the event, this event will be automatically reset to unsignaled.
	connect_FPGA_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	// Create tqueue for each thread
	int tqCapacity = 10;
	ProcessDatatq = new TQueue<UINT16*>(tqCapacity, delete_fun_UINT16_ptr);
	Savetq = new TQueue<UINT16*>(tqCapacity, delete_fun_UINT16_ptr);
	Displaytq = new TQueue<cuda::GpuMat*>(tqCapacity, delete_fun_GpuMat_ptr);
	Networktq = new TQueue<cuda::GpuMat*>(tqCapacity, delete_fun_GpuMat_ptr);
	

	// Spawn threads
	thread ReadThread(ReadData);
	thread ProcessImageThread(ProcessImage);
	thread DisplayThread(DisplayData);
	thread NetworkThread(RunNetwork);
	thread SaveDataThread(SaveData);

	// Join Display Thread
	DisplayThread.join();

	// Close data saving process (if any)
	if (saveState != Idle) {
		saveState = Complete;
	}

	stop_running = 1;

	// In case ReadThread is still waiting for the connect_FPGA_event
	SetEvent(connect_FPGA_event);

	// Join the rest of threads
	ReadThread.join();
	ProcessImageThread.join();
	NetworkThread.join();
	SaveDataThread.join();

	delete ProcessDatatq;
	delete Displaytq;
	delete Networktq;
	delete Savetq;

	CloseHandle(connect_FPGA_event);
	connect_FPGA_event = INVALID_HANDLE_VALUE;

	// Exist the program
	_logger->info("Existing Main...");

	Sleep(ExitDelay);

	return 0;
}
