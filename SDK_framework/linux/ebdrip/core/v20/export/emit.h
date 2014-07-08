/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:emit.h(EBDSDK_P.1) $
 * $Id: export:emit.h,v 1.16.10.1.1.1 2013/12/19 11:25:20 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS object emitter code.
 */

#ifndef __EMIT_H__
#define __EMIT_H__


#include "objecth.h"
#include "fileioh.h"
#include "chartype.h"

/* ----- External constants ----- */

/* ----- External structures ----- */

typedef struct emit_state EMIT_STATE ;

typedef int32 (*EMIT_EXTRATYPES_PROC)(FILELIST *flptr, OBJECT *theo,
                                      EMIT_STATE *state, int32 *len,
                                      void *params) ;

struct emit_state {
  int32 reclevel ;     /* Recursion level. Counts down. */
  uint8 *buffer ;      /* Buffer of at least linelimit, provided by caller */
  int32 linelimit ;    /* Maximum length of current line */
  int32 linelen ;      /* Length of current line */
  int32 totallen ;     /* Running total of bytes emitted */
  int32 indent_size ;  /* Size of indentation; 0 for no pretty-printing */
  int32 indent_level ; /* Current indentation depth */
  int32 optline ;      /* Newline before output if prettyprinting  */
  int32 optspace ;     /* Space before output if prettyprinting */
  int32 lastchar ;     /* Previous character output */
  uint32 override ;    /* Bitmask of types to callback for */
  EMIT_EXTRATYPES_PROC extra ; /* callback function */
} ;

/* _char_table and constants from chartype.h */
#define psdelimiter(c) ((int32)(_char_table[(uint8)(c)])&(WHITE_SPACE|SPECIAL_CHAR))

/* ----- Exported functions ----- */

int32 emit_tokens(uint8 *buf , int32 len ,
                  FILELIST *flptr, EMIT_STATE *state) ;

int32 emit_internal(OBJECT *theo, FILELIST *flptr, EMIT_STATE *state,
                    void *params) ;

/*
Log stripped */

#endif /* protection for multiple inclusion */
