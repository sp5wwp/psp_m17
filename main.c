/*
 * main.c - Simple PSP-3000 M17 reflector client.
 *
 * Wojciech Kaczmarski, SP5WWP
 * M17 Foundation, February 2026
 *
 * Based on a code sample written by:
 * James F <tyranid@gmail.com>
 */
// standard libs
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
// PSP stuff
#include <pspkernel.h>
#include <pspnet_apctl.h>
#include <pspsdk.h>
#include <psputility.h>
#include <pspaudiolib.h>
#include <pspaudio.h>
#include <psppower.h>
#include <psprtc.h>
// Internet stuff
#include <arpa/inet.h>
#include <errno.h>
// libm17
#include <m17.h>
// Codec 2
#include <codec2_mod.h>

// audio buffer length
#define FIFO_PACKET_LEN 30
#define FIFO_SAMPLES (FIFO_PACKET_LEN * 320)
#define HALF_AUDIO_FIFO (FIFO_PACKET_LEN * 320 / 2)

// resampler: 8 kHz Codec2 output -> 44.1 kHz audio hardware
#define IN_RATE 8000
#define OUT_RATE 44100
#define RS_TAPS 16 // filter length, in input samples
#define RS_STEP 100 // gcd(IN_RATE, OUT_RATE)
#define RS_PHASES (OUT_RATE / RS_STEP) // 441 distinct output phases
#define RS_CUTOFF 3600.0f // Hz, keeps the 4 kHz+ images out

// output gain applied to the decoded speech
#define AUDIO_GAIN 6

#define printf pspDebugScreenPrintf
#define MODULE_NAME "psp_m17"

// settings
PSP_MODULE_INFO(MODULE_NAME, 0, 1, 1);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
PSP_HEAP_SIZE_KB(-2048);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(1024);
// #define MAX_SPEED		//333MHz CPU?

char exec_path[64]; // absolute path to the executable

// Codec2 and audio
codec2_decoder_t c2;
int16_t audio_buff[2 * 160];
uint8_t audio_record; // audio record - debug (disabled by default)
int16_t fifo[FIFO_SAMPLES];
volatile uint16_t fifo_head = 0, fifo_tail = 0;

// default data: M17-KCW A
char ref_addr[16] = "172.234.217.28";
uint16_t ref_port = 17000;
char ref_name[8] = "M17-KCW";
char ref_module = 'A';
char src_call[10] = "SWL12345";

// misc
ScePspDateTime t;
volatile uint8_t audio_ready;
volatile uint8_t eot;

// RGB to u32
uint32_t getColor(uint8_t r, uint8_t g, uint8_t b)
{
	return (b << 16) | (g << 8) | r;
}

// print with color
void printfc(const uint32_t color, const char *fmt, ...)
{
	char str[200];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);

	if (color < 0x1000000U)
	{
		pspDebugScreenSetTextColor(color);
		printf(str);
		pspDebugScreenSetTextColor(getColor(255, 255, 255));
	}
	else
	{
		printf(str);
	}
}

/* audio buffering */
static inline uint16_t fifo_available_nolock(void)
{
	return (fifo_head - fifo_tail + FIFO_SAMPLES) % FIFO_SAMPLES;
}

uint16_t fifo_available(void)
{
	uint16_t avail;

	int old = sceKernelCpuSuspendIntr();
	avail = fifo_available_nolock();
	sceKernelCpuResumeIntr(old);

	return avail;
}

static inline uint16_t fifo_space_nolock(void)
{
	return FIFO_SAMPLES - fifo_available_nolock() - 1;
}

void fifo_push(int16_t *src, uint16_t n)
{
	int old = sceKernelCpuSuspendIntr();
	if (fifo_space_nolock() < n)
	{
		sceKernelCpuResumeIntr(old);
		return;
	}

	for (uint16_t i = 0; i < n; i++)
	{
		fifo[fifo_head] = src[i];
		fifo_head = (fifo_head + 1) % FIFO_SAMPLES;
	}
	sceKernelCpuResumeIntr(old);
}

uint16_t fifo_pop(int16_t *dst, uint16_t n)
{
	uint16_t i = 0;

	while (i < n)
	{
		int old = sceKernelCpuSuspendIntr();
		if (fifo_tail == fifo_head)
		{
			sceKernelCpuResumeIntr(old);
			break;
		}

		dst[i++] = fifo[fifo_tail];
		fifo_tail = (fifo_tail + 1) % FIFO_SAMPLES;
		sceKernelCpuResumeIntr(old);
	}

	return i;
}

/* Exit callback */
int exit_callback(int arg1, int arg2, void *common)
{
	(void)arg1;
	(void)arg2;
	(void)common;

	sceKernelExitGame();
	return 0;
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();

	return 0;
}

/*
 * Polyphase windowed-sinc interpolator.
 * 8000/44100 = 80/441, so the output instant can only fall on 441 distinct
 * positions between two input samples. One 16-tap filter is precomputed for
 * each of them (441 x 16 floats = ~28 KB), so no interpolation is needed at runtime.
 */
static float rs_coef[RS_PHASES][RS_TAPS];

void rs_init(void)
{
	const float fc = RS_CUTOFF / IN_RATE; // cutoff, normalised to the input rate

	for (uint16_t p = 0; p < RS_PHASES; p++)
	{
		float frac = (float)p / RS_PHASES; // output position past the newest-but-8 sample
		float sum = 0.0f;

		for (uint8_t k = 0; k < RS_TAPS; k++)
		{
			float t = (float)k - (RS_TAPS / 2 - 1) - frac; // tap distance, in input samples
			float x = (float)M_PI * 2.0f * fc * t;
			float s = (fabsf(t) < 1e-6f) ? 1.0f : sinf(x) / x;
			float n = (t + RS_TAPS / 2.0f) / RS_TAPS; // Blackman window position, 0..1
			float w = 0.42f - 0.5f * cosf(2.0f * (float)M_PI * n) + 0.08f * cosf(4.0f * (float)M_PI * n);

			rs_coef[p][k] = s * w;
			sum += rs_coef[p][k];
		}

		for (uint8_t k = 0; k < RS_TAPS; k++)
			rs_coef[p][k] /= sum; // unity gain at DC for every phase
	}
}

// called when the audio buffer needs refilling
void audioCallback(void *buf, unsigned int length, void *userdata)
{
	(void)userdata;
	int16_t *out = (int16_t *)buf; // length = stereo frames

	static uint32_t phase = 0;
	static float hist[RS_TAPS]; // last 16 input samples, newest at the end

	if (!audio_ready)
	{
		// start once the prebuffer is full - or immediately if the transmission
		// has already ended (shorter than the prebuffer)
		uint16_t avail = fifo_available();
		if (avail >= HALF_AUDIO_FIFO || (eot && avail > 0))
			audio_ready = 1;
	}

	// the resampler runs continuously - when not playing it is fed with zeros,
	// so the tail of a transmission rings out instead of being cut off
	uint8_t playing = audio_ready;
	uint8_t starved = 0;

	for (uint32_t i = 0; i < length; i++)
	{
		phase += IN_RATE;
		if (phase >= OUT_RATE)
		{
			phase -= OUT_RATE;

			int16_t s = 0;
			if (playing && fifo_pop(&s, 1) == 0)
			{
				s = 0;
				playing = 0; // FIFO ran dry - stop pulling for the rest of this buffer
				starved = 1;
			}

			memmove(&hist[0], &hist[1], (RS_TAPS - 1) * sizeof(float));
			hist[RS_TAPS - 1] = s;
		}

		const float *h = rs_coef[phase / RS_STEP];
		float acc = 0.0f;
		for (uint8_t k = 0; k < RS_TAPS; k++)
			acc += hist[k] * h[k];

		int16_t v;
		if (acc > 32767.0f)
			v = 32767;
		else if (acc < -32768.0f)
			v = -32768;
		else
			v = (int16_t)lrintf(acc);

		out[2 * i] = v;
		out[2 * i + 1] = v;
	}

	if (starved || (!audio_ready && eot))
	{
		// FIFO is empty: either the transmission is over, or the network
		// fell behind - in both cases go back to prebuffering
		int old = sceKernelCpuSuspendIntr();
		if (fifo_head == fifo_tail)
		{
			audio_ready = 0;
			eot = 0;
		}
		sceKernelCpuResumeIntr(old);
	}
}

/* Sets up the callback thread and returns its thread id */
int SetupCallbacks(void)
{
	int thid = 0;

	thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
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
	if (sock < 0)
	{
		return -1;
	}

	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	ret = bind(sock, (struct sockaddr *)&name, sizeof(name));
	if (ret < 0)
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

	// let's go!
	sock = socket(PF_INET, SOCK_DGRAM, 0);

	if (sock < 0)
	{
		printfc(getColor(255, 0, 0), "Error creating socket\n");
		return;
	}

	servername.sin_family = AF_INET;
	servername.sin_addr.s_addr = inet_addr(ref_addr);
	servername.sin_port = htons(port);

	if (connect(sock, (struct sockaddr *)&servername, sizeof(servername)) < 0)
	{
		printf("Error connecting\n");
		return;
	}
	else
	{
		srand(time(NULL));
		uint32_t r = rand() % 100000;
		sprintf(src_call, "SWL%05ld", r);
		printfc(getColor(0, 200, 0), "\nConnected to %s %c (%s:%d)\n", ref_name, ref_module, addr, port);
		printfc(getColor(0, 200, 0), "ID: %s\n", src_call);
	}

	uint8_t msg[11] = "LSTN";
	encode_callsign_bytes(&msg[4], (const char *)src_call);
	msg[10] = ref_module;

	write(sock, msg, 11);

	// test audio dump file (if enabled)
	SceUID audio_dump = 0;

	// stream tracking, used to reset the decoder at the start of each transmission
	uint16_t last_sid = 0;
	uint8_t in_stream = 0;

	while (1)
	{
		// prevent the screen from turning black
		scePowerTick(PSP_POWER_TICK_DISPLAY);

		uint8_t buff[1024] = {0};

		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int rd = recvfrom(sock, buff, sizeof(buff), 0, (struct sockaddr *)&src, &srclen);

		if (rd > 0)
		{
			/*printf("Read:");
			for (uint16_t i=0; i<rd; i++)
				printf(" %02X", buff[i]);
			printf("\n");*/

			if (memcmp(buff, (uint8_t *)"PING", 4) == 0) // PING packet from the server
			{
				msg[0] = 'P';
				msg[1] = 'O';
				msg[2] = 'N';
				msg[3] = 'G';
				write(sock, msg, 10);
				// printf("PONG\n");
			}
			else if (memcmp(buff, (uint8_t *)"M17 ", 4) == 0) // payload
			{
				uint8_t *lich = &buff[6];
				uint8_t *pld = &buff[36];

				uint16_t fn = (buff[34] << 8) + buff[35];
				uint16_t sid = (buff[4] << 8) + buff[5];

				char d_dst[10], d_src[10];
				decode_callsign_bytes(d_dst, &lich[0]);
				decode_callsign_bytes(d_src, &lich[6]);

				pspDebugScreenSetXY(0, 14);
				printfc(getColor(200, 200, 0), "Most recent activity:\n");
				sceRtcGetCurrentClockLocalTime(&t);
				printfc(getColor(200, 200, 0), "Time: ");
				printf("%02d:%02d:%02d\n", t.hour, t.minute, t.second);
				printfc(getColor(200, 200, 0), "SID:  ");
				printf("%04X\n", sid);
				printfc(getColor(200, 200, 0), "FN:   ");
				printf("%04X\n", fn);
				printfc(getColor(200, 200, 0), "SRC:  ");
				printf("%-10s\n", d_src);
				printfc(getColor(200, 200, 0), "DST:  ");
				printf("%-10s\n", d_dst);
				printfc(getColor(200, 200, 0), "PLD:  ");
				for (uint8_t i = 0; i < 16; i++)
					printf("%02X", pld[i]); // printf("\n");

				if (fn == 0)
				{
					if (audio_record)
					{
						char fname[12];
						sprintf(fname, "/%04X.raw", sid);
						if (strstr(exec_path, "raw")) // if we previously appended filename
						{
							for (uint8_t i = strlen(exec_path) - 1; i > 0; i--)
							{
								if (exec_path[i] == '/')
								{
									exec_path[i] = 0;
									break;
								}
							}
						}
						audio_dump = sceIoOpen(strcat(exec_path, fname), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
					}
				}

				// new transmission? reset the decoder here, in the thread that uses it
				if (!in_stream || sid != last_sid || (fn & 0x7FFF) == 0)
					codec2_decoder_init(&c2);
				last_sid = sid;
				in_stream = !(fn & 0x8000);

				// decode audio
				codec2_decode(&c2, &audio_buff[0], &pld[0]);
				codec2_decode(&c2, &audio_buff[160], &pld[8]);
				for (uint16_t i = 0; i < 320; i++)
				{
					// crank up the gain, saturating instead of wrapping around
					int32_t v = (int32_t)audio_buff[i] * AUDIO_GAIN;
					if (v > 32767)
						v = 32767;
					else if (v < -32768)
						v = -32768;
					audio_buff[i] = (int16_t)v;
				}
				fifo_push(audio_buff, 320);

				if (audio_record && audio_dump)
					sceIoWrite(audio_dump, (uint8_t *)audio_buff, sizeof(audio_buff));

				if (fn & 0x8000)
				{
					eot = 1;

					if (audio_record && audio_dump)
						sceIoClose(audio_dump);
				}
			}
		}
		else // no new data
		{
			// sceKernelDelayThread(5 * 1000); // 5ms delay to reduce CPU use
		}
	}
}

/* Connect to an access point */
int connect_to_apctl(int config)
{
	int err;
	int stateLast = -1;

	// Try to connect using the PSP’s saved WiFi profile (1)
	err = sceNetApctlConnect(config);
	if (err != 0)
	{
		printf(MODULE_NAME ": sceNetApctlConnect returned 0x%08X\n", err);
		return 0;
	}

	printf("Connecting...\n");

	while (1)
	{
		int state;
		err = sceNetApctlGetState(&state);

		if (err != 0)
		{
			printf(MODULE_NAME ": sceNetApctlGetState returned 0x%08X\n", err);
			return 0;
		}

		if (state > stateLast)
		{
			switch (state)
			{
			case PSP_NET_APCTL_STATE_DISCONNECTED:
				printf(" disconnected\n");
				break;
			case PSP_NET_APCTL_STATE_SCANNING:
				printf(" scanning\n");
				break;
			case PSP_NET_APCTL_STATE_JOINING:
				printf(" joining AP\n");
				break;
			case PSP_NET_APCTL_STATE_GETTING_IP:
				printf(" getting IP\n");
				break;
			case PSP_NET_APCTL_STATE_GOT_IP:
				printf(" got IP address\n");
				break;
			}
			stateLast = state;
		}

		if (state == PSP_NET_APCTL_STATE_GOT_IP) // == 4
			break;

		sceKernelDelayThread(50 * 1000);
	}

	printf("Connected!\n");
	return 1;
}

int net_thread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	int err;

	do
	{
		// Allow WiFi hardware to wake up
		sceKernelDelayThread(1500 * 1000);

		// Initialize networking (required)
		if ((err = pspSdkInetInit()))
		{
			printf(MODULE_NAME ": could not initialise network 0x%08X\n", err);
			break;
		}

		// Now connect using the stored profile (1)
		if (connect_to_apctl(1))
		{
			// connected, get IP
			union SceNetApctlInfo info;
			if (sceNetApctlGetInfo(8, &info) != 0)
				strcpy(info.ip, "unknown IP");

			start_client(ref_addr, ref_port);
		}

	} while (0); // what?

	return 0;
}

/* Simple thread */
int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
#ifdef MAX_SPEED
	scePowerSetClockFrequency(333, 333, 166);
#endif

	SceUID thid;

	// retrieve the path to this executable
	if (audio_record)
	{
		strcpy(exec_path, argv[0]);
		for (uint8_t i = strlen(exec_path) - 1; i > 0; i--)
		{
			if (exec_path[i] == '/')
			{
				exec_path[i] = 0;
				break;
			}
		}
	}

	SetupCallbacks();

	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
	sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
	sceUtilityLoadNetModule(PSP_NET_MODULE_PARSEURI);
	sceUtilityLoadNetModule(PSP_NET_MODULE_PARSEHTTP);
	sceUtilityLoadNetModule(PSP_NET_MODULE_HTTP);

	codec2_decoder_init(&c2);
	rs_init(); // before the audio callback starts running

	pspAudioInit();
	pspAudioSetChannelCallback(0, audioCallback, NULL);
	pspAudioSetVolume(0, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX);

	pspDebugScreenInit();
	printf("Sony PSP M");
	printfc(getColor(255, 0, 0), "17");
	printf(" Reflector Client by SP5WWP\n");
	printf("Using libm17 v%s\n", LIBM17_VERSION);
	printf("CPU at %dMHz\n\n", scePowerGetCpuClockFrequency());

	/* Create a user thread to do the real work */
	thid = sceKernelCreateThread("net_thread", net_thread,
								 0x18,		 // default priority
								 256 * 1024, // stack size (256KB is regular default)
								 PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, NULL);

	if (thid < 0)
	{
		printfc(getColor(255, 0, 0), "Could not create thread\n");
		sceKernelSleepThread();
	}

	sceKernelStartThread(thid, 0, NULL);

	sceKernelExitDeleteThread(0);

	return 0;
}
