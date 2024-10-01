/*
 *  Copyright (C) 2023 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace tinyxml2 // Forward
{
class XMLElement;
class XMLAttribute;
} // namespace tinyxml2
namespace pugi
{
class xml_node;
class xml_attribute;
}

namespace UTILS
{
namespace XML
{
/*!
 * \brief Parses an XML date string.
 * \param timeStr The date string
 * \param fallback [OPT] The fallback value when parse fails, by default set as max uint64_t value
 * \return The parsed date in seconds, or fallback value when fails.
 */
double ParseDate(std::string_view timeStr, double fallback = std::numeric_limits<double>::max());

/*!
 * \brief Parses an XML duration string.
 *        Negative values are not supported. Years and months are treated as exactly
 *        365 and 30 days respectively.
 *        See http://www.datypic.com/sc/xsd/t-xsd_duration.html
 * \param durationStr The duration string, e.g., "PT1H3M43.2S",
 *                    which means 1 hour, 3 minutes, and 43.2 seconds.
 * \return The parsed duration in seconds.
 */
double ParseDuration(std::string_view durationStr);

/*!
 * \brief Count the total childrens of a node tag element.
 * \param node The node for which children are counted.
 * \param childTagName [OPT] Search for a specific child tag, if empty count all children
 * \return The number of childrens.
 */
size_t CountChilds(pugi::xml_node node, std::string_view childTagName = "");

/*!
 * \brief Find the first attribute that have the specified name without take in account the prefix (prefix:name).
 * \param node The node where search the attribute.
 * \param attributeName The attribute name.
 * \return The attribute if found, otherwise an empty attribute.
 */
pugi::xml_attribute FirstAttributeNoPrefix(pugi::xml_node node, std::string_view attributeName);

/*!
 * \brief Get the specified attribute name.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param defaultValue The default fallback value.
 * \return If the attribute exist return the attribute value, otherwise the default fallback value.
 */
std::string_view GetAttrib(pugi::xml_node& node,
                           std::string_view name,
                           std::string_view defaultValue = "");

/*!
 * \brief Get the specified attribute name.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param defaultValue The default fallback value.
 * \return If the attribute exist return the attribute value, otherwise the default fallback value.
 */
int GetAttribInt(pugi::xml_node& node, std::string_view name, int defaultValue = 0);
/*!
 * \brief Get the specified attribute name.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param defaultValue The default fallback value.
 * \return If the attribute exist return the attribute value, otherwise the default fallback value.
 */
uint32_t GetAttribUint32(pugi::xml_node& node, std::string_view name, uint32_t defaultValue = 0);
/*!
 * \brief Get the specified attribute.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param defaultValue The default fallback value.
 * \return If the attribute exist return the attribute value, otherwise the default fallback value.
 */
uint64_t GetAttribUint64(pugi::xml_node& node, std::string_view name, uint64_t defaultValue = 0);

/*!
 * \brief Query to try get the specified attribute.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param value[OUT] Output the value of the attribute, if the attribute does not exist the variable will not be modified.
 * \return True if the attribute exists, otherwise false.
 */
bool QueryAttrib(pugi::xml_node& node, std::string_view name, std::string& value);
/*!
 * \brief Query to try get the specified attribute.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param value[OUT] Output the value of the attribute, if the attribute does not exist the variable will not be modified.
 * \return True if the attribute exists, otherwise false.
 */
bool QueryAttrib(pugi::xml_node& node, std::string_view name, int& value);
/*!
 * \brief Query to try get the specified attribute.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param value[OUT] Output the value of the attribute, if the attribute does not exist the variable will not be modified.
 * \return True if the attribute exists, otherwise false.
 */
bool QueryAttrib(pugi::xml_node& node, std::string_view name, uint32_t& value);
/*!
 * \brief Query to try get the specified attribute.
 * \param node The node where search the attribute.
 * \param name The attribute name.
 * \param value[OUT] Output the value of the attribute, if the attribute does not exist the variable will not be modified.
 * \return True if the attribute exists, otherwise false.
 */
bool QueryAttrib(pugi::xml_node& node, std::string_view name, uint64_t& value);

/*!
 * \brief Get value from an unknown XML nodes path,
 *        then traverse all even nested XML tags to search for the tag name.
 * \param node The XML node where find the tag
 * \param tagName The tag name to search for
 * \return The node if found, otherwise empty object.
 */
const pugi::xml_node GetNodeTraverseTags(pugi::xml_node node, const std::string& tagName);

} // namespace XML
}
