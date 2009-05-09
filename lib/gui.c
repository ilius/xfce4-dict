/*  $Id$
 *
 *  Copyright 2006-2009 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


/* Creation of main window and helper functions (GUI stuff). */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libxfcegui4/libxfcegui4.h>

#include "common.h"
#include "gui.h"
#include "sexy-icon-entry.h"
#include "searchentry.h"
#include "inline-icon.h"



static gboolean hovering_over_link = FALSE;
static GdkCursor *hand_cursor = NULL;
static GdkCursor *regular_cursor = NULL;
static gboolean entry_is_dirty = FALSE;
static const GdkColor error_color = { 0, 0x8000, 0, 0 }; /* dark red */


/* all textview_* functions are from the gtk-demo app to get links in the textview working */
static void textview_follow_if_link(GtkWidget *text_view, GtkTextIter *iter, DictData *dd)
{
	GSList *tags = NULL, *tagp = NULL;

	tags = gtk_text_iter_get_tags(iter);
	for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
	{
		GtkTextTag *tag = tagp->data;
		gchar *found_link;

		found_link = g_object_get_data(G_OBJECT(tag), "link");
		if (found_link != NULL)
		{
			gtk_entry_set_text(GTK_ENTRY(dd->main_entry), found_link);
			dict_search_word(dd, found_link);
			break;
		}
		g_object_get(G_OBJECT(tag), "name", &found_link, NULL);
		if (found_link != NULL && strcmp("link", found_link) == 0)
		{
			gboolean browser_started = dict_start_web_query(dd, dd->searched_word);
			if (browser_started && dd->is_plugin)
				gtk_widget_hide(dd->window);
			g_free(found_link);
			break;
		}
	}
	if (tags)
		g_slist_free(tags);
}


static gboolean textview_key_press_event(GtkWidget *text_view, GdkEventKey *event, DictData *dd)
{
	GtkTextIter iter;

	switch (event->keyval)
	{
		case GDK_Return:
		case GDK_KP_Enter:
		{
			gtk_text_buffer_get_iter_at_mark(dd->main_textbuffer, &iter,
				gtk_text_buffer_get_insert(dd->main_textbuffer));
			textview_follow_if_link(text_view, &iter, dd);
			break;
		}
		default:
			break;
	}

	return FALSE;
}


static gboolean textview_event_after(GtkWidget *text_view, GdkEvent *ev, DictData *dd)
{
	GtkTextIter start, end, iter;
	GdkEventButton *event;
	gint x, y;

	if (ev->type != GDK_BUTTON_RELEASE)
		return FALSE;

	event = (GdkEventButton *)ev;

	if (event->button != 1)
		return FALSE;

	/* we shouldn't follow a link if the user has selected something */
	gtk_text_buffer_get_selection_bounds(dd->main_textbuffer, &start, &end);
	if (gtk_text_iter_get_offset(&start) != gtk_text_iter_get_offset(&end))
		return FALSE;

	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view), GTK_TEXT_WINDOW_WIDGET,
		event->x, event->y, &x, &y);

	gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(text_view), &iter, x, y);

	textview_follow_if_link(text_view, &iter, dd);

	return FALSE;
}


static void textview_set_cursor_if_appropriate(GtkTextView *view, gint x, gint y, GdkWindow *win)
{
	GSList *tags = NULL, *tagp = NULL;
	GtkTextIter iter;
	gboolean hovering = FALSE;

	gtk_text_view_get_iter_at_location(view, &iter, x, y);

	tags = gtk_text_iter_get_tags(&iter);
	for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
	{
		GtkTextTag *tag = tagp->data;
		gchar *name;

		if (g_object_get_data(G_OBJECT(tag), "link") != NULL)
		{
			hovering = TRUE;
			break;
		}
		g_object_get(G_OBJECT(tag), "name", &name, NULL);
		if (name != NULL && strcmp("link", name) == 0)
		{
			hovering = TRUE;
			g_free(name);
			break;
		}
	}

	if (hovering != hovering_over_link)
	{
		hovering_over_link = hovering;

		if (hovering_over_link)
			gdk_window_set_cursor(win, hand_cursor);
		else
			gdk_window_set_cursor(win, regular_cursor);
	}

	if (tags)
		g_slist_free(tags);
}


static gboolean textview_motion_notify_event(GtkWidget *text_view, GdkEventMotion *event)
{
	gint x, y;

	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view), GTK_TEXT_WINDOW_WIDGET,
		event->x, event->y, &x, &y);

	textview_set_cursor_if_appropriate(GTK_TEXT_VIEW(text_view), x, y, event->window);

	gdk_window_get_pointer(text_view->window, NULL, NULL, NULL);

	return FALSE;
}


static gboolean textview_visibility_notify_event(GtkWidget *text_view, GdkEventVisibility *event)
{
	gint wx, wy, bx, by;

	gdk_window_get_pointer(text_view->window, &wx, &wy, NULL);

	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view),
		GTK_TEXT_WINDOW_WIDGET, wx, wy, &bx, &by);

	textview_set_cursor_if_appropriate(GTK_TEXT_VIEW(text_view), bx, by, event->window);

	return FALSE;
}


static void textview_popup_item_cb(GtkWidget *widget, DictData *dd)
{
	gchar *word;
	GtkTextIter start, end;

	if (! gtk_text_buffer_get_selection_bounds(dd->main_textbuffer, &start, &end))
	{
		gint wx, wy, bx, by;

		gdk_window_get_pointer(dd->main_textview->window, &wx, &wy, NULL);

		gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(dd->main_textview),
			GTK_TEXT_WINDOW_WIDGET, wx, wy, &bx, &by);

		gtk_text_buffer_get_iter_at_mark(dd->main_textbuffer, &start, dd->mark_click);
		if (! gtk_text_iter_starts_word(&start))
			gtk_text_iter_backward_word_start(&start);

		end = start;

		if (gtk_text_iter_inside_word(&end))
			gtk_text_iter_forward_word_end(&end);
	}

	word = gtk_text_buffer_get_text(dd->main_textbuffer, &start, &end, FALSE);

	gtk_entry_set_text(GTK_ENTRY(dd->main_entry), word);
	dict_search_word(dd, word);
	gtk_widget_grab_focus(dd->main_entry);

	g_free(word);
}


static void textview_populate_popup_cb(GtkTextView *textview, GtkMenu *menu, DictData *dd)
{
	GtkWidget *search = gtk_image_menu_item_new_from_stock(GTK_STOCK_FIND, NULL);
	GtkWidget *sep = gtk_separator_menu_item_new();

	gtk_widget_show(sep);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), sep);

	gtk_widget_show(search);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), search);

	g_signal_connect(search, "activate", G_CALLBACK(textview_popup_item_cb), dd);
}


static gboolean textview_button_press_cb(GtkTextView *view, GdkEventButton *event, DictData *dd)
{
	if (event->button == 3)
	{
		gint x, y;
		GtkTextIter iter;

		gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_TEXT, event->x, event->y, &x, &y);
		gtk_text_view_get_iter_at_location(view, &iter, x, y);
		gtk_text_buffer_move_mark(dd->main_textbuffer, dd->mark_click, &iter);
	}

	return FALSE;
}


void dict_gui_set_panel_entry_text(DictData *dd, const gchar *text)
{
	if (dd->panel_entry != NULL)
		gtk_entry_set_text(GTK_ENTRY(dd->panel_entry), text);
}


void dict_gui_status_add(DictData *dd, const gchar *format, ...)
{
	static gchar string[512];
	va_list args;

	string[0] = ' ';
	va_start(args, format);
	g_vsnprintf(string + 1, (sizeof string) - 1, format, args);
	va_end(args);

	gtk_statusbar_pop(GTK_STATUSBAR(dd->statusbar), 1);
	gtk_statusbar_push(GTK_STATUSBAR(dd->statusbar), 1, string);
	if (dd->verbose_mode)
		g_message("%s", string);
}


void dict_gui_clear_text_buffer(DictData *dd)
{
	GtkTextIter end_iter;

	gtk_text_buffer_get_start_iter(dd->main_textbuffer, &dd->textiter);
	gtk_text_buffer_get_end_iter(dd->main_textbuffer, &end_iter);
	gtk_text_buffer_delete(dd->main_textbuffer, &dd->textiter, &end_iter);

	gtk_widget_grab_focus(dd->main_entry);
}


static void entry_activate_cb(GtkEntry *entry, DictData *dd)
{
	const gchar *entered_text = gtk_entry_get_text(GTK_ENTRY(dd->main_entry));

	dict_search_word(dd, entered_text);
}


static void entry_icon_pressed_cb(SexyIconEntry *entry, gint icon_pos, gint button, DictData *dd)
{
	if (button != 1)
		return;

	if (icon_pos == SEXY_ICON_ENTRY_PRIMARY)
	{
		entry_activate_cb(NULL, dd);
		gtk_widget_grab_focus(dd->main_entry);
	}
	else if (icon_pos == SEXY_ICON_ENTRY_SECONDARY)
	{
		dict_gui_clear_text_buffer(dd);
		gtk_entry_set_text(GTK_ENTRY(dd->main_entry), "");
		dict_gui_set_panel_entry_text(dd, "");
		dict_gui_status_add(dd, _("Ready"));
	}
}


static void combo_changed_cb(GtkComboBox *combo, DictData *dd)
{
	gchar *text = gtk_combo_box_get_active_text(combo);

	dict_search_word(dd, text);

	g_free(text);
}


static void entry_changed_cb(GtkEditable *editable, DictData *dd)
{
	entry_is_dirty = TRUE;
}


static void entry_button_clicked_cb(GtkButton *button, DictData *dd)
{
	entry_activate_cb(NULL, dd);
	gtk_widget_grab_focus(dd->main_entry);
}


static gboolean entry_button_press_cb(GtkWidget *widget, GdkEventButton *event, DictData *dd)
{
	if (! entry_is_dirty)
	{
		entry_is_dirty = TRUE;
		if (event->button == 1)
			gtk_entry_set_text(GTK_ENTRY(widget), "");
	}

	return FALSE;
}


static const gchar *get_icon_name(const gchar *req1, const gchar *req2, const gchar *fallback)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();

	if (gtk_icon_theme_has_icon(theme, req1))
		return req1;
	else if (gtk_icon_theme_has_icon(theme, req2))
		return req2;
	else
		return fallback;
}


static void update_search_button(DictData *dd, GtkWidget *box)
{
	static GtkWidget *button = NULL;
	/* cache the image name to not lookup through the theme each time */
	static const gchar *web_image_name = NULL;
	GtkWidget *image = NULL;

	if (button == NULL)
	{
		button = gtk_button_new_from_stock("gtk-find");
		gtk_widget_show(button);
		gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
		g_signal_connect(button, "clicked", G_CALLBACK(entry_button_clicked_cb), dd);

		/* "internet-web-browser" is Tango, "web-browser" is Rodent, "gtk-find" is GTK */
		web_image_name = get_icon_name("internet-web-browser", "web-browser", "gtk-find");
	}

	switch (dd->mode_in_use)
	{
		case DICTMODE_DICT:
		{
			image = gtk_image_new_from_stock("gtk-find", GTK_ICON_SIZE_BUTTON);
			break;
		}
		case DICTMODE_WEB:
		{
			image = gtk_image_new_from_icon_name(web_image_name, GTK_ICON_SIZE_BUTTON);
			break;
		}
		case DICTMODE_SPELL:
		{
			image = gtk_image_new_from_stock("gtk-spell-check", GTK_ICON_SIZE_BUTTON);
			break;
		}
		default:
			break;
	}
	if (image != NULL)
	{
		gtk_button_set_image(GTK_BUTTON(button), image);
	}
}


static void search_mode_dict_toggled(GtkToggleButton *togglebutton, DictData *dd)
{
	if (gtk_toggle_button_get_active(togglebutton))
	{
		dd->mode_in_use = DICTMODE_DICT;
		gtk_widget_grab_focus(dd->main_entry);
		update_search_button(dd, NULL);
	}
}


static void search_mode_web_toggled(GtkToggleButton *togglebutton, DictData *dd)
{
	if (gtk_toggle_button_get_active(togglebutton))
	{
		dd->mode_in_use = DICTMODE_WEB;
		gtk_widget_grab_focus(dd->main_entry);
		update_search_button(dd, NULL);
	}
}


static void search_mode_spell_toggled(GtkToggleButton *togglebutton, DictData *dd)
{
	if (gtk_toggle_button_get_active(togglebutton))
	{
		dd->mode_in_use = DICTMODE_SPELL;
		gtk_widget_grab_focus(dd->main_entry);
		update_search_button(dd, NULL);
	}
}


const guint8 *dict_gui_get_icon_data(void)
{
	return dict_icon_data;
}


 static GtkWidget *create_file_menu(DictData *dd)
{
	GtkWidget *menubar, *file, *file_menu, *help, *help_menu, *menu_item;
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(dd->window), accel_group);

	menubar = gtk_menu_bar_new();

	file = gtk_menu_item_new_with_mnemonic(_("_File"));

	file_menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), file_menu);

	dd->pref_menu_item = gtk_image_menu_item_new_from_stock("gtk-preferences", accel_group);
	gtk_widget_add_accelerator(dd->pref_menu_item, "activate", accel_group,
			GDK_p, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_container_add(GTK_CONTAINER(file_menu), dd->pref_menu_item);

	gtk_container_add(GTK_CONTAINER(file_menu), gtk_separator_menu_item_new());

	dd->close_menu_item = gtk_image_menu_item_new_from_stock(
			(dd->is_plugin) ? "gtk-close" : "gtk-quit", accel_group);
	gtk_container_add(GTK_CONTAINER(file_menu), dd->close_menu_item);

	help = gtk_menu_item_new_with_mnemonic(_("_Help"));

	help_menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), help_menu);

	menu_item = gtk_image_menu_item_new_from_stock("gtk-about", accel_group);
	gtk_container_add(GTK_CONTAINER(help_menu), menu_item);
	g_signal_connect(menu_item, "activate", G_CALLBACK(dict_gui_about_dialog), dd);

	gtk_container_add(GTK_CONTAINER(menubar), file);
	gtk_container_add(GTK_CONTAINER(menubar), help);

	gtk_widget_show_all(menubar);

	return menubar;
}


void dict_gui_finalize(DictData *dd)
{
	if (hand_cursor)
		gdk_cursor_unref(hand_cursor);
	if (regular_cursor)
		gdk_cursor_unref(regular_cursor);
}


void dict_gui_create_main_window(DictData *dd)
{
	GtkWidget *main_box, *entry_box, *label_box;
	GtkWidget *sep, *align, *scrolledwindow_results;
	GdkPixbuf *icon;
	GtkWidget *method_chooser, *radio, *label;

	dd->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(dd->window), _("Dictionary"));
	gtk_window_set_default_size(GTK_WINDOW(dd->window), 500, 300);
	gtk_widget_set_name(dd->window, "Xfce4Dict");

	icon = gdk_pixbuf_new_from_inline(-1, dict_icon_data, FALSE, NULL);
	gtk_window_set_icon(GTK_WINDOW(dd->window), icon);
	g_object_unref(icon);

	main_box = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(main_box);
	gtk_container_add(GTK_CONTAINER(dd->window), main_box);

	gtk_box_pack_start(GTK_BOX(main_box), create_file_menu(dd), FALSE, TRUE, 0);

	/* entry box (label, entry, buttons) */
	entry_box = gtk_hbox_new(FALSE, 10);
	gtk_widget_show(entry_box);
	gtk_container_set_border_width(GTK_CONTAINER(entry_box), 2);
	gtk_box_pack_start(GTK_BOX(main_box), entry_box, FALSE, TRUE, 5);

	label_box = gtk_hbox_new(FALSE, 5);
	gtk_widget_show(label_box);
	gtk_box_pack_start(GTK_BOX(entry_box), label_box, TRUE, TRUE, 0);

	dd->main_combo = xfd_search_entry_new(_("Search term"));
	gtk_widget_show(dd->main_combo);
	gtk_box_pack_start(GTK_BOX(label_box), dd->main_combo, TRUE, TRUE, 0);
	g_signal_connect(dd->main_combo, "active-changed", G_CALLBACK(combo_changed_cb), dd);

	dd->main_entry = gtk_bin_get_child(GTK_BIN(dd->main_combo));
	g_signal_connect(dd->main_entry, "changed", G_CALLBACK(entry_changed_cb), dd);
	g_signal_connect(dd->main_entry, "activate", G_CALLBACK(entry_activate_cb), dd);
	g_signal_connect(dd->main_entry, "icon_released", G_CALLBACK(entry_icon_pressed_cb), dd);
	g_signal_connect(dd->main_entry, "button-press-event", G_CALLBACK(entry_button_press_cb), dd);

	update_search_button(dd, entry_box);

	/* just make some space */
	align = gtk_alignment_new(1, 0.5, 0, 0);
	gtk_alignment_set_padding(GTK_ALIGNMENT(align), 0, 0, 10, 0);
	gtk_widget_show(align);
	gtk_container_add(GTK_CONTAINER(align), gtk_label_new(""));
	gtk_box_pack_start(GTK_BOX(entry_box), align, FALSE, FALSE, 0);

	dd->close_button = gtk_button_new_from_stock((dd->is_plugin) ? "gtk-close" : "gtk-quit");
	gtk_widget_show(dd->close_button);
	gtk_box_pack_end(GTK_BOX(entry_box), dd->close_button, FALSE, FALSE, 2);

	/* insert it here and it will(hopefully) be placed before the Close button */
	sep = gtk_vseparator_new();
	gtk_widget_show(sep);
	gtk_box_pack_end(GTK_BOX(entry_box), sep, FALSE, FALSE, 5);

	/* search method chooser */
	method_chooser = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(method_chooser);
	gtk_box_pack_start(GTK_BOX(main_box), method_chooser, FALSE, FALSE, 0);

	label = gtk_label_new(_("Search with:"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(method_chooser), label, FALSE, FALSE, 6);

	radio = gtk_radio_button_new_with_mnemonic(NULL, _("_Dictionary Server"));
	gtk_widget_show(radio);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), (dd->mode_in_use == DICTMODE_DICT));
	g_signal_connect(radio, "toggled", G_CALLBACK(search_mode_dict_toggled), dd);
	gtk_box_pack_start(GTK_BOX(method_chooser), radio, FALSE, FALSE, 6);

	radio = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(radio), _("_Web Service"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), (dd->mode_in_use == DICTMODE_WEB));
	g_signal_connect(radio, "toggled", G_CALLBACK(search_mode_web_toggled), dd);
	gtk_widget_show(radio);
	gtk_box_pack_start(GTK_BOX(method_chooser), radio, FALSE, FALSE, 6);

	radio = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(radio), _("_Spell Checker"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), (dd->mode_in_use == DICTMODE_SPELL));
	g_signal_connect(radio, "toggled", G_CALLBACK(search_mode_spell_toggled), dd);
	gtk_widget_show(radio);
	gtk_box_pack_start(GTK_BOX(method_chooser), radio, FALSE, FALSE, 6);

	/* results area */
	scrolledwindow_results = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_show(scrolledwindow_results);
	gtk_container_set_border_width(GTK_CONTAINER(scrolledwindow_results), 4);
	gtk_box_pack_start(GTK_BOX(main_box), scrolledwindow_results, TRUE, TRUE, 0);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwindow_results),
								GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow_results),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	/* searched words textview stuff */
	dd->main_textview = gtk_text_view_new();
	gtk_widget_set_name(dd->window, "Xfce4DictTextView");
	gtk_text_view_set_editable(GTK_TEXT_VIEW(dd->main_textview), FALSE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(dd->main_textview), 5);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(dd->main_textview), 5);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(dd->main_textview), GTK_WRAP_WORD);
	dd->main_textbuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(dd->main_textview));
	gtk_text_buffer_create_tag(dd->main_textbuffer,
			"bold",
			"weight", PANGO_WEIGHT_BOLD,
			"style", PANGO_STYLE_ITALIC,
			"indent", 10,
			"pixels-below-lines", 5, NULL);
	gtk_text_buffer_create_tag(dd->main_textbuffer,
			"error",
			"style", PANGO_STYLE_ITALIC,
			"foreground-gdk", &error_color, NULL);
	dd->phon_tag = gtk_text_buffer_create_tag(dd->main_textbuffer,
			"phonetic",
			"style", PANGO_STYLE_ITALIC,
			"foreground-gdk", dd->phon_color, NULL);
	dd->link_tag = gtk_text_buffer_create_tag(dd->main_textbuffer,
			"link",
			"underline", PANGO_UNDERLINE_SINGLE,
			"foreground-gdk", dd->link_color, NULL);

	/* support for links (cross-references) for dictd responses */
	{
		hand_cursor = gdk_cursor_new(GDK_HAND2);
		regular_cursor = gdk_cursor_new(GDK_XTERM);

		g_signal_connect(dd->main_textview, "key-press-event",
			G_CALLBACK(textview_key_press_event), dd);
		g_signal_connect(dd->main_textview, "event-after",
			G_CALLBACK(textview_event_after), dd);
		g_signal_connect(dd->main_textview, "motion-notify-event",
			G_CALLBACK(textview_motion_notify_event), NULL);
		g_signal_connect(dd->main_textview, "visibility-notify-event",
			G_CALLBACK(textview_visibility_notify_event), NULL);
	}
	/* support for 'Search' menu item in the textview popup menu */
	{
		GtkTextIter start;
		gtk_text_buffer_get_bounds(dd->main_textbuffer, &start, &start);
		dd->mark_click = gtk_text_buffer_create_mark(dd->main_textbuffer, NULL, &start, TRUE);

		g_signal_connect(dd->main_textview, "button-press-event",
			G_CALLBACK(textview_button_press_cb), dd);
		g_signal_connect(dd->main_textview, "populate-popup",
			G_CALLBACK(textview_populate_popup_cb), dd);
	}

	gtk_widget_show(dd->main_textview);
	gtk_container_add(GTK_CONTAINER(scrolledwindow_results), dd->main_textview);

	/* status bar */
	dd->statusbar = gtk_statusbar_new();
	gtk_widget_show(dd->statusbar);
	gtk_box_pack_end(GTK_BOX(main_box), dd->statusbar, FALSE, FALSE, 0);

	/* DnD */
	g_signal_connect(dd->main_entry, "drag-data-received", G_CALLBACK(dict_drag_data_received), dd);
	/* TODO: find a way to get this working, the textview doesn't receive anything as long as it isn't
	 * editable. scrolledwindow_results and a surrounding event box as receivers also don't work. */
	g_signal_connect(dd->main_textview, "drag-data-received", G_CALLBACK(dict_drag_data_received), dd);

	/* use the saved window geometry */
	if (dd->geometry[0] != -1)
	{
		gtk_window_move(GTK_WINDOW(dd->window), dd->geometry[0], dd->geometry[1]);
		gtk_window_set_default_size(GTK_WINDOW(dd->window), dd->geometry[2], dd->geometry[3]);
		if (dd->geometry[4] == 1)
			gtk_window_maximize(GTK_WINDOW(dd->window));
	}
}


void dict_gui_show_main_window(DictData *dd)
{
	gtk_widget_show(dd->window);
	gtk_window_deiconify(GTK_WINDOW(dd->window));
	gtk_window_present(GTK_WINDOW(dd->window));
}


static void about_activate_link(GtkAboutDialog *about, const gchar *ref, gpointer data)
{
	gchar *cmd = g_strconcat("xdg-open ", ref, NULL);
	g_spawn_command_line_async(cmd, NULL);
	g_free(cmd);
}


void dict_gui_about_dialog(GtkWidget *widget, DictData *dd)
{
	const gchar *authors[]= { "Enrico Tröger <enrico@xfce.org>", NULL };
	const gchar *title = _("Xfce4 Dictionary");
	GdkPixbuf *logo = gdk_pixbuf_new_from_inline(-1, dict_icon_data, FALSE, NULL);

	gtk_about_dialog_set_email_hook(about_activate_link, NULL, NULL);
	gtk_about_dialog_set_url_hook(about_activate_link, NULL, NULL);
	gtk_show_about_dialog(GTK_WINDOW(dd->window),
		"destroy-with-parent", TRUE,
		"authors", authors,
		"comments", _("A client program to query different dictionaries."),
		"copyright", _("Copyright \302\251 2006-2009 Enrico Tröger"),
		"website", "http://goodies.xfce.org/projects/applications/xfce4-dict",
		"logo", logo,
		"translator-credits", _("translator-credits"),
		"license", XFCE_LICENSE_GPL,
		"version", PACKAGE_VERSION,
#if GTK_CHECK_VERSION(2,11,0)
		"program-name", title,
#else
		"name", title,
#endif
		  NULL);

	if (logo != NULL)
		g_object_unref(logo);
}


void dict_gui_query_geometry(DictData *dd)
{
	gtk_window_get_position(GTK_WINDOW(dd->window),	&dd->geometry[0], &dd->geometry[1]);
	gtk_window_get_size(GTK_WINDOW(dd->window),	&dd->geometry[2], &dd->geometry[3]);

	if (gdk_window_get_state(dd->window->window) & GDK_WINDOW_STATE_MAXIMIZED)
		dd->geometry[4] = 1;
	else
		dd->geometry[4] = 0;
}
