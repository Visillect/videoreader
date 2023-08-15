#!/usr/bin/env python3
from __future__ import annotations
from typing import Callable, Iterator
from minimg import MinImg, ffi_new as minimg_ffi_new

from cffi import FFI

ffi = FFI()

header = """
typedef void (*videoreader_log)(char*, int, void*);

int videoreader_create(
    struct videoreader**,
    char const* video_path,
    char const* argv[],
    int argc,
    videoreader_log callback,
    void* userdata);

void videoreader_delete(struct videoreader*);

int videoreader_next_frame(
    struct videoreader*,
    struct MinImg* dst_img,
    uint64_t* number,
    double* timestamp_s,
    bool decode);

int videoreader_size(struct videoreader*, uint64_t* count);
"""
ffi.cdef(header)

path = './libvideoreader_c.so'
try:
    backend = ffi.dlopen(path)
except OSError as e:
    raise OSError(f'cant load dll "{path}", make sure it is compiled')


@ffi.callback("void(char*, int, void*)")
def videoreader_log(message, level, handler):
    message = ffi.string(message).decode()
    ffi.from_handle(handler).log_callback(level, message)


FATAL = 0
ERROR = 1
WARNING = 2
INFO = 3
DEBUG = 4


class VideoReader:
    def __init__(self,
                 path: str,
                 arguments: list[str] = [],
                 log_callback: Callable[[str, int], None] | None=None):
        handler = ffi.new("struct videoreader **")
        self.log_callback = log_callback
        self.frame_idx = 0

        argv_keepalive = [ffi.new("char[]", arg) for arg in arguments]
        backend.videoreader_create(
            handler,
            str(path).encode("utf-8"),
            argv_keepalive,
            len(argv_keepalive),
            videoreader_log if log_callback else ffi.NULL,
            ffi.new_handle(self),
        )
        self._handler = ffi.gc(handler[0], backend.videoreader_delete)

    def __iter__(self, decode:bool=True) -> Iterator[tuple[MinImg, int, float]]:
        number = ffi.new("uint64_t *")
        timestamp = ffi.new("double *")
        while True:
            image = minimg_ffi_new("MinImg *")
            ret = backend.videoreader_next_frame(
                self._handler,
                ffi.cast("struct MinImg *", image),
                number,
                timestamp,
                decode,
            )
            self.frame_idx += 1
            if ret != 0:
                return
            img = MinImg(image)
            assert img._mi.is_owner
            yield img, number[0], timestamp[0]

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


def videoreader_n_frames(uri: str) -> int:
    """
    Get number of frames in a file
    """
    handler = ffi.new("struct videoreader **")
    backend.videoreader_create(
        handler, uri.encode("utf-8"), [], 0,  ffi.NULL, ffi.NULL)
    n_frames = ffi.new("uint64_t *")
    backend.videoreader_size(handler[0], n_frames)
    backend.videoreader_delete(handler[0])
    return n_frames[0]


if __name__ == "__main__":
    from pathlib import Path
    from argparse import ArgumentParser
    parser = ArgumentParser()
    default = Path(__file__).parent / 'test' / 'big_buck_bunny_480p_1mb.mp4'
    parser.add_argument('uri', nargs='?', default=default)
    reader = VideoReader(parser.parse_args().uri)
    idx = 0
    for img, number, timestamp in reader:
        print(img, number, timestamp)
        idx += 1
        if idx > 10:
            break
