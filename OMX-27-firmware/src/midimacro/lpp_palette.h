#pragma once
#include <stdint.h>
// Launchpad programmer-mode velocity palette (128 entries) -> 0xRRGGBB.
// Source: Novation "Launchpad Pro [MK3] Programmer's Reference Manual" (LPP3_prog_ref_guide_200415.pdf),
// "Colour palette" figure, page 9 (decimal-indexed table), pixel-sampled from the official PDF artwork.
// Cross-checked against the "Launchpad X Programmer's Reference Manual" Colour palette figure (page 12),
// which shares the identical 128-colour palette (also used by Launchpad Mini MK3) -- 96 of 128 entries
// matched byte-for-byte between the two independently rendered manuals; the remainder (grey/red row 0-7,
// mixed-hue row 64-71 and the two dimmest rows 56-63/120-127) differed only by minor JPEG/anti-aliasing
// noise between the two PDFs, so the Launchpad Pro [MK3] manual's values were kept as the primary source.
// Index 0 (velocity 0 / Note Off) is forced to pure black (0x000000): the printed chart renders it as a
// dark grey swatch purely so it remains visible on the page, but the manual's own text (LED lighting
// SysEx / Note Off sections) defines velocity 0 as "LED off".
#if defined(ARDUINO_ARCH_RP2040) || defined(__IMXRT1062__) || defined(__MK20DX256__)
#include <Arduino.h>
#endif
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif
static const uint32_t lppPalette[128] PROGMEM = {
  0x000000, 0xB3B3B3, 0xDDDDDD, 0xFFFFFF, 0xFFB3B3, 0xFF6161, 0xDD6161, 0xB36161,
  0xFFF3D5, 0xFFB361, 0xDD8C61, 0xB37661, 0xFFEEA1, 0xFFFF61, 0xDDDD61, 0xB3B361,
  0xDDFFA1, 0xC2FF61, 0xA1DD61, 0x81B361, 0xC2FFB3, 0x61FF61, 0x61DD61, 0x61B361,
  0xC2FFC2, 0x61FF8C, 0x61DD76, 0x61B36B, 0xC2FFCC, 0x61FFCC, 0x61DDA1, 0x61B381,
  0xC2FFF3, 0x61FFE9, 0x61DDC2, 0x61B396, 0xC2F3FF, 0x61EEFF, 0x61C7DD, 0x61A1B3,
  0xC2DDFF, 0x61C7FF, 0x61A1DD, 0x6181B3, 0xA18CFF, 0x6161FF, 0x6161DD, 0x6161B3,
  0xCCB3FF, 0xA161FF, 0x8161DD, 0x7661B3, 0xFFB3FF, 0xFF61FF, 0xDD61DD, 0xB361B3,
  0x312229, 0x311225, 0x2A121F, 0x22121B, 0x311612, 0x2D2212, 0x2A2512, 0x1F1F12,
  0x61B361, 0x61B38C, 0x618CD5, 0x6161FF, 0x61B3B3, 0x8C61F3, 0xCCB3C2, 0x8C7681,
  0xFF6161, 0xF3FFA1, 0xEEFC61, 0xCCFF61, 0x76DD61, 0x61FFCC, 0x61E9FF, 0x61A1FF,
  0x8C61FF, 0xCC61FC, 0xEE8CDD, 0xA17661, 0xFFA161, 0xDDF961, 0xD5FF8C, 0x61FF61,
  0xB3FFA1, 0xCCFCD5, 0xB3FFF6, 0xCCE4FF, 0xA1C2F6, 0xD5C2F9, 0xF98CFF, 0xFF61CC,
  0xFFC261, 0xF3EE61, 0xE4FF61, 0xDDCC61, 0xB3A161, 0x61BA76, 0x76C28C, 0x8181A1,
  0x818CCC, 0xCCAA81, 0xDD6161, 0xF9B3A1, 0xF9BA76, 0xFFF38C, 0xE9F9A1, 0xD5EE76,
  0x8181A1, 0xF9F9D5, 0xDDFCE4, 0xE9E9FF, 0xE4D5FF, 0xB3B3B3, 0xD5D5D5, 0xF9FFFF,
  0x2D1212, 0x201212, 0x182F12, 0x122212, 0x2F2E12, 0x221F12, 0x2E2512, 0x251612
};
// Returns the 0xRRGGBB colour for a palette index (out of range -> 0).
inline uint32_t lppPaletteColor(uint8_t idx) { if (idx > 127) return 0; return pgm_read_dword(&lppPalette[idx]); }
