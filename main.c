/*
 * main.c - Simple PSP-3000 M17 reflector client.
 *
 * Wojciech Kaczmarski, SP5WWP
 * M17 Project, October 2024
 *
 * Based on a code sample written by:
 * James F <tyranid@gmail.com>
 */
//standard libs
#include <string.h>
#include <unistd.h>
//PSP stuff
#include <pspkernel.h>
#include <pspnet_apctl.h>
#include <pspsdk.h>
#include <psputility.h>
//Internet stuff
#include <arpa/inet.h>
#include <errno.h>
//libm17
#include <m17.h>
//Codec 2
//#include <codec2.h>

#define printf			pspDebugScreenPrintf
#define MODULE_NAME		"psp_m17"

//settings
PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
PSP_HEAP_SIZE_KB(-2048);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(1024);

//struct CODEC2 *c2;

/* Exit callback */
int exit_callback(int arg1, int arg2, void *common)
{
	sceKernelExitGame();
	return 0;
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp)
{
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();

	return 0;
}

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks(void)
{
	int thid = 0;

	thid =
			sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
	if (thid >= 0)
	{
		sceKernelStartThread(thid, 0, 0);
	}

	return thid;
}

int make_socket(uint16_t port)
{
	int sock;
	int ret;
	struct sockaddr_in name;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock<0)
	{
		return -1;
	}

	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	ret = bind(sock, (struct sockaddr *)&name, sizeof(name));
	if(ret<0)
	{
		return -1;
	}

	return sock;
}

/* Connect to a reflector and wait */
void start_client(const char *addr, uint16_t port)
{
	int sock;
	struct sockaddr_in servername;
	
	sock = socket(PF_INET, SOCK_DGRAM, 0);

	if(sock<0)
    {
		printf("Error creating socket\n");
		return;
    }

	servername.sin_family = AF_INET;
	servername.sin_addr.s_addr = inet_addr("152.70.192.70");
	servername.sin_port = htons(port);

	if(0>connect(sock, (struct sockaddr*)&servername, sizeof(servername)))
    {
		printf("Error connecting\n");
		return;
    }
	else
	{
		printf("Connected to reflector at %s, port %d\n", addr, port);
	}

	uint8_t msg[11]="CONN      C";
	msg[4]=0x01; msg[5]=0x31; msg[6]=0x92; msg[7]=0x41; msg[8]=0xB0; msg[9]=0x93;

	write(sock, msg, 11);

	while(1)
	{
		uint8_t buff[1024]={0};

		int rd = read(sock, &buff, sizeof(buff));

		if(rd>0)
		{
			//printf("Read: %s\n", buff);

			if(strcmp(buff, "PING")==0) //PING packet from the server
			{
				msg[0]='P'; msg[1]='O'; msg[2]='N'; msg[3]='G';
				int wb = write(sock, msg, 10);
				//printf("-> PONG\n", wb);
			}
			else if(strncmp(buff, "M17 ", 4)==0) //payload
			{
				uint8_t *lich=&buff[6]; 
				uint8_t *pld=&buff[36];

				uint16_t fn=(buff[34]<<8)+buff[35];
				uint16_t sid=(buff[4]<<8)+buff[5];

				uint8_t d_dst[10], d_src[10];
				decode_callsign_bytes(d_dst, &lich[0]);
				decode_callsign_bytes(d_src, &lich[6]);

				printf("%04X %04X %10s -> %-10s ", sid, fn, d_src, d_dst);
				for(uint8_t i=0; i<16; i++) printf("%02X", pld[i]);
				printf("\n");

				if(fn==0)
				{
					//c2 = codec2_create(CODEC2_MODE_3200);
				}
			}
		}
	}
}

/* Connect to an access point */
int connect_to_apctl(int config)
{
	int err;
	int stateLast = -1;

	/* Connect using the first profile */
	err = sceNetApctlConnect(config);

	if(err != 0)
	{
		printf(MODULE_NAME ": sceNetApctlConnect returns %08X\n", err);
		return 0;
	}

	printf(MODULE_NAME ": Connecting...\n");

	while(1)
	{
		int state;
		err = sceNetApctlGetState(&state);

		if(err!=0)
		{
			printf(MODULE_NAME ": sceNetApctlGetState returns $%x\n", err);
			break;
		}
		if(state>stateLast)
		{
			switch(state)
			{
				case PSP_NET_APCTL_STATE_DISCONNECTED:
					; //printf("*disconnected\n");
				break;

				case PSP_NET_APCTL_STATE_SCANNING:
					printf(" scanning\n");
				break;

				case PSP_NET_APCTL_STATE_JOINING:
					printf(" joining AP\n");
				break;

				case PSP_NET_APCTL_STATE_GETTING_IP:
					printf(" getting IP address\n");
				break;

				case PSP_NET_APCTL_STATE_GOT_IP:
					printf(" got IP address\n");
				break;

				default:
					;
				break;
			}

			stateLast = state;
		}
		if(state==4)
			break; // connected with static IP

		// wait a little before polling again
		sceKernelDelayThread(50 * 1000); // 50ms
	}

	printf(" connected!\n");

	if(err!=0)
	{
		return 0;
	}

	return 1;
}

int net_thread(SceSize args, void *argp)
{
	int err;
	do
	{
		if((err = pspSdkInetInit()))
		{
			printf(MODULE_NAME ": Error, could not initialise the network %08X\n", err);
			break;
		}

		if(connect_to_apctl(1))
		{
			// connected, get my IPADDR and run test
			union SceNetApctlInfo info;

			if (sceNetApctlGetInfo(8, &info) != 0)
				strcpy(info.ip, "unknown IP");

			start_client("152.70.192.70", 17000);
		}
	}while(0);
}
/* Simple thread */
int main(int argc, char **argv)
{
	SceUID thid;

	SetupCallbacks();

	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);

	pspDebugScreenInit();

	/* Create a user thread to do the real work */
	thid = sceKernelCreateThread("net_thread", net_thread,
		0x11,			 // default priority
		256 * 1024, // stack size (256KB is regular default)
		PSP_THREAD_ATTR_USER, NULL);

	if(thid<0)
	{
		printf("Error, could not create thread\n");
		sceKernelSleepThread();
	}

	sceKernelStartThread(thid, 0, NULL);

	sceKernelExitDeleteThread(0);

	return 0;
}
