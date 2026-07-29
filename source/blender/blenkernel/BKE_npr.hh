/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup bke
 */

namespace blender {

struct Main;
struct bNodeTree;
struct Material;
struct bNode;

bNodeTree *BKE_npr_tree_add(Main *bmain, const char *name);

/* NPR Bridge: generate the AOV slot name for a bridge socket.
 * Format: __nprbr__<base>__<color|float|vector>
 * socket_type: 0=color, 1=float, 2=vector
 * base = storage->name when non-empty, otherwise <material_name>_<node_name>. */
void BKE_npr_bridge_socket_name(const Material *mat,
                                const bNode *node,
                                int socket_type,
                                char *out,
                                size_t out_len);

}  // namespace blender
