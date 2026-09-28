#include "comm.h"

extern char nand_magic[11];

uint8_t read_nand();
void nand_write(uint8_t);

void read_nand0_file();
void read_nand_file();

void write_nand0_file(string file="");
void write_nand_file(string file="");

void clear_nand_status();
/* debug: pointer to the emulator's view of a device NAND page (528 bytes) */
const uint8_t* nand_device_page_ptr(uint32_t page);
