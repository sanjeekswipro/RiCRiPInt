/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!src:metrics.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of core metrics interface.
 *
 * This is a first cut of a metrics implementation for internal RIP
 * usage. Please note that metrics are NOT a PS concept hence a C
 * API. If we find that we need a PS interface to handle metrics, then
 * that should be implemented around this API or this implementation
 * should be reworked to make that easier. Currently, the only way to
 * get metric information is to emit the metrics to an open PS file as
 * an XML stream. We use this interface in the nightly regressions
 * which emits the metrics to an XML file which is then opened and
 * processed by the regression server to add meta data to each job for
 * searching purposes. Emitting the metrics as XML makes reading the
 * metrics via this server far easier compared to parsing some
 * arbitrary output in the monitor.
 *
 * All metrics exist within metric groups. There is always at least
 * one metric group active when metrics are being collected. Metric
 * groups can be nested and are "opened" and "closed" via C calls from
 * within the RIP. Metric groups have names and need not be
 * unique. These metric groups are flattened to XML when emitted via
 * the PS operator metricsemit. Example:
 *
 * \verbatim
 * <?xml version="1.0" encoding="utf-8"?>
 * <ScriptWorksMetrics xmlns="http://schemas.globalgraphics.com/metrics/2007/06">
 *   <UsesTransparency type="Boolean" value="true"/>
 *   <Page>
 *     <NumberOfGlyphs type="Integer" value="20"/>
 *     <NumberOfFonts type="Integer" value="20"/>
 *     <NumberOfImages type="Integer" value="10"/>
 *   </Page>
 *   <Page>
 *     <NumberOfGlyphs type="Integer" value="5"/>
 *     <NumberOfFonts type="Integer" value="6"/>
 *     <NumberOfImages type="Integer" value="7"/>
 *   </Page>
 * </ScriptWorksMetrics>
 * \endverbatim
 */

#include "core.h"
#include "coreinit.h"
#include "group.h"
#include "fileio.h"
#include "metrics.h"
#include "objnamer.h"
#include "swdataimpl.h"
#include "namedef_.h"
#include "hqmemset.h"

#include "dicthash.h"
#include "hqmemcpy.h"
#include "mmcompat.h"   /* mm_alloc_with_header etc.. */
#include "stacks.h"
#include "swcopyf.h"
#include "swerrors.h"
#include "utils.h"      /* get1B */

#include <stdarg.h>
#include <math.h>

/** Are we collecting metrics? If FALSE, all exported function calls
   are noops. */
static Bool metrics_collection_on = FALSE ;

/* Be careful to keep structure 32bit aligned. */
struct sw_metric {
  const char *name ;
  uint32 name_len ;
  sw_metric *next ;
  sw_datum datum ;
} ;

/* Be careful to keep structure 32bit aligned. */
struct sw_metrics_group {
  const char *name ;
  uint32 name_len ;

  const char *type ;

  sw_metric *metrics ;
  sw_metric **metrics_insert ;

  sw_metrics_group *next ;
  sw_metrics_group *sub_group ;
  sw_metrics_group **sub_group_insert ;
  sw_metrics_group *parent_group ;

  OBJECT_NAME_MEMBER
} ;

#define SW_METRICS_GROUP_NAME "Metrics group"

#define SW_METRICS_DOCUMENT_ELEMENT "ScriptWorksMetrics"

#define SW_METRICS_NAMESPACE "http://schemas.globalgraphics.com/metrics/2007/06"
/** The root of the metrics tree. When called for update and reset, the
    metrics group is always the root. */
static sw_metrics_group root_metric_group = {
  METRIC_NAME_AND_LENGTH(SW_METRICS_DOCUMENT_ELEMENT), /* name, name_len */
  NULL, /* type */
  NULL, /* metrics */
  NULL, /* metric_insert */
  NULL, /* next */
  NULL, /* sub_group */
  NULL, /* sub_group_insert */
  NULL /* parent_group */
} ;

static sw_metric static_null_metric = {
  METRIC_NAME_AND_LENGTH(""), /* name, name_len */
  NULL, /* next */
  SW_DATUM_NOTHING
} ;

/* Allow for the end element tag which needs 6 extra chars:
     NUL and new line(CRLF). i.e. "< / > CR LF NUL"
   For values need 18 extra chars:
     < SPC type="" SPC value=""
   First group output needs 8 extra chars for namespace declaration:
     xmlns=""
   Largest extra chars = 10
 */
#define LARGEST_EXTRA_CHARS 18

/* Write a formatted string to a file. */
static Bool sw_metrics_writef(FILELIST *flptr, char *format, ...)
{
  uint8 temp_outbuf[SW_METRIC_MAX_OUT_LENGTH + LARGEST_EXTRA_CHARS] ;

  va_list ap ;
  va_start(ap, format) ;
  vswcopyf(temp_outbuf, (uint8 *)format, ap) ;
  va_end(ap) ;

  return file_write(flptr, temp_outbuf, strlen_int32((char *)temp_outbuf)) ;
}

static Bool emit_spaces(FILELIST *flptr, uint32 depth)
{
#define NUM_STATIC_SPACES 80
#define INDENT 2
                                            /* 12345678901234567890 */ /* 20 spaces */
  static char spaces[NUM_STATIC_SPACES + 1] = "                    "
                                              "                    "
                                              "                    "
                                              "                    " ;
  uint32 indent = depth * 2 ;

  HQASSERT(strlen(spaces) == NUM_STATIC_SPACES, "spaces length is wrong") ;

  /* Give up on indentation if we have a depth of more than 80. */
  if (indent <= NUM_STATIC_SPACES) {
    if (! file_write(flptr, (uint8*)spaces, indent))
      return FALSE ;
  }
  return TRUE ;
}

static Bool emit_metrics(FILELIST *flptr,
                         const sw_metric *metric,
                         uint32 depth)
{
  HQASSERT(flptr != NULL, "flptr is equal to NULL") ;
  HQASSERT(metric != NULL, "next is equal to NULL") ;

  for (; metric != NULL; metric = metric->next) {
    HQASSERT(metric->name != NULL, "metric name is NULL") ;
    /* It should be impossible to create a metric name which is too
       long. */
    HQASSERT(metric->name_len <= SW_METRIC_MAX_OUT_LENGTH,
             "metric name too long") ;

    /* Skip unset metrics completely */
    if ( metric->datum.type == SW_DATUM_TYPE_NOTHING )
      continue ;

    if (! emit_spaces(flptr, depth))
      return FALSE ;

    /* emit metric as a single line - be VERY careful to make sure
       temp_outbuf is large enough for the extra chars. See comment
       around temp_outbuf. */
    if (! sw_metrics_writef(flptr, "<%s ", metric->name))
      return FALSE ;

    switch (metric->datum.type) {
    case SW_DATUM_TYPE_NULL:
      /* empty value */
      break ;
    case SW_DATUM_TYPE_BOOLEAN:
      if (! file_write(flptr, STRING_AND_LENGTH("type=\"Boolean\" value=\"")))
        return FALSE ;

      if (metric->datum.value.boolean) {
        if (! file_write(flptr, STRING_AND_LENGTH("true")))
          return FALSE ;
      } else {
        if (! file_write(flptr, STRING_AND_LENGTH("false")))
          return FALSE ;
      }
      break ;

    case SW_DATUM_TYPE_INTEGER:
      if (! file_write(flptr, STRING_AND_LENGTH("type=\"Integer\" value=\"")))
        return FALSE ;

      if (! sw_metrics_writef(flptr, "%d", metric->datum.value.integer))
        return FALSE ;
      break ;

    case SW_DATUM_TYPE_FLOAT:
      if (! file_write(flptr, STRING_AND_LENGTH("type=\"Float\" value=\"")))
        return FALSE ;

      if (! sw_metrics_writef(flptr, "%f", metric->datum.value.real))
        return FALSE ;
      break ;

    case SW_DATUM_TYPE_STRING:
      if (! file_write(flptr, STRING_AND_LENGTH("type=\"String\" value=\"")))
        return FALSE ;

      if (! sw_metrics_writef(flptr, "%.*s",
                              CAST_SIZET_TO_INT32(metric->datum.length),
                              metric->datum.value.string))
        return FALSE ;
      break ;

    case SW_DATUM_TYPE_ARRAY:
      { /* Arrays of integers are used for histograms. Represent the values
           as integer elements. */
        const sw_datum *array = metric->datum.value.opaque ;
        size_t length = metric->datum.length ;

        if (! file_write(flptr, STRING_AND_LENGTH("type=\"Array\" length=\"")) ||
            ! sw_metrics_writef(flptr, "%d\" value=\"", CAST_SIZET_TO_INT32(length)) )
          return FALSE ;

        for ( ; length > 0 ; ++array, --length ) {
          if ( array->type == SW_DATUM_TYPE_INTEGER ) {
            if (! sw_metrics_writef(flptr, "%d ", array->value.integer))
              return FALSE ;
          } else {
            HQASSERT(array->type == SW_DATUM_TYPE_FLOAT,
                     "Metrics array element is not an integer or a float") ;
            if (! sw_metrics_writef(flptr, "%f ", array->value.real))
              return FALSE ;
          }
        }
      }
      break ;
    default:
      return FAILURE(FALSE) ;
    }

    if (! file_write(flptr, STRING_AND_LENGTH("\"/>\n")))
      return FALSE ;
  }

  return TRUE ;
}

static Bool emit_groups(FILELIST *flptr,
                        sw_metrics_group *group,
                        uint32 group_depth)
{
  HQASSERT(flptr != NULL, "flptr is equal to NULL") ;
  HQASSERT(group != NULL, "No group") ;

  for (; group != NULL; group = group->next) {

    HQASSERT(group->name != NULL, "group name is NULL") ;
    /* It should be impossible to create a group name which is too
       long. */

    if (! emit_spaces(flptr, group_depth))
      return FALSE ;

    /* emit start group element */
    HQASSERT(group->name_len <= SW_METRIC_MAX_OUT_LENGTH, "group name too long") ;
    if (! sw_metrics_writef(flptr, "<%s", group->name))
      return FALSE ;

    if (group_depth == 0) {
      HQASSERT(strlen(SW_METRICS_NAMESPACE) <= SW_METRIC_MAX_OUT_LENGTH, "namespace too long") ;
      if (! sw_metrics_writef(flptr, " xmlns=\"%s\"", SW_METRICS_NAMESPACE))
        return FALSE ;
    }

    if ( group->type != NULL &&
         ! sw_metrics_writef(flptr, " type=\"%s\"", group->type))
      return FALSE ;

    if (! sw_metrics_writef(flptr, ">\n"))
      return FALSE ;

    if (group->metrics != NULL &&
        ! emit_metrics(flptr, group->metrics, group_depth + 1))
      return FALSE ;

    /* recursively output any child subgroups */
    if (group->sub_group != NULL &&
        ! emit_groups(flptr, group->sub_group, group_depth + 1))
      return FALSE ;

    if (! emit_spaces(flptr, group_depth))
      return FALSE ;

    /* emit end group element */
    if (! sw_metrics_writef(flptr, "</%s>\n", group->name))
      return FALSE ;
  }

  return TRUE ;
}

static void deallocate_metrics(sw_metrics_group *group)
{
  VERIFY_OBJECT(group, SW_METRICS_GROUP_NAME) ;

  group->metrics_insert = &group->metrics ;
  while ( group->metrics != NULL ) {
    sw_metric *metric = group->metrics ;
    group->metrics = metric->next ;

    switch ( metric->datum.type ) {
    case SW_DATUM_TYPE_STRING:
      if ( metric->datum.length > 0 ) {
        mm_free(mm_pool_temp, (mm_addr_t)metric->datum.value.string,
                metric->datum.length) ;
        metric->datum.value.string = NULL ;
        metric->datum.length = 0 ;
      }
      break ;
    case SW_DATUM_TYPE_ARRAY:
      if ( metric->datum.length > 0 ) {
        mm_free(mm_pool_temp, (mm_addr_t)metric->datum.value.opaque,
                metric->datum.length * sizeof(sw_datum)) ;
        metric->datum.value.opaque = NULL ;
        metric->datum.length = 0 ;
      }
      break ;
    }

    mm_free_with_header(mm_pool_temp, metric) ;
  }
}

static void deallocate_sub_groups(sw_metrics_group *group)
{
  VERIFY_OBJECT(group, SW_METRICS_GROUP_NAME) ;

  group->sub_group_insert = &group->sub_group ;
  while ( group->sub_group != NULL ) {
    sw_metrics_group *subgroup = group->sub_group ;
    group->sub_group = subgroup->next ;

    VERIFY_OBJECT(subgroup, SW_METRICS_GROUP_NAME) ;

    deallocate_metrics(subgroup) ;
    deallocate_sub_groups(subgroup) ;

    UNNAME_OBJECT(subgroup) ;

    mm_free_with_header(mm_pool_temp, subgroup) ;
  }
}

/* ============================================================================
 * Public interface.
 * ============================================================================
 */

/** Registered modular metrics handling. */
static sw_metrics_callbacks *metric_callback_list = NULL ;

/** Call all registered modular metrics updaters for an update. */
static Bool sw_metrics_update(sw_metrics_group *group)
{
  sw_metrics_callbacks *funcs ;

  /* Verifying the root group to make sure we're called late enough. */
  VERIFY_OBJECT(&root_metric_group, SW_METRICS_GROUP_NAME) ;

  for ( funcs = metric_callback_list ; funcs ; funcs = funcs->next ) {
    HQASSERT(funcs->update, "No metrics update function") ;
    if ( !(*funcs->update)(group) )
      return FAILURE(FALSE) ;
  }

  return TRUE ;
}

void sw_metrics_reset(int reason)
{
  sw_metrics_callbacks *funcs ;

  /* Verifying the root group to make sure we're called late enough. */
  VERIFY_OBJECT(&root_metric_group, SW_METRICS_GROUP_NAME) ;

  for ( funcs = metric_callback_list ; funcs ; funcs = funcs->next ) {
    HQASSERT(funcs->reset, "No metrics reset function") ;
    (*funcs->reset)(reason) ;
  }
}

void sw_metrics_register(sw_metrics_callbacks *funcs)
{
  HQASSERT(funcs, "No metrics callbacks to register") ;
  HQASSERT(funcs->update, "No metrics update callback") ;
  HQASSERT(funcs->reset, "No metrics reset callback") ;
  /* Verifying the root group to make sure we're called late enough. */
  VERIFY_OBJECT(&root_metric_group, SW_METRICS_GROUP_NAME) ;
  funcs->next = metric_callback_list ;
  metric_callback_list = funcs ;
}

/** Common core for metrics int/float/bool reporting. This function tries to
    find an existing entry for the metric name, and sets it if it exists. If
    not, it tries to create a new entry for the metric. */
static inline Bool sw_metric_common(sw_metrics_group *group,
                                    const char *name, size_t name_len,
                                    sw_datum datum)
{
  sw_metric *metric = sw_metric_get(group, name, name_len);
  if ( metric == NULL ) {
    metric = sw_metric_create(group, name, name_len);
    if (metric == NULL)
      return FAILURE(FALSE);
  }
  return sw_metric_set_value(metric, datum) ;
}

Bool sw_metric_integer(sw_metrics_group *metrics, const char *name,
                       size_t name_len, int32 value)
{
  sw_datum datum = SW_DATUM_INTEGER(0) ;

  datum.value.integer = value;

  return sw_metric_common(metrics, name, name_len, datum) ;
}

Bool sw_metric_float(sw_metrics_group *metrics, const char *name,
                     size_t name_len, float value)
{
  sw_datum datum = SW_DATUM_FLOAT(SW_DATUM_0_0F) ;

  datum.value.real = value;

  return sw_metric_common(metrics, name, name_len, datum) ;
}

Bool sw_metric_boolean(sw_metrics_group *metrics, const char *name,
                       size_t name_len, Bool value)
{
  sw_datum datum = SW_DATUM_BOOLEAN(FALSE) ;

  datum.value.boolean = value;

  return sw_metric_common(metrics, name, name_len, datum) ;
}

static void init_C_globals_metrics(void)
{
  metrics_collection_on = FALSE ;

  root_metric_group.metrics = NULL ;
  root_metric_group.metrics_insert = &root_metric_group.metrics ;
  root_metric_group.next = NULL ;
  root_metric_group.sub_group = NULL ;
  root_metric_group.sub_group_insert = &root_metric_group.sub_group ;
  root_metric_group.parent_group = NULL ;

  NAME_OBJECT(&root_metric_group, SW_METRICS_GROUP_NAME) ;

  metric_callback_list = NULL ;
}

/* Metrics initialisation. */
static Bool sw_metrics_postboot(void)
{
  /* This function exists solely to get the finish routine to run. */
  return TRUE ;
}

/* Metrics shutdown. */
static void sw_metrics_finish(void)
{
  HQASSERT(root_metric_group.next == NULL,
           "Root metrics group should have no siblings") ;
  HQASSERT(root_metric_group.parent_group == NULL,
           "Root metrics group should have no parent") ;

  /* Free root metrics. */
  deallocate_metrics(&root_metric_group) ;

  /* Free all sub groups */
  deallocate_sub_groups(&root_metric_group) ;

  UNNAME_OBJECT(&root_metric_group) ;
}

IMPORT_INIT_C_GLOBALS( jobmetrics )

/** Module runtime initialisation */
void sw_metrics_C_globals(core_init_fns *fns)
{
  init_C_globals_metrics() ;
  fns->postboot = sw_metrics_postboot ;
  fns->finish = sw_metrics_finish ;
}

Bool sw_metrics_emit(FILELIST *flptr)
{
  /* Having no metrics is not an error, just a noop. */
  if (! metrics_collection_on)
    return TRUE ;

  HQASSERT(flptr != NULL, "flptr is equal to NULL") ;

  if ( !sw_metrics_update(&root_metric_group) )
    return FALSE ;

  if (! file_write(flptr, STRING_AND_LENGTH("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")) )
    return FALSE ;

  return emit_groups(flptr, &root_metric_group, 0 /* first group */) ;
}

Bool sw_metrics_open_group(sw_metrics_group **group,
                           const char *group_name, size_t group_name_len)
{
  sw_metrics_group *new_group, *parent ;

  HQASSERT(group != NULL, "Nowhere to find old/put new group") ;

  parent = *group ;
  VERIFY_OBJECT(parent, SW_METRICS_GROUP_NAME) ;

  HQASSERT(group_name != NULL, "group_name is NULL") ;
  HQASSERT(strlen(group_name) == group_name_len, "Wrong group name length") ;

  /* Try to find group in existing group's sub-groups. */
  for ( new_group = parent->sub_group ; new_group != NULL ; new_group = new_group->next ) {
    VERIFY_OBJECT(new_group, SW_METRICS_GROUP_NAME) ;
    if ( new_group->name_len == group_name_len &&
         strcmp(new_group->name, group_name) == 0 ) {
      *group = new_group ;
      return TRUE ;
    }
  }

  /* Not found, we have to create it */
  /* Allocate memory for stucture and strings (with NUL termination)
     in one go. */
  if ( (new_group = mm_alloc_with_header(mm_pool_temp,
                                         sizeof(sw_metrics_group) +
                                         group_name_len + 1,
                                         MM_ALLOC_CLASS_METRICS)) == NULL)
    return FAILURE(FALSE) ;

  /* Copy name and description. */
  new_group->name = (char*)new_group + sizeof(sw_metrics_group) ;
  HqMemCpy(new_group->name, group_name, group_name_len + 1) ;
  new_group->name_len = CAST_SIZET_TO_UINT32(group_name_len) ;
  new_group->type = NULL ;

  new_group->metrics = NULL ;
  new_group->metrics_insert = &new_group->metrics ;
  new_group->sub_group = NULL ;
  new_group->sub_group_insert = &new_group->sub_group ;
  new_group->parent_group = parent ;

  /* Maintain linked lists. */
  new_group->next = NULL ; /* Always add to the end of the list. */
  NAME_OBJECT(new_group, SW_METRICS_GROUP_NAME) ;

  /* Link into parent's sub-group chain. */
  *(parent->sub_group_insert) = new_group ;
  parent->sub_group_insert = &new_group->next ;

  /* Navigate to new group. */
  *group = new_group ;

  return TRUE ;
}

void sw_metrics_close_group(sw_metrics_group **group)
{
  HQASSERT(group, "Nowhere to find group") ;
  VERIFY_OBJECT(*group, SW_METRICS_GROUP_NAME) ;

  HQASSERT((*group)->parent_group != NULL, "Navigating past root of metrics") ;
  *group = (*group)->parent_group ;
}

sw_metric* sw_metric_create(sw_metrics_group *group,
                            const char *metric_name, size_t metric_name_len)
{
  sw_metric *new_metric ;

  /* Having no metrics is not an error, just a noop. */
  if (! metrics_collection_on)
    return &static_null_metric ;

  VERIFY_OBJECT(group, SW_METRICS_GROUP_NAME) ;
  HQASSERT(metric_name != NULL, "metric_name is NULL") ;
  HQASSERT(strlen(metric_name) == metric_name_len, "Wrong metric name length") ;

  /* Allocate memory for structure (with NUL termination for the name) in one
     go. */
  if ( (new_metric = mm_alloc_with_header(mm_pool_temp,
                                          sizeof(sw_metric) +
                                          metric_name_len + 1,
                                          MM_ALLOC_CLASS_METRICS)) == NULL)
    return NULL ;

  /* Copy name. */
  new_metric->name = (char*)new_metric + sizeof(sw_metric) ;
  HqMemCpy(new_metric->name, metric_name, metric_name_len + 1) ;
  new_metric->name_len = CAST_SIZET_TO_UINT32(metric_name_len) ;
  new_metric->datum = static_null_metric.datum ;

  /* Insert the new metric into the group list. */
  new_metric->next = NULL ;
  *(group->metrics_insert) = new_metric ;
  group->metrics_insert = &new_metric->next ;

  return new_metric ;
}

sw_metric *sw_metric_get(sw_metrics_group *group,
                         const char *metric_name, size_t metric_name_len)
{
  sw_metric *scan ;

  VERIFY_OBJECT(group, SW_METRICS_GROUP_NAME) ;
  HQASSERT(metric_name != NULL, "metric_name is NULL") ;
  HQASSERT(strlen(metric_name) == metric_name_len, "Wrong metric name length") ;

  for ( scan = group->metrics; scan != NULL ; scan = scan->next ) {
    if (scan->name_len == metric_name_len &&
        strcmp(metric_name, scan->name) == 0)
      return scan;
  }

  return NULL;
}

sw_datum sw_metric_get_value(const sw_metric* metric)
{
  HQASSERT(metric != NULL, "metric is NULL") ;

  return metric->datum ;
}

Bool sw_metric_set_value(sw_metric* metric, sw_datum value)
{
  HQASSERT(metric != NULL, "metric is NULL") ;

  /* If set, a metrics value cannot be changed */
  if ( value.type != metric->datum.type &&
       metric->datum.type != SW_DATUM_TYPE_NOTHING )
    return FAILURE(FALSE) ;

  /* Free old storage for string if we cannot reuse it. */
  if ( value.length != metric->datum.length ) {
    if ( metric->datum.length > 0 ) {
      HQASSERT(metric->datum.type == SW_DATUM_TYPE_STRING,
               "Stored metric type not known") ;
      mm_free(mm_pool_temp, (mm_addr_t)metric->datum.value.string,
              metric->datum.length) ;
      metric->datum.length = 0 ;
      metric->datum.value.string = NULL ;
    }
  }

  switch ( value.type ) {
  case SW_DATUM_TYPE_NULL:
  case SW_DATUM_TYPE_BOOLEAN:
  case SW_DATUM_TYPE_INTEGER:
  case SW_DATUM_TYPE_FLOAT: /* simple types to handle */
    metric->datum = value ;
    break ;
  case SW_DATUM_TYPE_STRING:
    if ( value.length > 0 ) {
      /* Allocate string, if we don't have one */
      if ( metric->datum.length == 0 ) {
        if ( (metric->datum.value.string = mm_alloc(mm_pool_temp, value.length,
                                                    MM_ALLOC_CLASS_METRICS)) == NULL )
          return FAILURE(FALSE) ;
        metric->datum.length = value.length ;
      }
      HQASSERT(value.length == metric->datum.length, "Metric string is wrong length") ;
      HqMemCpy(metric->datum.value.string, value.value.string, value.length) ;
    }
    break ;
  default:
    /* It will not be possible to set a metric with any other type. */
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}

/** \brief Produce a PostScript dictionary containing of the metrics. */
static Bool sw_metrics_dictionary(sw_metrics_group *group, OBJECT *thed)
{
  OBJECT nameo = OBJECT_NOTVM_NAME(NAME_false, LITERAL) ;
  int32 length = 0 ;
  sw_metric *metric ;
  sw_metrics_group *subgroup ;

  /* Count direct metrics */
  for ( metric = group->metrics ; metric ; metric = metric->next )
    ++length ;

  /* Count sub-groups metrics */
  for ( subgroup = group->sub_group ; subgroup ; subgroup = subgroup->next )
    ++length ;

  if ( group->type != NULL )
    ++length ;

  if ( ! ps_dictionary(thed, length))
    return FALSE ;

  if ( group->type != NULL ) {
    /* Swap /false for the actual metric name */
    if ( (oName(nameo) = cachename((const uint8 *)group->type,
                                   (uint32)strlen(group->type))) == NULL )
      return FALSE ;

    if ( !fast_insert_hash_name(thed, NAME_Type, &nameo) )
      return FALSE ;
  }

  for ( metric = group->metrics ; metric ; metric = metric->next ) {
    OBJECT datao = OBJECT_NOTVM_NULL;

    /* Skip unset metrics, ignoring the wasted dictionary space. */
    if ( metric->datum.type == SW_DATUM_TYPE_NOTHING )
      continue ;

    /* Swap /false for the actual metric name */
    if ( (oName(nameo) = cachename((const uint8 *)metric->name,
                                   metric->name_len)) == NULL )
      return FALSE ;

    if ( object_from_swdatum(&datao, &metric->datum) != SW_DATA_OK )
      return FALSE ;

    if ( !fast_insert_hash(thed, &nameo, &datao) )
      return FALSE ;
  }

  for ( subgroup = group->sub_group ; subgroup ; subgroup = subgroup->next ) {
    OBJECT subdict = OBJECT_NOTVM_NULL;

    /* Swap /false for the actual subgroup name */
    if ( (oName(nameo) = cachename((const uint8 *)subgroup->name,
                                   subgroup->name_len)) == NULL )
      return FALSE ;

    if ( !sw_metrics_dictionary(subgroup, &subdict) )
      return FALSE ;

    if ( !fast_insert_hash(thed, &nameo, &subdict) )
      return FALSE ;
  }

  return TRUE ;
}

/* ============================================================================
 * Histogram support.
 * ============================================================================
 */

void sw_metric_histogram_reset(sw_metric_histogram_base *hist,
                               size_t size, double base,
                               double domain_lo, double domain_hi)
{
  HQASSERT(hist, "No metrics histogram to reset") ;
  HQASSERT(size > 0, "No buckets for histogram") ;
  HQASSERT(base >= 1.0, "Invalid base for histogram") ;
  HQASSERT(domain_lo < domain_hi, "Empty domain for metrics histogram") ;

  hist->length = size ;
  hist->base = base ;
  hist->domain_lo = domain_lo ;
  hist->domain_hi = domain_hi ;

  /* Compute the scales. We use the identity:

     log_b(x) = log_n(x) / log_n(b)

     To compute the log mapping.

     The input value is in the domain [domain_lo,domain_hi[ (using ISO 31-11
     range notation). We map this to the range [1,domain_hi-domain_lo+1[
     before taking the logarithm, and because log_b(1) == 0 for all b, this
     will yield a value in the range [0,log_b(domain_hi-domain_lo+1)[. We
     want to divide and quantise this to get a bucket index in the range
     [0..length[. We compute a multiplicative scale factor so we can perform
     one log() operation, one multiply, and a conversion to integer. The
     scale factor is:

       length/(log_b(domain_hi-domain_lo+1) * log_n(b))

     which simplifies via:

       length/(log_n(domain_hi-domain_lo+1) / log_n(b) * log_n(b))

     to:

       length/log_n(domain_hi-domain_lo+1)
  */
  if ( base == 1.0 ) { /* Linear is a special case. */
    hist->scale = (double)size / (domain_hi - domain_lo) ;
  } else {
    hist->scale = (double)size / log(domain_hi - domain_lo + 1.0) ;
  }

  /* Clear all of the counters */
  hist->lower = 0 ;
  hist->higher = 0 ;
  HqMemZero(hist + 1, size * sizeof(int32)) ;
}

void sw_metric_histogram_count(sw_metric_histogram_base *hist,
                               double value, int32 amount)
{
  HQASSERT(hist, "No metrics histogram to count") ;
  HQASSERT(hist->length > 0, "Histogram has zero length") ;
  HQASSERT(hist->scale > 0, "Histogram has zero scale") ;
  HQASSERT(hist->domain_lo < hist->domain_hi, "Histogram domain too small") ;

  if (! metrics_collection_on)
    return ;

  if ( value < hist->domain_lo ) {
    hist->lower += amount ;
  } else if ( value >= hist->domain_hi ) {
    hist->higher += amount ;
  } else if ( hist->base == 1.0 ) {
    /* Special case for linear optimises out log() */
    /* Assume the bucket storage follows the histogram structure directly,
       and is aligned adequately. */
    int32 *buckets = (int32 *)(hist + 1) ;
    uint32 index = (uint32)((value - hist->domain_lo) * hist->scale) ;
    HQASSERT(index >= 0 && index < hist->length,
             "Histogram index out of range") ;
    buckets[index] += amount ;
  } else {
    /* Assume the bucket storage follows the histogram structure directly,
       and is aligned adequately. */
    int32 *buckets = (int32 *)(hist + 1) ;
    size_t index = (size_t)(log(value - hist->domain_lo + 1.0) * hist->scale
                            /* We add a very tiny epsilon before quantising,
                               because the precision of the log() and scale
                               can result in slight miscounting of common
                               log_2 scales. The epsilon is 25 binary digits,
                               so when the number is converted to a float, it
                               won't be noticable. */
                            + 2.9802322387695313e-008) ;
    HQASSERT(index >= 0 && index < hist->length,
             "Histogram index out of range") ;
    buckets[index] += amount ;
  }
}

Bool sw_metric_histogram(sw_metrics_group *group,
                         const char *name, size_t name_len,
                         sw_metric_histogram_base *hist)
{
  int32 *buckets ;
  sw_datum *array ;
  size_t length ;
  sw_metric *metric ;

  HQASSERT(hist, "No metrics histogram to count") ;
  HQASSERT(hist->length > 0, "Histogram has zero length") ;
  HQASSERT(hist->scale > 0, "Histogram has zero scale") ;
  HQASSERT(hist->domain_lo < hist->domain_hi, "Histogram domain too small") ;

  /* A histogram is represented by a group, containing the appropriate
     parameters. */
  if ( !sw_metrics_open_group(&group, name, name_len) )
    return FALSE ;

  /* Histograms have a group type */
  group->type = "Histogram" ;

  metric = sw_metric_get(group, METRIC_NAME_AND_LENGTH("Values"));
  if ( metric == NULL ) {
    metric = sw_metric_create(group, METRIC_NAME_AND_LENGTH("Values"));
    if (metric == NULL)
      return FAILURE(FALSE);
  }

  /* Can't change type of a metric, unless it's uninitialised. */
  if ( metric->datum.type == SW_DATUM_TYPE_NOTHING ) {
    if ( (array = mm_alloc(mm_pool_temp, hist->length * sizeof(sw_datum),
                           MM_ALLOC_CLASS_METRICS)) == NULL )
      return FAILURE(FALSE) ;

    metric->datum.type = SW_DATUM_TYPE_ARRAY ;
    metric->datum.value.opaque = array ;
    metric->datum.length = length = hist->length ;

    for ( ; length > 0 ; --length, ++array ) {
      array->type = SW_DATUM_TYPE_INTEGER ;
      array->owner = NULL ;
      array->length = 0 ;
      array->value.integer = 0 ;
    }
  } else if ( metric->datum.type != SW_DATUM_TYPE_ARRAY ||
              metric->datum.length != hist->length ) {
    /* We cannot change the type of an object, or change the size of a
       histogram. */
    return FAILURE(FALSE) ;
  }

  /* Assume the bucket storage follows the histogram structure directly,
     and is aligned adequately. */
  buckets = (int32 *)(hist + 1) ;
  array = (sw_datum *)metric->datum.value.opaque ;
  length = metric->datum.length ;

  while ( length > 0 ) {
    --length ;
    HQASSERT(array[length].type == SW_DATUM_TYPE_INTEGER,
             "Metrics array is no longer integer") ;
    array[length].value.integer = buckets[length] ;
  }

  /* Create a Bounds array, with either all integers or all floats,
     describing the range of each bucket. */
  metric = sw_metric_get(group, METRIC_NAME_AND_LENGTH("Bounds"));
  if ( metric == NULL ) {
    size_t i ;
    Bool allint ;
    double rangesize, range_lo, domainsize ;

    metric = sw_metric_create(group, METRIC_NAME_AND_LENGTH("Bounds"));
    if (metric == NULL)
      return FAILURE(FALSE);

    /* Bounds size is one more than histogram length, to include upper
       limit. */
    length = hist->length ;

    /* Bounds are only created once. */
    if ( (array = mm_alloc(mm_pool_temp, (length + 1) * sizeof(sw_datum),
                           MM_ALLOC_CLASS_METRICS)) == NULL )
      return FAILURE(FALSE) ;

    metric->datum.type = SW_DATUM_TYPE_ARRAY ;
    metric->datum.value.opaque = array ;
    metric->datum.length = length + 1 ;

    domainsize = hist->domain_hi - hist->domain_lo ;
    if ( hist->base == 1.0 ) {
      range_lo = 0.0 ;
      rangesize = (double)length ;
    } else {
      range_lo = 1.0 ;
      rangesize = pow(hist->base, (double)length) - range_lo ;
    }

    /* Initialise int tracker to indicate if the domain can be integers. */
    allint = (intrange(hist->domain_lo) && intrange(hist->domain_hi)) ;
    for ( i = 0 ; i <= length ; ++i ) {
      double bound ;

      if ( hist->base == 1.0 ) { /* Optimised case for linear mapping */
        bound = (double)i * domainsize / rangesize + hist->domain_lo ;
      } else {
        bound = (pow(hist->base, (double)i) - range_lo) * domainsize / rangesize + hist->domain_lo ;
      }

      if ( allint && (int32)bound == bound ) {
        array[i].type = SW_DATUM_TYPE_INTEGER ;
        array[i].value.integer = (int32)bound ;
      } else {
        if ( allint ) { /* Go back and convert previous values to float */
          size_t j ;
          for ( j = 0 ; j < i ; ++j ) {
            array[j].type = SW_DATUM_TYPE_FLOAT ;
            array[j].value.real = (float)array[j].value.integer ;
          }
          allint = FALSE ;
        }
        array[i].type = SW_DATUM_TYPE_FLOAT ;
        array[i].value.real = (float)bound ;
      }
      array[i].owner = NULL ;
      array[i].length = 0 ;
    }
  }

  /* Base can be shown as an integer or float. */
  if ( intrange(hist->base) && (int32)hist->base == hist->base ) {
    if ( !sw_metric_integer(group, METRIC_NAME_AND_LENGTH("Base"), (int32)hist->base) )
      return FALSE ;
  } else {
    if ( !sw_metric_float(group, METRIC_NAME_AND_LENGTH("Base"), (float)hist->base) )
      return FALSE ;
  }

  /* Below and above are counters. */
  if ( !sw_metric_integer(group, METRIC_NAME_AND_LENGTH("BelowBounds"), hist->lower) ||
       !sw_metric_integer(group, METRIC_NAME_AND_LENGTH("AboveBounds"), hist->higher) )
    return FALSE ;

  return TRUE ;
}

/* ============================================================================
 * Hash support. Key is a uintptr_t. This means that the hash values can be
 * pointers should that be useful.
 * ============================================================================
 */
struct sw_metric_hash_entry {
  uintptr_t hashval ;
  struct sw_metric_hash_entry *next ; /* next in hash chain */
  uint32 counter ;
} ;

struct sw_metric_hashtable {
  size_t size ; /* size hash table */
  size_t num_entries ;
  sw_metric_hash_callback_funct_t hashfunct ;
  sw_metric_deallocate_hashkey_callback_funct_t deallocate_hashkey_funct ;

  struct sw_metric_hash_entry **table ;
} ;

void sw_metric_hashtable_reset(sw_metric_hashtable *hashtable)
{
  size_t i ;

  HQASSERT(hashtable != NULL, "hashtable is NULL") ;

  for (i=0; i < hashtable->size; i++) {
    while (hashtable->table[i] != NULL) {
      struct sw_metric_hash_entry *entry = hashtable->table[i] ;
      hashtable->table[i] = entry->next ;

      if (hashtable->deallocate_hashkey_funct != NULL)
        (hashtable->deallocate_hashkey_funct)(entry->hashval) ;
      mm_free_with_header(mm_pool_temp, (mm_addr_t)entry) ;

      hashtable->num_entries-- ;
    }
  }
  HQASSERT(hashtable->num_entries == 0, "Number of hash entries is incorrect.") ;
}

Bool sw_metric_hashtable_create(sw_metric_hashtable **hashtable,
                                size_t hash_table_size,
                                sw_metric_hash_callback_funct_t hashfunct,
                                sw_metric_deallocate_hashkey_callback_funct_t deallocate_hashkey_funct)
{
  size_t i ;
  uint8 *mem_block ;

  HQASSERT(hashtable != NULL, "hashtable pointer is NULL") ;
  HQASSERT(hashfunct != NULL, "hashfunct is NULL") ;

  /* Allocate top level structure and table in one allocation. */
  if ( (mem_block = mm_alloc_with_header(mm_pool_temp,
                                         sizeof(struct sw_metric_hashtable) +
                                         (hash_table_size * sizeof(struct sw_metric_hashtable *)),
                                         MM_ALLOC_CLASS_METRICS)) == NULL) {
    *hashtable = NULL ;
    return FALSE ;
  }

  *hashtable = (struct sw_metric_hashtable *)mem_block ;

  mem_block += sizeof(struct sw_metric_hashtable) ;
  (*hashtable)->table =(struct sw_metric_hash_entry **)mem_block ;

  (*hashtable)->size = hash_table_size ;
  (*hashtable)->num_entries = 0 ;
  (*hashtable)->hashfunct = hashfunct ;
  (*hashtable)->deallocate_hashkey_funct = deallocate_hashkey_funct ;
  for (i=0; i < hash_table_size; i++) {
    (*hashtable)->table[i] = NULL ;
  }
  return TRUE ;
}

Bool sw_metric_hashtable_increment_key_counter(sw_metric_hashtable *hashtable,
                                               uintptr_t hashkey)
{
  uint32 pos ;
  struct sw_metric_hash_entry **insert_pos ;

  HQASSERT(hashtable != NULL, "hashtable is NULL") ;

  pos = (hashtable->hashfunct)(hashkey) ;
  HQASSERT(pos >= 0 && pos < hashtable->size, "pos out of range") ;

  insert_pos = &(hashtable->table[pos]) ;

  while ((*insert_pos) != NULL) {
    /* If we have an entry, increment counter and we are done. */
    if ((*insert_pos)->hashval == hashkey) {
      (*insert_pos)->counter++ ;
      return TRUE ;
    }
    insert_pos = &((*insert_pos)->next) ;
  }

  /* Limit to a int32. Should be more than enough. */
  if (hashtable->num_entries > MAXINT32)
    return TRUE ;

  /* We *must* be at the end of the hash chain, so append the new value. */
  HQASSERT(insert_pos != NULL, "insert_pos is NULL") ;
  HQASSERT(*insert_pos == NULL, "*insert_pos is not NULL") ;

  if ( ((*insert_pos) = mm_alloc_with_header(mm_pool_temp,
                                             sizeof(struct sw_metric_hash_entry),
                                             MM_ALLOC_CLASS_METRICS)) == NULL )
    return FALSE ;
  (*insert_pos)->hashval = hashkey ;
  (*insert_pos)->counter = 1 ;
  (*insert_pos)->next = NULL ;

  /* Overflow is avoided by MAXINT32 check above. */
  hashtable->num_entries++ ;

  return TRUE ;
}

void sw_metric_hashtable_destroy(sw_metric_hashtable **hashtable)
{
  HQASSERT(hashtable != NULL, "hashtable pointer is NULL") ;

  sw_metric_hashtable_reset(*hashtable) ;
  mm_free_with_header(mm_pool_temp, *hashtable) ;
  *hashtable = NULL ;
}

Bool sw_metric_hashtable_enumerate(sw_metric_hashtable *hashtable,
                                   sw_metric_enumerate_callback_funct_t enumeratefunct,
                                   void *data)
{
  size_t i ;

  HQASSERT(hashtable != NULL, "hashtable is NULL") ;

  for (i=0; i < hashtable->size; i++) {
    struct sw_metric_hash_entry *entry = hashtable->table[i] ;
    while (entry != NULL) {
      if (! (enumeratefunct)(entry->hashval, entry->counter, data))
        return FALSE ;
      entry = entry->next ;
    }
  }
  return TRUE ;
}

size_t sw_metric_hashtable_key_count(sw_metric_hashtable *hashtable)
{
  HQASSERT(hashtable != NULL, "hashtable is NULL") ;
  return hashtable->num_entries ;
}

/* ============================================================================
 * PS operators.
 * ============================================================================
 */

/* PS operators to emit metric information.

   Implemented:
     <openfile> metricsemit
     <Boolean> setmetrics
     metricsreset

   NYI:
     currentmetrics
     <name> metricsopengroup
     metricsclosegroup
     <name> <value> metricscreate
     <name> metricsget <value>
     <name> metricstimer <value>
*/

/** Turn metric collection on or off. */
Bool setmetrics_(ps_context_t *pscontext)
{
  Bool value ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( !get1B(&value) )
    return FALSE ;

  metrics_collection_on = value ;

  return TRUE ;
}

/** Reset the values of all metrics to the initial values. */
Bool metricsreset_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  sw_metrics_reset(SW_METRICS_RESET_PS) ;

  return TRUE;
}

/** Emit all the metrics to a file as an XML stream. If we find that we need
    to map metrics to PS then we ought to add additional PS opertors to do
    that. */
Bool metricsemit_(ps_context_t *pscontext)
{
  OBJECT *ofile ;
  FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Need some arguments. */
  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  /* Pick off the top object and check that its a file. */
  ofile = theTop(operandstack) ;
  if (oType(*ofile) != OFILE)
    return error_handler(TYPECHECK) ;

  /* Check its open and writable. */
  flptr = oFile(*ofile) ;
  if (! isIOpenFileFilter(ofile, flptr) || !isIOutputFile(flptr))
    return error_handler(IOERROR) ;

  /* Emit that data man! */
  if (! sw_metrics_emit(flptr))
    return error_handler(IOERROR) ;

  /* Cleanup arguments. */
  pop(&operandstack) ;

  return TRUE ;
}

/** Returns a PS object of all the job level metrics. This is a dumbed
    down PS operator to the metrics interface. If it turns out that we
    want all metrics available in PS, then we will need to decide on
    how to model the groups using PS objects. Currently I don't see a
    need for this but this could be added. */
Bool jobmetrics_(ps_context_t *pscontext)
{
  OBJECT thed = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Having no metrics is not an error, just a noop. */
  if ( metrics_collection_on ) {
    if ( !sw_metrics_update(&root_metric_group) )
      return FALSE ;

    if ( !sw_metrics_dictionary(&root_metric_group, &thed) )
      return FALSE ;
  } else {
    /* Having no metrics is not an error, just a noop. */
    if ( ! ps_dictionary(&thed, 0) )
      return FALSE ;
  }

  return push( &thed , &operandstack ) ;
}

/* ============================================================================
* Log stripped */
