#include "NirImager.h"

NirImager::NirImager()
{
	// Local reference to the FPGA
	dev = NULL;

	// Variables for SPI
	exposure_low = 0 ;
	exposure_mid = 0;
	exposure_high = 0;
	channelNum = 3;
	PgaGain = 0;

	_logger = spdlog::stdout_color_mt("FPGA Imager");
}

NirImager::~NirImager()
{
	delete dev;
}

/*
Initialize FPGA object
Return:
	Return a okCFrontPanel* object if FPGA initialization success
	Null if failed
*/
okCFrontPanel* NirImager::initializeFPGA()
{
	okCFrontPanel *dev;
	std::string   config_filename;

	// Open the first XEM - try all board types.
	dev = new okCFrontPanel;
	if (okCFrontPanel::NoError != dev->OpenBySerial()) {
		delete dev;
		_logger->warn("Cannot open FPGA. Is it connected to the computer?");
		return(NULL);
	}

	printf("A device is found: %s\n", dev->GetBoardModelString(dev->GetBoardModel()).c_str());
	okTDeviceInfo g_devInfo;
	dev->GetDeviceInfo(&g_devInfo);

	// Set memory configuration
	switch (dev->GetBoardModel()) {
	case okCFrontPanel::brdXEM3005:
	case okCFrontPanel::brdXEM3010:
		g_nMemSize = 32 * 1024 * 1024;
		g_nMems = 1;
		break;
	case okCFrontPanel::brdXEM3050:
		g_nMemSize = 32 * 1024 * 1024;
		g_nMems = 2;
		break;
	case okCFrontPanel::brdXEM5010:
	case okCFrontPanel::brdXEM5010LX110:
		g_nMemSize = 128 * 1024 * 1024;
		g_nMems = 2;
		break;
	case okCFrontPanel::brdXEM6320LX130T:
		g_nMemSize = 512 * 1024 * 1024;
		g_nMems = 1;
		break;
	case okCFrontPanel::brdXEM7350K70T:
	case okCFrontPanel::brdXEM7350K160T:
		g_nMemSize = 512 * 1024 * 1024;
		g_nMems = 1;
		break;
	case okCFrontPanel::brdXEM6006LX9:
	case okCFrontPanel::brdXEM6006LX16:
	case okCFrontPanel::brdXEM6006LX25:
	case okCFrontPanel::brdXEM6010LX45:
	case okCFrontPanel::brdXEM6010LX150:
	case okCFrontPanel::brdXEM6310LX45:
	case okCFrontPanel::brdXEM6310LX150:
	case okCFrontPanel::brdXEM6310MTLX45T:
	case okCFrontPanel::brdXEM6110LX45:
	case okCFrontPanel::brdXEM6110LX150:
	case okCFrontPanel::brdXEM6110v2LX45:
	case okCFrontPanel::brdXEM6110v2LX150:
	case okCFrontPanel::brdZEM4310:
		g_nMemSize = 128 * 1024 * 1024;
		g_nMems = 1;
		break;
	default:
		_logger->warn("Unsupported device.");
		delete dev;
		return(NULL);
	}

	// Configure the PLL appropriately
	dev->LoadDefaultPLLConfiguration();

	// Get some general information about the XEM.
	printf("Device firmware version: %d.%d\n", dev->GetDeviceMajorVersion(), dev->GetDeviceMinorVersion());
	printf("Device serial number: %s\n", dev->GetSerialNumber().c_str());
	printf("Device ID string: %s\n", dev->GetDeviceID().c_str());

	// Download the configuration file.
	switch (dev->GetBoardModel()) {
	case okCFrontPanel::brdZEM4310:
		config_filename = ALTERA_CONFIGURATION_FILE;
		break;
	default:
		config_filename = XILINX_CONFIGURATION_FILE;
		break;
	}

	if (okCFrontPanel::NoError != dev->ConfigureFPGA(config_filename)) {
		_logger->warn("FPGA configuration failed.");
		delete dev;
		return(NULL);
	}

	// Check for FrontPanel support in the FPGA configuration.
	if (false == dev->IsFrontPanelEnabled()) {
		_logger->warn("FrontPanel support is not enabled.");
		delete dev;
		return(NULL);
	}

	_logger->info("FrontPanel support is enabled.");

	return(dev);
}

/*
The toplevel function that check, and establish FPGA connection
If FPGA sets up successfully, TRUE will return. Otherwise, FALSE will return
Return:
	-1 if cannot establish the FPGA connection
	0 if success
*/
int NirImager::checkFPGA() {
	char dll_date[32], dll_time[32];
	_logger->info("Connecting to the FPGA...");

	// Check if okFrontPanelDLL lib exists 
	if (FALSE == okFrontPanelDLL_LoadLib(NULL)) {
		_logger->warn("FrontPanel DLL could not be loaded.");
		return(-1);
	}

	okFrontPanelDLL_GetVersion(dll_date, dll_time);
	printf("FrontPanel DLL loaded.  Built: %s  %s\n", dll_date, dll_time);

	// Assign dev to the FPGA device
	dev = initializeFPGA();

	// Initialize the FPGA with our configuration bitfile.
	if (NULL == dev) {
		_logger->warn("FPGA could not be initialized.");
		return(-1);
	}

	_logger->info("FPGA Check Success");
	return 0;
}

/*
Update all the exposure parameters based on the input exposure
*/
void NirImager::setExposurePara(double exposure) {
	_logger->info("Setup Exposure Parameters");
	unsigned int reg_value = unsigned int((exposure * (40000) / 13) - 6);
	cout << "register value: " << reg_value << endl;
	cout << "low: " << (reg_value & 0x000000FF) << endl;
	cout << "mid: " << ((reg_value & 0x0000FF00) >> 8) << endl;
	cout << "high: " << ((reg_value & 0x00FF0000) >> 16) << endl;

	// Set global exposure values
	exposure_low = (reg_value & 0x000000FF);
	exposure_mid = (reg_value & 0x0000FF00) >> 8;
	exposure_high = (reg_value & 0x00FF0000) >> 16;
}

/*
Set up the imager
Input:
	exposure - the exposure time
Return:
	return FALSE if start imager fails, return TRUE if success
*/
BOOL NirImager::SetupImager(double exposure) {
	// checkFPGA will initalize the FPGA if possible
	// it will return -1 if it fails to initialize FPGA
	if (checkFPGA() == -1) {
		return FALSE;
	}

	// Set up exposure parameters
	setExposurePara(exposure);

	dev->SetWireInValue(0x00, 0x00000000, 0x0000000F);
	dev->UpdateWireIns();

	// Initialize Imager system
	InitializeImager();

	// Write initial SPI values to registers (set registers, No of frames, test mode etc)
	initialSPIwrite();

	// Confirm that the SPI initial values are set
	confirmSPIread();

	checkTemperature();
	Sleep(1);

	enableBitslip();
	Sleep(1);

	frameRequest();

	return TRUE;
}

/*
Initiliaze the FIFO, SPI in the Imager system
*/
void NirImager::InitializeImager() {
	//START OSCILLATOR AND CMV300 first
	// Disable FIFO
	disableFIFO();
	Sleep(10);
	
	// [Unkown Setting]
	dev->SetWireInValue(0x02, 0xFFFFFFFF, 0x00000100);
	dev->UpdateWireIns();
	cout << "Imager reset complete." << endl;
	Sleep(1);

	// Reset SPI
	resetSPI();

	// Enable FIFO
	enableFIFO();
}

// Disable the FIFO in FPGA
void NirImager::disableFIFO() {
	dev->SetWireInValue(0x02, 0, 0xFFFFFFFF);
	dev->UpdateWireIns();
	_logger->info("FIFO disabled.");
}

// Enable the FIFO in FPGA
void NirImager::enableFIFO() {
	//now enable the FIFO so that data (no matter valid or not) will be written
	dev->SetWireInValue(0x02, 0xFFFFFFFF, 0x00000111);
	dev->UpdateWireIns();
	_logger->info("FIFO enabled.");
}

void NirImager::initialSPIwrite() {
	_logger->info("Start writting new SPI configuration");
	spiDataTransfer(1, 83, 251);			// pll
	spiDataTransfer(1, 42, exposure_low);	//exposure time lower 8 bit
	spiDataTransfer(1, 43, exposure_mid);	//exposure time middle 8 bit
	spiDataTransfer(1, 44, exposure_high);	//exposure time higher 8 bit
	spiDataTransfer(1, 57, channelNum);		// set number of channel we want to use
	spiDataTransfer(1, 58, 44);	 
	spiDataTransfer(1, 59, 240);			// offset_bottom_low 240
	spiDataTransfer(1, 60, 10);				// offset_bottom_high 10
	spiDataTransfer(1, 67, 0);				// test mode
	spiDataTransfer(1, 69, 9);
	spiDataTransfer(1, 80, PgaGain);		// PGA gain
	spiDataTransfer(1, 97, 240);			// offset_top_low 240
	spiDataTransfer(1, 98, 10);				// offset_top_high 10
	spiDataTransfer(1, 100, 124);			// ADC gain
	spiDataTransfer(1, 101, 98);
	spiDataTransfer(1, 102, 34);
	spiDataTransfer(1, 103, 64);
	spiDataTransfer(1, 106, 90);
	spiDataTransfer(1, 107, 110);
	spiDataTransfer(1, 108, 91);
	spiDataTransfer(1, 109, 82);
	spiDataTransfer(1, 110, 80);
	spiDataTransfer(1, 117, 91);
	_logger->info("New SPI configuration has been updated.");
}

void NirImager::confirmSPIread() {
	cout << "Current SPI registers' value:" << endl;
	spiDataRead(0, 83, 0);	// pll
	spiDataRead(0, 42, 0);	//exposure time lower 8 bit
	spiDataRead(0, 43, 0);	//exposure time middle 8 bit
	spiDataRead(0, 44, 0);	//exposure time higher 8 bit
	spiDataRead(0, 57, 0);	// set number of channel we want to use
	spiDataRead(0, 58, 0);	// 
	spiDataRead(0, 59, 0);	// offset_bottom_low
	spiDataRead(0, 60, 0);	// offset_bottom_high
	spiDataRead(0, 67, 0);	// test mode
	spiDataRead(0, 69, 0);
	spiDataRead(0, 80, 0);	// PGA gain
	spiDataRead(0, 97, 0);	// offset_top_low
	spiDataRead(0, 98, 0);	// offset_top_high
	spiDataRead(0, 100, 0);	// ADC gain
	spiDataRead(0, 101, 0);
	spiDataRead(0, 102, 0);
	spiDataRead(0, 103, 0);
	spiDataRead(0, 106, 0);
	spiDataRead(0, 107, 0);
	spiDataRead(0, 108, 0);
	spiDataRead(0, 109, 0);
	spiDataRead(0, 110, 0);
	spiDataRead(0, 117, 0);
	cout << "------------------------------" << endl;
}

// Transfer data to SPI
void NirImager::spiDataTransfer(int wr, int addr, int val) {

	// Set correct bits for SPI 
	unsigned int spiWriteData = ((wr & 0x01) << 31) + ((addr & 0x7F) << 24) + ((val & 0xFF) << 16);
	dev->SetWireInValue(0x03, spiWriteData, 0xFFFFFFFF);
	dev->UpdateWireIns();
	dev->ActivateTriggerIn(0x40, 0);

	//wait for the signal indicating that the SPI write has completed.
	do
	{
		dev->UpdateTriggerOuts();
	} while (!dev->IsTriggered(0x60, 0x1));

	//if we've arrived here, the SPI write has completed.
	dev->UpdateWireOuts();

}

// Read data from SPI
void NirImager::spiDataRead(int wr, int addr, int val) {
	spiDataTransfer(wr, addr, val);
	cout << "Register " << addr << " value is " << (dev->GetWireOutValue(0x22) & 0xFFFF) << endl;
}

/*
Check the physical temperature of the sensor
*/
void NirImager::checkTemperature() {
	_logger->info("Check the temperature of sensor");
	// Read temperature
	unsigned int spiWriteData = 0x4F004E00;
	dev->SetWireInValue(0x03, spiWriteData, 0xFFFFFFFF);
	dev->UpdateWireIns();

	dev->ActivateTriggerIn(0x40, 0);
	//wait for the signal indicating that the SPI write has completed.
	Sleep(500);
	do
	{
		dev->UpdateTriggerOuts();
	} while (!dev->IsTriggered(0x60, 0x1));

	//if we've arrived here, the SPI write has completed.
	dev->UpdateWireOuts();
	printf("Sensor Temperature:   0x%.4X\n", (dev->GetWireOutValue(0x22) & 0xFFFF));
}

/*
Enable the bit slip to compensate the offset of differential signals
*/
void NirImager::enableBitslip() {
	dev->ActivateTriggerIn(0x41, 0);
	dev->UpdateWireOuts();
	_logger->info("Bitslip enabled");
}

/*
Tell the imager sensor to generate the frame
*/
void NirImager::frameRequest() {
	dev->SetWireInValue(0x00, 0xFFFFFFFF, 0x00000002);
	dev->UpdateWireIns();
}

/*
Disable the frame generation in the imager sensor
*/
void NirImager::disableFrameRequest() {
	dev->SetWireInValue(0x00, 0x00000000, 0x00000002);
	dev->UpdateWireIns();
}

/*
Read data from the Imager
Return:
	Return the data read from the Imager, it is user's resposibility to delete the dynamically allocated data
	NULL will be returned when we fail to read a full frame of data, or any error code occurs.
*/
unsigned char* NirImager::readImagerData() {
	unsigned char *dataIn = new unsigned char[READ_SIZE]();
	int rlen = dev->ReadFromBlockPipeOut(0xA0, BLOCK_SIZE, READ_SIZE, dataIn);

	if (rlen != READ_SIZE) {
		if (rlen < 0) {
			_logger->warn("readImagerData() failed with error: {0}", rlen);
			//cout << "readImagerData() failed with error: " << rlen << endl;
		}
		else {
			_logger->warn("Fail to read a complete frame of data.");
			//cout << "Fail to read a complete frame of data.\n";
		}
		delete[] dataIn;
		dataIn = NULL;
	}

	return dataIn;
}

void NirImager::resetFIFO() {
	dev->SetWireInValue(0x00, 0x00000001, 0x00000001);
	dev->UpdateWireIns();
	Sleep(100);

	dev->SetWireInValue(0x00, 0x00000000, 0x00000001);
	dev->UpdateWireIns();
	Sleep(100);
}

void NirImager::resetSPI() {
	dev->ActivateTriggerIn(0x40, 0x02); //SPI reset trigger
	cout << "SPI has been reset." << endl;
}

/*
Change the exposure time of the sensor
Reading from the sensor has to be disable before this function is called. Related sensor parameters, FIFO, SPI
will be reset.
*/
void NirImager::changeExposure(double exposure) {
	setExposurePara(exposure);

	disableFrameRequest();
	Sleep(100);

	resetFIFO();
	resetSPI();
	initialSPIwrite();
	confirmSPIread();
	Sleep(1);

	// Enable frame request
	frameRequest();
}