/*
 * selection_transfer.c - objects for selection transfer header
 *
 * Copyright © 2019 Uli Schlachter <psychon@znc.in>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "objects/selection_transfer.h"
#include "common/luaobject.h"
#include "common/atoms.h"
#include "globalconf.h"

#define REGISTRY_TRANSFER_TABLE_INDEX "awesome_selection_transfers"

enum transfer_state {
    TRANSFER_WAIT_FOR_DATA,
    TRANSFER_DONE
};

typedef struct selection_transfer_t
{
    LUA_OBJECT_HEADER
    /** Reference in the special table to this object */
    int ref;
    /* Information from the xcb_selection_request_event_t */
    xcb_window_t requestor;
    xcb_atom_t selection;
    xcb_atom_t target;
    xcb_atom_t property;
    xcb_timestamp_t time;
    /* Current state of the transfer */
    enum transfer_state state;
} selection_transfer_t;

static lua_class_t selection_transfer_class;
LUA_OBJECT_FUNCS(selection_transfer_class, selection_transfer_t, selection_transfer)

static void
selection_transfer_notify(xcb_window_t requestor, xcb_atom_t selection,
        xcb_atom_t target, xcb_atom_t property, xcb_timestamp_t time)
{
    xcb_selection_notify_event_t ev;

    p_clear(&ev, 1);
    ev.response_type = XCB_SELECTION_NOTIFY;
    ev.requestor = requestor;
    ev.selection = selection;
    ev.target = target;
    ev.property = property;
    ev.time = time;

    xcb_send_event(globalconf.connection, false, requestor,
            XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
}

void
selection_transfer_reject(xcb_window_t requestor, xcb_atom_t selection,
        xcb_atom_t target, xcb_timestamp_t time)
{
    selection_transfer_notify(requestor, selection, target, XCB_NONE, time);
}

static void
transfer_done(lua_State *L, selection_transfer_t *transfer)
{
    transfer->state = TRANSFER_DONE;

    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    luaL_unref(L, -1, transfer->ref);
    transfer->ref = LUA_NOREF;
    lua_pop(L, 1);
}

void
selection_transfer_begin(lua_State *L, int ud, xcb_window_t requestor,
        xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property,
        xcb_timestamp_t time)
{
    ud = luaA_absindex(L, ud);

    /* Allocate a transfer object */
    selection_transfer_t *transfer = (void *) selection_transfer_class.allocator(L);
    transfer->requestor = requestor;
    transfer->selection = selection;
    transfer->target = target;
    transfer->property = property;
    transfer->time = time;
    transfer->state = TRANSFER_WAIT_FOR_DATA;

    /* Save the object in the registry */
    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, -2);
    transfer->ref = luaL_ref(L, -2);
    lua_pop(L, 1);

    /* Get the atom name */
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(globalconf.connection,
            xcb_get_atom_name_unchecked(globalconf.connection, target), NULL);
    if (reply) {
        lua_pushlstring(L, xcb_get_atom_name_name(reply),
                xcb_get_atom_name_name_length(reply));
        p_delete(&reply);
    } else
        lua_pushnil(L);

    /* Emit the request signal with target and transfer object */
    lua_pushvalue(L, -2);
    luaA_object_emit_signal(L, ud, "request", 2);

    /* Reject the transfer if Lua did not do anything */
    if (transfer->state == TRANSFER_WAIT_FOR_DATA) {
        selection_transfer_reject(requestor, selection, target, time);
        transfer_done(L, transfer);
    }

    /* Remove the transfer object from the stack */
    lua_pop(L, 1);
}

static bool
selection_transfer_checker(selection_transfer_t *transfer)
{
    return transfer->state != TRANSFER_DONE;
}

void
selection_transfer_class_setup(lua_State *L)
{
    static const struct luaL_Reg selection_transfer_methods[] =
    {
        { NULL, NULL }
    };

    static const struct luaL_Reg selection_transfer_meta[] =
    {
        LUA_OBJECT_META(selection_transfer)
        LUA_CLASS_META
        { NULL, NULL }
    };

    /* Store a table in the registry that tracks active selection_transfer_t. */
    lua_pushliteral(L, REGISTRY_TRANSFER_TABLE_INDEX);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    luaA_class_setup(L, &selection_transfer_class, "selection_transfer", NULL,
            (lua_class_allocator_t) selection_transfer_new, NULL,
            (lua_class_checker_t) selection_transfer_checker,
            luaA_class_index_miss_property, luaA_class_newindex_miss_property,
            selection_transfer_methods, selection_transfer_meta);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:textwidth=80
