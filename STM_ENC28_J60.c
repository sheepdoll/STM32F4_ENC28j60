

#include "STM_ENC28_J60.h"

uint16_t bufferSize;
bool ENC28J60_broadcast_enabled = false;
bool ENC28J60_promiscuous_enabled = false;



static uint8_t waitgwmac; // Bitwise flags of gateway router status - see below for states
//Define gateway router ARP statuses
#define WGW_INITIAL_ARP 1 // First request, no answer yet
#define WGW_HAVE_GW_MAC 2 // Have gateway router MAC
#define WGW_REFRESHING 4 // Refreshing but already have gateway MAC
#define WGW_ACCEPT_ARP_REPLY 8 // Accept an ARP reply



static uint8_t arpreqhdr[] = { 0,1,8,0,6,4,0,1 }; // ARP request header
const unsigned char iphdr[]  = { 0x45,0,0,0x82,0,0,0x40,0,0x20 }; //IP header

const uint8_t allOnes[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // Used for hardware (MAC) and IP broadcast addresses

extern uint8_t mymac[];
uint8_t myip[IP_LEN];
uint8_t netmask[IP_LEN]; // subnet mask
uint8_t broadcastip[IP_LEN]; // broadcast address
uint8_t gwip[IP_LEN];
uint8_t dhcpip[IP_LEN]; // dhcp server
uint8_t dnsip[IP_LEN];  // dns server
uint8_t hisip[IP_LEN];  // ip address of remote host
uint16_t hisport = HTTP_PORT; // tcp port to browse to
bool using_dhcp = false;
bool persist_tcp_connection = false;
uint16_t delaycnt = 0; //request gateway ARP lookup

static void (*icmp_cb)(uint8_t *ip); // Pointer to callback function for ICMP ECHO response handler (triggers when localhost receives ping response (pong))

static uint8_t destmacaddr[ETH_LEN]; // storing both dns server and destination mac addresses, but at different times because both are never needed at same time.
static bool waiting_for_dns_mac = false; //might be better to use bit flags and bitmask operations for these conditions
static bool has_dns_mac = false;
static bool waiting_for_dest_mac = false;
static bool has_dest_mac = false;
uint8_t gwmacaddr[ETH_LEN];

/* 
	in the C++ land of arduino buffer[] resolves through defines and pointers
	to the following allocation gPB which is defined in dchp.cpp
*/
uint8_t gPB[500]; // also called buffer through a define directive


//variables
extern SPI_HandleTypeDef hspi1;
static uint8_t Enc28_Bank;




uint8_t ENC28_readOp(uint8_t oper, uint8_t addr)
{
	uint8_t spiData[2];
	enableChip();
	spiData[0] = (oper| (addr & ADDR_MASK));
	HAL_SPI_Transmit(&hspi1, spiData, 1, 100);
	if(addr & 0x80)
	{
		//HAL_SPI_Transmit(&hspi1, spiData, 1, 100);
		HAL_SPI_Receive(&hspi1, &spiData[1], 1, 100);
	}
	HAL_SPI_Receive(&hspi1, &spiData[1], 1, 100);
	disableChip();
	
	return spiData[1];
}
void ENC28_writeOp(uint8_t oper, uint8_t addr, uint8_t data)
{
	uint8_t spiData[2];
	enableChip();
	spiData[0] = (oper| (addr & ADDR_MASK)); //((oper<<5)&0xE0)|(addr & ADDR_MASK);
	spiData[1] = data;
	HAL_SPI_Transmit(&hspi1, spiData, 2, 100);
	disableChip();
}
uint8_t ENC28_readReg8(uint8_t addr)
{
	ENC28_setBank(addr);
	return ENC28_readOp(ENC28J60_READ_CTRL_REG, addr);
}

void ENC28_writeReg8(uint8_t addr, uint8_t data)
{
	ENC28_setBank(addr);
	ENC28_writeOp(ENC28J60_WRITE_CTRL_REG, addr, data);
}

uint16_t ENC28_readReg16( uint8_t addr)
{
	return ENC28_readReg8(addr) + (ENC28_readReg8(addr+1) << 8);
}

void ENC28_writeReg16(uint8_t addrL, uint16_t data)
{
	ENC28_writeReg8(addrL, data);
	ENC28_writeReg8(addrL+1, data >> 8);
}

void ENC28_setBank(uint8_t addr)
{
	if ((addr & BANK_MASK) != Enc28_Bank) 
	{
		ENC28_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_BSEL1|ECON1_BSEL0);
		Enc28_Bank = addr & BANK_MASK;
		ENC28_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, Enc28_Bank>>5);
	}
}

void ENC28_writePhy(uint8_t addr, uint16_t data)
{
	ENC28_writeReg8(MIREGADR, addr);
	ENC28_writeReg16(MIWR, data);
	while (ENC28_readReg8(MISTAT) & MISTAT_BUSY)
		;
}

uint8_t ENC28_readPhyByte(uint8_t addr)
{
	ENC28_writeReg8(MIREGADR, addr);							// pass the PHY address to the MII
	ENC28_writeReg8(MICMD, MICMD_MIIRD);					// Enable Read bit
	while (ENC28_readReg8(MISTAT) & MISTAT_BUSY)
		;	// Poll for end of reading
	ENC28_writeReg8(MICMD, 0x00);									// Disable MII Read
	
//	return ENC28_readReg8(MIRDL) | (ENC28_readReg8(MIRDH) << 8);
	return ENC28_readReg8(MIRDH);
}

uint8_t ENC28_Init(const uint8_t* macaddr) {
	/* in Arduino lib this function is called with a buffer size, mac address and CS pin
	 * this buffer does not seem to be used anywhere local
	uint8_t spiData[2];

	*/
	bufferSize = sizeof(gPB);  // sets up read buffer sise
	
	// (1): Disable the chip CS pin
	disableChip();
	HAL_Delay(1);

	// (2): Perform soft reset to the ENC28J60 module
	ENC28_writeOp(ENC28J60_SOFT_RESET, 0, ENC28J60_SOFT_RESET);
	HAL_Delay(2);

	// (3): Wait untill Clock is ready
	while(!ENC28_readOp(ENC28J60_READ_CTRL_REG, ESTAT) & ESTAT_CLKRDY)
		;

	// (4): Initialise RX and TX buffer size
	ENC28_writeReg16(ERXST, RXSTART_INIT);
	ENC28_writeReg16(ERXRDPT, RXSTART_INIT);
	ENC28_writeReg16(ERXND, RXSTOP_INIT);
	ENC28_writeReg16(ETXST, TXSTART_INIT);
	ENC28_writeReg16(ETXND, TXSTOP_INIT);
	
	// Arduino lib set this here
	// Stretch pulses for LED, LED_A=Link, LED_B=activity
	ENC28_writePhy(PHLCON, 0x476);

	
	// (5): Receive buffer filters
	ENC28_writeReg8(ERXFCON, ERXFCON_UCEN|ERXFCON_CRCEN|ERXFCON_PMEN|ERXFCON_BCEN);
	
	// additional Arduino setup
	ENC28_writeReg16(EPMM0, 0x303f); // pattern match filter
    ENC28_writeReg16(EPMCS, 0xf7f9); // pattern match checksum filter

	// (6): MAC Control Register 1
//	ENC28_writeReg8(MACON1, MACON1_MARXEN|MACON1_TXPAUS|MACON1_RXPAUS|MACON1_PASSALL);
// changed to 
	ENC28_writeReg8(MACON1, MACON1_MARXEN);

	// (7): MAC Control Register 3
	ENC28_writeOp(ENC28J60_BIT_FIELD_SET, MACON3,
		MACON3_PADCFG0|MACON3_TXCRCEN|MACON3_FRMLNEN);
 
  // (8): NON/Back to back gap
	ENC28_writeReg16(MAIPG, 0x0C12);  // NonBackToBack gap
	ENC28_writeReg8(MABBIPG, 0x12);  // BackToBack gap

	// (9): Set Maximum framelenght
	ENC28_writeReg16(MAMXFL, MAX_FRAMELEN);	// Set Maximum frame length (any packet bigger will be discarded)

	// (10): Set the MAC address of the device
	ENC28_writeReg8(MAADR5, macaddr[0]);
	ENC28_writeReg8(MAADR4, macaddr[1]);
	ENC28_writeReg8(MAADR3, macaddr[2]);
	ENC28_writeReg8(MAADR2, macaddr[3]);
	ENC28_writeReg8(MAADR1, macaddr[4]);
	ENC28_writeReg8(MAADR0, macaddr[5]);
	
	/* 
		could back check the MADDR registers and see if they are
		loaded with the correct MAC
	*/
	
	//**********Advanced Initialisations************//
	// (1): Initialise PHY layer registers
//	ENC28_writePhy(PHLCON, PHLCON_LED);
	ENC28_writePhy(PHCON2, PHCON2_HDLDIS);

	// (2): Enable Rx interrupt line
	ENC28_setBank(ECON1);

	ENC28_writeOp(ENC28J60_BIT_FIELD_SET, EIE, EIE_INTIE|EIE_PKTIE);
	ENC28_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_RXEN);

	// not used in Arduino code
	// ENC28_writeOp(ENC28J60_BIT_FIELD_SET, EIR, EIR_PKTIF);
	
	icmp_cb = NULL; // make sure this does not point to anything

	uint8_t rev  = ENC28_readReg8(EREVID);
    // microchip forgot to step the number on the silicon when they
    // released the revision B7. 6 is now rev B7. We still have
    // to see what they do when they release B8. At the moment
    // there is no B8 out yet
    if (rev > 5) ++rev; // implement arduino's revision value return.
		return rev;
	
}

bool ENC28J60_isLinkUp(void) {
    return (ENC28_readPhyByte(PHSTAT2) >> 2) & 1;
}

void ENC28J60_enableBroadcast (bool temporary) {
    ENC28_writeReg8(ERXFCON, ENC28_readReg8(ERXFCON) | ERXFCON_BCEN);
    if(!temporary)
    	ENC28J60_broadcast_enabled = true;
}

void ENC28J60_disableBroadcast (bool temporary) {
    if(!temporary)
    	ENC28J60_broadcast_enabled = false;
    if(!ENC28J60_broadcast_enabled)
        ENC28_writeReg8(ERXFCON, ENC28_readReg8(ERXFCON) & ~ERXFCON_BCEN);
}


void ENC28_packetSend(uint16_t len)
{
	uint8_t retry = 0;
	
	while(1)
	{
        // latest errata sheet: DS80349C
        // always reset transmit logic (Errata Issue 12)
        // the Microchip TCP/IP stack implementation used to first check
        // whether TXERIF is set and only then reset the transmit logic
        // but this has been changed in later versions; possibly they
        // have a reason for this; they don't mention this in the errata
        // sheet
		ENC28_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRST);
    	ENC28_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRST);
		ENC28_writeOp(ENC28J60_BIT_FIELD_CLR, EIR, EIR_TXERIF|EIR_TXIF);
		
		// prepare new transmission 
		if(retry == 0)
		{
			ENC28_writeReg16(EWRPT, TXSTART_INIT);
			ENC28_writeReg16(ETXND, TXSTART_INIT+len);
			ENC28_writeOp(ENC28J60_WRITE_BUF_MEM, 0, 0);  //line 485 enc28j60.cpp
			ENC28_writeBuf(len, gPB);
		}
		
		// initiate transmission
		ENC28_writeOp(ENC28J60_BIT_FIELD_SET, ECON1, ECON1_TXRTS);

       // wait until transmission has finished; referring to the data sheet and
        // to the errata (Errata Issue 13; Example 1) you only need to wait until either
        // TXIF or TXERIF gets set; however this leads to hangs; apparently Microchip
        // realized this and in later implementations of their tcp/ip stack they introduced
        // a counter to avoid hangs; of course they didn't update the errata sheet
 		uint16_t count = 0;
		while ((ENC28_readReg8(EIR) & (EIR_TXIF | EIR_TXERIF)) == 0 && ++count < 1000U);
		if (!(ENC28_readReg8(EIR) & EIR_TXERIF) && count < 1000U) 
		{
			// no error; start new transmission
			break;
		}

		// cancel previous transmission if stuck
		ENC28_writeOp(ENC28J60_BIT_FIELD_CLR, ECON1, ECON1_TXRTS);
			break;
	
		retry++;  // from Arduino enc28j60.cpp
	}
}


uint16_t ENC28J60_packetReceive(void) {
    static uint16_t gNextPacketPtr = RXSTART_INIT;
    static bool     unreleasedPacket = false;
    uint16_t len = 0;

    if (unreleasedPacket) {
        if (gNextPacketPtr == 0)
            ENC28_writeReg16(ERXRDPT, RXSTOP_INIT);
        else
            ENC28_writeReg16(ERXRDPT, gNextPacketPtr - 1);
        unreleasedPacket = false;
    }

    if (ENC28_readReg8(EPKTCNT) > 0) {
        ENC28_writeReg16(ERDPT, gNextPacketPtr);

        struct {
            uint16_t nextPacket;
            uint16_t byteCount;
            uint16_t status;
        } header;

        readBuf(sizeof header, (uint8_t*) &header);

        gNextPacketPtr  = header.nextPacket;
        len = header.byteCount - 4; //remove the CRC count
        if (len>bufferSize-1)
            len=bufferSize-1;
        if ((header.status & 0x80)==0)
            len = 0;
        else
            readBuf(len, gPB);
        gPB[len] = 0;
        unreleasedPacket = true;

        ENC28_writeOp(ENC28J60_BIT_FIELD_SET, ECON2, ECON2_PKTDEC);
    }
    return len;
}

void readBuf(uint16_t len, uint8_t* data) {
 //   uint8_t nextbyte;
	uint8_t spiData[2];


    enableChip();
    if (len != 0) {

		spiData[0] = ENC28J60_READ_BUF_MEM;
		HAL_SPI_Transmit(&hspi1, spiData, 1, 100);
		
		HAL_SPI_Receive(&hspi1, data, len, 100);

      }
    disableChip();

}

void ENC28_writeBuf(uint16_t len, uint8_t* data)
{
	uint8_t spiData[2];
	// enable chip
	enableChip();

	spiData[0] = ENC28J60_WRITE_BUF_MEM;
	HAL_SPI_Transmit(&hspi1, spiData, 1, 100);

//	spiData[1] = 0xFF;
//	HAL_SPI_Transmit(&hspi1, &spiData[1], 1, 100);

	HAL_SPI_Transmit(&hspi1, data, len, 100);

	// disable chip
	disableChip();
}


/* 
	this code should be part of tcpip.c  it is derived from tcpip.cpp

	Our target is ip and udp, we at the moment have no interest in
	TCP other than academic.

	IP, ARP, UDP and !TCP functions.
	Author: Guido Socher
	Copyright: GPL V2

	The TCP implementation uses some size optimisations which are valid
	only if all data can be sent in one single packet. This is however
	not a big limitation for a microcontroller as you will anyhow use
	small web-pages. The web server must send the entire web page in one
	packet. The client "web browser" as implemented here can also receive
	large pages.

	2010-05-20 <jc@wippler.nl>
*/

static void fill_checksum(uint8_t dest, uint8_t off, uint16_t len,uint8_t type) {
    const uint8_t* ptr = gPB + off;
    uint32_t sum = type==1 ? IP_PROTO_UDP_V+len-8 :
                   type==2 ? IP_PROTO_TCP_V+len-8 : 0;
    while(len >1) {
        sum += (uint16_t) (((uint32_t)*ptr<<8)|*(ptr+1));
        ptr+=2;
        len-=2;
    }
    if (len)
        sum += ((uint32_t)*ptr)<<8;
    while (sum>>16)
        sum = (uint16_t) sum + (sum >> 16);
    uint16_t ck = ~ (uint16_t) sum;
    gPB[dest] = ck>>8;
    gPB[dest+1] = ck;
}

static void setMACs (const uint8_t *mac) {
    memcpy(gPB + ETH_DST_MAC, mac, ETH_LEN);
    memcpy(gPB + ETH_SRC_MAC, mymac, ETH_LEN);
}

static void setMACandIPs (const uint8_t *mac, const uint8_t *dst) {
    setMACs(mac);
    memcpy(gPB + IP_DST_P, dst, IP_LEN);
    memcpy(gPB + IP_SRC_P, myip, IP_LEN);
}

void ENC28J60_setGwIp (const uint8_t *gwipaddr) {
    delaycnt = 0; //request gateway ARP lookup
    waitgwmac = WGW_INITIAL_ARP; // causes an arp request in the packet loop
    memcpy(gwip, gwipaddr,IP_LEN);
}

void EtherCard_updateBroadcastAddress(void)
{
    for(uint8_t i=0; i<IP_LEN; i++)
        broadcastip[i] = myip[i] | ~netmask[i];
}


bool EtherCard_staticSetup (const uint8_t* my_ip,
                             const uint8_t* gw_ip,
                             const uint8_t* dns_ip,
                             const uint8_t* mask) {
    using_dhcp = false;

    if (my_ip != 0)
        memcpy(myip, my_ip,IP_LEN);
    if (gw_ip != 0)
        ENC28J60_setGwIp(gw_ip);
    if (dns_ip != 0)
        memcpy(dnsip, dns_ip,IP_LEN);
    if(mask != 0)
        memcpy(netmask, mask,IP_LEN);
    EtherCard_updateBroadcastAddress();
    delaycnt = 0; //request gateway ARP lookup
    return true;
}


void make_arp_answer_from_request(void) {
	setMACs(gPB + ETH_SRC_MAC);
    gPB[ETH_ARP_OPCODE_H_P] = ETH_ARP_OPCODE_REPLY_H_V;
    gPB[ETH_ARP_OPCODE_L_P] = ETH_ARP_OPCODE_REPLY_L_V;
    memcpy(gPB + ETH_ARP_DST_MAC_P, gPB + ETH_ARP_SRC_MAC_P, ETH_LEN);
	memcpy(gPB + ETH_ARP_SRC_MAC_P, mymac, ETH_LEN);
    memcpy(gPB + ETH_ARP_DST_IP_P, gPB + ETH_ARP_SRC_IP_P, IP_LEN);
    memcpy(gPB + ETH_ARP_SRC_IP_P, myip, IP_LEN);
    ENC28_packetSend(42);
}



void client_arp_whohas(uint8_t *ip_we_search) {
/*
	setMACs(allOnes);
    gPB[ETH_TYPE_H_P] = ETHTYPE_ARP_H_V;
    gPB[ETH_TYPE_L_P] = ETHTYPE_ARP_L_V;
    memcpy_P(gPB + ETH_ARP_P, arpreqhdr, sizeof arpreqhdr);
    memset(gPB + ETH_ARP_DST_MAC_P, 0, ETH_LEN);
    EtherCard::copyMac(gPB + ETH_ARP_SRC_MAC_P, EtherCard::mymac);
    EtherCard::copyIp(gPB + ETH_ARP_DST_IP_P, ip_we_search);
    EtherCard::copyIp(gPB + ETH_ARP_SRC_IP_P, EtherCard::myip);
    EtherCard::packetSend(42);

*/
	// all ones function
	setMACs(allOnes);
  	gPB[ETH_TYPE_H_P] = ETHTYPE_ARP_H_V;
    gPB[ETH_TYPE_L_P] = ETHTYPE_ARP_L_V;
    memcpy(gPB + ETH_ARP_P, arpreqhdr, sizeof arpreqhdr);
    memset(gPB + ETH_ARP_DST_MAC_P, 0, ETH_LEN);
	memcpy(gPB + ETH_ARP_SRC_MAC_P, mymac, ETH_LEN); // copyMac
	memcpy(gPB + ETH_ARP_DST_IP_P, ip_we_search, IP_LEN); // copyIp
	memcpy(gPB + ETH_ARP_SRC_IP_P, myip, IP_LEN); // copyIp
    ENC28_packetSend(42);

}

static void fill_ip_hdr_checksum() {
    gPB[IP_CHECKSUM_P] = 0;
    gPB[IP_CHECKSUM_P+1] = 0;
    gPB[IP_FLAGS_P] = 0x40; // don't fragment
    gPB[IP_FLAGS_P+1] = 0;  // fragment offset
    gPB[IP_TTL_P] = 64; // ttl
    fill_checksum(IP_CHECKSUM_P, IP_P, IP_HEADER_LEN,0);
}


static void make_eth_ip() {
    setMACs(gPB + ETH_SRC_MAC);
    memcpy(gPB + IP_DST_P, gPB + IP_SRC_P, IP_LEN);
  	memcpy(gPB + IP_SRC_P, myip, IP_LEN);
    fill_ip_hdr_checksum();
}

static void make_echo_reply_from_request(uint16_t len) {
    make_eth_ip();
    gPB[ICMP_TYPE_P] = ICMP_TYPE_ECHOREPLY_V;
    if (gPB[ICMP_CHECKSUM_P] > (0xFF-0x08))
        gPB[ICMP_CHECKSUM_P+1]++;
    gPB[ICMP_CHECKSUM_P] += 0x08;
    ENC28_packetSend(len);
}

uint8_t client_store_mac(uint8_t *source_ip, uint8_t *mac) {
    if (memcmp(gPB + ETH_ARP_SRC_IP_P, source_ip, IP_LEN) != 0)
        return 0;
    memcpy(mac, gPB + ETH_ARP_SRC_MAC_P, ETH_LEN);
    return 1;
}


bool is_lan(const uint8_t source[IP_LEN], const uint8_t destination[IP_LEN]) {
    if(source[0] == 0 || destination[0] == 0) {
        return false;
    }
    for(int i = 0; i < IP_LEN; i++)
        if((source[i] & netmask[i]) != (destination[i] & netmask[i])) {
            return false;
        }
    return true;
}


uint8_t eth_type_is_arp_and_my_ip(uint16_t len) {
    return len >= 41 && gPB[ETH_TYPE_H_P] == ETHTYPE_ARP_H_V &&
           gPB[ETH_TYPE_L_P] == ETHTYPE_ARP_L_V &&
           memcmp(gPB + ETH_ARP_DST_IP_P, myip, IP_LEN) == 0;
}


uint8_t eth_type_is_ip_and_my_ip(uint16_t len) {
    return len >= 42 && gPB[ETH_TYPE_H_P] == ETHTYPE_IP_H_V &&
           gPB[ETH_TYPE_L_P] == ETHTYPE_IP_L_V &&
           gPB[IP_HEADER_LEN_VER_P] == 0x45 &&
           (memcmp(gPB + IP_DST_P, myip, IP_LEN) == 0  //not my IP
            || (memcmp(gPB + IP_DST_P, broadcastip, IP_LEN) == 0) //not subnet broadcast
            || (memcmp(gPB + IP_DST_P, allOnes, IP_LEN) == 0)); //not global broadcasts
    //!@todo Handle multicast
}




/* 
	in the off chance we want to
	do something when pinged
*/
void EtherCard_registerPingCallback (void (*callback)(uint8_t *srcip)) {
    icmp_cb = callback;
}


uint16_t ENC28J60_packetLoop(uint16_t plen) {
//    uint16_t len;

#if ETHERCARD_DHCP
    if(using_dhcp) {
        EtherCard_DhcpStateMachine(plen);
    }
#endif

    if (plen==0) {
        //Check every 65536 (no-packet) cycles whether we need to retry ARP request for gateway
        if (((waitgwmac & WGW_INITIAL_ARP) || (waitgwmac & WGW_REFRESHING)) &&
                delaycnt==0 && ENC28J60_isLinkUp()) {
            client_arp_whohas(gwip);
            waitgwmac |= WGW_ACCEPT_ARP_REPLY;
        }
        delaycnt++;

#if ETHERCARD_TCPCLIENT
        //Initiate TCP/IP session if pending
        if (tcp_client_state==TCP_STATE_SENDSYN && (waitgwmac & WGW_HAVE_GW_MAC)) { // send a syn
            tcp_client_state = TCP_STATE_SYNSENT;
            tcpclient_src_port_l++; // allocate a new port
            client_syn(((tcp_fd<<5) | (0x1f & tcpclient_src_port_l)),tcp_client_port_h,tcp_client_port_l);
        }
#endif

       
        //!@todo this is trying to find mac only once. Need some timeout to make another call if first one doesn't succeed.
        if(is_lan(myip, dnsip) && !has_dns_mac && !waiting_for_dns_mac) {
            client_arp_whohas(dnsip);
            waiting_for_dns_mac = true;
        }

        //!@todo this is trying to find mac only once. Need some timeout to make another call if first one doesn't succeed.
        if(is_lan(myip, hisip) && !has_dest_mac && !waiting_for_dest_mac) {
            client_arp_whohas(hisip);
            waiting_for_dest_mac = true;
        }
		
        return 0;
    }

	// at this point plen should not be zero

    if (eth_type_is_arp_and_my_ip(plen))
    {   //Service ARP request
        if (gPB[ETH_ARP_OPCODE_L_P]==ETH_ARP_OPCODE_REQ_L_V)
            make_arp_answer_from_request();
        if ((waitgwmac & WGW_ACCEPT_ARP_REPLY) && (gPB[ETH_ARP_OPCODE_L_P]==ETH_ARP_OPCODE_REPLY_L_V)
        	 && client_store_mac(gwip, gwmacaddr))
            waitgwmac = WGW_HAVE_GW_MAC;

        if (!has_dns_mac && waiting_for_dns_mac && client_store_mac(dnsip, destmacaddr)) {
            has_dns_mac = true;
            waiting_for_dns_mac = false;
        }
        if (!has_dest_mac && waiting_for_dest_mac && client_store_mac(hisip, destmacaddr)) {
            has_dest_mac = true;
            waiting_for_dest_mac = false;
        }

        return 0;
    }

    if (eth_type_is_ip_and_my_ip(plen)==0)
    {   //Not IP so ignoring
        //!@todo Add other protocols (and make each optional at compile time)
        return 0;
    }


#if ETHERCARD_ICMP
    if (gPB[IP_PROTO_P]==IP_PROTO_ICMP_V && gPB[ICMP_TYPE_P]==ICMP_TYPE_ECHOREQUEST_V)
    {   //Service ICMP echo request (ping)
        if (icmp_cb)
            (*icmp_cb)(&(gPB[IP_SRC_P]));
        make_echo_reply_from_request(plen);
        return 0;
    }
#endif

#if ETHERCARD_UDPSERVER
    if (udpServerListening() && gPB[IP_PROTO_P]==IP_PROTO_UDP_V)
    {   //Call UDP server handler (callback) if one is defined for this packet
        if(udpServerHasProcessedPacket(plen))
            return 0; //An UDP server handler (callback) has processed this packet
    }
#endif

    if (plen<54 || gPB[IP_PROTO_P]!=IP_PROTO_TCP_V )
        return 0; //from here on we are only interested in TCP-packets; these are longer than 54 bytes

#if ETHERCARD_TCPCLIENT
    if (gPB[TCP_DST_PORT_H_P]==TCPCLIENT_SRC_PORT_H)
    {   //Source port is in range reserved (by EtherCard) for client TCP/IP connections
        if (check_ip_message_is_from(hisip)==0)
            return 0; //Not current TCP/IP connection (only handle one at a time)
        if (gPB[TCP_FLAGS_P] & TCP_FLAGS_RST_V)
        {   //TCP reset flagged
            if (client_tcp_result_cb)
                (*client_tcp_result_cb)((gPB[TCP_DST_PORT_L_P]>>5)&0x7,3,0,0);
            tcp_client_state = TCP_STATE_CLOSING;
            return 0;
        }
        len = getTcpPayloadLength();
        if (tcp_client_state==TCP_STATE_SYNSENT)
        {   //Waiting for SYN-ACK
            if ((gPB[TCP_FLAGS_P] & TCP_FLAGS_SYN_V) && (gPB[TCP_FLAGS_P] &TCP_FLAGS_ACK_V))
            {   //SYN and ACK flags set so this is an acknowledgement to our SYN
                make_tcp_ack_from_any(0,0);
                gPB[TCP_FLAGS_P] = TCP_FLAGS_ACK_V|TCP_FLAGS_PUSH_V;
                if (client_tcp_datafill_cb)
                    len = (*client_tcp_datafill_cb)((gPB[TCP_SRC_PORT_L_P]>>5)&0x7);
                else
                    len = 0;
                tcp_client_state = TCP_STATE_ESTABLISHED;
                make_tcp_ack_with_data_noflags(len);
            }
            else
            {   //Expecting SYN+ACK so reset and resend SYN
                tcp_client_state = TCP_STATE_SENDSYN; // retry
                len++;
                if (gPB[TCP_FLAGS_P] & TCP_FLAGS_ACK_V)
                    len = 0;
                make_tcp_ack_from_any(len,TCP_FLAGS_RST_V);
            }
            return 0;
        }
        if (tcp_client_state==TCP_STATE_ESTABLISHED && len>0)
        {   //TCP connection established so read data
            if (client_tcp_result_cb) {
                uint16_t tcpstart = TCP_DATA_START; // TCP_DATA_START is a formula
                if (tcpstart>plen-8)
                    tcpstart = plen-8; // dummy but save
                uint16_t save_len = len;
                if (tcpstart+len>plen)
                    save_len = plen-tcpstart;
                (*client_tcp_result_cb)((gPB[TCP_DST_PORT_L_P]>>5)&0x7,0,tcpstart,save_len); //Call TCP handler (callback) function

                if(persist_tcp_connection)
                {   //Keep connection alive by sending ACK
                    make_tcp_ack_from_any(len,TCP_FLAGS_PUSH_V);
                }
                else
                {   //Close connection
                    make_tcp_ack_from_any(len,TCP_FLAGS_PUSH_V|TCP_FLAGS_FIN_V);
                    tcp_client_state = TCP_STATE_CLOSED;
                }
                return 0;
            }
        }
        if (tcp_client_state != TCP_STATE_CLOSING)
        {   //
            if (gPB[TCP_FLAGS_P] & TCP_FLAGS_FIN_V) {
                if(tcp_client_state == TCP_STATE_ESTABLISHED) {
                    return 0; // In some instances FIN is received *before* DATA.  If that is the case, we just return here and keep looking for the data packet
                }
                make_tcp_ack_from_any(len+1,TCP_FLAGS_PUSH_V|TCP_FLAGS_FIN_V);
                tcp_client_state = TCP_STATE_CLOSED; // connection terminated
            } else if (len>0) {
                make_tcp_ack_from_any(len,0);
            }
        }
        return 0;
    }
#endif

#if ETHERCARD_TCPSERVER
    //If we are here then this is a TCP/IP packet targeted at us and not related to our client connection so accept
    return accept(hisport, plen);
#else
    return 0;
#endif
}

/* 
	the UDP stuff should be in it's own file too 

*/
void EtherCard_udpPrepare (uint16_t sport, const uint8_t *dip, uint16_t dport) {
    if(is_lan(myip, dip)) {                    // this works because both dns mac and destinations mac are stored in same variable - destmacaddr
        setMACandIPs(destmacaddr, dip);        // at different times. The program could have separate variable for dns mac, then here should be
    } else {                                   // checked if dip is dns ip and separately if dip is hisip and then use correct mac.
        setMACandIPs(gwmacaddr, dip);
    }
    // see http://tldp.org/HOWTO/Multicast-HOWTO-2.html
    // multicast or broadcast address, https://github.com/njh/EtherCard/issues/59
    if ((dip[0] & 0xF0) == 0xE0 || *((unsigned long*) dip) == 0xFFFFFFFF || !memcmp(broadcastip,dip,IP_LEN))
    	memcpy(gPB + ETH_DST_MAC, allOnes,ETH_LEN);
    gPB[ETH_TYPE_H_P] = ETHTYPE_IP_H_V;
    gPB[ETH_TYPE_L_P] = ETHTYPE_IP_L_V;
    memcpy(gPB + IP_P,iphdr,sizeof iphdr);
    gPB[IP_TOTLEN_H_P] = 0;
    gPB[IP_PROTO_P] = IP_PROTO_UDP_V;
    gPB[UDP_DST_PORT_H_P] = (dport>>8);
    gPB[UDP_DST_PORT_L_P] = dport;
    gPB[UDP_SRC_PORT_H_P] = (sport>>8);
    gPB[UDP_SRC_PORT_L_P] = sport;
    gPB[UDP_LEN_H_P] = 0;
    gPB[UDP_CHECKSUM_H_P] = 0;
    gPB[UDP_CHECKSUM_L_P] = 0;
}

void EtherCard_udpTransmit (uint16_t datalen) {
    gPB[IP_TOTLEN_H_P] = (IP_HEADER_LEN+UDP_HEADER_LEN+datalen) >> 8;
    gPB[IP_TOTLEN_L_P] = IP_HEADER_LEN+UDP_HEADER_LEN+datalen;
    fill_ip_hdr_checksum();
    gPB[UDP_LEN_H_P] = (UDP_HEADER_LEN+datalen) >>8;
    gPB[UDP_LEN_L_P] = UDP_HEADER_LEN+datalen;
    fill_checksum(UDP_CHECKSUM_H_P, IP_SRC_P, 16 + datalen,1);
    ENC28_packetSend(UDP_HEADER_LEN+IP_HEADER_LEN+ETH_HEADER_LEN+datalen);
}


