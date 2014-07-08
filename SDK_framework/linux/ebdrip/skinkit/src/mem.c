/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:mem.c(EBDSDK_P.1) $
 * Memory related utility functions
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Memory-related utility functions.
 */

#include <stdlib.h>
#include <string.h>    /* memset */

#include "mem.h"
#include "mpscmvff.h"
#include "mpsavm.h"
#include "mpsacl.h"
#include "skinkit.h"
#include "mps.h"
#include "mpslib.h"

/**
 * \brief The default maximum address space size.
 */
#ifdef PLATFORM_IS_64BIT
  /* We will assume that 128 GiB is enough virtual address space for
     now on 64bit platforms. Note that reserving virtual address space
     has a memory cost which is not insignificant so we don't allow
     more. */
#define MAX_ADDRESS_SPACE_SIZE_IN_BYTES ((size_t)128 * (size_t)1024 * (size_t)1024 * (size_t)1024)
#else
  /* Just under 2 GiB. Allow some space for other memory
     allocations. */
#define OTHER_ALLOCATIONS_IN_BYTES ((size_t)50 * (size_t)1024 * (size_t)1024)

#define MAX_ADDRESS_SPACE_SIZE_IN_BYTES (((size_t)2024 * (size_t)1024 * (size_t)1024) - OTHER_ALLOCATIONS_IN_BYTES)
#endif

/**
 * \brief The maximum amount of memory, in bytes, to be used by the
 * core RIP in cases where the physical RAM configuration cannot be
 * determined.
 */
#if defined(PLATFORM_IS_64BIT)
#  define RIP_DEFAULT_MEMORY_IN_BYTES 2000 * 1024 * 1024
#else
#  define RIP_DEFAULT_MEMORY_IN_BYTES 80 * 1024 * 1024
#endif

/**
 * \brief Each block allocated needs to remember its size, so we add
 * a header to keep it in.  HEADER_BLOCK and BLOCK_HEADER translate
 * between a pointer to the header and a pointer to the block (what
 * the caller of malloc gets).
 */

typedef struct header_s {
  size_t size;
} header_s;

#define HEADER_SIZE MemAllocAlign(sizeof(header_s))
#define HEADER_BLOCK(header) \
  ((void *)((char *)(header) + HEADER_SIZE))
#define BLOCK_HEADER(block) \
  ((header_s *)((char *)(block) - HEADER_SIZE))


typedef struct mem_header_debug_s {
  mps_debug_info_s info;
  size_t seqNo;
} mem_header_debug_s;

#ifdef DEBUG_BUILD
/* Additional structure to track MemAlloc() allocations */
typedef struct mem_pool_debug_s {
  size_t seqNo;
  size_t totalAllocs;
  size_t maxAllocs;
} mem_pool_debug_s;

mem_pool_debug_s skin_pool_debug = { 0, 0, 0 };

static mps_pool_debug_option_s poolDebugOptions =
  { "KEEP OFF", 8, "DEADDEAD", 8, TRUE, sizeof(mem_header_debug_s) };

#endif


/* To emulate EPDL or EPDR-like behaviour using mvff */
#define EPDL_LIKE ( mps_bool_t )0, ( mps_bool_t )0, ( mps_bool_t )1
#define EPDR_LIKE ( mps_bool_t )1, ( mps_bool_t )1, ( mps_bool_t )1


/**
 * \brief The single MPS arena used by both core and skin.
 */
static mps_arena_t mem_arena = NULL ;

/**
 * \brief The memory pool for use in skin.
 */
static mps_pool_t  skin_pool = NULL ;
static HqBool      fMemInitialised = FALSE ;

static void * RIPCALL DefaultSysAlloc(size_t cbSize);
static void   RIPCALL DefaultSysFree(void * ptr);

static SysAllocFn   * gSysAllocFn = DefaultSysAlloc;
static SysFreeFn    * gSysFreeFn = DefaultSysFree;

static mps_word_t mem_skinpool_label = 0 ;


#ifdef MEM_LOCATION


typedef struct {
  char *location; /* NULL means unused */
  mps_word_t label;
} mem_location_entry_t;

#define MEM_LOCATIONS_COUNT (999)

/* A closed hash table to store labels for each location. */
static mem_location_entry_t mem_locations[MEM_LOCATIONS_COUNT];


static void mem_location_init(void)
{
  size_t i;
  mem_location_entry_t init = { 0 } ;

  mem_skinpool_label = mps_telemetry_intern("SKINKIT");
  for (i = 0; i < sizeof(mem_locations)/sizeof(mem_locations[0]); i++)
    mem_locations[i] = init;
}


static mps_word_t mem_location_label( char *location )
{
  size_t hash, i;

  hash = (((size_t)location >> 2) ^ ((size_t)location << 6))
         % MEM_LOCATIONS_COUNT;
  i = hash;
  do {
    if (mem_locations[i].location == location)
      return mem_locations[i].label;
    if (mem_locations[i].location == NULL) {
      mem_locations[i].location = location;
      mem_locations[i].label = mps_telemetry_intern(location);
      return mem_locations[i].label;
    }
    i++; if (i == MEM_LOCATIONS_COUNT) i = 0;
  } while (i != hash);
  /* Didn't find it, and no free slots, give up and just make a new one. */
  return mps_telemetry_intern(location);
}


char *mem_label_location(mps_word_t label)
{
  size_t i;

  for ( i = 0 ; i < MEM_LOCATIONS_COUNT ; ++i )
    if ( mem_locations[i].label == label )
      return mem_locations[i].location;
  /* Not found, probably overflow. To get the location, use the MPS log, or
     increase MEM_LOCATIONS_COUNT. */
  return NULL;
}


#else /* !MEM_LOCATION */


static void mem_location_init(void)
{
  mem_skinpool_label = mps_telemetry_intern("SKINKIT");
}


#endif /* !MEM_LOCATION */


void MemLogInit(const char *mps_log, unsigned long mps_telemetry)
{
  if (! fMemInitialised) {
    mps_lib_telemetry_defaults(mps_log, mps_telemetry) ;
  }
}

size_t MemInit(size_t *RIP_maxAddressSpaceInBytes,
               size_t RIP_workingSizeInBytes,
               void * pMemory, SysMemFns * pSysMemFns)
{
  HQASSERT(RIP_DEFAULT_MEMORY_IN_BYTES < MAX_ADDRESS_SPACE_SIZE_IN_BYTES,
           "RIP default memory too large");
  HQASSERT(RIP_maxAddressSpaceInBytes != NULL,
           "Nowhere for max address space size");

  if (! fMemInitialised) {
    size_t maxAddressSpaceInBytes = 0;
    mps_res_t res;

    /* We'd like to do this in mpslib, but the core-side probe handler is only
       set up when the core is initialised. */
    SwLeProbe(SW_TRACE_MPS_COMMITTED, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY) ;

    /* If pre-allocated block is supplied, RIP_workingSizeInBytes must denote
       its size. It cannot be zero, because that will cause us to calculate a
       default size, which will not be correct. */
    if ( pMemory != NULL ) {
      HQASSERT(RIP_workingSizeInBytes != 0 , "RIP working size invalid");
      /*
       * If a block of memory has been supplied by the caller, then use the
       * client arena running in that fixed block.
       */
      res = mps_arena_create( &mem_arena, mps_arena_class_cl(), RIP_workingSizeInBytes, pMemory );

    } else {
      /* ------ Determine the maximum RIP address space. ------ */
      uint32 physical_memory = GetPhysicalRAMSizeInMegabytes();

      if (physical_memory != 0) {
        maxAddressSpaceInBytes = (size_t)physical_memory * 1024 * 1024;

        /* The OS can tell us how much total virtual memory exists but
           we limit this to the actual physical memory on the machine
           * 2 on 64bit platforms. We do not want to reserve all the
           virtual memory on 64bit platforms because the amount of RAM
           required for the page tables gets very large when reserving
           8 TB+ (numerous GB of page tables gets commited exhausting
           much of the available commitable memory). Because our MPS
           uses high and low address spaces to avoid fragmentation at
           the arena level, on 64bit platforms we currently return 2 x
           the physical memory. */
#if defined(PLATFORM_IS_64BIT)
        maxAddressSpaceInBytes *= 2;
        if ( maxAddressSpaceInBytes > MAX_ADDRESS_SPACE_SIZE_IN_BYTES ) {
          maxAddressSpaceInBytes = MAX_ADDRESS_SPACE_SIZE_IN_BYTES;
        }
#else
        /* On 32bit platforms we should just use the 2 GiB
           available. It has little cost and helps avoid fragmentation
           in the MPS. */
        maxAddressSpaceInBytes = MAX_ADDRESS_SPACE_SIZE_IN_BYTES;
#endif
      } else {
        maxAddressSpaceInBytes = MAX_ADDRESS_SPACE_SIZE_IN_BYTES;
      }

      /* ------ Determine the max RIP memory. ------ */

      /* Otherwise, configure the RIP to manage its own memory
         allocation. */
      if ( RIP_workingSizeInBytes == 0 ) {
        RIP_workingSizeInBytes = (size_t)physical_memory * 1024 * 1024;
      }

      /* If not specified and not able to determine system memory
         installed. */
      if ( RIP_workingSizeInBytes == 0 ) {
        RIP_workingSizeInBytes = RIP_DEFAULT_MEMORY_IN_BYTES;
      }

      if (RIP_workingSizeInBytes > MAX_ADDRESS_SPACE_SIZE_IN_BYTES) {
        RIP_workingSizeInBytes = MAX_ADDRESS_SPACE_SIZE_IN_BYTES;
      }

      /* Make sure we have enough address space required for the work
         size. */
      if (maxAddressSpaceInBytes < RIP_workingSizeInBytes) {
        maxAddressSpaceInBytes = RIP_workingSizeInBytes;
#if defined(PLATFORM_IS_64BIT)
        maxAddressSpaceInBytes *= 2;
#endif
        if ( maxAddressSpaceInBytes > MAX_ADDRESS_SPACE_SIZE_IN_BYTES ) {
          maxAddressSpaceInBytes = MAX_ADDRESS_SPACE_SIZE_IN_BYTES;
        }
      }

      res = mps_arena_create( &mem_arena, mps_arena_class_vmnz(),
                              maxAddressSpaceInBytes );
    }

    if ( res != MPS_RES_OK ) {
      SkinExit( 1, (uint8 *)"Could not create memory arena" );
    }

    mem_location_init() ;

    /* Create the skin pool in this arena */
    res = mps_pool_create(&skin_pool, mem_arena,
#ifndef DEBUG_BUILD
                          mps_class_mvff(),
#else
                          mps_class_mvff_debug(), &poolDebugOptions,
#endif
                          (size_t)65536, (size_t)32, (size_t)8, EPDR_LIKE);
    if ( res != MPS_RES_OK ) {
      SkinExit(1, (uint8 *)"Could not create skin memory pool." );
    }

    mps_telemetry_label((mps_addr_t)skin_pool, mem_skinpool_label);

#ifdef DEBUG_BUILD
    skin_pool_debug.seqNo = 0;
    skin_pool_debug.totalAllocs = 0;
    skin_pool_debug.maxAllocs = 0;
#endif

    if ( pSysMemFns != NULL ) {
      gSysAllocFn = pSysMemFns->pAllocFn;
      gSysFreeFn = pSysMemFns->pFreeFn;
    }

    fMemInitialised = TRUE;
    *RIP_maxAddressSpaceInBytes = maxAddressSpaceInBytes;
  }

  return RIP_workingSizeInBytes;
}


#ifdef DEBUG_BUILD

static void MemReport(mps_addr_t *addr, size_t size, mps_fmt_t fmt,
                      mps_pool_t pool, mps_debug_info_s *info, void *p)
{
  mem_header_debug_s *tag = (mem_header_debug_s *)info;
  char *location;

  UNUSED_PARAM(mps_fmt_t, fmt); UNUSED_PARAM(mps_pool_t, pool);
  UNUSED_PARAM(void*, p);
  SkinMonitorf("%p: %7lu bytes %c %-33s %-16s at %p seq=%06lu, ",
               HEADER_BLOCK(addr), size - HEADER_SIZE,
               'L', "SKIN", "SKIN_POOL_TYPE",
               skin_pool, tag->seqNo);
  if ( (location = mem_label_location(info->location)) != NULL )
    SkinMonitorf("%s\n", location);
  else
    SkinMonitorf("label%lu\n", (unsigned long)info->location);
}

#endif


void MemFinish(int32 fError)
{
  if (fMemInitialised)
  {
    if (skin_pool != NULL)
    {
#ifdef DEBUG_BUILD
      size_t managed_size, free_size;

      mps_pool_size(skin_pool, &managed_size, &free_size);
      /* Report any outstanding allocations */
      if (managed_size - free_size > 0)
      {
        mps_pool_debug_walk(skin_pool, MemReport, NULL);
        SkinMonitorf("MemFinish: %lu bytes allocated, %lu left\n",
                     (unsigned long)skin_pool_debug.totalAllocs,
                     managed_size - free_size);
      }
#endif
      mps_pool_destroy( skin_pool );
      skin_pool = NULL ;
    }
    if (mem_arena != NULL)
    {
      if ( fError != 0 )
        mps_arena_abort( mem_arena );
      else
        mps_arena_destroy( mem_arena );
      mem_arena = NULL ;
    }

    gSysAllocFn = DefaultSysAlloc;
    gSysFreeFn = DefaultSysFree;

    fMemInitialised = FALSE;
  }
}


#ifndef MEM_LOCATION
void * RIPCALL MemAlloc(size_t cbSize, int32 fZero, int32 fExitOnFail)
#else
void * RIPCALL MemAllocDebug(size_t cbSize, int32 fZero, int32 fExitOnFail, char * location)
#endif
{
  HQASSERT(fMemInitialised, "Memory system initialised");

  /* "size + HEADER_SIZE <= size": .header.overflow:
   * request cannot be satisfied.
   */
  if (cbSize != 0 && (cbSize + HEADER_SIZE > cbSize))
  {
    void * p;
    mps_res_t res;
#ifdef MEM_LOCATION
    mem_header_debug_s tag;
    tag.info.location = mem_location_label(location);
    tag.info.mps_class = mem_skinpool_label ;
#ifdef DEBUG_BUILD
    tag.seqNo = skin_pool_debug.seqNo++; /* not thread-safe */
#endif
    res = mps_alloc_debug(&p, skin_pool, cbSize + HEADER_SIZE, &tag.info);
#else
    res = mps_alloc(&p, skin_pool, cbSize + HEADER_SIZE);
#endif
    if (res == MPS_RES_OK)
    {
      header_s *header;
      header = p;
      header->size = cbSize + HEADER_SIZE;
      if (fZero)
        memset(HEADER_BLOCK(header), 0, cbSize);
#ifdef DEBUG_BUILD
      /* The alloc counters are not thread-safe, but we don't care. */
      skin_pool_debug.totalAllocs += cbSize;
      if (skin_pool_debug.maxAllocs < skin_pool_debug.totalAllocs)
        skin_pool_debug.maxAllocs = skin_pool_debug.totalAllocs;
#endif
      return HEADER_BLOCK(header) ;
    }
  }

  if (fExitOnFail)
  {
#ifdef MEM_LOCATION
    SkinMonitorf("Failed to allocate %u bytes at %s\n", cbSize, location);
#else
    SkinMonitorf("Failed to allocate %u bytes\n", cbSize);
#endif
    SkinExit(1, (uint8 *)"Out of memory.");
  }
  return NULL;
}


#ifndef MEM_LOCATION
void * RIPCALL MemRealloc(void *ptr, size_t cbSize)
#else
void * RIPCALL MemReallocDebug(void *ptr, size_t cbSize, char * location)
#endif
{
  header_s *header;
  size_t oldSize;

  HQASSERT(fMemInitialised, "Memory system initialised");

  if(ptr == NULL)
#ifndef MEM_LOCATION
    return MemAlloc(cbSize, FALSE, FALSE);
#else
    return MemAllocDebug(cbSize, FALSE, FALSE, location);
#endif

  /* Get the size and check that it's not a free block. */
  header = BLOCK_HEADER(ptr);
  oldSize = header->size;
  HQASSERT(oldSize != 0, "Memory header size invalid");

  if(cbSize == oldSize - HEADER_SIZE)
    return ptr;

  if(cbSize != 0) {
    void *newBlock;

#ifndef MEM_LOCATION
    newBlock = MemAlloc(cbSize, FALSE, FALSE);
#else
    newBlock = MemAllocDebug(cbSize, FALSE, FALSE, location);
#endif
    if(newBlock != NULL) {
      memcpy(newBlock, ptr, min(oldSize - HEADER_SIZE, cbSize));
      MemFree(ptr);
    }
    return newBlock;
  } else {
    MemFree(ptr);
    return NULL;
  }
}


void RIPCALL MemFree(void * ptr)
{
  size_t size;
  header_s *header;

  HQASSERT(fMemInitialised, "Memory system not initialised");

  if(ptr == NULL)
    return;

  header = BLOCK_HEADER(ptr);
  size = header->size;

  /* detect double frees */
  HQASSERT(size != 0, "Free size zero");
  header->size = 0;

#ifdef DEBUG_BUILD
  skin_pool_debug.totalAllocs -= (size - HEADER_SIZE);
#endif

  mps_free(skin_pool, header, size);
}


static void * RIPCALL DefaultSysAlloc(size_t cbSize)
{
  return malloc(cbSize);
}

static void RIPCALL DefaultSysFree(void * ptr)
{
  free(ptr);
}

void * RIPCALL SysAlloc(size_t cbSize)
{
  HQASSERT(fMemInitialised, "Memory system not initialised");

  return (gSysAllocFn)(cbSize);
}

void RIPCALL SysFree(void * ptr)
{
  HQASSERT(fMemInitialised, "Memory system not initialised");

  if(ptr == NULL)
    return;

  (gSysFreeFn)(ptr);
}

mps_arena_t MemGetArena(void)
{
  return mem_arena ;
}

