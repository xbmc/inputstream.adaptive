/*
 *  Copyright (C) 2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstddef>

namespace UTILS
{
namespace MEMORY
{
/*!
 * \brief Allocate a memory block with alignment suitable for all memory accesses
 * \param size Size in bytes for the memory block to be allocated
 * \return Pointer to the allocated block, or `nullptr` if the block cannot
 *         be allocated
 */
void* AlignedMalloc(size_t size);

/*!
 * \brief Free a memory block which has been allocated with a function of AlignedMalloc()
 * \param ptr Pointer to the memory block which should be freed.
 */
void AlignedFree(void* ptr);

}
} // namespace UTILS
