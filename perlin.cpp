#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <memory.h>
#include <math.h>

#define HOST ((char*)"192.168.180.184")
#define PORT 56700

// 50000 5000 40

#define SCENE_DELAY 100000
#define TILE_DELAY   10000
#define FADE_TIME      100

void Quit (int sig);
void timer_handler (int signum);
void InitPerlin (void);
void NextPerlin (void);
void SendLevels (void);
void TurnOnLights (void);
void TurnOffLights (void);
void paddr(unsigned char *a);

struct hostent *hp;
char *host = HOST;
struct sockaddr_in servaddr;
int fd;

void InitPerlin (void);
void NextPerlin (void);
int32_t noise (uint16_t x, uint16_t y, uint16_t z);

#define DISPLAY_HEIGHT 8
#define DISPLAY_WIDTH 40

typedef struct {
	uint16_t h;
	uint16_t s;
	uint16_t v;
	uint16_t k;
} hsvk16;

hsvk16 levels[DISPLAY_HEIGHT][DISPLAY_WIDTH];

int main (int argc, char *argv[])
{
    struct sigaction sa;
    struct itimerval timer;

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("cannot create socket\n");
        return 0;
    }

    hp = gethostbyname(host);
    if (!hp) {
        fprintf(stderr, "could not obtain address of %s\n", host);
        return 0;
    }

    for (int i=0; hp->h_addr_list[i] != 0; i++) {
        paddr((unsigned char*) hp->h_addr_list[i]);
    }

    memset((char*)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    memcpy((void *)&servaddr.sin_addr, hp->h_addr_list[0], hp->h_length);

    // trap ctrl-c to call quit function 
    signal (SIGINT, Quit);

	TurnOnLights ();

	InitPerlin ();

    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
			levels[y][x].h = 0;
			levels[y][x].s = 65535;
			levels[y][x].v = 65535;
			levels[y][x].k = 2700;
		}
	}

	// install timer handler
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = &timer_handler;
    sigaction (SIGALRM, &sa, NULL);

    // configure the timer to expire after 125 msec
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = SCENE_DELAY;

    // and every 125 msec after that.
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = SCENE_DELAY;

    // start the timer
    setitimer (ITIMER_REAL, &timer, NULL);

	// wait forever
	while (1) {
		sleep (1);
	}

	return 0;
};


void Quit (int sig)
{
	TurnOffLights ();
	exit (-1);
}


void timer_handler (int signum)
{
	printf (".");
	fflush (stdout);
	NextPerlin ();
	SendLevels ();
}

const uint8_t lifxFrameHeader[8] = {
	0x2e, 0x02, 0x00, 0x34, 'G', 'L', 'E', 'N'
};

const uint8_t lifxFrameAddress[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t lifxProtocolHeader[12] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xcb, 0x02, 0x00, 0x00
};

const uint8_t tileHeader[10] = {
	0x00, 0x01, 0x00, 0x00, 0x00, 0x08, FADE_TIME, 0x00, 0x00, 0x00
};


void TurnOnLights (void)
{
	uint8_t packet[42] = {
		0x2a, 0x00, 0x00, 0x34, 'G', 'L', 'E', 'N',
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x75, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
		0x00, 0x00
	};

	if (sendto(fd, packet, sizeof (packet), 0,
			(struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("sendto2 failed");
		Quit (0);
	}
}

void TurnOffLights (void)
{
	uint8_t packet[42] = {
		0x2a, 0x00, 0x00, 0x34, 'G', 'L', 'E', 'N',
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00
	};

	if (sendto(fd, packet, sizeof (packet), 0,
			(struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("sendto2 failed");
		Quit (0);
	}
}

void SendLevels (void)
{
	uint8_t packet[558];

	memcpy (&packet[ 0], lifxFrameHeader, 8);
	memcpy (&packet[ 8], lifxFrameAddress, 16);
	memcpy (&packet[24], lifxProtocolHeader, 12);
	memcpy (&packet[36], tileHeader, 10);

	for (int tile = 0; tile < 5; tile++) {

		packet[36] = tile;
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++) {
			
				packet[46 + y * 64 + x * 8 + 0] = levels[y][tile * 8 + x].h & 0xff; // hue lo
				packet[46 + y * 64 + x * 8 + 1] = levels[y][tile * 8 + x].h >> 8;   // hue hi
				packet[46 + y * 64 + x * 8 + 2] = 0xff; // sat lo
				packet[46 + y * 64 + x * 8 + 3] = 0xff; // sat hi
				packet[46 + y * 64 + x * 8 + 4] = 0xff; // val lo
				packet[46 + y * 64 + x * 8 + 5] = 0xff; // val hi
				packet[46 + y * 64 + x * 8 + 6] = 0x8c; // kel lo
				packet[46 + y * 64 + x * 8 + 7] = 0x0a; // kel hi

			}
		}

		if (sendto(fd, packet, sizeof (packet), 0,
				(struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
			perror("sendto2 failed");
			Quit (0);
		}

		usleep (TILE_DELAY);
	}
}

void paddr(unsigned char *a)
{
    printf("%d.%d.%d.%d\n", a[0], a[1], a[2], a[3]);
}


int32_t m_width;
int32_t m_height;
int32_t m_mode;
int32_t m_xy_scale;
float m_z_step;
float m_z_depth;
float m_hue_options;
float m_z_state;
float m_hue_state;
float m_min, m_max;


void InitPerlin (void)
{
    // new Perlin (DISPLAY_WIDTH, DISPLAY_HEIGHT, 2, 8.0/64.0, 1.0/64.0, 256.0, 0.005);
	m_width = DISPLAY_WIDTH;
	m_height = DISPLAY_HEIGHT;
	m_mode = 2;
	m_xy_scale = 2.0/64.0*256.0;
	m_z_step = 1.0/64.0;
	m_z_depth = 256.0;
	m_hue_options = 0.005;
	m_z_state = 0;
	m_hue_state = 0;
	m_min = 1;
	m_max = 1;
}


void NextPerlin (void)
{
    int32_t x, y;
    uint16_t sx, sy;
	int32_t n1, n2;
	float n;
    int32_t hue;

	int16_t sz1 = (float)m_z_state * 256.0;
	int16_t sz2 = (float)(m_z_state - m_z_depth) * 256.0;

    // row
    for (y = 0; y < m_height; y++) {

        // scale y
        sy = y * m_xy_scale;

        // column
        for (x = 0; x < m_width; x++) {

            // scale x
            sx = x * m_xy_scale;

            // generate noise at plane z_state
            n1 = noise (sx, sy, sz1);

            // generate noise at plane z_state - z_depth
            n2 = noise (sx, sy, sz2);

            // combine noises to make a seamless transition from plane
            // at z = z_depth back to plane at z = 0
            n = ((m_z_depth - m_z_state) * (float)n1 + (m_z_state) * (float)n2) / m_z_depth;

            // normalize combined noises to a number between 0 and 1
            if (n > m_max) m_max = n;
            if (n < m_min) m_min = n;
            n = n + fabs (m_min);               // make noise a positive value
            n = n / (m_max + fabs (m_min));     // scale noise to between 0 and 1

            // set hue and/or brightness based on mode
            switch (m_mode) {

                // base hue fixed, varies based on noise
                case 1:
                    hue = (m_hue_options + n)*65536.0 + 0.5;
					levels[y][x].h = hue; // implicit mod 65536 here
                    break;

                // hue rotates at constant velocity, varies based on noise
                case 2:
                    hue = (m_hue_state + n)*65536.0 + 0.5;
					levels[y][x].h = hue; // implicit mod 65536 here
                    break;

                // hue rotates at constant velocity, brightness varies based on noise
                case 3:
                    hue = (m_hue_state)*65536.0 + 0.5;
					levels[y][x].h = hue; // implicit mod 65536 here
                    break;

            }
        }
    }

    // update state variables
    m_z_state = fmod (m_z_state + m_z_step, m_z_depth);
    m_hue_state = fmod (m_hue_state + m_hue_options, 1.0);
}


//---------------------------------------------------------------------------------------------
// noise
//

#define lerp1(t, a, b) (((a)<<12) + (t) * ((b) - (a)))

static const int8_t GRAD3[16][3] = {
    {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
    {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
    {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1},
    {1,0,-1},{-1,0,-1},{0,-1,1},{0,1,1}};

static const uint8_t PERM[512] = {
  151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140,
  36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120,
  234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33,
  88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71,
  134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133,
  230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161,
  1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130,
  116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250,
  124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227,
  47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44,
  154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98,
  108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34,
  242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14,
  239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121,
  50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243,
  141, 128, 195, 78, 66, 215, 61, 156, 180, 151, 160, 137, 91, 90, 15, 131,
  13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37,
  240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252,
  219, 203, 117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125,
  136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158,
  231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245,
  40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187,
  208, 89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198,
  173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126,
  255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223,
  183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167,
  43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185,
  112, 104, 218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179,
  162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199,
  106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236,
  205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156,
  180};

static const uint16_t easing_function_lut[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 3, 4, 6, 7,
    9, 10, 12, 14, 17, 19, 22, 25, 29, 32, 36, 40, 45, 49, 54, 60,
    65, 71, 77, 84, 91, 98, 105, 113, 121, 130, 139, 148, 158, 167, 178, 188,
    199, 211, 222, 234, 247, 259, 273, 286, 300, 314, 329, 344, 359, 374, 390, 407,
    424, 441, 458, 476, 494, 512, 531, 550, 570, 589, 609, 630, 651, 672, 693, 715,
    737, 759, 782, 805, 828, 851, 875, 899, 923, 948, 973, 998, 1023, 1049, 1074, 1100,
    1127, 1153, 1180, 1207, 1234, 1261, 1289, 1316, 1344, 1372, 1400, 1429, 1457, 1486, 1515,
    1543, 1572, 1602, 1631, 1660, 1690, 1719, 1749, 1778, 1808, 1838, 1868, 1898, 1928, 1958,
    1988, 2018, 2048, 2077, 2107, 2137, 2167, 2197, 2227, 2257, 2287, 2317, 2346, 2376, 2405,
    2435, 2464, 2493, 2523, 2552, 2580, 2609, 2638, 2666, 2695, 2723, 2751, 2779, 2806, 2834,
    2861, 2888, 2915, 2942, 2968, 2995, 3021, 3046, 3072, 3097, 3122, 3147, 3172, 3196, 3220,
    3244, 3267, 3290, 3313, 3336, 3358, 3380, 3402, 3423, 3444, 3465, 3486, 3506, 3525, 3545,
    3564, 3583, 3601, 3619, 3637, 3654, 3672, 3688, 3705, 3721, 3736, 3751, 3766, 3781, 3795,
    3809, 3822, 3836, 3848, 3861, 3873, 3884, 3896, 3907, 3917, 3928, 3937, 3947, 3956, 3965,
    3974, 3982, 3990, 3997, 4004, 4011, 4018, 4024, 4030, 4035, 4041, 4046, 4050, 4055, 4059,
    4063, 4066, 4070, 4073, 4076, 4078, 4081, 4083, 4085, 4086, 4088, 4089, 4091, 4092, 4092,
    4093, 4094, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095
};

static inline int16_t grad3(const uint8_t h, const int16_t x, const int16_t y, const int16_t z)
{
    return x * GRAD3[h][0] + y * GRAD3[h][1] + z * GRAD3[h][2];
}

int32_t noise (uint16_t x, uint16_t y, uint16_t z)
{
    uint8_t i0, j0, k0;     // integer part of (x, y, z)
    uint8_t i1, j1, k1;     // integer part plus one of (x, y, z)
    uint8_t xx, yy, zz;     // fractional part of (x, y, z)
    uint16_t fx, fy, fz;    // easing function result, add 4 LS bits

    // drop fractional part of each input
    i0 = x >> 8;
    j0 = y >> 8;
    k0 = z >> 8;

    // integer part plus one, wrapped between 0x00 and 0xff
    i1 = i0 + 1;
    j1 = j0 + 1;
    k1 = k0 + 1;

    // fractional part of each input
    xx = x & 0xff;
    yy = y & 0xff;
    zz = z & 0xff;

    // apply easing function
    fx = easing_function_lut[xx];
    fy = easing_function_lut[yy];
    fz = easing_function_lut[zz];

    uint8_t A, AA, AB, B, BA, BB;
    uint8_t CA, CB, CC, CD, CE, CF, CG, CH;

    // apply permutation functions
    A = PERM[i0];
    AA = PERM[A + j0];
    AB = PERM[A + j1];
    B = PERM[i1];
    BA = PERM[B + j0];
    BB = PERM[B + j1];
    CA = PERM[AA + k0] & 0xf;
    CB = PERM[BA + k0] & 0xf;
    CC = PERM[AB + k0] & 0xf;
    CD = PERM[BB + k0] & 0xf;
    CE = PERM[AA + k1] & 0xf;
    CF = PERM[BA + k1] & 0xf;
    CG = PERM[AB + k1] & 0xf;
    CH = PERM[BB + k1] & 0xf;

    // subtract 1.0 from xx, yy, zz
    int16_t xxm1 = xx - 256;
    int16_t yym1 = yy - 256;
    int16_t zzm1 = zz - 256;

    // result is -2 to exactly +2
    int16_t g1 = grad3 (CA, xx,   yy,   zz  );
    int16_t g2 = grad3 (CB, xxm1, yy,   zz  );
    int16_t g3 = grad3 (CC, xx,   yym1, zz  );
    int16_t g4 = grad3 (CD, xxm1, yym1, zz  );
    int16_t g5 = grad3 (CE, xx,   yy,   zzm1);
    int16_t g6 = grad3 (CF, xxm1, yy,   zzm1);
    int16_t g7 = grad3 (CG, xx,   yym1, zzm1);
    int16_t g8 = grad3 (CH, xxm1, yym1, zzm1);

    // linear interpolations
    int32_t l1 = lerp1(fx, g1, g2) >> 6;
    int32_t l2 = lerp1(fx, g3, g4) >> 6;
    int32_t l3 = lerp1(fx, g5, g6) >> 6;
    int32_t l4 = lerp1(fx, g7, g8) >> 6;

    int32_t l5 = lerp1(fy, l1, l2) >> 12;
    int32_t l6 = lerp1(fy, l3, l4) >> 12;

    int32_t l7 = lerp1(fz, l5, l6) >> 12;

	return l7;
}

