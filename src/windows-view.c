/*
 * windows-view: A view showing visible windows
 * 
 * Copyright 2012-2013 Stephan Haller <nomad@froevel.de>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include "windows-view.h"
#include "live-window.h"
#include "scaled-table-layout.h"
#include "utils.h"
#include "stage.h"
#include "application.h"
#include "view.h"
#include "drop-action.h"
#include "quicklaunch.h"
#include "application-button.h"
#include "window-tracker.h"

/* Define this class in GObject system */
G_DEFINE_TYPE(XfdashboardWindowsView,
				xfdashboard_windows_view,
				XFDASHBOARD_TYPE_VIEW)

/* Private structure - access only by public API if needed */
#define XFDASHBOARD_WINDOWS_VIEW_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE((obj), XFDASHBOARD_TYPE_WINDOWS_VIEW, XfdashboardWindowsViewPrivate))

struct _XfdashboardWindowsViewPrivate
{
	/* Properties related */
	XfdashboardWindowTrackerWorkspace				*workspace;

	/* Instance related */
	XfdashboardWindowTracker	*windowTracker;
};

/* Properties */
enum
{
	PROP_0,

	PROP_WORKSPACE,

	PROP_LAST
};

static GParamSpec* XfdashboardWindowsViewProperties[PROP_LAST]={ 0, };

/* Forward declaration */
static XfdashboardLiveWindow* _xfdashboard_windows_view_create_actor(XfdashboardWindowsView *self, XfdashboardWindowTrackerWindow *inWindow);
static void _xfdashboard_windows_view_set_active_workspace(XfdashboardWindowsView *self, XfdashboardWindowTrackerWorkspace *inWorkspace);

/* IMPLEMENTATION: Private variables and methods */
#define DEFAULT_SPACING		8.0f					// TODO: Replace by settings/theming object
#define DEFAULT_VIEW_ICON	GTK_STOCK_FULLSCREEN	// TODO: Replace by settings/theming object


/* Find live window actor by wnck-window */
static XfdashboardLiveWindow* _xfdashboard_windows_view_find_by_window(XfdashboardWindowsView *self,
																		XfdashboardWindowTrackerWindow *inWindow)
{
	XfdashboardLiveWindow	*liveWindow;
	ClutterActor			*child;
	ClutterActorIter		iter;

	g_return_val_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self), NULL);
	g_return_val_if_fail(XFDASHBOARD_IS_WINDOW_TRACKER_WINDOW(inWindow), NULL);

	/* Iterate through list of current actors and find the one for requested window */
	clutter_actor_iter_init(&iter, CLUTTER_ACTOR(self));
	while(clutter_actor_iter_next(&iter, &child))
	{
		if(!XFDASHBOARD_IS_LIVE_WINDOW(child)) continue;

		liveWindow=XFDASHBOARD_LIVE_WINDOW(child);
		if(xfdashboard_live_window_get_window(liveWindow)==inWindow) return(liveWindow);
	}

	/* If we get here we did not find the window and we return NULL */
	return(NULL);
}

/* Drag of an actor to this view as drop target begins */
static gboolean _xfdashboard_windows_view_on_drop_begin(XfdashboardWindowsView *self,
														XfdashboardDragAction *inDragAction,
														gpointer inUserData)
{
	ClutterActor					*dragSource;
	ClutterActor					*draggedActor;
	gboolean						canHandle;

	g_return_val_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction), FALSE);
	g_return_val_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData), FALSE);

	canHandle=FALSE;

	/* Get source where dragging started and actor being dragged */
	dragSource=xfdashboard_drag_action_get_source(inDragAction);
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);

	/* Check if we can handle dragged actor from given source */
	if(XFDASHBOARD_IS_QUICKLAUNCH(dragSource) &&
		XFDASHBOARD_IS_APPLICATION_BUTTON(draggedActor))
	{
		canHandle=TRUE;
	}

	/* Return TRUE if we can handle dragged actor in this drop target
	 * otherwise FALSE
	 */
	return(canHandle);
}

/* Dragged actor was dropped on this drop target */
static void _xfdashboard_windows_view_on_drop_drop(XfdashboardWindowsView *self,
													XfdashboardDragAction *inDragAction,
													gfloat inX,
													gfloat inY,
													gpointer inUserData)
{
	ClutterActor						*draggedActor;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_DRAG_ACTION(inDragAction));
	g_return_if_fail(XFDASHBOARD_IS_DROP_ACTION(inUserData));

	/* Get dragged actor */
	draggedActor=xfdashboard_drag_action_get_actor(inDragAction);
	g_return_if_fail(XFDASHBOARD_IS_APPLICATION_BUTTON(draggedActor));

	/* Launch application being dragged here */
	xfdashboard_application_button_execute(XFDASHBOARD_APPLICATION_BUTTON(draggedActor));
}

/* Active workspace was changed */
static void _xfdashboard_windows_view_on_active_workspace_changed(XfdashboardWindowsView *self,
																	XfdashboardWindowTrackerWorkspace *inPrevWorkspace,
																	XfdashboardWindowTrackerWorkspace *inNewWorkspace,
																	gpointer inUserData)
{
	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));

	/* Update window list */
	_xfdashboard_windows_view_set_active_workspace(self, inNewWorkspace);
}

/* A window was opened */
static void _xfdashboard_windows_view_on_window_opened(XfdashboardWindowsView *self,
														XfdashboardWindowTrackerWindow *inWindow,
														gpointer inUserData)
{
	XfdashboardWindowsViewPrivate		*priv;
	XfdashboardWindowTrackerWorkspace	*workspace;
	XfdashboardLiveWindow				*liveWindow;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_WINDOW_TRACKER_WINDOW(inWindow));

	priv=self->priv;

	/* Check if event happened on active screen and active workspace */
	workspace=xfdashboard_window_tracker_window_get_workspace(inWindow);
	if(workspace==NULL || workspace!=priv->workspace) return;

	/* Create actor */
	liveWindow=_xfdashboard_windows_view_create_actor(self, inWindow);
	if(liveWindow) clutter_actor_insert_child_below(CLUTTER_ACTOR(self), CLUTTER_ACTOR(liveWindow), NULL);
}

/* A window was closed */
static void _xfdashboard_windows_view_on_window_closed(XfdashboardWindowsView *self,
														XfdashboardWindowTrackerWindow *inWindow,
														gpointer inUserData)
{
	XfdashboardLiveWindow				*liveWindow;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_WINDOW_TRACKER_WINDOW(inWindow));

	/* Find live window for window just being closed and destroy it */
	liveWindow=_xfdashboard_windows_view_find_by_window(self, inWindow);
	if(G_LIKELY(liveWindow)) clutter_actor_destroy(CLUTTER_ACTOR(liveWindow));
}

/* A live window was clicked */
static void _xfdashboard_windows_view_on_window_clicked(XfdashboardWindowsView *self,
														gpointer inUserData)
{
	XfdashboardLiveWindow				*liveWindow;
	XfdashboardWindowTrackerWindow		*window;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_LIVE_WINDOW(inUserData));

	liveWindow=XFDASHBOARD_LIVE_WINDOW(inUserData);

	/* Activate clicked window */
	window=XFDASHBOARD_WINDOW_TRACKER_WINDOW(xfdashboard_live_window_get_window(liveWindow));
	xfdashboard_window_tracker_window_activate(window);

	/* Quit application */
	xfdashboard_application_quit();
}

/* The close button of a live window was clicked */
static void _xfdashboard_windows_view_on_window_close_clicked(XfdashboardWindowsView *self,
																gpointer inUserData)
{
	XfdashboardLiveWindow				*liveWindow;
	XfdashboardWindowTrackerWindow		*window;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_LIVE_WINDOW(inUserData));

	liveWindow=XFDASHBOARD_LIVE_WINDOW(inUserData);

	/* Close clicked window */
	window=XFDASHBOARD_WINDOW_TRACKER_WINDOW(xfdashboard_live_window_get_window(liveWindow));
	xfdashboard_window_tracker_window_close(window);
}

/* A window was moved or resized */
static void _xfdashboard_windows_view_on_window_geometry_changed(XfdashboardWindowsView *self,
																	gpointer inUserData)
{
	XfdashboardLiveWindow				*liveWindow;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_LIVE_WINDOW(inUserData));

	liveWindow=XFDASHBOARD_LIVE_WINDOW(inUserData);

	/* Force a relayout to reflect new size of window */
	clutter_actor_queue_relayout(CLUTTER_ACTOR(liveWindow));
}

/* A window was hidden or shown */
static void _xfdashboard_windows_view_on_window_visibility_changed(XfdashboardWindowsView *self,
																	gboolean inIsVisible,
																	gpointer inUserData)
{
	XfdashboardLiveWindow				*liveWindow;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_LIVE_WINDOW(inUserData));

	liveWindow=XFDASHBOARD_LIVE_WINDOW(inUserData);

	/* If window is shown, show it in window list - otherwise hide it.
	 * We should not destroy the live window actor as the window might
	 * get visible again.
	 */
	if(inIsVisible) clutter_actor_show(CLUTTER_ACTOR(liveWindow));
		else clutter_actor_hide(CLUTTER_ACTOR(liveWindow));
}

/* A window changed workspace or was pinned to all workspaces */
static void _xfdashboard_windows_view_on_window_workspace_changed(XfdashboardWindowsView *self,
																	gpointer inUserData)
{
	XfdashboardWindowsViewPrivate		*priv;
	XfdashboardLiveWindow				*liveWindow;
	XfdashboardWindowTrackerWindow		*window;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(XFDASHBOARD_IS_LIVE_WINDOW(inUserData));

	priv=self->priv;
	liveWindow=XFDASHBOARD_LIVE_WINDOW(inUserData);

	/* If window is neither on this workspace nor pinned then destroy it */
	window=xfdashboard_live_window_get_window(liveWindow);
	if(!xfdashboard_window_tracker_window_is_pinned(window) &&
		xfdashboard_window_tracker_window_get_workspace(window)!=priv->workspace)
	{
		clutter_actor_destroy(CLUTTER_ACTOR(liveWindow));
	}
}

/* Create actor for wnck-window and connect signals */
static XfdashboardLiveWindow* _xfdashboard_windows_view_create_actor(XfdashboardWindowsView *self,
																		XfdashboardWindowTrackerWindow *inWindow)
{
	ClutterActor	*actor;

	g_return_val_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self), NULL);
	g_return_val_if_fail(XFDASHBOARD_IS_WINDOW_TRACKER_WINDOW(inWindow), NULL);

	/* Check if window opened is a stage window */
	if(xfdashboard_window_tracker_window_find_stage(inWindow))
	{
		g_debug("Will not create live-window actor for stage window.");
		return(NULL);
	}

	/* Create actor and connect signals */
	actor=xfdashboard_live_window_new();
	g_signal_connect_swapped(actor, "clicked", G_CALLBACK(_xfdashboard_windows_view_on_window_clicked), self);
	g_signal_connect_swapped(actor, "close", G_CALLBACK(_xfdashboard_windows_view_on_window_close_clicked), self);
	g_signal_connect_swapped(actor, "geometry-changed", G_CALLBACK(_xfdashboard_windows_view_on_window_geometry_changed), self);
	g_signal_connect_swapped(actor, "visibility-changed", G_CALLBACK(_xfdashboard_windows_view_on_window_visibility_changed), self);
	g_signal_connect_swapped(actor, "workspace-changed", G_CALLBACK(_xfdashboard_windows_view_on_window_workspace_changed), self);
	xfdashboard_live_window_set_window(XFDASHBOARD_LIVE_WINDOW(actor), inWindow);

	return(XFDASHBOARD_LIVE_WINDOW(actor));
}

/* Set active screen */
static void _xfdashboard_windows_view_set_active_workspace(XfdashboardWindowsView *self,
															XfdashboardWindowTrackerWorkspace *inWorkspace)
{
	XfdashboardWindowsViewPrivate			*priv;
	GList									*windowsList;

	g_return_if_fail(XFDASHBOARD_IS_WINDOWS_VIEW(self));
	g_return_if_fail(inWorkspace==NULL || XFDASHBOARD_IS_WINDOW_TRACKER_WORKSPACE(inWorkspace));

	priv=XFDASHBOARD_WINDOWS_VIEW(self)->priv;

	/* Do not anything if workspace is the same as before */
	if(inWorkspace==priv->workspace) return;

	/* Set new workspace */
	priv->workspace=inWorkspace;

	/* Destroy all actors */
	clutter_actor_destroy_all_children(CLUTTER_ACTOR(self));

	/* Create live window actors for new workspace */
	if(priv->workspace!=NULL)
	{
		/* Get list of all windows open */
		windowsList=xfdashboard_window_tracker_get_windows(priv->windowTracker);

		/* Iterate through list of window (from last to first), check if window
		 * is visible and create actor for it if it is.
		 */
		windowsList=g_list_last(windowsList);
		while(windowsList)
		{
			XfdashboardWindowTrackerWindow	*window=XFDASHBOARD_WINDOW_TRACKER_WINDOW(windowsList->data);
			XfdashboardLiveWindow			*liveWindow;

			/* Window must be on workspace and must not be flagged to skip tasklist */
			if(xfdashboard_window_tracker_window_is_visible_on_workspace(window, priv->workspace) &&
				xfdashboard_window_tracker_window_is_skip_pager(window)==FALSE &&
				xfdashboard_window_tracker_window_is_skip_tasklist(window)==FALSE)
			{
				/* Create actor */
				liveWindow=_xfdashboard_windows_view_create_actor(XFDASHBOARD_WINDOWS_VIEW(self), window);
				if(liveWindow) clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(liveWindow));
			}

			/* Next window */
			windowsList=g_list_previous(windowsList);
		}
	}

	/* Notify about property change */
	g_object_notify_by_pspec(G_OBJECT(self), XfdashboardWindowsViewProperties[PROP_WORKSPACE]);
}

/* IMPLEMENTATION: GObject */

/* Dispose this object */
static void _xfdashboard_windows_view_dispose(GObject *inObject)
{
	XfdashboardWindowsView			*self=XFDASHBOARD_WINDOWS_VIEW(inObject);
	XfdashboardWindowsViewPrivate	*priv=XFDASHBOARD_WINDOWS_VIEW(self)->priv;

	/* Release allocated resources */
	_xfdashboard_windows_view_set_active_workspace(self, NULL);

	if(priv->windowTracker)
	{
		g_signal_handlers_disconnect_by_data(priv->windowTracker, self);
		g_object_unref(priv->windowTracker);
		priv->windowTracker=NULL;
	}

	/* Call parent's class dispose method */
	G_OBJECT_CLASS(xfdashboard_windows_view_parent_class)->dispose(inObject);
}

/* Set/get properties */
static void _xfdashboard_windows_view_set_property(GObject *inObject,
													guint inPropID,
													const GValue *inValue,
													GParamSpec *inSpec)
{
	XfdashboardWindowsView		*self=XFDASHBOARD_WINDOWS_VIEW(inObject);
	
	switch(inPropID)
	{
		case PROP_WORKSPACE:
			_xfdashboard_windows_view_set_active_workspace(self, g_value_get_object(inValue));
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(inObject, inPropID, inSpec);
			break;
	}
}

static void _xfdashboard_windows_view_get_property(GObject *inObject,
													guint inPropID,
													GValue *outValue,
													GParamSpec *inSpec)
{
	XfdashboardWindowsView		*self=XFDASHBOARD_WINDOWS_VIEW(inObject);

	switch(inPropID)
	{
		case PROP_WORKSPACE:
			g_value_set_object(outValue, self->priv->workspace);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(inObject, inPropID, inSpec);
			break;
	}
}

/* Class initialization
 * Override functions in parent classes and define properties
 * and signals
 */
static void xfdashboard_windows_view_class_init(XfdashboardWindowsViewClass *klass)
{
	GObjectClass			*gobjectClass=G_OBJECT_CLASS(klass);

	/* Override functions */
	gobjectClass->dispose=_xfdashboard_windows_view_dispose;
	gobjectClass->set_property=_xfdashboard_windows_view_set_property;
	gobjectClass->get_property=_xfdashboard_windows_view_get_property;

	/* Set up private structure */
	g_type_class_add_private(klass, sizeof(XfdashboardWindowsViewPrivate));

	/* Define properties */
	XfdashboardWindowsViewProperties[PROP_WORKSPACE]=
		g_param_spec_object ("workspace",
								_("Current workspace"),
								_("The current workspace whose windows are shown"),
								XFDASHBOARD_TYPE_WINDOW_TRACKER_WORKSPACE,
								G_PARAM_READABLE);

	g_object_class_install_properties(gobjectClass, PROP_LAST, XfdashboardWindowsViewProperties);
}

/* Object initialization
 * Create private structure and set up default values
 */
static void xfdashboard_windows_view_init(XfdashboardWindowsView *self)
{
	XfdashboardWindowsViewPrivate	*priv;
	ClutterLayoutManager			*layout;
	ClutterAction					*action;

	self->priv=priv=XFDASHBOARD_WINDOWS_VIEW_GET_PRIVATE(self);

	/* Set up default values */
	priv->windowTracker=xfdashboard_window_tracker_get_default();
	priv->workspace=NULL;

	/* Set up view */
	xfdashboard_view_set_internal_name(XFDASHBOARD_VIEW(self), "windows");
	xfdashboard_view_set_name(XFDASHBOARD_VIEW(self), _("Windows"));
	xfdashboard_view_set_icon(XFDASHBOARD_VIEW(self), DEFAULT_VIEW_ICON);
	xfdashboard_view_set_fit_mode(XFDASHBOARD_VIEW(self), XFDASHBOARD_FIT_MODE_BOTH);

	/* Setup actor */
	layout=xfdashboard_scaled_table_layout_new();
	xfdashboard_scaled_table_layout_set_spacing(XFDASHBOARD_SCALED_TABLE_LAYOUT(layout), DEFAULT_SPACING);
	xfdashboard_scaled_table_layout_set_relative_scale(XFDASHBOARD_SCALED_TABLE_LAYOUT(layout), TRUE);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), layout);

	action=xfdashboard_drop_action_new();
	clutter_actor_add_action(CLUTTER_ACTOR(self), action);
	g_signal_connect_swapped(action, "begin", G_CALLBACK(_xfdashboard_windows_view_on_drop_begin), self);
	g_signal_connect_swapped(action, "drop", G_CALLBACK(_xfdashboard_windows_view_on_drop_drop), self);

	/* Connect signals */
	g_signal_connect_swapped(priv->windowTracker,
								"active-workspace-changed",
								G_CALLBACK(_xfdashboard_windows_view_on_active_workspace_changed),
								self);

	g_signal_connect_swapped(priv->windowTracker,
								"window-opened",
								G_CALLBACK(_xfdashboard_windows_view_on_window_opened),
								self);

	g_signal_connect_swapped(priv->windowTracker,
								"window-closed",
								G_CALLBACK(_xfdashboard_windows_view_on_window_closed),
								self);
}
