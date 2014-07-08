#ifndef __LISTUTIL_H__
#define __LISTUTIL_H__

/*
 * listutil.h List Manipulation utility functions for the GDM
 *
 * $HopeName: SWcoreskin!export:listutil.h(EBDSDK_P.1) $
 *
* Log stripped */


/*
 * To use these functions, we need to represent a list (single or multicolumn)
 * as a linked list - for argument's sake we call it here struct multiList.
 * The requirement for this structure is, that each row is a structure, with
 * the first 2 fields, pointers to struct multiList or to the listStruct
 * defined below.
 *
 * The list proper, is linked using the pNext field, and the list that holds
 * the selected items is linked using the pNextSelected field.
 * The client will of course have to allocate memory for each item in the list.
 * To construct the list we use the listAddItem function. The client will
 * also have to declare a pointer to the struct multiList to be used to hold
 * the selected items. This second list can be updated with a call to
 * listUpdateSelectedItems.
 *
 * In the following declarations, ppListStruct is a pointer to the head of the
 * linked list, and ppListStructSelected is a pointer to the head of the
 * linked list that holds the selected items.
 *
 * For an example see gtestdlg.c
 */


#include "coreskin.h"    /* includes std.h guiconfg.h swvalues.h */

typedef struct _listStruct {
  struct _listStruct * pNext ;
  struct _listStruct * pNextSelected ;
} listStruct ;

/* Use this for initializing lists */
extern listStruct listEmpty;

/* Access macros */
#define listGetNext(pList, _type_) ( \
  (_type_)(((listStruct*)(pList))->pNext) \
)

#define listGetNextSelected(pList, _type_) ( \
  (_type_)(((listStruct*)(pList))->pNextSelected) \
)

#ifdef __cplusplus
extern "C" {
#endif

/* Adds item as last item in list */
extern void listAddItem(listStruct ** ppListStruct,
                        listStruct * pItem) ;


/* Adds item in position */
extern void listAddItemPos(listStruct ** ppListStruct,
                           listStruct * pItem,
                           int32 position) ;

/* Removes the selected items */
extern void listRemoveSelectedItems(listStruct ** ppListStruct,
                                    listStruct * pListStructSelected) ;

/* Removes the item from the list. Returns the removed item or NULL if the
 * item was not found
 */
extern listStruct * listRemoveItem(listStruct ** ppListStruct,
                                   listStruct * pItem) ;

/* Removes the item from the list at position. Returns the item
 * that was removed
 */
extern  listStruct * listRemoveItemPos(listStruct ** pplistStruct , int32 position) ;

/*
 * Moves selected items in pplistStructSelected to position.
 * This function will have no effect if the drag is not possible
 * (e.g. dragging a selection to a row within itself).
 */
extern int32 listDragItemPos(listStruct ** ppListStruct,
                             listStruct ** ppListStructSelected,
                             int32 position) ;

/* Checks if the pItem is in the selected list and returns TRUE if
 * found, FALSE otherwise
 */
extern int32 listIsItemSelected(listStruct * pListStructSelected,
                                listStruct * pItem) ;

/* Returns the row at rowIndex */
extern listStruct * listGetListRow(listStruct * pListStruct,
                                   int32 rowIndex) ;

/** Manipulation of Selected List **/
/* Add item as last item in Selected list */
extern void listAddItemToSelectedList(listStruct ** ppListStructSelected,
                               listStruct * pItem) ;

/* Remove the item from the selected list. Returns the removed item or NULL
 * if the item was not found
 */
extern listStruct * listRemoveItemFromSelectedList(listStruct ** ppListStructSelected,
                                            listStruct * pItem) ;

/* Returns the number of items in the list */
extern int32 listCountItems(listStruct * pListStruct);


/* Returns the index of the item in the list, or -1 if the item is not in the list */
extern int32 listGetRowIndex(listStruct * pListStruct, listStruct * pItem);


/* TODO: the following codes are subject to move */

#ifndef __FWDIALOG_H__
struct FwDlgPackage;    /* to avoid including fwdialog.h */
#endif

/* Updates the selected list. Returns the number of selected items.
 * No memory is allocated for the items in ppListStructSelected. They
 * are just the selected items in ppListStruct but linked together
 * using the pNextSelected field of the structure
 */
extern int32 listUpdateSelectedItems(struct FwDlgPackage * pDP,
                                     int32 doobriIndex,
                                     listStruct * pListStruct,
                                     listStruct ** ppListStructSelected) ;

/* Returns selected indexes in the selection_idx array. The client
 * is responsible for allocating and freeing the array
 */
extern void listGetSelectedIndexes(struct FwDlgPackage * pDP,
                                   int32 doobriIndex,
                                   int32 selection_idx[]) ;


#ifdef __cplusplus
}
#endif

#endif /* ! __LISTUTIL_H__ */

/* eof listutil.h */
