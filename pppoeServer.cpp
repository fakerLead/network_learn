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

#define TAG_TYPE_SERVICE_NAME 0x0101
#define TAG_TYPE_HOST_UNIQ 0x103


#define FRAME_LEN 128

struct MacAddr
{
	uint8_t addr[MAC_ADDR_LEN];
};

static uint8_t g_broadcastAddr[MAC_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static uint16_t g_sessionId = 0;
static std::map<uint16_t, MacAddr> g_sessionMap;

uint16_t u8ToU16(uint8_t a, uint8_t b)
{
	uint16_t dst = a;

	dst = dst << 8 | b;

	return dst;
}

void ackPadiFrame(int32_t fd, uint8_t *buffer, uint8_t *mac)
{
	uint16_t tagType = u8ToU16(buffer[20], buffer[21]);

	printf("ackPadiFrame tagType <%u> \n", tagType);
	
	uint8_t *ptrPadoFrame = new uint8_t[FRAME_LEN];

	memcpy(ptrPadoFrame, &buffer[6], MAC_ADDR_LEN);
	memcpy(ptrPadoFrame + 6, mac, MAC_ADDR_LEN);

	//ethernet type
	ptrPadoFrame[12] = 0x88;
	ptrPadoFrame[13] = 0x63;

	//pppoe ver type
	ptrPadoFrame[14] = 0x11; 

	//code
	ptrPadoFrame[15] = FRAME_TYPE_PADO_CODE;

	//session id
	ptrPadoFrame[16] = 0x00;
	ptrPadoFrame[17] = 0x00;

	//payload length
	ptrPadoFrame[18] = 0x00;
	ptrPadoFrame[19] = 0x08;

	//tag type
	if (TAG_TYPE_HOST_UNIQ == tagType)
	{
		ptrPadoFrame[20] = buffer[20];
		ptrPadoFrame[21] = buffer[21];

		uint16_t tagLen = u8ToU16(buffer[22], buffer[23]);
		//Sevice Name
		memcpy(&ptrPadoFrame[22], &buffer[22], tagLen);
	}
	
	if (send(fd, ptrPadoFrame, 22 + tagLen, 0) < 0)
	{
		printf("send pado error \n");
	}

	delete []ptrPadoFrame;
	ptrPadoFrame = NULL;
}

void ackPadrFrame(int32_t fd, uint8_t *buffer, uint8_t *mac)
{
	uint16_t tagType = u8ToU16(buffer[20], buffer[21]);

	printf("ackPadrFrame tagType <%u> \n", tagType);

	uint8_t *ptrPadsFrame = new uint8_t[FRAME_LEN];

	memcpy(ptrPadsFrame, &buffer[6], MAC_ADDR_LEN);
	memcpy(ptrPadsFrame + 6, mac, MAC_ADDR_LEN);

	//ethernet type
	ptrPadsFrame[12] = 0x88;
	ptrPadsFrame[13] = 0x63;

	//pppoe ver type
	ptrPadsFrame[14] = 0x11; 

	//code
	ptrPadsFrame[15] = FRAME_TYPE_PADS_CODE; 

	//session id
	g_sessionId++;
	ptrPadsFrame[16] = (g_sessionId >> 8) & 0xff;
	ptrPadsFrame[17] = g_sessionId & 0xff;

	//保存mac地址，sessionid的对应关系
	MacAddr macAddr;
	memcpy(macAddr.addr, &buffer[6], MAC_ADDR_LEN);

	std::pair<std::map<uint16_t, MacAddr>::iterator, bool> ret;

	ret = g_sessionMap.insert(std::pair<uint16_t, MacAddr>( g_sessionId, macAddr));

	printf("g_sessionMap insert result <%d> \n", ret.second);

	//payload length
	ptrPadsFrame[18] = 0x00;
	ptrPadsFrame[19] = 0x04;

	//tag type host uniq
	ptrPadsFrame[20] = 0x01;
	ptrPadsFrame[21] = 0x03;

	//Sevice Name
	memcpy(&ptrPadsFrame[22], g_serverName, 4);

	if (send(fd, ptrPadsFrame, 26, 0) < 0)
	{
		printf("send pads error \n");
	}

	delete []ptrPadsFrame;
	ptrPadsFrame = NULL;
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

	strncpy(ifreq.ifr_name, "ens33", strlen("ens33"));

	if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0)
	{
		printf("get mac addr error \n");
		return -1;
	}

	printf("server mac:\n");
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
			for (int32_t i = 0; i < 14; i++)
			{
				printf("%02x ", buffer[i]);
			}
			printf("\n");
			
			if (0 == memcmp(g_broadcastAddr, &buffer[0], MAC_ADDR_LEN))
			{
				printf("recv broadcast \n");

				//获取帧类型
				uint16_t ethType = u8ToU16(buffer[12], buffer[13]);
				
				printf("ethType <%x> \n", ethType);

				if (FRAME_TYPE_PPPOE_DISCOVERY == ethType)
				{
					uint8_t code = buffer[15];

					if (FRAME_TYPE_PADI == code)
					{
						ackPadiFrame(fd, buffer, hostMac);
					}	
				}
			}
			else if (0 == memcmp(ifreq.ifr_hwaddr.sa_data, &buffer[0], MAC_ADDR_LEN))
			{
				printf("recv host mac \n");
				//获取帧类型
				uint16_t ethType = u8ToU16(buffer[12], buffer[13]);
				
				printf("ethType <%x> \n", ethType);

				if (FRAME_TYPE_PPPOE_DISCOVERY == ethType)
				{
					uint8_t code = buffer[15];

					if (FRAME_TYPE_PADR_CODE == code)
					{
						ackPadrFrame(fd, buffer, hostMac);
					}
					else if (FRAME_TYPE_PADT_CODE == code)
					{
						uint16_t sessionId = u8ToU16(buffer[16], buffer[17]);

						g_sessionMap.erase(sessionId);
					}
				}
				else if (FRAME_TYPE_PPPOE_SESSION == ethType)
				{
					uint8_t code = buffer[15];

					if (FRAME_TYPE_PPP_SESSION_CODE == code)
					{
						//session阶段

					}
				}
			}
		}
	}

	delete []buffer;
	buffer = NULL;
	close(fd);

	return 0;
}
