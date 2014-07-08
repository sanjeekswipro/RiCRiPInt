/* impl.h.table: Interface for a dictionary
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $Id: table.h,v 1.4.1.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!table.h(EBDSDK_P.1) $
 */

#ifndef table_h
#define table_h

#include "mpmtypes.h"
#include <stddef.h>


typedef struct TableStruct *Table;

extern Res TableCreate(Table *tableReturn, size_t length);
extern void TableDestroy(Table table);
extern Res TableDefine(Table table, Word key, void *value);
extern Res TableRedefine(Table table, Word key, void *value);
extern Bool TableLookup(void **valueReturn, Table table, Word key);
extern Res TableRemove(Table table, Word key);
extern size_t TableCount(Table table);
extern void TableMap(Table table,
                     void(*fun)(Word key, void *value, void *data),
                     void *data);


#endif /* table_h */
