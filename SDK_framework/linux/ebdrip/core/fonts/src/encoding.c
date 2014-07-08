/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:encoding.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2000-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for initialising and using encodings
 */


#include "core.h"
#include "coreinit.h"

#include "objects.h"
#include "encoding.h"
#include "namedef_.h"
#include "dicthash.h"
#include "uvms.h"               /* UVS */

#include "mm.h"
#include "mmcompat.h"

/*--------------------------------------------------------------------------*/
NAMECACHE *MacExpertEncoding[ 256 ] ;

static const char *MacExpertEncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 32-63 */
  "space",            "exclamsmall",      "Hungarumlautsmall","centoldstyle",
  "dollaroldstyle",   "dollarsuperior",   "ampersandsmall",   "Acutesmall",
  "parenleftsuperior","parenrightsuperior","twodotenleader",  "onedotenleader",
  "comma",            "hyphen",           "period",           "fraction",
  "zerooldstyle",     "oneoldstyle",      "twooldstyle",      "threeoldstyle",
  "fouroldstyle",     "fiveoldstyle",     "sixoldstyle",      "sevenoldstyle",
  "eightoldstyle",    "nineoldstyle",     "colon",            "semicolon",
  ".notdef",          "threequartersemdash",".notdef",        "questionsmall",
/* 64-95 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  "Ethsmall",         ".notdef",          ".notdef",          "onequarter",
  "onehalf",          "threequarters",    "oneeighth",        "threeeighths",
  "fiveeighths",      "seveneighths",     "onethird",         "twothirds",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          "ff",               "fi",
  "fl",               "ffi",              "ffl",              "parenleftinferior",
  ".notdef",          "parenrightinferior","Circumflexsmall", "hypheninferior",
/* 96-127 */
  "Gravesmall",       "Asmall",           "Bsmall",           "Csmall",
  "Dsmall",           "Esmall",           "Fsmall",           "Gsmall",
  "Hsmall",           "Ismall",           "Jsmall",           "Ksmall",
  "Lsmall",           "Msmall",           "Nsmall",           "Osmall",
  "Psmall",           "Qsmall",           "Rsmall",           "Ssmall",
  "Tsmall",           "Usmall",           "Vsmall",           "Wsmall",
  "Xsmall",           "Ysmall",           "Zsmall",           "colonmonetary",
  "onefitted",        "rupiah",           "Tildesmall",       ".notdef",
/* 128-159 */
  ".notdef",          "asuperior",        "centsuperior",     ".notdef",
  ".notdef",          ".notdef",          ".notdef",          "Aacutesmall",
  "Agravesmall",      "Acircumflexsmall", "Adieresissmall",   "Atildesmall",
  "Aringsmall",       "Ccedillasmall",    "Eacutesmall",      "Egravesmall",
  "Ecircumflexsmall", "Edieresissmall",   "Iacutesmall",      "Igravesmall",
  "Icircumflexsmall", "Idieresissmall",   "Ntildesmall",      "Oacutesmall",
  "Ogravesmall",      "Ocircumflexsmall", "Odieresissmall",   "Otildesmall",
  "Uacutesmall",      "Ugravesmall",      "Ucircumflexsmall", "Udieresissmall",
/* 160-191 */
  ".notdef",          "eightsuperior",    "fourinferior",     "threeinferior",
  "sixinferior",      "eightinferior",    "seveninferior",    "Scaronsmall",
  ".notdef",          "centinferior",     "twoinferior",      ".notdef",
  "Dieresissmall",    ".notdef",          "Caronsmall",       "osuperior",
  "fiveinferior",     ".notdef",          "commainferior",    "periodinferior",
  "Yacutesmall",      ".notdef",          "dollarinferior",   ".notdef",
  ".notdef",          "Thornsmall",       ".notdef",          "nineinferior",
  "zeroinferior",     "Zcaronsmall",      "AEsmall",          "Oslashsmall",
/* 192-223 */
  "questiondownsmall","oneinferior",      "Lslashsmall",      ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          "Cedillasmall",     ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          "OEsmall",
  "figuredash",       "hyphensuperior",   ".notdef",          ".notdef",
  ".notdef",          ".notdef",          "exclamdownsmall",  ".notdef",
  "Ydieresissmall",   ".notdef",          "onesuperior",      "twosuperior",
  "threesuperior",    "foursuperior",     "fivesuperior",     "sixsuperior",
/* 224-255 */
  "sevensuperior",    "ninesuperior",     "zerosuperior",     ".notdef",
  "esuperior",        "rsuperior",        "tsuperior",        ".notdef",
  ".notdef",          "isuperior",        "ssuperior",        "dsuperior",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          "lsuperior",        "Ogoneksmall",      "Brevesmall",
  "Macronsmall",      "bsuperior",        "nsuperior",        "msuperior",
  "commasuperior",    "periodsuperior",   "Dotaccentsmall",   "Ringsmall",
  ".notdef",          ".notdef",          ".notdef",          ".notdef"
} ;

/*--------------------------------------------------------------------------*/

/* The Apple Standard Roman character set is mapped (with some omissions)
   by MacRomanEncoding. The definitive reference for the Apple Standard
   roman set was http://developer.apple.com/techpubs/mac/Text/Text-516.html
   (now dead) so see http://en.wikipedia.org/wiki/Mac_OS_Roman
 */

NAMECACHE *MacRomanEncoding[ 256 ] ;

/* Note that this contains two 'space' names */

static const char *MacRomanEncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 32-63 */
 "space",             "exclam",           "quotedbl",         "numbersign",
 "dollar",            "percent",          "ampersand",        "quotesingle",
 "parenleft",         "parenright",       "asterisk",         "plus",
 "comma",             "hyphen",           "period",           "slash",
 "zero",              "one",              "two",              "three",
 "four",              "five",             "six",              "seven",
 "eight",             "nine",             "colon",            "semicolon",
 "less",              "equal",            "greater",          "question",
/* 64-95 */
 "at",                "A",                "B",                "C",
 "D",                 "E",                "F",                "G",
 "H",                 "I",                "J",                "K",
 "L",                 "M",                "N",                "O",
 "P",                 "Q",                "R",                "S",
 "T",                 "U",                "V",                "W",
 "X",                 "Y",                "Z",                "bracketleft",
 "backslash",         "bracketright",     "asciicircum",      "underscore",
/* 96-127 */
 "grave",             "a",                "b",                "c",
 "d",                 "e",                "f",                "g",
 "h",                 "i",                "j",                "k",
 "l",                 "m",                "n",                "o",
 "p",                 "q",                "r",                "s",
 "t",                 "u",                "v",                "w",
 "x",                 "y",                "z",                "braceleft",
 "bar",               "braceright",       "asciitilde",       ".notdef",
/* 128-159 */
 "Adieresis",         "Aring",            "Ccedilla",         "Eacute",
 "Ntilde",            "Odieresis",        "Udieresis",        "aacute",
 "agrave",            "acircumflex",      "adieresis",        "atilde",
 "aring",             "ccedilla",         "eacute",           "egrave",
 "ecircumflex",       "edieresis",        "iacute",           "igrave",
 "icircumflex",       "idieresis",        "ntilde",           "oacute",
 "ograve",            "ocircumflex",      "odieresis",        "otilde",
 "uacute",            "ugrave",           "ucircumflex",      "udieresis",
/* 160-191 */
 "dagger",            "degree",           "cent",             "sterling",
 "section",           "bullet",           "paragraph",        "germandbls",
 "registered",        "copyright",        "trademark",        "acute",
 "dieresis",          ".notdef",          "AE",               "Oslash",
 ".notdef",           "plusminus",        ".notdef",          ".notdef",
 "yen",               "mu",               ".notdef",          ".notdef",
 ".notdef",           ".notdef",          ".notdef",          "ordfeminine",
 "ordmasculine",      ".notdef",          "ae",               "oslash",
/* 192-223 */
 "questiondown",      "exclamdown",       "logicalnot",       ".notdef",
 "florin",            ".notdef",          ".notdef",          "guillemotleft",
 "guillemotright",    "ellipsis",         "space",            "Agrave",
 "Atilde",            "Otilde",           "OE",               "oe",
 "endash",            "emdash",           "quotedblleft",     "quotedblright",
 "quoteleft",         "quoteright",       "divide",           ".notdef",
 "ydieresis",         "Ydieresis",        "fraction",         "currency",
 "guilsinglleft",     "guilsinglright",   "fi",               "fl",
/* 224-255 */
 "daggerdbl",         "periodcentered",   "quotesinglbase",   "quotedblbase",
 "perthousand",       "Acircumflex",      "Ecircumflex",      "Aacute",
 "Edieresis",         "Egrave",           "Iacute",           "Icircumflex",
 "Idieresis",         "Igrave",           "Oacute",           "Ocircumflex",
 ".notdef",           "Ograve",           "Uacute",           "Ucircumflex",
 "Ugrave",            "dotlessi",         "circumflex",       "tilde",
 "macron",            "breve",            "dotaccent",        "ring",
 "cedilla",           "hungarumlaut",     "ogonek",           "caron"
} ;

/*--------------------------------------------------------------------------*/
/* This version of the Mac Roman encoding is mandated by the PDF TrueType
   encoding lookup algorithm, and is referred to as MacOSEncoding in line with
   that documentation to avoid confusion.
 */

NAMECACHE *MacOSEncoding[ 256 ] ;

static const char *MacOSEncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 32-63 */
  "space",            "exclam",           "quotedbl",         "numbersign",
  "dollar",           "percent",          "ampersand",        "quotesingle",
  "parenleft",        "parenright",       "asterisk",         "plus",
  "comma",            "hyphen",           "period",           "slash",
  "zero",             "one",              "two",              "three",
  "four",             "five",             "six",              "seven",
  "eight",            "nine",             "colon",            "semicolon",
  "less",             "equal",            "greater",          "question",
/* 64-95 */
  "at",               "A",                "B",                "C",
  "D",                "E",                "F",                "G",
  "H",                "I",                "J",                "K",
  "L",                "M",                "N",                "O",
  "P",                "Q",                "R",                "S",
  "T",                "U",                "V",                "W",
  "X",                "Y",                "Z",                "bracketleft",
  "backslash",        "bracketright",     "asciicircum",      "underscore",
/* 96-127 */
  "grave",            "a",                "b",                "c",
  "d",                "e",                "f",                "g",
  "h",                "i",                "j",                "k",
  "l",                "m",                "n",                "o",
  "p",                "q",                "r",                "s",
  "t",                "u",                "v",                "w",
  "x",                "y",                "z",                "braceleft",
  "bar",              "braceright",       "asciitilde",       ".notdef",
/* 128-159 */
  "Adieresis",        "Aring",            "Ccedilla",         "Eacute",
  "Ntilde",           "Odieresis",        "Udieresis",        "aacute",
  "agrave",           "acircumflex",      "adieresis",        "atilde",
  "aring",            "ccedilla",         "eacute",           "egrave",
  "ecircumflex",      "edieresis",        "iacute",           "igrave",
  "icircumflex",      "idieresis",        "ntilde",           "oacute",
  "ograve",           "ocircumflex",      "odieresis",        "otilde",
  "uacute",           "ugrave",           "ucircumflex",      "udieresis",
/* 160-191 */
  "dagger",           "degree",           "cent",             "sterling",
  "section",          "bullet",           "paragraph",        "germandbls",
  "registered",       "copyright",        "trademark",        "acute",
  "dieresis",         "notequal",         "AE",               "Oslash",
  "infinity",         "plusminus",        "lessequal",        "greaterequal",
  "yen",              "mu",               "partialdiff",      "summation",
  "product",          "pi",               "integral",         "ordfeminine",
  "ordmasculine",     "Omega",            "ae",               "oslash",
/* 192-223 */
  "questiondown",     "exclamdown",       "logicalnot",       "radical",
  "florin",           "approxequal",      "Delta",            "guillemotleft",
  "guillemotright",   "ellipsis",         "uni00A0",          "Agrave",
  "Atilde",           "Otilde",           "OE",               "oe",
  "endash",           "emdash",           "quotedblleft",     "quotedblright",
  "quoteleft",        "quoteright",       "divide",           "lozenge",
  "ydieresis",        "Ydieresis",        "fraction",         "Euro",
  "guilsinglleft",    "guilsinglright",   "fi",               "fl",
/* 224-255 */
  "daggerdbl",        "periodcentered",   "quotesinglbase",   "quotedblbase",
  "perthousand",      "Acircumflex",      "Ecircumflex",      "Aacute",
  "Edieresis",        "Egrave",           "Iacute",           "Icircumflex",
  "Idieresis",        "Igrave",           "Oacute",           "Ocircumflex",
  "apple",            "Ograve",           "Uacute",           "Ucircumflex",
  "Ugrave",           "dotlessi",         "circumflex",       "tilde",
  "macron",           "breve",            "dotaccent",        "ring",
  "cedilla",          "hungarumlaut",     "ogonek",           "caron"
} ;

/*--------------------------------------------------------------------------*/
NAMECACHE *PDFDocEncoding[ 256 ] ;

static const char *PDFDocEncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  "breve",            "caron",            "circumflex",       "dotaccent",
  "hungarumlaut",     "ogonek",           "ring",             "tilde",
/* 32-63 */
  "space",            "exclam",           "quotedbl",         "numbersign",
  "dollar",           "percent",          "ampersand",        "quotesingle",
  "parenleft",        "parenright",       "asterisk",         "plus",
  "comma",            "hyphen",           "period",           "slash",
  "zero",             "one",              "two",              "three",
  "four",             "five",             "six",              "seven",
  "eight",            "nine",             "colon",            "semicolon",
  "less",             "equal",            "greater",          "question",
/* 64-95 */
  "at",               "A",                "B",                "C",
  "D",                "E",                "F",                "G",
  "H",                "I",                "J",                "K",
  "L",                "M",                "N",                "O",
  "P",                "Q",                "R",                "S",
  "T",                "U",                "V",                "W",
  "X",                "Y",                "Z",                "bracketleft",
  "backslash",        "bracketright",     "asciicircum",      "underscore",
/* 96-127 */
  "grave",            "a",                "b",                "c",
  "d",                "e",                "f",                "g",
  "h",                "i",                "j",                "k",
  "l",                "m",                "n",                "o",
  "p",                "q",                "r",                "s",
  "t",                "u",                "v",                "w",
  "x",                "y",                "z",                "braceleft",
  "bar",              "braceright",       "asciitilde",       ".notdef",
/* 128-159 */
  "bullet",           "dagger",           "daggerdbl",        "ellipsis",
  "emdash",           "endash",           "florin",           "fraction",
  "guilsinglleft",    "guilsinglright",   "minus",            "perthousand",
  "quotedblbase",     "quotedblleft",     "quotedblright",    "quoteleft",
  "quoteright",       "quotesinglbase",   "trademark",        "fi",
  "fl",               "Lslash",           "OE",               "Scaron",
  "Ydieresis",        "Zcaron",           "dotlessi",         "lslash",
  "oe",               "scaron",           "zcaron",           ".notdef",
/* 160-191 */
  "Euro",             "exclamdown",       "cent",             "sterling",
  "currency",         "yen",              "brokenbar",        "section",
  "dieresis",         "copyright",        "ordfeminine",      "guillemotleft",
  "logicalnot",       ".notdef",          "registered",       "macron",
  "degree",           "plusminus",        "twosuperior",      "threesuperior",
  "acute",            "mu",               "paragraph",        "periodcentered",
  "cedilla",          "onesuperior",      "ordmasculine",     "guillemotright",
  "onequarter",       "onehalf",          "threequarters",    "questiondown",
/* 192-223 */
  "Agrave",           "Aacute",           "Acircumflex",      "Atilde",
  "Adieresis",        "Aring",            "AE",               "Ccedilla",
  "Egrave",           "Eacute",           "Ecircumflex",      "Edieresis",
  "Igrave",           "Iacute",           "Icircumflex",      "Idieresis",
  "Eth",              "Ntilde",           "Ograve",           "Oacute",
  "Ocircumflex",      "Otilde",           "Odieresis",        "multiply",
  "Oslash",           "Ugrave",           "Uacute",           "Ucircumflex",
  "Udieresis",        "Yacute",           "Thorn",            "germandbls",
/* 224-255 */
  "agrave",           "aacute",           "acircumflex",      "atilde",
  "adieresis",        "aring",            "ae",               "ccedilla",
  "egrave",           "eacute",           "ecircumflex",      "edieresis",
  "igrave",           "iacute",           "icircumflex",      "idieresis",
  "eth",              "ntilde",           "ograve",           "oacute",
  "ocircumflex",      "otilde",           "odieresis",        "divide",
  "oslash",           "ugrave",           "uacute",           "ucircumflex",
  "udieresis",        "yacute",           "thorn",            "ydieresis"
} ;

/*--------------------------------------------------------------------------*/
NAMECACHE *WinAnsiEncoding[ 256 ] ;

/* Note that this contains two 'space' and 'hyphen' names */

static const char *WinAnsiEncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 32-63 */
  "space",            "exclam",           "quotedbl",         "numbersign",
  "dollar",           "percent",          "ampersand",        "quotesingle",
  "parenleft",        "parenright",       "asterisk",         "plus",
  "comma",            "hyphen",           "period",           "slash",
  "zero",             "one",              "two",              "three",
  "four",             "five",             "six",              "seven",
  "eight",            "nine",             "colon",            "semicolon",
  "less",             "equal",            "greater",          "question",
/* 64-95 */
  "at",               "A",                "B",                "C",
  "D",                "E",                "F",                "G",
  "H",                "I",                "J",                "K",
  "L",                "M",                "N",                "O",
  "P",                "Q",                "R",                "S",
  "T",                "U",                "V",                "W",
  "X",                "Y",                "Z",                "bracketleft",
  "backslash",        "bracketright",     "asciicircum",      "underscore",
/* 96-127 */
  "grave",            "a",                "b",                "c",
  "d",                "e",                "f",                "g",
  "h",                "i",                "j",                "k",
  "l",                "m",                "n",                "o",
  "p",                "q",                "r",                "s",
  "t",                "u",                "v",                "w",
  "x",                "y",                "z",                "braceleft",
  "bar",              "braceright",       "asciitilde",       "bullet",
/* 128-159 */
  "Euro",             "bullet",           "quotesinglbase",   "florin",
  "quotedblbase",     "ellipsis",         "dagger",           "daggerdbl",
  "circumflex",       "perthousand",      "Scaron",           "guilsinglleft",
  "OE",               "bullet",           "Zcaron",           "bullet",
  "bullet",           "quoteleft",        "quoteright",       "quotedblleft",
  "quotedblright",    "bullet",           "endash",           "emdash",
  "tilde",            "trademark",        "scaron",           "guilsinglright",
  "oe",               "bullet",           "zcaron",           "Ydieresis",
/* 160-191 */
  "space",            "exclamdown",       "cent",             "sterling",
  "currency",         "yen",              "brokenbar",        "section",
  "dieresis",         "copyright",        "ordfeminine",      "guillemotleft",
  "logicalnot",       "hyphen",           "registered",       "macron",
  "degree",           "plusminus",        "twosuperior",      "threesuperior",
  "acute",            "mu",               "paragraph",        "periodcentered",
  "cedilla",          "onesuperior",      "ordmasculine",     "guillemotright",
  "onequarter",       "onehalf",          "threequarters",    "questiondown",
/* 192-223 */
  "Agrave",           "Aacute",           "Acircumflex",      "Atilde",
  "Adieresis",        "Aring",            "AE",               "Ccedilla",
  "Egrave",           "Eacute",           "Ecircumflex",      "Edieresis",
  "Igrave",           "Iacute",           "Icircumflex",      "Idieresis",
  "Eth",              "Ntilde",           "Ograve",           "Oacute",
  "Ocircumflex",      "Otilde",           "Odieresis",        "multiply",
  "Oslash",           "Ugrave",           "Uacute",           "Ucircumflex",
  "Udieresis",        "Yacute",           "Thorn",            "germandbls",
/* 224-255 */
  "agrave",           "aacute",           "acircumflex",      "atilde",
  "adieresis",        "aring",            "ae",               "ccedilla",
  "egrave",           "eacute",           "ecircumflex",      "edieresis",
  "igrave",           "iacute",           "icircumflex",      "idieresis",
  "eth",              "ntilde",           "ograve",           "oacute",
  "ocircumflex",      "otilde",           "odieresis",        "divide",
  "oslash",           "ugrave",           "uacute",           "ucircumflex",
  "udieresis",        "yacute",           "thorn",            "ydieresis"
} ;

/*--------------------------------------------------------------------------*/
NAMECACHE *StandardEncoding[ 256 ] ;

static const char *StandardEncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 32-63 */
  "space",            "exclam",           "quotedbl",         "numbersign",
  "dollar",           "percent",          "ampersand",        "quoteright",
  "parenleft",        "parenright",       "asterisk",         "plus",
  "comma",            "hyphen",           "period",           "slash",
  "zero",             "one",              "two",              "three",
  "four",             "five",             "six",              "seven",
  "eight",            "nine",             "colon",            "semicolon",
  "less",             "equal",            "greater",          "question",
  /* 64-95 */
  "at",               "A",                "B",                "C",
  "D",                "E",                "F",                "G",
  "H",                "I",                "J",                "K",
  "L",                "M",                "N",                "O",
  "P",                "Q",                "R",                "S",
  "T",                "U",                "V",                "W",
  "X",                "Y",                "Z",                "bracketleft",
  "backslash",        "bracketright",     "asciicircum",      "underscore",
/* 96-127 */
  "quoteleft",        "a",                "b",                "c",
  "d",                "e",                "f",                "g",
  "h",                "i",                "j",                "k",
  "l",                "m",                "n",                "o",
  "p",                "q",                "r",                "s",
  "t",                "u",                "v",                "w",
  "x",                "y",                "z",                "braceleft",
  "bar",              "braceright",       "asciitilde",       ".notdef",
/* 128-159 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 160-191 */
  ".notdef",          "exclamdown",       "cent",             "sterling",
  "fraction",         "yen",              "florin",           "section",
  "currency",         "quotesingle",      "quotedblleft",     "guillemotleft",
  "guilsinglleft",    "guilsinglright",   "fi",               "fl",
  ".notdef",          "endash",           "dagger",           "daggerdbl",
  "periodcentered",   ".notdef",          "paragraph",        "bullet",
  "quotesinglbase",   "quotedblbase",     "quotedblright",    "guillemotright",
  "ellipsis",         "perthousand",      ".notdef",          "questiondown",
/* 192-223 */
  ".notdef",          "grave",            "acute",            "circumflex",
  "tilde",            "macron",           "breve",            "dotaccent",
  "dieresis",         ".notdef",          "ring",             "cedilla",
  ".notdef",          "hungarumlaut",     "ogonek",           "caron",
  "emdash",           ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 224-255 */
  ".notdef",          "AE",               ".notdef",          "ordfeminine",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  "Lslash",           "Oslash",           "OE",               "ordmasculine",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          "ae",               ".notdef",          ".notdef",
  ".notdef",          "dotlessi",         ".notdef",          ".notdef",
  "lslash",           "oslash",           "oe",               "germandbls",
  ".notdef",          ".notdef",          ".notdef",          ".notdef"
};

/*--------------------------------------------------------------------------*/
NAMECACHE *ISOLatin1Encoding[ 256 ] ;

/* Note that this contains two 'space' and 'hyphen' names */

static const char *ISOLatin1EncodingStrings[ 256 ] = {
/* 0-31 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
/* 32-63 */
  "space",            "exclam",           "quotedbl",         "numbersign",
  "dollar",           "percent",          "ampersand",        "quoteright",
  "parenleft",        "parenright",       "asterisk",         "plus",
  "comma",            "minus",            "period",           "slash",
  "zero",             "one",              "two",              "three",
  "four",             "five",             "six",              "seven",
  "eight",            "nine",             "colon",            "semicolon",
  "less",             "equal",            "greater",          "question",
/* 64-95 */
  "at",               "A",                "B",                "C",
  "D",                "E",                "F",                "G",
  "H",                "I",                "J",                "K",
  "L",                "M",                "N",                "O",
  "P",                "Q",                "R",                "S",
  "T",                "U",                "V",                "W",
  "X",                "Y",                "Z",                "bracketleft",
  "backslash",        "bracketright",     "asciicircum",      "underscore",
/* 96-127 */
  "quoteleft",        "a",                "b",                "c",
  "d",                "e",                "f",                "g",
  "h",                "i",                "j",                "k",
  "l",                "m",                "n",                "o",
  "p",                "q",                "r",                "s",
  "t",                "u",                "v",                "w",
  "x",                "y",                "z",                "braceleft",
  "bar",              "braceright",       "asciitilde",       ".notdef",
/* 128-159 */
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  ".notdef",          ".notdef",          ".notdef",          ".notdef",
  "dotlessi",         "grave",            "acute",            "circumflex",
  "tilde",            "macron",           "breve",            "dotaccent",
  "dieresis",         ".notdef",          "ring",             "cedilla",
  ".notdef",          "hungarumlaut",     "ogonek",           "caron",
/* 160-191 */
  "space",            "exclamdown",       "cent",             "sterling",
  "currency",         "yen",              "brokenbar",        "section",
  "dieresis",         "copyright",        "ordfeminine",      "guillemotleft",
  "logicalnot",       "hyphen",           "registered",       "macron",
  "degree",           "plusminus",        "twosuperior",      "threesuperior",
  "acute",            "mu",               "paragraph",        "periodcentered",
  "cedilla",          "onesuperior",      "ordmasculine",     "guillemotright",
  "onequarter",       "onehalf",          "threequarters",    "questiondown",
/* 192-223 */
  "Agrave",           "Aacute",           "Acircumflex",      "Atilde",
  "Adieresis",        "Aring",            "AE",               "Ccedilla",
  "Egrave",           "Eacute",           "Ecircumflex",      "Edieresis",
  "Igrave",           "Iacute",           "Icircumflex",      "Idieresis",
  "Eth",              "Ntilde",           "Ograve",           "Oacute",
  "Ocircumflex",      "Otilde",           "Odieresis",        "multiply",
  "Oslash",           "Ugrave",           "Uacute",           "Ucircumflex",
  "Udieresis",        "Yacute",           "Thorn",            "germandbls",
/* 224-255 */
  "agrave",           "aacute",           "acircumflex",      "atilde",
  "adieresis",        "aring",            "ae",               "ccedilla",
  "egrave",           "eacute",           "ecircumflex",      "edieresis",
  "igrave",           "iacute",           "icircumflex",      "idieresis",
  "eth",              "ntilde",           "ograve",           "oacute",
  "ocircumflex",      "otilde",           "odieresis",        "divide",
  "oslash",           "ugrave",           "uacute",           "ucircumflex",
  "udieresis",        "yacute",           "thorn",            "ydieresis"
};

/* -------------------------------------------------------------------------- */

NAMECACHE *MacTTGlyphNames[ 258 ] ; /* Note size of 258 */

/* See: http://developer.apple.com/fonts/TTRefMan/RM06/Chap6post.html (obs)
   Superceded by http://www.microsoft.com/typography/otspec/WGL4B.HTM */
static const char *MacTTGlyphNameStrings[ 258 ] = {
/* 0-31 */
  ".notdef",          ".null",            "nonmarkingreturn", "space",
  "exclam",           "quotedbl",         "numbersign",       "dollar",
  "percent",          "ampersand",        "quotesingle",      "parenleft",
  "parenright",       "asterisk",         "plus",             "comma",
  "hyphen",           "period",           "slash",            "zero",
  "one",              "two",              "three",            "four",
  "five",             "six",              "seven",            "eight",
  "nine",             "colon",            "semicolon",        "less",
/* 32-63 */
  "equal",            "greater",          "question",         "at",
  "A",                "B",                "C",                "D",
  "E",                "F",                "G",                "H",
  "I",                "J",                "K",                "L",
  "M",                "N",                "O",                "P",
  "Q",                "R",                "S",                "T",
  "U",                "V",                "W",                "X",
  "Y",                "Z",                "bracketleft",      "backslash",
/* 64-95 */
  "bracketright",     "asciicircum",      "underscore",       "grave",
  "a",                "b",                "c",                "d",
  "e",                "f",                "g",                "h",
  "i",                "j",                "k",                "l",
  "m",                "n",                "o",                "p",
  "q",                "r",                "s",                "t",
  "u",                "v",                "w",                "x",
  "y",                "z",                "braceleft",        "bar",
/* 96-127 */
  "braceright",       "asciitilde",       "Adieresis",        "Aring",
  "Ccedilla",         "Eacute",           "Ntilde",           "Odieresis",
  "Udieresis",        "aacute",           "agrave",           "acircumflex",
  "adieresis",        "atilde",           "aring",            "ccedilla",
  "eacute",           "egrave",           "ecircumflex",      "edieresis",
  "iacute",           "igrave",           "icircumflex",      "idieresis",
  "ntilde",           "oacute",           "ograve",           "ocircumflex",
  "odieresis",        "otilde",           "uacute",           "ugrave",
/* 128-159 */
  "ucircumflex",      "udieresis",        "dagger",           "degree",
  "cent",             "sterling",         "section",          "bullet",
  "paragraph",        "germandbls",       "registered",       "copyright",
  "trademark",        "acute",            "dieresis",         "notequal",
  "AE",               "Oslash",           "infinity",         "plusminus",
  "lessequal",        "greaterequal",     "yen",              "mu",
  "partialdiff",      "summation",        "product",          "pi",
  "integral",         "ordfeminine",      "ordmasculine",     "Omega",
/* 160-191 */
  "ae",               "oslash",           "questiondown",     "exclamdown",
  "logicalnot",       "radical",          "florin",           "approxequal",
  "Delta",            "guillemotleft",    "guillemotright",   "ellipsis",
  "uni00A0",          "Agrave",           "Atilde",           "Otilde",
  "OE",               "oe",               "endash",           "emdash",
  "quotedblleft",     "quotedblright",    "quoteleft",        "quoteright",
  "divide",           "lozenge",          "ydieresis",        "Ydieresis",
  "fraction",         "currency",         "guilsinglleft",    "guilsinglright",
/* 192-223 */
  "fi",               "fl",               "daggerdbl",        "periodcentered",
  "quotesinglbase",   "quotedblbase",     "perthousand",      "Acircumflex",
  "Ecircumflex",      "Aacute",           "Edieresis",        "Egrave",
  "Iacute",           "Icircumflex",      "Idieresis",        "Igrave",
  "Oacute",           "Ocircumflex",      "apple",            "Ograve",
  "Uacute",           "Ucircumflex",      "Ugrave",           "dotlessi",
  "circumflex",       "tilde",            "macron",           "breve",
  "dotaccent",        "ring",             "cedilla",          "hungarumlaut",
/* 224-255 */
  "ogonek",           "caron",            "Lslash",           "lslash",
  "Scaron",           "scaron",           "Zcaron",           "zcaron",
  "brokenbar",        "Eth",              "eth",              "Yacute",
  "yacute",           "Thorn",            "thorn",            "minus",
  "multiply",         "onesuperior",      "twosuperior",      "threesuperior",
  "onehalf",          "onequarter",       "threequarters",    "franc",
  "Gbreve",           "gbreve",           "Idot",             "Scedilla",
  "scedilla",         "Cacute",           "cacute",           "Ccaron",
/* 256,257 */
  "ccaron",           "dmacron"
} ;


/* initEncodings - initialize standard encodings */
Bool initEncodings(void)
{
  register int32 i ;
  OBJECT *starray, *isoarray ;
  OBJECT stencode = OBJECT_NOTVM_NOTHING, isoencode = OBJECT_NOTVM_NOTHING ;

  starray  = get_gomemory(256);
  isoarray = get_gomemory(256);
  if (starray == NULL || isoarray == NULL)
    return FALSE ;

  theTags(stencode) = OARRAY | LITERAL | READ_ONLY ;
  SETGLOBJECTTO(stencode, TRUE) ;
  theLen(stencode) = 256 ;
  oArray(stencode) = starray ;

  theTags(isoencode) = OARRAY | LITERAL | READ_ONLY ;
  SETGLOBJECTTO(isoencode, TRUE) ;
  theLen(isoencode) = 256 ;
  oArray(isoencode) = isoarray ;

  for ( i = 0 ; i < 256 ; ++i ) {
    StandardEncoding[i] = cachename((uint8 *)StandardEncodingStrings[i],
                                    strlen_uint32(StandardEncodingStrings[i])) ;
    HQASSERT(StandardEncoding[i], "cachename failed for StandardEncoding") ;

    object_store_namecache(&starray[i], StandardEncoding[i], LITERAL) ;

    ISOLatin1Encoding[i] = cachename((uint8 *)ISOLatin1EncodingStrings[i],
                                     strlen_uint32(ISOLatin1EncodingStrings[i])) ;
    HQASSERT(ISOLatin1Encoding[i], "cachename failed for ISOLatin1Encoding") ;

    object_store_namecache(&isoarray[i], ISOLatin1Encoding[i], LITERAL) ;

    MacExpertEncoding[i] = cachename((uint8 *)MacExpertEncodingStrings[i],
                                     strlen_uint32(MacExpertEncodingStrings[i])) ;
    HQASSERT(MacExpertEncoding[i], "cachename failed for MacExpertEncoding") ;

    MacRomanEncoding[i] = cachename((uint8 *)MacRomanEncodingStrings[i],
                                    strlen_uint32(MacRomanEncodingStrings[i])) ;
    HQASSERT(MacRomanEncoding[i], "cachename failed for MacRomanEncoding") ;

    MacOSEncoding[i] = cachename((uint8 *)MacOSEncodingStrings[i],
                                 strlen_uint32(MacOSEncodingStrings[i])) ;
    HQASSERT(MacOSEncoding[i], "cachename failed for MacOSEncoding") ;

    PDFDocEncoding[i] = cachename((uint8 *)PDFDocEncodingStrings[i],
                                  strlen_uint32(PDFDocEncodingStrings[i])) ;
    HQASSERT(PDFDocEncoding[i], "cachename failed for PDFDocEncoding") ;

    WinAnsiEncoding[i] = cachename((uint8 *)WinAnsiEncodingStrings[i],
                                   strlen_uint32(WinAnsiEncodingStrings[i])) ;
    HQASSERT(WinAnsiEncoding[i], "cachename failed for WinAnsiEncoding") ;
  }

  for ( i = 0 ; i < 258 ; ++i ) {
    MacTTGlyphNames[i] = cachename((uint8 *)MacTTGlyphNameStrings[i],
                                   strlen_uint32(MacTTGlyphNameStrings[i])) ;
    HQASSERT(MacTTGlyphNames[i], "cachename failed for MacTTGlyphNames") ;
  }

  oName(nnewobj) = &system_names[NAME_StandardEncoding] ;
  i = insert_hash(&systemdict, &nnewobj, &stencode) ;
  HQASSERT(i, "StandardEncoding not defined in systemdict") ;

  oName(nnewobj) = &system_names[NAME_ISOLatin1Encoding] ;
  i = insert_hash(&systemdict, &nnewobj, &isoencode) ;
  HQASSERT(i, "StandardEncoding not defined in systemdict") ;

  return TRUE ;
}

/* Log stripped */
