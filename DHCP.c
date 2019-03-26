/*
 * DHCP.c
 *
 *  Created on: Mar 24, 2019
 *      Author: Arethusa
 */

#include "DHCP.h"


extern uint8_t gPB[500];
extern bool using_dhcp;

extern uint8_t mymac[];
extern uint8_t myip[IP_LEN];


extern uint8_t dhcpip[IP_LEN]; // dhcp server
extern uint8_t netmask[IP_LEN]; // subnet mask
extern uint8_t gwip[IP_LEN];
extern uint8_t dnsip[IP_LEN];  // dns server

extern uint16_t delaycnt;

#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTRESPONSE 2

// DHCP Message Type (option 53) (ref RFC 2132)
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_DECLINE 4
#define DHCP_ACK 5
#define DHCP_NAK 6
#define DHCP_RELEASE 7

// DHCP States for access in applications (ref RFC 2131)
enum {
    DHCP_STATE_INIT,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_RENEWING,
};

/*
   op            1  Message op code / message type.
                    1 = BOOTREQUEST, 2 = BOOTREPLY
   htype         1  Hardware address type, see ARP section in "Assigned
                    Numbers" RFC; e.g., '1' = 10mb ethernet.
   hlen          1  Hardware address length (e.g.  '6' for 10mb
                    ethernet).
   hops          1  Client sets to zero, optionally used by relay agents
                    when booting via a relay agent.
   xid           4  Transaction ID, a random number chosen by the
                    client, used by the client and server to associate
                    messages and responses between a client and a
                    server.
   secs          2  Filled in by client, seconds elapsed since client
                    began address acquisition or renewal process.
   flags         2  Flags (see figure 2).
   ciaddr        4  Client IP address; only filled in if client is in
                    BOUND, RENEW or REBINDING state and can respond
                    to ARP requests.
   yiaddr        4  'your' (client) IP address.
   siaddr        4  IP address of next server to use in bootstrap;
                    returned in DHCPOFFER, DHCPACK by server.
   giaddr        4  Relay agent IP address, used in booting via a
                    relay agent.
   chaddr       16  Client hardware address.
   sname        64  Optional server host name, null terminated string.
   file        128  Boot file name, null terminated string; "generic"
                    name or null in DHCPDISCOVER, fully qualified
                    directory-path name in DHCPOFFER.
   options     var  Optional parameters field.  See the options
                    documents for a list of defined options.
 */




// size 236
typedef struct {
	uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t ciaddr[IP_LEN], yiaddr[IP_LEN], siaddr[IP_LEN], giaddr[IP_LEN];
    uint8_t chaddr[16], sname[64], file[128];
    // options
} DHCPdata;

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

// timeouts im ms
#define DHCP_REQUEST_TIMEOUT 10000

#define DHCP_HOSTNAME_MAX_LEN 32

// RFC 2132 Section 3.3:
// The time value of 0xffffffff is reserved to represent "infinity".
#define DHCP_INFINITE_LEASE  0xffffffff

static uint8_t dhcpState = DHCP_STATE_INIT;
static char hostname[DHCP_HOSTNAME_MAX_LEN] = "STM32F4-ENC28j60-00";   // Last two characters will be filled by last 2 MAC digits ;
static uint32_t currentXid;
static uint32_t stateTimer;
static uint32_t leaseStart;
static uint32_t leaseTime;
static uint8_t* bufPtr;

static uint8_t dhcpCustomOptionNum = 0;
static DhcpOptionCallback dhcpCustomOptionCallback = NULL;

extern uint8_t allOnes[];// = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void addToBuf (uint8_t b) {
    *bufPtr++ = b;
}

static void addBytes (uint8_t len, const uint8_t* data) {
    while (len-- > 0)
        addToBuf(*data++);
}

static void addOption (uint8_t opt, uint8_t len, const uint8_t* data) {
    addToBuf(opt);
    addToBuf(len);
    addBytes(len, data);
}

// Main DHCP sending function

// implemented
// state             / msgtype
// INIT              / DHCPDISCOVER
// SELECTING         / DHCPREQUEST
// BOUND (RENEWING)  / DHCPREQUEST

// ----------------------------------------------------------
// |              |SELECTING    |RENEWING     |INIT         |
// ----------------------------------------------------------
// |broad/unicast |broadcast    |unicast      |broadcast    |
// |server-ip     |MUST         |MUST NOT     |MUST NOT     | option 54
// |requested-ip  |MUST         |MUST NOT     |MUST NOT     | option 50
// |ciaddr        |zero         |IP address   |zero         |
// ----------------------------------------------------------

// options used (both send/receive)
#define DHCP_OPT_SUBNET_MASK            1
#define DHCP_OPT_ROUTERS                3
#define DHCP_OPT_DOMAIN_NAME_SERVERS    6
#define DHCP_OPT_HOSTNAME               12
#define DHCP_OPT_REQUESTED_ADDRESS      50
#define DHCP_OPT_LEASE_TIME             51
#define DHCP_OPT_MESSAGE_TYPE           53
#define DHCP_OPT_SERVER_IDENTIFIER      54
#define DHCP_OPT_PARAMETER_REQUEST_LIST 55
#define DHCP_OPT_RENEWAL_TIME           58
#define DHCP_OPT_CLIENT_IDENTIFIER      61
#define DHCP_OPT_END                    255

#define DHCP_HTYPE_ETHER 1

static void send_dhcp_message(uint8_t *requestip) {

    memset(gPB, 0, UDP_DATA_P + sizeof( DHCPdata ));

    EtherCard_udpPrepare(DHCP_CLIENT_PORT,
                          (dhcpState == DHCP_STATE_BOUND ? dhcpip : allOnes),
                          DHCP_SERVER_PORT);

    // If we ever don't do this, the DHCP renewal gets sent to whatever random
    // destmacaddr was used by other code. Rather than cache the MAC address of
    // the DHCP server, just force a broadcast here in all cases.
    memcpy(gPB + ETH_DST_MAC, allOnes, ETH_LEN); //force broadcast mac

    // Build DHCP Packet from buf[UDP_DATA_P]
    DHCPdata *dhcpPtr = (DHCPdata*) (gPB + UDP_DATA_P);
    dhcpPtr->op = DHCP_BOOTREQUEST;
    dhcpPtr->htype = 1;
    dhcpPtr->hlen = 6;
    dhcpPtr->xid = currentXid;
    if (dhcpState == DHCP_STATE_BOUND) {
        memcpy(dhcpPtr->ciaddr, myip, IP_LEN);
    }
    memcpy(dhcpPtr->chaddr, mymac, ETH_LEN);

    // options defined as option, length, value
    bufPtr = gPB + UDP_DATA_P + sizeof( DHCPdata );
    /* DHCP magic cookie
    static const uint8_t cookie[] PROGMEM = { 0x63,0x82,0x53,0x63 };
    for (uint8_t i = 0; i < sizeof(cookie); i++)
        addToBuf(pgm_read_byte(&cookie[i]));
    */
    static const uint8_t cookie[] = { 0x63,0x82,0x53,0x63 };
    for (uint8_t i = 0; i < sizeof(cookie); i++)
        addToBuf(cookie[i]);
       
    addToBuf(DHCP_OPT_MESSAGE_TYPE); // DHCP_STATE_SELECTING, DHCP_STATE_REQUESTING
    addToBuf(1);   // Length
    addToBuf(dhcpState == DHCP_STATE_INIT ? DHCP_DISCOVER : DHCP_REQUEST);

    // Client Identifier Option, this is the client mac address
    addToBuf(DHCP_OPT_CLIENT_IDENTIFIER);
    addToBuf(1 + ETH_LEN); // Length (hardware type + client MAC)
    addToBuf(DHCP_HTYPE_ETHER);
    addBytes(ETH_LEN, mymac);

    if (hostname[0]) {
        addOption(DHCP_OPT_HOSTNAME, strlen(hostname), (uint8_t*) hostname);
    }

    if (requestip != NULL) {
        addOption(DHCP_OPT_REQUESTED_ADDRESS, IP_LEN, requestip);
        addOption(DHCP_OPT_SERVER_IDENTIFIER, IP_LEN, dhcpip);
    }

    // Additional info in parameter list - minimal list for what we need
    uint8_t len = 3;
    if (dhcpCustomOptionNum)
        len++;
    addToBuf(DHCP_OPT_PARAMETER_REQUEST_LIST);
    addToBuf(len);    // Length
    addToBuf(DHCP_OPT_SUBNET_MASK);
    addToBuf(DHCP_OPT_ROUTERS);
    addToBuf(DHCP_OPT_DOMAIN_NAME_SERVERS);
    if (dhcpCustomOptionNum)
        addToBuf(dhcpCustomOptionNum);  // Custom option

    addToBuf(DHCP_OPT_END);

    // packet size will be under 300 bytes
    EtherCard_udpTransmit((bufPtr - gPB) - UDP_DATA_P);
}

static void process_dhcp_offer(uint16_t len, uint8_t *offeredip) {
    // Map struct onto payload
    DHCPdata *dhcpPtr = (DHCPdata*) (gPB + UDP_DATA_P);

    // Offered IP address is in yiaddr
    memcpy(offeredip, dhcpPtr->yiaddr, IP_LEN);

    // Search for the server IP
    uint8_t *ptr = (uint8_t*) (dhcpPtr + 1) + 4;
    do {
        uint8_t option = *ptr++;
        uint8_t optionLen = *ptr++;
        if (option == DHCP_OPT_SERVER_IDENTIFIER) {
            memcpy(dhcpip, ptr, IP_LEN);
            break;
        }
        ptr += optionLen;
    } while (ptr < gPB + len);
}

static void process_dhcp_ack(uint16_t len) {
    // Map struct onto payload
    DHCPdata *dhcpPtr = (DHCPdata*) (gPB + UDP_DATA_P);

    // Allocated IP address is in yiaddr
    memcpy(myip, dhcpPtr->yiaddr, IP_LEN);

    // Scan through variable length option list identifying options we want
    uint8_t *ptr = (uint8_t*) (dhcpPtr + 1) + 4;
    bool done = false;
    do {
        uint8_t option = *ptr++;
        uint8_t optionLen = *ptr++;
        switch (option) {
        case DHCP_OPT_SUBNET_MASK:
            memcpy(netmask, ptr, IP_LEN);
            break;
        case DHCP_OPT_ROUTERS:
            memcpy(gwip, ptr, IP_LEN);
            break;
        case DHCP_OPT_DOMAIN_NAME_SERVERS:
            memcpy(dnsip, ptr, IP_LEN);
            break;
        case DHCP_OPT_LEASE_TIME:
        case DHCP_OPT_RENEWAL_TIME:
            leaseTime = 0;
            for (uint8_t i = 0; i<4; i++)
                leaseTime = (leaseTime << 8) + ptr[i];
            if (leaseTime != DHCP_INFINITE_LEASE) {
                leaseTime *= 1000;      // milliseconds
            }
            break;
        case DHCP_OPT_END:
            done = true;
            break;
        default: {
            // Is is a custom configured option?
            if (dhcpCustomOptionCallback && option == dhcpCustomOptionNum) {
                dhcpCustomOptionCallback(option, ptr, optionLen);
            }
        }
        }
        ptr += optionLen;
    } while (!done && ptr < gPB + len);
}

static bool dhcp_received_message_type (uint16_t len, uint8_t msgType) {
    // Map struct onto payload
    DHCPdata *dhcpPtr = (DHCPdata*) (gPB + UDP_DATA_P);

    if (len >= 70 && gPB[UDP_SRC_PORT_L_P] == DHCP_SERVER_PORT &&
            dhcpPtr->xid == currentXid ) {

        uint8_t *ptr = (uint8_t*) (dhcpPtr + 1) + 4;
        do {
            uint8_t option = *ptr++;
            uint8_t optionLen = *ptr++;
            if(option == DHCP_OPT_MESSAGE_TYPE && *ptr == msgType ) {
                return true;
            }
            ptr += optionLen;
        } while (ptr < gPB + len);
    }
    return false;
}

static char toAsciiHex(uint8_t b) {
    char c = b & 0x0f;
    c += (c <= 9) ? '0' : 'A'-10;
    return c;
}

bool EtherCard_dhcpSetup (const char *hname, bool fromRam) {
    // Use during setup, as this discards all incoming requests until it returns.
    // That shouldn't be a problem, because we don't have an IPaddress yet.
    // Will try 60 secs to obtain DHCP-lease.

    using_dhcp = true;

    if(hname != NULL) {
        if(fromRam) {
            strncpy(hostname, hname, DHCP_HOSTNAME_MAX_LEN);
        }
 		// we do not currently implement the progmem direcitives.
    }
    else {
        // Set a unique hostname, use Arduino-?? with last octet of mac address
        hostname[strlen(hostname) - 2] = toAsciiHex(mymac[5] >> 4);   // Appends mac to last 2 digits of the hostname
        hostname[strlen(hostname) - 1] = toAsciiHex(mymac[5]);   // Even if it's smaller than the maximum <thus, strlen(hostname)>
    }

    dhcpState = DHCP_STATE_INIT;
    uint16_t start = HAL_GetTick();

    while ((dhcpState != DHCP_STATE_BOUND) && (((uint16_t)HAL_GetTick() - start) < 60000)) {
        if (ENC28J60_isLinkUp()) EtherCard_DhcpStateMachine(ENC28J60_packetReceive());
    }
    EtherCard_updateBroadcastAddress();
    delaycnt = 0;
    return dhcpState == DHCP_STATE_BOUND ;
}

void EtherCard_dhcpAddOptionCallback(uint8_t option, DhcpOptionCallback callback)
{
    dhcpCustomOptionNum = option;
    dhcpCustomOptionCallback = callback;
}

void EtherCard_DhcpStateMachine (uint16_t len)
{

#if DHCPDEBUG
    if (dhcpState != DHCP_STATE_BOUND) {
        Serial.print(HAL_GetTick());
        Serial.print(" State: ");
    }
    switch (dhcpState) {
    case DHCP_STATE_INIT:
        Serial.println("Init");
        break;
    case DHCP_STATE_SELECTING:
        Serial.println("Selecting");
        break;
    case DHCP_STATE_REQUESTING:
        Serial.println("Requesting");
        break;
    case DHCP_STATE_RENEWING:
        Serial.println("Renew");
        break;
    }
#endif

    switch (dhcpState) {

    case DHCP_STATE_BOUND:
        //!@todo Due to HAL_GetTick() wrap-around, DHCP renewal may not work if leaseTime is larger than 49days
        if ((leaseTime != DHCP_INFINITE_LEASE) && ((HAL_GetTick() - leaseStart) >= leaseTime)) {
            send_dhcp_message(myip);
            dhcpState = DHCP_STATE_RENEWING;
            stateTimer = HAL_GetTick();
        }
        break;

    case DHCP_STATE_INIT:
        currentXid = HAL_GetTick();
        memset(myip,0,IP_LEN); // force ip 0.0.0.0
        send_dhcp_message(NULL);
        ENC28J60_enableBroadcast(true); //Temporarily enable broadcasts
        dhcpState = DHCP_STATE_SELECTING;
        stateTimer = HAL_GetTick();
        break;

    case DHCP_STATE_SELECTING:
        if (dhcp_received_message_type(len, DHCP_OFFER)) {
            uint8_t offeredip[IP_LEN];
            process_dhcp_offer(len, offeredip);
            send_dhcp_message(offeredip);
            dhcpState = DHCP_STATE_REQUESTING;
            stateTimer = HAL_GetTick();
        } else {
            if (HAL_GetTick() - stateTimer > DHCP_REQUEST_TIMEOUT) {
                dhcpState = DHCP_STATE_INIT;
            }
        }
        break;

    case DHCP_STATE_REQUESTING:
    case DHCP_STATE_RENEWING:
        if (dhcp_received_message_type(len, DHCP_ACK)) {
            ENC28J60_disableBroadcast(true); //Disable broadcast after temporary enable
            process_dhcp_ack(len);
            leaseStart = HAL_GetTick();
            if (gwip[0] != 0) ENC28J60_setGwIp(gwip); // why is this? because it initiates an arp request
            dhcpState = DHCP_STATE_BOUND;
        } else {
            if (HAL_GetTick() - stateTimer > DHCP_REQUEST_TIMEOUT) {
                dhcpState = DHCP_STATE_INIT;
            }
        }
        break;

    }
}



