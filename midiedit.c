/****************************************************************************
 * midiedit - GTK based editor for MIDI synthesisers
 *
 * midiedit.c - Main program, with most of the synth-agnostic UI
 *              implementation.
 * 
 * Copyright (C) 2014  Ricard Wanderlof <ricard2013@butoba.net>
 *
 * Originally based on:
 * MIDI Controller - A program that runs MIDI controller GUIs built in Glade
 * Copyright (C) 2004  Lars Luthman <larsl@users.sourceforge.net>
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
#include <poll.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "param.h"
#include "blofeld_params.h"
#include "midi.h"

#include "debug.h"

const char *main_window_name = "Main Window";
GtkWidget *main_window = NULL;

/* Parameter handler */
struct param_handler phandler;
struct param_handler *param_handler = &phandler;

struct adjustor {
  const char *id; /* name of parameter, e.g. "Filter 1 Cutoff" */
  int parnum;  /* parameter number. Redundant, but practical */
  GList *widgets; /* list of widgets controlling parameter */
};

/* List of all adjustors, indexed by parameter number. */
struct adjustor **adjustors;

/* used to temporarily block updates to MIDI */
int block_updates;

/* buffer number currently shown */
int current_buffer_no;

/* current patch name */
char current_patch_name[BLOFELD_PATCH_NAME_LEN_MAX + 1] = { 0 };
int current_patch_name_max = BLOFELD_PATCH_NAME_LEN_MAX;
GtkWidget *patch_name_widget;

/* structure for mapping keys to specific widget focus */
struct keymap {
  const gchar *key_name; /* e.g. "s" */
  guint keyval;          /* GDK_ code for key */
  const gchar *param_name;
  GtkWidget *widget;
  int param_arg;
  const gchar *parent_name;
  GtkWidget *parent;
  int parent_arg;
};

GList *keymaps = NULL;

void set_title(void)
{
  char title[80];

  sprintf(title, "%s Editor - %s (Part %d)", 
          param_handler->name, current_patch_name, current_buffer_no + 1);

  if (main_window && GTK_IS_WINDOW(main_window))
    gtk_window_set_title(GTK_WINDOW(main_window), title);
}
  
void 
on_Main_Window_destroy (GtkObject *object, gpointer user_data)
{
  gtk_main_quit();
}

void
on_midi_input (gpointer data, gint fd, GdkInputCondition condition)
{
  dprintf("Received MIDI data on fd %d\n", fd);
  midi_input();
}

struct adj_update {
  GtkWidget *widget; /* widget requesting update, or NULL */
  const void *valptr; /* pointer to new value to set */
};

static void
update_adjustor(gpointer data, gpointer user_data)
{
  GtkWidget *widget = data;
  struct adj_update *adj_update = user_data;
  const void *valptr = adj_update->valptr;

  /* We only update adjustors that aren't the same as the widget generating 
   * the update. For updates arriving from MIDI, there is no such widget,
   * and it is set to NULL, so we update all widgets.
   */
  if (widget != adj_update->widget) {
    if (GTK_IS_RANGE(widget))
      gtk_range_set_value(GTK_RANGE(widget), *(const int *)valptr);
    else if (GTK_IS_COMBO_BOX(widget))
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), *(const int *)valptr);
    else if (GTK_IS_TOGGLE_BUTTON(widget))
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !!*(const int *)valptr);
    else if (GTK_IS_ENTRY(widget))
      gtk_entry_set_text(GTK_ENTRY(widget), valptr);
  }
}

void update_adjustors(struct adjustor *adjustor, const void *valptr,
                      GtkWidget *updating_widget)
{
  struct adj_update adj_update = { .widget = updating_widget, 
                                   .valptr = valptr };

  block_updates = 1;
  g_list_foreach(adjustor->widgets, update_adjustor, &adj_update);
  block_updates = 0;
}

/* Called whenever parameter change arrives via MIDI */
void
param_changed(int parnum, int buffer_no, void *valptr, void *ref)
{
  if (buffer_no == current_buffer_no && valptr) {
    struct adjustor *adjustor = adjustors[parnum];
    if (adjustor)
      update_adjustors(adjustor, valptr, NULL);
  }
}

/* Called whenever a widget's value changes.
 * We send the update via MIDI, but also to other widgets with same parameter
 * name (e.g. on other editor pages or tabs.)
 */
static void update_parameter(struct adjustor *adjustor, const void *valptr, GtkWidget *widget)
{
  param_handler->param_update_parameter(adjustor->parnum, current_buffer_no, valptr);

  update_adjustors(adjustor, valptr, widget);
}

void
on_entry_changed(GtkObject *object, gpointer user_data)
{
  GtkEntry *gtkentry = GTK_ENTRY (object);
  struct adjustor *adjustor = user_data;
  const char *stringptr;

  if (block_updates)
    return;

  if (gtkentry) {
    stringptr = gtk_entry_get_text(gtkentry);
    dprintf("Entry %p: name %s, value \"%s\", parnum %d\n",
            gtkentry, gtk_buildable_get_name(GTK_BUILDABLE(gtkentry)),
            stringptr, adjustor->parnum);
    /* Set our global patch name if respective widget and update title */
    if (GTK_WIDGET(object) == patch_name_widget) {
      strncpy(current_patch_name, stringptr, current_patch_name_max);
      set_title();
    }
    update_parameter(adjustor, stringptr, GTK_WIDGET(object));
  }
}

void
on_value_changed (GtkObject *object, gpointer user_data)
{
  GtkRange *gtkrange = GTK_RANGE (object);
  struct adjustor *adjustor = user_data;
  int value;

  if (block_updates)
    return;

  if (gtkrange) {
    dprintf("Slider %p: name %s, value %d, parnum %d\n",
            gtkrange, gtk_buildable_get_name(GTK_BUILDABLE(gtkrange)),
            (int) gtk_range_get_value(gtkrange), adjustor->parnum);
    value = (int) gtk_range_get_value(gtkrange);
    update_parameter(adjustor, &value, GTK_WIDGET(object));
  }
}

void
on_combobox_changed (GtkObject *object, gpointer user_data)
{
  GtkComboBox *cb = GTK_COMBO_BOX (object);
  struct adjustor *adjustor = user_data;
  int value;

  if (block_updates)
    return;

  if (cb) {
    dprintf("Combobox %p: name %s, value %d, parnum %d\n",
            cb, gtk_buildable_get_name(GTK_BUILDABLE(cb)),
            gtk_combo_box_get_active(cb), adjustor->parnum);
    value = (int) gtk_combo_box_get_active(cb);
    update_parameter(adjustor, &value, GTK_WIDGET(object));
  }
}

void
on_togglebutton_changed (GtkObject *object, gpointer user_data)
{
  GtkToggleButton *tb = GTK_TOGGLE_BUTTON (object);
  struct adjustor *adjustor = user_data;
  int value;

  if (block_updates)
    return;

  if (tb) {
    dprintf("Togglebutton %p: name %s, value %d, parnum %d\n",
            tb, gtk_buildable_get_name(GTK_BUILDABLE(tb)),
            gtk_toggle_button_get_active(tb), adjustor->parnum);
    value = gtk_toggle_button_get_active(tb);
    update_parameter(adjustor, &value, GTK_WIDGET(object));
  }
}

void
on_button_pressed (GtkObject *object, gpointer user_data)
{
  GtkButton *button = GTK_BUTTON (object);
  if (button)
    dprintf("Button %p: name %s, user_data %p\n",
            button, gtk_buildable_get_name(GTK_BUILDABLE(button)), user_data);
}


gboolean navigation(GtkWidget *widget, GtkWidget *focus, GdkEventKey *event)
{
  GtkWidget *parent;
  int shifted = event->state & GDK_SHIFT_MASK;
  int arg = -1;
#define SET_ARG(value) if (arg < 0) arg = (value)
  GtkWidget *what = NULL;
  const char *signal = NULL;

  switch (event->keyval) {
    case GDK_Right:
      arg = GTK_DIR_RIGHT;
    case GDK_Left:
      if (arg < 0) arg = GTK_DIR_LEFT;
    case GDK_Up:
      if (arg < 0) arg = GTK_DIR_UP;
    case GDK_Down:
      if (arg < 0) arg = GTK_DIR_DOWN;
      what = widget;
      signal = "move-focus";
      break;
    case GDK_Forward:
    case GDK_Page_Up:
    case GDK_plus:
      if (GTK_IS_RANGE(focus)) {
        arg = shifted ? GTK_SCROLL_PAGE_FORWARD : GTK_SCROLL_STEP_FORWARD;
        what = focus;
        signal = "move-slider";
      }
      if (GTK_IS_TOGGLE_BUTTON(focus) && 
          (parent = gtk_widget_get_parent(focus)) &&
          GTK_IS_COMBO_BOX(parent)) {
        arg = shifted ? GTK_SCROLL_PAGE_FORWARD : GTK_SCROLL_STEP_FORWARD;
        what = parent;
        signal = "move-active";
      }
      break;
    case GDK_Back:
    case GDK_Page_Down:
    case GDK_minus:
      if (GTK_IS_RANGE(focus)) {
        arg = shifted ? GTK_SCROLL_PAGE_BACKWARD : GTK_SCROLL_STEP_BACKWARD;
        what = focus;
        signal = "move-slider";
      }
      if (GTK_IS_TOGGLE_BUTTON(focus) && 
          (parent = gtk_widget_get_parent(focus)) &&
          GTK_IS_COMBO_BOX(parent)) {
        arg = shifted ? GTK_SCROLL_PAGE_BACKWARD : GTK_SCROLL_STEP_BACKWARD;
        what = parent;
        signal = "move-active";
      }
      break;
    default:
      break;
  }
  if (what && signal) {
    g_signal_emit_by_name(GTK_OBJECT(what), signal, arg);
    return TRUE;
  }
  return FALSE;
}


/* Is 'parent' identical to or a parent of 'widget' ? */
int is_parent(GtkWidget *widget, GtkWidget *parent)
{
  do {
dprintf("Scanning %s (%p), looking for %s (%p)\n", gtk_buildable_get_name(GTK_BUILDABLE(widget)), widget, gtk_buildable_get_name(GTK_BUILDABLE(parent)), parent);
    if (widget == parent)
      return 1;
  } while (widget = gtk_widget_get_parent(widget));
  return 0;
}

/* Used for searching for valid key map given key val and current focus */
struct key_search_spec {
  guint keyval;
  GtkWidget *focus_widget;
};

/* Used for g_list_find_custom to find key val in keymaps */
static gint find_keymap(gconstpointer data, gconstpointer user_data)
{
  const struct keymap *keymap = data;
  const struct key_search_spec *search = user_data;

dprintf("Scan keymap %s: %s\n", keymap->key_name, keymap->param_name);
  if (keymap->keyval != search->keyval)
    return 1; /* not the key we're looking for found */
  if (!keymap->widget)
    return 1; /* Widget not set, UI specified unknown Param or Parent */
dprintf("Found keyval\n");
  if (!keymap->parent) /* keymap has no parent specified; we're done */
    return 0; /* found */
dprintf("Has parent %s\n", keymap->parent_name);
  /* If parent is a notebook, then check for the relevant notebook page. */
  if (GTK_IS_NOTEBOOK(keymap->parent))
    return gtk_notebook_get_current_page(GTK_NOTEBOOK(keymap->parent)) !=
           keymap->parent_arg; /* 0 if on correct page */
dprintf("Parent is not a notebook\n");
  /* Otherwise check if the currently focused widget has the same parent
   * as the parameter specified in the keymap. */
  return !is_parent(search->focus_widget, keymap->parent); /* 0 if found */
}

/* Handle keys mapped in UI KeyMapping liststore */  
gboolean mapped_key(GtkWidget *widget, GtkWidget *focus, GdkEventKey *event)
{
  struct key_search_spec key_search_spec;
  key_search_spec.keyval = event->keyval; /* event to search for in keymaps */
  key_search_spec.focus_widget = focus; /* currently focused widget */

  GList *keymap_l = g_list_find_custom(keymaps, &key_search_spec, find_keymap);
  if (!keymap_l)
    return FALSE; /* can't find valid key mapping */

  struct keymap *keymap = keymap_l->data;
  dprintf("Found key map for %s: widget %s (%p)\n", keymap->key_name, keymap->param_name, keymap->widget);

  if (!keymap->widget) /* Can happen if ParamName note found */
    return FALSE;

  if (GTK_IS_NOTEBOOK(keymap->widget)) {
    dprintf("Setting notebook page to %d\n", keymap->param_arg);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(keymap->widget),
                                  keymap->param_arg);
    return TRUE;
  }

  if (GTK_IS_BUTTON(keymap->widget)) {
    gtk_button_pressed(GTK_BUTTON(keymap->widget));
    return TRUE;
  }

  /* All other widget types */
  gtk_widget_grab_focus(keymap->widget);

  return TRUE; /* key handled */
}


/* Handle all key events arriving in the main window */
gboolean
key_event(GtkWidget *widget, GdkEventKey *event)
{
  GtkWidget *focus = GTK_WINDOW(widget)->focus_widget;

  dprintf("Key pressed: \"%s\" (0x%08x), widget %p, focus widget %p, (main window %p)\n", gdk_keyval_name(event->keyval), event->keyval, widget, focus, main_window);
  dprintf("Focused widget is a %s, name %s\n", gtk_widget_get_name(focus), gtk_buildable_get_name(GTK_BUILDABLE(focus)));

  if (GTK_IS_ENTRY(focus))
    return FALSE; /* We let GTK handle all key events for GtkEntries*/

  if (navigation(widget, focus, event))
    return TRUE;

  if (mapped_key(widget, focus, event))
    return TRUE;

  return FALSE; /* key not handled - defer to GTK defaults */
}


/* A collection of three functions and passing struct that work together
 * in order to find a widget with a given id in a whole window hierarchy,
 * starting with the top window.
 */

/* The union is a hack to get around the fact that the GCompareFunc for
 * g_list_find_custom has gcoinstpointers for arguments, while we want
 * to be able to export the resulting GtkWidget out of the whole
 * kit-n.kaboodle when we've actually found the widget we're looking for. */
struct find_widget_data {
  const char *id;
  union {
    const GtkWidget **const_ptr;
    GtkWidget **ptr;
  } result;
};

/* Forward declaration since the two following functions call each other. */
int find_widgets_id(GList *widget_list, const struct find_widget_data *find_widget_data);

/* GCompareFunc for g_list_find_custom: compare the names of the
 * search element pointed to by data and the find_widget_data pointed
 * to by user_data. If equal, return 0 and set the result.[const_]ptr of
 * the find_widget_data. If not found, and the widget is a container,
 * get a list of its children, and do a new search through the result list. */
int find_widget_id(gconstpointer data, gconstpointer user_data)
{
  const GtkWidget *widget = data;
  const struct find_widget_data *find_widget_data = user_data;
  const gchar *widget_id = gtk_buildable_get_name(GTK_BUILDABLE(widget));
  if (widget_id && !strcmp(find_widget_data->id, widget_id))
  {
    *find_widget_data->result.const_ptr = widget; /* only set if found. */
    return 0; /* found it! */
  }
  if (GTK_IS_CONTAINER(widget))
    return find_widgets_id(gtk_container_get_children(GTK_CONTAINER(widget)), find_widget_data);
  return 1; /* we didn't find it this time around */
}

/* Try to find a widget in widget_list with the id in find_widget_data,
 * recursively. Needs to return an int in order to be useful when called
 * from a GCompareFunc. */
int find_widgets_id(GList *widget_list, const struct find_widget_data *find_widget_data)
{
  /* return 0 if found */
  return !g_list_find_custom(widget_list, find_widget_data, find_widget_id);
}

/* Top level interface for widget search function. Search recursively from
 * widget and all its children for a widget with the id 'id', and return
 * the widget. */
GtkWidget *find_widget_with_id(GtkWidget *widget, const char *id)
{
  GtkWidget *result = NULL;
  struct find_widget_data find_widget_data;

  dprintf("Searching for widget with id %s\n", id);
  find_widget_data.id = id;
  find_widget_data.result.ptr = &result;
  find_widget_id(widget, &find_widget_data);
  if (result)
    dprintf("Found it!\n");

  return result;
}


/* GFunc for iterating over keymaps when adding new widgets in
 * create_adjustor. */
void add_to_keymap(gpointer data, gpointer user_data)
{
  struct keymap *keymap = data; /* current keymap definition */
  struct keymap *keymap_add = user_data; /* data we want to add to key map */

  if (keymap->param_name && !strcmp(keymap->param_name, keymap_add->param_name))
  {
    /* Scan all widgets from the top window and down for a parent with the
     * name specified in the keymap. If there is no parent_name specified,
     * that means that the key map is valid in any context. */
    GtkWidget *parent = NULL;
    if (keymap->parent_name) {
      parent = find_widget_with_id(gtk_widget_get_toplevel(keymap_add->widget),
                                   keymap->parent_name);
      if (!parent) {
        eprintf("Warning: Can't find parent %s for key %s map for %s, skipping!\n",
               keymap->parent_name, keymap->key_name, keymap->param_name);
        /* We return here to avoid setting an unconditional mapping (since
         * parent is NULL) which is not what the user intended, and might
         * screw up other mappings, very confusing. */
        return;
      }
    }
    if (!keymap->parent_name || parent) { /* no parent specified; or, found */
      keymap->widget = keymap_add->widget;
      keymap->parent = parent;
      dprintf("Mapped key %s to widget %s (%p) arg %d parent %s (%p) arg %d\n", keymap->key_name, keymap->param_name, keymap->widget, keymap->param_arg, keymap->parent_name, keymap->parent, keymap->parent_arg);
    }
  }
}

/* Add widget with id param_name to keymaps, if any key maps reference it. */
void add_to_keymaps(GList *keymaps, GtkWidget *widget, const char *param_name)
{
  struct keymap map_data;

  map_data.param_name = param_name;
  map_data.widget = widget;
  g_list_foreach(keymaps, add_to_keymap, &map_data);
}


/* Chop trailing digits off name 
 * I.e. for "LFO 1 Shape2" return "LFO 1 Shape".
 * Returned string must be freed.
 */
gchar *chop_name(const gchar *name)
{
  gchar *new_name = g_strdup(name);
  if (new_name) {
    gchar *name_end = new_name + strlen(new_name) - 1;
    while (name_end >= new_name && g_ascii_isdigit(*name_end))
      name_end--;
    name_end[1] = '\0';
  }
  return new_name;
}

void add_adjustors (GList *widget_list, struct adjustor **adjustors);

void create_adjustor (gpointer data, gpointer user_data)
{
  GtkWidget *this = data;
  struct adjustor **adjustors = user_data;
  int parnum;
  static const char *patch_name = NULL;

  const gchar *name = gtk_buildable_get_name(GTK_BUILDABLE(this));
  gchar *id = chop_name(name);

  dprintf("Widget: %s, name %s id %s\n", gtk_widget_get_name(this), name ? name : "none", id ? id : "none");

  /* Scan keymaps, add widget if found */
  if (name)
    add_to_keymaps(keymaps, this, name);

  if (id && (parnum = param_handler->param_find_index(id)) >= 0) {
    dprintf("has parameter\n");
    struct adjustor *adjustor = adjustors[parnum];
    if (!adjustors[parnum]) {
      /* no adjustor for this parameter yet; create one */
      adjustor = g_new0(struct adjustor, 1);
      adjustor->id = id;
      adjustor->parnum = parnum;
      adjustors[parnum] = adjustor;
    }
    /* Add our widget to the list of widgets for this parameter */
    /* prepend is faster than append, and ok since we don't care about order */
    adjustor->widgets = g_list_prepend(adjustor->widgets, this);

    if (GTK_IS_RANGE(this)) {
      struct param_properties props;
      GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(this));
      /* It will always have an adjustment, but set all required properties
       * for it so we don't need to set them in the UI for all parameters.
       * In effect, this means that in the UI we do not need to set an
       * adjustment for any parameter. */
      if (adj) {
        param_handler->param_get_properties(parnum, &props);
        g_object_set(adj, "lower", (gdouble) props.ui_min, NULL);
        g_object_set(adj, "upper", (gdouble) props.ui_max, NULL);
        g_object_set(adj, "step-increment", (gdouble) props.ui_step, NULL);
        g_object_set(adj, "page-increment", (gdouble) 10 * props.ui_step, NULL);
        g_object_set(adj, "page-size", (gdouble) 0, NULL);
      } else
        eprintf("Warning: GtkRange %s has no adjustment\n", id);
      g_signal_connect(this, "value-changed", G_CALLBACK(on_value_changed), adjustor);
    }

    else if (GTK_IS_COMBO_BOX(this))
      g_signal_connect(this, "changed", G_CALLBACK(on_combobox_changed), adjustor);
    else if (GTK_IS_TOGGLE_BUTTON(this))
      g_signal_connect(this, "toggled", G_CALLBACK(on_togglebutton_changed), adjustor);

    else if (GTK_IS_ENTRY(this)) {
      g_signal_connect(this, "changed", G_CALLBACK(on_entry_changed), adjustor);
      /* Fetch patch name id (i.e. "Patch Name") if we haven't got it yet */
      if (!patch_name)
        patch_name = param_handler->param_get_patch_name_id();
      /* If we're looking at that parameter, save widget ref for later. */
      if (!strcmp(id, patch_name))
        patch_name_widget = this;
    }
  }

  g_free(id);

  if (GTK_IS_CONTAINER(this)) {
     dprintf("It's a container\n");
     add_adjustors(gtk_container_get_children(GTK_CONTAINER(this)), adjustors);
  }
}

void add_adjustors (GList *widget_list, struct adjustor **adjustors)
{
  g_list_foreach (widget_list, create_adjustor, adjustors);
}


void display_adjustor(gpointer data, gpointer user_data)
{
  GtkWidget *adj = data;
  
  if (GTK_IS_RANGE(adj)) {
    GtkRange *range = GTK_RANGE (adj);
    dprintf("Slider %p: name %s, value %d\n",
            range, gtk_buildable_get_name(GTK_BUILDABLE(range)),
            (int) gtk_range_get_value(range));
  }
  else if (GTK_IS_COMBO_BOX(adj)) {
    GtkComboBox *cb = GTK_COMBO_BOX (adj);
    dprintf("Combobox %p: name %s, value %d\n",
            cb, gtk_buildable_get_name(GTK_BUILDABLE(cb)),
            gtk_combo_box_get_active(cb));
  }
  else if (GTK_IS_TOGGLE_BUTTON(adj)) {
    GtkToggleButton *tb = GTK_TOGGLE_BUTTON (adj);
    dprintf("Togglebutton %p: name %s, value %d\n",
            tb, gtk_buildable_get_name(GTK_BUILDABLE(tb)),
            gtk_toggle_button_get_active(tb));
  }
  else if (GTK_IS_ENTRY(adj)) {
    GtkEntry *e = GTK_ENTRY (adj);
    dprintf("Entry %p: name %s, value \"%s\"\n",
            e, gtk_buildable_get_name(GTK_BUILDABLE(e)),
            gtk_entry_get_text(e));
  }
}

void display_adjustors(struct adjustor *adjustor)
{
  g_list_foreach(adjustor->widgets, display_adjustor, NULL);
}

void
create_adjustors_list (int ui_params, GtkWidget *top_widget)
{
  adjustors = g_malloc0_n(ui_params, sizeof(struct adjustor *));
  if (!adjustors) return;
  create_adjustor(top_widget, adjustors);

  int i;
  for (i = 0; i < ui_params; i++) {
    struct adjustor *adjustor = adjustors[i];
    if (adjustor)
      display_adjustors(adjustor);
  }
}


/* GtkTreeModelForeachFunc to read one row from the KeyMappings liststore
 * in the UI definition file, and create an entry in the keymaps list for it. */
static gboolean
get_liststore_keymap(GtkTreeModel *model,
                     GtkTreePath *path,
                     GtkTreeIter *iter,
                     gpointer user_data)
{
  gchar *key, *param_name, *parent_name;
  int keyval, param_arg, parent_arg;
  GList **keymaps = user_data;

  gtk_tree_model_get(model, iter, 
                     0, &key,
                     1, &param_name,
                     2, &param_arg,
                     3, &parent_name,
                     4, &parent_arg, -1);
  keyval = gdk_keyval_from_name(key);
  if (keyval == GDK_VoidSymbol) {
    g_free(key);
    g_free(param_name);
    g_free(parent_name);
    return FALSE;
  }

  gchar *tree_path_str = gtk_tree_path_to_string(path); /* TODO: Don't really need this */

  dprintf("Keymap row: %s: key %s mapping %s, arg %d, parent %s, arg %d\n", tree_path_str, key, param_name, param_arg, parent_name, parent_arg);

  /* Empty parent_name string means there is no specified parent.
   * Easier to manage if just set to NULL rather than having zero-length
   * string.
   */
  if (parent_name && !parent_name[0])
  {
    g_free(parent_name);
    parent_name = NULL;
  }

  struct keymap *map = g_new0(struct keymap, 1);
  map->key_name = key;
  map->keyval = keyval;
  map->param_name = param_name;
  map->param_arg = param_arg;
  map->parent_name = parent_name;
  map->parent_arg = parent_arg;

  /* We need to use append here, because we want to preserve ordering */
  *keymaps = g_list_append(*keymaps, map);

  g_free(tree_path_str);

  return FALSE;
}

/* Load UI KeyMappings liststore into keymaps list */
static void
setup_hotkeys(GtkBuilder *builder, const gchar *store_name)
{
  GtkListStore *store;

  store = GTK_LIST_STORE( gtk_builder_get_object( builder, "KeyMappings" ) );
  if (!store) {
    dprintf("Can't find key mappings in UI file!\n");
    return;
  }

  gtk_tree_model_foreach(GTK_TREE_MODEL(store), get_liststore_keymap, &keymaps);
}


int
main (int argc, char *argv[])
{
  GtkBuilder *builder;
  struct polls *polls;
  int poll_tag;
  const char *gladename;

  memset(param_handler, 0, sizeof (*param_handler));
  blofeld_init(param_handler);

  gladename = param_handler->ui_filename;
  if (argv[1]) gladename = argv[1];
  
  gtk_init (&argc, &argv);
  
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, gladename, NULL);

  main_window = GTK_WIDGET (gtk_builder_get_object (builder, main_window_name));
  gtk_builder_connect_signals (builder, NULL);
#if 0 /* example of explicit signal connection */
  g_signal_connect (main_window, "destroy", G_CALLBACK (on_Main_Window_destroy), NULL);
#endif

  setup_hotkeys(builder, "KeyMappings");
  g_signal_connect(main_window, "key-press-event", G_CALLBACK(key_event), NULL);

  g_object_unref (G_OBJECT (builder));

  polls = midi_init_alsa();
  if (!polls)
    return 2;

  /* TODO: Should really loop over all potential fds */
  poll_tag = gdk_input_add (polls->pollfds[0].fd, GDK_INPUT_READ, on_midi_input, NULL);

  create_adjustors_list(param_handler->params, main_window);

  param_handler->param_register_notify_cb(param_changed, NULL);

  midi_connect(param_handler->remote_midi_device);
  
  block_updates = 0;

  set_title();
  gtk_widget_show (main_window);       
  gtk_main ();
  
  return 0;
}

