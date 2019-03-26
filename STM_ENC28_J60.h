#ifndef ENC28J60_H
#define ENC28J60_H

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>

#include "net.h"  // buffer/gPB offsets for field placements

#define ETHERCARD_DHCP 1
#define ETHERCARD_TCPCLIENT 0
#define ETHERCARD_ICMP 1
#define ETHERCARD_UDPSERVER 1
#define ETHERCARD_TCPSERVER 0


// Operations Defines
// SPI operation codes
#define ENC28J60_READ_CTRL_REG       0x00
#define ENC28J60_READ_BUF_MEM        0x3A
#define ENC28J60_WRITE_CTRL_REG      0x40
#define ENC28J60_WRITE_BUF_MEM       0x7A
#define ENC28J60_BIT_FIELD_SET       0x80
#define ENC28J60_BIT_FIELD_CLR       0xA0
#define ENC28J60_SOFT_RESET          0xFF

// Masks and some constants
// ENC28J60 Control Registers
// Control register definitions are a combination of address,
// bank number, and Ethernet/MAC/PHY indicator bits.
// - Register address        (bits 0-4)
// - Bank number        (bits 5-6)
// - MAC/PHY indicator        (bit 7)
#define ADDR_MASK        0x1F
#define BANK_MASK        0x60
#define SPRD_MASK        0x80

#define RXSTART_INIT							0x0000
#define RXSTOP_INIT								0x0BFF
#define TXSTART_INIT							0x0C00
#define TXSTOP_INIT								0x11FF

#define MAX_FRAMELEN							1500

#define MISTAT_BUSY								0x01

// Bank0 - control registers addresses
// These are updated from arduino lib
#define ERDPT           (0x00|0x00)
#define EWRPT           (0x02|0x00)
#define ETXST           (0x04|0x00)
#define ETXND           (0x06|0x00)
#define ERXST           (0x08|0x00)
#define ERXND           (0x0A|0x00)
#define ERXRDPT         (0x0C|0x00)
// #define ERXWRPT         (0x0E|0x00)
#define EDMAST          (0x10|0x00)
#define EDMAND          (0x12|0x00)
// #define EDMADST         (0x14|0x00)
#define EDMACS          (0x16|0x00)

// Bank1 - control registers addresses
// These are updated from arduino lib
#define EHT0            (0x00|0x20)
#define EHT1            (0x01|0x20)
#define EHT2            (0x02|0x20)
#define EHT3            (0x03|0x20)
#define EHT4            (0x04|0x20)
#define EHT5            (0x05|0x20)
#define EHT6            (0x06|0x20)
#define EHT7            (0x07|0x20)
#define EPMM0           (0x08|0x20)
#define EPMM1           (0x09|0x20)
#define EPMM2           (0x0A|0x20)
#define EPMM3           (0x0B|0x20)
#define EPMM4           (0x0C|0x20)
#define EPMM5           (0x0D|0x20)
#define EPMM6           (0x0E|0x20)
#define EPMM7           (0x0F|0x20)
#define EPMCS           (0x10|0x20)
// #define EPMO            (0x14|0x20)
#define EWOLIE          (0x16|0x20)
#define EWOLIR          (0x17|0x20)
#define ERXFCON         (0x18|0x20)
#define EPKTCNT         (0x19|0x20)

// Bank2 - control registers addresses
// These are updated from arduino lib
#define MACON1          (0x00|0x40|0x80)
#define MACON3          (0x02|0x40|0x80)
#define MACON4          (0x03|0x40|0x80)
#define MABBIPG         (0x04|0x40|0x80)
#define MAIPG           (0x06|0x40|0x80)
#define MACLCON1        (0x08|0x40|0x80)
#define MACLCON2        (0x09|0x40|0x80)
#define MAMXFL          (0x0A|0x40|0x80)
#define MAPHSUP         (0x0D|0x40|0x80)
#define MICON           (0x11|0x40|0x80)
#define MICMD           (0x12|0x40|0x80)
#define MIREGADR        (0x14|0x40|0x80)
#define MIWR            (0x16|0x40|0x80)
#define MIRD            (0x18|0x40|0x80)
#define MIRDL			(0x18|0x40|0x80)
#define MIRDH			(0x19|0x40|0x80)

// Bank3 - control registers addresses
#define MAADR1           (0x00|0x60|0x80)
#define MAADR0           (0x01|0x60|0x80)
#define MAADR3           (0x02|0x60|0x80)
#define MAADR2           (0x03|0x60|0x80)
#define MAADR5           (0x04|0x60|0x80)
#define MAADR4           (0x05|0x60|0x80)

#define EBSTSD          (0x06|0x60)
#define EBSTCON         (0x07|0x60)
#define EBSTCS          (0x08|0x60)
#define MISTAT          (0x0A|0x60|0x80)
#define EREVID          (0x12|0x60)
#define ECOCON          (0x15|0x60)
#define EFLOCON         (0x17|0x60)
#define EPAUS           (0x18|0x60)

// Common registers
// All-bank registers
#define EIE             0x1B
#define EIR             0x1C
#define ESTAT           0x1D
#define ECON2           0x1E
#define ECON1           0x1F

// BitField Defines
#define ECON1_BSEL0								0x01
#define ECON1_BSEL1								0x02
#define ESTAT_CLKRDY 							0x01
#define ECON2_PKTDEC							0x40
#define ECON2_AUTOINC							0x80
#define ECON1_RXEN								0x04
#define ECON1_TXRST								0x80
#define ECON1_TXRTS								0x08
#define ERXFCON_UCEN							0x80
#define ERXFCON_CRCEN							0x20
#define ERXFCON_PMEN							0x10
#define ERXFCON_BCEN							0x01 
#define ERXFCON_ANDOR							0x40
#define MACON1_MARXEN							0x01
#define MACON1_TXPAUS							0x08
#define MACON1_RXPAUS							0x04
#define MACON1_PASSALL						0x02
#define MACON3_PADCFG0						0x20
#define MACON3_TXCRCEN						0x10
#define MACON3_FRMLNEN						0x02
#define EIE_INTIE									0x80 
#define EIE_PKTIE									0x40
#define EIR_TXERIF								0x02
#define EIR_PKTIF 								0x40
#define EIR_TXIF									0x08
#define MICMD_MIIRD								0x01

// PHY layer
// ENC28J60 PHY PHCON1 Register Bit Definitions
#define PHCON1_PRST      0x8000
#define PHCON1_PLOOPBK   0x4000
#define PHCON1_PPWRSV    0x0800
#define PHCON1_PDPXMD    0x0100
// ENC28J60 PHY PHSTAT1 Register Bit Definitions
#define PHSTAT1_PFDPX    0x1000
#define PHSTAT1_PHDPX    0x0800
#define PHSTAT1_LLSTAT   0x0004
#define PHSTAT1_JBSTAT   0x0002
// ENC28J60 PHY PHCON2 Register Bit Definitions
#define PHCON2_FRCLINK   0x4000
#define PHCON2_TXDIS     0x2000
#define PHCON2_JABBER    0x0400
#define PHCON2_HDLDIS    0x0100
#define PHLCON_LED		 0x0122

// PHY registers
#define PHCON1           0x00
#define PHSTAT1          0x01
#define PHHID1           0x02
#define PHHID2           0x03
#define PHCON2           0x10
#define PHSTAT2          0x11
#define PHIE             0x12
#define PHIR             0x13
#define PHLCON           0x14

/** This type definition defines the structure of a UDP server event handler callback function */
typedef void (*UdpServerCallback)(
    uint16_t dest_port,    ///< Port the packet was sent to
    uint8_t src_ip[IP_LEN],    ///< IP address of the sender
    uint16_t src_port,    ///< Port the packet was sent from
    const char *data,   ///< UDP payload data
    uint16_t len);        ///< Length of the payload data

/** This type definition defines the structure of a DHCP Option callback function */
typedef void (*DhcpOptionCallback)(
    uint8_t option,     ///< The option number
    const uint8_t* data,   ///< DHCP option data
    uint8_t len);       ///< Length of the DHCP option data


// accessors to HAL GPIO defines
void enableChip (void);
void disableChip (void);

//Functions prototypes
uint8_t ENC28_readOp(uint8_t oper, uint8_t addr);
void ENC28_writeOp(uint8_t oper, uint8_t addr, uint8_t data);

uint8_t ENC28_readReg8( uint8_t addr);
void ENC28_writeReg8(uint8_t addr, uint8_t data);

uint16_t ENC28_readReg16( uint8_t addr);
void ENC28_writeReg16(uint8_t addrL, uint16_t data);

void ENC28_setBank(uint8_t addr);

uint8_t ENC28_Init(const uint8_t* macaddr);
bool ENC28J60_isLinkUp(void);
void ENC28J60_enableBroadcast (bool temporary);
void ENC28J60_disableBroadcast (bool temporary);

void ENC28_writePhy(uint8_t addr, uint16_t data);
uint8_t ENC28_readPhyByte(uint8_t addr);

void ENC28_packetSend(uint16_t len);
uint16_t ENC28J60_packetReceive(void);

void ENC28_writeBuf(uint16_t len, uint8_t* data);
void readBuf(uint16_t len, uint8_t* data);

void ENC28J60_setGwIp (const uint8_t *gwipaddr);
void EtherCard_updateBroadcastAddress(void);

bool EtherCard_staticSetup (const uint8_t* my_ip,
                             const uint8_t* gw_ip,
                             const uint8_t* dns_ip,
                             const uint8_t* mask);

void client_arp_whohas(uint8_t *ip_we_search);

bool is_lan(const uint8_t source[IP_LEN], const uint8_t destination[IP_LEN]);
uint8_t eth_type_is_ip_and_my_ip(uint16_t len);

uint8_t eth_type_is_arp_and_my_ip(uint16_t len);

void EtherCard_registerPingCallback (void (*callback)(uint8_t *srcip));


uint16_t ENC28J60_packetLoop(uint16_t plen);




bool EtherCard_dhcpSetup (const char *hname, bool fromRam);
void EtherCard_dhcpAddOptionCallback(uint8_t option, DhcpOptionCallback callback);
void EtherCard_DhcpStateMachine (uint16_t len);

void EtherCard_udpPrepare (uint16_t sport, const uint8_t *dip, uint16_t dport);
void EtherCard_udpTransmit (uint16_t datalen);

void udpServerListenOnPort(UdpServerCallback callback, uint16_t port);
void udpServerPauseListenOnPort(uint16_t port);
void udpServerResumeListenOnPort(uint16_t port);
bool udpServerListening();
bool udpServerHasProcessedPacket(uint16_t plen);

#endif
