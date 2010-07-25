#ifndef XMODEM_H
#define XMODEM_H

int xmodemReceive(unsigned char *dest, int destsz);
int xmodemTransmit(unsigned char *src, int srcsz);

#endif
