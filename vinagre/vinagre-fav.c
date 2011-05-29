/*
 * vinagre-fav.c
 * This file is part of vinagre
 *
 * Copyright (C) 2007,2008 - Jonh Wendell <wendell@bani.com.br>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include "vinagre-fav.h"
#include "vinagre-dnd.h"
#include "vinagre-bookmarks.h"
#include "vinagre-bookmarks-ui.h"
#include "vinagre-window-private.h"
#include "vinagre-bookmarks-entry.h"
#include "vinagre-plugins-engine.h"
#include "vinagre-commands.h"

#ifdef VINAGRE_HAVE_AVAHI
#include "vinagre-mdns.h"
#endif

#define VINAGRE_FAV_UI_XML_FILE "vinagre-fav-ui.xml"
#define VINAGRE_FAV_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), VINAGRE_TYPE_FAV, VinagreFavPrivate))

struct _VinagreFavPrivate
{
  VinagreWindow         *window;
  GtkWidget             *tree;
  GtkTreeModel          *model;
  GtkActionGroup        *action_group, *always_sensitive_action_group;
  VinagreBookmarksEntry *selected;
  GtkBox                *box;
};

G_DEFINE_TYPE(VinagreFav, vinagre_fav, GTK_TYPE_FRAME)

/* Signals */
enum
{
  FAV_ACTIVATED,
  FAV_SELECTED,
  LAST_SIGNAL
};

/* TreeModel */
enum {
  IMAGE_COL = 0,
  NAME_COL,
  ENTRY_COL,
  IS_FOLDER_COL,
  IS_GROUP_COL,
  IS_AVAHI_COL,
  NUM_COLS
};

/* Properties */
enum
{
  PROP_0,
  PROP_WINDOW,
};


static guint signals[LAST_SIGNAL] = { 0 };

static void
vinagre_fav_set_property (GObject      *object,
			  guint         prop_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
  VinagreFav *fav = VINAGRE_FAV (object);

  switch (prop_id)
    {
      case PROP_WINDOW:
	fav->priv->window = VINAGRE_WINDOW (g_value_get_object (value));
	break;

      default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
    }
}

static void
vinagre_fav_get_property (GObject    *object,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
  VinagreFav *fav = VINAGRE_FAV (object);

  switch (prop_id)
    {
      case PROP_WINDOW:
	g_value_set_object (value, fav->priv->window);
	break;

      default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;			
    }
}

static void
vinagre_fav_row_activated_cb (GtkTreeView       *treeview,
			      GtkTreePath       *path,
			      GtkTreeViewColumn *column,
			      VinagreFav         *fav)
{
  GtkTreeIter iter;
  VinagreBookmarksEntry *entry;
  gboolean folder, group;

  gtk_tree_model_get_iter (fav->priv->model, &iter, path);
  gtk_tree_model_get (fav->priv->model, 
		      &iter,
		      ENTRY_COL, &entry,
                      IS_FOLDER_COL, &folder,
                      IS_GROUP_COL, &group,
		      -1);

  if (folder || group)
    {
      if (gtk_tree_view_row_expanded (treeview, path))
        gtk_tree_view_collapse_row (treeview, path);
      else
        gtk_tree_view_expand_row (treeview, path, FALSE);
      return;
    }

  vinagre_cmd_open_bookmark (fav->priv->window,
			     vinagre_bookmarks_entry_get_conn (entry));

  /* Emits the signal saying that user has activated a bookmark */
  g_signal_emit (G_OBJECT (fav), 
		 signals[FAV_ACTIVATED],
		 0, 
		 entry);
  g_object_unref (entry);
}

static void
vinagre_fav_selection_changed_cb (GtkTreeSelection *selection,
				  VinagreFav       *fav)
{
  GtkTreeIter iter;
  VinagreBookmarksEntry *entry = NULL;
  gboolean avahi;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      gtk_tree_model_get (fav->priv->model, 
			  &iter,
			  ENTRY_COL, &entry,
                          IS_AVAHI_COL, &avahi,
			  -1);
      if (avahi)
	{
	  g_object_unref (entry);
	  entry = NULL;
	}
    }

  fav->priv->selected = entry;

  gtk_action_group_set_sensitive (fav->priv->action_group,
				  entry != NULL);

  /* Emits the signal saying that user has selected a bookmark */
  g_signal_emit (G_OBJECT (fav), 
		 signals[FAV_SELECTED],
		 0, 
		 entry);
  if (entry)
    g_object_unref (entry);
}

static GtkTreePath *
get_current_path (VinagreFav *fav)
{
  GtkTreePath *path = NULL;
  GtkTreeIter iter;
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (fav->priv->tree));

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    path = gtk_tree_model_get_path (fav->priv->model, &iter);

  return path;
}

static void
menu_position (GtkMenu    *menu,
	       gint       *x,
	       gint       *y,
	       gboolean   *push_in,
	       VinagreFav *fav)
{
  GtkTreePath *path;
  GdkRectangle rect;
  gint wy;
  GtkRequisition requisition;
  GtkWidget *w;
  GtkAllocation allocation;

  w = fav->priv->tree;
  path = get_current_path (fav);

  gtk_tree_view_get_cell_area (GTK_TREE_VIEW (w),
			       path,
			       NULL,
			       &rect);

  wy = rect.y;

  gdk_window_get_origin (gtk_widget_get_window (w), x, y);
	
  gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
  gtk_widget_get_allocation (w, &allocation);

  if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL)
    *x += allocation.x + allocation.width - requisition.width - 10;
  else
    *x += allocation.x + 10 ;

  wy = MAX (*y + 5, *y + wy + 5);
  wy = MIN (wy, *y + allocation.height - requisition.height - 5);
	
  *y = wy;

  *push_in = TRUE;

  if (path)
    gtk_tree_path_free (path);
}

static gboolean
show_popup_menu (VinagreFav      *fav,
		 GdkEventButton  *event,
		 const gchar     *menu_name)
{
  GtkWidget *menu = NULL;

  menu = gtk_ui_manager_get_widget (vinagre_window_get_ui_manager (fav->priv->window),
				    menu_name);
  g_return_val_if_fail (menu != NULL, FALSE);

  if (event)
    {
      gtk_menu_popup (GTK_MENU (menu), 
		      NULL, 
		      NULL,
		      NULL, 
		      NULL,
		      event->button, 
		      event->time);
    }
  else
    {
      gtk_menu_popup (GTK_MENU (menu), 
		      NULL, 
		      NULL,
		      (GtkMenuPositionFunc) menu_position, 
		      fav,
		      0, 
		      gtk_get_current_event_time ());
    }

  return TRUE;
}

static gboolean
fav_button_press_event (GtkTreeView    *treeview,
			GdkEventButton *event,
			VinagreFav     *fav)
{
  GtkTreePath *path = NULL;
  GtkTreeIter  iter;
  gboolean     folder, group, avahi;

  if (!((GDK_BUTTON_PRESS == event->type) && (3 == event->button)))
    return FALSE;

  if (event->window != gtk_tree_view_get_bin_window (treeview))
    return FALSE;

  if (gtk_tree_view_get_path_at_pos (treeview,
				     event->x,
				     event->y,
				     &path,
				     NULL,
				     NULL,
			             NULL))
    {				
      gtk_tree_model_get_iter (fav->priv->model, &iter, path);
      gtk_tree_model_get (fav->priv->model, 
			  &iter,
			  IS_FOLDER_COL, &folder,
			  IS_GROUP_COL, &group,
			  IS_AVAHI_COL, &avahi,
			  -1);
      if (group || avahi)
	{
	  gtk_tree_path_free (path);
	  return FALSE;
	}

      gtk_tree_view_set_cursor (treeview,
				path,
				NULL,
				FALSE);
      gtk_tree_path_free (path);
      return show_popup_menu (fav, event, folder?"/FavPopupFolder":"/FavPopupConn");
    }
  else
    return show_popup_menu (fav, event, "/FavPopupEmpty");

  return FALSE;
}


static gboolean
fav_popup_menu (GtkWidget  *treeview,
		VinagreFav *fav)
{
  /* Only respond if the treeview is the actual focus */
  if (gtk_window_get_focus (GTK_WINDOW (fav->priv->window)) == treeview)
    return show_popup_menu (fav, NULL, FALSE);

  return FALSE;
}

static gboolean
vinagre_fav_tooltip (GtkWidget *widget,
                     gint       x,
                     gint       y,
                     gboolean   k,
                     GtkTooltip *tooltip,
                     VinagreFav *fav)
{
  gchar                 *tip, *name, *uri;
  GtkTreePath           *path;
  gint                   bx, by;
  GtkTreeIter            iter;
  gboolean               folder, group;
  VinagreConnection     *conn;
  VinagreBookmarksEntry *entry;

  gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (widget),
                                                     x, y,
                                                     &bx, &by);
  
  if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
				      bx,
				      by,
				      &path,
				      NULL,
				      NULL,
			              NULL))
    return FALSE;

  gtk_tree_model_get_iter (fav->priv->model, &iter, path);
  gtk_tree_model_get (fav->priv->model, 
		      &iter,
		      ENTRY_COL, &entry, 
                      IS_FOLDER_COL, &folder,
                      IS_GROUP_COL, &group,
		      -1);

  gtk_tree_path_free (path);

  if (folder || group)
    {
      if (entry)
        g_object_unref (entry);
      return FALSE;
    }

  conn = vinagre_bookmarks_entry_get_conn (entry);
  name = vinagre_connection_get_best_name (conn);
  uri = vinagre_connection_get_string_rep (conn, TRUE);
  tip = g_markup_printf_escaped ("<b>%s</b>\n"
                                 "<small><b>%s</b> %s</small>",
                                 name,
				 _("Host:"), uri);

  gtk_tooltip_set_markup (tooltip, tip);
  g_free (tip);
  g_free (name);
  g_free (uri);
  g_object_unref (entry);

  return TRUE;
}

static void
vinagre_fav_cell_set_background (VinagreFav       *fav,
				 GtkCellRenderer  *cell,
				 gboolean         is_group,
				 gboolean         is_active)
{
  GdkColor  color;
  GtkStyle *style;

  g_return_if_fail (fav != NULL);
  g_return_if_fail (cell != NULL);

  style = gtk_widget_get_style (GTK_WIDGET (fav));

  if (!is_group)
    {
      if (is_active)
        {
          color = style->bg[GTK_STATE_SELECTED];

	  /* Here we take the current theme colour and add it to
	   * the colour for white and average the two. This
	   * gives a colour which is inline with the theme but
	   * slightly whiter.
	   */
	  color.red = (color.red + (style->white).red) / 2;
	  color.green = (color.green + (style->white).green) / 2;
	  color.blue = (color.blue + (style->white).blue) / 2;

	  g_object_set (cell,
		        "cell-background-gdk", &color,
		        NULL);
	
        }
      else
        {
	  g_object_set (cell,
	                "cell-background-gdk", NULL,
		        NULL);
        }
      }
    else
      {
        color = style->text_aa[GTK_STATE_INSENSITIVE];

	color.red = (color.red + (style->white).red) / 2;
	color.green = (color.green + (style->white).green) / 2;
	color.blue = (color.blue + (style->white).blue) / 2;

	g_object_set (cell,
		      "cell-background-gdk", &color,
		      NULL);
      }
}

static void
vinagre_fav_pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
				   GtkCellRenderer   *cell,
				   GtkTreeModel      *model,
				   GtkTreeIter       *iter,
				   VinagreFav        *fav)
{
  GdkPixbuf *pixbuf;
  gboolean   is_group;
  gboolean   is_active;

  gtk_tree_model_get (model, iter,
		      IS_GROUP_COL, &is_group,
		      IMAGE_COL, &pixbuf,
		      -1);

  g_object_set (cell,
	        "visible", !is_group,
		"pixbuf", pixbuf,
		NULL);

  if (pixbuf != NULL)
    g_object_unref (pixbuf);

  is_active = FALSE;
  vinagre_fav_cell_set_background (fav, cell, is_group, is_active);
}

static void
vinagre_fav_title_cell_data_func (GtkTreeViewColumn *column,
				  GtkCellRenderer   *renderer,
				  GtkTreeModel      *tree_model,
				  GtkTreeIter       *iter,
				  VinagreFav        *fav)
{
  char    *str;
  gboolean is_group;
  gboolean is_active;

  gtk_tree_model_get (GTK_TREE_MODEL (fav->priv->model), iter,
		      NAME_COL, &str,
		      IS_GROUP_COL, &is_group,
		      -1);

  g_object_set (renderer,
	        "text", str,
		NULL);

  is_active = FALSE;
  vinagre_fav_cell_set_background (fav, renderer, is_group, is_active);

  g_free (str);
}

static void
vinagre_fav_bookmarks_new_folder (GtkAction  *action,
				  VinagreFav *fav)
{
  vinagre_bookmarks_new_folder (vinagre_bookmarks_get_default (),
				GTK_WINDOW (fav->priv->window));
}

static void
vinagre_fav_bookmarks_edit (GtkAction  *action,
			    VinagreFav *fav)
{
  vinagre_bookmarks_edit (vinagre_bookmarks_get_default (),
                          fav->priv->selected,
                          GTK_WINDOW (fav->priv->window));
}

static void
vinagre_fav_bookmarks_del (GtkAction  *action,
			   VinagreFav *fav)
{
  vinagre_bookmarks_del (vinagre_bookmarks_get_default (),
                         fav->priv->selected,
                         GTK_WINDOW (fav->priv->window));
}

void
vinagre_fav_bookmarks_open (GtkAction  *action,
			    VinagreFav *fav)
{
  VinagreConnection *conn;

  conn = g_object_get_data (G_OBJECT (action), "conn");
  if (!conn)
    conn = vinagre_bookmarks_entry_get_conn (fav->priv->selected);

  g_return_if_fail (VINAGRE_IS_CONNECTION (conn));

  vinagre_cmd_open_bookmark (fav->priv->window, conn);
}

static const GtkActionEntry always_sensitive_actions[] =
{
  {"BookmarksNewFolder", "folder-new", N_("_New folder"), NULL,
    N_("Create a new folder"), G_CALLBACK (vinagre_fav_bookmarks_new_folder) },
};

static const GtkActionEntry actions[] =
{
  { "BookmarksOpen", GTK_STOCK_CONNECT, N_("_Open bookmark"), NULL,
    N_("Connect to this remote desktop"), G_CALLBACK (vinagre_fav_bookmarks_open) },
  {"BookmarksEdit", GTK_STOCK_EDIT, N_("_Edit bookmark"), NULL,
    N_("Edit the details of selected bookmark"), G_CALLBACK (vinagre_fav_bookmarks_edit) },
  {"BookmarksDel", GTK_STOCK_DELETE, N_("_Remove from bookmarks"), NULL,
    N_("Remove current selected connection from bookmarks"), G_CALLBACK (vinagre_fav_bookmarks_del) },
};

static void
vinagre_fav_create_toolbar (VinagreFav *fav)
{
  GtkWidget *toolbar;
  GtkActionGroup *action_group;
  GtkUIManager *manager;
  GError *error = NULL;

  manager = fav->priv->window->priv->manager;
  gtk_ui_manager_add_ui_from_file (manager,
				   vinagre_fav_get_ui_xml_filename (),
				   &error);
  if (error != NULL)
    {
      g_critical (_("Could not merge UI XML file: %s"), error->message);
      g_error_free (error);
      return;
    }

  action_group = gtk_action_group_new ("BookmarksActionGroup");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group,
				actions,
				G_N_ELEMENTS (actions),
				fav);
  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  gtk_action_group_set_sensitive (action_group, FALSE);
  fav->priv->action_group = action_group;

  action_group = gtk_action_group_new ("BookmarksAlwaysSensitiveActionGroup");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group,
				always_sensitive_actions,
				G_N_ELEMENTS (always_sensitive_actions),
				fav);
  gtk_ui_manager_insert_action_group (manager, action_group, 0);
  fav->priv->always_sensitive_action_group = action_group;

  toolbar = gtk_ui_manager_get_widget (manager, "/FavToolBar");
  gtk_style_context_add_class (gtk_widget_get_style_context (toolbar),
                               GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (gtk_widget_get_style_context (toolbar),
                                        GTK_JUNCTION_BOTTOM);
  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), GTK_TOOLBAR_ICONS);
  gtk_toolbar_set_icon_size (GTK_TOOLBAR (toolbar), GTK_ICON_SIZE_MENU);

  gtk_widget_show (toolbar);
  gtk_box_pack_start (fav->priv->box, toolbar, FALSE, FALSE, 0);
}

static void
vinagre_fav_create_tree (VinagreFav *fav)
{
  GtkCellRenderer   *cell;
  GtkWidget         *scroll;
  GtkTreeSelection  *selection;
  GtkTreeViewColumn *main_column;

  /* Create the scrolled window */
  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll),
				       GTK_SHADOW_IN);
  gtk_style_context_set_junction_sides (gtk_widget_get_style_context (scroll),
                                        GTK_JUNCTION_TOP);

  gtk_box_pack_start (fav->priv->box, scroll, TRUE, TRUE, 0);

  /* Create the model */
  fav->priv->model = GTK_TREE_MODEL (gtk_tree_store_new (NUM_COLS,
							 GDK_TYPE_PIXBUF,
							 G_TYPE_STRING,
							 VINAGRE_TYPE_BOOKMARKS_ENTRY,
                                                         G_TYPE_BOOLEAN,
                                                         G_TYPE_BOOLEAN,
                                                         G_TYPE_BOOLEAN));

  fav->priv->tree = gtk_tree_view_new_with_model (fav->priv->model);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (fav->priv->tree), FALSE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (fav->priv->tree));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
  g_signal_connect (selection, "changed", G_CALLBACK (vinagre_fav_selection_changed_cb), fav);

  main_column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_clickable (main_column, FALSE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (fav->priv->tree), main_column);

  /* Set up the pixbuf column */
  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (main_column, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (main_column,
					   cell,
					   (GtkTreeCellDataFunc) vinagre_fav_pixbuf_cell_data_func,
					    fav,
					    NULL);

  /* Set up the name column */
  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell,
		"ellipsize", PANGO_ELLIPSIZE_END,
		 NULL);
  gtk_tree_view_column_pack_start (main_column, cell, TRUE);
  gtk_tree_view_column_set_cell_data_func (main_column,
				           cell,
					   (GtkTreeCellDataFunc) vinagre_fav_title_cell_data_func,
					    fav,
					    NULL);

  g_signal_connect (fav->priv->tree,
		    "row-activated",
		    G_CALLBACK (vinagre_fav_row_activated_cb),
		    fav);

  g_signal_connect (fav->priv->tree,
		    "button-press-event",
		    G_CALLBACK (fav_button_press_event),
		    fav);

  g_signal_connect (fav->priv->tree,
		    "popup-menu",
		    G_CALLBACK (fav_popup_menu),
		    fav);

  gtk_widget_set_has_tooltip (fav->priv->tree, TRUE);
  g_signal_connect (fav->priv->tree,
		    "query-tooltip",
		    G_CALLBACK (vinagre_fav_tooltip),
		    fav);

  g_idle_add ((GSourceFunc)vinagre_fav_update_list, fav);

  gtk_container_add (GTK_CONTAINER(scroll), fav->priv->tree);

  gtk_widget_show (fav->priv->tree);
  g_object_unref (G_OBJECT (fav->priv->model));
}

static void
drag_data_received_handl (GtkWidget *widget,
			  GdkDragContext *context,
			  gint x,
			  gint y,
			  GtkSelectionData *selection_data,
			  guint target_type,
			  guint time,
			  VinagreFav *fav)
{
  gchar *_sdata;
  gboolean success = FALSE;

  if ((selection_data != NULL) && (gtk_selection_data_get_length (selection_data) >= 0))
    {
      switch (target_type)
	{
	  case TARGET_VINAGRE:
	    _sdata = (gchar*)gtk_selection_data_get_data (selection_data);
	    success = TRUE;
	   break;

	  default:
	    g_print ("nothing good");
	}
    }

  gtk_drag_finish (context, success, FALSE, time);

  if (success)
    {
      VinagreConnection *conn;
      gchar **info;
      gchar *error = NULL;

      info = g_strsplit (_sdata, "||", 2);
      if (g_strv_length (info) != 2)
	{
	  vinagre_utils_show_error_dialog (_("Invalid operation"),
				    _("Data received from drag&drop operation is invalid."),
				    GTK_WINDOW (fav->priv->window));
	  return;
	}

      conn = vinagre_connection_new_from_string (info[1], &error, FALSE);
      if (!conn)
	{
	  g_strfreev (info);
	  vinagre_utils_show_error_dialog (NULL,
				    error ? error : _("Unknown error"),
				    GTK_WINDOW (fav->priv->window));
	  g_free (error);
	  return;
	}

      vinagre_connection_set_name (conn, info[0]);
      vinagre_bookmarks_add (vinagre_bookmarks_get_default (),
			     conn,
			     GTK_WINDOW (fav->priv->window));

      g_strfreev (info);
      g_object_unref (conn);
    }
}

static gboolean
drag_drop_handl (GtkWidget *widget,
		 GdkDragContext *context,
		 gint x,
		 gint y,
		 guint time,
		 gpointer user_data)
{
  gboolean is_valid_drop_site;
  GdkAtom  target_type;
  GList    *targets;

  is_valid_drop_site = FALSE;
  targets = gdk_drag_context_list_targets (context);

  if (targets)
    {
      target_type = GDK_POINTER_TO_ATOM (g_list_nth_data (targets, TARGET_VINAGRE));
      gtk_drag_get_data (widget,
			 context,
			 target_type,
			 time);
      is_valid_drop_site = TRUE;
    }

  return  is_valid_drop_site;
}

static void
vinagre_fav_init (VinagreFav *fav)
{
  fav->priv = VINAGRE_FAV_GET_PRIVATE (fav);
  fav->priv->selected = NULL;

  gtk_frame_set_shadow_type (GTK_FRAME (fav), GTK_SHADOW_ETCHED_IN);
  fav->priv->box = GTK_BOX (gtk_vbox_new (FALSE, 0));
  gtk_container_add (GTK_CONTAINER (fav), GTK_WIDGET (fav->priv->box));
}

GtkWidget *
vinagre_fav_new (VinagreWindow *window)
{
  g_return_val_if_fail (VINAGRE_IS_WINDOW (window), NULL);

  return GTK_WIDGET (g_object_new (VINAGRE_TYPE_FAV,
				   "window", window,
				   NULL));
}

static void
vinagre_fav_hide (GtkButton *button, VinagreFav *fav)
{
  GtkAction *action;

  action = gtk_action_group_get_action (fav->priv->window->priv->always_sensitive_action_group,
					"ViewSidePanel");
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);
}

static GtkWidget *
_create_small_close_button ()
{
  GtkRcStyle *rcstyle;
  GtkWidget *image;
  GtkWidget *close_button;

  close_button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (close_button),
			 GTK_RELIEF_NONE);
  /* don't allow focus on the close button */
  gtk_button_set_focus_on_click (GTK_BUTTON (close_button), FALSE);

  /* make it as small as possible */
  rcstyle = gtk_rc_style_new ();
  rcstyle->xthickness = rcstyle->ythickness = 0;
  gtk_widget_modify_style (close_button, rcstyle);
  g_object_unref (rcstyle),

  image = gtk_image_new_from_stock (GTK_STOCK_CLOSE,
				    GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (close_button), image);

  return close_button;
}

static void
vinagre_fav_create_title (VinagreFav *fav)
{
  GtkWidget *box, *label, *close_button, *image;

  box = gtk_hbox_new (FALSE, 0);

 /* setup image */
  image = gtk_image_new_from_icon_name ("user-bookmarks", GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_misc_set_alignment (GTK_MISC (image), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);

 /* setup label */
  label = gtk_label_new (_("Bookmarks"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (label), 6, 0);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

  /* setup small close button */
  close_button = _create_small_close_button ();
  gtk_box_pack_start (GTK_BOX (box), close_button, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text (close_button, _("Hide panel"));
  g_signal_connect (close_button,
		    "clicked",
		    G_CALLBACK (vinagre_fav_hide),
		    fav);

  gtk_box_pack_start (fav->priv->box, box, FALSE, FALSE, 0);
  gtk_widget_show_all (box);
}

static void
protocol_added_removed_cb (VinagrePluginsEngine *engine,
			   VinagreProtocol      *protocol,
			   VinagreFav           *fav)
{
  vinagre_fav_update_list (fav);
}

static void
vinagre_fav_constructed (GObject *object)
{
  VinagrePluginsEngine *engine;
  VinagreFav *fav = VINAGRE_FAV (object);

  if (G_OBJECT_CLASS (vinagre_fav_parent_class)->constructed)
    G_OBJECT_CLASS (vinagre_fav_parent_class)->constructed (object);

  vinagre_fav_create_title (fav);
  vinagre_fav_create_toolbar (fav);
  vinagre_fav_create_tree (fav);

  gtk_drag_dest_set (fav->priv->tree,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     vinagre_target_list,
		     G_N_ELEMENTS (vinagre_target_list),
		     GDK_ACTION_COPY);

  g_signal_connect (fav->priv->tree,
		    "drag-data-received",
		    G_CALLBACK(drag_data_received_handl),
		    fav);
  g_signal_connect (fav->priv->tree,
		    "drag-drop",
		    G_CALLBACK (drag_drop_handl),
		    NULL);

  g_signal_connect_swapped (vinagre_bookmarks_get_default (),
                            "changed",
                            G_CALLBACK (vinagre_fav_update_list),
                            fav);
#ifdef VINAGRE_HAVE_AVAHI
  g_signal_connect_swapped (vinagre_mdns_get_default (),
                            "changed",
                            G_CALLBACK (vinagre_fav_update_list),
                            fav);
#endif

  engine = vinagre_plugins_engine_get_default ();
  g_signal_connect_after (engine,
			  "protocol-added",
			  G_CALLBACK (protocol_added_removed_cb),
			  fav);
  g_signal_connect_after (engine,
			  "protocol-removed",
			  G_CALLBACK (protocol_added_removed_cb),
			  fav);
}

static void
vinagre_fav_class_init (VinagreFavClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = vinagre_fav_get_property;
  object_class->set_property = vinagre_fav_set_property;
  object_class->constructed  = vinagre_fav_constructed;

  g_object_class_install_property (object_class,
				   PROP_WINDOW,
				   g_param_spec_object ("window",
							"Window",
							"The VinagreWindow this panel is associated with",
							 VINAGRE_TYPE_WINDOW,
							 G_PARAM_READWRITE |
							 G_PARAM_CONSTRUCT_ONLY));

  signals[FAV_ACTIVATED] =
		g_signal_new ("fav-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (VinagreFavClass, fav_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

  signals[FAV_SELECTED] =
		g_signal_new ("fav-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (VinagreFavClass, fav_selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_OBJECT);

  g_type_class_add_private (object_class, sizeof (VinagreFavPrivate));
}

static void
vinagre_fav_fill_bookmarks (GtkTreeStore *store, GSList *list, GtkTreeIter *parent_iter, gboolean is_avahi)
{
  GdkPixbuf             *pixbuf;
  GtkIconTheme          *icon_theme;
  gchar                 *name;
  VinagreBookmarksEntry *entry;
  GSList                *l;
  GtkTreeIter            iter;
  VinagreConnection     *conn;
  VinagreProtocol       *ext;

 for (l = list; l; l = l->next)
    {
      entry = (VinagreBookmarksEntry *) l->data;
      switch (vinagre_bookmarks_entry_get_node (entry))
	{
	  case VINAGRE_BOOKMARKS_ENTRY_NODE_FOLDER:
	    icon_theme = gtk_icon_theme_get_default ();
	    pixbuf = gtk_icon_theme_load_icon  (icon_theme,
					        "folder",
						16,
						0,
						NULL);

	    gtk_tree_store_append (store, &iter, parent_iter);
	    gtk_tree_store_set (store, &iter,
				IMAGE_COL, pixbuf,
				NAME_COL, vinagre_bookmarks_entry_get_name (entry),
				ENTRY_COL, entry,
				IS_FOLDER_COL, TRUE,
				IS_GROUP_COL, FALSE,
				IS_AVAHI_COL, FALSE,
				-1);
	    if (pixbuf != NULL)
	      g_object_unref (pixbuf);

	    vinagre_fav_fill_bookmarks (store, vinagre_bookmarks_entry_get_children (entry), &iter, is_avahi);
	    break;

	  case VINAGRE_BOOKMARKS_ENTRY_NODE_CONN:
	    conn = vinagre_bookmarks_entry_get_conn (entry);
	    ext = vinagre_plugins_engine_get_plugin_by_protocol (vinagre_plugins_engine_get_default (),
								 vinagre_connection_get_protocol (conn));
	    if (!ext)
	      continue;

	    pixbuf = vinagre_protocol_get_icon (ext, 16);
	    name = vinagre_connection_get_best_name (conn);

	    gtk_tree_store_append (store, &iter, parent_iter);
	    gtk_tree_store_set (store, &iter,
				IMAGE_COL, pixbuf,
				NAME_COL, name,
				ENTRY_COL, entry,
				IS_FOLDER_COL, FALSE,
				IS_GROUP_COL, FALSE,
				IS_AVAHI_COL, is_avahi,
				-1);
	    g_free (name);
	    if (pixbuf != NULL)
	      g_object_unref (pixbuf);
	    break;

	  default:
	    g_assert_not_reached ();
	}
    }
}

gboolean
vinagre_fav_update_list (VinagreFav *fav)
{
  GtkTreeIter   parent_iter;
  GtkTreeStore *store;
  GSList       *list;
    
  g_return_val_if_fail (VINAGRE_IS_FAV (fav), FALSE);

  store = GTK_TREE_STORE (fav->priv->model);
  gtk_tree_store_clear (store);

  /* bookmarks */
  list = vinagre_bookmarks_get_all (vinagre_bookmarks_get_default ());

  vinagre_fav_fill_bookmarks (store, list, NULL, FALSE);

#ifdef VINAGRE_HAVE_AVAHI
  list = vinagre_mdns_get_all (vinagre_mdns_get_default ());
  if (!list)
    return FALSE;

  gtk_tree_store_append (store, &parent_iter, NULL);
  gtk_tree_store_set (store, &parent_iter,
                      NAME_COL, _("Hosts nearby"),
                      IS_GROUP_COL, TRUE,
                      IS_FOLDER_COL, FALSE,
                      IS_AVAHI_COL, FALSE,
                      -1);

  vinagre_fav_fill_bookmarks (store, list, NULL, TRUE);
#endif

  return FALSE;
}

const gchar *
vinagre_fav_get_ui_xml_filename (void)
{
  if (g_file_test (VINAGRE_FAV_UI_XML_FILE, G_FILE_TEST_EXISTS))
    return VINAGRE_FAV_UI_XML_FILE;
  else
    return VINAGRE_DATADIR "/" VINAGRE_FAV_UI_XML_FILE;
}

/* vim: set ts=8: */
