from __future__ import annotations
import sys
from pathlib import Path
from cffi import FFI


ffibuilder = FFI()
ffibuilder.set_source("videoreader._videoreader", None)

ffibuilder.cdef(
    """
typedef struct {
  int32_t height;
  int32_t width;
  int32_t channels;
  int32_t scalar_type;
  int32_t stride;
  uint8_t *data;
  void *user_data;
} VRImage;
typedef void (*videoreader_log_t)(char const*, int, void*);
typedef void (*videoreader_alloc_t)(VRImage*,void*);

int videoreader_create(
    struct videoreader**,
    char const* video_path,
    char const* argv[],
    int argc,
    char const* extras[],
    int extrasc,
    videoreader_alloc_t alloc_callback,
    videoreader_alloc_t free_callback,
    videoreader_log_t callback,
    void* userdata);

char const* videoreader_what(void);

void videoreader_delete(struct videoreader*);

int videoreader_next_frame(
    struct videoreader*,
    VRImage* dst_img,
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

int videoreader_stop(struct videoreader*);

int videoreader_size(struct videoreader*, uint64_t* count);

// writer
int videowriter_create(
    struct videowriter** writer,
    char const* video_path,
    VRImage const* frame_format,
    char const* argv[],
    int argc,
    bool,
    videoreader_log_t callback,
    void* userdata
);

void videowriter_delete(struct videowriter* reader);

int videowriter_push(
    struct videowriter* writer,
    VRImage const* img,
    double timestamp_s);

int videowriter_close(struct videowriter* writer);

void free(void *p);  // for cleaning up "extras"
"""
)

if __name__ == "__main__":
    py_file = Path(ffibuilder.compile(verbose=True))
    libname = (
        "libvideoreader_c.so"
        if sys.platform != "win32"
        else "videoreader_c.dll"
    )
    with py_file.open("a", encoding="utf-8") as out:
        print(
            f"""
CData = ffi.CData
from pathlib import Path

def open_backend():
    path = str(Path(__file__).parent / {libname!r})
    return ffi.dlopen(path)""",
            file=out,
        )
