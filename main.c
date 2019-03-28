#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include "smc.h"
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

#define say(s) write(1,s,sizeof(s))

static io_connect_t conn;
struct termios orig_termios;

void reset_terminal_mode()
{
	say("\x1b[?25h");
	tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
	setvbuf(stdout, NULL, _IONBF, 0);
	struct termios new_termios;

	/* take two copies - one for now, one for later */
	tcgetattr(1, &orig_termios);
	new_termios = orig_termios;
	/* register cleanup handler, and set the new terminal mode */
	atexit(reset_terminal_mode);
	//cfmakeraw(&new_termios);
	new_termios.c_lflag &= (~ECHO & ~ICANON);
	tcsetattr(1, TCSANOW, &new_termios);
}

UInt32 _strtoul(char *str, int size, int base)
{
	UInt32 total = 0;
	int i;

	for (i = 0; i < size; i++)
	{
		if (base == 16)
			total += str[i] << (size - 1 - i) * 8;
		else
		   total += (unsigned char) (str[i] << (size - 1 - i) * 8);
	}
	return total;
}

void _ultostr(char *str, UInt32 val)
{
	str[0] = '\0';
	sprintf(str, "%c%c%c%c", 
			(unsigned int) val >> 24,
			(unsigned int) val >> 16,
			(unsigned int) val >> 8,
			(unsigned int) val);
}

kern_return_t SMCOpen(void)
{
	kern_return_t result;
	io_iterator_t iterator;
	io_object_t   device;

	CFMutableDictionaryRef matchingDictionary = IOServiceMatching("AppleSMC");
	result = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDictionary, &iterator);
	if (result != kIOReturnSuccess) {
		printf("Error: IOServiceGetMatchingServices() = %08x\n", result);
		return 1;
	}

	device = IOIteratorNext(iterator);
	IOObjectRelease(iterator);
	if (device == 0) {
		printf("Error: no SMC found\n");
		return 1;
	}

	result = IOServiceOpen(device, mach_task_self(), 0, &conn);
	IOObjectRelease(device);
	if (result != kIOReturnSuccess) {
		printf("Error: IOServiceOpen() = %08x\n", result);
		return 1;
	}

	return kIOReturnSuccess;
}

kern_return_t SMCClose()
{
	return IOServiceClose(conn);
}

kern_return_t SMCCall(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure)
{
	size_t   structureInputSize;
	size_t   structureOutputSize;

	structureInputSize = sizeof(SMCKeyData_t);
	structureOutputSize = sizeof(SMCKeyData_t);

	#if MAC_OS_X_VERSION_10_5
	return IOConnectCallStructMethod( conn, index,
							// inputStructure
							inputStructure, structureInputSize,
							// ouputStructure
							outputStructure, &structureOutputSize );
	#else
	return IOConnectMethodStructureIStructureO( conn, index,
												structureInputSize, /* structureInputSize */
												&structureOutputSize,   /* structureOutputSize */
												inputStructure,		/* inputStructure */
												outputStructure);	   /* ouputStructure */
	#endif

}

kern_return_t SMCReadKey(UInt32Char_t key, SMCVal_t *val)
{
	kern_return_t result;
	SMCKeyData_t  inputStructure;
	SMCKeyData_t  outputStructure;

	memset(&inputStructure, 0, sizeof(SMCKeyData_t));
	memset(&outputStructure, 0, sizeof(SMCKeyData_t));
	memset(val, 0, sizeof(SMCVal_t));

	inputStructure.key = _strtoul(key, 4, 16);
	inputStructure.data8 = SMC_CMD_READ_KEYINFO;

	result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
	if (result != kIOReturnSuccess)
		return result;

	val->dataSize = outputStructure.keyInfo.dataSize;
	_ultostr(val->dataType, outputStructure.keyInfo.dataType);
	inputStructure.keyInfo.dataSize = val->dataSize;
	inputStructure.data8 = SMC_CMD_READ_BYTES;

	result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
	if (result != kIOReturnSuccess)
		return result;

	memcpy(val->bytes, outputStructure.bytes, sizeof(outputStructure.bytes));

	return kIOReturnSuccess;
}

double SMCGetTemperature(char *key)
{
	SMCVal_t val;
	kern_return_t result;

	result = SMCReadKey(key, &val);
	if (result == kIOReturnSuccess) {
		// read succeeded - check returned value
		if (val.dataSize > 0) {
			if (strcmp(val.dataType, DATATYPE_SP78) == 0) {
				// convert sp78 value to temperature
				int intValue = val.bytes[0] * 256 + (unsigned char)val.bytes[1];
				return intValue / 256.0;
			}
		}
	}
	// read failed
	return 0.0;
}

float SMCGetFanRPM(char *key)
{
	SMCVal_t val;
	kern_return_t result;

	result = SMCReadKey(key, &val);
	if (result == kIOReturnSuccess) {
		// read succeeded - check returned value
		if (val.dataSize > 0) {
			if (strcmp(val.dataType, DATATYPE_FPE2) == 0) {
				// convert fpe2 value to RPM
				return ntohs(*(UInt16*)val.bytes) / 4.0;
			}
		}
	}
	// read failed
	return -1.f;
}


float read_cpu_temp(int show)
{
	double temp = SMCGetTemperature(SMC_KEY_CPU_TEMP);
	if (show)
		printf("CPU: %0.2fºC\n", temp);
	return temp;
}

int read_gpu_temp()
{
	double temp = SMCGetTemperature(SMC_KEY_GPU_TEMP);
	printf("GPU: %0.2fºC\n", temp);
	return 0;
}

int read_pro_temp()
{
	double temp = SMCGetTemperature(SMC_KEY_CPU_PROX_TEMP);
	printf("PROX: %0.2fºC\n", temp);
	return 0;
}

int read_fan_speed()
{
	float rpm = SMCGetFanRPM(SMC_KEY_FAN0_RPM_CUR);
	printf("FAN: %0.2fRPM\n",rpm);
	return 0;
}

int read_fan_info()
{
	float rpm = SMCGetFanRPM(SMC_KEY_FAN0_RPM_MAX);
	printf("MAX: %0.2fRPM\n",rpm);
	rpm = SMCGetFanRPM(SMC_KEY_FAN0_RPM_CUR);
	printf("CUR: %0.2fRPM\n",rpm);
	rpm = SMCGetFanRPM(SMC_KEY_FAN0_RPM_MIN);
	printf("MIN: %0.2fRPM\n",rpm);
	return 0;
}

int print_progress(int d, int color)
{
	int i;
	d = (d*30)/100;
	switch(color) {
		case 1:
			say("\x1b[31m");
			break;
		case 2:
			say("\x1b[32m");
			break;
		case 3:
			say("\x1b[33m");
			break;
		case 4:
			say("\x1b[34m");
			break;
	}
	for (i=0; i<d && i<30; i++) write(1, "█", 3);
	say("\x1b[m");
	for (; i<30; i++) write(1, "▒", 3);

	return 0;
}

int interactive()
{
	char c;
	int retval = 0;
	float temp;
	int per;
	char buff[16];
	unsigned int siz;
	struct timeval tv;
	fd_set rfds;
	FILE *pipe;

	set_conio_terminal_mode();
	say("\x1b[?1049h\x1b[2J");
	say("\x1b[?25l");
	say("\x1b[2;2H");
	say("CPU USAGE:");
	say("\x1b[6;2H");
	say("MEM USAGE:");
	say("\x1b[10;2H");
	say("CPU TEMP:");
	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		retval = select(1, &rfds, NULL, NULL, &tv);
		if ( retval > 0) {
			read(0, &c, 1);
			if (c == 'q') break;
		}
		pipe = popen("ps -A -o \%cpu | awk \'{s+=$1} END {print s \"\%\"}\'", "r");
		fgets(buff, sizeof(buff), pipe);
		pclose(pipe);
		say("\x1b[2;13H\x1b[K");
		printf("%s",buff);
		say("\x1b[4;3H");
		print_progress(atoi(buff),4);

		pipe = popen("ps -A -o \%mem | awk \'{s+=$1} END {print s \"\%\"}\'", "r");
		fgets(buff, sizeof(buff), pipe);
		pclose(pipe);
		say("\x1b[6;13H\x1b[K");
		printf("%s",buff);
		say("\x1b[8;3H");
		print_progress(atoi(buff),3);
		usleep(1000000);

		temp = read_cpu_temp(0);
		say("\x1b[10;12H\x1b[K");
		siz = snprintf(buff, 16, "%.2fºC", temp);
		write(1, buff, siz);

		temp = temp - 30.0;
		temp = temp * (1/(90.0-30.0)) * 100;
		per = (int) temp;
		say("\x1b[12;3H");
		print_progress(per,2);

	}
	say("\x1b[2J\x1b[?1049l");
	return 0;
}

int main(int argc, char **argv)
{
	int c;
	int all = 1;
	int cpu = 0;
	int gpu = 0;
	int pro = 0;
	int fan = 0;
	int fan_info = 0;
	int inter = 0;

	while ((c = getopt(argc, argv, "cgpsih?")) != -1) {
		switch (c) {
			case 'c':
				cpu = 1;
				all = 0;
				break;
			case 'g':
				gpu = 1;
				all = 0;
				break;
			case 'p':
				pro = 1;
				all = 0;
				break;
			case 's':
				fan = 1;
				all = 0;
				break;
			case 'i':
				inter = 1;
				all = 0;
				break;
			case 'h':
			case '?':
				printf("Usage: cpu_temp [c|p|g|s|f [speed]|h]\n");
				printf("\t-c gets cpu temp\n");
				printf("\t-p gets proximity temp\n");
				printf("\t-g gets gpu temp\n");
				printf("\t-s gets fan speed\n");
				printf("\t-i Interactive info\n");
				printf("\t-h display this help\n");
				return 0;
		}
	}

	if (SMCOpen() != kIOReturnSuccess)
		return 1;

	if (all) {
		cpu = 1;
		gpu = 1;
		pro = 1;
		fan = 1;
	}

	if (pro)
		read_pro_temp();

	if (cpu)
		read_cpu_temp(1);

	if (gpu)
		read_gpu_temp();

	if (fan)
		read_fan_speed();

	if (fan_info)
		read_fan_info();

	if (inter)
		interactive();

	SMCClose();
	
	return 0;
}
