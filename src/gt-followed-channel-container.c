/*
 *  This file is part of GNOME Twitch - 'Enjoy Twitch on your GNU/Linux desktop'
 *  Copyright © 2017 Vincent Szolnoky <vinszent@vinszent.com>
 *
 *  GNOME Twitch is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GNOME Twitch is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNOME Twitch. If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>
#include "gt-followed-channel-container.h"
#include "gt-item-container.h"
#include "gt-twitch.h"
#include "gt-app.h"
#include "gt-channels-container-child.h"
#include "gt-win.h"

#define TAG "GtFollowedChannelContainer"
#include "gnome-twitch/gt-log.h"

#define CHILD_WIDTH 350
#define CHILD_HEIGHT 230
#define APPEND_EXTRA FALSE

typedef struct
{
    gchar* query;
    GtkWidget* item_flow;
} GtFollowedChannelContainerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(GtFollowedChannelContainer, gt_followed_channel_container, GT_TYPE_ITEM_CONTAINER);

enum
{
    PROP_0,
    PROP_QUERY,
    NUM_PROPS
};

static GParamSpec* props[NUM_PROPS];

static void
get_properties(GtItemContainer* self,
    gint* child_width, gint* child_height, gboolean* append_extra,
    gchar** empty_label_text, gchar** empty_sub_label_text, gchar** empty_image_name,
    gchar** error_label_text, gchar** fetching_label_text)
{
    *child_width = CHILD_WIDTH;
    *child_height = CHILD_HEIGHT;
    *append_extra = APPEND_EXTRA;
    *empty_label_text = g_strdup(_("No channels followed"));
    *empty_sub_label_text = g_strdup(_("Follow channels that you like for them to show up here!"));
    *error_label_text = g_strdup(_("An error occurred when fetching the games"));
    *empty_image_name = g_strdup("emblem-favorite-symbolic");
    *fetching_label_text = g_strdup(_("Fetching channels"));
}

static GtkWidget*
create_child(gpointer data)
{
    g_assert(GT_IS_CHANNEL(data));

    return GTK_WIDGET(gt_channels_container_child_new(GT_CHANNEL(data)));
}

static void
activate_child(GtItemContainer* item_container,
    gpointer _child)
{
    g_assert(GT_IS_FOLLOWED_CHANNEL_CONTAINER(item_container));
    g_assert(GT_IS_CHANNELS_CONTAINER_CHILD(_child));

    GtChannelsContainerChild* child = GT_CHANNELS_CONTAINER_CHILD(_child);

    if (gt_channel_is_online(child->channel))
    {
        gt_win_open_channel(GT_WIN_ACTIVE,
            GT_CHANNELS_CONTAINER_CHILD(child)->channel);
    }
}

static void
fetch_items(GTask* task,
    gpointer source, gpointer task_data,
    GCancellable* cancel)
{
    g_assert(G_IS_TASK(task));

    if (g_task_return_error_if_cancelled(task))
        return;

    FetchItemsData* data = task_data;

    g_assert_nonnull(data);

    g_task_return_pointer(task, main_app->fav_mgr->follow_channels, NULL);
}

static void
finished_loading_follows_cb(GtFollowsManager* mgr,
    gpointer udata)
{
    g_assert(GT_IS_FOLLOWED_CHANNEL_CONTAINER(udata));

    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(udata);
    GtFollowedChannelContainerPrivate* priv = gt_followed_channel_container_get_instance_private(self);

    gt_item_container_refresh(GT_ITEM_CONTAINER(self));
}

static void
channel_followed_cb(GtFollowsManager* mgr,
    GtChannel* chan, gpointer udata)
{
    g_assert(GT_IS_FOLLOWED_CHANNEL_CONTAINER(udata));
    g_assert(GT_IS_CHANNEL(chan));
    g_assert(GT_IS_FOLLOWS_MANAGER(mgr));

    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(udata);

    //TODO: Ideally we should just add the channel instead of refreshing
    // the entire container.
    gt_item_container_refresh(GT_ITEM_CONTAINER(self));
}

static void
channel_unfollowed_cb(GtFollowsManager* mgr,
    GtChannel* chan, gpointer udata)
{
    g_assert(GT_IS_FOLLOWED_CHANNEL_CONTAINER(udata));
    g_assert(GT_IS_CHANNEL(chan));
    g_assert(GT_IS_FOLLOWS_MANAGER(mgr));

    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(udata);

    gt_item_container_refresh(GT_ITEM_CONTAINER(self));
}

static gboolean
filter_by_name(GtkFlowBoxChild* _child,
    gpointer udata)
{
    g_assert(GT_IS_CHANNELS_CONTAINER_CHILD(_child));
    g_assert(GT_IS_FOLLOWED_CHANNEL_CONTAINER(udata));

    GtChannelsContainerChild* child = GT_CHANNELS_CONTAINER_CHILD(_child);
    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(udata);
    GtFollowedChannelContainerPrivate* priv = gt_followed_channel_container_get_instance_private(self);
    gboolean ret = FALSE;

    if (!priv->query)
        ret = TRUE;
    else
    {
        gchar* name = g_utf8_casefold(gt_channel_get_name(child->channel), -1);
        gchar* query = g_utf8_casefold(priv->query, -1);

        ret = g_strrstr(name, query) != NULL;

        g_free(name);
        g_free(query);
    }

    return ret;
}

static gint
sort_by_name_and_online(GtkFlowBoxChild* _child1,
    GtkFlowBoxChild* _child2, gpointer udata)
{
    g_assert(GT_IS_FOLLOWED_CHANNEL_CONTAINER(udata));
    g_assert(GT_IS_CHANNELS_CONTAINER_CHILD(_child1));
    g_assert(GT_IS_CHANNELS_CONTAINER_CHILD(_child2));

    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(udata);
    GtChannelsContainerChild* child1 = GT_CHANNELS_CONTAINER_CHILD(_child1);
    GtChannelsContainerChild* child2 = GT_CHANNELS_CONTAINER_CHILD(_child2);
    gboolean online1;
    gboolean online2;
    g_autofree gchar* name1;
    g_autofree gchar* name2;
    gint ret = 0;

    g_object_get(child1->channel,
                 "online", &online1,
                 "name", &name1,
                 NULL);
    g_object_get(child2->channel,
                 "online", &online2,
                 "name", &name2,
                 NULL);

    if(online1 && !online2)
        ret = -1;
    else if (!online1 && online2)
        ret = 1;
    else
        ret = g_strcmp0(name1, name2);

    return ret;
}

static void
get_property(GObject* obj,
    guint prop,
    GValue* val,
    GParamSpec* pspec)
{
    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(obj);
    GtFollowedChannelContainerPrivate* priv = gt_followed_channel_container_get_instance_private(self);

    switch (prop)
    {
        case PROP_QUERY:
            g_value_set_string(val, priv->query);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
set_property(GObject* obj,
    guint prop,
    const GValue* val,
    GParamSpec* pspec)
{
    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(obj);
    GtFollowedChannelContainerPrivate* priv = gt_followed_channel_container_get_instance_private(self);

    switch (prop)
    {
        case PROP_QUERY:
            g_clear_pointer(&priv->query, g_free);
            priv->query = g_value_dup_string(val);

            gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(priv->item_flow));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, pspec);
    }
}

static void
constructed(GObject* obj)
{
    GtFollowedChannelContainer* self = GT_FOLLOWED_CHANNEL_CONTAINER(obj);
    GtFollowedChannelContainerPrivate* priv = gt_followed_channel_container_get_instance_private(self);

    gtk_flow_box_set_filter_func(GTK_FLOW_BOX(priv->item_flow),
        (GtkFlowBoxFilterFunc) filter_by_name,
        self, NULL);

    gtk_flow_box_set_sort_func(GTK_FLOW_BOX(priv->item_flow),
        (GtkFlowBoxSortFunc) sort_by_name_and_online, self, NULL);

    //TODO: Need to refresh everytime a channel is followed or unfollowed
    g_signal_connect(main_app->fav_mgr, "finished-loading-follows", G_CALLBACK(finished_loading_follows_cb), self);
    g_signal_connect(main_app->fav_mgr, "channel-followed", G_CALLBACK(channel_followed_cb), self);
    g_signal_connect(main_app->fav_mgr, "channel-unfollowed", G_CALLBACK(channel_unfollowed_cb), self);

    gt_item_container_refresh(GT_ITEM_CONTAINER(self));

    G_OBJECT_CLASS(gt_followed_channel_container_parent_class)->constructed(obj);
}

static void
gt_followed_channel_container_class_init(GtFollowedChannelContainerClass* klass)
{
    G_OBJECT_CLASS(klass)->constructed = constructed;
    G_OBJECT_CLASS(klass)->get_property = get_property;
    G_OBJECT_CLASS(klass)->set_property = set_property;

    GT_ITEM_CONTAINER_CLASS(klass)->fetch_items = fetch_items;
    GT_ITEM_CONTAINER_CLASS(klass)->create_child = create_child;
    GT_ITEM_CONTAINER_CLASS(klass)->get_properties = get_properties;
    GT_ITEM_CONTAINER_CLASS(klass)->activate_child = activate_child;

    props[PROP_QUERY] = g_param_spec_string("query", "Query", "Current query", NULL, G_PARAM_READWRITE);

    g_object_class_install_properties(G_OBJECT_CLASS(klass), NUM_PROPS, props);
}

static void
gt_followed_channel_container_init(GtFollowedChannelContainer* self)
{
    GtFollowedChannelContainerPrivate* priv = gt_followed_channel_container_get_instance_private(self);

    priv->item_flow = gt_item_container_get_flow_box(GT_ITEM_CONTAINER(self));
}

GtFollowedChannelContainer*
gt_followed_channel_container_new()
{
    return g_object_new(GT_TYPE_FOLLOWED_CHANNEL_CONTAINER,
        NULL);
}
