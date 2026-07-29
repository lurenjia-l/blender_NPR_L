/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

#include <cstdio>

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "GPU_material.hh"

#include "BLI_hash.h"

#include "DNA_material_types.h"
#include "DNA_node_types.h"

#include "BKE_npr.hh"

namespace blender {

namespace nodes::node_shader_npr_bridge_output_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Color").default_value({0.0f, 0.0f, 0.0f, 1.0f});
  b.add_input<decl::Float>("Float").default_value(0.0f);
  b.add_input<decl::Vector>("Vector").default_value({0.0f, 0.0f, 0.0f});
  b.add_input<decl::Shader>("Shader");
}

static void node_shader_buts_npr_bridge_output(ui::Layout &layout,
                                               bContext * /*C*/,
                                               PointerRNA *ptr)
{
  layout.prop(ptr, "bridge_name", ui::ITEM_R_SPLIT_EMPTY_NAME, std::nullopt, ICON_NONE);
}

static void node_shader_init_npr_bridge_output(bNodeTree * /*ntree*/, bNode *node)
{
  NodeShaderNPRBridge *data = MEM_new<NodeShaderNPRBridge>("NodeShaderNPRBridge");
  node->storage = data;
}

static int node_shader_gpu_npr_bridge_output(GPUMaterial *mat,
                                             bNode *node,
                                             bNodeExecData * /*execdata*/,
                                             GPUNodeStack *in,
                                             GPUNodeStack *out)
{
  Material *material = GPU_material_get_material(mat);
  GPU_material_flag_set(mat, GPU_MATFLAG_AOV);
  printf("[NPR Bridge] Output gpu_fn: node=%s in[0].link=%p in[1].link=%p in[2].link=%p in[3].link=%p\n",
         node->name,
         (void *)in[0].link,
         (void *)in[1].link,
         (void *)in[2].link,
         (void *)in[3].link);

  /* Color and Float write via node_output_aov using manual GPU_link (not GPU_stack_link, which
   * mis-binds when the node has 4 inputs but node_output_aov only takes color+value). */
  auto emit_stack = [&](GPUNodeLink *value_link, int socket_type) {
    if (value_link == nullptr) {
      return;
    }
    char name[128];
    BKE_npr_bridge_socket_name(material, node, socket_type, name, sizeof(name));
    uint hash = BLI_hash_string(name);
    GPUNodeLink *hash_link = GPU_constant(reinterpret_cast<float *>(&hash));
    GPUNodeLink *outlink = nullptr;
    if (socket_type == 0) {
      /* Color: color = value_link, value = 0 */
      float zero_f = 0.0f;
      GPUNodeLink *zero_value = GPU_constant(&zero_f);
      GPU_link(mat, "node_output_aov", value_link, zero_value, hash_link, &outlink);
    }
    else {
      /* Float: color = 0, value = value_link */
      GPUNodeLink *zero_color = nullptr;
      GPU_link(mat, "set_rgba_zero", &zero_color);
      GPU_link(mat, "node_output_aov", zero_color, value_link, hash_link, &outlink);
    }
    GPU_material_add_output_link_aov(mat, outlink, hash);
  };
  emit_stack(in[0].link, 0); /* Color -> color buffer */
  emit_stack(in[1].link, 1); /* Float -> value buffer */

  /* Vector: pack to color first, then write via node_output_aov (color buffer). */
  if (in[2].link != nullptr) {
    char name[128];
    BKE_npr_bridge_socket_name(material, node, 2, name, sizeof(name));
    uint hash = BLI_hash_string(name);
    GPUNodeLink *hash_link = GPU_constant(reinterpret_cast<float *>(&hash));
    GPUNodeLink *vec_color = nullptr;
    GPU_link(mat, "npr_bridge_vec_to_color", in[2].link, &vec_color);
    printf("[NPR Bridge] Output Vector: name=%s hash=%u vec_color=%p\n",
           name,
           hash,
           (void *)vec_color);
    float zero = 0.0f;
    GPUNodeLink *zero_link = GPU_constant(&zero);
    GPUNodeLink *outlink = nullptr;
    GPU_link(mat, "node_output_aov", vec_color, zero_link, hash_link, &outlink);
    GPU_material_add_output_link_aov(mat, outlink, hash);
  }

  /* Shader (BSDF) bridge is handled on the Input side via same-chain detection (T6). */
  return true;
}

}  // namespace nodes::node_shader_npr_bridge_output_cc

void register_node_type_sh_npr_bridge_output()
{
  namespace file_ns = nodes::node_shader_npr_bridge_output_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeNPR_BridgeOutput", SH_NODE_NPR_BRIDGE_OUTPUT);
  ntype.enum_name_legacy = "NPR_BRIDGE_OUTPUT";
  ntype.ui_name = "NPR Bridge Output";
  ntype.ui_description =
      "Bridge material-tree data (Color/Float/Vector/Shader) to the attached NPR tree";
  ntype.nclass = NODE_CLASS_OUTPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_shader_nodes_poll;
  ntype.draw_buttons = file_ns::node_shader_buts_npr_bridge_output;
  ntype.initfunc = file_ns::node_shader_init_npr_bridge_output;
  bke::node_type_storage(
      ntype, "NodeShaderNPRBridge", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = file_ns::node_shader_gpu_npr_bridge_output;

  ntype.no_muting = true;

  bke::node_register_type(ntype);
}

}  // namespace blender
