import argparse
import copy
import math
import sys

import bpy

sys.path.append(bpy.utils.system_resource("SCRIPTS", path="addons_core"))
from io_scene_fbx import encode_bin, parse_fbx
import numpy as np
from mathutils import Euler, Matrix, Vector

USAGE = """Converts Unreal skeletal mesh FBX exports onto the reference skeleton Lumen's animations use.

  blender -b --factory-startup --python tools/ue5_to_lumen.py -- --reference AnimMan.FBX --name Echo --out Echo.FBX SKM_Echo.FBX SKM_Echo_Hair.FBX

Every input must share one Unreal skeleton (body, hair, attachments of one character). They are merged into
one FBX whose bones match the reference by name, hierarchy and rest rotation, while keeping the character's
own bone lengths. Bones the reference does not have are kept and follow their parent. Only LOD0 is kept and
blend shapes are dropped."""

BONE_KINDS = (b"LimbNode", b"Root")
BAD_PROPS = ("PreRotation", "PostRotation", "RotationOffset", "RotationPivot", "ScalingOffset", "ScalingPivot", "GeometricTranslation", "GeometricRotation")
CORE_BONES = ["root", "pelvis", "spine_01", "neck_01", "head"] + [f"{b}_{s}" for s in "lr" for b in ("clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot")]
FINGERS = ("index", "middle", "ring", "pinky", "thumb")
MATERIAL_KINDS = (b"Material", b"Texture", b"Video")
GROUNDED = {f"foot_{s}" for s in "lr"}


class El:
    __slots__ = ("id", "props", "types", "elems")

    def __init__(self, id, props=None, types=None, elems=None):
        self.id = id
        self.props = props if props is not None else []
        self.types = types if types is not None else []
        self.elems = elems if elems is not None else []


def conv(e):
    return El(e.id, list(e.props), list(e.props_type), [conv(c) for c in e.elems])


def child(e, id):
    for c in e.elems:
        if c.id == id:
            return c
    return None


def name_of(o):
    return o.props[1].split(b"\x00\x01")[0].decode()


def set_name(o, name):
    o.props[1] = name.encode() + b"\x00\x01" + o.props[1].split(b"\x00\x01")[1]


def prop70(o, name):
    p = child(o, b"Properties70")
    if p:
        for x in p.elems:
            if x.props[0] == name.encode():
                return x.props[4:]
    return None


def set_prop70(o, name, vals):
    p = child(o, b"Properties70")
    if p is None:
        p = El(b"Properties70")
        o.elems.insert(1, p)
    for x in p.elems:
        if x.props[0] == name.encode():
            x.props[4:] = [float(v) for v in vals]
            x.types[4:] = [ord("D")] * len(vals)
            return
    p.elems.append(El(b"P", [name.encode(), name.encode(), b"", b"A"] + [float(v) for v in vals], [ord("S")] * 4 + [ord("D")] * len(vals)))


def local_matrix(o):
    for bad in BAD_PROPS:
        v = prop70(o, bad)
        if v is not None and any(abs(x) > 1e-6 for x in v):
            raise SystemExit(f"'{name_of(o)}' uses {bad}, which this tool does not support. Export it from Unreal.")
    ro = prop70(o, "RotationOrder")
    if ro is not None and ro[0] != 0:
        raise SystemExit(f"'{name_of(o)}' uses a rotation order other than XYZ.")
    t = prop70(o, "Lcl Translation") or (0.0, 0.0, 0.0)
    r = prop70(o, "Lcl Rotation") or (0.0, 0.0, 0.0)
    s = prop70(o, "Lcl Scaling") or (1.0, 1.0, 1.0)
    return Matrix.Translation(t) @ Euler([math.radians(x) for x in r], "XYZ").to_matrix().to_4x4() @ Matrix.Diagonal((s[0], s[1], s[2], 1.0))


def fbx_to_matrix(arr):
    a = list(arr)
    return Matrix([a[0:4], a[4:8], a[8:12], a[12:16]]).transposed()


def to_np(m):
    return np.array([list(row) for row in m], dtype=np.float64)


def matrix_to_fbx(m):
    t = m.transposed()
    return np.array([float(t[r][c]) for r in range(4) for c in range(4)], dtype=np.float64)


class Scene:
    def __init__(self, path):
        self.path = path
        root, self.version = parse_fbx.parse(path)
        self.root = conv(root)
        self.objs = child(self.root, b"Objects")
        self.conns = child(self.root, b"Connections")
        self.byid = {o.props[0]: o for o in self.objs.elems}
        self.parent = {}
        self.children = {}
        for c in self.conns.elems:
            if c.props[0] != b"OO":
                continue
            a, b = c.props[1], c.props[2]
            self.children.setdefault(b, []).append(a)
            ao, bo = self.byid.get(a), self.byid.get(b)
            if ao is not None and ao.id == b"Model" and (b == 0 or (bo is not None and bo.id == b"Model")):
                self.parent[a] = b
        self._global = {}
        self.bones = {name_of(o): o for o in self.objs.elems if o.id == b"Model" and o.props[2] in BONE_KINDS}

    def models(self, kinds):
        return [o for o in self.objs.elems if o.id == b"Model" and o.props[2] in kinds]

    def global_matrix(self, oid):
        if oid == 0:
            return Matrix.Identity(4)
        if oid not in self._global:
            self._global[oid] = self.global_matrix(self.parent.get(oid, 0)) @ local_matrix(self.byid[oid])
        return self._global[oid]

    def linked(self, oid, id=None, sub=None):
        out = []
        for c in self.children.get(oid, []):
            o = self.byid.get(c)
            if o is not None and (id is None or o.id == id) and (sub is None or o.props[2] == sub):
                out.append(o)
        return out

    def bone_parent(self, name):
        p = self.parent.get(self.bones[name].props[0], 0)
        return name_of(self.byid[p]) if p and self.byid[p].props[2] in BONE_KINDS else None

    def bone_global(self, name):
        return self.global_matrix(self.bones[name].props[0])


def dfs(names, parent):
    kids = {}
    for n in names:
        kids.setdefault(parent[n], []).append(n)
    out = []
    stack = list(reversed(kids.get(None, [])))
    while stack:
        n = stack.pop()
        out.append(n)
        stack.extend(reversed(kids.get(n, [])))
    if len(out) != len(names):
        raise SystemExit("The skeleton has bones that are not connected to its root.")
    return out


def build_profile(names):
    has = set(names).__contains__
    missing = [b for b in CORE_BONES if not has(b)]
    if missing:
        raise SystemExit(f"Not an Unreal mannequin skeleton, missing: {', '.join(missing)}")
    remap, corr, aim = {}, {}, {}
    if has("spine_05"):
        print("Skeleton: UE5 mannequin")
        for src, dst in (("spine_02", "spine_01"), ("spine_03", "spine_02"), ("spine_04", "spine_02"), ("spine_05", "spine_03"), ("neck_02", "neck_01")):
            if has(src):
                remap[src] = dst
        corr = {"spine_02": "spine_03", "spine_03": "spine_05"}
        spine = [("spine_01", "spine_03", "spine_01", "spine_02"), ("spine_03", "spine_05", "spine_02", "spine_03"), ("spine_05", "neck_01", "spine_03", "neck_01")]
    else:
        print("Skeleton: UE4 mannequin")
        spine = [("spine_01", "spine_02", "spine_01", "spine_02"), ("spine_02", "spine_03", "spine_02", "spine_03"), ("spine_03", "neck_01", "spine_03", "neck_01")]
    spine.append(("neck_01", "head", "neck_01", "head"))
    for bone, e_aim, a_head, a_aim in spine:
        if has(bone) and has(e_aim):
            aim[bone] = (e_aim, a_head, a_aim)
    for s in "lr":
        for f in ("index", "middle", "ring", "pinky"):
            if has(f"{f}_metacarpal_{s}"):
                remap[f"{f}_metacarpal_{s}"] = f"hand_{s}"
        for src, dst in (("lowerarm_twist_02", "lowerarm"), ("upperarm_twist_02", "upperarm"), ("thigh_twist_02", "thigh_twist_01"), ("calf_twist_02", "calf_twist_01")):
            if has(f"{src}_{s}"):
                remap[f"{src}_{s}"] = f"{dst}_{s}"
        hand_aim = next((f"{f}_01_{s}" for f in ("middle", "index", "ring") if has(f"{f}_01_{s}")), None)
        chains = [[f"clavicle_{s}", f"upperarm_{s}", f"lowerarm_{s}", f"hand_{s}"] + ([hand_aim] if hand_aim else []), [f"thigh_{s}", f"calf_{s}", f"foot_{s}"]]
        chains += [[f"{f}_01_{s}", f"{f}_02_{s}", f"{f}_03_{s}"] for f in FINGERS]
        for ch in chains:
            for a, b in zip(ch, ch[1:]):
                if has(a) and has(b):
                    aim[a] = (b, a, b)
    return remap, corr, aim


def pick_meshes(scene):
    meshes = scene.models([b"Mesh"])
    lod0 = [m for m in meshes if name_of(m).endswith("_LOD0")]
    if lod0:
        return lod0
    out = []
    for m in meshes:
        p = scene.parent.get(m.props[0], 0)
        if p and scene.byid[p].props[2] == b"LodGroup":
            siblings = [c for c in scene.linked(p, b"Model", b"Mesh")]
            if siblings and siblings[0] is not m:
                continue
        out.append(m)
    return out


def control_points_of_polygon_vertices(geom):
    pvi = np.array(child(geom, b"PolygonVertexIndex").props[0], dtype=np.int64)
    return np.where(pvi < 0, ~pvi, pvi)


def transform_layer(geom, layer_id, data_id, rot):
    for layer in [e for e in geom.elems if e.id == layer_id]:
        data = child(layer, data_id)
        if data is None:
            continue
        mapping = child(layer, b"MappingInformationType").props[0]
        reference = child(layer, b"ReferenceInformationType").props[0]
        vals = np.array(data.props[0], dtype=np.float64).reshape(-1, 3)
        if mapping in (b"ByVertice", b"ByVertex"):
            cps = np.arange(len(rot))
        elif mapping == b"ByPolygonVertex":
            cps = control_points_of_polygon_vertices(geom)
        else:
            raise SystemExit(f"Unsupported {layer_id.decode()} mapping {mapping.decode()}.")
        if reference == b"Direct":
            targets = np.arange(len(cps))
        else:
            index = child(layer, data_id + b"Index")
            targets = np.array(index.props[0], dtype=np.int64)
        out = vals.copy()
        out[targets] = np.einsum("nij,nj->ni", rot[cps], vals[targets])
        out /= np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-12)
        data.props[0] = out.reshape(-1)


def fit_skin(sc, mesh, geom, skin, F, GE):
    verts = np.array(child(geom, b"Vertices").props[0], dtype=np.float64).reshape(-1, 3)
    acc = np.zeros((len(verts), 3, 4))
    wsum = np.zeros(len(verts))
    weights = {}
    template = None
    for cl in sc.linked(skin.props[0], b"Deformer", b"Cluster"):
        bone = sc.linked(cl.props[0], b"Model")
        if not bone or child(cl, b"Indexes") is None:
            continue
        template = template or cl
        e = name_of(bone[0])
        idx = np.array(child(cl, b"Indexes").props[0], dtype=np.int64)
        w = np.array(child(cl, b"Weights").props[0], dtype=np.float64)
        bind_mesh = sc.global_matrix(mesh.props[0])
        bind_bone = fbx_to_matrix(child(cl, b"TransformLink").props[0])
        f = to_np(bind_mesh.inverted() @ F[e] @ GE[e] @ bind_bone.inverted() @ bind_mesh)[:3, :]
        np.add.at(acc, idx, w[:, None, None] * f[None, :, :])
        np.add.at(wsum, idx, w)
        weights.setdefault(e, []).append((idx, w))
    loose = wsum < 1e-8
    acc[loose] = np.eye(3, 4)
    wsum[loose] = 1.0
    acc /= wsum[:, None, None]
    return verts, acc, weights, template


def skinned_meshes(sc):
    out = []
    for mesh in pick_meshes(sc):
        geoms = sc.linked(mesh.props[0], b"Geometry", b"Mesh")
        if len(geoms) != 1:
            continue
        skins = sc.linked(geoms[0].props[0], b"Deformer", b"Skin")
        if len(skins) != 1:
            print(f"skipping '{name_of(mesh)}' in {sc.path}: not skinned")
            continue
        out.append((mesh, geoms[0], skins[0]))
    return out


def up_axis(sc):
    gs = child(child(sc.root, b"GlobalSettings"), b"Properties70")
    axis, sign = 2, 1
    for p in gs.elems:
        if p.props[0] == b"UpAxis":
            axis = int(p.props[4])
        elif p.props[0] == b"UpAxisSign":
            sign = int(p.props[4])
    v = Vector((0.0, 0.0, 0.0))
    v[axis] = float(sign)
    return v


def encode(e):
    fe = encode_bin.FBXElem(e.id)
    for p, t in zip(e.props, e.types):
        c = chr(t)
        if c == "Z": fe.add_int8(int(p))
        elif c == "Y": fe.add_int16(int(p))
        elif c == "B": fe.add_bool(bool(p))
        elif c == "C": fe.add_char(p if isinstance(p, bytes) else bytes([p]))
        elif c == "I": fe.add_int32(int(p))
        elif c == "L": fe.add_int64(int(p))
        elif c == "F": fe.add_float32(float(p))
        elif c == "D": fe.add_float64(float(p))
        elif c == "R": fe.add_bytes(bytes(p))
        elif c == "S": fe.add_string(bytes(p))
        elif c == "i": fe.add_int32_array(np.asarray(p, dtype=np.int32))
        elif c == "l": fe.add_int64_array(np.asarray(p, dtype=np.int64))
        elif c == "f": fe.add_float32_array(np.asarray(p, dtype=np.float32))
        elif c == "d": fe.add_float64_array(np.asarray(p, dtype=np.float64))
        elif c == "b": fe.add_bool_array(np.asarray(p, dtype=bool))
        elif c == "c": fe.add_byte_array(np.asarray(p, dtype=np.byte))
        else: raise ValueError(c)
    for ch in e.elems:
        fe.elems.append(encode(ch))
    return fe


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser(prog="ue5_to_lumen", description=USAGE, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reference", required=True, help="FBX with the skeleton the animations were authored on (AnimMan.FBX)")
    ap.add_argument("--out", required=True, help="output FBX path")
    ap.add_argument("--name", help="name of the merged model (default: output file name)")
    ap.add_argument("inputs", nargs="+", help="Unreal skeletal mesh FBX exports that share one skeleton")
    args = ap.parse_args(argv)
    out_name = args.name or args.out.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0]

    ref = Scene(args.reference)
    srcs = [Scene(p) for p in args.inputs]

    ref_names = list(ref.bones)
    ref_parent = {n: ref.bone_parent(n) for n in ref_names}
    ref_order = dfs(ref_names, ref_parent)
    GA = {n: ref.bone_global(n) for n in ref_names}
    pos_a = {n: Vector(GA[n].translation) for n in ref_names}

    src_parent, GE, owner, differing = {}, {}, {}, []
    for sc in srcs:
        for n in sc.bones:
            p = sc.bone_parent(n)
            g = sc.bone_global(n)
            if n in GE:
                if src_parent[n] != p:
                    raise SystemExit(f"'{n}' has parent '{p}' in {sc.path} but '{src_parent[n]}' in {owner[n]}. The inputs do not share a skeleton.")
                if (Vector(g.translation) - Vector(GE[n].translation)).length > 0.05:
                    differing.append(n)
                continue
            src_parent[n], GE[n], owner[n] = p, g, sc.path
    if differing:
        print(f"note: {len(differing)} bones have a different rest pose between inputs, each mesh is re-posed from its own bind pose")
    src_order = dfs(list(GE), src_parent)
    remap, corr, aim = build_profile(src_order)
    specials = [e for e in src_order if e not in remap and e not in ref.bones]

    pos_e = {n: Vector(GE[n].translation) for n in src_order}
    R, P, F = {}, {}, {}
    for e in src_order:
        p = src_parent[e]
        if p is None:
            R[e], P[e] = Matrix.Identity(3), pos_e[e].copy()
        else:
            P[e] = P[p] + R[p] @ (pos_e[e] - pos_e[p])
            R[e] = Matrix.Identity(3) if e in GROUNDED else R[p].copy()
            if e in aim:
                e_aim, a_head, a_aim = aim[e]
                d_src = (R[p] @ (pos_e[e_aim] - pos_e[e])).normalized()
                d_ref = (pos_a[a_aim] - pos_a[a_head]).normalized()
                R[e] = d_src.rotation_difference(d_ref).to_matrix() @ R[p]
        m = R[e].to_4x4()
        m.translation = P[e] - R[e] @ pos_e[e]
        F[e] = m

    up = up_axis(srcs[0])
    before, after = math.inf, math.inf
    for sc in srcs:
        for mesh, geom, skin in skinned_meshes(sc):
            verts, acc, _, template = fit_skin(sc, mesh, geom, skin, F, GE)
            if template is None:
                continue
            bind_mesh = to_np(sc.global_matrix(mesh.props[0]))
            fitted = np.einsum("nij,nj->ni", acc[:, :, :3], verts) + acc[:, :, 3]
            height = lambda v: (v @ bind_mesh[:3, :3].T + bind_mesh[:3, 3]) @ np.array(up)
            before = min(before, float(height(verts).min()))
            after = min(after, float(height(fitted).min()))
    if math.isfinite(before) and abs(before - after) > 0.01:
        lift = up * (before - after)
        print(f"Lifting the fitted pose {before - after:.2f} cm so it stands where the input stood")
        for e in src_order:
            if src_parent[e] is not None:
                P[e] += lift
                F[e].translation = F[e].translation + lift

    GF = {}
    for a in ref_order:
        if a.startswith("ik_"):
            continue
        e = corr.get(a, a)
        m = GA[a].to_3x3().normalized().to_4x4()
        if e in P:
            m.translation = P[e]
        else:
            parent = ref_parent[a]
            m.translation = (GF[parent] @ (GA[parent].inverted() @ GA[a])).translation if parent else GA[a].translation
            print(f"warning: '{a}' is not in the input skeleton, placed from the reference")
        GF[a] = m
    for a in ref_order:
        if not a.startswith("ik_"):
            continue
        m = GA[a].to_3x3().normalized().to_4x4()
        if pos_a[a].length > 1e-4:
            near = min((b for b in ref_order if not b.startswith("ik_") and b != "root"), key=lambda b: (pos_a[b] - pos_a[a]).length)
            m.translation = GF[near].translation if (pos_a[near] - pos_a[a]).length < 0.5 else GF[ref_parent[a]].translation + (pos_a[a] - pos_a[ref_parent[a]])
        GF[a] = m
    for e in specials:
        m = (R[e] @ GE[e].to_3x3().normalized()).to_4x4()
        m.translation = P[e]
        GF[e] = m

    def final_name(e):
        return e if e in specials else remap.get(e, e)

    final_parent = dict(ref_parent)
    for e in specials:
        final_parent[e] = final_name(src_parent[e])
    final_order = dfs(ref_order + specials, final_parent)
    LF = {n: GF[n] if final_parent[n] is None else GF[final_parent[n]].inverted() @ GF[n] for n in final_order}
    print(f"Bones: {len(src_order)} in input, {len(remap)} merged into reference bones, {len(specials)} kept, {len(final_order)} in output")
    if len(final_order) > 255:
        print("warning: more than 255 bones, Lumen meshes index at most 255 joints per vertex")

    base = srcs[0]
    used = set()
    for sc in [ref] + srcs:
        used |= set(sc.byid)
    counter = [max(used) + 1000]

    def new_id():
        counter[0] += 16
        return counter[0]

    objects, conns = [], []

    def conn(a, b, kind=b"OO", prop=None):
        props, types = [kind, a, b], [ord("S"), ord("L"), ord("L")]
        if prop is not None:
            props.append(prop)
            types.append(ord("S"))
        conns.append(El(b"C", props, types))

    root_null = copy.deepcopy(next((m for m in base.models([b"Null"]) if base.parent.get(m.props[0], 0) == 0), None) or El(b"Model", [0, b"\x00\x01Model", b"Null"], [ord("L"), ord("S"), ord("S")], [El(b"Version", [232], [ord("I")])]))
    null_id = new_id()
    root_null.props[0] = null_id
    set_name(root_null, out_name)
    objects.append(root_null)
    conn(null_id, 0)

    bone_ids = {}
    for n in final_order:
        sc, srcm = (ref, ref.bones[n]) if n in ref.bones else (next(s for s in srcs if n in s.bones), None)
        if srcm is None:
            srcm = sc.bones[n]
        model = copy.deepcopy(srcm)
        mid = new_id()
        model.props[0] = mid
        bone_ids[n] = mid
        t = LF[n].translation
        set_prop70(model, "Lcl Translation", (t.x, t.y, t.z))
        if n not in ref.bones:
            set_prop70(model, "Lcl Rotation", [math.degrees(x) for x in LF[n].to_3x3().normalized().to_euler("XYZ")])
            set_prop70(model, "Lcl Scaling", (1.0, 1.0, 1.0))
        objects.append(model)
        for a in sc.linked(srcm.props[0], b"NodeAttribute"):
            attr = copy.deepcopy(a)
            aid = new_id()
            attr.props[0] = aid
            objects.append(attr)
            conn(aid, mid)
    for n in final_order:
        conn(bone_ids[n], bone_ids[final_parent[n]] if final_parent[n] else null_id)

    stack_done = False
    used_names = set()
    for sc in srcs:
        idmap = {}
        for o in sc.objs.elems:
            if o.id in (b"AnimationStack", b"AnimationLayer") and not stack_done:
                c = copy.deepcopy(o)
                idmap[o.props[0]] = c.props[0] = new_id()
                objects.append(c)
        stack_done = stack_done or bool(idmap)
        for c in sc.conns.elems:
            if c.props[1] in idmap and c.props[2] in idmap:
                conn(idmap[c.props[1]], idmap[c.props[2]])

        for mesh, geom_src, skin_src in skinned_meshes(sc):
            skins = [skin_src]
            label = name_of(mesh)
            if label.endswith("_LOD0"):
                label = label[:-5]
            while label in used_names:
                label += "_"
            used_names.add(label)

            gmesh = sc.global_matrix(mesh.props[0])
            geom = copy.deepcopy(geom_src)
            verts, acc, raw_weights, template = fit_skin(sc, mesh, geom, skins[0], F, GE)
            if template is None:
                print(f"skipping '{label}': no skin weights")
                continue
            weights = {}
            for e, entries in raw_weights.items():
                weights.setdefault(final_name(e), []).extend(entries)
            child(geom, b"Vertices").props[0] = (np.einsum("nij,nj->ni", acc[:, :, :3], verts) + acc[:, :, 3]).reshape(-1)
            for layer_id, data_id in ((b"LayerElementNormal", b"Normals"), (b"LayerElementTangent", b"Tangents"), (b"LayerElementBinormal", b"Binormals")):
                transform_layer(geom, layer_id, data_id, acc[:, :, :3])

            model = copy.deepcopy(mesh)
            mid = new_id()
            model.props[0] = mid
            set_name(model, label)
            gid = new_id()
            geom.props[0] = gid
            set_name(geom, label)
            skin = copy.deepcopy(skins[0])
            sid = new_id()
            skin.props[0] = sid
            objects += [model, geom, skin]
            conn(mid, null_id)
            conn(gid, mid)
            conn(sid, gid)

            mat_ids = {}
            for c in sc.conns.elems:
                if c.props[2] == mesh.props[0] and sc.byid.get(c.props[1]) is not None and sc.byid[c.props[1]].id == b"Material":
                    m = copy.deepcopy(sc.byid[c.props[1]])
                    mat_ids[c.props[1]] = m.props[0] = new_id()
                    objects.append(m)
                    conn(m.props[0], mid)
            frontier = dict(mat_ids)
            while frontier:
                found = {}
                for c in sc.conns.elems:
                    o = sc.byid.get(c.props[1])
                    if c.props[2] in frontier and o is not None and o.id in MATERIAL_KINDS and c.props[1] not in mat_ids:
                        d = copy.deepcopy(o)
                        found[c.props[1]] = mat_ids[c.props[1]] = d.props[0] = new_id()
                        objects.append(d)
                    if c.props[2] in frontier and c.props[1] in mat_ids:
                        conn(mat_ids[c.props[1]], frontier[c.props[2]], c.props[0], c.props[3] if len(c.props) > 3 else None)
                frontier = found

            for n in final_order:
                cid = new_id()
                el = El(b"Deformer", [cid, b"\x00\x01SubDeformer", b"Cluster"], [ord("L"), ord("S"), ord("S")])
                for c in template.elems:
                    if c.id in (b"Indexes", b"Weights"):
                        continue
                    if c.id == b"Transform":
                        el.elems.append(El(b"Transform", [matrix_to_fbx(GF[n].inverted() @ gmesh)], [ord("d")]))
                    elif c.id == b"TransformLink":
                        if n in weights:
                            idx = np.concatenate([x[0] for x in weights[n]])
                            w = np.concatenate([x[1] for x in weights[n]])
                            uniq, inv = np.unique(idx, return_inverse=True)
                            el.elems.append(El(b"Indexes", [uniq.astype(np.int32)], [ord("i")]))
                            el.elems.append(El(b"Weights", [np.bincount(inv, weights=w)], [ord("d")]))
                        el.elems.append(El(b"TransformLink", [matrix_to_fbx(GF[n])], [ord("d")]))
                    else:
                        el.elems.append(copy.deepcopy(c))
                objects.append(el)
                conn(cid, sid)
                conn(bone_ids[n], cid)

            pose = El(b"Pose", [new_id(), label.encode() + b"\x00\x01Pose", b"BindPose"], [ord("L"), ord("S"), ord("S")], [
                El(b"Type", [b"BindPose"], [ord("S")]), El(b"Version", [100], [ord("I")]), El(b"NbPoseNodes", [len(final_order) + 2], [ord("I")])])
            for node, mtx in [(null_id, Matrix.Identity(4)), (mid, gmesh)] + [(bone_ids[n], GF[n]) for n in final_order]:
                pose.elems.append(El(b"PoseNode", [], [], [El(b"Node", [node], [ord("L")]), El(b"Matrix", [matrix_to_fbx(mtx)], [ord("d")])]))
            objects.append(pose)
            shapes = len(sc.linked(geom_src.props[0], b"Deformer", b"BlendShape"))
            print(f"mesh '{label}' from {sc.path}: {len(verts)} vertices{', blend shapes dropped' if shapes else ''}")

    base.objs.elems = objects
    base.conns.elems = conns

    templates = {}
    for sc in [base] + srcs[1:] + [ref]:
        for d in child(sc.root, b"Definitions").elems:
            if d.id == b"ObjectType" and d.props[0] not in templates:
                templates[d.props[0]] = d
    counts = {}
    for o in objects:
        counts[o.id] = counts.get(o.id, 0) + 1
    counts[b"GlobalSettings"] = 1
    defs = child(base.root, b"Definitions")
    kept = [e for e in defs.elems if e.id != b"ObjectType"]
    total = 0
    for kind, d in templates.items():
        if counts.get(kind, 0) == 0:
            continue
        d = copy.deepcopy(d)
        child(d, b"Count").props[0] = counts[kind]
        total += counts[kind]
        kept.append(d)
    defs.elems = kept
    child(defs, b"Count").props[0] = total

    out_root = encode_bin.FBXElem(b"")
    for e in base.root.elems:
        out_root.elems.append(encode(e))
    encode_bin.write(args.out, out_root, base.version)
    print("wrote", args.out)


main()
