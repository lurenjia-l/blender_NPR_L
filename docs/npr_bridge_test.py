"""NPR Bridge 节点测试脚本

在 Blender 的 Scripting 工作区 Text Editor 里运行(需先编译并启动含 NPR Bridge 节点的 blender)。
验证 Color/Float/Vector/Shader 桥接通路。

用法:
  1. 编译并启动新 blender
  2. 切到 Scripting 工作区,新建 Text,粘贴本脚本
  3. 点 Run Script
  4. 切到 Layout 工作区,把视图调成 Rendered,观察结果
"""

import bpy


def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat)


def make_material_with_bridge():
    """创建一个材质:Principled BSDF -> Material Output + Bridge Output(同链 Shader)。"""
    mat = bpy.data.materials.new("NPR_Bridge_Test")
    mat.use_nodes = True
    nt = mat.node_tree
    nodes = nt.nodes
    links = nt.links

    # 清掉默认节点,重建
    for n in list(nodes):
        nodes.remove(n)

    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (-300, 100)
    bsdf.inputs["Base Color"].default_value = (0.2, 0.5, 0.9, 1.0)  # 蓝

    output = nodes.new("ShaderNodeOutputMaterial")
    output.location = (300, 100)
    links.new(bsdf.outputs["BSDF"], output.inputs["Surface"])

    # Bridge Output:Color(红)+ Shader(同链 BSDF)
    bridge_out = nodes.new("ShaderNodeNPR_BridgeOutput")
    bridge_out.location = (300, -150)
    bridge_out.bridge_name = "slot1"

    rgb = nodes.new("ShaderNodeRGB")
    rgb.location = (50, -300)
    rgb.outputs["Color"].default_value = (1.0, 0.1, 0.1, 1.0)  # 红
    links.new(rgb.outputs["Color"], bridge_out.inputs["Color"])
    links.new(bsdf.outputs["BSDF"], bridge_out.inputs["Shader"])  # 同链

    return mat, output


def attach_npr_tree(mat, material_output):
    """给材质的 EEVEE Output 挂一个 NPR 树,里面放 Bridge Input + NPR Output。"""
    # NPR 树通过材质的 EEVEE Output 节点的 id 字段关联(见 npr_tree_get)。
    # UI 上:选中材质 -> EEVEE Output 节点 -> NPR Tree 字段 -> New。
    # 这里用 Python:创建一个 shader node tree,挂到 output.id。
    npr_tree = bpy.data.node_groups.new("NPR_Tree", "ShaderNodeTree")
    material_output.id = npr_tree  # 关联 NPR 树(参考 npr_tree_get:output->id)

    nt = npr_tree
    nodes = nt.nodes
    links = nt.links

    # NPR Input(对照)
    npr_input = nodes.new("ShaderNodeNPR_Input")
    npr_input.location = (-300, 200)

    # Bridge Input(name 匹配 "slot1")
    bridge_in = nodes.new("ShaderNodeNPR_BridgeInput")
    bridge_in.location = (-300, 0)
    bridge_in.bridge_name = "slot1"

    # NPR Output
    npr_output = nodes.new("ShaderNodeNPR_Output")
    npr_output.location = (300, 100)
    # 把 Bridge Input 的 Shader 输出(== BSDF 同链 g_combined_color)连到 NPR Output
    links.new(bridge_in.outputs["Shader"], npr_output.inputs["Color"])

    return npr_tree, bridge_in


def main():
    clear_scene()
    bpy.ops.mesh.primitive_plane_add(size=2)
    obj = bpy.context.object

    mat, mat_output = make_material_with_bridge()
    obj.data.materials.append(mat)

    npr_tree, bridge_in = attach_npr_tree(mat, mat_output)

    print("=== NPR Bridge 测试场景已创建 ===")
    print(f"材质: {mat.name}")
    print(f"NPR 树: {npr_tree.name}")
    print(f"Bridge Input name: {bridge_in.bridge_name}")
    print("")
    print("验证步骤:")
    print("  1. 视图切 Rendered(Eevee)")
    print("  2. NPR Output 收的是 Bridge Input Shader = BSDF 同链 g_combined_color")
    print("     -> 应看到蓝色平面(BSDF 完全光照结果,== BSDF 直连 Material Output)")
    print("  3. 把 NPR Output 改连 Bridge Input Color -> 应看到红色(RGB 节点的色)")
    print("  4. Bridge Output Shader 改连独立 BSDF(不连主输出) -> Bridge Input Shader 返回黑色")
    print("")


main()
