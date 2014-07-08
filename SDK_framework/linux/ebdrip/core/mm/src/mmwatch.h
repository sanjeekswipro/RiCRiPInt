/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmwatch.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Basic MM watch internal interface
 */

#ifndef __MMWATCH_H__
#define __MMWATCH_H__

/* == Watch Debugging == */

/* This allows the core RIP to "watch" what is going on in the MM.      */
/* This defines some macros used in the 'tag' debugging code, so has    */
/* to come here.                                                        */

#ifdef MM_DEBUG_WATCH

extern mm_debug_watcher_t mm_debug_watcher ;

void mm_watch_init( void ) ;
void mm_watch_finish( void ) ;

#define MM_DEBUG_WATCH_ALLOC(ptr,size,pool,class,seq,file,line)                \
        MACRO_START                                                            \
          if (mm_debug_watcher != NULL)                                        \
            mm_debug_watcher(ptr,0,size,pool,class,                            \
                             seq,file,line,MM_WATCH_ALLOC);                    \
        MACRO_END

#define MM_DEBUG_WATCH_FREE(ptr,size,pool,class,seq,file,line)                 \
        MACRO_START                                                            \
          if (mm_debug_watcher != NULL)                                        \
            mm_debug_watcher(ptr,0,size,pool,class,                            \
                             seq,file,line,MM_WATCH_FREE);                     \
        MACRO_END

#define MM_DEBUG_WATCH_TRUNCATE(ptr,oldsize,newsize,pool,class,seq,file,line)  \
        MACRO_START                                                            \
          if (mm_debug_watcher != NULL)                                        \
            mm_debug_watcher(ptr,oldsize,newsize,pool,class,                   \
                             seq,file,line,MM_WATCH_TRUNCATE);                 \
        MACRO_END

#else

#define mm_watch_init() EMPTY_STATEMENT()
#define mm_watch_finish() EMPTY_STATEMENT()

#define MM_DEBUG_WATCH_ALLOC(ptr,size,pool,class,seq,file,line) EMPTY_STATEMENT()
#define MM_DEBUG_WATCH_FREE(ptr,size,pool,class,seq,file,line) EMPTY_STATEMENT()
#define MM_DEBUG_WATCH_TRUNCATE(ptr,oldsize,newsize,pool,class,seq,file,line) EMPTY_STATEMENT()

#endif /* MM_DEBUG_WATCH */

#endif /* __MMWATCH_H__ */


/* Log stripped */
