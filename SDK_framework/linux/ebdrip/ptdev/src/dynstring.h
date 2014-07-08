/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWptdev!src:dynstring.h(EBDSDK_P.1) $
 */

/**
 * @file
 * @brief Dynamic string interface.
 */

#include "ptincs.h"


#ifdef INRIP_PTDEV
typedef struct PSString PSString;
#else
typedef PlgFwStrRecord PSString;
#endif


/**
 * \brief Create a new string object.
 *
 * Use \c PSStringClose() to release the string object.
 *
 * \param[out] ppString  Points to a newly allocated string object (or \c NULL
 * if the operation failed).
 * \return \c TRUE on success, \c FALSE otherwise.
 */
int32 PSStringOpen (PSString** ppString);

/**
 * \brief Close a string object, releasing any memory it referenced.
 *
 * \param pString  The string to close.
 *
 * \see PSStringOpen()
 */
void PSStringClose (PSString* pString);

/**
 * \brief Append a single character to the end of a string.
 *
 * \param pString  The string to append to.
 * \param ch  The character to append.
 */
void PSStringAppendChar (PSString* pString, uint8 ch);

/**
 * \brief Append one string to another.
 *
 * \param pString  The string to append to.
 * \param pbz  The characters to append.
 */
void PSStringAppendString (PSString* pString, const char* pbz);

/**
 * \brief Convert a \c double to a string, and append it to another.
 *
 * \param pString  The string to append to.
 * \param fVal  The value to append.
 */
void PSStringAppendDouble (PSString* pString, double fVal);

/**
 * \brief Convert an integer to a string, and append it to another.
 *
 * \param pString  The string to append to.
 * \param nVal  The value to append.
 */
void PSStringAppendInt (PSString* pString, int32 nVal);

/**
 * \brief Copy string content into a newly allocated buffer.
 *
 * \param pString  The string to copy.
 * \return A pointer to newly allocated memory (to be released with
 * \c MemFree()), or \c NULL on error.
 */
char* PSStringCopyBuffer (PSString* pString);


