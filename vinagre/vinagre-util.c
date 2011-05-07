/*
 * vinagre-utils.c
 * This file is part of vinagre
 *
 * Copyright (C) 2007,2008,2009 - Jonh Wendell <wendell@bani.com.br>
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

#include <string.h>
#include <glib/gi18n.h>
#include "vinagre-util.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

void
vinagre_utils_show_many_errors (const gchar *title, GSList *items, GtkWindow *parent)
{
  GString *msg;
  GSList  *l;

  msg = g_string_new (NULL);

  for (l = items; l; l = l->next)
    g_string_append_printf (msg, "%s\n", (gchar *)l->data);

  vinagre_utils_show_error_dialog (title, msg->str, parent);
  g_string_free (msg, TRUE);
}

void
vinagre_utils_toggle_widget_visible (GtkWidget *widget)
{
  if (gtk_widget_get_visible (widget))
    gtk_widget_hide (widget);
  else
    gtk_widget_show_all (widget);
}

static const gchar *
_get_ui_filename (void)
{
    static const gchar ui_file[] = "vinagre.ui";

  if (g_file_test (ui_file, G_FILE_TEST_EXISTS))
    return ui_file;
  else
    return g_build_filename (VINAGRE_DATADIR, ui_file, NULL);
}

/**
 * vinagre_utils_get_builder:
 *
 * Return value: (allow-none) (transfer full):
 */
GtkBuilder *
vinagre_utils_get_builder (const gchar *filename)
{
  GtkBuilder *xml = NULL;
  GError     *error = NULL;
  gchar      *actual_filename;

  if (filename)
    actual_filename = g_strdup (filename);
  else
    actual_filename = g_strdup (_get_ui_filename ());

  xml = gtk_builder_new ();
  if (!gtk_builder_add_from_file (xml,
				  actual_filename,
				  &error))
    {
      GString *str = g_string_new (NULL);

      if (filename)
	g_string_append (str, _("A plugin tried to open an UI file but did not succeed, with the error message:"));
      else
	g_string_append (str, _("The program tried to open an UI file but did not succeed, with the error message:"));

      g_string_append_printf (str, "\n\n%s\n\n", error->message);
      g_string_append (str, _("Please check your installation."));
      vinagre_utils_show_error_dialog (_("Error loading UI file"), str->str, NULL);
      g_error_free (error);
      g_string_free (str, TRUE);
      g_object_unref (xml);
      xml = NULL;
    }

  g_free (actual_filename);
  return xml;
}

/*
 * Doubles underscore to avoid spurious menu accels.
 */
gchar *
vinagre_utils_escape_underscores (const gchar* text,
				  gssize       length)
{
	GString *str;
	const gchar *p;
	const gchar *end;

	g_return_val_if_fail (text != NULL, NULL);

	if (length < 0)
		length = strlen (text);

	str = g_string_sized_new (length);

	p = text;
	end = text + length;

	while (p != end)
	{
		const gchar *next;
		next = g_utf8_next_char (p);

		switch (*p)
		{
			case '_':
				g_string_append (str, "__");
				break;
			default:
				g_string_append_len (str, p, next - p);
				break;
		}

		p = next;
	}

	return g_string_free (str, FALSE);
}

gboolean
vinagre_utils_parse_boolean (const gchar* value)
{
  if (g_ascii_strcasecmp (value, "true") == 0 || strcmp (value, "1") == 0)
    return TRUE;

  return FALSE;
}

/**
 * vinagre_utils_ask_question:
 * @parent: transient parent, or NULL for none
 * @message: The message to be displayed, if it contains multiple lines,
 *  the first one is considered as the title.
 * @choices: NULL-terminated array of button's labels of the dialog
 * @choice: Place to store the selected button. Zero is the first.
 *
 * Displays a dialog with a message and some options to the user.
 *
 * Returns TRUE if the user has selected any option, FALSE if the dialog
 *  was canceled.
 */
gboolean
vinagre_utils_ask_question (GtkWindow  *parent,
			    const char *message,
			    char       **choices,
			    int        *choice)
{
  gchar **messages;
  GtkWidget *d;
  int i, n_choices, result;

  g_return_val_if_fail (message && choices && choice, FALSE);

  messages = g_strsplit (message, "\n", 2);

  d = gtk_message_dialog_new (parent,
			      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			      GTK_MESSAGE_QUESTION,
			      GTK_BUTTONS_NONE,
			      "%s",
			      messages[0]);

  if (g_strv_length (messages) > 1)
    gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (d),
						"%s",
						messages[1]);
  g_strfreev (messages);

  n_choices = g_strv_length (choices);
  for (i = 0; i < n_choices; i++)
    gtk_dialog_add_button (GTK_DIALOG (d), choices[i], i);

  result = gtk_dialog_run (GTK_DIALOG (d));
  gtk_widget_destroy (d);

  if (result == GTK_RESPONSE_NONE || result == GTK_RESPONSE_DELETE_EVENT)
    return FALSE;

  *choice = result;
  return TRUE;
}

typedef struct {
  GtkWidget *uname, *pw, *button;
} ControlOKButton;

static void
control_ok_button (GtkEditable *entry, ControlOKButton *data)
{
  gboolean enabled = TRUE;

  if (gtk_widget_get_visible (data->uname))
    enabled = enabled && gtk_entry_get_text_length (GTK_ENTRY (data->uname)) > 0;

  if (gtk_widget_get_visible (data->pw))
    enabled = enabled && gtk_entry_get_text_length (GTK_ENTRY (data->pw)) > 0;

  gtk_widget_set_sensitive (data->button, enabled);
}

gboolean
vinagre_utils_ask_credential (GtkWindow *parent,
			      gchar *kind,
			      gchar *host,
			      gboolean need_username,
			      gboolean need_password,
			      gint password_limit,
			      gchar **username,
			      gchar **password,
			      gboolean *save_in_keyring)
{
  GtkBuilder      *xml;
  GtkWidget       *password_dialog, *save_credential_check;
  GtkWidget       *password_label, *username_label, *image;
  int             result;
  ControlOKButton control;

  xml = vinagre_utils_get_builder (NULL);

  password_dialog = GTK_WIDGET (gtk_builder_get_object (xml, "auth_required_dialog"));
  if (parent)
    gtk_window_set_transient_for (GTK_WINDOW (password_dialog), parent);

  if (kind)
    {
       /* Translators: %s is a protocol, like VNC or SSH */
       gchar *str = g_strdup_printf (_("%s authentication is required"), kind);
       GtkWidget *auth_label = GTK_WIDGET (gtk_builder_get_object (xml, "auth_required_label"));
       gtk_label_set_label (GTK_LABEL (auth_label), str);
       g_free (str);
    }

  if (host)
    {
       GtkWidget *host_label = GTK_WIDGET (gtk_builder_get_object (xml, "host_label"));
       gtk_label_set_label (GTK_LABEL (host_label), host);
    }

  control.uname  = GTK_WIDGET (gtk_builder_get_object (xml, "username_entry"));
  control.pw     = GTK_WIDGET (gtk_builder_get_object (xml, "password_entry"));
  control.button = GTK_WIDGET (gtk_builder_get_object (xml, "ok_button"));
  password_label = GTK_WIDGET (gtk_builder_get_object (xml, "password_label"));
  username_label = GTK_WIDGET (gtk_builder_get_object (xml, "username_label"));
  save_credential_check = GTK_WIDGET (gtk_builder_get_object (xml, "save_credential_check"));

  image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION, GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (control.button), image);

  g_signal_connect (control.uname, "changed", G_CALLBACK (control_ok_button), &control);
  g_signal_connect (control.pw, "changed", G_CALLBACK (control_ok_button), &control);

  if (need_username)
    {
      if (username && *username)
        gtk_entry_set_text (GTK_ENTRY (control.uname), *username);
    }
  else
    {
      gtk_widget_hide (username_label);
      gtk_widget_hide (control.uname);
    }

  if (need_password)
    {
      gtk_entry_set_max_length (GTK_ENTRY (control.pw), password_limit);
      if (password && *password)
        gtk_entry_set_text (GTK_ENTRY (control.pw), *password);
    }
  else
    {
      gtk_widget_hide (password_label);
      gtk_widget_hide (control.pw);
    }

  result = gtk_dialog_run (GTK_DIALOG (password_dialog));
  if (result == -5)
    {
      if (username)
        {
          g_free (*username);
          if (gtk_entry_get_text_length (GTK_ENTRY (control.uname)) > 0)
	    *username = g_strdup (gtk_entry_get_text (GTK_ENTRY (control.uname)));
          else
	    *username = NULL;
	}

      if (password)
        {
          g_free (*password);
          if (gtk_entry_get_text_length (GTK_ENTRY (control.pw)) > 0)
	    *password = g_strdup (gtk_entry_get_text (GTK_ENTRY (control.pw)));
          else
	    *password = NULL;
	}

      if (save_in_keyring)
        *save_in_keyring = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (save_credential_check));
    }

  gtk_widget_destroy (GTK_WIDGET (password_dialog));
  g_object_unref (xml);

  return result == -5;
}

/* vim: set ts=8: */
