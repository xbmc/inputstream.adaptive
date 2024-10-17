# -*- coding: utf-8 -*-
"""
    Copyright (C) 2024 Team Kodi
    SPDX-License-Identifier: GPL-2.0-or-later
    See LICENSES/README.md for more information.
"""
import operator

import xbmc
import xbmcaddon

LOG_ERROR = "error"
LOG_DEBUG = "debug"
LOG_INFO = "info"

ISA_ADDON_NAME = "inputstream.adaptive"
ISAS_ADDON_NAME = "plugin.video.isasamples"

SETTING_SHOW_FEATURE_FLAGS = 'streams.show.featureflags'
SETTING_SHOW_CRYPTO_FLAGS = 'streams.show.cryptoflags'
SETTING_SHOW_CODECS = 'streams.show.codecs'
# Each drm method setting, must match the enum on the "isa.drm.preferred.method" xml setting
SETTING_DRM_METHOD_PROPS = 'props'
SETTING_DRM_METHOD_DRMLEGACY = 'drm-legacy'
SETTING_DRM_METHOD_DRM = 'drm'


def log(log_type, message):
    log_level = xbmc.LOGDEBUG
    if log_type == LOG_ERROR:
        log_level = xbmc.LOGERROR
    elif log_type == LOG_INFO:
        log_level = xbmc.LOGINFO
    elif log_type == LOG_DEBUG:
        log_level = xbmc.LOGDEBUG
    xbmc.log(f'[{ISAS_ADDON_NAME}] ' + message, log_level)


def get_value_from_path(path, json_data):
    if not path:
        return None
    keys = path.split('/')
    current_data = json_data
    try:
        for key in keys:
            current_data = current_data[key]
    except KeyError:
        log(LOG_ERROR, f'The json path: "{path}" dont exists')
        return None
    return current_data


class CmpVersion:
    """Comparator for version numbers"""

    def __init__(self, version):
        self.__version = str(version or '')
        self.__ver_list = (self.__version or '0').split('.')

    def __str__(self):
        return self.__version

    def __repr__(self):
        return self.__version

    def __bool__(self):
        """
        Allow "if" operator to check if there is a version set.
        Will return False only when "version" set is an empty string or None.
        """
        return bool(self.__version)

    def __iter__(self):
        """Allow to get the version list by using "list" command builtin"""
        return iter(self.__ver_list)

    def __lt__(self, other):
        """Operator <"""
        return operator.lt(*zip(*map(lambda x, y: (x or 0, y or 0),
                                     map(int, self.__ver_list),
                                     map(int, self.__conv_to_list(other)))))

    def __le__(self, other):
        """Operator <="""
        return operator.le(*zip(*map(lambda x, y: (x or 0, y or 0),
                                     map(int, self.__ver_list),
                                     map(int, self.__conv_to_list(other)))))

    def __gt__(self, other):
        """Operator >"""
        return operator.gt(*zip(*map(lambda x, y: (x or 0, y or 0),
                                     map(int, self.__ver_list),
                                     map(int, self.__conv_to_list(other)))))

    def __ge__(self, other):
        """Operator >="""
        return operator.ge(*zip(*map(lambda x, y: (x or 0, y or 0),
                                     map(int, self.__ver_list),
                                     map(int, self.__conv_to_list(other)))))

    def __eq__(self, other):
        """Operator =="""
        return operator.eq(*zip(*map(lambda x, y: (x or 0, y or 0),
                                     map(int, self.__ver_list),
                                     map(int, self.__conv_to_list(other)))))

    def __ne__(self, other):
        """Operator !="""
        return operator.ne(*zip(*map(lambda x, y: (x or 0, y or 0),
                                     map(int, self.__ver_list),
                                     map(int, self.__conv_to_list(other)))))

    def __conv_to_list(self, value):
        """Convert a string or number or CmpVersion object to a list of strings"""
        if isinstance(value, CmpVersion):
            return list(value)
        return str(value or '0').split('.')


class KodiVersion(CmpVersion):
    """Comparator for Kodi version numbers"""
    # Examples of some types of supported strings:
    # 10.1 Git:Unknown                       PRE-11.0 Git:Unknown                  11.0-BETA1 Git:20111222-22ad8e4
    # 18.1-RC1 Git:20190211-379f5f9903       19.0-ALPHA1 Git:20190419-c963b64487
    def __init__(self):
        import re
        self.build_version = xbmc.getInfoLabel('System.BuildVersion')
        # Parse the version number
        result = re.search(r'\d+\.\d+', self.build_version)
        self.version = result.group(0) if result else ''
        super().__init__(self.version)
        # Parse the date of GIT build
        result = re.search(r'(Git:)(\d+?(?=(-|$)))', self.build_version)
        self.date = int(result.group(2)) if result and len(result.groups()) >= 2 else None
        # Parse the stage name
        result = re.search(r'(\d+\.\d+-)(.+)(?=\s)', self.build_version)
        if not result:
            result = re.search(r'^(.+)(-\d+\.\d+)', self.build_version)
            self.stage = result.group(1) if result else ''
        else:
            self.stage = result.group(2) if result else ''


def get_isa_version():
    """Return the InputStream Adaptive version in a CmpVersion object"""
    try:
        addon = xbmcaddon.Addon(ISA_ADDON_NAME)
    except RuntimeError:
        log(LOG_ERROR, f'Cannot get {ISA_ADDON_NAME} version')
        return CmpVersion(0)
    return CmpVersion(addon.getAddonInfo('version'))


def jsonrpc(*args, **kwargs):
    """Perform JSONRPC calls"""
    from json import dumps, loads
    if args and kwargs:
        log(LOG_ERROR, 'Bad jsonrpc() method arguments')
        return None
    # Process a list of actions
    if args:
        for (idx, cmd) in enumerate(args):
            if cmd.get('id') is None:
                cmd.update(id=idx)
            if cmd.get('jsonrpc') is None:
                cmd.update(jsonrpc='2.0')
        return loads(xbmc.executeJSONRPC(dumps(args)))
    # Process a single action
    if kwargs.get('id') is None:
        kwargs.update(id=0)
    if kwargs.get('jsonrpc') is None:
        kwargs.update(jsonrpc='2.0')
    return loads(xbmc.executeJSONRPC(dumps(kwargs)))


def check_isa_addon():
    """Returns whether InputStream Adaptive add-on is installed and enabled"""
    data = jsonrpc(method='Addons.GetAddonDetails',
                   params={'addonid': ISA_ADDON_NAME, 'properties': ['enabled']})
    if data.get('result', {}).get('addon', {}).get('enabled'):
        return True
    return False


def show_dialog_ok(message='', heading=''):
    """Show Kodi's OK dialog"""
    from xbmcgui import Dialog
    if not heading:
        heading = xbmcaddon.Addon(ISAS_ADDON_NAME).getAddonInfo('name')
    return Dialog().ok(heading=heading, message=message)


def show_dialog_text(message='', heading=''):
    """Show Kodi's Text viewer dialog"""
    from xbmcgui import Dialog
    if not heading:
        heading = xbmcaddon.Addon(ISAS_ADDON_NAME).getAddonInfo('name')
    return Dialog().textviewer(heading=heading, text=message)


def determines_mime_type(url):
    """
    Determines mime type from a manifest URL
    Returns mime type value
    """
    url_lw = url.lower()
    if url_lw.endswith('.m3u8'):
        return 'application/vnd.apple.mpegurl'
    if url_lw.endswith('.mpd'):
        return 'application/dash+xml'
    if (url_lw.endswith('.ism/manifest') or url_lw.endswith('.isml/manifest')
            or url_lw.endswith('.isml') or url_lw.endswith('.ism')):
        return 'application/vnd.ms-sstr+xml'
    # Unknown, don't matter if wrong it's irrelevant to ISA
    return 'application/dash+xml'


def get_menu_config():
    """Fetch menu settings, for a fast menu loading"""
    cfg = {}
    addon = xbmcaddon.Addon()
    cfg[SETTING_SHOW_FEATURE_FLAGS] = addon.getSettingBool(SETTING_SHOW_FEATURE_FLAGS)
    cfg[SETTING_SHOW_CRYPTO_FLAGS] = addon.getSettingBool(SETTING_SHOW_CRYPTO_FLAGS)
    cfg[SETTING_SHOW_CODECS] = addon.getSettingBool(SETTING_SHOW_CODECS)
    return cfg
