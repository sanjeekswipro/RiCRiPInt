/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_checksum!genkey.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 */

#ifndef __GENKEY_H__
#define __GENKEY_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Enumeration of the security features protected by this password
 * mechanism.
 */
#define GNKEY_BASE                1
#define GNKEY_FEATURE_HDS         1
#define GNKEY_FEATURE_HMS         2
#define GNKEY_FEATURE_HCS         3
#define GNKEY_FEATURE_HCMS        4
#define GNKEY_FEATURE_CAL         5
#define GNKEY_REVISION_PASSWORD   6     /* For major version upgrades prior to v6 */
#define GNKEY_FEATURE_IDLOM       7
#define GNKEY_FEATURE_CP2RSH      8
#define GNKEY_FEATURE_EASYTRAP    9
#define GNKEY_FEATURE_TRAP_PRO_LITE  GNKEY_FEATURE_EASYTRAP  /* EasyTrap / TrapWorks / Trap Pro-Lite all need same password */
#define GNKEY_FEATURE_TIFF_IT     10
#define GNKEY_FEATURE_HDSLOWRES   11
#define GNKEY_FEATURE_SUBFEATURE  12
#define GNKEY_FEATURE_CHCS8452    13
#define GNKEY_DEVFEATURE_HCP      14    /* Was ..._FEATURE_... */
#define GNKEY_DEVFEATURE_HCMSSE   15    /* Was ..._FEATURE_... */
#define GNKEY_FEATURE_ICC         16
#define GNKEY_FEATURE_COLOR_PRO   GNKEY_FEATURE_ICC  /* HIPP has been rebadged as Color Pro */
#define GNKEY_FEATURE_HCMS_LITE   17
#define GNKEY_FEATURE_2THREAD     18
#define GNKEY_FEATURE_PLEX        19
#define GNKEY_DEVFEATURE_ICC      20    /* Was ..._FEATURE_ICCS */
#define GNKEY_DEVFEATURE_HCMS     21
#define GNKEY_DEVFEATURE_HCMSLITE 22
#define GNKEY_DEVFEATURE_HSCSX    23
#define GNKEY_FEATURE_TIFF6       24
#define GNKEY_FEATURE_POSTSCRIPT  25
#define GNKEY_FEATURE_PDF         26
#define GNKEY_FEATURE_PDFOUT      27
#define GNKEY_FEATURE_HCEP        28
#define GNKEY_DEVFEATURE_HCEP     29
#define GNKEY_FEATURE_MEDIASAVING 30
#define GNKEY_FEATURE_TOOTHPICK   31
#define GNKEY_FEATURE_MPSOUTLINE  32
#define GNKEY_REV_6_PASSWORD      33
#define GNKEY_FEATURE_TRAP_PRO    34
#define GNKEY_PLATFORM_PASSWORD   35
#define GNKEY_FEATURE_HPS         36
#define GNKEY_FEATURE_SIMPLE_IMPOSITION 37
#define GNKEY_REV_7_PASSWORD      38
#define GNKEY_FEATURE_HXM         39
#define GNKEY_FEATURE_XPS         40
#define GNKEY_FEATURE_APPLY_WATERMARK 41
#define GNKEY_FEATURE_CORE_MODULE     42
#define GNKEY_REV_8_PASSWORD      43
#define GNKEY_REV_9_PASSWORD      44
#define GNKEY_REV_TW2_PASSWORD    45
#define GNKEY_FEATURE_MTC         46
#define GNKEY_FEATURE_HXMLOWRES   47
#define GNKEY_FEATURE_MAX_THREADS_LIMIT 48
#define GNKEY_FEATURE_PIPELINING  49
#define GNKEY_FEATURE_64_BIT      50
#define GNKEY_REV_MR3_PASSWORD    51
#define GNKEY_FEATURE_HVD_EXTERNAL 52
#define GNKEY_FEATURE_HVD_INTERNAL 53
#define GNKEY_FEATURE_32_BIT      54
#define GNKEY_REV_MR4_32_PASSWORD 55
#define GNKEY_REV_MR4_64_PASSWORD 56
#define GNKEY_MAX                 57

/* Miramar is outside the normal password mechanism and so it doesn't matter
 * that it overlaps with another password above.
 */
#define GNKEY_MIRAMAR             30

/* Definition of sub-feature strings*/

#define GNSTR_DEVFEATURE_HCP            FWSTR_TEXTSTRING("HCP")
#define GNSTR_DEVFEATURE_HCMSSE         FWSTR_TEXTSTRING("HCMSSE")
#define GNSTR_DEVFEATURE_ICC            FWSTR_TEXTSTRING("HIPP")
#define GNSTR_DEVFEATURE_HCMS           FWSTR_TEXTSTRING("HFCS")
#define GNSTR_DEVFEATURE_HCMSLITE       FWSTR_TEXTSTRING("HSCS")
#define GNSTR_DEVFEATURE_HSCSX          FWSTR_TEXTSTRING("HSCSX")       /* HSCS variant */
#define GNSTR_DEVFEATURE_HCEP       FWSTR_TEXTSTRING("HCEP")

#define GNKEY_SET_SUBFEATURE_STRING(s, f) \
MACRO_START \
            if ((f) == GNKEY_DEVFEATURE_HCP) \
                FwStrCpy((s), GNSTR_DEVFEATURE_HCP); \
            else if ((f) == GNKEY_DEVFEATURE_HCMSSE) \
                FwStrCpy((s), GNSTR_DEVFEATURE_HCMSSE); \
            else if ((f) == GNKEY_DEVFEATURE_ICC) \
                FwStrCpy((s), GNSTR_DEVFEATURE_ICC); \
            else if ((f) == GNKEY_DEVFEATURE_HCMS) \
                FwStrCpy((s), GNSTR_DEVFEATURE_HCMS); \
            else if ((f) == GNKEY_DEVFEATURE_HCMSLITE) \
                FwStrCpy((s), GNSTR_DEVFEATURE_HCMSLITE); \
            else if ((f) == GNKEY_DEVFEATURE_HSCSX) \
                FwStrCpy((s), GNSTR_DEVFEATURE_HSCSX); \
            else if ((f) == GNKEY_DEVFEATURE_HCEP) \
                FwStrCpy((s), GNSTR_DEVFEATURE_HCEP); \
            else \
                HQFAIL("Illegal subfeature"); \
MACRO_END


/* Definition of leading part of protected device key string */
#define GNSTR_PROTECTED_DEVICE          "ProtDev"

/* Definition of leading part of protected screen key string */
#define GNSTR_PROTECTED_SCREEN          "ProtScr"

/* Definition of leading part of protected core module key string */
#define GNSTR_PROTECTED_CORE_MODULE     "ProtCM"

/* Definition of leading part of active threads key string */
#define GNSTR_MAX_THREADS_LIMIT         "Threads"

#define GENERATE_MAX_THREADS_STRING(sprintf_fn, str_array, format_type, nthreads)  \
MACRO_START                                                                        \
  if ((nthreads) == 0)                                                             \
    sprintf_fn(str_array, (format_type)"%s%u", GNSTR_MAX_THREADS_LIMIT, nthreads); \
  else                                                                             \
    sprintf_fn(str_array, (format_type)"%s%u-%u-%u", GNSTR_MAX_THREADS_LIMIT,      \
      (nthreads) * 13847, (nthreads) * 501763, (nthreads) * 9538714);              \
MACRO_END


/* GENERATEKEY (uint32 FinalResult,
                uint32 nSecurity, uint32 KeyTable [16][2])
   This is a macro so that it can be shared easily between the rip and
   the key generator program.
*/

#define GENERATEKEY(FinalResult, nSecurity, KeyTable, nFeature)             \
MACRO_START                                                                 \
  uint32 __i, __serialnumber = (nSecurity);                                 \
  int32 __nOneBits;                                                         \
  FinalResult = __nOneBits = 0;                                             \
  for (__i = 0; __i < 16; __i++) {                                          \
    FinalResult |= (__serialnumber & 1) << (KeyTable [__i]);                \
    __nOneBits += (__serialnumber & 1);                                     \
    __serialnumber >>= 1;                                                   \
    __serialnumber = ~ __serialnumber;                                      \
  }                                                                         \
  /* disguise any pattern */                                                \
  FinalResult = 0xaa ^ ((FinalResult >> __nOneBits) |                       \
    ((FinalResult << (16 - __nOneBits)) & 0xffff));                         \
  /* Compute checksum and or into top word of result */                     \
  CHECKSUM (FinalResult, __i, nFeature);                                    \
  FinalResult |= __i << 16;                                                 \
MACRO_END


/* Perturb the table by the string given. Note that this table
 * must begin with 15, and no other features should have 15 as their
 * first number. This prevents sub-features from accidently assuming
 * the same table as a regular feature.
 */

#define PERTURBKEYTABLE(pTable, aTable, pbzPeturbStr)                       \
MACRO_START                                                                 \
  uint8    __aModulos[] = {13, 15, 7, 11, 5, 9};                            \
  int32    __i, __l;                                                        \
  int32 __v1, __v2;                                                         \
  uint8 __temp;                                                             \
                                                                            \
  __v1 = 0;                                                                 \
  __l  = (int32)strlen((char*)(pbzPeturbStr));                              \
  for (__i = __l - 1; __i >= 0; __i--)                                      \
    __v1 += (pbzPeturbStr)[__i];                                            \
  __v1 = (__v1 % 15) + 1;                                                   \
  /* take a "shifted" copy of the table */                                  \
  (aTable)[0] = (pTable)[0];                                                \
  for (__i = 1; __i < 16; __i++) {                                          \
    __v2 = (__i + __v1) & 0xf;                                              \
    (aTable)[__i] = (pTable)[__v2];                                         \
  }                                                                         \
  for (__i = 0; __i < __l; __i += 2) {                                      \
    if ((__v1 = (pbzPeturbStr)[__i]) == 0)                                  \
      break;                                                                \
    if ((__v2 = (pbzPeturbStr)[__i + 1]) == 0)                              \
      break;                                                                \
    __v1 = (__v1%(int32)__aModulos[(__i%NUM_ARRAY_ITEMS(__aModulos))])+1;   \
    __v2 = (__v2%(int32)__aModulos[(__i%NUM_ARRAY_ITEMS(__aModulos))+1])+1; \
    __temp = (aTable)[__v1];                                                \
    (aTable)[__v1] = (aTable)[__v2];                                        \
    (aTable)[__v2] = __temp;                                                \
  }                                                                         \
MACRO_END

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
