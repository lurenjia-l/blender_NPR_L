/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "GPU_material.hh"

#include "BLI_hash.h"
#include "BLI_string.h"

#include "DNA_material_types.h"
#include "DNA_node_types.h"

#include "BLI_listbase.h"

#include "BKE_context.hh"
#include "BKE_main.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_runtime.hh"
#include "BKE_npr.hh"

#include "NOD_shader.h"

namespace blender {

namespace nodes::node_shader_npr_bridge_input_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Image>("Color"_ustr);
  b.add_output<decl::Image>("Float"_ustr);
  b.add_output<decl::Image>("Vector"_ustr);
  b.add_output<decl::Image>("Shader"_ustr);
}

static void node_shader_buts_npr_bridge_input(ui::Layout &layout,
                                              bContext *C,
                                              PointerRNA *ptr)
{
  layout.prop(ptr, "bridge_name", ui::ITEM_R_SPLIT_EMPTY_NAME, std::nullopt, ICON_NONE);

  /* Warn when the bridged BSDF is not on the same chain as Material Output Surface. */
  bNode *node = (bNode *)ptr->data;
  Main *bmain = CTX_data_main(C);
  bNodeTree *npr_tree = (node->runtime != nullptr) ? node->runtime->owner_tree : nullptr;
  bNodeTree *mat_tree = nullptr;
  Material *mat_owner = nullptr;
  if (bmain != nullptr && npr_tree != nullptr) {
    for (Material &mat : bmain->materials) {
      if (npr_tree_get_from_mat(&mat) == npr_tree) {
        mat_tree = mat.nodetree;
        mat_owner = &mat;
        break;
      }
    }
  }
  if (mat_tree != nullptr && mat_owner != nullptr) {
    char base[88];
    NodeShaderNPRBridge *storage = static_cast<NodeShaderNPRBridge *>(node->storage);
    if (storage != nullptr && storage->name[0] != '\0') {
      BLI_strncpy(base, storage->name, sizeof(base));
    }
    else {
      SNPRINTF(base, "%s_%s", mat_owner->id.name + 2, node->name);
    }
    for (bNode &onode_ref : mat_tree->nodes) {
      bNode *onode = &onode_ref;
      if (onode->type_legacy != SH_NODE_NPR_BRIDGE_OUTPUT) {
        continue;
      }
      char obase[88];
      NodeShaderNPRBridge *ostorage = static_cast<NodeShaderNPRBridge *>(onode->storage);
      if (ostorage != nullptr && ostorage->name[0] != '\0') {
        BLI_strncpy(obase, ostorage->name, sizeof(obase));
      }
      else {
        SNPRINTF(obase, "%s_%s", mat_owner->id.name + 2, onode->name);
      }
      if (STREQ(base, obase)) {
        if (!npr_bridge_shader_same_chain(mat_tree, onode)) {
          layout.label("BSDF not same chain: Shader output is black", ICON_ERROR);
        }
        break;
      }
    }
  }
}

static void node_shader_init_npr_bridge_input(bNodeTree * /*ntree*/, bNode *node)
{
  NodeShaderNPRBridge *data = MEM_new<NodeShaderNPRBridge>("NodeShaderNPRBridge");
  node->storage = data;
}

static int node_shader_gpu_npr_bridge_input(GPUMaterial *mat,
                                            bNode *node,
                                            bNodeExecData * /*execdata*/,
                                            GPUNodeStack *in,
                                            GPUNodeStack *out)
{
  Material *material = GPU_material_get_material(mat);

  char name_c[128], name_f[128], name_v[128];
  BKE_npr_bridge_socket_name(material, node, 0, name_c, sizeof(name_c));
  BKE_npr_bridge_socket_name(material, node, 1, name_f, sizeof(name_f));
  BKE_npr_bridge_socket_name(material, node, 2, name_v, sizeof(name_v));

  uint hash_c = BLI_hash_string(name_c);
  uint hash_f = BLI_hash_string(name_f);
  uint hash_v = BLI_hash_string(name_v);

  GPUNodeLink *hc = GPU_constant(reinterpret_cast<float *>(&hash_c));
  GPUNodeLink *hf = GPU_constant(reinterpret_cast<float *>(&hash_f));
  GPUNodeLink *hv = GPU_constant(reinterpret_cast<float *>(&hash_v));

  /* Same-chain detection: find the matching Bridge Output in the material tree (by base name) and
   * check if its Shader input shares a closure source with Material Output Surface. */
  bool same_chain = false;
  bNodeTree *mat_tree = (material != nullptr) ? material->nodetree : nullptr;
  if (mat_tree != nullptr) {
    char base[88];
    NodeShaderNPRBridge *storage = static_cast<NodeShaderNPRBridge *>(node->storage);
    if (storage != nullptr && storage->name[0] != '\0') {
      BLI_strncpy(base, storage->name, sizeof(base));
    }
    else {
      SNPRINTF(base, "%s_%s", material->id.name + 2, node->name);
    }
    for (bNode &onode_ref : mat_tree->nodes) {
      bNode *onode = &onode_ref;
      if (onode->type_legacy != SH_NODE_NPR_BRIDGE_OUTPUT) {
        continue;
      }
      char obase[88];
      NodeShaderNPRBridge *ostorage = static_cast<NodeShaderNPRBridge *>(onode->storage);
      if (ostorage != nullptr && ostorage->name[0] != '\0') {
        BLI_strncpy(obase, ostorage->name, sizeof(obase));
      }
      else {
        SNPRINTF(obase, "%s_%s", material->id.name + 2, onode->name);
      }
      if (STREQ(base, obase)) {
        same_chain = npr_bridge_shader_same_chain(mat_tree, onode);
        break;
      }
    }
  }

  float same_chain_f = same_chain ? 1.0f : 0.0f;
  GPUNodeLink *sc_link = GPU_constant(&same_chain_f);

  /* node_input_npr_bridge outputs color/value/vector/shader handles bound to out[0..3].
   * Shader output is g_combined_color (same chain) or black (different chain, Route A fallback). */
  GPU_stack_link(mat, node, "node_input_npr_bridge", in, out, hc, hf, hv, sc_link);
  return true;
}

}  // namespace nodes::node_shader_npr_bridge_input_cc

void register_node_type_sh_npr_bridge_input()
{
  namespace file_ns = nodes::node_shader_npr_bridge_input_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeNPR_BridgeInput"_ustr, SH_NODE_NPR_BRIDGE_INPUT);
  ntype.enum_name_legacy = "NPR_BRIDGE_INPUT";
  ntype.ui_name = "NPR Bridge Input";
  ntype.ui_description = "Read bridged material-tree data inside the NPR shader tree";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = npr_shader_nodes_poll;
  ntype.draw_buttons = file_ns::node_shader_buts_npr_bridge_input;
  ntype.initfunc = file_ns::node_shader_init_npr_bridge_input;
  bke::node_type_storage(
      ntype, "NodeShaderNPRBridge", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = file_ns::node_shader_gpu_npr_bridge_input;

  ntype.no_muting = true;

  bke::node_register_type(ntype);
}

}  // namespace blender
