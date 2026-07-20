#!/usr/bin/env python3
"""Subdivide triangular Descent 3 OOF submodels while preserving their metadata."""

from __future__ import annotations

import argparse
import math
import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Face:
    textured: int
    material: bytes
    corners: list[tuple[int, float, float]]
    lightmap_size: bytes


def read_hog_member(path: Path, member: str) -> bytes:
    with path.open("rb") as stream:
        if stream.read(4) != b"HOG2":
            raise ValueError(f"{path} is not a HOG2 archive")
        file_count, data_offset = struct.unpack("<II", stream.read(8))
        stream.seek(4 + 64)
        entries: list[tuple[str, int]] = []
        for _ in range(file_count):
            name, _flags, size, _timestamp = struct.unpack("<36sIII", stream.read(48))
            entries.append((name.split(b"\0", 1)[0].decode("latin1"), size))

        offset = data_offset
        for name, size in entries:
            if name.casefold() == member.casefold():
                stream.seek(offset)
                return stream.read(size)
            offset += size
    raise FileNotFoundError(f"{member} was not found in {path}")


def normalize(vector: tuple[float, float, float], length: float = 1.0) -> tuple[float, float, float]:
    magnitude = math.sqrt(sum(component * component for component in vector))
    if magnitude == 0.0:
        raise ValueError("Cannot normalize a zero-length vector")
    scale = length / magnitude
    return tuple(component * scale for component in vector)


def face_normal(vertices: list[tuple[float, float, float]], indices: tuple[int, int, int]) -> tuple[float, float, float]:
    a, b, c = (vertices[index] for index in indices)
    ab = tuple(b[i] - a[i] for i in range(3))
    ac = tuple(c[i] - a[i] for i in range(3))
    return normalize(
        (
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
    )


def subdivide_sobj(body: bytes, version: int) -> tuple[bytes, int, int, int, int]:
    major_version = version // 100
    cursor = 0

    # Fixed SOBJ metadata through the optional geometric center.
    cursor += 8 + 12 + 4 + 12 + 12 + 4 + 4 + 4
    if version > 1805:
        cursor += 12

    # Name and properties are length-prefixed strings.
    for _ in range(2):
        string_size = struct.unpack_from("<i", body, cursor)[0]
        cursor += 4 + string_size

    cursor += 8  # movement type and axis
    free_chunk_count = struct.unpack_from("<i", body, cursor)[0]
    cursor += 4 + 4 * free_chunk_count

    vertex_count_offset = cursor
    old_vertex_count = struct.unpack_from("<i", body, cursor)[0]
    cursor += 4
    vertices = [struct.unpack_from("<fff", body, cursor + 12 * i) for i in range(old_vertex_count)]
    cursor += 12 * old_vertex_count
    normals = [struct.unpack_from("<fff", body, cursor + 12 * i) for i in range(old_vertex_count)]
    cursor += 12 * old_vertex_count
    if major_version >= 23:
        alpha = list(struct.unpack_from(f"<{old_vertex_count}f", body, cursor))
        cursor += 4 * old_vertex_count
    else:
        alpha = [1.0] * old_vertex_count

    old_face_count = struct.unpack_from("<i", body, cursor)[0]
    cursor += 4
    faces: list[Face] = []
    for _ in range(old_face_count):
        cursor += 12  # The old plane normal is regenerated after subdivision.
        corner_count, textured = struct.unpack_from("<ii", body, cursor)
        cursor += 8
        if corner_count != 3:
            raise ValueError(f"Subdivision requires triangles, found a {corner_count}-vertex face")
        material_size = 4 if textured else 3
        material = body[cursor : cursor + material_size]
        cursor += material_size
        corners = [struct.unpack_from("<iff", body, cursor + 12 * i) for i in range(3)]
        cursor += 36
        lightmap_size = body[cursor : cursor + 8] if major_version >= 21 else b""
        cursor += len(lightmap_size)
        faces.append(Face(textured, material, corners, lightmap_size))

    tail = body[cursor:]
    if tail:
        raise ValueError(f"Unexpected {len(tail)} trailing bytes in SOBJ chunk")

    radii = [math.sqrt(sum(component * component for component in vertex)) for vertex in vertices]
    radius = sum(radii) / len(radii)
    if max(abs(value - radius) for value in radii) > radius * 1.0e-4:
        raise ValueError("Submodel is not centered on a consistent spherical radius")

    edge_midpoints: dict[tuple[int, int], int] = {}

    def midpoint(a: int, b: int) -> int:
        edge = (min(a, b), max(a, b))
        existing = edge_midpoints.get(edge)
        if existing is not None:
            return existing

        position = normalize(tuple((vertices[a][i] + vertices[b][i]) * 0.5 for i in range(3)), radius)
        normal = normalize(tuple((normals[a][i] + normals[b][i]) * 0.5 for i in range(3)))
        index = len(vertices)
        vertices.append(position)
        normals.append(normal)
        alpha.append((alpha[a] + alpha[b]) * 0.5)
        edge_midpoints[edge] = index
        return index

    new_faces: list[Face] = []
    for face in faces:
        (a, au, av), (b, bu, bv), (c, cu, cv) = face.corners
        ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
        uv_ab = ((au + bu) * 0.5, (av + bv) * 0.5)
        uv_bc = ((bu + cu) * 0.5, (bv + cv) * 0.5)
        uv_ca = ((cu + au) * 0.5, (cv + av) * 0.5)
        children = (
            ((a, au, av), (ab, *uv_ab), (ca, *uv_ca)),
            ((ab, *uv_ab), (b, bu, bv), (bc, *uv_bc)),
            ((ca, *uv_ca), (bc, *uv_bc), (c, cu, cv)),
            ((ab, *uv_ab), (bc, *uv_bc), (ca, *uv_ca)),
        )
        new_faces.extend(Face(face.textured, face.material, list(child), face.lightmap_size) for child in children)

    output = bytearray(body[:vertex_count_offset])
    output += struct.pack("<i", len(vertices))
    for vertex in vertices:
        output += struct.pack("<fff", *vertex)
    for normal in normals:
        output += struct.pack("<fff", *normal)
    if major_version >= 23:
        output += struct.pack(f"<{len(alpha)}f", *alpha)

    output += struct.pack("<i", len(new_faces))
    for face in new_faces:
        indices = tuple(corner[0] for corner in face.corners)
        output += struct.pack("<fff", *face_normal(vertices, indices))
        output += struct.pack("<ii", 3, face.textured)
        output += face.material
        for corner in face.corners:
            output += struct.pack("<iff", *corner)
        output += face.lightmap_size

    return bytes(output), old_vertex_count, len(vertices), old_face_count, len(new_faces)


def subdivide_oof(data: bytes) -> tuple[bytes, list[tuple[int, int, int, int]]]:
    if data[:4] != b"PSPO":
        raise ValueError("Input is not a new-style PSPO/OOF model")
    version = struct.unpack_from("<I", data, 4)[0]
    output = bytearray(data[:8])
    cursor = 8
    changes: list[tuple[int, int, int, int]] = []
    while cursor < len(data):
        chunk_id = data[cursor : cursor + 4]
        chunk_size = struct.unpack_from("<I", data, cursor + 4)[0]
        body_start = cursor + 8
        body = data[body_start : body_start + chunk_size]
        if chunk_id == b"SOBJ":
            body, old_vertices, new_vertices, old_faces, new_faces = subdivide_sobj(body, version)
            changes.append((old_vertices, new_vertices, old_faces, new_faces))
        output += chunk_id + struct.pack("<I", len(body)) + body
        cursor = body_start + chunk_size
    if cursor != len(data):
        raise ValueError("OOF chunk table extends beyond the input file")
    return bytes(output), changes


def main() -> None:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="standalone OOF input")
    source.add_argument("--hog", type=Path, help="HOG2 archive containing the input")
    parser.add_argument("--member", help="member name when --hog is used")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if args.hog:
        if not args.member:
            parser.error("--member is required with --hog")
        original = read_hog_member(args.hog, args.member)
    else:
        original = args.input.read_bytes()

    subdivided, changes = subdivide_oof(original)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(subdivided)
    for index, (old_vertices, new_vertices, old_faces, new_faces) in enumerate(changes):
        print(
            f"submodel {index}: {old_vertices} -> {new_vertices} vertices, "
            f"{old_faces} -> {new_faces} triangles"
        )


if __name__ == "__main__":
    main()
