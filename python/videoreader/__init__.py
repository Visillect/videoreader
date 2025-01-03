#!/usr/bin/env python3
from __future__ import annotations
from typing import Callable, Iterator, NoReturn, TypeAlias, Generic, TypeVar
from ._videoreader import ffi
from pathlib import Path


path = str(Path(__file__).parent / "libvideoreader_c.so")
try:
    backend = ffi.dlopen(path)
except OSError as e:
    raise OSError(f'cant load dll "{path}", make sure it is compiled {e}')


@ffi.callback("void(char const*, int, void*)")
def videoreader_log(message: ffi.CData, level: int, handler: ffi.CData):
    message = ffi.string(message).decode()
    ffi.from_handle(handler).log_callback(level, message)


FATAL = 0
ERROR = 1
WARNING = 2
INFO = 3
DEBUG = 4

LogCallback: TypeAlias = Callable[[str, int], None] | None
AllocCallback: TypeAlias = Callable[[ffi.CData, ffi.CData], None] | None


def raise_error() -> NoReturn:
    raise ValueError(ffi.string(backend.videoreader_what()).decode())


T = TypeVar("T")


class VideoReaderBase(Generic[T]):
    def __init__(
        self,
        path: str,
        arguments: list[str] = [],
        extras: list[str] = [],
        alloc_callback: AllocCallback = ffi.NULL,
        free_callback: AllocCallback = ffi.NULL,
        log_callback: LogCallback = None,
    ):
        handler = ffi.new("struct videoreader **")
        self.log_callback = log_callback
        self.frame_idx = 0

        argv_keepalive = [ffi.new("char[]", arg.encode()) for arg in arguments]
        extras_keepalive = [ffi.new("char[]", arg.encode()) for arg in extras]
        self._self_handle = ffi.new_handle(self)

        if (
            backend.videoreader_create(
                handler,
                str(path).encode("utf-8"),
                argv_keepalive,
                len(argv_keepalive),
                extras_keepalive,
                len(extras_keepalive),
                alloc_callback,
                free_callback,
                backend.videoreader_log if log_callback else ffi.NULL,
                self._self_handle,
            )
            != 0
        ):
            raise_error()
        self._handler = ffi.gc(handler[0], backend.videoreader_delete)

    def __iter__(self, decode: bool = True) -> Iterator[tuple[T, int, float]]:
        number = ffi.new("uint64_t *")
        timestamp = ffi.new("double *")
        extras_p = ffi.new("unsigned char **")
        extras_size = ffi.new("unsigned int *")
        while True:
            image = ffi.new("VRImage *")
            ret = backend.videoreader_next_frame(
                self._handler,
                image,
                number,
                timestamp,
                extras_p,
                extras_size,
                decode,
            )
            self.frame_idx += 1
            if ret == 0:
                extras = extras_p[0]
                if extras == ffi.NULL:
                    yield image, number[0], timestamp[0]
                else:
                    from msgpack import unpackb

                    try:
                        info = unpackb(ffi.buffer(extras, extras_size[0]))
                    finally:
                        backend.free(extras)
                    yield image, number[0], timestamp[0], info
            elif ret == 1:  # empty frame
                return
            else:
                raise_error()

    def iter_fast(self):
        return self.__iter__(decode=False)

    def seek(self, seek_idx: int) -> None:
        if self.frame_idx == seek_idx:
            return
        for _ in self.iter_fast():
            if self.frame_idx == seek_idx:
                break
        if self.frame_idx != seek_idx:
            raise ValueError(f"No frame with index `{seek_idx}` was found.")

    def seek_get_img(self, seek_idx: int) -> T | None:
        self.seek(seek_idx)
        for frame, *_ in self:
            break
        else:
            return None
        return frame

    def set(self, arguments: list[str]) -> None:
        argv_keepalive = [ffi.new("char[]", arg.encode()) for arg in arguments]
        if backend.videoreader_set(self._handler, argv_keepalive, len(argv_keepalive)):
            raise_error()


class VideoWriter:
    def __init__(
        self,
        path: str,
        width: int,
        height: int,
        arguments: list[str] = [],
        realtime=False,
        log_callback: Callable[[str, int], None] | None = None,
    ):
        handler = ffi.new("struct videowriter **")
        self.log_callback = log_callback
        self.frame_idx = 0

        argv_keepalive = [ffi.new("char[]", arg.encode()) for arg in arguments]
        image = ffi.new(
            "VRImage *",
            {
                "width": width,
                "height": height,
                "channels": 1,
                "scalar_type": 0,
            },
        )
        if (
            backend.videowriter_create(
                handler,
                str(path).encode("utf-8"),
                image,
                argv_keepalive,
                len(argv_keepalive),
                realtime,
                videoreader_log if log_callback else ffi.NULL,
                ffi.new_handle(self),
            )
            != 0
        ):
            raise_error()
        self._handler = ffi.gc(handler[0], backend.videowriter_delete)

    def push(self, image: ffi.CData, timestamp: float) -> bool:
        ret: int = backend.videowriter_push(self._handler, image, timestamp)
        if ret < 0:
            raise_error()
        return ret == 0

    def close(self) -> None:
        if backend.videowriter_close(self._handler) != 0:
            raise_error()


def videoreader_n_frames(uri: str) -> int:
    """
    Get number of frames in a file
    """
    handler = ffi.new("struct videoreader **")
    backend.videoreader_create(handler, uri.encode("utf-8"), [], 0, ffi.NULL, ffi.NULL)
    n_frames = ffi.new("uint64_t *")
    backend.videoreader_size(handler[0], n_frames)
    backend.videoreader_delete(handler[0])
    return n_frames[0]
