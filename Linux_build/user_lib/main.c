
/* Includes ------------------------------------------------------------------*/
#include <malloc.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include "VL53L1X_api.h"
#include "VL53L1X_calibration.h"
#include "vl53l1_platform.h"
/* Defines ------------------------------------------------------------------*/

/* Uncomment this line to use polling instead of IT relayed by st_tof_module */
/* #define POLLING 1 */

#define MICRO_WAIT 200000

#define VL53L1_MAX_I2C_XFER_SIZE 256

#define MSG_START "VL53L1X sensor detected\n"
#define MSG_OK "ok\n"
#define MSG_UNKNOWN_CMD "Unknown command\n"
#define MSG_WRONG_VALUE "Warning: Wrong value sent\n"
#define INPUT_BUFFER_SIZE 512

#define ST_TOF_IOCTL_WFI 1

char cmd[1024];
int user_cmd_set;
int SensorStateBool;

uint16_t Dev;

/* main  --------------------------------------------------------------------*/

int main(int argc, char **argv)
{
	int file = 0;
	int status;
	int calibration_status;
	uint8_t byteData, sensorState = 0;
	uint16_t wordData;
	VL53L1X_Result_t Results;
	uint8_t first_range = 1;
	uint8_t I2cDevAddr = 0x29;
	int16_t offset;
	uint16_t xtalk;

	file = VL53L1X_UltraLite_Linux_I2C_Init(Dev, 1, I2cDevAddr);
	if (file == -1)
		exit(1);

#if !defined(POLLING)
	status = VL53L1X_UltraLite_Linux_Interrupt_Init();
	if (status == -1)
		exit(1);
#endif

	status = VL53L1_RdByte(Dev, 0x010F, &byteData);
	printf("VL53L1X Model_ID: %X\n", byteData);
	status += VL53L1_RdByte(Dev, 0x0110, &byteData);
	printf("VL53L1X Module_Type: %X\n", byteData);
	status += VL53L1_RdWord(Dev, 0x010F, &wordData);
	printf("VL53L1X: %X\n", wordData);
	while (sensorState == 0) {
		status += VL53L1X_BootState(Dev, &sensorState);
		VL53L1_WaitMs(Dev, 2);
	}
	printf("Chip booted\n");

	status = VL53L1X_SensorInit(Dev);
	/* status += VL53L1X_SetInterruptPolarity(Dev, 0); */
	status += VL53L1X_SetDistanceMode(Dev, 2); /* 1=short, 2=long */
	status += VL53L1X_SetTimingBudgetInMs(Dev, 100);
	status += VL53L1X_SetInterMeasurementInMs(Dev, 100);
	status += VL53L1X_StartRanging(Dev);

	int calibration_choice;
	printf("-----Select Calibration Type-----\n1 - Offset Calibration\n2 - Crosstalk Calibration\nYour choice (1 or 2): ");
	scanf("%d", &calibration_choice);

	uint16_t target_distance;
	printf("Enter target distance (mm): ");
	scanf("%hu", &target_distance);

	if(calibration_choice == 1) {
		printf("Starting offset calibration...\n");
		calibration_status = VL53L1X_CalibrateOffset(Dev, target_distance, &offset); /* may take few second to perform the offset cal*/
		printf("Offset calibration status: %d\nOffset correction value: %X\n", calibration_status, offset);
	} else if(calibration_choice == 2) {
		printf("Starting crosstalk calibration...\n");
		calibration_status = VL53L1X_CalibrateXtalk(Dev, target_distance, &xtalk); /* may take few second to perform the xtalk cal */
		printf("Crosstalk calibration status: %d\nCrosstalk correction value: %hu\n", calibration_status, xtalk);
	}
	printf("-----Lidar Readings-----\n");

	// int16_t get_offset;
	// uint16_t get_xtalk;
	// calibration_status = VL53L1X_GetOffset(Dev, &get_offset);
	// calibration_status = VL53L1X_GetXtalk(Dev, &get_xtalk);
	// printf("Fetched offset: %X\nFetched xtalk: %X\n", get_offset, get_xtalk);

	/* read and display data loop */
	int counter;
	for(counter = 0; counter < 5; counter++) {
#if defined(POLLING)
		uint8_t dataReady = 0;

		while (dataReady == 0) {
			status = VL53L1X_CheckForDataReady(Dev, &dataReady);
			usleep(1);
		}
#else
		status = VL53L1X_UltraLite_WaitForInterrupt(ST_TOF_IOCTL_WFI);
		if (status) {
			printf("ST_TOF_IOCTL_WFI failed, err = %d\n", status);
			return -1;
		}
#endif
		/* Get the data the new way */
		status += VL53L1X_GetResult(Dev, &Results);

		printf("Status = %2d, dist = %5d, Ambient = %2d, Signal = %5d, #ofSpads = %5d\n",
			Results.Status, Results.Distance, Results.Ambient,
                                Results.SigPerSPAD, Results.NumSPADs);

		/* trigger next ranging */
		status += VL53L1X_ClearInterrupt(Dev);
		if (first_range) {
			/* very first measurement shall be ignored
			 * thus requires twice call
			 */
			status += VL53L1X_ClearInterrupt(Dev);
			first_range = 0;
		}
	}
	return 0;
}
