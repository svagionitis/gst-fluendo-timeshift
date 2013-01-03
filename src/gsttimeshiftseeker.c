/* GStreamer
 * Copyright (C) 2013 Youview TV Ltd. <william.manley@youview.com>
 * Copyright (C) 2011 Fluendo S.A. <support@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gsttimeshiftseeker
 *
 * The timeshiftseeker element transforms TIME to BYTES in segment/seek events
 * based upon a time/byte index.  This element is not useful by itself and
 * should be used in conjunction with an indexer with a shared index.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbasetransform.h>
#include "gsttimeshiftseeker.h"

GST_DEBUG_CATEGORY_STATIC (gst_time_shift_seeker_debug_category);
#define GST_CAT_DEFAULT gst_time_shift_seeker_debug_category

/* prototypes */


static void gst_time_shift_seeker_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_time_shift_seeker_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_time_shift_seeker_dispose (GObject * object);
static void gst_time_shift_seeker_finalize (GObject * object);

static gboolean gst_time_shift_seeker_start (GstBaseTransform * trans);
static gboolean gst_time_shift_seeker_stop (GstBaseTransform * trans);
static gboolean
gst_time_shift_seeker_sink_event (GstBaseTransform * trans, GstEvent * event);
static gboolean
gst_time_shift_seeker_src_event (GstBaseTransform * trans, GstEvent * event);
static void gst_time_shift_seeker_replace_index(GstTimeShiftSeeker * seeker,
    GstIndex * new_index);
static gboolean
gst_time_shift_seeker_query (GstBaseTransform *trans, GstPadDirection direction,
                             GstQuery *query);
static GstFlowReturn
gst_time_shift_seeker_transform_ip (GstBaseTransform * trans, GstBuffer * buf);

enum
{
  PROP_0,
  PROP_INDEX
};

/* pad templates */

static GstStaticPadTemplate gst_time_shift_seeker_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );

static GstStaticPadTemplate gst_time_shift_seeker_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );


/* class initialization */

G_DEFINE_TYPE (GstTimeShiftSeeker, gst_time_shift_seeker, GST_TYPE_BASE_TRANSFORM);
#define parent_class gst_time_shift_seeker_parent_class

static void
gst_time_shift_seeker_class_init (GstTimeShiftSeekerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_time_shift_seeker_set_property;
  gobject_class->get_property = gst_time_shift_seeker_get_property;
  gobject_class->dispose = gst_time_shift_seeker_dispose;
  gobject_class->finalize = gst_time_shift_seeker_finalize;
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_stop);
  base_transform_class->sink_event = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_sink_event);
  base_transform_class->src_event = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_src_event);
  base_transform_class->query = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_query);
  base_transform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_transform_ip);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_time_shift_seeker_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_time_shift_seeker_src_template));

  g_object_class_install_property (gobject_class, PROP_INDEX,
      g_param_spec_object ("index", "Index",
          "The index from which to read indexing information",
          GST_TYPE_INDEX, (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  gst_element_class_set_static_metadata (element_class, "Time-shift seeker",
      "Generic", "Transforms time to bytes as required by seek/segment events",
      "William Manley <william.manley@youview.com>");

  GST_DEBUG_CATEGORY_INIT (gst_time_shift_seeker_debug_category,
      "gst_time_shift_seeker", 0, "Time-shift seeker");
}

static void
gst_time_shift_seeker_init (GstTimeShiftSeeker * seeker)
{
}

static void
gst_time_shift_seeker_replace_index(GstTimeShiftSeeker * seeker,
    GstIndex * new_index)
{
  if (seeker->index) {
    gst_object_unref (seeker->index);
    seeker->index = NULL;
  }
  if (new_index) {
    gst_object_ref (new_index);
    seeker->index = new_index;
  }
}


void
gst_time_shift_seeker_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTimeShiftSeeker *seeker = GST_TIME_SHIFT_SEEKER (object);

  switch (property_id) {
    case PROP_INDEX:
      gst_time_shift_seeker_replace_index(seeker, g_value_dup_object(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_seeker_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstTimeShiftSeeker *seeker = GST_TIME_SHIFT_SEEKER (object);

  switch (property_id) {
    case PROP_INDEX:
      g_value_set_object(value, seeker->index);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_seeker_dispose (GObject * object)
{
  GstTimeShiftSeeker *seeker = GST_TIME_SHIFT_SEEKER (object);

  /* clean up as possible.  may be called multiple times */
  gst_time_shift_seeker_replace_index(seeker, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_time_shift_seeker_finalize (GObject * object)
{
  /* GstTimeShiftSeeker *timeshiftseeker = GST_TIME_SHIFT_SEEKER (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_time_shift_seeker_start (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_time_shift_seeker_stop (GstBaseTransform * trans)
{

  return TRUE;
}

static GstClockTime
gst_time_shift_seeker_bytes_to_stream_time(GstTimeShiftSeeker * ts,
    guint64 buffer_offset)
{
  GstIndexEntry *entry = NULL;
  GstClockTime ret;

  /* Let's check if we have an index entry for that seek bytes */
  entry = gst_index_get_assoc_entry (ts->index,
      GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_NONE, GST_FORMAT_BYTES,
      buffer_offset);

  if (entry) {
    gint64 offset;
    gint64 time = GST_CLOCK_TIME_NONE;

    gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &offset);
    gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time);

    GST_DEBUG_OBJECT (ts, "found index entry at %" GST_TIME_FORMAT " pos %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (time), offset);
    if (buffer_offset == offset) {
      GST_ELEMENT_WARNING (ts, RESOURCE, FAILED, ("Bytes->time conversion inaccurate"), ("Lookup of byte offset not accurate: Returned byte offset %lld doesn't match requested offset %lld.  Time: %lld", offset, buffer_offset, time));
    }
    ret = (GstClockTime) time;
  }
  else if (buffer_offset == 0) {
    ret = 0;
  }
  else {
    GST_ELEMENT_WARNING (ts, RESOURCE, FAILED, ("Bytes->time conversion failed"), ("Lookup of byte offset %i failed: No index entry for that byte offset", buffer_offset));
    ret = GST_CLOCK_TIME_NONE;
  }
  return ret;
}

static void
gst_time_shift_seeker_transform_segment_event (GstTimeShiftSeeker *seeker,
    GstEvent ** event)
{
  GstEvent *newevent;
  GstSegment segment;

  gst_event_copy_segment (*event, &segment);
  if (segment.format != GST_FORMAT_BYTES) {
    GST_DEBUG_OBJECT (seeker, "time shift seeker received non-bytes segment");
    goto beach;
  }
  if (!(segment.flags & GST_SEGMENT_FLAG_RESET)) {
    /* We can only handle flushing segments ATM as otherwise running time is >0
       so filling in segment.base becomes trickier TODO: Fix this. */
    GST_DEBUG_OBJECT (seeker, "time shift seeker can only deal with flushing "
                              "seeks");
    goto beach;
  }
  if (!seeker->index) {
    GST_DEBUG_OBJECT (seeker, "no index");
    goto beach;
  }

  segment.format = GST_FORMAT_TIME;
  segment.base = 0;
  segment.start = gst_time_shift_seeker_bytes_to_stream_time (seeker, segment.start);
  if (segment.stop != -1) {
    segment.stop = gst_time_shift_seeker_bytes_to_stream_time (seeker, segment.stop);
  }
  segment.time = segment.start;

  newevent = gst_event_new_segment(&segment);
  gst_event_set_seqnum (newevent, gst_event_get_seqnum (*event));
  gst_event_replace (event, newevent);

  seeker->timestamp_next_buffer = TRUE;

  GST_DEBUG_OBJECT (seeker, "forwarding segment %" GST_SEGMENT_FORMAT, &segment);
beach:
  return;
}

static gboolean
gst_time_shift_seeker_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstTimeShiftSeeker *seeker = GST_TIME_SHIFT_SEEKER (trans);

  if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
    gst_time_shift_seeker_transform_segment_event (seeker, &event);
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static guint64
gst_time_shift_seeker_get_duration_bytes (GstTimeShiftSeeker * seeker)
{
  GstBaseTransform * base = GST_BASE_TRANSFORM (seeker);
  GstQuery * query = gst_query_new_duration (GST_FORMAT_BYTES);
  gboolean success = gst_pad_peer_query(base->sinkpad, query);
  if (success) {
    gint64 duration = -1;
    gst_query_parse_duration(query, NULL, &duration);
    return duration;
  }
  else {
    return 0;
  }
}

static GstClockTime
gst_time_shift_seeker_get_last_time (GstTimeShiftSeeker * base)
{
  if (!base->index) {
    GST_DEBUG_OBJECT (base, "no index");
    return GST_CLOCK_TIME_NONE;
  }
  else {
    gint64 time;
    gint64 offset;
    GstIndexEntry *entry = NULL;
    guint64 len = gst_time_shift_seeker_get_duration_bytes(base);

    entry = gst_index_get_assoc_entry (base->index, GST_INDEX_LOOKUP_BEFORE,
        GST_ASSOCIATION_FLAG_NONE, GST_FORMAT_BYTES, len - 1000000);

    if (entry) {
      gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &offset);
      gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time);

      GST_DEBUG_OBJECT (base, "found index entry at %" GST_TIME_FORMAT " pos %"
          G_GUINT64_FORMAT, GST_TIME_ARGS (time), offset);
      return time;
    }
    else {
      GST_DEBUG_OBJECT (base, "no entry for position %lli in %p", len, base->index);
      return GST_CLOCK_TIME_NONE;
    }
  }
}

static guint64
gst_time_shift_seeker_seek (GstTimeShiftSeeker * base,
    GstSeekType type, gint64 start)
{
  GstIndexEntry *entry = NULL;
  gint64 offset = -1;
  gint64 time;
  GstClockTime pos = 0;

  if (type == GST_SEEK_TYPE_NONE) {
    /* Base class checks: Should never happen */
    goto beach;
  }

  GST_DEBUG_OBJECT (base, "seeking at time %" GST_TIME_FORMAT " type %d",
      GST_TIME_ARGS (start), type);

  if (!base->index) {
    GST_DEBUG_OBJECT (base, "no index");
    goto beach;
  }

  if (type == GST_SEEK_TYPE_SET) {
    pos = start;
  } else if (type == GST_SEEK_TYPE_END) {
    pos = gst_time_shift_seeker_get_last_time(base) + start;
  }

  GST_DEBUG_OBJECT (base, "seek in index for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (pos));

  /* Let's check if we have an index entry for that seek time */
  entry = gst_index_get_assoc_entry (base->index, GST_INDEX_LOOKUP_BEFORE,
      GST_ASSOCIATION_FLAG_NONE, GST_FORMAT_TIME, pos);

  if (entry) {
    gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &offset);
    gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time);

    GST_DEBUG_OBJECT (base, "found index entry at %" GST_TIME_FORMAT " pos %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (time), offset);
  }

beach:
  return offset;
}

static void
gst_time_shift_seeker_transform_offset(GstTimeShiftSeeker * ts, GstSeekType * type,
      gint64 * offset)
{
  if (*type == GST_SEEK_TYPE_NONE) {
    /* pass: no transformation required */
  }
  else {
    *offset = gst_time_shift_seeker_seek (ts, *type, *offset);
    *type = GST_SEEK_TYPE_SET;
  }
}

/* Converts any seek events with format TIME to one with format BYTES */
static void
gst_time_shift_seeker_transform_seek_event (GstTimeShiftSeeker * seeker, GstEvent ** event)
{
  GstEvent * new_event = NULL;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate;

  gst_event_parse_seek (*event, &rate, &format, &flags,
      &start_type, &start, &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    goto beach;
  }

  if (!seeker->index) {
    GST_ELEMENT_WARNING (seeker, CORE, SEEK, NULL, ("Seeker has no index set"));
    goto beach;
  }

  if (rate < 0.0) {
    GST_WARNING_OBJECT (seeker, "we only support forward playback");
    goto beach;
  }

  gst_time_shift_seeker_transform_offset(seeker, &start_type, &start);
  gst_time_shift_seeker_transform_offset(seeker, &stop_type, &stop);
  new_event = gst_event_new_seek(rate, GST_FORMAT_BYTES, flags, start_type,
      start, stop_type, stop);
  gst_event_set_seqnum(new_event, gst_event_get_seqnum(*event));
  gst_event_replace (event, new_event);

beach:
  return;
}

static gboolean
gst_time_shift_seeker_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstTimeShiftSeeker *seeker = GST_TIME_SHIFT_SEEKER (trans);

  if (GST_EVENT_TYPE(event) == GST_EVENT_SEEK) {
    gst_time_shift_seeker_transform_seek_event (seeker, &event);
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

static gboolean
gst_time_shift_seeker_query (GstBaseTransform *base, GstPadDirection direction,
                             GstQuery *query)
{
  GstTimeShiftSeeker * ts = GST_TIME_SHIFT_SEEKER (base);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration(query, &format, NULL);
      if (format == GST_FORMAT_TIME && direction == GST_PAD_SRC) {
        GST_DEBUG_OBJECT (base, "Responding to duration query with time  %" GST_TIME_FORMAT,
          GST_TIME_ARGS (gst_time_shift_seeker_get_last_time(ts)));

        gst_query_set_duration(query, format,
              gst_time_shift_seeker_get_last_time(ts));
        return TRUE;
      }
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        gst_query_set_seeking (query, fmt, TRUE, 0, -1);
        return TRUE;
      }
      break;
    }
    default:
      break;
  }
  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (base, direction,
                                                         query);
}

static GstFlowReturn
gst_time_shift_seeker_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstTimeShiftSeeker * seeker = GST_TIME_SHIFT_SEEKER (base);
  g_assert (gst_buffer_is_writable (buf));
  if (seeker->timestamp_next_buffer) {
    GST_BUFFER_TIMESTAMP (buf) = gst_time_shift_seeker_bytes_to_stream_time(
        seeker, GST_BUFFER_OFFSET (buf));
    seeker->timestamp_next_buffer = FALSE;
  }
  return GST_FLOW_OK;
}
