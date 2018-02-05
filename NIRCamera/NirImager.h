#pragma once

#include <iostream>
#include <Windows.h>
// Copy the okFrontPanelDLL.h and okFrontPanel.lib to 
// the same directory as this file 
#include "okFrontPanelDLL.h"
#include "spdlog/spdlog.h"

// Define HDL bit file 
#define XILINX_CONFIGURATION_FILE  "first.bit"
#define ALTERA_CONFIGURATION_FILE  "first.rbf"
// Define the read buffer
#define FRAMES_PER_TRANSFER 4
#define READ_SIZE 648*488*2*FRAMES_PER_TRANSFER
#define BLOCK_SIZE 512

using namespace std;

class NirImager
{
private:
	// Global variables for FPGA
	int g_nMems, g_nMemSize;

	// Local reference to the FPGA
	okCFrontPanel *dev;

	// Variables for SPI
	int exposure_low;
	int exposure_mid;
	int exposure_high;
	int channelNum;
	int PgaGain;

	// ----- FPGA function -----
	okCFrontPanel *initializeFPGA();
	int checkFPGA();

	// ----- Imager function -----
	void enableFIFO();
	void disableFIFO();
	void resetFIFO();

	void initialSPIwrite();
	void confirmSPIread();
	void resetSPI();

	void spiDataTransfer(int wr, int addr, int val);
	void spiDataRead(int wr, int addr, int val);

	void checkTemperature();

	void enableBitslip();

	void frameRequest();
	void disableFrameRequest();

	void setExposurePara(double exposure);

	void InitializeImager();

	std::shared_ptr<spdlog::logger> _logger;

public:
	NirImager();
	~NirImager();

	BOOL SetupImager(double exposure);

	unsigned char *readImagerData();

	void changeExposure(double exposure);
};