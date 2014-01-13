#include <gtk/gtk.h>

struct adjustor {
  const char *id;
  GtkAdjustment *adj;
};

void 
on_winMain_destroy (GtkObject *object, gpointer user_data)
{
  gtk_main_quit();
}

void
on_adjustment_value_changed (GtkObject *object, gpointer user_data)
{
  GtkAdjustment *adj = GTK_ADJUSTMENT (object);
  printf("Got value changed: adj %p\n", adj);
  if (adj)
    printf("Adj %p: name %s, value %d, user_data %p\n",
           adj, gtk_widget_get_name(GTK_WIDGET(object)),
           (int) gtk_adjustment_get_value(adj), user_data);
}

void
on_value_changed (GtkObject *object, gpointer user_data)
{
  GtkRange *range = GTK_RANGE (object);
  if (range)
    printf("Slider %p: name %s, value %d, user_data %p\n",
           range, gtk_buildable_get_name(GTK_BUILDABLE(range)),
           (int) gtk_range_get_value(range), user_data);
}


void
on_combobox_changed (GtkObject *object, gpointer user_data)
{
  GtkComboBox *cb = GTK_COMBO_BOX (object);
  if (cb)
    printf("Combobox %p: name %s, value %d, user_data %p\n",
           cb, gtk_buildable_get_name(GTK_BUILDABLE(cb)),
           gtk_combo_box_get_active(cb), user_data);
}

void
on_button_pressed (GtkObject *object, gpointer user_data)
{
  GtkButton *button = GTK_BUTTON (object);
  if (button)
    printf("Button %p: name %s, user_data %p\n",
           button, gtk_buildable_get_name(GTK_BUILDABLE(button)), user_data);
}


void add_adjustments(GList *widget_list, GList **adj_list);

void create_adjustment (gpointer data, gpointer user_data)
{
  GtkWidget *this = data;
  GList **adj_list = user_data;

  const char *id = gtk_buildable_get_name(GTK_BUILDABLE(this));

  printf("Widget: %s, id %s\n", gtk_widget_get_name(this), id ? id : "none");

  if (id && blofeld_find_index(id) >= 0) {
    GObject *adjobj;
    struct adjustor *adjustor = g_new(struct adjustor, 1);
    g_object_get(this, "adjustment", &adjobj, NULL);
    if (adjobj && GTK_IS_ADJUSTMENT(adjobj)) {
      printf("has adjustment\n");
      adjustor->id = id;
      adjustor->adj = GTK_ADJUSTMENT (adjobj);
      *adj_list = g_list_append(*adj_list, adjustor);
    }
  }

  if (GTK_IS_CONTAINER(this)) {
     printf("It's a container\n");
     add_adjustments(gtk_container_get_children(GTK_CONTAINER(this)), adj_list);
  }
}

void add_adjustments (GList *widget_list, GList **adj_list)
{
  g_list_foreach (widget_list, create_adjustment, adj_list);
}

void display_adjustment(gpointer data, gpointer user_data)
{
  struct adjustor *adjustor = data;
  
  if (GTK_IS_ADJUSTMENT(adjustor->adj)) {
    printf("Id %s (%p): value %d\n", adjustor->id, adjustor->adj, gtk_adjustment_get_value(adjustor->adj));
  }
}

void
create_adjustments_list (GtkWidget *top_widget)
{
  GList *adjustment_list = NULL;
  create_adjustment(top_widget, &adjustment_list);

  g_list_foreach (adjustment_list, display_adjustment, 0);
}

int
main (int argc, char *argv[])
{
  GtkBuilder *builder;
  GtkWidget *window;
  
  gtk_init (&argc, &argv);
  
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "controller-rw.glade", NULL);

  window = GTK_WIDGET (gtk_builder_get_object (builder, "winMain"));
  gtk_builder_connect_signals (builder, NULL);
#if 0 /* example of explicit signal connection */
  g_signal_connect (window, "destroy", G_CALLBACK (on_winMain_destroy), NULL);
#endif
  g_object_unref (G_OBJECT (builder));

  create_adjustments_list(window);
  
  gtk_widget_show (window);       
  gtk_main ();
  
  return 0;
}

