// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The layout of the kanji image (scripts/mkkanji.py writes it, the loader copies it from the SPI flash to the
// PSRAM, the HDMI decoder and the display read it).  Offsets are bytes from the start of the image.
//
//   +0x0000  'K' 'J' 'F' '1'
//   +0x0100  page directory: 256 little endian words, indexed by the high byte of a Unicode character; the
//            number of its page in the page area, or $FFFF when the page has no glyphs
//   +0x0300  pages: 512 bytes each: 256 little endian words, indexed by the low byte; the glyph number or $FFFF
//   +0x10000 glyphs: 32 bytes each, the 16 lines of 16 dots (the left 8 dots, the right 8 dots of a line)
//
// The glyph number (0..3759) is the JIS X 0208 row/column in a compact order: rows 1-8 are 0-7, rows 16-47
// (the level 1 kanji) are 8-39, 94 columns per row.
`define KJ_MAGIC0   8'h4b
`define KJ_DIR      32'h000100
`define KJ_PAGES    32'h000300
`define KJ_GLYPH    32'h010000
`define KJ_SIZE     32'h02d600
`define KJ_PSRAM    22'h200000           // where the image is in the PSRAM (2 MB: the Level 2 machine has the 2 MB below it)
`define KJ_FLASH    24'h100000           // and in the SPI flash
`define KJ_GETA     12'd107              // the glyph for a character that has none: row 2, column 14 (JIS $222E)
