/*
 * GStreamer
 * Copyright (C) 2018-2020 RidgeRun <support@ridgerun.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

/**
 * SECTION:element-gstinferencebin
 *
 * Helper element that simplifies inference by creating a bin with the
 * required elements in the typical inference configuration.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 v4l2src device=$CAMERA ! inferencebin arch=tinyyolov2 model-location=$MODEL_LOCATION ! \
 * backend=tensorflow arch::backend::input-layer=$INPUT_LAYER arch::backend::output-layer=OUTPUT_LAYER ! \
 * videoconvert ! ximagesink sync=false
 * ]|
 * Detect object in a camera stream
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstinferencebin.h"

/* generic templates */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_inference_bin_debug_category);
#define GST_CAT_DEFAULT gst_inference_bin_debug_category

static void gst_inference_bin_finalize (GObject * object);
static void gst_inference_bin_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inference_bin_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_inference_bin_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_inference_bin_start (GstInferenceBin * self);
static gboolean gst_inference_bin_stop (GstInferenceBin * self);

enum
{
  PROP_0,
  PROP_ARCH
};

#define PROP_ARCH_DEFAULT "tinyyolov2"

struct _GstInferenceBin
{
  GstBin parent;

  gchar *arch;
  GstPad *sinkpad;
  GstPad *srcpad;
};

struct _GstInferenceBinClass
{
  GstBinClass parent;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstInferenceBin, gst_inference_bin,
    GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_inference_bin_debug_category, "inferencebin",
        0, "debug category for inferencebin element"));

static void
gst_inference_bin_class_init (GstInferenceBinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "inferencebin", "Filter",
      "A bin with the inference element in their typical configuration",
      "Michael Gruner <michael.gruner@ridgerun.com>");

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_inference_bin_change_state);

  object_class->finalize = gst_inference_bin_finalize;
  object_class->set_property = gst_inference_bin_set_property;
  object_class->get_property = gst_inference_bin_get_property;

  g_object_class_install_property (object_class, PROP_ARCH,
      g_param_spec_string ("arch", "Architecture",
          "The factory name of the network architecture to use for inference",
          PROP_ARCH_DEFAULT, G_PARAM_READWRITE));
}

static void
gst_inference_bin_init (GstInferenceBin * self)
{
  self->arch = g_strdup (PROP_ARCH_DEFAULT);

  self->sinkpad =
      gst_ghost_pad_new_no_target_from_template (NULL,
      gst_static_pad_template_get (&sink_template));

  gst_pad_set_active (self->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad =
      gst_ghost_pad_new_no_target_from_template (NULL,
      gst_static_pad_template_get (&src_template));
  gst_pad_set_active (self->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static void
gst_inference_bin_finalize (GObject * object)
{
  GstInferenceBin *self = GST_INFERENCE_BIN (object);

  g_free (self->arch);
  self->arch = NULL;

  G_OBJECT_CLASS (gst_inference_bin_parent_class)->finalize (object);
}

static void
gst_inference_bin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInferenceBin *self = GST_INFERENCE_BIN (object);

  GST_LOG_OBJECT (self, "set_property");

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_ARCH:
      g_free (self->arch);
      self->arch = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_inference_bin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInferenceBin *self = GST_INFERENCE_BIN (object);

  GST_LOG_OBJECT (self, "get_property");

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_ARCH:
      g_value_set_string (value, self->arch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static gboolean
gst_inference_bin_start (GstInferenceBin * self)
{
  gboolean ret = FALSE;
  const gchar *template =
      "inferencefilter ! videoconvert !  tee name=tee "
      "tee. ! queue ! arch.sink_bypass "
      "tee. ! queue ! detectioncrop ! videoscale ! arch.sink_model "
      "arch.src_bypass ! queue ! inferenceoverlay " "%s name=arch";
  gchar *desc = NULL;
  GstElement *bin = NULL;
  GError *error = NULL;
  GstPad *sinkpad = NULL;
  GstPad *srcpad = NULL;

  g_return_val_if_fail (self, FALSE);

  desc = g_strdup_printf (template, self->arch);
  bin =
      gst_parse_bin_from_description_full (desc, TRUE, NULL,
      GST_PARSE_FLAG_FATAL_ERRORS, &error);
  g_free (desc);

  if (!bin) {
    GST_ERROR_OBJECT (self, "Unable to create bin: %s", error->message);
    g_error_free (error);
    goto out;
  }

  if (!gst_bin_add (GST_BIN (self), bin)) {
    GST_ERROR_OBJECT (self, "Unable to add to bin");
    gst_object_unref (bin);
    goto out;
  }

  sinkpad = gst_bin_find_unlinked_pad (GST_BIN (bin), GST_PAD_SINK);
  srcpad = gst_bin_find_unlinked_pad (GST_BIN (bin), GST_PAD_SRC);

  gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), sinkpad);
  gst_ghost_pad_set_target (GST_GHOST_PAD (self->srcpad), srcpad);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  GST_INFO_OBJECT (self, "Created bin successfully");
  ret = TRUE;

out:
  return ret;
}

static gboolean
gst_inference_bin_stop (GstInferenceBin * self)
{
  g_return_val_if_fail (self, FALSE);

  return TRUE;
}

static GstStateChangeReturn
gst_inference_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstInferenceBin *self = GST_INFERENCE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (FALSE == gst_inference_bin_start (self)) {
        GST_ERROR_OBJECT (self, "Failed to start");
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_inference_bin_parent_class)->change_state
      (element, transition);
  if (GST_STATE_CHANGE_FAILURE == ret) {
    GST_ERROR_OBJECT (self, "Parent failed to change state");
    goto out;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (FALSE == gst_inference_bin_stop (self)) {
        GST_ERROR_OBJECT (self, "Failed to stop");
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
    default:
      break;
  }

out:
  return ret;
}
