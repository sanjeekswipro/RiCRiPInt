
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* sfntenum.h */


/*
   File:    sfnt_enum.h

   Written by: Mike Reed

   Copyright: 1988-1990 by Apple Computer, Inc., all rights reserved.


- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <3+>	7/11/91		CKL		Added standard header conditionals
		 <3>	12/20/90	MR		Correct INTEL definition of tag_FontProgram. [rb]
		 <2>	12/11/90	MR		Add trademark selector to naming table list. [rb]
		<10>	  8/8/90	JT		Fixed spelling of SFNT_ENUMS semaphore at the top of the file.
									This prevents the various constants from being included twice.
		 <9>	 7/18/90	MR		Fixed INTEL version of tag_GlyphData
		 <8>	 7/16/90	MR		Conditionalize redefinition of script codes
		 <7>	 7/13/90	MR		Conditionalize enums to allow for byte-reversal on INTEL chips
		 <6>	 6/30/90	MR		Remove tag reference to mvt and cryp
		 <4>	 6/26/90	MR		Add all script codes, with SM naming conventions
		 <3>	 6/20/90	MR		Change tag enums to #defines to be ansi-correct
		 <2>	  6/1/90	MR		Add postscript name to sfnt_NameIndex and post to tags.
	To Do:
*/

#ifndef __SFNT_ENUM__
#define __SFNT_ENUM__

typedef SL32 sfnt_TableTag;

#define tag_glyf_HILO	0x676c7966L        /* 'glyf' (Motorola order; also ROM version) */
#define tag_GSUB_HILO	0x47535542L        /* 'GSUB' (Motorola order; also ROM version) */
#define tag_cmap_HILO	0x636d6170L        /* 'cmap' (Motorola order; also ROM version) */
#define tag_name_HILO	0x6e616d65L        /* 'name' (Motorola order; also ROM version) */
#define tag_ttcf_HILO	0x74746366L		   /* 'ttcf' (Motorola order; also ROM version) */
#define tag_vhea_HILO	0x76686561L			/* 'vhea' keb 4/00*/
#define tag_vmtx_HILO	0x766d7478L			/* 'vmtx' */
#define tag_EBLC_HILO	0x45424c43L			/* 'EBLC' (Motorola order; also ROM version) 10-02-00 jfd */
#define tag_EBDT_HILO	0x45424454L			/* 'EBDT' (Motorola order; also ROM version) 10-02-00 jfd */
#define	tag_CFF_HILO	0x43464620L			/* 'CFF ' (Motorola order; also ROM version) 05-23-01 jfd */
/* for CCC font */
#define tag_HMTX_HILO	0x686d7478L			/* 'hmtx' (Motorola order; also ROM version) */
#define tag_MAXP_HILO	0x6d617870L			/* 'maxp' (Motorola order; also ROM version) */
#define tag_nstk_HILO	0x6e73746bL			/* 'nstk' (Motorola order; also ROM version) 12-06-01 jfd */
#define tag_HEAD_HILO	0x68656164L			/* 'head' (Motorola order; also ROM version) 08-23-04 qwu */

#define tag_vorg_HILO	0x564f5247L			/* 'VORG' (Motorola order; also ROM version) */

#define	tag_ADFH_HILO	0x41444648L			/* 'ADFH' (Motorola order; also ROM version) */

#if 0	/**** tags we might want to add... (sandra, 12 Nov 99) ****/  
/* copied from RAC directory - not handled by us */
//#define tag_MetricValue         0x6d767420        /* 'mvt ' */
//#define tag_Kerning             0x6b65726e        /* 'kern' */
//#define tag_Encryption          0x63727970        /* 'cryp' */

//#define tag_EBLC				0x45424C43		  /* 'EBLC' */
//#define tag_EBLC2				0x626c6f63		  /* 'bloc' Apple's TT GX version */
//#define tag_EBDT				0x45424454		  /* 'EBDT' */
//#define tag_EBDT2				0x62646174		  /* 'bdat' Apple's TT GX version */

/* in sample TTC file, but not handled by us */
//#define tag_gasp_HILO			0x67617370			/* 'gasp' */
//#define tag_mort_HILO			0x6d6f7274			/* 'mort' */
#endif	/********/  

#if (BYTEORDER == HILO)      /* Motorola byte order */

#define tag_FontHeader          0x68656164L        /* 'head' */
#define tag_HoriHeader          0x68686561L        /* 'hhea' */
#define tag_IndexToLoc          0x6c6f6361L        /* 'loca' */
#define tag_MaxProfile          0x6d617870L        /* 'maxp' */
#define tag_ControlValue        0x63767420L        /* 'cvt ' */
#define tag_PreProgram          0x70726570L        /* 'prep' */
#define tag_GlyphData           tag_glyf_HILO      /* 'glyf' */
#define tag_HorizontalMetrics   0x686d7478L        /* 'hmtx' */
#define tag_CharToIndexMap      tag_cmap_HILO	   /* 'cmap' */
#define tag_Kerning             0x6b65726eL        /* 'kern' */
#define tag_HoriDeviceMetrics   0x68646d78L        /* 'hdmx' */
#define tag_NamingTable         tag_name_HILO      /* 'name' */
#define tag_FontProgram         0x6670676dL        /* 'fpgm' */
#define tag_Postscript          0x706f7374L        /* 'post' */
#define tag_OS_2                0x4f532f32L        /* 'OS/2' */
#define tag_PCLT                0x50434c54L        /* 'PCLT' */
#define tag_GSUB                tag_GSUB_HILO      /* 'GSUB' */
#define tag_TTCHeader			tag_ttcf_HILO	   /* 'ttcf' */
#define tag_VertHeader          tag_vhea_HILO      /* 'vhea' keb 4/00 */
#define tag_VerticalMetrics     tag_vmtx_HILO      /* 'vmtx' */
#define tag_EBLC				tag_EBLC_HILO      	/* 'EBLC' */
#define tag_EBDT				tag_EBDT_HILO      	/* 'EBDT' */
#define tag_CompactFontFormat   tag_CFF_HILO       /* 'CFF ' */ /* 05-23-01 jfd */
#define tag_nstk				tag_nstk_HILO		/* 'nstk' */ /* 12-06-01 jfd */
#define tag_Vorg				tag_vorg_HILO		/* 'vorg' */
#define tag_VertDeviceMetrics   0x56444d58          /* 'VDMX' */
#define tag_ADFH                tag_ADFH_HILO       /* 'ADFH' */
#define tag_gdir                0x67646972L         /* 'gdir' */
#define tag_BASE				0x42415345L         /* 'BASE' */
#define tag_GDEF				0x47444546L         /* 'GDEF' */
#define tag_GPOS				0x47504f53L         /* 'GPOS' */
#define tag_JSFT				0x4a534654L		    /* 'JSFT' */				
#define tag_DSIG				0x44534947L			/* 'DSIG' */
#define tag_gasp				0x67617370L			/* 'gasp' */
#define tag_LTSH				0x4c545348L			/* 'LTSH' */
#else               /* Intel byte order */

#define tag_FontHeader          0x64616568L        /* 'head' */
#define tag_HoriHeader          0x61656868L        /* 'hhea' */
#define tag_IndexToLoc          0x61636f6cL        /* 'loca' */
#define tag_MaxProfile          0x7078616dL        /* 'maxp' */
#define tag_ControlValue        0x20747663L        /* 'cvt ' */
#define tag_PreProgram          0x70657270L        /* 'prep' */
#define tag_GlyphData           0x66796c67L        /* 'glyf' */
#define tag_HorizontalMetrics   0x78746d68L        /* 'hmtx' */
#define tag_CharToIndexMap      0x70616d63L        /* 'cmap' */
#define tag_Kerning             0x6e72656bL        /* 'kern' */
#define tag_HoriDeviceMetrics   0x786d6468L        /* 'hdmx' */
#define tag_NamingTable         0x656d616eL        /* 'name' */
#define tag_FontProgram         0x6d677066L        /* 'fpgm' */
#define tag_Postscript          0x74736f70L        /* 'post' */
#define tag_OS_2                0x322f534fL        /* 'OS/2' */
#define tag_PCLT                0x544c4350L        /* 'PCLT' */
#define tag_GSUB                0x42555347L        /* 'GSUB' */
#define tag_TTCHeader			0x66637474L		   /* 'ttcf' */
#define tag_VertHeader			0x61656876L		   /* 'vhea' keb 4/00*/
#define tag_VerticalMetrics		0x78746d76L	       /* 'vmtx' */
#define tag_EBLC				0x434c4245L         /* 'EBLC' */
#define tag_EBDT				0x54444245L			/* 'EBDT' */
#define tag_CompactFontFormat   0x20464643L        /* 'CFF ' */ /* 05-23-01 jfd */
#define tag_nstk				0x6b74736eL			/* 'nstk' */ /* 12-06-01 jfd */
#define tag_Vorg				0x47524f56L			/* 'VORG' */
#define tag_VertDeviceMetrics   0x584d4456        	/* 'VDMX' */
#define tag_ADFH                0x48464441L        /* 'ADFH' */

#define tag_gdir                0x72696467L         /* 'gdir' */
#define tag_BASE				0x45534142L			/* 'BASE' */
#define tag_GDEF				0x46454474L			/* 'GDEF' */
#define tag_GPOS				0x534f5047L			/* 'GPOS' */
#define tag_JSFT				0x5446534aL			/* 'JSFT' */
#define tag_DSIG				0x47495344L			/* 'DSIG' */
#define tag_gasp				0x70736167L			/* 'gasp' */
#define tag_LTSH				0x4853544cL			/* 'LTSH' */
#endif  /* BYTEORDER */

#endif  /*  __SFNT_ENUM__  */

/* -------------------------------------------------------------------
                            END OF "sfnt_enum.h"
   ------------------------------------------------------------------- */
