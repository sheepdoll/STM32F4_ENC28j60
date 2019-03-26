This is a quick and dirty fix and addendum for the MYaqoobEmbedded ENC28j60 tutorial on youtube <https://www.youtube.com/watch?v=A4c0nJudOI0&t=9s>

It should extend the code to support ARP,DHCP, ping(ICMP) and UDP.  TCP is not of interest at this time. 

That tutorial only gets as far as ARP requests and has a number of bugs. Out of the box Wireshark sees no ARP requests. 

This port merges in code from the Arduino EtherCard library v1.1.0, It looks like this code came from other places, including raspberry pi as the default MAC address a default address from a pi when read as an ASCII string.

The target for this was a Waveshare ENC28J60 NIC on a STM32F401 Nucleo board, using CubeMX 5.1.0 and SW4STM32. To use this a SW4STM32 project needs to be created.  

Since SW4STM32 project is eclipse based and eclipse is not too friendly with paths only the CubeMX .ioc file is included in the repository.  This will save time creating a project from scratch. This works best when the .ioc file is copied into the project folder in the workspace.  he project should not be created first, just the project folder, as CubeMX will create the project with the default settings from the .ioc file.

The critical things to enable are the SPI pins, the Chip select and the clock speeds, not forgetting the SPI speed must be less that 20 MHz. This is done in the tutorial.  Since the Arduino library polls for packets, the interrupt line is not used.  On my Uno ENC28j60 Arduino shield this is connected to Pin 2 of the Arduino header.

After the project is generated, copy the files from this repository into the correct inc and src directories in eclipse workspace.  Note that main.c and main.h will already exist.  It might be a good idea to diff these rather than replace them. 

Eclipse is really bad at importing things so it is up to the user to know how to move the files into the correct place so the IDE can see them. Best to copy them with the project closed.

Also watch out for the chip enable/disable accessors which are in main.c so as to take advantage of pin naming. 

A LED shield was used connected to GPIO pins for debug states, this is optional, The extra GPIO was connected to this shield for debug.  If these are not used then the GPIO pins can be removed from main.c using the cubeMX project.  

The serial backchannel is enabled if that is preferred, however most of the string formatting function overhead has not been ported over.

The default behavior is to toggle a LED when a UDP packet is received on port 1337.
LEDS On PortA will activate when DHCP is acquired or indicate an error state if not. There is also a LED on port B that turns on when the chip is initialized.


No warranty is made that this will even compile or that all the parts are there.

