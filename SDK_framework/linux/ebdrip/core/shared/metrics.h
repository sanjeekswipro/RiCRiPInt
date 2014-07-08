#ifndef __METRICS_H__
#define __METRICS_H__

/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:metrics.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Metric objects are built up during job execution and can be emitted
 * as XML to a file stream.
 */

#include "swdataapi.h"

struct FILELIST ;
struct OBJECT ;
struct core_init_fns ;

/* The boot functions are available in all builds; in non-metrics variants
   they are stubbed out. */

/** \brief Initialise the metrics system. */
void sw_metrics_C_globals(struct core_init_fns *fns) ;

#ifdef METRICS_BUILD

/** The maximum length of an element name (group or metric). */
#define SW_METRIC_MAX_OUT_LENGTH 1024

/** \brief Opaque type for a single metric.

    Most users will not need to use individual metric objects, the
    convenience functions hide these objects nearly all the time. */
typedef struct sw_metric sw_metric ;

/** \brief Opaque type for a metric hash table.
 */
typedef struct sw_metric_hashtable sw_metric_hashtable ;

/** \brief Opaque type for metrics groups.

    This will only be used for navigating down and up the group tree
    when in an update callback. */
typedef struct sw_metrics_group sw_metrics_group ;

/** \brief Macro to turn constant strings into a form suitable for use in
    name,len arguments for metrics functions. */
#define METRIC_NAME_AND_LENGTH(s_) ""s_"", sizeof(""s_"")-1

/** \brief Reason codes passed to the \c sw_metrics_callbacks::reset callback
    function. */
enum {
  SW_METRICS_RESET_BOOT,     /**< The RIP is starting to boot. */
  SW_METRICS_RESET_POSTBOOT, /**< The RIP has just completed booting. */
  SW_METRICS_RESET_PS        /**< metricsreset PostScript operator was called.. */
} ;

/** \brief Call all registered modular metrics updaters for a reset.

    \param reason One of the SW_METRICS_RESET_* codes, indicating the reason
    for the reset. */
void sw_metrics_reset(int reason) ;

/** \brief Structure used to link together metrics implemented in variant
    parts of the RIP. */
typedef struct sw_metrics_callbacks {
  /** \brief Modular metrics update callback.

      The RIP will call this function when it wants a module to prepare
      metrics for output. The callback is expected to call the metrics
      recording functions \c sw_metric_integer(), \c sw_metric_float(), or \c
      sw_metric_boolean(), possibly interspersed with nested sequences of \c
      sw_metrics_group_open() and \c sw_metrics_group_close().

      \param[in] metrics An opaque pointer to the root metrics group. This
                         pointer must be passed back to the RIP's metrics
                         recording functions. The callback may navigate the
                         group structure by modifying this pointer through
                         calls to \c sw_metrics_open_group() and
                         \c sw_metrics_close_group.

      \retval TRUE if the metrics update succeeded.
      \retval FALSE if the metrics update failed, usually because the RIP
                    returned FALSE from the recording or grouping function.
  */
  Bool (*update)(sw_metrics_group *group) ;

  /** \brief Reset modular metrics.

      The RIP calls this function when it wants a module to reset its metrics
      to their initial values.

      \param reason  One of the SW_METRICS_RESET_* reason codes, indicating
                     why metrics are being reset.
  */
  void (*reset)(int reason) ;

  /** Next metrics function link. A module registering metrics callbacks
      should not manipulate this pointer in any way. */
  struct sw_metrics_callbacks *next ;
} sw_metrics_callbacks ;

/** \brief Emit the current metrics as an XML file to a file stream.

    \param[in] flptr  The file stream pointer to which metrics will be sent.

    \retval TRUE if the metrics output was successful.
    \retval FALSE if there was a problem (usually \c IOERROR) emitting
                  metrics.
*/
Bool sw_metrics_emit(/*@notnull@*/ /*@in@*/ struct FILELIST *flptr) ;

/** \brief Navigate to a metrics sub-group, creating it if necessary.

    \param[in,out] group  The current metrics group, as passed to a
                          \c sw_metrics_callbacks::update function or previous
                          group navigations.

    \retval TRUE if the group was either existed already, or was created. On
                 a successful exit, the \a group referent is set to the
                 sub-group.
    \retval FALSE if there was a problem opening the group.
*/
Bool sw_metrics_open_group(/*@notnull@*/ sw_metrics_group **group,
                           /*@notnull@*/ /*@in@*/ const char *group_name,
                           size_t group_name_len) ;

/** \brief Navigate to the parent group.

    \param[in,out] group  The current metrics group, as passed to a previous
                          \c sw_metrics_open_group() function.

    On exit, the \a group referent is set to the parent group. An assertion
    will be thrown if an attempt is made to navigate to the parent of the
    root group.
*/
void sw_metrics_close_group(/*@notnull@*/ sw_metrics_group **group) ;

/** \brief Shorthand for a single statement enclosed in a metrics group
    open/close. */
#define SW_METRICS_GROUP(grp_, statement_) MACRO_START \
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH(grp_)) ) \
    return FALSE ; \
  statement_ ; \
  sw_metrics_close_group(&metrics) ; \
MACRO_END

/** \brief Low-level function to create a new metric.

    This function is not needed most of the time; the convenience functions
    wrap up the most common usage of \c sw_metric_create(). A search for the
    metric should be performed using \c sw_metric_get() before calling this
    function, or a duplicate metric may be created.

    \param[in] group  The current metrics group, as passed to a
                      \c sw_metrics_callbacks::update function or previous
                      group navigations. The metric will be created as an
                      element within this group.
    \param[in] metric_name  A NUL-terminated name of the metric to create.
    \param metric_name_len  The length of the metric name. This must be the
                            same as \c strlen(metric_name).

    \returns If this function fails, it returns NULL. If metrics are turned
    off, it returns a metric with an empty name and a value of \c
    SW_DATUM_TYPE_NOTHING. If the function succeeds, it initialises a new
    NOTHING metric, and returns a pointer to it. This metric must be set
    using \c sw_metric_set_value() to make it visible. */
sw_metric *sw_metric_create(/*@notnull@*/ sw_metrics_group *group,
                            /*@notnull@*/ /*@in@*/ const char *metric_name,
                            size_t metric_name_len) ;

/** \brief Low-level function to return a named metric from a group.

    \param[in] group  The current metrics group, as passed to a
                      \c sw_metrics_callbacks::update function or previous
                      group navigations.
    \param[in] metric_name  A NUL-terminated name of the metric to create.
    \param metric_name_len  The length of the metric name. This must be the
                            same as \c strlen(metric_name).

    \returns If the named metric does not exist in the group, NULL is
    returned. */
sw_metric *sw_metric_get(sw_metrics_group *group,
                         const char* metric_name,
                         size_t metric_name_len);

/** \brief Low-level function to extract a metric's value.

    \param[in] metric A non-NULL metric pointer, as returned by \c
                      sw_metric_get().

    \returns A copy of the metric's \c sw_datum. If the metric is a string,
    the string memory is not copied. The string memory must \e not be freed.
*/
/*@observer@*/
sw_datum sw_metric_get_value(/*@notnull@*/ /*@in@*/ const sw_metric* metric) ;

/** \brief Low-level function to set a metric's value.

    This function is not needed most of the time; the convenience functions
    wrap up the most common usage of \c sw_metric_set_value().

    \param metric A non-NULL metric pointer, as returned by \c sw_metric_get().
    \param value  The data value to set. The only types supported are
                  integer, float, boolean, null, and string. The type of a
                  metric cannot be changed once it has been set.

    \retval TRUE if the datum was successfully copied. If the value of the
    datum is a string, that string is copied into the existing metric's
    string memory, or a new memory location.
    \retval FALSE if the function failed.
*/
Bool sw_metric_set_value(sw_metric* metric, sw_datum value) ;

/** \brief Convenience function to record an integer counter metric.

    \param[in] metrics An opaque pointer passed to
                       \c sw_metrics_callbacks::update by the core.
    \param[in] name    The name under which the counter will be displayed.
    \param value       The value of the counter.

    \retval TRUE if the metric was recorded successfully.
    \retval FALSE if the metric could not be recorded. The update function
                  should close any open metrics groups, and return FALSE in
                  response to this.
*/
Bool sw_metric_integer(sw_metrics_group *metrics, const char *name,
                       size_t name_len, int32 value) ;

/** \brief Short form caller for sw_metric_integer, returning if false. */
#define SW_METRIC_INTEGER(name_, value_) MACRO_START \
  if (! sw_metric_integer(metrics, METRIC_NAME_AND_LENGTH(name_), (value_))) \
    return FALSE; \
MACRO_END

/** \brief Convenience function to record a floating-point metric.

    \param[in] metrics An opaque pointer passed to
                       \c sw_metrics_callbacks::update by the core.
    \param[in] name    The name under which the counter will be displayed.
    \param value       The value of the metric.

    \retval TRUE if the metric was recorded successfully.
    \retval FALSE if the metric could not be recorded. The update function
                  should close any open metrics groups, and return FALSE in
                  response to this.
*/
Bool sw_metric_float(sw_metrics_group *metrics, const char *name,
                     size_t name_len, float value) ;

/** \brief Short form caller for sw_metric_float, returning if false. */
#define SW_METRIC_FLOAT(name_, value_) MACRO_START \
  if (! sw_metric_float(metrics, METRIC_NAME_AND_LENGTH(name_), (value_))) \
    return FALSE; \
MACRO_END

/** \brief Convenience function to record a boolean metric.

    \param[in] metrics An opaque pointer passed to
                       \c sw_metrics_callbacks::update by the core.
    \param[in] name    The name under which the counter will be displayed.
    \param value       The value of the metric.

    \retval TRUE if the metric was recorded successfully.
    \retval FALSE if the metric could not be recorded. The update function
                  should close any open metrics groups, and return FALSE in
                  response to this.
*/
Bool sw_metric_boolean(sw_metrics_group *metrics, const char *name,
                       size_t name_len, int32 value) ;

/** \brief Short form caller for sw_metric_boolean, returning if false. */
#define SW_METRIC_BOOLEAN(name_, value_) MACRO_START \
  if (! sw_metric_boolean(metrics, METRIC_NAME_AND_LENGTH(name_), (value_))) \
    return FALSE; \
MACRO_END

/** \brief Register modular metrics callback functions.

    Modules can call this function on initialisation, using a
    statically-scoped \c sw_metrics_callbacks parameter, to note that they
    collect metrics, and wish the RIP to report them.

    \param funcs The callback functions to register. Both of the \c update
    and \c reset function pointers \e must be set.

    The module should not manipulate the \c funcs::next field after this
    call.
*/
void sw_metrics_register(sw_metrics_callbacks *funcs) ;

/** \brief Base type for a metrics histogram.

    The odd structure of metrics histograms is because to make it easy to
    use, we want to be able to declare a constant-sized histogram with
    enough storage for the histogram data in one go.

    This macro can be used for C stack, static, or struct field type
    declarations. It can't be used for parameter or return type declarations,
    so don't try that.

    Metrics histograms map values from a domain to a fixed number of counter
    buckets. The domain is mapped through a logarithm, with a base specified
    at initialisation time. This can be used to adjust the bucket distribution
    to expand or reduce the visibility of some part of the domain.

    The domain is half-open, so values accepted into the buckets are in the
    range domain_lo <= x < domain_hi. The high domain limit will normally
    be 1 more than the last representable value in the domain.

    Values lower or higher than the domain are all accumulated separately
    into a pairs of exception counters.

    The metrics counter function \c sw_metric_histogram_count()
    allows the counters to be incremented by any integer, so weighted
    histograms are also possible.

    This structure should be kept to a multiple of 32 bits size.
*/
typedef struct sw_metric_histogram_base {
  size_t length ;       /**< Number of buckets. */
  double base ;         /**< Logarithm base and domain. */
  double domain_lo, domain_hi ; /**< Domain of histogram. */
  double scale ;  /**< Computed scale: log_base(domain_hi-domain_lo). */
  int32 lower, higher ; /**< Count of items before, after domain */
} sw_metric_histogram_base ;

/** \brief Declared type of metrics histogram.

    The odd structure of metrics histograms is because to make it easy to
    use, we want to be able to declare a constant-sized histogram with
    enough storage for the histogram data in one go.

    This macro can be used for C stack, static, or struct field type
    declarations. It can't be used for parameter or return type declarations,
    so don't try that.

    When calling the metrics histogram functions, the address of the base
    field of structures declared with this macro \e must be passed as the
    histogram parameter.
*/
#define sw_metric_histogram_t(size_) \
  struct { \
    sw_metric_histogram_base info ; \
    int32 buckets[size_] ; /**< Enough space for items. */ \
  }

/** \brief Extract the allocated size from a histogram variable. */
#define SW_METRIC_HISTOGRAM_SIZE(var_) (sizeof((var_).buckets)/sizeof((var_).buckets[0]))

/** \brief Log base for linear mapping to buckets. */
#define SW_METRIC_HISTOGRAM_LINEAR 1.0

/** \brief Log base for power of two mapping to buckets. */
#define SW_METRIC_HISTOGRAM_LOG2 2.0

/** \brief Function to reset a histogram.

    Histogram metrics can be safely reset in the modular metric
    \c sw_metrics_callbacks::reset function.

    \param hist  The \c info field of a histogram structure declared with \c
                 sw_metric_histogram_t.
    \param size  The size of the histogram, as declared to
                 \c sw_metric_histogram_t.
    \param base  The logarithm used to scale the histogram.
    \param domain_lo  The lower inclusive bound of the histogram domain.
    \param domain_hi  The upper exclusive bound of the histogram domain.
*/
void sw_metric_histogram_reset(sw_metric_histogram_base *hist,
                               size_t size, double base,
                               double domain_lo, double domain_hi) ;

/** \brief Function to accumulate a value into a histogram.

    \param hist  The \c info field of a histogram structure declared with \c
                 sw_metric_histogram_t.
    \param value The value to incorporate into the histogram.
    \param amount How much to alter the histogram counter.
*/
void sw_metric_histogram_count(sw_metric_histogram_base *hist,
                               double value, int32 amount) ;

/** \brief Convenience function to record a histogram metric.

    \param[in] metrics An opaque pointer passed to
                       \c sw_metrics_callbacks::update by the core.
    \param[in] name    The name under which the counter will be displayed.
    \param hist        The \c info field of a histogram structure declared
                       with \c sw_metric_histogram_t.

    \retval TRUE if the metric was recorded successfully.
    \retval FALSE if the metric could not be recorded. The update function
                  should close any open metrics groups, and return FALSE in
                  response to this.
*/
Bool sw_metric_histogram(sw_metrics_group *metrics,
                         const char *name, size_t name_len,
                         sw_metric_histogram_base *hist) ;

/** \brief User hash function callback. Return value *MUST* be between
    zero and hash_table_size passed into the
    sw_metric_hashtable_create() function.
 */
typedef uint32 (*sw_metric_hash_callback_funct_t)(uintptr_t hashkey) ;

/** \brief De-allocate the hash table key. This is only likely to be
    used when the hash key is a pointer to some sort of allocated C
    object. If the hashkey is merely an integral type, this will be
    NULL.
 */
typedef void (*sw_metric_deallocate_hashkey_callback_funct_t)(uintptr_t hashkey) ;

/** \brief Callback when enumerating all values in the hash table.
 */
typedef Bool (*sw_metric_enumerate_callback_funct_t)(uintptr_t hashkey,
                                                     size_t counter,
                                                     void *data) ;

/** \brief Create a hash table with size hash_table_size. The hash
    table merely keeps a hit count for keys. Provide your own hash
    function and key de-allocator should you need one. The key is a
    uintptr_t so that pointers may be used for hashing.
 */
Bool sw_metric_hashtable_create(sw_metric_hashtable **hashtable,
                                size_t hash_table_size,
                                sw_metric_hash_callback_funct_t hashfunct,
                                sw_metric_deallocate_hashkey_callback_funct_t deallocate_hashkey_funct) ;

/** \brief Destroy all memory for the hash table. Sets *hashtable to
    NULL.
 */
void sw_metric_hashtable_destroy(sw_metric_hashtable **hashtable) ;

/** \brief Empty the hash table.
 */
void sw_metric_hashtable_reset(sw_metric_hashtable *hashtable) ;

/** \brief If the key does not exist, create a new entry on the hash
    table and set its counter to one.
 */
Bool sw_metric_hashtable_increment_key_counter(sw_metric_hashtable *hashtable,
                                               uintptr_t hashkey) ;

/** \brief Enumerate over all entries in the hash table.
 */
Bool sw_metric_hashtable_enumerate(sw_metric_hashtable *hashtable,
                                   sw_metric_enumerate_callback_funct_t enumeratefunct,
                                   void *data) ;

/** \brief Number of unique entries keys currently in hash table.
 */
size_t sw_metric_hashtable_key_count(sw_metric_hashtable *hashtable) ;

#else /* !METRICS_BUILD */

/* If metrics are turned off, ignore any call to register them. */
#define sw_metrics_register(x_) EMPTY_STATEMENT()

/* If metrics are turned off, supply a stub type definition for histograms. */
#define sw_metric_histogram_t(size_) void *

/* If metrics are turned off, the histogram counter does nothing. */
#define sw_metric_histogram_count(hist_, value_, amount_) EMPTY_STATEMENT()

#endif /* METRICS_BUILD */

/* ============================================================================
* Log stripped */
#endif
