/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:rop.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL ROP implementation.
 */
#include "core.h"

/* Rop instruction opcodes. */

#define END 0

/* Load instructions. */
#define SRC 1
#define TXT 2
#define DST 3

/* Operation instructions. */
#define AND 4
#define OR 5
#define XOR 6
#define NOT 7

#define INSTRUCTION_BITS 4
#define INSTRUCTION_MASK ((1 << INSTRUCTION_BITS) - 1)
#define INSTRUCTION_WORD_BITS 16

/* Build an instruction word; the first instruction is placed in the least
 * significant bits. */
#define ONE_WORD(a_, b_, c_, d_) \
  ((uint16)((a_) | ((b_) << 4) | ((c_) << 8) | ((d_) << 12)))
#define TWO_WORDS(a_, b_, c_, d_, e_, f_, g_, h_) \
  ONE_WORD(a_, b_, c_, d_), ONE_WORD(e_, f_, g_, h_)
#define THREE_WORDS(a_, b_, c_, d_, e_, f_, g_, h_, i_, j_, k_, l_) \
  TWO_WORDS(a_, b_, c_, d_, e_, f_, g_, h_), ONE_WORD(i_, j_, k_, l_)

uint16 rop_1[2] = {TWO_WORDS(DST, TXT, SRC, OR, OR, NOT, END, END)};
uint16 rop_2[2] = {TWO_WORDS(DST, TXT, SRC, OR, NOT, AND, END, END)};
uint16 rop_3[2] = {TWO_WORDS(TXT, SRC, OR, NOT, END, END, END, END)};
uint16 rop_4[2] = {TWO_WORDS(SRC, DST, TXT, OR, NOT, AND, END, END)};
uint16 rop_5[2] = {TWO_WORDS(DST, TXT, OR, NOT, END, END, END, END)};
uint16 rop_6[2] = {TWO_WORDS(TXT, DST, SRC, XOR, NOT, OR, NOT, END)};
uint16 rop_7[2] = {TWO_WORDS(TXT, DST, SRC, AND, OR, NOT, END, END)};
uint16 rop_8[2] = {TWO_WORDS(SRC, DST, TXT, NOT, AND, AND, END, END)};
uint16 rop_9[2] = {TWO_WORDS(TXT, DST, SRC, XOR, OR, NOT, END, END)};
uint16 rop_10[2] = {TWO_WORDS(DST, TXT, NOT, AND, END, END, END, END)};
uint16 rop_11[2] = {TWO_WORDS(TXT, SRC, DST, NOT, AND, OR, NOT, END)};
uint16 rop_12[2] = {TWO_WORDS(SRC, TXT, NOT, AND, END, END, END, END)};
uint16 rop_13[2] = {TWO_WORDS(TXT, DST, SRC, NOT, AND, OR, NOT, END)};
uint16 rop_14[2] = {TWO_WORDS(TXT, DST, SRC, OR, NOT, OR, NOT, END)};
uint16 rop_15[1] = {ONE_WORD(TXT, NOT, END, END)};
uint16 rop_16[2] = {TWO_WORDS(TXT, DST, SRC, OR, NOT, AND, END, END)};
uint16 rop_17[2] = {TWO_WORDS(DST, SRC, OR, NOT, END, END, END, END)};
uint16 rop_18[2] = {TWO_WORDS(SRC, DST, TXT, XOR, NOT, OR, NOT, END)};
uint16 rop_19[2] = {TWO_WORDS(SRC, DST, TXT, AND, OR, NOT, END, END)};
uint16 rop_20[2] = {TWO_WORDS(DST, TXT, SRC, XOR, NOT, OR, NOT, END)};
uint16 rop_21[2] = {TWO_WORDS(DST, TXT, SRC, AND, OR, NOT, END, END)};
uint16 rop_22[3] = {THREE_WORDS(TXT, SRC, DST, TXT, SRC, AND, NOT, AND, XOR, XOR, END, END)};
uint16 rop_23[3] = {THREE_WORDS(SRC, SRC, TXT, XOR, DST, SRC, XOR, AND, XOR, NOT, END, END)};
uint16 rop_24[2] = {TWO_WORDS(SRC, TXT, XOR, TXT, DST, XOR, AND, END)};
uint16 rop_25[3] = {THREE_WORDS(SRC, DST, TXT, SRC, AND, NOT, AND, XOR, NOT, END, END, END)};
uint16 rop_26[2] = {TWO_WORDS(TXT, DST, SRC, TXT, AND, OR, XOR, END)};
uint16 rop_27[3] = {THREE_WORDS(SRC, DST, TXT, SRC, XOR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_28[2] = {TWO_WORDS(TXT, SRC, DST, TXT, AND, OR, XOR, END)};
uint16 rop_29[3] = {THREE_WORDS(DST, SRC, TXT, DST, XOR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_30[2] = {TWO_WORDS(TXT, DST, SRC, OR, XOR, END, END, END)};
uint16 rop_31[2] = {TWO_WORDS(TXT, DST, SRC, OR, AND, NOT, END, END)};
uint16 rop_32[2] = {TWO_WORDS(DST, TXT, SRC, NOT, AND, AND, END, END)};
uint16 rop_33[2] = {TWO_WORDS(SRC, DST, TXT, XOR, OR, NOT, END, END)};
uint16 rop_34[2] = {TWO_WORDS(DST, SRC, NOT, AND, END, END, END, END)};
uint16 rop_35[2] = {TWO_WORDS(SRC, TXT, DST, NOT, AND, OR, NOT, END)};
uint16 rop_36[2] = {TWO_WORDS(SRC, TXT, XOR, DST, SRC, XOR, AND, END)};
uint16 rop_37[3] = {THREE_WORDS(TXT, DST, SRC, TXT, AND, NOT, AND, XOR, NOT, END, END, END)};
uint16 rop_38[2] = {TWO_WORDS(SRC, DST, TXT, SRC, AND, OR, XOR, END)};
uint16 rop_39[3] = {THREE_WORDS(SRC, DST, TXT, SRC, XOR, NOT, OR, XOR, END, END, END, END)};
uint16 rop_40[2] = {TWO_WORDS(DST, TXT, SRC, XOR, AND, END, END, END)};
uint16 rop_41[3] = {THREE_WORDS(TXT, SRC, DST, TXT, SRC, AND, OR, XOR, XOR, NOT, END, END)};
uint16 rop_42[2] = {TWO_WORDS(DST, TXT, SRC, AND, NOT, AND, END, END)};
uint16 rop_43[3] = {THREE_WORDS(SRC, SRC, TXT, XOR, TXT, DST, XOR, AND, XOR, NOT, END, END)};
uint16 rop_44[2] = {TWO_WORDS(SRC, TXT, DST, SRC, OR, AND, XOR, END)};
uint16 rop_45[2] = {TWO_WORDS(TXT, SRC, DST, NOT, OR, XOR, END, END)};
uint16 rop_46[2] = {TWO_WORDS(TXT, SRC, DST, TXT, XOR, OR, XOR, END)};
uint16 rop_47[2] = {TWO_WORDS(TXT, SRC, DST, NOT, OR, AND, NOT, END)};
uint16 rop_48[2] = {TWO_WORDS(TXT, SRC, NOT, AND, END, END, END, END)};
uint16 rop_49[2] = {TWO_WORDS(SRC, DST, TXT, NOT, AND, OR, NOT, END)};
uint16 rop_50[2] = {TWO_WORDS(SRC, DST, TXT, SRC, OR, OR, XOR, END)};
uint16 rop_51[1] = {ONE_WORD(SRC, NOT, END, END)};
uint16 rop_52[2] = {TWO_WORDS(SRC, TXT, DST, SRC, AND, OR, XOR, END)};
uint16 rop_53[3] = {THREE_WORDS(SRC, TXT, DST, SRC, XOR, NOT, OR, XOR, END, END, END, END)};
uint16 rop_54[2] = {TWO_WORDS(SRC, DST, TXT, OR, XOR, END, END, END)};
uint16 rop_55[2] = {TWO_WORDS(SRC, DST, TXT, OR, AND, NOT, END, END)};
uint16 rop_56[2] = {TWO_WORDS(TXT, SRC, DST, TXT, OR, AND, XOR, END)};
uint16 rop_57[2] = {TWO_WORDS(SRC, TXT, DST, NOT, OR, XOR, END, END)};
uint16 rop_58[2] = {TWO_WORDS(SRC, TXT, DST, SRC, XOR, OR, XOR, END)};
uint16 rop_59[2] = {TWO_WORDS(SRC, TXT, DST, NOT, OR, AND, NOT, END)};
uint16 rop_60[1] = {ONE_WORD(TXT, SRC, XOR, END)};
uint16 rop_61[3] = {THREE_WORDS(SRC, TXT, DST, SRC, OR, NOT, OR, XOR, END, END, END, END)};
uint16 rop_62[3] = {THREE_WORDS(SRC, TXT, DST, SRC, NOT, AND, OR, XOR, END, END, END, END)};
uint16 rop_63[2] = {TWO_WORDS(TXT, SRC, AND, NOT, END, END, END, END)};
uint16 rop_64[2] = {TWO_WORDS(TXT, SRC, DST, NOT, AND, AND, END, END)};
uint16 rop_65[2] = {TWO_WORDS(DST, TXT, SRC, XOR, OR, NOT, END, END)};
uint16 rop_66[2] = {TWO_WORDS(SRC, DST, XOR, TXT, DST, XOR, AND, END)};
uint16 rop_67[3] = {THREE_WORDS(SRC, TXT, DST, SRC, AND, NOT, AND, XOR, NOT, END, END, END)};
uint16 rop_68[2] = {TWO_WORDS(SRC, DST, NOT, AND, END, END, END, END)};
uint16 rop_69[2] = {TWO_WORDS(DST, TXT, SRC, NOT, AND, OR, NOT, END)};
uint16 rop_70[2] = {TWO_WORDS(DST, SRC, TXT, DST, AND, OR, XOR, END)};
uint16 rop_71[3] = {THREE_WORDS(TXT, SRC, DST, TXT, XOR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_72[2] = {TWO_WORDS(SRC, DST, TXT, XOR, AND, END, END, END)};
uint16 rop_73[3] = {THREE_WORDS(TXT, DST, SRC, TXT, DST, AND, OR, XOR, XOR, NOT, END, END)};
uint16 rop_74[2] = {TWO_WORDS(DST, TXT, SRC, DST, OR, AND, XOR, END)};
uint16 rop_75[2] = {TWO_WORDS(TXT, DST, SRC, NOT, OR, XOR, END, END)};
uint16 rop_76[2] = {TWO_WORDS(SRC, DST, TXT, AND, NOT, AND, END, END)};
uint16 rop_77[3] = {THREE_WORDS(SRC, SRC, TXT, XOR, DST, SRC, XOR, OR, XOR, NOT, END, END)};
uint16 rop_78[2] = {TWO_WORDS(TXT, DST, SRC, TXT, XOR, OR, XOR, END)};
uint16 rop_79[2] = {TWO_WORDS(TXT, DST, SRC, NOT, OR, AND, NOT, END)};
uint16 rop_80[2] = {TWO_WORDS(TXT, DST, NOT, AND, END, END, END, END)};
uint16 rop_81[2] = {TWO_WORDS(DST, SRC, TXT, NOT, AND, OR, NOT, END)};
uint16 rop_82[2] = {TWO_WORDS(DST, TXT, SRC, DST, AND, OR, XOR, END)};
uint16 rop_83[3] = {THREE_WORDS(SRC, TXT, DST, SRC, XOR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_84[2] = {TWO_WORDS(DST, TXT, SRC, OR, NOT, OR, NOT, END)};
uint16 rop_85[1] = {ONE_WORD(DST, NOT, END, END)};
uint16 rop_86[2] = {TWO_WORDS(DST, TXT, SRC, OR, XOR, END, END, END)};
uint16 rop_87[2] = {TWO_WORDS(DST, TXT, SRC, OR, AND, NOT, END, END)};
uint16 rop_88[2] = {TWO_WORDS(TXT, DST, SRC, TXT, OR, AND, XOR, END)};
uint16 rop_89[2] = {TWO_WORDS(DST, TXT, SRC, NOT, OR, XOR, END, END)};
uint16 rop_90[1] = {ONE_WORD(DST, TXT, XOR, END)};
uint16 rop_91[3] = {THREE_WORDS(DST, TXT, SRC, DST, OR, NOT, OR, XOR, END, END, END, END)};
uint16 rop_92[2] = {TWO_WORDS(DST, TXT, SRC, DST, XOR, OR, XOR, END)};
uint16 rop_93[2] = {TWO_WORDS(DST, TXT, SRC, NOT, OR, AND, NOT, END)};
uint16 rop_94[3] = {THREE_WORDS(DST, TXT, SRC, DST, NOT, AND, OR, XOR, END, END, END, END)};
uint16 rop_95[2] = {TWO_WORDS(DST, TXT, AND, NOT, END, END, END, END)};
uint16 rop_96[2] = {TWO_WORDS(TXT, DST, SRC, XOR, AND, END, END, END)};
uint16 rop_97[3] = {THREE_WORDS(DST, SRC, TXT, DST, SRC, AND, OR, XOR, XOR, NOT, END, END)};
uint16 rop_98[2] = {TWO_WORDS(DST, SRC, TXT, DST, OR, AND, XOR, END)};
uint16 rop_99[2] = {TWO_WORDS(SRC, DST, TXT, NOT, OR, XOR, END, END)};
uint16 rop_100[2] = {TWO_WORDS(SRC, DST, TXT, SRC, OR, AND, XOR, END)};
uint16 rop_101[2] = {TWO_WORDS(DST, SRC, TXT, NOT, OR, XOR, END, END)};
uint16 rop_102[1] = {ONE_WORD(DST, SRC, XOR, END)};
uint16 rop_103[3] = {THREE_WORDS(SRC, DST, TXT, SRC, OR, NOT, OR, XOR, END, END, END, END)};
uint16 rop_104[3] = {THREE_WORDS(DST, SRC, TXT, DST, SRC, OR, NOT, OR, XOR, XOR, NOT, END)};
uint16 rop_105[2] = {TWO_WORDS(TXT, DST, SRC, XOR, XOR, NOT, END, END)};
uint16 rop_106[2] = {TWO_WORDS(DST, TXT, SRC, AND, XOR, END, END, END)};
uint16 rop_107[3] = {THREE_WORDS(TXT, SRC, DST, TXT, SRC, OR, AND, XOR, XOR, NOT, END, END)};
uint16 rop_108[2] = {TWO_WORDS(SRC, DST, TXT, AND, XOR, END, END, END)};
uint16 rop_109[3] = {THREE_WORDS(TXT, DST, SRC, TXT, DST, OR, AND, XOR, XOR, NOT, END, END)};
uint16 rop_110[3] = {THREE_WORDS(SRC, DST, TXT, SRC, NOT, OR, AND, XOR, END, END, END, END)};
uint16 rop_111[2] = {TWO_WORDS(TXT, DST, SRC, XOR, NOT, AND, NOT, END)};
uint16 rop_112[2] = {TWO_WORDS(TXT, DST, SRC, AND, NOT, AND, END, END)};
uint16 rop_113[3] = {THREE_WORDS(SRC, SRC, DST, XOR, TXT, DST, XOR, AND, XOR, NOT, END, END)};
uint16 rop_114[2] = {TWO_WORDS(SRC, DST, TXT, SRC, XOR, OR, XOR, END)};
uint16 rop_115[2] = {TWO_WORDS(SRC, DST, TXT, NOT, OR, AND, NOT, END)};
uint16 rop_116[2] = {TWO_WORDS(DST, SRC, TXT, DST, XOR, OR, XOR, END)};
uint16 rop_117[2] = {TWO_WORDS(DST, SRC, TXT, NOT, OR, AND, NOT, END)};
uint16 rop_118[3] = {THREE_WORDS(SRC, DST, TXT, SRC, NOT, AND, OR, XOR, END, END, END, END)};
uint16 rop_119[2] = {TWO_WORDS(DST, SRC, AND, NOT, END, END, END, END)};
uint16 rop_120[2] = {TWO_WORDS(TXT, DST, SRC, AND, XOR, END, END, END)};
uint16 rop_121[3] = {THREE_WORDS(DST, SRC, TXT, DST, SRC, OR, AND, XOR, XOR, NOT, END, END)};
uint16 rop_122[3] = {THREE_WORDS(DST, TXT, SRC, DST, NOT, OR, AND, XOR, END, END, END, END)};
uint16 rop_123[2] = {TWO_WORDS(SRC, DST, TXT, XOR, NOT, AND, NOT, END)};
uint16 rop_124[3] = {THREE_WORDS(SRC, TXT, DST, SRC, NOT, OR, AND, XOR, END, END, END, END)};
uint16 rop_125[2] = {TWO_WORDS(DST, TXT, SRC, XOR, NOT, AND, NOT, END)};
uint16 rop_126[2] = {TWO_WORDS(SRC, TXT, XOR, DST, SRC, XOR, OR, END)};
uint16 rop_127[2] = {TWO_WORDS(DST, TXT, SRC, AND, AND, NOT, END, END)};
uint16 rop_128[2] = {TWO_WORDS(DST, TXT, SRC, AND, AND, END, END, END)};
uint16 rop_129[3] = {THREE_WORDS(SRC, TXT, XOR, DST, SRC, XOR, OR, NOT, END, END, END, END)};
uint16 rop_130[2] = {TWO_WORDS(DST, TXT, SRC, XOR, NOT, AND, END, END)};
uint16 rop_131[3] = {THREE_WORDS(SRC, TXT, DST, SRC, NOT, OR, AND, XOR, NOT, END, END, END)};
uint16 rop_132[2] = {TWO_WORDS(SRC, DST, TXT, XOR, NOT, AND, END, END)};
uint16 rop_133[3] = {THREE_WORDS(TXT, DST, SRC, TXT, NOT, OR, AND, XOR, NOT, END, END, END)};
uint16 rop_134[3] = {THREE_WORDS(DST, SRC, TXT, DST, SRC, OR, AND, XOR, XOR, END, END, END)};
uint16 rop_135[2] = {TWO_WORDS(TXT, DST, SRC, AND, XOR, NOT, END, END)};
uint16 rop_136[1] = {ONE_WORD(DST, SRC, AND, END)};
uint16 rop_137[3] = {THREE_WORDS(SRC, DST, TXT, SRC, NOT, AND, OR, XOR, NOT, END, END, END)};
uint16 rop_138[2] = {TWO_WORDS(DST, SRC, TXT, NOT, OR, AND, END, END)};
uint16 rop_139[3] = {THREE_WORDS(DST, SRC, TXT, DST, XOR, OR, XOR, NOT, END, END, END, END)};
uint16 rop_140[2] = {TWO_WORDS(SRC, DST, TXT, NOT, OR, AND, END, END)};
uint16 rop_141[3] = {THREE_WORDS(SRC, DST, TXT, SRC, XOR, OR, XOR, NOT, END, END, END, END)};
uint16 rop_142[3] = {THREE_WORDS(SRC, SRC, DST, XOR, TXT, DST, XOR, AND, XOR, END, END, END)};
uint16 rop_143[2] = {TWO_WORDS(TXT, DST, SRC, AND, NOT, AND, NOT, END)};
uint16 rop_144[2] = {TWO_WORDS(TXT, DST, SRC, XOR, NOT, AND, END, END)};
uint16 rop_145[3] = {THREE_WORDS(SRC, DST, TXT, SRC, NOT, OR, AND, XOR, NOT, END, END, END)};
uint16 rop_146[3] = {THREE_WORDS(DST, TXT, SRC, DST, TXT, OR, AND, XOR, XOR, END, END, END)};
uint16 rop_147[2] = {TWO_WORDS(SRC, TXT, DST, AND, XOR, NOT, END, END)};
uint16 rop_148[3] = {THREE_WORDS(TXT, SRC, DST, TXT, SRC, OR, AND, XOR, XOR, END, END, END)};
uint16 rop_149[2] = {TWO_WORDS(DST, TXT, SRC, AND, XOR, NOT, END, END)};
uint16 rop_150[2] = {TWO_WORDS(DST, TXT, SRC, XOR, XOR, END, END, END)};
uint16 rop_151[3] = {THREE_WORDS(TXT, SRC, DST, TXT, SRC, OR, NOT, OR, XOR, XOR, END, END)};
uint16 rop_152[3] = {THREE_WORDS(SRC, DST, TXT, SRC, OR, NOT, OR, XOR, NOT, END, END, END)};
uint16 rop_153[2] = {TWO_WORDS(DST, SRC, XOR, NOT, END, END, END, END)};
uint16 rop_154[2] = {TWO_WORDS(DST, TXT, SRC, NOT, AND, XOR, END, END)};
uint16 rop_155[3] = {THREE_WORDS(SRC, DST, TXT, SRC, OR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_156[2] = {TWO_WORDS(SRC, TXT, DST, NOT, AND, XOR, END, END)};
uint16 rop_157[3] = {THREE_WORDS(DST, SRC, TXT, DST, OR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_158[3] = {THREE_WORDS(DST, SRC, TXT, DST, SRC, AND, OR, XOR, XOR, END, END, END)};
uint16 rop_159[2] = {TWO_WORDS(TXT, DST, SRC, XOR, AND, NOT, END, END)};
uint16 rop_160[1] = {ONE_WORD(DST, TXT, AND, END)};
uint16 rop_161[3] = {THREE_WORDS(TXT, DST, SRC, TXT, NOT, AND, OR, XOR, NOT, END, END, END)};
uint16 rop_162[2] = {TWO_WORDS(DST, TXT, SRC, NOT, OR, AND, END, END)};
uint16 rop_163[3] = {THREE_WORDS(DST, TXT, SRC, DST, XOR, OR, XOR, NOT, END, END, END, END)};
uint16 rop_164[3] = {THREE_WORDS(TXT, DST, SRC, TXT, OR, NOT, OR, XOR, NOT, END, END, END)};
uint16 rop_165[2] = {TWO_WORDS(TXT, DST, XOR, NOT, END, END, END, END)};
uint16 rop_166[2] = {TWO_WORDS(DST, SRC, TXT, NOT, AND, XOR, END, END)};
uint16 rop_167[3] = {THREE_WORDS(TXT, DST, SRC, TXT, OR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_168[2] = {TWO_WORDS(DST, TXT, SRC, OR, AND, END, END, END)};
uint16 rop_169[2] = {TWO_WORDS(DST, TXT, SRC, OR, XOR, NOT, END, END)};
uint16 rop_170[1] = {ONE_WORD(DST, END, END, END)};
uint16 rop_171[2] = {TWO_WORDS(DST, TXT, SRC, OR, NOT, OR, END, END)};
uint16 rop_172[2] = {TWO_WORDS(SRC, TXT, DST, SRC, XOR, AND, XOR, END)};
uint16 rop_173[3] = {THREE_WORDS(DST, TXT, SRC, DST, AND, OR, XOR, NOT, END, END, END, END)};
uint16 rop_174[2] = {TWO_WORDS(DST, SRC, TXT, NOT, AND, OR, END, END)};
uint16 rop_175[2] = {TWO_WORDS(DST, TXT, NOT, OR, END, END, END, END)};
uint16 rop_176[2] = {TWO_WORDS(TXT, DST, SRC, NOT, OR, AND, END, END)};
uint16 rop_177[3] = {THREE_WORDS(TXT, DST, SRC, TXT, XOR, OR, XOR, NOT, END, END, END, END)};
uint16 rop_178[3] = {THREE_WORDS(SRC, SRC, TXT, XOR, DST, SRC, XOR, OR, XOR, END, END, END)};
uint16 rop_179[2] = {TWO_WORDS(SRC, DST, TXT, AND, NOT, AND, NOT, END)};
uint16 rop_180[2] = {TWO_WORDS(TXT, SRC, DST, NOT, AND, XOR, END, END)};
uint16 rop_181[3] = {THREE_WORDS(DST, TXT, SRC, DST, OR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_182[3] = {THREE_WORDS(DST, TXT, SRC, DST, TXT, AND, OR, XOR, XOR, END, END, END)};
uint16 rop_183[2] = {TWO_WORDS(SRC, DST, TXT, XOR, AND, NOT, END, END)};
uint16 rop_184[2] = {TWO_WORDS(TXT, SRC, DST, TXT, XOR, AND, XOR, END)};
uint16 rop_185[3] = {THREE_WORDS(DST, SRC, TXT, DST, AND, OR, XOR, NOT, END, END, END, END)};
uint16 rop_186[2] = {TWO_WORDS(DST, TXT, SRC, NOT, AND, OR, END, END)};
uint16 rop_187[2] = {TWO_WORDS(DST, SRC, NOT, OR, END, END, END, END)};
uint16 rop_188[3] = {THREE_WORDS(SRC, TXT, DST, SRC, AND, NOT, AND, XOR, END, END, END, END)};
uint16 rop_189[3] = {THREE_WORDS(SRC, DST, XOR, TXT, DST, XOR, AND, NOT, END, END, END, END)};
uint16 rop_190[2] = {TWO_WORDS(DST, TXT, SRC, XOR, OR, END, END, END)};
uint16 rop_191[2] = {TWO_WORDS(DST, TXT, SRC, AND, NOT, OR, END, END)};
uint16 rop_192[1] = {ONE_WORD(TXT, SRC, AND, END)};
uint16 rop_193[3] = {THREE_WORDS(SRC, TXT, DST, SRC, NOT, AND, OR, XOR, NOT, END, END, END)};
uint16 rop_194[3] = {THREE_WORDS(SRC, TXT, DST, SRC, OR, NOT, OR, XOR, NOT, END, END, END)};
uint16 rop_195[2] = {TWO_WORDS(TXT, SRC, XOR, NOT, END, END, END, END)};
uint16 rop_196[2] = {TWO_WORDS(SRC, TXT, DST, NOT, OR, AND, END, END)};
uint16 rop_197[3] = {THREE_WORDS(SRC, TXT, DST, SRC, XOR, OR, XOR, NOT, END, END, END, END)};
uint16 rop_198[2] = {TWO_WORDS(SRC, DST, TXT, NOT, AND, XOR, END, END)};
uint16 rop_199[3] = {THREE_WORDS(TXT, SRC, DST, TXT, OR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_200[2] = {TWO_WORDS(SRC, DST, TXT, OR, AND, END, END, END)};
uint16 rop_201[2] = {TWO_WORDS(SRC, TXT, DST, OR, XOR, NOT, END, END)};
uint16 rop_202[2] = {TWO_WORDS(DST, TXT, SRC, DST, XOR, AND, XOR, END)};
uint16 rop_203[3] = {THREE_WORDS(SRC, TXT, DST, SRC, AND, OR, XOR, NOT, END, END, END, END)};
uint16 rop_204[1] = {ONE_WORD(SRC, END, END, END)};
uint16 rop_205[2] = {TWO_WORDS(SRC, DST, TXT, OR, NOT, OR, END, END)};
uint16 rop_206[2] = {TWO_WORDS(SRC, DST, TXT, NOT, AND, OR, END, END)};
uint16 rop_207[2] = {TWO_WORDS(SRC, TXT, NOT, OR, END, END, END, END)};
uint16 rop_208[2] = {TWO_WORDS(TXT, SRC, DST, NOT, OR, AND, END, END)};
uint16 rop_209[3] = {THREE_WORDS(TXT, SRC, DST, TXT, XOR, OR, XOR, NOT, END, END, END, END)};
uint16 rop_210[2] = {TWO_WORDS(TXT, DST, SRC, NOT, AND, XOR, END, END)};
uint16 rop_211[3] = {THREE_WORDS(SRC, TXT, DST, SRC, OR, AND, XOR, NOT, END, END, END, END)};
uint16 rop_212[3] = {THREE_WORDS(SRC, SRC, TXT, XOR, TXT, DST, XOR, AND, XOR, END, END, END)};
uint16 rop_213[2] = {TWO_WORDS(DST, TXT, SRC, AND, NOT, AND, NOT, END)};
uint16 rop_214[3] = {THREE_WORDS(TXT, SRC, DST, TXT, SRC, AND, OR, XOR, XOR, END, END, END)};
uint16 rop_215[2] = {TWO_WORDS(DST, TXT, SRC, XOR, AND, NOT, END, END)};
uint16 rop_216[2] = {TWO_WORDS(TXT, DST, SRC, TXT, XOR, AND, XOR, END)};
uint16 rop_217[3] = {THREE_WORDS(SRC, DST, TXT, SRC, AND, OR, XOR, NOT, END, END, END, END)};
uint16 rop_218[3] = {THREE_WORDS(DST, TXT, SRC, DST, AND, NOT, AND, XOR, END, END, END, END)};
uint16 rop_219[3] = {THREE_WORDS(SRC, TXT, XOR, DST, SRC, XOR, AND, NOT, END, END, END, END)};
uint16 rop_220[2] = {TWO_WORDS(SRC, TXT, DST, NOT, AND, OR, END, END)};
uint16 rop_221[2] = {TWO_WORDS(SRC, DST, NOT, OR, END, END, END, END)};
uint16 rop_222[2] = {TWO_WORDS(SRC, DST, TXT, XOR, OR, END, END, END)};
uint16 rop_223[2] = {TWO_WORDS(SRC, DST, TXT, AND, NOT, OR, END, END)};
uint16 rop_224[2] = {TWO_WORDS(TXT, DST, SRC, OR, AND, END, END, END)};
uint16 rop_225[2] = {TWO_WORDS(TXT, DST, SRC, OR, XOR, NOT, END, END)};
uint16 rop_226[2] = {TWO_WORDS(DST, SRC, TXT, DST, XOR, AND, XOR, END)};
uint16 rop_227[3] = {THREE_WORDS(TXT, SRC, DST, TXT, AND, OR, XOR, NOT, END, END, END, END)};
uint16 rop_228[2] = {TWO_WORDS(SRC, DST, TXT, SRC, XOR, AND, XOR, END)};
uint16 rop_229[3] = {THREE_WORDS(TXT, DST, SRC, TXT, AND, OR, XOR, NOT, END, END, END, END)};
uint16 rop_230[3] = {THREE_WORDS(SRC, DST, TXT, SRC, AND, NOT, AND, XOR, END, END, END, END)};
uint16 rop_231[3] = {THREE_WORDS(SRC, TXT, XOR, TXT, DST, XOR, AND, NOT, END, END, END, END)};
uint16 rop_232[3] = {THREE_WORDS(SRC, SRC, TXT, XOR, DST, SRC, XOR, AND, XOR, END, END, END)};
uint16 rop_233[3] = {THREE_WORDS(DST, SRC, TXT, DST, SRC, AND, NOT, AND, XOR, XOR, NOT, END)};
uint16 rop_234[2] = {TWO_WORDS(DST, TXT, SRC, AND, OR, END, END, END)};
uint16 rop_235[2] = {TWO_WORDS(DST, TXT, SRC, XOR, NOT, OR, END, END)};
uint16 rop_236[2] = {TWO_WORDS(SRC, DST, TXT, AND, OR, END, END, END)};
uint16 rop_237[2] = {TWO_WORDS(SRC, DST, TXT, XOR, NOT, OR, END, END)};
uint16 rop_238[1] = {ONE_WORD(DST, SRC, OR, END)};
uint16 rop_239[2] = {TWO_WORDS(SRC, DST, TXT, NOT, OR, OR, END, END)};
uint16 rop_240[1] = {ONE_WORD(TXT, END, END, END)};
uint16 rop_241[2] = {TWO_WORDS(TXT, DST, SRC, OR, NOT, OR, END, END)};
uint16 rop_242[2] = {TWO_WORDS(TXT, DST, SRC, NOT, AND, OR, END, END)};
uint16 rop_243[2] = {TWO_WORDS(TXT, SRC, NOT, OR, END, END, END, END)};
uint16 rop_244[2] = {TWO_WORDS(TXT, SRC, DST, NOT, AND, OR, END, END)};
uint16 rop_245[2] = {TWO_WORDS(TXT, DST, NOT, OR, END, END, END, END)};
uint16 rop_246[2] = {TWO_WORDS(TXT, DST, SRC, XOR, OR, END, END, END)};
uint16 rop_247[2] = {TWO_WORDS(TXT, DST, SRC, AND, NOT, OR, END, END)};
uint16 rop_248[2] = {TWO_WORDS(TXT, DST, SRC, AND, OR, END, END, END)};
uint16 rop_249[2] = {TWO_WORDS(TXT, DST, SRC, XOR, NOT, OR, END, END)};
uint16 rop_250[1] = {ONE_WORD(DST, TXT, OR, END)};
uint16 rop_251[2] = {TWO_WORDS(DST, TXT, SRC, NOT, OR, OR, END, END)};
uint16 rop_252[1] = {ONE_WORD(TXT, SRC, OR, END)};
uint16 rop_253[2] = {TWO_WORDS(TXT, SRC, DST, NOT, OR, OR, END, END)};
uint16 rop_254[2] = {TWO_WORDS(DST, TXT, SRC, OR, OR, END, END, END)};

uint16* rops[256] = {
  NULL, rop_1, rop_2, rop_3, rop_4, rop_5, rop_6, rop_7, rop_8, rop_9, rop_10,
  rop_11, rop_12, rop_13, rop_14, rop_15, rop_16, rop_17, rop_18, rop_19,
  rop_20, rop_21, rop_22, rop_23, rop_24, rop_25, rop_26, rop_27, rop_28,
  rop_29, rop_30, rop_31, rop_32, rop_33, rop_34, rop_35, rop_36, rop_37,
  rop_38, rop_39, rop_40, rop_41, rop_42, rop_43, rop_44, rop_45, rop_46,
  rop_47, rop_48, rop_49, rop_50, rop_51, rop_52, rop_53, rop_54, rop_55,
  rop_56, rop_57, rop_58, rop_59, rop_60, rop_61, rop_62, rop_63, rop_64,
  rop_65, rop_66, rop_67, rop_68, rop_69, rop_70, rop_71, rop_72, rop_73,
  rop_74, rop_75, rop_76, rop_77, rop_78, rop_79, rop_80, rop_81, rop_82,
  rop_83, rop_84, rop_85, rop_86, rop_87, rop_88, rop_89, rop_90, rop_91,
  rop_92, rop_93, rop_94, rop_95, rop_96, rop_97, rop_98, rop_99, rop_100,
  rop_101, rop_102, rop_103, rop_104, rop_105, rop_106, rop_107, rop_108,
  rop_109, rop_110, rop_111, rop_112, rop_113, rop_114, rop_115, rop_116,
  rop_117, rop_118, rop_119, rop_120, rop_121, rop_122, rop_123, rop_124,
  rop_125, rop_126, rop_127, rop_128, rop_129, rop_130, rop_131, rop_132,
  rop_133, rop_134, rop_135, rop_136, rop_137, rop_138, rop_139, rop_140,
  rop_141, rop_142, rop_143, rop_144, rop_145, rop_146, rop_147, rop_148,
  rop_149, rop_150, rop_151, rop_152, rop_153, rop_154, rop_155, rop_156,
  rop_157, rop_158, rop_159, rop_160, rop_161, rop_162, rop_163, rop_164,
  rop_165, rop_166, rop_167, rop_168, rop_169, rop_170, rop_171, rop_172,
  rop_173, rop_174, rop_175, rop_176, rop_177, rop_178, rop_179, rop_180,
  rop_181, rop_182, rop_183, rop_184, rop_185, rop_186, rop_187, rop_188,
  rop_189, rop_190, rop_191, rop_192, rop_193, rop_194, rop_195, rop_196,
  rop_197, rop_198, rop_199, rop_200, rop_201, rop_202, rop_203, rop_204,
  rop_205, rop_206, rop_207, rop_208, rop_209, rop_210, rop_211, rop_212,
  rop_213, rop_214, rop_215, rop_216, rop_217, rop_218, rop_219, rop_220,
  rop_221, rop_222, rop_223, rop_224, rop_225, rop_226, rop_227, rop_228,
  rop_229, rop_230, rop_231, rop_232, rop_233, rop_234, rop_235, rop_236,
  rop_237, rop_238, rop_239, rop_240, rop_241, rop_242, rop_243, rop_244,
  rop_245, rop_246, rop_247, rop_248, rop_249, rop_250, rop_251, rop_252,
  rop_253, rop_254, NULL};

/* See header for doc. */
uint32 rop(uint32 s, uint32 t, uint32 d, uint8 rop)
{
  /* Check for special or common cases.  The following list is based on those
   * appearing in the Perf41 PCL 5c and XL jobs. */
  switch (rop) {
    case 0:
      return 0;

    case 90: /* DTx */
      return d ^ t;

    case 102: /* DSx */
      return d ^ s;

    case 136: /* DSa */
      return d & s;

    case 160: /* DTa */
      return d & t;

    case 170: /* D */
      return d;

    case 184: /* TSDTxax */
      return ((d ^ t) & s) ^ t;

    case 204: /* S */
      return s;

    case 240: /* T */
      return t;

    case 250: /* DTo */
      return d | t;

    case 252: /* TSo */
      return t | s;

    case 255:
      return 0xffffffff;
  }

  {
    uint32 stack[16];
    uint32* sp = stack;
    uint16* program = rops[rop];
    int32 shift = 0;

    while (TRUE) {
      uint32 instruction = (program[0] >> shift) & INSTRUCTION_MASK;
      switch (instruction) {
        default:
          HQFAIL("Corrupt ROP program.");
          return 0;

        case SRC:
          sp[0] = s;
          sp ++;
          break;

        case TXT:
          sp[0] = t;
          sp ++;
          break;

        case DST:
          sp[0] = d;
          sp ++;
          break;

        case AND:
          sp[-2] = sp[-1] & sp[-2];
          sp --;
          break;

        case OR:
          sp[-2] = sp[-1] | sp[-2];
          sp --;
          break;

        case XOR:
          sp[-2] = sp[-1] ^ sp[-2];
          sp --;
          break;

        case NOT:
          sp[-1] = ~sp[-1];
          break;

        case END:
          return sp[-1];
      }

      /* Move on to next instruction. */
      shift += INSTRUCTION_BITS;
      if (shift == INSTRUCTION_WORD_BITS) {
        shift = 0;
        program ++;
      }
    }
  }
  HQFAIL("Should never get here.");
  return 0;
}

/* Log stripped */

