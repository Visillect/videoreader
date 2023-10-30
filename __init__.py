#!/usr/bin/env python3
from __future__ import annotations
from typing import Callable, Iterator, NoReturn
from minimg import MinImg, ffi_new as minimg_ffi_new

from cffi import FFI

ffi = FFI()

header = """
typedef void (*videoreader_log)(char const*, int, void*);

int videoreader_create(
    struct videoreader**,
    char const* video_path,
    char const* argv[],
    int argc,
    char const* extras[],
    int extrasc,
    videoreader_log callback,
    void* userdata);

char const* videoreader_what(void);

void videoreader_delete(struct videoreader*);

int videoreader_next_frame(
    struct videoreader*,
    struct MinImg* dst_img,
    uint64_t* number,
    double* timestamp_s,
    unsigned char* extras[],
    unsigned int* extras_size,
    bool decode);

int videoreader_set(
    struct videoreader*,
    char const* argv[],
    int argc
);

int videoreader_size(struct videoreader*, uint64_t* count);

// writer
int videowriter_create(
    struct videowriter** writer,
    char const* video_path,
    struct MinImg const* frame_format,
    char const* argv[],
    int argc,
    bool,
    videoreader_log callback,
    void* userdata
);

void videowriter_delete(struct videowriter* reader);

int videowriter_push(
    struct videowriter* writer,
    struct MinImg const* img,
    double timestamp_s);

int videowriter_close(struct videowriter* writer);

void free(void *p);
"""
ffi.cdef(header)

path = "libvideoreader_c.so"
try:
    backend = ffi.dlopen(path)
except OSError as e:
    raise OSError(f'cant load dll "{path}", make sure it is compiled')


@ffi.callback("void(char const*, int, void*)")
def videoreader_log(message: ffi.CData, level: int, handler: ffi.CData):
    message = ffi.string(message).decode()
    ffi.from_handle(handler).log_callback(level, message)


FATAL = 0
ERROR = 1
WARNING = 2
INFO = 3
DEBUG = 4


def raise_error() -> NoReturn:
    raise ValueError(ffi.string(backend.videoreader_what()).decode())


class VideoReader:
    def __init__(
        self,
        path: str,
        arguments: list[str] = [],
        extras: list[str] = [],
        log_callback: Callable[[str, int], None] | None = None,
    ):
        handler = ffi.new("struct videoreader **")
        self.log_callback = log_callback
        self.frame_idx = 0

        argv_keepalive = [ffi.new("char[]", arg.encode()) for arg in arguments]
        extras_keepalive = [ffi.new("char[]", arg.encode()) for arg in extras]

        if (
            backend.videoreader_create(
                handler,
                str(path).encode("utf-8"),
                argv_keepalive,
                len(argv_keepalive),
                extras_keepalive,
                len(extras_keepalive),
                videoreader_log if log_callback else ffi.NULL,
                ffi.new_handle(self),
            )
            != 0
        ):
            raise_error()
        self._handler = ffi.gc(handler[0], backend.videoreader_delete)

    def __iter__(
        self, decode: bool = True
    ) -> Iterator[tuple[MinImg, int, float]]:
        number = ffi.new("uint64_t *")
        timestamp = ffi.new("double *")
        extras_p = minimg_ffi_new("unsigned char **")
        extras_size = minimg_ffi_new("unsigned int *")
        while True:
            image = minimg_ffi_new("MinImg *")
            ret = backend.videoreader_next_frame(
                self._handler,
                ffi.cast("struct MinImg *", image),
                number,
                timestamp,
                extras_p,
                extras_size,
                decode,
            )
            self.frame_idx += 1
            if ret == 0:
                img = MinImg(image)
                assert img._mi.is_owner
                extras = extras_p[0]
                if extras == ffi.NULL:
                    yield img, number[0], timestamp[0]
                else:
                    from msgpack import unpackb

                    try:
                        info = unpackb(ffi.buffer(extras, extras_size[0]))
                    finally:
                        backend.free(extras)
                    yield img, number[0], timestamp[0], info
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

    def seek_get_img(self, seek_idx: int) -> MinImg | None:
        self.seek(seek_idx)
        for frame, *_ in self:
            break
        else:
            return None
        return frame

    def set(self, arguments: list[str]) -> None:
        argv_keepalive = [ffi.new("char[]", arg.encode()) for arg in arguments]
        if backend.videoreader_set(
            self._handler, argv_keepalive, len(argv_keepalive)
        ):
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
        image = minimg_ffi_new(
            "MinImg *",
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
                ffi.cast("struct MinImg *", image),
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

    def push(self, image: MinImg, timestamp: float) -> bool:
        ret: int = backend.videowriter_push(
            self._handler, ffi.cast("struct MinImg *", image[:]._mi), timestamp
        )
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
    backend.videoreader_create(
        handler, uri.encode("utf-8"), [], 0, ffi.NULL, ffi.NULL
    )
    n_frames = ffi.new("uint64_t *")
    backend.videoreader_size(handler[0], n_frames)
    backend.videoreader_delete(handler[0])
    return n_frames[0]


if __name__ == "__main__":
    from pathlib import Path
    from argparse import ArgumentParser

    parser = ArgumentParser()
    default = Path(__file__).parent / "test" / "big_buck_bunny_480p_1mb.mp4"
    parser.add_argument("uri", nargs="?", default=default)
    parser.add_argument("out", nargs="?")
    args = parser.parse_args()
    reader = VideoReader(args.uri, extras=["pkt_dts", "pts"])
    if args.out:
        writer = VideoWriter(
            args.out, 640, 480, arguments=[], log_callback=print, realtime=True
        )
    delay = 0.0
    for img, number, timestamp, extras in reader:
        print(img, number, timestamp, extras)
        if args.out:
            from random import random

            delay += 0.0  # random() * 2
            ret = writer.push(img, timestamp * (1.0) + delay)
            if not ret:
                print(f"skipping frame {number}!")
