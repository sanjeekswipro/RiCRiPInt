/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:chartype.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Defines the lookup table that the scanner uses to class characters
 */

#include "core.h"

#include "chartype.h" /* we are chartype.c, so incl our own header */
#include "psvm.h"     /* workingsave */

uint8 _char_table[256] = {
/* 0 */   END_MARKER_PS | END_MARKER_PDF | WHITE_SPACE ,
/* 1 */   REGULAR ,
/* 2 */   REGULAR ,
/* 3 */   REGULAR ,
/* 4 */   REGULAR | COMMS_CHAR ,
/* 5 */   REGULAR ,
/* 6 */   REGULAR ,
/* 7 */   REGULAR ,
/* 10 */  REGULAR ,
/* 11 */  END_MARKER_PS | END_MARKER_PDF | WHITE_SPACE ,
/* 12 */  END_MARKER_PS | END_MARKER_PDF | WHITE_SPACE | END_LINE ,
/* 13 */  REGULAR ,
/* 14 */  END_MARKER_PS | END_MARKER_PDF | WHITE_SPACE ,
/* 15 */  END_MARKER_PS | END_MARKER_PDF | WHITE_SPACE | END_LINE ,
/* 16 */  REGULAR ,
/* 17 */  REGULAR ,
/* 20 */  REGULAR ,
/* 21 */  REGULAR ,
/* 22 */  REGULAR ,
/* 23 */  REGULAR ,
/* 24 */  REGULAR | COMMS_CHAR ,
/* 25 */  REGULAR ,
/* 26 */  REGULAR ,
/* 27 */  REGULAR ,
/* 30 */  REGULAR ,
/* 31 */  REGULAR ,
/* 32 */  REGULAR | COMMS_CHAR ,
/* 33 */  REGULAR ,
/* 34 */  REGULAR ,
/* 35 */  REGULAR ,
/* 36 */  REGULAR ,
/* 37 */  REGULAR ,
/* 40 */  END_MARKER_PS | END_MARKER_PDF | WHITE_SPACE ,
/* 41 */  REGULAR ,
/* 42 */  REGULAR ,
/* 43 */  REGULAR ,
/* 44 */  REGULAR ,
/* 45 */  END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 46 */  REGULAR ,
/* 47 */  REGULAR ,
/* 50 */  END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 51 */  END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 52 */  REGULAR ,
/* 53 */  REGULAR ,
/* 54 */  REGULAR ,
/* 55 */  REGULAR | START_NUM ,
/* 56 */  REGULAR | START_NUM ,
/* 57 */  END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 60 */  REGULAR | START_NUM ,
/* 61 */  REGULAR | START_NUM ,
/* 62 */  REGULAR | START_NUM ,
/* 63 */  REGULAR | START_NUM ,
/* 64 */  REGULAR | START_NUM ,
/* 65 */  REGULAR | START_NUM ,
/* 66 */  REGULAR | START_NUM ,
/* 67 */  REGULAR | START_NUM ,
/* 70 */  REGULAR | START_NUM ,
/* 71 */  REGULAR | START_NUM ,
/* 72 */  REGULAR ,
/* 73 */  REGULAR ,
/* 74 */  END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 75 */  REGULAR ,
/* 76 */  END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 77 */  REGULAR ,
/* 100 */ REGULAR ,
/* 101 */ REGULAR ,
/* 102 */ REGULAR ,
/* 103 */ REGULAR ,
/* 104 */ REGULAR ,
/* 105 */ REGULAR ,
/* 106 */ REGULAR ,
/* 107 */ REGULAR ,
/* 110 */ REGULAR ,
/* 111 */ REGULAR ,
/* 112 */ REGULAR ,
/* 113 */ REGULAR ,
/* 114 */ REGULAR ,
/* 115 */ REGULAR ,
/* 116 */ REGULAR ,
/* 117 */ REGULAR ,
/* 120 */ REGULAR ,
/* 121 */ REGULAR ,
/* 122 */ REGULAR ,
/* 123 */ REGULAR ,
/* 124 */ REGULAR ,
/* 125 */ REGULAR ,
/* 126 */ REGULAR ,
/* 127 */ REGULAR ,
/* 130 */ REGULAR ,
/* 131 */ REGULAR ,
/* 132 */ REGULAR ,
/* 133 */ END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 134 */ REGULAR ,
/* 135 */ END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 136 */ REGULAR ,
/* 137 */ REGULAR ,
/* 140 */ REGULAR ,
/* 141 */ REGULAR ,
/* 142 */ REGULAR ,
/* 143 */ REGULAR ,
/* 144 */ REGULAR ,
/* 145 */ REGULAR ,
/* 146 */ REGULAR ,
/* 147 */ REGULAR ,
/* 150 */ REGULAR ,
/* 151 */ REGULAR ,
/* 152 */ REGULAR ,
/* 153 */ REGULAR ,
/* 154 */ REGULAR ,
/* 155 */ REGULAR ,
/* 156 */ REGULAR ,
/* 157 */ REGULAR ,
/* 160 */ REGULAR ,
/* 161 */ REGULAR ,
/* 162 */ REGULAR ,
/* 163 */ REGULAR ,
/* 164 */ REGULAR ,
/* 165 */ REGULAR ,
/* 166 */ REGULAR ,
/* 167 */ REGULAR ,
/* 170 */ REGULAR ,
/* 171 */ REGULAR ,
/* 172 */ REGULAR ,
/* 173 */ END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 174 */ REGULAR ,
/* 175 */ END_MARKER_PS | END_MARKER_PDF | SPECIAL_CHAR ,
/* 176 */ REGULAR ,
/* 177 */ REGULAR ,
/* 200 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 201 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 202 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 203 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 204 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 205 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 206 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 207 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 210 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 211 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 212 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 213 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 214 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 215 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 216 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 217 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 220 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 221 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 222 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 223 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 224 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 225 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 226 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 227 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 230 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 231 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 232 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 233 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 234 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 235 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 236 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 237 */ REGULAR , /* or BINARY_TOKEN | END_MARKER_PS */
/* 240 */ REGULAR ,
/* 241 */ REGULAR ,
/* 242 */ REGULAR ,
/* 243 */ REGULAR ,
/* 244 */ REGULAR ,
/* 245 */ REGULAR ,
/* 246 */ REGULAR ,
/* 247 */ REGULAR ,
/* 250 */ REGULAR ,
/* 251 */ REGULAR ,
/* 252 */ REGULAR ,
/* 253 */ REGULAR ,
/* 254 */ REGULAR ,
/* 255 */ REGULAR ,
/* 256 */ REGULAR ,
/* 257 */ REGULAR ,
/* 260 */ REGULAR ,
/* 261 */ REGULAR ,
/* 262 */ REGULAR ,
/* 263 */ REGULAR ,
/* 264 */ REGULAR ,
/* 265 */ REGULAR ,
/* 266 */ REGULAR ,
/* 267 */ REGULAR ,
/* 270 */ REGULAR ,
/* 271 */ REGULAR ,
/* 272 */ REGULAR ,
/* 273 */ REGULAR ,
/* 274 */ REGULAR ,
/* 275 */ REGULAR ,
/* 276 */ REGULAR ,
/* 277 */ REGULAR ,
/* 300 */ REGULAR ,
/* 301 */ REGULAR ,
/* 302 */ REGULAR ,
/* 303 */ REGULAR ,
/* 304 */ REGULAR ,
/* 305 */ REGULAR ,
/* 306 */ REGULAR ,
/* 307 */ REGULAR ,
/* 310 */ REGULAR ,
/* 311 */ REGULAR ,
/* 312 */ REGULAR ,
/* 313 */ REGULAR ,
/* 314 */ REGULAR ,
/* 315 */ REGULAR ,
/* 316 */ REGULAR ,
/* 317 */ REGULAR ,
/* 320 */ REGULAR ,
/* 321 */ REGULAR ,
/* 322 */ REGULAR ,
/* 323 */ REGULAR ,
/* 324 */ REGULAR ,
/* 325 */ REGULAR ,
/* 326 */ REGULAR ,
/* 327 */ REGULAR ,
/* 330 */ REGULAR ,
/* 331 */ REGULAR ,
/* 332 */ REGULAR ,
/* 333 */ REGULAR ,
/* 334 */ REGULAR ,
/* 335 */ REGULAR ,
/* 336 */ REGULAR ,
/* 337 */ REGULAR ,
/* 340 */ REGULAR ,
/* 341 */ REGULAR ,
/* 342 */ REGULAR ,
/* 343 */ REGULAR ,
/* 344 */ REGULAR ,
/* 345 */ REGULAR ,
/* 346 */ REGULAR ,
/* 347 */ REGULAR ,
/* 350 */ REGULAR ,
/* 351 */ REGULAR ,
/* 352 */ REGULAR ,
/* 353 */ REGULAR ,
/* 354 */ REGULAR ,
/* 355 */ REGULAR ,
/* 356 */ REGULAR ,
/* 357 */ REGULAR ,
/* 360 */ REGULAR ,
/* 361 */ REGULAR ,
/* 362 */ REGULAR ,
/* 363 */ REGULAR ,
/* 364 */ REGULAR ,
/* 365 */ REGULAR ,
/* 366 */ REGULAR ,
/* 367 */ REGULAR ,
/* 370 */ REGULAR ,
/* 371 */ REGULAR ,
/* 372 */ REGULAR ,
/* 373 */ REGULAR ,
/* 374 */ REGULAR ,
/* 375 */ REGULAR ,
/* 376 */ REGULAR ,
/* 377 */ REGULAR 

};


void remap_bin_token_chars(void)
{
  int32 i ;
  uint8 chartype ;

  if ( theIObjectFormat( workingsave ) == 0 )
    chartype = REGULAR ;
  else
    chartype = BINARY_TOKEN | END_MARKER_PS ;

  for ( i = 128 ; i < 160 ; i++ ) {
    _char_table[i] = chartype ;
  }
}

/* EOF */

/* Log stripped */
