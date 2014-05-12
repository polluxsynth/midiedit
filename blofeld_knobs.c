/****************************************************************************
 * midiedit - GTK based editor for MIDI synthesizers
 *
 * blofeld_knobs.c - Map Blofeld UI parameters to controller knobs.
 *
 * Copyright (C) 2014  Ricard Wanderlof <ricard2013@butoba.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ****************************************************************************/

#include <stdio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "midi.h"
#include "knob_mapper.h"

#include "debug.h"

/* We keep this structure local, as its contents depends on what we as a
 * specific knob mapper implementation require.
 * Use void * for communicating it externally. */
struct knobmap {
  GtkContainer *container;
  GList *potlist;
  GList *active_pots;
  gboolean sorted;
};

/* Structure build time variables */
static GtkContainer *current_container = NULL;

/* Callback for notifying gui when knob value has changed. */
static knob_notify_cb notify_ui = NULL;
static void *notify_ref;

/* Callback management */
static void
register_notify_cb(knob_notify_cb cb, void *ref)
{
  notify_ui = cb;
  notify_ref = ref;
}

#ifdef DEBUG
static void
print_knob(gpointer data, gpointer user_data)
{
  struct knob_descriptor *knob_description = data;
  GtkWidget *widget = knob_description->widget;
  printf("Widget %s:%s (%d,%d): %s\n",
         gtk_widget_get_name(widget), gtk_buildable_get_name(GTK_BUILDABLE(widget)), widget->allocation.x, widget->allocation.y, GTK_WIDGET_VISIBLE(widget) ? "visible" : "hidden");
}

static void
print_knobmap(struct knobmap *knobmap)
{
  printf("Frame %s:\n", gtk_buildable_get_name(GTK_BUILDABLE(knobmap->container)));
  g_list_foreach(knobmap->active_pots, print_knob, NULL);
}
#endif

/* Originally from gtkcontainer.c */
/* GCompareFunc to compare left-right positions of widgets a and b. */
static gint
left_right_compare (gconstpointer a, gconstpointer b)
{
  GtkWidget *widget1 = ((struct knob_descriptor *)a)->widget;
  GtkWidget *widget2 = ((struct knob_descriptor *)b)->widget;

  gint x1 = widget1->allocation.x + widget1->allocation.width / 2;
  gint x2 = widget2->allocation.x + widget2->allocation.width / 2;

  if (x1 == x2)
    {
      gint y1 = widget1->allocation.y + widget1->allocation.height / 2;
      gint y2 = widget2->allocation.y + widget2->allocation.height / 2;

      return (y1 < y2) ? -1 : ((y1 == y2) ? 0 : 1);
    }
  else
    return (x1 < x2) ? -1 : 1;
}

/* Add knob_descriptor data to list pointed to by user_data, but 
 * only if the corresponding widget is VISIBLE */
static void
add_if_active(gpointer data, gpointer user_data)
{
  struct knob_descriptor *knob_descriptor = data;
  GList **active_pots = user_data;

  if (GTK_WIDGET_VISIBLE(knob_descriptor->widget))
    *active_pots = g_list_prepend(*active_pots, knob_descriptor);
}

/* Copy knob_descriptors with visible widgets to new list */
static GList *
copy_active(GList *knoblist)
{
  GList *active_pots = NULL;
  g_list_foreach(knoblist, add_if_active, &active_pots);

  return active_pots;
}


/* Knob map build time functions */

/* Create a new map for container */
static void *
blofeld_knobs_container_new(GtkContainer *container)
{
  struct knobmap *knobmap = g_new0(struct knobmap, 1);

  if (current_container)
    eprintf("WARNING: %s called with container set\n", __func__);
  /* Save the container as the current one */
  current_container = container;

  /* Initialize knobmap structure */
  knobmap->container = container;
  /* knobnap->potlist = NULL; done by g_new0() */

  return knobmap;
}

/* Finish creating map for container */
static void *
blofeld_knobs_container_done(void *knobmap_in)
{
  struct knobmap *knobmap = knobmap_in;

  if (!current_container) {
    eprintf("WARNING: %s called with no container set\n", __func__);
    return NULL;
  }

  current_container = NULL;
  return knobmap;
}

/* Add widget to map for container */
static void *
blofeld_knobs_container_add_widget(void *knobmap_in,
                                   struct knob_descriptor *knob_description)
{
  struct knobmap *knobmap = knobmap_in;

  if (!knob_description) return knobmap;

  if (!GTK_IS_RANGE(knob_description->widget))
    knobmap->potlist = g_list_prepend(knobmap->potlist, knob_description);

  return knobmap;
}

/* Return knob_descriptor for knob no knob_no in the knobmap_in knob map */
static struct knob_descriptor *
blofeld_knob(void *knobmap_in, int knob_no)
{
  static int prev_knob_no = -1;
  static struct knobmap *prev_knobmap = NULL;
  static struct knob_descriptor *knob_descriptor = NULL;

  struct knobmap *knobmap = knobmap_in;

  if (!knobmap) return NULL;

  /* Caching: if called with same inparams as last time, and the knobmap
   * is still sorted (i.e. has not been invalidated), return same
   * knob_descriptor as last time. */
  if (knob_no == prev_knob_no &&
      knobmap == prev_knobmap &&
      knobmap->sorted)
    return knob_descriptor;

  if (!knobmap->sorted) {
    if (knobmap->active_pots) {
      g_list_free(knobmap->active_pots);
      knobmap->active_pots = NULL;
    }
    knobmap->active_pots = copy_active(knobmap->potlist);
    knobmap->active_pots = g_list_sort(knobmap->active_pots,
                                       left_right_compare);
#ifdef DEBUG
    print_knobmap(knobmap);
#endif
    knobmap->sorted = TRUE;
  }

  return knob_descriptor = g_list_nth_data(knobmap->active_pots, knob_no);
}

/* Invalidate current active knobmap, forcing blofeld_knob to create a new
 * list next the time a knob within the frame is moved, the most important
 * consequence of is that the visibility of the widgets is considered. */
static void
blofeld_invalidate(void *knobmap_in)
{
  struct knobmap *knobmap = knobmap_in;

  if (!knobmap) return;

  knobmap->sorted = FALSE;
}

/* Initialize knob mapper. */
void
blofeld_knobs_init(struct knob_mapper *knob_mapper)
{
  /* Start-up-time configuraton */
  knob_mapper->register_notify_cb = register_notify_cb;
  knob_mapper->container_new = blofeld_knobs_container_new;
  knob_mapper->container_done = blofeld_knobs_container_done;
  knob_mapper->container_add_widget = blofeld_knobs_container_add_widget;
  /* Run-time mapping */
  knob_mapper->knob = blofeld_knob;
  knob_mapper->invalidate = blofeld_invalidate;
};

/********************** End of file blofeld_knobs.c **************************/
