
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* dbg_ufst.h */


 /*----------------------------DEBUG.H--------------------------------------
  *  DEBUG.H  This header file contains the statements necessary for the
  *           conditional compilation of debug statements
  *
  *------------------------------------------------------------------------*/

#ifndef __DBG_UFST__
#define __DBG_UFST__

#ifdef AGFADEBUG
#define GENERATE_STATEMENTS(statements) statements
#else
#define GENERATE_STATEMENTS(statements)  ;   
#endif

#define DBG(user_text) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text))

#define DBG1(user_text,a1) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1))

#define DBG2(user_text,a1,a2) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1,a2))

#define DBG3(user_text,a1,a2,a3) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1,a2,a3))

#define DBG4(user_text,a1,a2,a3,a4) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1,a2,a3,a4))

#define DBG5(user_text,a1,a2,a3,a4,a5) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1,a2,a3,a4,a5))

#define DBG6(user_text,a1,a2,a3,a4,a5,a6) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1,a2,a3,a4,a5,a6))

#define DBG8(user_text,a1,a2,a3,a4,a5,a6,a7,a8) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) printf(user_text,a1,a2,a3,a4,a5,a6,a7,a8))
    
#define OUTLINEDBG(user_text) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) fprintf(if_state.outStik, user_text))

#define OUTLINEDBG1(user_text, a1) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) fprintf(if_state.outStik, user_text, a1))

#define OUTLINEDBG2(user_text, a1, a2) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) fprintf(if_state.outStik, user_text, a1, a2))

#define OUTLINEDBG3(user_text, a1, a2, a3) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) fprintf(if_state.outStik, user_text, a1, a2, a3))

#define OUTLINEDBG4(user_text, a1, a2, a3, a4) \
    GENERATE_STATEMENTS(if (UFST_get_debug(FSA0)) fprintf(if_state.outStik, user_text, a1, a2, a3, a4))

#endif	/* __DBG_UFST__	*/

