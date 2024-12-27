from __future__ import annotations
from typing import TypeAlias, Any
import numpy as np
from . import VideoReaderBase, ffi, LogCallback
from collections.abc import Iterator


Image: TypeAlias = np.ndarray[Any, np.dtype[np.uint8]]


@ffi.callback("void (VRImage*, void*)")
def alloc_callback_numpy(image: ffi.CData, self: ffi.CData) -> None:
    assert image.scalar_type == 0, f"non uint8 images not yet supported"
    if image.channels == 3:
        arr = np.empty((image.height, image.width, image.channels), dtype=np.uint8)
        ai = arr.__array_interface__
        address = ai["data"][0]
        assert (
            ai["strides"] is None
        ), f"{image.height}x{image.width}x{image.channels} images not yet supported"
        assert (
            image.stride == arr.dtype.itemsize * image.width * image.channels
        ), "unsupported image"
        memory = ffi.from_handle(self).memory
        image.data = ffi.cast("uint8_t *", address)
        image.user_data = ffi.NULL
        assert address not in memory, "programmer error"
        memory[address] = arr
    else:
        assert False, f"{image.channels} channels not yet supported"


@ffi.callback("void (VRImage*, void*)")
def free_callback_callback_numpy(image: ffi.CData, self: ffi.CData) -> None:
    address = int(ffi.cast("uintptr_t", image.data))
    assert address in ffi.from_handle(self).memory


class VideoReaderNumpy(VideoReaderBase[Image]):
    def __init__(
        self,
        path: str,
        arguments: list[str] = [],
        extras: list[str] = [],
        log_callback: LogCallback = None,
    ):
        self.memory: dict[int, Image] = {}
        super().__init__(
            path,
            arguments,
            extras,
            alloc_callback_numpy,
            free_callback_callback_numpy,
            log_callback,
        )

    def __iter__(self) -> Iterator[tuple[Image, int, float]]:
        for image, *other in super().__iter__():
            address = int(ffi.cast("uintptr_t", image.data))
            yield (self.memory.pop(address), *other)

    def __del__(self):
        if self.memory:
            print(f"something went wrong when using {self}")
        self.memory.clear()
