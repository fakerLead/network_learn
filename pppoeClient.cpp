#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <map>

#define BUFFER_LEN 2048
#define MAC_ADDR_LEN 6

#define FRAME_TYPE_PPPOE_DISCOVERY 0x8863
#define FRAME_TYPE_PPPOE_SESSION 0x8864
#define FRAME_TYPE_PADI_CODE 0x09
#define FRAME_TYPE_PADO_CODE 0x07
#define FRAME_TYPE_PADT_CODE 0xa7
#define FRAME_TYPE_PADR_CODE 0x19
#define FRAME_TYPE_PADS_CODE 0x65

#define FRAME_TYPE_PPP_SESSION_CODE 0x0

#define TAG_TYPE 0x0101

#define FRAME_LEN 128

struct MacAddr
{
	uint8_t addr[MAC_ADDR_LEN];
};

static uint8_t g_broadcastAddr[MAC_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static uint8_t g_serverName[4] = {0x00, 0x48, 0x40, 0x08};
static uint16_t g_sessionId = 0;
static std::map<uint16_t, MacAddr> g_sessionMap;

uint16_t u8ToU16(uint8_t a, uint8_t b)
{
	uint16_t dst = a;

	dst = dst << 8 | b;

	return dst;
}

void sendPadiFrame(int32_t fd, uint8_t *buffer, uint8_t *mac)
{
	uint8_t *ptrPadiFrame = new uint8_t[FRAME_LEN];

	memcpy(ptrPadiFrame, g_broadcastAddr, MAC_ADDR_LEN);
	memcpy(ptrPadiFrame + 6, mac, MAC_ADDR_LEN);

	//ethernet type
	ptrPadiFrame[12] = 0x88;
	ptrPadiFrame[13] = 0x63;

	//pppoe ver type
	ptrPadiFrame[14] = 0x11; 

	//code
	ptrPadiFrame[15] = FRAME_TYPE_PADI_CODE;

	//session id
	ptrPadiFrame[16] = 0x00;
	ptrPadiFrame[17] = 0x00;

	//payload length
	ptrPadiFrame[18] = 0x00;
	ptrPadiFrame[19] = 0x04;

	//tag type
	ptrPadiFrame[20] = 0x01;
	ptrPadiFrame[21] = 0x01;

	//Sevice Name
	memcpy(&ptrPadiFrame[22], g_serverName, 4);

	if (send(fd, ptrPadiFrame, 26, 0) < 0)
	{
		printf("send pads error \n");
	}

	delete []ptrPadiFrame;
	ptrPadiFrame = NULL;
}


int32_t main(int32_t argc, char **argv)
{
	int32_t fd = 0;
	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

	if (fd == -1)
	{
		printf("socket create error \n");
		return -1;
	}

	struct ifreq ifreq;
	uint8_t hostMac[MAC_ADDR_LEN]={0};

	strncpy(ifreq.ifr_name, "eth0", strlen("eth0"));

	if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0)
	{
		printf("get mac addr error \n");
		return -1;
	}

	printf("client mac:\n");
	memcpy(hostMac, ifreq.ifr_hwaddr.sa_data, sizeof(hostMac));
	for (uint8_t i = 0; i < MAC_ADDR_LEN; i++)
	{
		printf("%02x ", hostMac[i]);
	}
	printf("\n");
	
	ssize_t len = 0;
	uint8_t *buffer = new uint8_t[BUFFER_LEN];

	while (1)
	{


		len = recv(fd, buffer, BUFFER_LEN, MSG_TRUNC);
		
		if (-1 != len)
		{
			for (int32_t i = 0; i < 64; i++)
			{
				printf("%02x ", buffer[i]);
			}
			printf("\n");
			
			if (0 == memcmp(g_broadcastAddr, &buffer[0], MAC_ADDR_LEN))
			{
				printf("recv broadcast \n");

				//获取帧类型
				uint16_t ethType = u8ToU16(buffer[12], buffer[13]);
				
				printf("ethType <%u> \n", ethType);

				if (FRAME_TYPE_PPPOE == ethType)
				{
					uint8_t code = buffer[15];

					if (FRAME_TYPE_PADI == code)
					{
						ackPadiFrame(fd, buffer, ifreq);
					}	
				}
			}
			else if (0 == memcmp(ifreq.ifr_hwaddr.sa_data, &buffer[0], MAC_ADDR_LEN))
			{
				uint8_t code = buffer[15];

				if (FRAME_TYPE_PADR == code)
				{
					ackPadrFrame(fd, buffer, ifreq);
				}
				else if (FRAME_TYPE_PADT == code)
				{
					uint16_t sessionId = u8ToU16(buffer[16], buffer[17]);

					g_sessionMap.erase(sessionId);

				}
			}
		}
	}

	delete []buffer;
	buffer = NULL;
	close(fd);

	return 0;
}
