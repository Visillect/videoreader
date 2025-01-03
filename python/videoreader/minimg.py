"""
Backend for proprietary "minimg" library
"""

from __future__ import annotations
from . import VideoReaderBase, ffi, LogCallback
from collections.abc import Iterator
from minimg import MinImg, TYP_UINT8


@ffi.callback("void (VRImage*, void*)")
def alloc_callback_numpy(image: ffi.CData, self: ffi.CData) -> None:
    assert image.scalar_type == 0, f"non uint8 images not yet supported"
    img = MinImg.empty(
        image.width, image.height, image.channels, mintype=TYP_UINT8
    )
    image.data = img.data
    image.stride = img.stride
    address = int(ffi.cast("uintptr_t", image.data))
    memory = ffi.from_handle(self).memory
    assert address not in memory, "programmer error"
    memory[address] = img


@ffi.callback("void (VRImage*, void*)")
def free_callback_callback_numpy(image: ffi.CData, self: ffi.CData) -> None:
    address = int(ffi.cast("uintptr_t", image.data))
    assert address in ffi.from_handle(self).memory


class VideoReaderMinImg(VideoReaderBase[MinImg]):
    def __init__(
        self,
        path: str,
        arguments: list[str] = [],
        extras: list[str] = [],
        log_callback: LogCallback = None,
    ):
        self.memory: dict[int, MinImg] = {}
        super().__init__(
            path,
            arguments,
            extras,
            alloc_callback_numpy,
            free_callback_callback_numpy,
            log_callback,
        )

    def __iter__(self) -> Iterator[tuple[MinImg, int, float]]:
        for image, *other in super().__iter__():
            address = int(ffi.cast("uintptr_t", image.data))
            yield (self.memory.pop(address), *other)

    def __del__(self):
        if self.memory:
            print(f"something went wrong when using {self}")
        self.memory.clear()
