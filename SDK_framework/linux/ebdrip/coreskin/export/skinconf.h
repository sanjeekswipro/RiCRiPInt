#ifndef __SKINCONF_H__
#define __SKINCONF_H__

/*
 * $HopeName: SWcoreskin!export:skinconf.h(EBDSDK_P.1) $
 *
 * This file acts as central point for setting feature defines for all
 * products built on top of the coreskin. The product dependent skins
 * cannot override these settings. They must use methods other than
 * conditional compilation to achieve the specialization required for
 * their product, eg FwObserv. The aim is to make the coreskin and the
 * layers below it product independent. Conditional compilation for
 * coreskin and below is allowed for other dimensions eg: single /
 * multi processing.
 *
* Log stripped */

/* Dont include coreskin.h as must consist of simply #defines so that can be
 * include by resource files etc.
 */


/* CAN_FIND_MEMORY_SIZE indicates whether SW knows how to find the RAM
 * size.  A value of 1 means it finds the size of installed RAM; a value
 * of 2 means it finds the available RAM. */

/* There are three alternatives for memory allocation:
 *
 * MUST_ALLOC_RIPMEMORY indicates the skin must allocate the memory for
 * core, and pass it down.  In SMP, this is shared memory.
 * MUST_USE_SHARED_ARENA indicates the skin must ask the core to use a
 * shared arena, and transmit its details to renderer processes.
 * MUST_USE_VM_ARENA indicates the skin must ask the core to use VM. */

/* PLATFORM_HAS_VM indicates whether there are any configurations for
 * this platform that use VM (even if the one being built doesn't).
 * This is needed to ensure compatibility of the configuration file. */

/* -------------------------- Windows -------------------------------------- */

#if defined(WIN32) || defined(IBMPC) || defined(RC_INVOKED)

/* --- need to be provided by all platforms, alphabetically please ! --- */
#define BLOCK_PAGEBUFFER_WRITES          (128*1024)
#define CAN_FIND_MEMORY_SIZE 1
#undef  GET_DATA_FROM_RESOURCES

#define MUST_USE_VM_ARENA
#define PLATFORM_HAS_VM
#define NEEDS_SWMEMORY

#define STANDARD_MINMEMORYFORSYS_DEFAULT (4 * 1024) /* special cases follow */
#define      AXP_MINMEMORYFORSYS_DEFAULT (24 * 1024)
#define  DOS_WIN_MINMEMORYFORSYS_DEFAULT 1536
#define       NT_MINMEMORYFORSYS_DEFAULT (28 * 1024)

#define SENTINEL

/* --- this platform only, alphabetically please ! --- */

#endif  /* WIN32 || IBMPC || RC_INVOKED */

/* -------------------------- Unix ----------------------------------------- */

#ifdef  UNIX

/* --- need to be provided by all platforms, alphabetically please ! --- */
#undef  BLOCK_PAGEBUFFER_WRITES
#if defined(linux) || defined(Solaris) || defined(SGI)
#define CAN_FIND_MEMORY_SIZE 1
#else
#undef  CAN_FIND_MEMORY_SIZE
#endif
#undef  GET_DATA_FROM_RESOURCES

#define MUST_USE_VM_ARENA
#define PLATFORM_HAS_VM
#define NEEDS_SWMEMORY
#define STANDARD_MINMEMORYFORSYS_DEFAULT 512

#ifdef linux
/* Only have Sentinel SuperPro support for Linux */
#define SENTINEL
#else
/* And only MICROPHAR for all other UNIX platforms */
#define MICROPHAR
#endif

/* --- this platform only, alphabetically please ! --- */

#endif  /* UNIX */


/* -------------------------- Macintosh ------------------------------------ */

#ifdef  MACINTOSH

/* --- need to be provided by all platforms, alphabetically please ! --- */
#define BLOCK_PAGEBUFFER_WRITES          (128*1024)
#define CAN_FIND_MEMORY_SIZE 1

#define GET_DATA_FROM_RESOURCES


#define MUST_USE_VM_ARENA
#define PLATFORM_HAS_VM
#define NEEDS_SWMEMORY


#define STANDARD_MINMEMORYFORSYS_DEFAULT (5 * 1024)

/* dongle selection */
#define SENTINEL 1

/* --- this platform only, alphabetically please ! --- */

#endif  /* MACINTOSH */


#endif /* ! __SKINCONF_H__ */

/* eof skinconf.h */
