from __future__ import annotations
from typing import TypeAlias, Any
import numpy as np
from . import VideoReaderBase, VideoWriterBase, ffi, LogCallback, CData
from collections.abc import Iterator


Image: TypeAlias = np.ndarray[Any, np.dtype[np.uint8]]


@ffi.callback("void (VRImage*, void*)")
def alloc_callback_numpy(image: CData, self: CData) -> None:
    assert image.scalar_type == 0, f"non uint8 images not yet supported"
    assert image.channels >= 1
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


@ffi.callback("void (VRImage*, void*)")
def free_callback_callback_numpy(image: CData, self: CData) -> None:
    address = int(ffi.cast("uintptr_t", image.data))
    assert address in ffi.from_handle(self).memory


class VideoReaderNumpy(VideoReaderBase):
    def __init__(
        self,
        path: str,
        arguments: list[str] = [],
        extras: list[str] = [],
        log_callback: LogCallback = None,
    ) -> None:
        self.memory: dict[int, Image] = {}
        super().__init__(
            path,
            arguments,
            extras,
            alloc_callback_numpy,
            free_callback_callback_numpy,
            log_callback,
        )

    def __iter__(self) -> "Iterator[tuple[Image, *tuple[int | float, ...]]]":
        for image, *other in self._iter():
            address = int(ffi.cast("uintptr_t", image.data))
            yield (self.memory.pop(address), *other)

    def __del__(self) -> None:
        if self.memory:
            print(f"{len(self.memory)} items were left allocated in {self}")
        self.memory.clear()


class VideoWriterNumpy(VideoWriterBase):
    def push(self, image: Image, timestamp: float) -> bool:
        height, width, channels = image.shape
        if image.dtype != np.uint8:
            raise ValueError(
                f"Only uint8 image are supported, not {image.dtype}."
            )
        vr_image = ffi.new(
            "VRImage *",
            {
                "width": width,
                "height": height,
                "channels": channels,
                "scalar_type": 0,
                "stride": width * channels,
                "data": ffi.cast("uint8_t*", image.ctypes.data),
            },
        )
        return self._push(vr_image, timestamp)
