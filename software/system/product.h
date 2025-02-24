#ifndef PRODUCT_H
#define PRODUCT_H

#define PRODUCT_UNKNOWN                0x00
#define PRODUCT_ULTIMATE_II            0x01
#define PRODUCT_ULTIMATE_II_PLUS       0x02
#define PRODUCT_ULTIMATE_II_PLUS_L     0x03
#define PRODUCT_ULTIMATE_64            0x04
#define PRODUCT_ULTIMATE_64_ELITE      0x05
#define PRODUCT_ULTIMATE_64_ELITE_II   0x06

#if U64
const char *getBoardRevision(void);
bool isEliteBoard(void);
#endif

uint8_t getProductId();
const char *getProductString();
char *getProductVersionString(char *buf, int sz);
char *getProductTitleString(char *buf, int sz);
char *getProductDefaultHostname(char *buf, int sz);
char *getProductUpdateFileExtension();
char *getProductUniqueId();

#endif  // PRODUCT_H
