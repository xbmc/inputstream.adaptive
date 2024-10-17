# -*- coding: utf-8 -*-
"""
    Copyright (C) 2024 Team Kodi
    SPDX-License-Identifier: GPL-2.0-or-later
    See LICENSES/README.md for more information.
"""
import copy
import json
import urllib
from urllib.parse import parse_qsl

import xbmcaddon
import xbmcgui
import xbmcplugin

import menu_data as md
from helper import (log, LOG_DEBUG, LOG_ERROR, LOG_INFO, get_value_from_path, check_isa_addon, show_dialog_ok,
                    get_isa_version, determines_mime_type, ISA_ADDON_NAME, SETTING_DRM_METHOD_PROPS,
                    SETTING_DRM_METHOD_DRMLEGACY, SETTING_DRM_METHOD_DRM, KodiVersion, get_menu_config,
                    SETTING_SHOW_CRYPTO_FLAGS,
                    SETTING_SHOW_FEATURE_FLAGS, SETTING_SHOW_CODECS, ISAS_ADDON_NAME, show_dialog_text)

PLUGIN_URL = ''
PLUGIN_HANDLE = 0
ACTION_LISTING = 'listing'
ACTION_FILTER = 'filter'
ACTION_PLAY = 'play'
ACTION_SHOW_DIALOG = 'show_dialog'
EXTRADATA_ENC_FLAGS = 'extradata_encrypt_flags'
EXTRADATA_FEAT_FLAGS = 'extradata_feature_flags'


def make_li_url(action, path, wnd_title=None, extradata=''):
    """Make the URL for a ListItem"""
    if not wnd_title:
        wnd_title = path.replace('/', ' / ')
    if extradata:
        extradata = '&extradata=' + urllib.parse.quote(extradata)
    return '{0}?action={1}&path={2}&title={3}'.format(PLUGIN_URL, action, urllib.parse.quote(path),
                                                      urllib.parse.quote(wnd_title)) + extradata


def make_play_listitem(cfg):
    """Make a ListItem configured to play a stream with InputStream Adaptive"""
    addon = xbmcaddon.Addon()
    isa_version = get_isa_version()
    if not 'manifest_url' in cfg:
        raise Exception('Missing "manifest_url" in the stream configuration')
    manifest_url = cfg.pop('manifest_url')

    # On ListItem use "offscreen" to avoid locking GUI, since it will not be rendered on the screen
    play_item = xbmcgui.ListItem(path=manifest_url, offscreen=True)
    play_item.setProperty('isPlayable', 'true')
    play_item.setProperty('inputstream', ISA_ADDON_NAME)

    # Following two lines are to avoid HTTP HEAD requests from Kodi core (cause problems to some services)
    play_item.setMimeType(determines_mime_type(manifest_url))
    play_item.setContentLookup(False)

    # Try to find the preferred DRM property, if available
    prefer_drm_method = addon.getSettingString('isa.drm.preferred.method')
    selected_prop = None
    is_drm_legacy_found = 'drm_legacy' in cfg
    is_drm_found = 'drm' in cfg
    is_old_props_found = 'license_type' in cfg or 'license_key' in cfg

    if prefer_drm_method == SETTING_DRM_METHOD_DRMLEGACY and is_drm_legacy_found:
        selected_prop = SETTING_DRM_METHOD_DRMLEGACY
    elif prefer_drm_method == SETTING_DRM_METHOD_DRM and is_drm_found and isa_version >= 22:
        selected_prop = SETTING_DRM_METHOD_DRM
    elif prefer_drm_method == SETTING_DRM_METHOD_PROPS and is_old_props_found:
        selected_prop = SETTING_DRM_METHOD_PROPS

    if not selected_prop:  # preferred not found, take the first available
        if is_drm_found and isa_version >= 22:
            selected_prop = SETTING_DRM_METHOD_DRM
        elif is_drm_legacy_found:
            selected_prop = SETTING_DRM_METHOD_DRMLEGACY
        elif is_old_props_found:
            selected_prop = SETTING_DRM_METHOD_PROPS

    if selected_prop:
        # delete unused DRM configuration methods, using multiple DRM methods are not allowed on ISA add-on,
        # we allow to set multiple methods on streams to test all use cases without having to edit the streams each time
        if selected_prop != SETTING_DRM_METHOD_PROPS:
            cfg.pop('license_type', None)
            cfg.pop('license_key', None)
            cfg.pop('license_data', None)
            cfg.pop('license_flags', None)
            cfg.pop('server_certificate', None)
        if selected_prop != SETTING_DRM_METHOD_DRMLEGACY:
            cfg.pop('drm_legacy', None)
        if selected_prop != SETTING_DRM_METHOD_DRM:
            cfg.pop('drm', None)

    # Set ISA properties
    prop_prefix = f'{ISA_ADDON_NAME}.'
    for prop_name, value in cfg.items():
        play_item.setProperty(prop_prefix + prop_name, value)
    return play_item


def make_menu_listitem(path, item_name, title, data, menu_cfg):
    # Create a ListItem for the new entry
    list_item = xbmcgui.ListItem(title)
    if md.MI_CONFIG in data:  # Make a menu item
        is_folder = True
        action = ACTION_LISTING
    elif md.SI_CONFIG in data:  # Make an audio/video stream item
        is_folder = False
        action = ACTION_PLAY
        if menu_cfg[SETTING_SHOW_FEATURE_FLAGS] and md.SI_FEATURE in data:
            title += ' [COLOR blue][' + data[md.SI_FEATURE] + '][/COLOR]'
        if menu_cfg[SETTING_SHOW_CRYPTO_FLAGS] and md.SI_ENCRYPT in data:
            title += ' [COLOR red][' + data[md.SI_ENCRYPT] + '][/COLOR]'
        if menu_cfg[SETTING_SHOW_CODECS] and md.SI_CODECS in data:
            title += ' [' + data[md.SI_CODECS] + ']'
        list_item.setProperty('IsPlayable', 'true')
        list_item.setInfo('video', {'mediatype': 'movie', 'title': title})
        ctx_menus = [('InputStream Adaptive settings', f'Addon.OpenSettings({ISA_ADDON_NAME})'),
                     ('Add-on settings', f'Addon.OpenSettings({ISAS_ADDON_NAME})'), ]
        if md.SI_CONFIG in data:
            query = '{0}?action={1}&path={2}&title={3}&extradata={4}'.format(PLUGIN_URL, ACTION_SHOW_DIALOG,
                                                                             urllib.parse.quote(path + '/' + item_name),
                                                                             urllib.parse.quote(title), md.SI_CONFIG)
            ctx_menus.append(('Show ISA properties', f'RunPlugin({query})'))
        list_item.addContextMenuItems(ctx_menus)
        # Construct stream info
        info = ''
        if md.SI_FEATURE in data:
            text = '[COLOR blue]FEATURES:'
            for feat in data.get(md.SI_FEATURE, '').split(','):
                text += '[CR]' + md.STREAM_FEAT.get(feat, f'UNKNOWN FEATURE FLAG "{feat}"')
            info += text + '[/COLOR][CR]'
        if md.SI_ENCRYPT in data:
            text = '[COLOR red]ENCRYPTIONS:'
            for enc in data.get(md.SI_ENCRYPT, '').split(','):
                text += '[CR]' + md.STREAM_ENC.get(enc, f'UNKNOWN ENCRYPT FLAG "{enc}"')
            info += text + '[/COLOR][CR]'
        if md.SI_CODECS in data:
            info += 'CODECS: ' + str(data[md.SI_CODECS]) + '[CR]'
        if md.SI_INFO in data:
            info += '[COLOR green]INFO: ' + str(data[md.SI_INFO]) + '[/COLOR][CR]'
        if info:
            video_info = list_item.getVideoInfoTag()
            video_info.setPlot(info)
    else:
        log(LOG_ERROR, f'Cannot determine the menu item type for "{item_name}" on path "{path}"')
        return None
    url_path = '/'.join([path, item_name]).strip('/')
    return make_li_url(action, url_path), list_item, is_folder


def play_stream(path):
    log(LOG_INFO, f'Playing stream: {path}')
    try:
        if not check_isa_addon():
            show_dialog_ok('Cannot play the stream, InputStream Adaptive is not installed or not enabled.')
            raise Exception('InputStream Adaptive not available')
        # retrieve the stream config from the specified json path
        stream_data = get_value_from_path(path, md.menu_data)
        cfg = stream_data.get(md.SI_CONFIG)
        if not cfg:
            log(LOG_ERROR, f'Missing "{md.SI_CONFIG}" dict on json stream dict')
            xbmcplugin.endOfDirectory(handle=PLUGIN_HANDLE, succeeded=False)
            raise Exception(f'Missing "{md.SI_CONFIG}" dict on json stream dict')

        xbmcplugin.setResolvedUrl(PLUGIN_HANDLE, True, listitem=make_play_listitem(copy.deepcopy(cfg)))
    except Exception as exc:
        log(LOG_ERROR, f'Something was wrong, exception: {exc}')
        import traceback
        log(LOG_ERROR, traceback.format_exc())
        xbmcplugin.endOfDirectory(handle=PLUGIN_HANDLE, succeeded=False)


def show_text(path, wnd_title, extradata):
    log(LOG_DEBUG, f'Show text data from: {path}')
    try:
        stream_data = get_value_from_path(path, md.menu_data)
        if extradata == md.SI_CONFIG:
            show_dialog_text(json.dumps(stream_data.get(md.SI_CONFIG), indent=4), wnd_title)
        else:
            log(LOG_ERROR, f'Unhandled show_text extradata "{extradata}"')
    except Exception as exc:
        log(LOG_ERROR, f'Something was wrong, exception: {exc}')
        import traceback
        log(LOG_ERROR, traceback.format_exc())


def load_menu(path='', wnd_title=''):
    log(LOG_DEBUG, f'Loading menu "{path}"')
    if not path:
        menu_data = md.menu_data
    else:
        menu_data = get_value_from_path(path, md.menu_data)
    if not menu_data:
        log(LOG_ERROR, 'Unable to load the menu data')
        xbmcplugin.endOfDirectory(PLUGIN_HANDLE, False)
        return
    # Load menu items
    listing = []
    menu_cfg = get_menu_config()
    for e_title, e_data in menu_data.items():
        if e_title == md.MI_CONFIG:  # ignore it due to special handling
            continue
        list_item = make_menu_listitem(path, e_title, e_title, e_data, menu_cfg)
        if list_item is None:
            break
        listing.append(list_item)
    # Assume is main menu, add additional menus
    if not path:
        list_item = xbmcgui.ListItem(label='List DRM Widevine streams')
        url = make_li_url(ACTION_FILTER, 'DRMWV', list_item.getLabel(), EXTRADATA_ENC_FLAGS)
        listing.append((url, list_item, True))

        list_item = xbmcgui.ListItem(label='List DRM PlayReady streams')
        url = make_li_url(ACTION_FILTER, 'DRMPR', list_item.getLabel(), EXTRADATA_ENC_FLAGS)
        listing.append((url, list_item, True))

        list_item = xbmcgui.ListItem(label='List DRM ClearKey streams')
        url = make_li_url(ACTION_FILTER, 'DRMCK', list_item.getLabel(), EXTRADATA_ENC_FLAGS)
        listing.append((url, list_item, True))

        list_item = xbmcgui.ListItem(label='List CBCS streams')
        url = make_li_url(ACTION_FILTER, 'CBCS', list_item.getLabel(), EXTRADATA_ENC_FLAGS)
        listing.append((url, list_item, True))

        list_item = xbmcgui.ListItem(label='List streams with subtitles')
        url = make_li_url(ACTION_FILTER, 'SUB,SUBEXT,SUBMP4', list_item.getLabel(), EXTRADATA_FEAT_FLAGS)
        listing.append((url, list_item, True))

    xbmcplugin.addDirectoryItems(PLUGIN_HANDLE, listing, len(listing))
    xbmcplugin.setPluginCategory(PLUGIN_HANDLE, wnd_title)  # Set window title
    xbmcplugin.addSortMethod(PLUGIN_HANDLE, xbmcplugin.SORT_METHOD_LABEL_IGNORE_FOLDERS)
    xbmcplugin.endOfDirectory(PLUGIN_HANDLE, True, False, False)
    log(LOG_DEBUG, f'Menu "{path}" loaded.')


def filter_search(path, wnd_title, extradata):
    log(LOG_DEBUG, f'Loading filtered menu "{path}" with extradata "{extradata}"')
    if not path:
        log(LOG_ERROR, 'Unable to search an empty path')
        xbmcplugin.endOfDirectory(PLUGIN_HANDLE, False)
        return
    listing = []
    menu_cfg = get_menu_config()
    if extradata == EXTRADATA_ENC_FLAGS:
        for e_full_path, e_parent, e_title, e_data in md.find_encrypt_entries(md.menu_data, path):
            list_item = make_menu_listitem(e_full_path, e_title, f'[LIGHT][{e_parent}][/LIGHT] ' + e_title, e_data,
                                           menu_cfg)
            if list_item is None:
                break
            listing.append(list_item)
    if extradata == EXTRADATA_FEAT_FLAGS:
        feat_list = path.split(',')
        for e_full_path, e_parent, e_title, e_data in md.find_feature_entries(md.menu_data, feat_list):
            list_item = make_menu_listitem(e_full_path, e_title, f'[LIGHT][{e_parent}][/LIGHT] ' + e_title, e_data,
                                           menu_cfg)
            if list_item is None:
                break
            listing.append(list_item)
    xbmcplugin.addDirectoryItems(PLUGIN_HANDLE, listing, len(listing))
    xbmcplugin.setPluginCategory(PLUGIN_HANDLE, wnd_title)  # Set window title
    xbmcplugin.addSortMethod(PLUGIN_HANDLE, xbmcplugin.SORT_METHOD_LABEL_IGNORE_FOLDERS)
    xbmcplugin.endOfDirectory(PLUGIN_HANDLE, True, False, False)
    log(LOG_DEBUG, f'Filtered menu "{path}" loaded.')


def router(argv):
    # Update the global vars
    global PLUGIN_URL
    global PLUGIN_HANDLE
    PLUGIN_URL = argv[0]
    PLUGIN_HANDLE = int(argv[1])
    if KodiVersion() < 21:
        show_dialog_ok(f'This add-on is not compatible with Kodi version {KodiVersion().version}.[CR]'
                       'You must use Kodi v21 or above.')
        return
    # Parse the plugin parameters
    params_string = argv[2]
    params = dict(parse_qsl(params_string[1:]))
    if not check_isa_addon():
        show_dialog_ok('This add-on require InputStream Adaptive installed and enabled.')
        return
    # Route the callback to execute the requested action
    if params:
        action = params.get('action')
        path = urllib.parse.unquote(params.get('path', ''))
        title = urllib.parse.unquote(params.get('title', ''))
        extradata = urllib.parse.unquote(params.get('extradata', ''))
        if action == ACTION_LISTING: # List items for the requested menu
            load_menu(path, title)
        elif action == ACTION_FILTER: # List items for the requested filtered search
            filter_search(path, title, extradata)
        elif action == ACTION_PLAY: # Kodi player callback to play a stream
            play_stream(path)
        elif action == ACTION_SHOW_DIALOG: # Show a window text dialog
            show_text(path, title, extradata)
        elif action is None:
            log(LOG_ERROR, 'Missing "action" parameter to the ListItem path')
    else: # Add-on called from Kodi UI without any parameters
        load_menu(wnd_title='Main menu')
