/*
*      Copyright (C) 2017 peak3d
*      http://www.peak3d.de
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  <http://www.gnu.org/licenses/>.
*
*/

typedef enum LogLevel
{
  LOGLEVEL_DEBUG = 0,
  LOGLEVEL_INFO = 1,
  LOGLEVEL_NOTICE = 2,
  LOGLEVEL_WARNING = 3,
  LOGLEVEL_ERROR = 4,
  LOGLEVEL_SEVERE = 5,
  LOGLEVEL_FATAL = 6
} LogLevel;


extern void Log(const LogLevel loglevel, const char* format, ...);