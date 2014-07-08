/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!src:hqosarch.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A classification scheme for platforms.
 */

#include <string.h>

#if !defined(VXWORKS) && !defined(__CC_ARM)
#include <memory.h>
#endif

#include "std.h"
#include "hqosarch.h"

typedef struct NODE {
        char name[OSARCH_NAMESIZE];
        struct NODE **links;
        int index;
} NODE;

typedef struct NSTRING {
        char *str;
        int   len;
} NSTRING;

typedef struct PLATFORM {
        NSTRING os;
        NSTRING arch;
} PLATFORM;

/*
 * Hide the details of declaring the compatibility lists.
 * The main reason for this is not convenience or aesthetics,
 * but to help with visual consistency checking.
 */
#define DECNODE(type,name)       NODE *links_##type##_##name[] = {
#define ENDNODE(type,index,name) 0}; NODE type##_##name[1] = {#name, links_##type##_##name, index};

static char all[] = "all";

/*
 * Compatibility information for operating systems.
 *
 * The index numbers here must correspond exactly to the order in which
 * nodes appear in list_os. It would be safest to add new nodes at the end.
 */
DECNODE(os,all)                                      ENDNODE(os, 0,all)
DECNODE(os,pc)             os_all,                   ENDNODE(os, 1,pc)
DECNODE(os,dos)            os_pc,                    ENDNODE(os, 2,dos)
DECNODE(os,win)            os_dos,                   ENDNODE(os, 3,win)
DECNODE(os,win_32)         os_win,                   ENDNODE(os, 4,win_32)
DECNODE(os,win_95)         os_win_32,                ENDNODE(os, 5,win_95)
DECNODE(os,win_nt)         os_win_32,                ENDNODE(os, 6,win_nt)
DECNODE(os,win_nt_3)       os_win_nt,                ENDNODE(os, 7,win_nt_3)
DECNODE(os,win_nt_4)       os_win_nt_3,              ENDNODE(os, 8,win_nt_4)
DECNODE(os,win_nt_5)       os_win_nt_4,              ENDNODE(os, 9,win_nt_5)
DECNODE(os,unix)           os_all,                   ENDNODE(os,10,unix)
DECNODE(os,solaris)        os_unix,                  ENDNODE(os,11,solaris)
DECNODE(os,solaris_2)      os_solaris,               ENDNODE(os,12,solaris_2)
DECNODE(os,solaris_2_5)    os_solaris_2,             ENDNODE(os,13,solaris_2_5)
DECNODE(os,solaris_2_6)    os_solaris_2_5,           ENDNODE(os,14,solaris_2_6)
DECNODE(os,sunos)          os_unix,                  ENDNODE(os,15,sunos)
DECNODE(os,irix)           os_unix,                  ENDNODE(os,16,irix)
DECNODE(os,mac)            os_all,                   ENDNODE(os,17,mac)
DECNODE(os,macos)          os_mac,                   ENDNODE(os,18,macos)
DECNODE(os,macos_8)        os_macos,                 ENDNODE(os,19,macos_8)
DECNODE(os,irix_6)         os_irix,                  ENDNODE(os,20,irix_6)
DECNODE(os,irix_6_3)       os_irix_6,                ENDNODE(os,21,irix_6_3)
DECNODE(os,irix_6_4)       os_irix_6_3,              ENDNODE(os,22,irix_6_4)
DECNODE(os,irix_6_5)       os_irix_6_4,              ENDNODE(os,23,irix_6_5)
DECNODE(os,macos_9)        os_macos_8,               ENDNODE(os,24,macos_9)
DECNODE(os,linux)          os_unix,                  ENDNODE(os,25,linux)
  /* quick hack: */
DECNODE(os,linux_2)        os_linux,                 ENDNODE(os,26,linux_2)
DECNODE(os,win_98)         os_win_95,                ENDNODE(os,27,win_98)
DECNODE(os,macos_x)        os_macos,                 ENDNODE(os,28,macos_x)
DECNODE(os,win_me)         os_win_98,                ENDNODE(os,29,win_me)
DECNODE(os,win_nt_5_1)     os_win_nt_5,              ENDNODE(os,30,win_nt_5_1)
DECNODE(os,win_nt_5_2)     os_win_nt_5_1,            ENDNODE(os,31,win_nt_5_2)
DECNODE(os,win_nt_6)       os_win_nt_5_2,            ENDNODE(os,32,win_nt_6)
DECNODE(os,win_nt_6_1)     os_win_nt_6,              ENDNODE(os,33,win_nt_6_1)
DECNODE(os,win_64)         os_win,                   ENDNODE(os,34,win_64)
DECNODE(os,win_64_6)       os_win_64,                ENDNODE(os,35,win_64_6)
DECNODE(os,win_64_6_1)     os_win_64_6,              ENDNODE(os,36,win_64_6_1)


NODE *list_os[] = {
        os_all,
        os_pc,
        os_dos,
        os_win,
        os_win_32,
        os_win_95,
        os_win_nt,
        os_win_nt_3,
        os_win_nt_4,
        os_win_nt_5,
        os_unix,
        os_solaris,
        os_solaris_2,
        os_solaris_2_5,
        os_solaris_2_6,
        os_sunos,
        os_irix,
        os_mac,
        os_macos,
        os_macos_8,
        os_irix_6,
        os_irix_6_3,
        os_irix_6_4,
        os_irix_6_5,
        os_macos_9,
        os_linux,
        os_linux_2,
        os_win_98,
        os_macos_x,
        os_win_me,
        os_win_nt_5_1,
        os_win_nt_5_2,
        os_win_nt_6,
        os_win_nt_6_1,
        os_win_64,
        os_win_64_6,
        os_win_64_6_1,
        0
};
#define OS_COUNT (sizeof(list_os)/sizeof(*list_os) - 1)

/*
 * Compatibility information for architectures.
 *
 * The mips r5000, r8000 and r10000 are all considered equivalent,
 * hence the forward declaration and circular links below.
 *
 * The index numbers here must correspond exactly to the order in which
 * nodes appear in list_arch. It would be safest to add new nodes at the end.
 */
extern NODE arch_mips_r10000[];

DECNODE(arch,all)                                       ENDNODE(arch, 0,all)
DECNODE(arch,x86)          arch_all,                    ENDNODE(arch, 1,x86)
DECNODE(arch,386)          arch_x86,                    ENDNODE(arch, 2,386)
DECNODE(arch,486)          arch_386,                    ENDNODE(arch, 3,486)
DECNODE(arch,486_sx)       arch_486,                    ENDNODE(arch, 4,486_sx)
DECNODE(arch,486_dx)       arch_486_sx,                 ENDNODE(arch, 5,486_dx)
DECNODE(arch,pentium)      arch_486,                    ENDNODE(arch, 6,pentium)
DECNODE(arch,mips)         arch_all,                    ENDNODE(arch, 7,mips)
DECNODE(arch,mips_r4000)   arch_mips,                   ENDNODE(arch, 8,mips_r4000)
DECNODE(arch,mips_r5000)   arch_mips, arch_mips_r10000, ENDNODE(arch, 9,mips_r5000)
DECNODE(arch,mips_r8000)   arch_mips_r5000,             ENDNODE(arch,10,mips_r8000)
DECNODE(arch,mips_r10000)  arch_mips_r8000,             ENDNODE(arch,11,mips_r10000)
DECNODE(arch,ppc)          arch_all,                    ENDNODE(arch,17,ppc)
DECNODE(arch,601)          arch_ppc,                    ENDNODE(arch,12,601)
DECNODE(arch,603)          arch_601,                    ENDNODE(arch,13,603)
DECNODE(arch,604)          arch_603,                    ENDNODE(arch,14,604)
DECNODE(arch,g3)           arch_604,                    ENDNODE(arch,15,g3)
DECNODE(arch,x704)         arch_g3,                     ENDNODE(arch,16,x704)
DECNODE(arch,sparc)        arch_all,                    ENDNODE(arch,18,sparc)
DECNODE(arch,g4)           arch_x704,                   ENDNODE(arch,19,g4)
DECNODE(arch,ub)           arch_all,                    ENDNODE(arch,20,ub)
DECNODE(arch,ub_ppc)       arch_ub, arch_ppc,           ENDNODE(arch,21,ub_ppc)
DECNODE(arch,ub_386)       arch_ub, arch_386,           ENDNODE(arch,22,ub_386)
/* 64 bit CPU types. */
DECNODE(arch,gen64)        arch_all,                    ENDNODE(arch,23,gen64)
DECNODE(arch,amd64)        arch_gen64,                  ENDNODE(arch,24,amd64)
DECNODE(arch,itanium)      arch_gen64,                  ENDNODE(arch,25,itanium)
DECNODE(arch,pentium_64)   arch_amd64,                  ENDNODE(arch,26,pentium_64)
/* Various amd 64 bit CPU models */
DECNODE(arch,opteron)      arch_amd64,                  ENDNODE(arch,27,opteron)
DECNODE(arch,turon)        arch_amd64,                  ENDNODE(arch,28,turon)
DECNODE(arch,athlon)       arch_amd64,                  ENDNODE(arch,29,athlon)
/* Various intel 64 bit CPU models */
DECNODE(arch,xeon_64)      arch_pentium_64,             ENDNODE(arch,30,xeon_64)

NODE *list_arch[] = {
/*  0 */ arch_all,
         arch_x86,
         arch_386,
         arch_486,
         arch_486_sx,
/*  5 */ arch_486_dx,
         arch_pentium,
         arch_mips,
         arch_mips_r4000,
         arch_mips_r5000,
/* 10 */ arch_mips_r8000,
         arch_mips_r10000,
         arch_601,
         arch_603,
         arch_604,
/* 15 */ arch_g3,
         arch_x704,
         arch_ppc,
         arch_sparc,
         arch_g4,
/* 20 */ arch_ub,
         arch_ub_ppc,
         arch_ub_386,
         arch_gen64,
         arch_amd64,
/* 25 */ arch_itanium,
         arch_pentium_64,
         arch_opteron,
         arch_turon,
         arch_athlon,
/* 30 */ arch_xeon_64,
         0
};
#define ARCH_COUNT (sizeof(list_arch)/sizeof(*list_arch) - 1)

#define MAX_COUNT (OS_COUNT > ARCH_COUNT ? OS_COUNT : ARCH_COUNT)

static int
str_eq_nstring(char *str, NSTRING nstring)
{
        return strncmp(str, nstring.str, nstring.len) == 0 && str[nstring.len] == '\0';
}

static NODE *
find(NSTRING name, NODE **np)
{
        while (*np && !str_eq_nstring((*np)->name, name))
                np++;
        return *np;
}

static int
compatible_rec(NODE *n, NSTRING name, char *visited)
{
        int i;

        if (visited[n->index])
                return 0;
        visited[n->index] = 1;
        if (str_eq_nstring(n->name, name))
                return 1;
        for (i = 0; n->links[i]; i++)
                if (compatible_rec(n->links[i], name, visited))
                        return 1;
        return 0;
}

static int
compatible(NSTRING name1, NSTRING name2, NODE **list, char *visited, size_t list_len)
{
        while (name1.len > 0) {
                NODE *n = find(name1, list);
                if (n) {
                        memset(visited, 0, list_len);
                        return compatible_rec(n, name2, visited);
                }
                do {
                        name1.len--;
                } while (name1.len > 0 && name1.str[name1.len] != VERSION_SEPARATOR);
        }
        return 0;
}

static void
parse_platform(char *string, PLATFORM *p)
{
        char *cp = strrchr(string, COMPONENT_SEPARATOR);
        p->os.str = string;
        if (cp) {
                p->os.len = (int32)((ptrdiff_t)(cp - string));
                p->arch.str = cp+1;
        } else {
                p->os.len = strlen_int32(string);
                p->arch.str = all;
        }
        p->arch.len = strlen_int32(p->arch.str);
}

int
platform_includes(char *general, char *specific)
{
        char visited[MAX_COUNT];
        PLATFORM gen, spec;

        parse_platform(general,  &gen);
        parse_platform(specific, &spec);

        return compatible(spec.os,   gen.os,   list_os,   visited, OS_COUNT)
            && compatible(spec.arch, gen.arch, list_arch, visited, ARCH_COUNT);
}

int
platform_overlaps(char *platform1, char *platform2)
{
        char vis[MAX_COUNT];
        PLATFORM g, s;

        parse_platform(platform1, &g);
        parse_platform(platform2, &s);

        return  (compatible(s.os,   g.os,   list_os,   vis, OS_COUNT)   || compatible(g.os,   s.os,   list_os,   vis, OS_COUNT))
        &&      (compatible(s.arch, g.arch, list_arch, vis, ARCH_COUNT) || compatible(g.arch, s.arch, list_arch, vis, ARCH_COUNT));
}

int
platform_included(char *specific, char *general)
{
        return platform_includes(general, specific);
}

int
platform_identical(char *platform1, char *platform2)
{
        return strcmp(platform1, platform2) == 0;
}

int
platform_different(char *platform1, char *platform2)
{
        return strcmp(platform1, platform2) != 0;
}

/*
* Log stripped */
