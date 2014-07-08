/* $HopeName: SWtools!src:nametool.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
* Log stripped */

#define _SVID_SOURCE 1
#include <string.h>
#include "std.h"
#include <stdio.h>

/* operator class flags for the namecache entries. They are also defined in
 * v20:objects.h.
 */
 			    /* Can have up to 7 levels. */
#define LEVEL1OP            0x0001 /* operator is a level 1 one */
#define LEVEL2OP            0x0002 /* operator is a level 2 one */
#define LEVEL3OP            0x0003 /* operator is a level 3 one */
#define LEVEL4OP            0x0004 /* operator is a level 4 one */

#define COLOREXTOP          0x0008 /* operator is a colour extension operator */
#define SHADOWOP            0x0010 /* not used within names.nam */
#define CANNOTSHADOWOP      0x0020 /* dissallow operator to be shadowed */
#define SEPARATIONDETECTOP  0x0040 /* operator used for seps detection */
#define FUNCTIONOP          0x0080 /* Can be used as PDF+ encapsulated function */
#define RECOMBINEDETECTOP   0x0100 /* used for recombine color equivalents */
#define NAMEOVERRIDEOP      0x0200 /* Name is part of a nameoverride set. */

#define MAX_LINE_LENGTH 512
#define EXPLICIT_OPERATOR 1
#define EXPLICIT_NAME 2
#define IMPLICIT_OPERATOR 3
#define IMPLICIT_NAME 4

enum {
  CODING_UNKNOWN = 0,
  CODING_UTF8 = 1,
  CODING_UTF16 = 2
} ;

typedef struct line_s {
  struct line_s * next;
  int index, type, op_class, clen, reservedname, reservedop, coding, maxlength ;
  unsigned char *name, *cstring, *alias, *dict, *opname, *successor, *build ;
} LINE;

static LINE *namelist = NULL ;

typedef struct filename_t {
  struct filename_t *next ;
  int len ;
  char name[1] ; /* Extra allocated after struct */
} filename_t ;

static filename_t *reserved ;

static FILE * outfile = NULL ;
static int exclude = 0 ;

static char * operator_prototype = "/*@notnull@*/ /*@in@*/ struct ps_context_t *pscontext";


/* ---------------------------------------------------------------------- */
extern int main(int argc, char * argv []);

static unsigned char *find(unsigned char * line, unsigned char * match);
static void tidyexit( int exitcode );
static void read_the_whole_file (char * filename);
static void defs(FILE * f);
static void system_names(FILE * f);
static void system_ops(FILE * f);
static void decl(FILE * f);
static void odict(FILE * f);
static void sdict(FILE * f);
static void freeline(LINE *line) ;

/* ---------------------------------------------------------------------- */

typedef struct OPNAME_CACHE {
  struct OPNAME_CACHE*  next;
  unsigned char*        opname;
  int                   len;
} OPNAME_CACHE;

#define OPNAME_CACHE_SIZE (1049u)

OPNAME_CACHE* opname_cache[OPNAME_CACHE_SIZE];

#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

static unsigned long int opname_hash(
  unsigned char*  opname,
  long int        len)
{
  unsigned long int hash = 0;
  unsigned long int bits = 0;

  while ( --len >= 0 ) {
    hash = (hash << PJW_SHIFT) + *opname++;
    bits = hash&PJW_MASK;
    hash ^= bits|(bits >> PJW_RIGHT_SHIFT);
  }

  return(hash);
}

static int opname_exists(
  unsigned char*  opname)
{
  int len;
  unsigned long int hash;
  OPNAME_CACHE* entry;

  len = strlen(opname);
  hash = opname_hash(opname, len)%OPNAME_CACHE_SIZE;
  /* Scan cache chain for match */
  for ( entry = opname_cache[hash]; entry != NULL; entry = entry->next ) {
    if ( (len == entry->len) && (memcmp(opname, entry->opname, len) == 0) ) {
      return(1);
    }
  }
  return(0);
}

static void opname_add(
  unsigned char*  opname)
{
  int len;
  unsigned long int hash;
  OPNAME_CACHE* entry;

  entry = malloc(sizeof(OPNAME_CACHE));
  if ( entry == NULL ) {
    fprintf(stderr, "nametool: run out of memory\n");
    tidyexit(1) ;
  }
  /* Add entry to cache */
  len = strlen(opname);
  hash = opname_hash(opname, len)%OPNAME_CACHE_SIZE;
  entry->next = opname_cache[hash];
  entry->len = len;
  entry->opname = opname;
  /* Add to cache */
  opname_cache[hash] = entry;
}

static void opname_free(void)
{
  int i;
  OPNAME_CACHE* entry;
  OPNAME_CACHE* next;

  for ( i = 0; i < OPNAME_CACHE_SIZE; i++ ) {
    for ( entry = opname_cache[i]; entry; entry = next ) {
      next = entry->next;
      free(entry->opname);
      free(entry);
    }
  }
}

unsigned char* opname_copy(
  unsigned char*  str,
  unsigned char*  append)
{
  unsigned char*  copy;

  copy = malloc((strlen((char*)str) + strlen((char*)append) + 1)*sizeof(unsigned char));
  if ( copy == NULL ) {
    fprintf(stderr, "nametool: run out of memory\n");
    tidyexit(1) ;
  }
  sprintf(copy, "%s%s", str, append);
  return(copy);
}

/* ---------------------------------------------------------------------- */
/* find a field, and return the end address of the match (the value start) */
static unsigned char *find(unsigned char * line, unsigned char * match)
{
  int i=0, c, len ;
  unsigned char *source = line ;

  len = strlen( match ) ;
  while ( (c = *source++) != '\0' )
    if ( c == match[ i ]) {
      if ( ++i == len )  /* matched, so return argument */
	return source ;
    } else      /* not matched, so start looking from start again */
      i = 0 ;

  return NULL ;
}

/* ---------------------------------------------------------------------- */
static void tidyexit( int exitcode )
{
  /* free up any heap that we may have used */
  while ( namelist != NULL ) {
    LINE *next = namelist->next ;

    freeline( namelist ) ;
    namelist = next ;
  }

  opname_free();

  while ( reserved != NULL ) {
    filename_t *next = reserved->next ;

    free(reserved) ;
    reserved = next ;
  }

  if ( outfile != NULL )
  {
    fclose( outfile ) ;
    outfile = NULL ;
  }

  exit( exitcode ) ;
}


/* ---------------------------------------------------------------------- */
/* Safe strcmp for equality */
static int streq(char *a, char *b)
{
  if ( a == NULL ) {
    if ( b == NULL )
      return 1 ;
    return 0 ;
  }

  if ( b == NULL )
    return 0 ;

  return (strcmp(a, b) == 0) ;
}

/* ---------------------------------------------------------------------- */
/* strdup clone, with extra terminator for attribute values */
static unsigned char *dupstr(unsigned char *str, unsigned char terminator)
{
  int len ;
  unsigned char *ch, *dup ;

  if ( str == NULL )
    return NULL ;

  for ( ch = str ; *ch != terminator; ++ch )
    if ( *ch == '\0' ) {
      fprintf(stderr, "nametool: terminator '%c' not found in '%s'",
               terminator, str);
      tidyexit(1) ;
    }

  len = (ch - str) ;

  if ( (dup = malloc(len + 1)) == NULL ) {
    fprintf(stderr, "nametool: run out of memory");
    tidyexit(1) ;
  }

  for ( ch = dup ; len > 0 ; --len )
    *ch++ = *str++ ;

  *ch = '\0' ;

  return dup ;
}

/* ---------------------------------------------------------------------- */
static void freeline(LINE *line)
{
  if ( line->name )
    free(line->name) ;
  if ( line->cstring )
    free(line->cstring) ;
  if ( line->alias )
    free(line->alias) ;
  if ( line->dict )
    free(line->dict) ;
  if ( line->opname )
    free(line->opname) ;
  if ( line->successor )
    free(line->successor) ;

  free(line) ;
}

/* ---------------------------------------------------------------------- */
/* Read the names into a single list, in reverse order. */
static void read_the_whole_file (char * filename)
{
  FILE * infile = NULL;
  unsigned char * ch, * line;
  LINE * new, **prev ;
  char buffer [MAX_LINE_LENGTH];
  filename_t *base ;
  int inreserved = 0 ;
  int flen = strlen(filename) ;
  int lineno = 0 ;

  for ( base = reserved ; base ; base = base->next ) {
    int offset = flen - base->len ;

    if ( offset >= 0 &&
         strcmp(filename + offset, base->name) == 0 &&
         (offset == 0 ||
          filename[offset - 1] == '/' || /* Unix, MacOS X */
          filename[offset - 1] == ':' || /* MacOS 9 */
          filename[offset - 1] == '\\')  /* Windows */ ) {
      inreserved = 1 ;
      break ;
    }
  }

  infile = fopen(filename, "r");
  if (! infile) {
    fprintf(stderr, "nametool: cannot open input file '%s'\n", filename);
    tidyexit(1);
  }

  while (fgets (buffer, MAX_LINE_LENGTH, infile)) {
    ++lineno ;

    if (buffer [0] == '#') {
      continue; /* ignore lines starting with '#' */
    }
    /* strip off comments */
    ch = buffer;
    for (;;) {
      if (* ch == '%' || * ch == '\n' || * ch == '\r') {
        /* rest of line is comment, or end of line. Put in two terminators,
           in case only a name is present (otherwise line pointer would be
           invalid when searching attributes later). */
        ch[0] = '\0';
        ch[1] = '\0';
        break;
      }
      ch++;
    }
    /* now ignore blank lines */
    ch = buffer;
    while (* ch == ' ' || * ch == '\t') {
      ch++;
    }
    if (* ch == '\0') {
      continue;
    }
    /* Parse the line fully before testing if it is a duplicate */
    /* it's a useful line - save it */
    if ( (new = (LINE *)malloc(sizeof(LINE))) == NULL ) {
      fprintf(stderr, "nametool: run out of memory in file %s, line %d\n",
              filename, lineno);
      tidyexit(1) ;
    }

    /* Set default values */
    new->cstring = NULL ;
    new->next = NULL;
    new->index = -1;
    new->op_class = LEVEL1OP ;
    new->alias = NULL ;
    new->dict = NULL ;
    new->opname = NULL ;
    new->successor = NULL ;
    new->reservedname = inreserved ;
    new->reservedop = 0 ;
    new->coding = CODING_UTF8 ;
    new->maxlength = 127 ;
    new->build = NULL ;

    /* Extract string and names properly */
    for ( line = ch, new->clen = 0 ;
          *line != '\0' && *line != ' ' && *line != '\t' ;
          ++new->clen ) {
      if ( *line != '\\' ) {
        if ( *line >= 128 )
          new->coding = CODING_UNKNOWN ;
        line++ ;
      } else {
        switch( line[1] ) {
        case '\\': case '\'': case '\"': case '\?':
        case 'n': case 't': case 'v': case 'b':
        case 'r': case 'f': case 'a':
          line += 2;
          break;
        case '\0':
            fprintf(stderr,
                    "nametool: nametool doesn't support backslash line continuation in file %s, line %d: '%s'\n",
                    filename, lineno, ch) ;
            freeline(new) ;
            tidyexit(1) ;
        default:
          /* Accept only three-char octal for the moment.  Note
           * that we won't go past a '\0' terminator because '\0' < '0'. */
          if ( line[1] < '0' || line[1] > '3' ||
               line[2] < '0' || line[2] > '7' ||
               line[3] < '0' || line[3] > '7') {
            fprintf(stderr, "nametool: couldn't parse backslash sequence in file %s, line %d: '%s'\n",
                    filename, lineno, ch) ;
            freeline(new) ;
            tidyexit(1) ;
          }
          if ( line[1] >= '2' )
            new->coding = CODING_UNKNOWN ;
          line += 4 ;
        }
      }
    }
    *line++ = '\0' ;

    /* Copy string value */
    new->cstring = dupstr(ch, '\0') ;

    if ( (ch = find(line, "NAME(")) != NULL )
      new->name = dupstr(ch, ')') ;
    else
      new->name = dupstr(new->cstring, '\0') ;

    /* Contrary to old documentation in names.nam, these can appear for
       non-operators. */
    if ( find(line, "LEVEL2" ))
      new->op_class = LEVEL2OP ;
    if ( find(line, "LEVEL3" ))
      new->op_class = LEVEL3OP ;

    if ( find(line, "FUNCTION" ))
      new->op_class |= FUNCTIONOP ;
    if ( find(line, "RECOMBINEDETECT" ))
      new->op_class |= RECOMBINEDETECTOP ;

    if ( find(line, "LONG" ))
      new->maxlength = 65535 ;

    if ( (ch = find(line, "CODING(")) != NULL ) {
      if ( strncmp(ch, "UTF8)", 5) == 0 ) {
        new->coding = CODING_UTF8 ;
      } else if ( strncmp(ch, "UTF16)", 6) == 0 ) {
        new->coding = CODING_UTF16 ;
      } else {
        fprintf(stderr, "nametool: coding scheme file %s, line %d: CODING(%s is unknown\n",
                filename, lineno, ch) ;
        freeline(new) ;
        tidyexit(1) ;
      }
    }

    if ( (ch = find(line, "OPERATOR")) != NULL ) {
      /* Check OPERATOR-specific keywords */
      new->reservedop = inreserved ;

      if ( *ch++ == '(' ) /* OPERATOR(name) found */
        new->opname = dupstr(ch, ')') ;

      if ( find(line, "COLOREXT" ))
        new->op_class |= COLOREXTOP ;
      if ( find(line, "CANNOTSHADOW" ))
        new->op_class |= CANNOTSHADOWOP ;
      if ( find(line, "SEPARATIONDETECT" ))
        new->op_class |= SEPARATIONDETECTOP ;

      if ( (ch = find(line, "ALIAS(")) != NULL )
        new->alias = dupstr(ch, ')') ;
      if ( (ch = find(line, "DICT(")) != NULL )
        new->dict = dupstr(ch, ')') ;
      if ( (ch = find(line, "BUILD(")) != NULL )
        new->build = dupstr(ch, ')') ;

      if ( (ch = find(line, "INDEX(")) != NULL ) {
        new->index = atol(ch) ;
        new->type = EXPLICIT_OPERATOR ;
      } else {
        new->type = IMPLICIT_OPERATOR ;
      }
    } else {
      if ( (ch = find(line, "INDEX(")) != NULL ) {
        new->index = atol(ch) ;
        new->type = EXPLICIT_NAME ;
      } else {
        if ( (ch = find(line, "NEXT(")) != NULL )
          new->successor = dupstr(ch, ')') ;
        new->type = IMPLICIT_NAME ;
      }
    }

    /* Search for duplicate. One of the definitions must be a simple implicit
       name, or the parameters must match exactly. */
    for ( prev = &namelist ; *prev ; prev = &(*prev)->next ) {
      LINE *here = *prev ;

      if ( streq(here->name, new->name) ) {
        if ( !streq(here->cstring, new->cstring) ) {
          fprintf(stderr, "nametool: name %s has conflicting strings '%s' and '%s', file %s, line %d\n",
                  new->name, here->cstring, new->cstring, filename, lineno) ;
          freeline(new) ;
          tidyexit(1) ;
        }

        /* Reconcile ordering; new name can add order to old name */
        if ( !streq(new->successor, here->successor) ) {
          if ( new->successor == NULL && new->type == IMPLICIT_NAME ) {
            new->successor = dupstr(here->successor, '\0') ;
          } else if ( here->successor == NULL && here->type == IMPLICIT_NAME ) {
            here->successor = dupstr(new->successor, '\0') ;
          } else {
            fprintf(stderr, "nametool: name %s has conflicting successors '%s' and '%s', file %s, line %d\n",
                    new->name, here->successor, new->successor, filename, lineno) ;
            freeline(new) ;
            tidyexit(1) ;
          }
        }

        /* Reconcile encoding; new name can add encoding to old name */
        if ( here->coding == CODING_UNKNOWN )
          here->coding = new->coding ;
        else if ( new->coding == CODING_UNKNOWN )
          new->coding = here->coding ;

        if ( here->type == IMPLICIT_NAME &&
             (new->type != IMPLICIT_NAME || here->op_class == LEVEL1OP ) ) {
          new->next = here->next ; /* Use new definition */
          *prev = new ;
          new->reservedname &= here->reservedname ;
          if ( here->maxlength > new->maxlength )
            new->maxlength = here->maxlength ;
          freeline(here) ;
        } else if ( new->type == IMPLICIT_NAME &&
                    (here->type != IMPLICIT_NAME || new->op_class == LEVEL1OP ) ) {
          here->reservedname &= new->reservedname ;
          if ( new->maxlength > here->maxlength )
            here->maxlength = new->maxlength ;
          freeline(new) ; /* Use old definition */
        } else if ( new->index != here->index ||
                    new->type != here->type ||
                    new->op_class != here->op_class ||
                    new->clen != here->clen ||
                    !streq(new->alias, here->alias) ||
                    !streq(new->dict, here->dict) ||
                    !streq(new->opname, here->opname) ) {
          fprintf(stderr, "nametool: name %s has conflicting definitions, file %s, line %d\n",
                  new->name, filename, lineno) ;
          freeline(new) ;
          tidyexit(1) ;
        } else {
          here->reservedname &= new->reservedname ;
          here->reservedop &= new->reservedop ;
          if ( new->maxlength > here->maxlength )
            here->maxlength = new->maxlength ;
          freeline(new) ; /* Exactly the same, use old definition */
        }

        new = NULL ;

        break ; /* Dealt with duplicate */
      }
    }

    if ( new ) {
      new->next = namelist ;
      namelist = new ;
    }
  }

  fclose(infile);
}

/* Sort names from name list into explicit names by index, then implicit
   operators, then implicit names. */
void sort_names(void)
{
  int count = -1 ;
  LINE *sorted = NULL ;

  /* Insertion sort from namelist into the new list */
  while ( namelist ) {
    LINE **prev, *here, *next = namelist->next ;

    for ( prev = &sorted ; (here = *prev) != NULL ; prev = &here->next ) {
      if ( here->type == EXPLICIT_OPERATOR || here->type == EXPLICIT_NAME ) {
        if ( namelist->type == EXPLICIT_OPERATOR || namelist->type == EXPLICIT_NAME )
          if ( namelist->index < here->index )
            break ;
      } else if ( here->type == IMPLICIT_OPERATOR ) {
        if ( namelist->type != IMPLICIT_NAME )
          break ;
      } else if ( namelist->successor ) {
        if ( streq(namelist->successor, here->name) )
          break ; /* Found the position before NEXT(n) */
      } else {
        /* Found first implicit name. Insert before this, because
           namelist was built in reverse and this will unwind the reversal. */
        break ;
      }
    }

    if ( namelist->successor && here == NULL ) {
      /* No such name was found. Find the successor name in the namelist,
         move it to the head of this list, and insert it first. */

      for ( prev = &namelist->next ; (here = *prev) != NULL ; prev = &here->next ) {
        if ( streq(namelist->successor, here->name) ) {
          *prev = here->next ;
          here->next = namelist ;
          namelist = here ;
          break ;
        }
      }
      continue ;
    }

    /* Insert name at position found */
    namelist->next = *prev ;
    *prev = namelist ;

    namelist = next ;
  }
  namelist = sorted ;

  /* Number the implicit names */
  for ( sorted = namelist ; sorted ; sorted = sorted->next ) {
    if ( sorted->index == -1 )
      sorted->index = ++count ;
    else
      count = sorted->index ;
  }
}


/* ---------------------------------------------------------------------- */
/* output the #defines for the names */
static void defs(FILE * f)
{
  /* there are three streams of numbers running: the fixed indexes, the
     additional operators and the rest */
  LINE * here;
  int theindex = 0, theoperatorindex = 0;
#ifndef DECLARE_IN_NAMETAB
  int duplicate = 0;
  unsigned char *opname;
#endif /* !DECLARE_IN_NAMETAB */

  fprintf(f, "#ifndef __NAMEDEF__H__\n") ;
  fprintf(f, "#define __NAMEDEF__H__\n") ;
  fprintf(f,
          "/** \\file\n"
          " *  \\ingroup psops\n"
          " *  \\brief Machine generated definitions for PostScript names. DO NOT EDIT.\n"
          " */\n") ;

  for (here = namelist ; here ; here = here->next) {
    theindex = here->index;
    if ( here->type == IMPLICIT_OPERATOR || here->type == EXPLICIT_OPERATOR ) {
      theoperatorindex = theindex; /* Highest operator index */
    }
    if ( !exclude || !here->reservedname ) {
      if ( !streq(here->name, here->cstring) ) {
        fprintf(f, "#define NAME_%s %d /* aka '%s' */\n",
                 here->name, theindex, here->cstring);
      } else {
        fprintf(f, "#define NAME_%s %d\n", here->name, theindex);
      }
    }
  }

  fprintf(f,
          "\n"
          "struct ps_context_t ;\n"
          "\n"
          "/** \\defgroup psops PostScript Operators.\n"
          "    \\ingroup ps\n"
          "    \\{ */\n");

#ifndef DECLARE_IN_NAMETAB
  for (here = namelist ; here ; here = here->next) {
    if (here->type == IMPLICIT_NAME) {
      break;
    }
    if (here->type != EXPLICIT_NAME) {
      if ( !exclude || !here->reservedop ) {
        if ( here->opname ) {
          opname = opname_copy(here->opname, "");
        } else {
          opname = opname_copy(here->cstring, "_");
        }
        duplicate = opname_exists(opname);
        /* Note: an @ char needs to be escaped for Doxygen */
        fprintf(f, "/*%s The PostScript operator %s%s. */\n"
                "extern Bool %s(%s);\n",
                duplicate ? "" : "* \\brief",
                *here->cstring == '@' ? "\\" : "", here->cstring,
                opname, operator_prototype);
        if ( duplicate ) {
          free(opname);
        } else {
          opname_add(opname);
        }
      }
    }
  }
  fprintf(f, "/** \\brief A dummy PostScript operator. */\n"
          "extern Bool hqndummy_(%s);\n", operator_prototype);
#endif

  fprintf(f, "/** \\} */\n" "\n");
  fprintf(f, "#define NAMES_COUNTED (%d)\n", theindex + 1) ;
  fprintf(f, "#define OPS_COUNTED (%d)\n", theoperatorindex + 1) ;

  fprintf(f, "\n") ;

  /* This is the last line of the generated file: */
  fprintf(f, "#endif  /* protection for multiple inclusion */\n");
}


static void print_build(FILE *f, char *build)
{
  char *copy = strdup(build); /* because strtok modifies it */
  char *ptr = strtok(copy, " ");

  fprintf(f, "defined(%s_BUILD)", ptr);
  while ((ptr = strtok(NULL, " ")) != NULL)
    fprintf(f, " || defined(%s_BUILD)", ptr);
  free(copy);
}


/* ---------------------------------------------------------------------- */
/* create the NAMECACHE array for all system names */
static void system_names(FILE * f)
{
  LINE * here;
  int last = -1 ;

  fprintf(f, "NAMECACHE system_names[] = {\n");

  for (here = namelist ; here ; here = here->next) {
    while ( ++last < here->index ) {
      fprintf(f,"   /* %4d */ {0, 0, 0, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL}, /* Dummy */\n",
              last) ;
    }

    if (last > here->index) {
      fprintf(stderr, "nametool: illegal data format\n") ;
      tidyexit(1) ;
    }

    if (here->clen > here->maxlength) {
      fprintf(stderr, "nametool: name %s is longer (%d) than maximum allowed (%d).\n"
              " Use LONG for long interned strings (NOT names).\n",
              here->cstring, here->clen, here->maxlength) ;
      tidyexit(1) ;
    }

    if ( here->reservedname ) {
      fprintf(f,"   /* %4d */ {0, 0, 0, NULL, NULL, 0, 0, NULL, NULL, NULL, NULL}, /* NAME_%s only in variant */\n",
              here->index, here->name) ;
    } else {
      fprintf(f,
              "   /* %4d */ { tag_NCACHE, 0, %2d, NULL, (uint8 *)\"%s\", %d, NAME_%s, NULL, NULL, NULL, NULL},\n",
              here->index, here->clen, here->cstring, here->op_class, here->name);
    }
  }

  fprintf(f, "} ;\n") ;
}


/* ---------------------------------------------------------------------- */
/* define all the operators, giving the function to call at the
   right position in the array */
static void system_ops(FILE * f)
{
  LINE * here;
  int last = -1, count = 0 ;

  fprintf(f, "OPERATOR system_ops[] = {\n") ;

  for (here = namelist ; here ; here = here->next) {

    if (here->type == IMPLICIT_NAME) {
      break;
    }
    if (here->type != EXPLICIT_NAME) {
      unsigned char *alias ;

      ++count ;
      while ( ++last < here->index ) {
        fprintf(f, "  /* %4d */ { NULL, NULL }, /* Dummy */\n", last) ;
      }

      if ( last > here->index ) {
        fprintf(stderr,"nametool: illegal data format\n") ;
        tidyexit(1) ;
      }

      alias = here->alias ? here->alias : here->name ;

      if ( here->reservedop ) {
        fprintf(f, "  /* %4d */ { NULL, NULL }, /* NAME_%s only in variant */\n",
                here->index, alias) ;
      } else {
        if ( here->build ) {
          fprintf(f, "#if ");
          print_build(f, here->build);
          fprintf(f, "\n");
        }
        if ( here->opname ) {
          fprintf(f,
                  "  /* %4d */ { %s, system_names + NAME_%s }, /* aka '%s' */\n",
                  here->index, here->opname, alias, here->cstring) ;
        } else {
          fprintf(f, "  /* %4d */ { %s_, system_names + NAME_%s },\n",
                  here->index, here->cstring, alias) ;
        }
        if ( here->build )
          fprintf(f,
                  "#else\n"
                  "            { NULL, &system_names[%d] },\n"
                  "#endif\n ",
                  here->index);
      }
    }
  }

  fprintf(f, "} ;\n\n") ;
}

/* ---------------------------------------------------------------------- */
/* produce declarations for the operator functions */
static void decl(FILE * f)
{
#ifdef DECLARE_IN_NAMETAB
  LINE * here;
#endif

  fprintf(f, "/* machine generated -- DO NOT EDIT THIS FILE */\n") ;
  fprintf(f, "#include \"core.h\"\n");
  fprintf(f, "#include \"objects.h\"\n");
  fprintf(f, "#include \"namedef_.h\"\n\n");
  fprintf(f, "/*@-nullinit@*/\n\n");

#ifdef DECLARE_IN_NAMETAB
  fprintf(f,
          "\n"
          "struct ps_context_t ;\n"
          "\n") ;

  for (here = namelist ; here ; here = here->next) {
    if (here->type == IMPLICIT_NAME) {
      break;
    }
    if (here->type != EXPLICIT_NAME) {
      if ( !here->reservedop ) {
        if ( here->opname ) {
          fprintf(f, "extern Bool %s(%s); /* aka '%s' */\n",
                   here->opname, operator_prototype, here->cstring) ;
        } else {
          fprintf(f, "extern Bool %s_(%s);\n", here->cstring, operator_prototype);
        }
      }
    }
  }

  fprintf(f, "\n");
#endif
}

/* ---------------------------------------------------------------------- */
/* for those that arn't in systemdict, write out code to define them */
static void odict(FILE * f)
{
  LINE * here;

  fprintf(f, "%%!PS-ADOBE\n") ;
  fprintf(f, "%% machine-generated -- DO NOT EDIT THIS FILE\n") ;

  for (here = namelist ; here ; here = here->next) {
    if (here->type == IMPLICIT_NAME) {
      break;
    }
    if (here->type != EXPLICIT_NAME) {
      if ( !here->reservedop && here->dict ) {
        fprintf(f, "%s (%s) %d m_op\n", here->dict, here->cstring, here->index) ;
      }
    }
  }
}

/* ---------------------------------------------------------------------- */
/* write out code to define all the systemdict operators */
static void sdict(FILE * f)
{
  LINE * here;

  fprintf(f, "%%!PS-ADOBE\n") ;
  fprintf(f, "%% machine-generated -- DO NOT EDIT THIS FILE\n") ;
  fprintf(f, "%% systemdict will be on the stack on entry\n") ;

  for (here = namelist ; here ; here = here->next) {
    if (here->type == IMPLICIT_NAME) {
      break;
    }
    if (here->type != EXPLICIT_NAME) {
      if ( !here->reservedop && !here->dict ) {
        fprintf(f, "dup (%s) %d m_op\n", here->cstring, here->index) ;
      }
    }
  }
  fprintf(f, "pop %% systemdict, pushed in C\n") ;
}

/* ---------------------------------------------------------------------- */
int main(int argc, char * argv [])
{
  char *program ;

#define USAGE_STRING \
  "usage: %s [-R<reserved-files>]... <inputfile>... <namedefs> <nametables> <systemdict> <otherdicts>\n"

  for ( program = *argv++ ; --argc ; ++argv ) {
    if ( argv[0][0] != '-' )
      break ;

    switch ( argv[0][1] ) {
      filename_t *name ;
      int len ;
    case 'R':
      len = strlen(&argv[0][2]) ;
      if ( (name = malloc(sizeof(filename_t) + len)) == NULL ) {
        fprintf(stderr, "nametool: run out of memory\n");
        tidyexit(1) ;
      }

      strcpy(name->name, &argv[0][2]) ;
      name->len = len ;
      name->next = reserved ;
      reserved = name ;

      break ;
    case 'x':
      if ( argv[0][2] != '\0' ) {
        fprintf(stderr, USAGE_STRING "unrecognised option '%s'\n",
                program, *argv) ;
        tidyexit(1) ;
      }
      exclude = 1 ;
      break ;
    default:
      fprintf(stderr, USAGE_STRING "unrecognised option '%s'\n",
              program, *argv) ;
      tidyexit(1) ;
    }
  }

  if ( argc < 5 ) {
    fprintf(stderr, USAGE_STRING, program) ;
    tidyexit(1) ;
  }

  do {
    read_the_whole_file(*argv++);
  } while ( --argc > 4 ) ;

  sort_names() ;

  outfile = fopen(*argv++, "w");
  if (! outfile) {
    fprintf(stderr, "nametool: cannot open output file '%s'\n", *argv);
    tidyexit(1) ;
  }
  defs(outfile);
  fclose(outfile);

  outfile = fopen(*argv++, "w");
  if (! outfile) {
    fprintf(stderr, "nametool: cannot open output file '%s'\n", *argv);
    tidyexit(1) ;
  }
  decl(outfile);
  system_names(outfile);
  system_ops(outfile);
  fclose(outfile);

  outfile = fopen(*argv++, "w");
  if (! outfile) {
    fprintf(stderr, "nametool: cannot open output file '%s'\n", *argv);
    tidyexit(1);
  }
  sdict(outfile);
  fclose(outfile);

  outfile = fopen(*argv++, "w");
  if (! outfile) {
    fprintf(stderr, "nametool: cannot open output file '%s'\n", *argv);
    tidyexit(1) ;
  }
  odict(outfile);
  fclose(outfile);
  outfile = NULL;

  tidyexit(0);

  return (0);			/* only added to prevent the MPW compiler complaining */
}

/* end of nametool.c */
