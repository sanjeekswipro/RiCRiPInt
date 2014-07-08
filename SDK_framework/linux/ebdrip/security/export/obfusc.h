/* $HopeName: SWsecurity!export:obfusc.h(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
   Obfuscate some internal symbols in the security compound. Needed because
   we compile the compound into a library to ship to customers to link with
   their own software. This is switched on by default. It may upset you if
   you are trying to use a symbolic debugger to debug the security compound
   code. If it does, simply switch it off in your local copy by use of the
   conditional compilation given below.

   To minimise impact, this feature only obfuscates the symbols which would
   be exposed in the creation of the dongle library in SWprod_purup and
   SWprod_prp-pc. The mechanism can be extended if later required.
*/

/* Log stripped */

#ifndef DEBUG_SYMBOLS


#define securityDevice    symbol1
#define allowsDisktodev    symbol2
#define corestart    symbol3
#define resultArray    symbol4

#define lastTimeTested    symbol5
#define OneSecondDelay    symbol7
#define fullTestMicroPharx3    symbol10
#define testMicroPhar    symbol13

#define customerID    symbol22
#define productCode    symbol23
#define productVersion    symbol24
#define demonstrationOnly    symbol25
#define resolutionLimit    symbol26
#define upperResolutionLimit    symbol27
#define lowerResolutionLimit    symbol28
#define securityNumber    symbol29
#define featureLevel    symbol30
#define spare1    symbol31
#define spare2    symbol32
#define spare3    symbol33

#define checkCode    symbol34
#define DongleMaxResolution    symbol38
#define timer    symbol47

#define serial_number    symbol50
#define next_char    symbol51
#define rand_char    symbol52

#define gen_test_buffer    symbol53
#define decode_reply_buffer    symbol54
#define interact_with_dongle    symbol55
#define reply_buffer    symbol56
#define query_data    symbol57
#define query_length    symbol58
#define query_buffer    symbol59
#define reply_data    symbol60
#define microphar_initialized    symbol61
#define filename    symbol62

#define do_microphar_thing    symbol63
#define oldarg    symbol64
#define newarg    symbol65
#define fullTestMicroPhar    symbol66
#define ra_one_is_three    symbol67
#define ra_one_is_not_three    symbol68
#define ra_one    symbol69
#define ra_one_minus_7    symbol70

#define reportWatermarkError symbol71

#define delay_function  symbol80
#define unpack_string   symbol81
#define get_env   symbol82


/* following are for NT */

#define fullTestSuperpro symbols101
#define fullTestCplus symbols102
#define testSuperpro symbols103
#define testCplus symbols104
#define reportSentinelError symbols105
#define reportSuperproError symbols106
#define reportCplusError symbols107
#define reportSentinelorActivatorError symbols108

#ifdef CORESKIN
#define dongleIsTimedSuperPro symbols109
#define decrementSuperProTimer symbols110
#endif /* CORESKIN */

#define getSuperproSerialNo symbols111
#define getCplusSerialNo symbols112

#define getSuperproSerialAndSecurityNo symbols113
#define getCplusSerialAndSecurityNo symbols114

#endif /* DEBUG_SYMBOLS */
