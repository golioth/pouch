#!/usr/bin/env python3

from contextlib import contextmanager
import json
import struct

from itertools import batched
import cbor2
import typer
from typing_extensions import Annotated


LOREM_IPSUM = """\
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Mauris varius leo sed elit rutrum, sit amet imperdiet velit suscipit. Curabitur eget iaculis quam. Quisque porta risus orci, id sodales enim ullamcorper et. Donec eleifend orci velit, vel efficitur diam hendrerit eu. Donec et tempor sapien. Fusce ultrices varius fermentum. Interdum et malesuada fames ac ante ipsum primis in faucibus. Duis maximus id elit vel rutrum. Vestibulum in turpis pharetra, venenatis dolor consequat, commodo ligula. Nunc ac mi viverra, accumsan sapien vitae, porta velit. Nam diam lacus, luctus in consectetur quis, congue eget sapien. Vivamus interdum, nisi quis viverra tempor, erat justo pulvinar urna, non condimentum tortor ligula molestie nisl. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam eu tempus quam.
Donec ac interdum orci. Aenean gravida, massa sit amet efficitur condimentum, diam ipsum faucibus dolor, ut lacinia sapien neque sit amet tellus. Fusce pharetra vehicula erat et luctus. Ut euismod, neque vel mollis venenatis, ligula lorem convallis metus, ac dapibus nulla mauris eu ante. Aenean scelerisque dolor ipsum, non euismod lorem facilisis ac. Mauris rhoncus porta tortor at dapibus. Vestibulum eget justo sed velit finibus auctor facilisis eget arcu. Ut pulvinar consequat sapien, eu accumsan ipsum molestie et. In pharetra purus id porttitor vestibulum. Quisque ut rutrum dui, vitae egestas nulla. Aenean in ante nec elit faucibus iaculis. Curabitur vehicula ligula et leo semper bibendum.
Maecenas pretium tortor neque, id iaculis risus efficitur in. Mauris egestas finibus odio. Maecenas enim arcu, sagittis ut aliquet in, mollis a mauris. Quisque velit erat, congue in orci at, ultrices interdum urna. Aliquam erat volutpat. Nunc vestibulum ligula arcu, sit amet suscipit erat consectetur sed. Phasellus dictum pulvinar risus, et finibus arcu luctus et.
Phasellus id congue justo, vel dictum nulla. Maecenas sed mi libero. Integer efficitur velit turpis, laoreet volutpat dui rhoncus ac. Quisque aliquet dolor condimentum felis vulputate, a pulvinar quam rutrum. Quisque et neque eros. In id elit arcu. Aenean mattis eros ut elit feugiat, nec tincidunt justo dictum. Suspendisse tristique nisl et consequat suscipit. Nullam interdum eros vel nibh facilisis tristique. Duis purus turpis, mollis sed diam eget, maximus placerat massa. In tempus venenatis ultricies. Nunc ultricies purus eget auctor consectetur.
Sed a tortor finibus, semper nunc vitae, dignissim urna. Nunc fermentum volutpat velit. Praesent sit amet quam in augue interdum feugiat in vel nulla. Etiam blandit laoreet cursus. Maecenas faucibus, orci sed fermentum consectetur, justo neque aliquet velit, sed varius massa sapien a sapien. Nulla consectetur enim vitae mauris posuere maximus. Morbi lobortis dolor mauris, sit amet vestibulum velit varius vitae. Etiam luctus eros dolor, et congue velit finibus sit amet. Fusce luctus bibendum rutrum. Mauris ultricies convallis diam, non elementum elit rutrum sit amet. Nam ut ipsum vitae risus commodo porta.
Vestibulum ornare ex nec scelerisque sollicitudin. Nunc pulvinar risus tristique magna vestibulum, vel faucibus justo accumsan. Donec et nisi lacus. Nam fermentum eget erat et suscipit. Nunc pharetra, nunc nec accumsan bibendum, augue dolor vehicula ante, nec aliquam lorem tortor vel odio. Curabitur facilisis, sem ut sollicitudin gravida, elit leo rhoncus felis, non accumsan ipsum velit sit amet enim. Fusce viverra est quis lacus congue aliquam. Nulla ex leo, molestie sit amet lobortis sit amet, sagittis nec enim. Morbi nec mi ut elit luctus dictum. Nullam sed neque vitae nulla convallis facilisis.
Etiam id lobortis nisl. Morbi tempus tempus mi, sed ornare ligula vulputate in. Cras neque erat, viverra pulvinar auctor in, blandit sit amet nisl. Phasellus gravida neque eu velit facilisis ultrices. Sed maximus, urna a gravida tristique, magna elit tincidunt mi, ut aliquet ipsum metus nec nulla. Proin molestie justo id felis dapibus, at imperdiet neque ornare. Curabitur condimentum ex quis pharetra aliquet. Nulla sem tortor, congue in arcu et, ultricies sollicitudin sapien. Aenean nec orci augue. Pellentesque sit amet velit et arcu facilisis lobortis. Duis eu interdum leo. Proin lectus dui, placerat et ante dictum, scelerisque sodales justo. Pellentesque auctor eros quis lectus convallis eleifend. Orci varius natoque penatibus et magnis dis parturient montes, nascetur ridiculus mus. Praesent vel ipsum eu justo egestas sagittis vitae quis arcu.
"""

BLOCK_SIZE = 512
BLOCK_HEADER_F_LEN_LEN = 2
BLOCK_HEADER_F_ID_LEN = 1
BLOCK_HEADER_LEN = BLOCK_HEADER_F_LEN_LEN + BLOCK_HEADER_F_ID_LEN
BLOCK_FIRST = 0x40
BLOCK_LAST = 0x80

BLOCK_CAPACITY = BLOCK_SIZE - BLOCK_HEADER_LEN

CONTENT_TYPE_MAP = {
    "text/octet-stream": 42,
    "application/json": 50,
    "application/cbor": 60,
}


app = typer.Typer()


def entry_stream_chunks(entry: bytes):
    flags = BLOCK_FIRST

    while entry:
        chunk = entry[:BLOCK_SIZE - BLOCK_HEADER_LEN]
        entry = entry[BLOCK_SIZE - BLOCK_HEADER_LEN:]

        if len(entry) == 0:
            flags |= BLOCK_LAST

        yield chunk, flags

        flags = 0


def entry_pack(entry: dict, stream: bool) -> bytes:
    entry_bin = b""
    if not stream:
        entry_bin += struct.pack(">H", len(entry["data"]))

    entry_bin += struct.pack(">HB",
                             CONTENT_TYPE_MAP[entry["content_type"]],
                             len(entry["path"]))
    entry_bin += entry["path"].encode("utf-8")
    entry_bin += (entry["data"].encode("utf-8") if isinstance(entry["data"], str) else entry["data"])

    return entry_bin


def block_pack(payload: bytes, stream_id: int, flags: int) -> bytes:
    return struct.pack(">HB", len(payload) + 1, stream_id | flags) + payload


def payload_append(payload: bytearray, data: bytes, stream_id: int, flags: int):
    block = block_pack(data, stream_id, flags)
    payload += block


class CInitializer:
    def __init__(self, out, init_indent_level: int = 0):
        self.out = out
        self.indent_level = init_indent_level

    def indentation(self):
        return "\t" * self.indent_level

    @contextmanager
    def indent(self):
        self.indent_level += 1
        yield
        self.indent_level -= 1

    @contextmanager
    def array_or_struct(self, name: str | None, end: str = ",", cast: str = ""):
        if name:
            self.out.write((self.indentation() + f".{name} = {cast}" + "{\n"))
        else:
            self.out.write((self.indentation() + "{\n"))

        with self.indent():
            yield

        self.out.write((self.indentation() + "}" + end + "\n"))

    @contextmanager
    def array(self, name: str | None = None, end: str = ",", cast: str = ""):
        with self.array_or_struct(name, end, cast):
            yield

    @contextmanager
    def byte_array(self, name: str | None = None, end: str = ","):
        with self.array_or_struct(name, end, cast="(const uint8_t []) "):
            yield

    @contextmanager
    def struct(self, name: str | None = None, end: str = ","):
        with self.array_or_struct(name, end):
            yield

    def write(self, line: str):
        self.out.write((self.indentation() + line))

    def writeln(self, line: str):
        self.out.write((self.indentation() + line + "\n"))

    def write_batched(self, payload: str | bytes, n: int = 16):
        if isinstance(payload, str):
            payload = payload.encode("utf-8")

        for batch in batched(payload, n):
            self.writeln(" ".join([f"0x{c:02x}," for c in batch]))


@app.command()
def main(
        name: Annotated[str, typer.Argument],
        out: Annotated[typer.FileTextWrite, typer.Argument()],
        device_name: Annotated[str, typer.Option()] = "id123",
        length: Annotated[int, typer.Option(min=0)] = len(LOREM_IPSUM),
        count: Annotated[int, typer.Option()] = 1,
):
    obj = {
        "device_id": device_name,
        "entries": [
            {
                "path": "/.s/lorem",
                "content_type": "application/json",
                "data": json.dumps({"lorem": (LOREM_IPSUM * 50)[:length]}),
            },
        ] * count,
    }

    # Fixed header for plaintext encoding
    header = [1, [0, obj["device_id"]]]

    payload = bytearray()
    payload += cbor2.dumps(header)
    stream_id = 1

    block_buf = bytearray()

    for entry in obj["entries"]:
        if len(entry["data"]) > BLOCK_CAPACITY:
            if block_buf:
                payload_append(payload, block_buf, 0, BLOCK_FIRST | BLOCK_LAST)
                block_buf = bytearray()

            entry_bin = entry_pack(entry, True)

            for chunk, flags in entry_stream_chunks(entry_bin):
                payload_append(payload, chunk, stream_id, flags)

            stream_id += 1
        else:
            entry_bin = entry_pack(entry, False)

            if len(block_buf) + len(entry_bin) > BLOCK_CAPACITY:
                payload_append(payload, block_buf, 0, BLOCK_FIRST | BLOCK_LAST)
                block_buf = bytearray()

            block_buf += entry_bin

    if block_buf:
        payload_append(payload, block_buf, 0, BLOCK_FIRST | BLOCK_LAST)

    c = CInitializer(out)

    c.write(f"const struct pouch_test_item {name} = ")

    with c.struct(end=";"):
        # data
        with c.byte_array("data"):
            c.write_batched(payload)
        c.writeln(f".data_len = {len(payload)},")

        # entries
        with c.array("entries", cast="(const struct pouch_test_item_entry[]) "):
            for entry in obj["entries"]:
                with c.struct(""):
                    c.writeln(f'.path = "{entry['path']}",')
                    c.writeln(f'.content_type = {CONTENT_TYPE_MAP[entry['content_type']]},')

                    with c.byte_array("data"):
                        c.write_batched(entry["data"])
                    c.writeln(f".data_len = {len(entry['data'])},")
        c.writeln(f".num_entries = {len(obj['entries'])},")


if __name__ == '__main__':
    app()
