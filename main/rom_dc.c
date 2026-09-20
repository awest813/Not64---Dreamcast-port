/**
 * ROM load / byte-swap for the Dreamcast port (from rom_gc.c).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ROM-Cache.h"
#include "rom.h"
#include "timers.h"

int rom_length;
int rom_byte_swap;
rom_header ROM_HEADER;
rom_settings ROM_SETTINGS;

int init_byte_swap(unsigned int magicWord)
{
	switch (magicWord)
	{
	case 0x37804012: // aka byteswapped
		rom_byte_swap = BYTE_SWAP_BYTE;
		break;
	case 0x40123780: // aka little endian, aka halfswapped
		rom_byte_swap = BYTE_SWAP_NONE;
		break;
	case 0x80371240:
#ifdef __DREAMCAST__
		/* SH4 and the host stub are little-endian; store z64 as native
		 * 32-bit words so the interpreter's unsigned long fetches match
		 * MIPS encodings. */
		rom_byte_swap = BYTE_SWAP_HALF;
#else
		rom_byte_swap = BYTE_SWAP_NONE;
#endif
		break;
	default:
		rom_byte_swap = BYTE_SWAP_BAD;
		break;
	}
	
	return rom_byte_swap;
}

void byte_swap(char* buffer, unsigned int length)
{
	unsigned int i;

	if (rom_byte_swap == BYTE_SWAP_HALF) {
		for (i = 0; i < (length & ~3u); i += 4) {
			unsigned int v = *(unsigned int *)(buffer + i);
			*(unsigned int *)(buffer + i) =
				((v & 0x000000FFu) << 24) |
				((v & 0x0000FF00u) << 8) |
				((v & 0x00FF0000u) >> 8) |
				((v & 0xFF000000u) >> 24);
		}
	} else if (rom_byte_swap == BYTE_SWAP_BYTE) {
        /* Pair-swapped dump -> native little-endian 32-bit words. */
        for (i = 0; i < (length & ~3u); i += 4) {
            unsigned char a=buffer[i], b=buffer[i+1];
            buffer[i]=buffer[i+2]; buffer[i+1]=buffer[i+3];
            buffer[i+2]=a; buffer[i+3]=b;
        }
	}
}

void stripInvalidChars(char* str)
{
	char* p;
	
	while ((p = strpbrk(str, "\\/:*?\"<>|")) != NULL)
		*p = '_';
}

static struct {
	int Cartridge_ID;
	char* Country_codes;
} ROM_TABLE[] = {
	{ 'D3', "J",   }, // Akumajou Dracula Mokushiroku - Real Action Adventure
	{ 'D4', "J",   }, // Akumajou Dracula Mokushiroku Gaiden - Legend of Cornell
	{ 'B7', "EJPU" }, // Banjo-Tooie
	{ 'GT', "J",   }, // City Tour Grandprix - Zennihon GT Senshuken
	{ 'FU', "EP",  }, // Conker's Bad Fur Day
	{ 'CW', "EP"   }, // Cruis'n World
	{ 'CZ', "J",   }, // Custom Robo V2
	{ 'D6', "J",   }, // Densha de GO! 64
	{ 'DO', "EJP"  }, // Donkey Kong 64
	{ 'D2', "J",   }, // Doraemon 2 - Nobita to Hikari no Shinden
	{ '3D', "J",   }, // Doraemon 3 - Nobita no Machi SOS!
	{ 'MX', "EJP"  }, // Excitebike 64
	{ 'X7', "E"    }, // GoldenEye X 5d
	{ 'GC', "EP"   }, // GT 64 - Championship Edition
	{ 'IM', "J",   }, // Ide Yousuke no Mahjong Juku
	{ 'K4', "EJP"  }, // Kirby 64 - The Crystal Shards
	{ 'NB', "EP"   }, // Kobe Bryant in NBA Courtside
	{ 'MV', "EJP"  }, // Mario Party 3
	{ 'M8', "EJP"  }, // Mario Tennis
	{ 'EV', "J"    }, // Neon Genesis Evangelion
	{ 'PP', "J"    }, // Parlor! Pro 64 - Pachinko Jikki Simulation Game
	{ 'UB', "J"    }, // PD Ultraman Battle Collection 64
	{ 'PD', "EJP"  }, // Perfect Dark
	{ 'R7', "J"    }, // Robot Ponkottsu 64 - 7tsu no Umi no Caramel
	{ 'RZ', "EP"   }, // RR64 - Ridge Racer 64
	{ 'EP', "EJP"  }, // Star Wars Episode I - Racer
	{ 'YS', "EJP"  }, // Yoshi's Story
};

// Checks if the current game is in the ID list for 16kbit eeprom save type
// cause it's cheaper to have a ID list than an entire .ini file :)
static int isEEPROM16k()
{
	unsigned int i;
	
	for (i = 0; i < sizeof(ROM_TABLE) / sizeof(ROM_TABLE[0]); i++)
	{
		if (ROM_TABLE[i].Cartridge_ID == ROM_HEADER.Cartridge_ID &&
			strchr(ROM_TABLE[i].Country_codes, ROM_HEADER.Country_code))
			return 1;
	}

	return 0;
}

#ifdef __DREAMCAST__
static void reverse_word(unsigned char* p)
{
	unsigned char t0 = p[0], t1 = p[1];
	p[0] = p[3];
	p[1] = p[2];
	p[2] = t1;
	p[3] = t0;
}

/* The DC ROM cache stores each 32-bit word byte-reversed so the interpreter's
 * native word fetches match MIPS encodings. That leaves the header's 32-bit
 * fields (ClockRate, PC, CRC1/2, Manufacturer_ID) reading correctly and
 * scrambles every byte- and halfword-addressed field. Restore those in place,
 * so Name, Cartridge_ID and Country_code mean what the rest of the tree
 * expects (isEEPROM16k, saveregionstr, GetVILimit). */
static void dc_fix_header_byte_order(void)
{
	unsigned char* h = (unsigned char*)&ROM_HEADER;
	unsigned int i;
	unsigned char t;

	/* Every accepted dump format is normalized to native word order. */

	reverse_word(h + 0x00);                  /* init_PI_BSB_DOM1_* */
	for (i = 0x20; i < 0x34; i += 4)         /* Name[20] */
		reverse_word(h + i);
	reverse_word(h + 0x3C);                  /* Cartridge_ID/Country/Version */

	/* Cartridge_ID is big-endian in the cart; store it so a native halfword
	 * read still matches the 'XY' literals in ROM_TABLE. */
	t = h[0x3C];
	h[0x3C] = h[0x3D];
	h[0x3D] = t;
}
#endif

/* Loads the ROM into the ROM cache */
int rom_read(fileBrowser_file* file){

   int i;

   ROMCache_init(file);
   int ret = ROMCache_load(file);
   if(ret) {
     ROMCache_deinit();
     return ret;
   }
   ROMCache_read((unsigned char*)&ROM_HEADER, 0, sizeof(rom_header));
#ifdef __DREAMCAST__
   dc_fix_header_byte_order();
#endif

  //Copy header name as Goodname (in the .ini we can use CRC to identify ROMS)
  memcpy(ROM_SETTINGS.goodname, ROM_HEADER.Name, 20);
  ROM_SETTINGS.goodname[20] = '\0';
  //Maximum ROM name is 20 bytes. Lets make sure we cut off trailing spaces
  for(i = strlen(ROM_SETTINGS.goodname); i>0; i--)
  {
    if(ROM_SETTINGS.goodname[i-1] !=  ' ') {
  		ROM_SETTINGS.goodname[i] = '\0';
  		break;
    }
  }
  // Replace any non file system compliant chars with underscores
  stripInvalidChars(&ROM_SETTINGS.goodname[0]);
  // Fix save type for certain special sized (16kbit) eeprom games
  ROM_SETTINGS.isEEPROM16k = isEEPROM16k();

  //Set VI limit based on ROM header
  InitTimer();

   return ret;
}

void countrycodestring(unsigned short countrycode, char *string)
{
    switch (countrycode)
    {
    case 0:    /* Demo */
        strcpy(string, ("Demo"));
        break;

    case '7':  /* Beta */
        strcpy(string, ("Beta"));
        break;

    case 0x41: /* Japan / USA */
        strcpy(string, ("USA/Japan"));
        break;

    case 0x44: /* Germany */
        strcpy(string, ("Germany"));
        break;

    case 0x45: /* USA */
        strcpy(string, ("USA"));
        break;

    case 0x46: /* France */
        strcpy(string, ("France"));
        break;

    case 'I':  /* Italy */
        strcpy(string, ("Italy"));
        break;

    case 0x4A: /* Japan */
        strcpy(string, ("Japan"));
        break;

    case 'S':  /* Spain */
        strcpy(string, ("Spain"));
        break;

    case 0x55: case 0x59:  /* Australia */
        sprintf(string, ("Australia (0x%2.2X)"), countrycode);
        break;

    case 0x50: case 0x58: case 0x20:
    case 0x21: case 0x38: case 0x70:
        sprintf(string, ("Europe (0x%02X)"), countrycode);
        break;

    default:
        sprintf(string, ("Unknown (0x%02X)"), countrycode);
        break;
    }
}

char *saveregionstr()
{
    switch (ROM_HEADER.Country_code&0xFF)
    {
    case 0:    /* Demo */
        return "(Demo)";
        break;
    case '7':  /* Beta */
        return "(Beta)";
        break;
    case 0x41: /* Japan / USA */
        return "(JU)";
        break;
    case 0x44: /* Germany */
        return "(G)";
        break;
    case 0x45: /* USA */
        return "(U)";
        break;
    case 0x46: /* France */
        return "(F)";
        break;
    case 'I':  /* Italy */
        return "(I)";
        break;
    case 0x4A: /* Japan */
        return "(J)";
        break;
    case 'S':  /* Spain */
        return "(S)";
        break;
    case 0x55: case 0x59:  /* Australia */
        return "(A)";
        break;
    case 0x50: case 0x58: case 0x20:
    case 0x21: case 0x38: case 0x70:
        return "(E)";
        break;
    default:
        return "(Unk)";
        break;
    }
}

