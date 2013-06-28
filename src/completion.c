/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2012-2013 Daniel Carl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */

#include "main.h"
#include "completion.h"
#include "util.h"
#include "history.h"
#include "bookmark.h"
#include "command.h"
#include "setting.h"

#define TAG_INDICATOR '!'
#define COMP_ITEM 0

extern VbCore vb;

static struct {
    int   count;   /* command count before the completed content */
    char  *prefix; /* prefix that marks the completion ':', '/', ':open', ... */
    int   active;  /* number of the current active tree item */
    char  *text;   /* text of the current active tree item */
} comp;

typedef struct {
    GtkTreeView *tree;
    gboolean    back;
} cursor;

static gboolean init_completion(GList *source);
static void show(GtkTreeView *tree);
static void update(GtkTreeView *tree, gboolean back);
static gboolean tree_selection_func(GtkTreeSelection *selection,
    GtkTreeModel *model, GtkTreePath *path, gboolean selected, gpointer data);

gboolean completion_complete(gboolean back)
{
    VbInputType type;
    const char *input, *prefix, *suffix;
    GList *source = NULL;
    gboolean hasItems = false;

    input = GET_TEXT();
    type  = vb_get_input_parts(input, &prefix, &suffix);

    if (vb.state.mode & VB_MODE_COMPLETE) {
        if (comp.text && !strcmp(input, comp.text)) {
            /* step through the next/prev completion item */
            update(GTK_TREE_VIEW(vb.gui.compbox), back);
            return true;
        }
        /* if current input isn't the content of the completion item, stop
         * completion and start it after that again */
        completion_clean();
    }

    /* don't disturb other command sub modes - complete only if no sub mode
     * is set before */
    if (vb.state.mode != VB_MODE_COMMAND) {
        return false;
    }

    if (type == VB_INPUT_SET) {
        source = g_list_sort(setting_get_by_prefix(suffix), (GCompareFunc)g_strcmp0);
        hasItems = init_completion(source);
        g_list_free(source);
    } else if (type == VB_INPUT_OPEN || type == VB_INPUT_TABOPEN) {
        /* if search string begins with TAG_INDICATOR lookup the bookmarks */
        if (suffix && *suffix == TAG_INDICATOR) {
            source = bookmark_get_by_tags(suffix + 1);
            hasItems = init_completion(source);
        } else {
            source = history_get_by_tags(HISTORY_URL, suffix);
            hasItems = init_completion(source);
        }
        g_list_free_full(source, (GDestroyNotify)g_free);
    } else if (type == VB_INPUT_COMMAND) {
        char *command = NULL;
        /* remove counts before command and save it to print it later in inputbox */
        comp.count = g_ascii_strtoll(suffix, &command, 10);

        source = g_list_sort(command_get_by_prefix(command), (GCompareFunc)g_strcmp0);
        hasItems = init_completion(source);
        g_list_free(source);
    } else if (type == VB_INPUT_SEARCH_FORWARD || type == VB_INPUT_SEARCH_BACKWARD) {
        source = g_list_sort(history_get_by_tags(HISTORY_SEARCH, suffix), (GCompareFunc)g_strcmp0);
        hasItems = init_completion(source);
        g_list_free_full(source, (GDestroyNotify)g_free);
    }

    if (!hasItems) {
        return false;
    }

    vb_set_mode(VB_MODE_COMMAND | VB_MODE_COMPLETE, false);

    OVERWRITE_STRING(comp.prefix, prefix);
    show(GTK_TREE_VIEW(vb.gui.compbox));

    return true;
}

void completion_clean(void)
{
    if (vb.gui.compbox) {
        gtk_widget_destroy(vb.gui.compbox);
        vb.gui.compbox = NULL;
    }
    OVERWRITE_STRING(comp.prefix, NULL);
    OVERWRITE_STRING(comp.text, NULL);
    comp.count = 0;
    comp.active = 0;

    /* remove completion flag from mode */
    vb.state.mode &= ~VB_MODE_COMPLETE;
}

static gboolean init_completion(GList *source)
{
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GtkRequisition size;
    Gui *gui = &vb.gui;
    gboolean hasItems = (source != NULL);
    int height;

    /* init the tree view and the list store */
    gui->compbox = gtk_tree_view_new();
    gtk_box_pack_end(GTK_BOX(gui->box), gui->compbox, false, false, 0);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->compbox), false);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
        "font-desc", vb.style.comp_font,
        "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
        NULL
    );

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->compbox));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    gtk_tree_selection_set_select_function(selection, tree_selection_func, NULL, NULL);

    VB_WIDGET_OVERRIDE_COLOR(gui->compbox, GTK_STATE_NORMAL, &vb.style.comp_fg[VB_COMP_NORMAL]);
    VB_WIDGET_OVERRIDE_TEXT(gui->compbox, GTK_STATE_NORMAL, &vb.style.comp_fg[VB_COMP_NORMAL]);
    VB_WIDGET_OVERRIDE_BASE(gui->compbox, GTK_STATE_NORMAL, &vb.style.comp_bg[VB_COMP_NORMAL]);
    VB_WIDGET_OVERRIDE_BACKGROUND(gui->compbox, GTK_STATE_NORMAL, &vb.style.comp_bg[VB_COMP_NORMAL]);

    VB_WIDGET_OVERRIDE_COLOR(gui->compbox, GTK_STATE_ACTIVE, &vb.style.comp_fg[VB_COMP_ACTIVE]);
    VB_WIDGET_OVERRIDE_TEXT(gui->compbox, GTK_STATE_ACTIVE, &vb.style.comp_fg[VB_COMP_ACTIVE]);
    VB_WIDGET_OVERRIDE_BASE(gui->compbox, GTK_STATE_ACTIVE, &vb.style.comp_bg[VB_COMP_ACTIVE]);
    VB_WIDGET_OVERRIDE_BACKGROUND(gui->compbox, GTK_STATE_ACTIVE, &vb.style.comp_bg[VB_COMP_ACTIVE]);

    gtk_tree_view_insert_column_with_attributes(
        GTK_TREE_VIEW(gui->compbox), -1, "", renderer, "text", COMP_ITEM, NULL
    );

    store = gtk_list_store_new(1, G_TYPE_STRING);

    for (GList *l = source; l; l = l->next) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COMP_ITEM, l->data, -1);
    }

    /* add the model after inserting the items - that's faster */
    gtk_tree_view_set_model(GTK_TREE_VIEW(gui->compbox), GTK_TREE_MODEL(store));
    g_object_unref(store);

    /* use max 1/3 of window height for the completion */
#ifdef HAS_GTK3
    gtk_widget_get_preferred_size(gui->compbox, NULL, &size);
#else
    gtk_widget_size_request(gui->compbox, &size);
#endif
    gtk_window_get_size(GTK_WINDOW(gui->window), NULL, &height);
    height /= 3;
    if (size.height > height) {
        gtk_widget_set_size_request(gui->compbox, -1, height);
    }

    return hasItems;
}

/* allow to change the direction of display */
static void show(GtkTreeView *tree)
{
    GtkTreePath *path;

    /* this prevents the first item to be placed out of view if the completion
     * is shown */
    gtk_widget_show(GTK_WIDGET(tree));
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    /* select the first completion item */
    path = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_view_set_cursor(tree, path, NULL, false);
    gtk_tree_path_free(path);
}

static void update(GtkTreeView *tree, gboolean back)
{
    int rows;
    GtkTreePath *path;

    rows = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(tree), NULL);
    if (back) {
        /* step back */
        if (--comp.active < 0) {
            comp.active = rows - 1;
        }
    } else {
        /* step forward */
        if (++comp.active >= rows) {
            comp.active = 0;
        }
    }

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    /* get new path and move cursor to it */
    path = gtk_tree_path_new_from_indices(comp.active, -1);
    gtk_tree_view_set_cursor(tree, path, NULL, false);
    gtk_tree_path_free(path);
}

static gboolean tree_selection_func(GtkTreeSelection *selection,
    GtkTreeModel *model, GtkTreePath *path, gboolean selected, gpointer data)
{
    char *value;
    GtkTreeIter iter;

    /* if not selected means the item is going to be selected which we are
     * interested in */
    if (!selected && gtk_tree_model_get_iter(model, &iter, path)) {
        gtk_tree_model_get(model, &iter, COMP_ITEM, &value, -1);
        /* save the content of the selected item so wen can access it easy */
        if (comp.text) {
            g_free(comp.text);
            comp.text = NULL;
        }
        if (comp.count) {
            comp.text = g_strdup_printf("%s%d%s", comp.prefix, comp.count, value);
        } else {
            comp.text = g_strdup_printf("%s%s", comp.prefix, value);
        }
        /* print the text also into inputbox */
        vb_echo_force(VB_MSG_NORMAL, false, "%s", comp.text);
        g_free(value);
    }

    return true;
}
