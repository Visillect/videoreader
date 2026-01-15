# VideoReader
c++/c/python wrapper for

* [FFmpeg](https://ffmpeg.org/) library
* [Galaxy driver](https://en.daheng-imaging.com) used in Daheng cameras
* [pylon driver](https://www.baslerweb.com/) used in Basler cameras
* [iDatum driver](https://www.visiondatum.com) used in contrastech and visiondatum cameras


## Installation

### FFmpeg in Debian based distributions

`apt install libavcodec-dev libavdevice-dev libavformat-dev libswscale-dev`.

Set `FFMPEG_DIR` environment variable when setting custom ffmpeg directory. See [Findffmpeg.cmake](cmake/Findffmpeg.cmake)

```python
uri = 'test/big_buck_bunny_480p_1mb.mp4'
uri = 'https://example.com/hls/stream.m3u8'
```

See all supported protocols by running
```bash
ffmpeg -protocols
```

### Daheng cameras

Download [galaxy SDK](https://en.daheng-imaging.com/list-58-1.html). Then set `GALAXY_DIR` environment variable to the SDK directory.
See [Findgalaxy.cmake](cmake/Findgalaxy.cmake) for exact logic of finding galaxy SDK.

```python
uri = 'galaxy://192.168.0.13'
```

### Basler cameras

Download [pylon SDK](https://www.baslerweb.com/). Then set `PYLON_DIR` environment variable to the SDK directory. See [Findpylon.cmake](cmake/Findpylon.cmake) for exact logic of finding pylon SDK.

```python
uri = 'pylon://'
```

### iDatum cameras (contrastech)

Download [iDatum SDK](https://www.visiondatum.com/upfile/onlinedoc/LEO_Area_USB_EN.html). Then set `IDATUM_DIR` environment variable to the SDK directory. See [Findidatum.cmake](cmake/Findidatum.cmake) for exact logic of finding iDatum SDK.

```python
uri = 'idatum://192.168.0.13'
uri = 'idatum://usb_name'
```

### Installation using pip

`pip install -v git+https://github.com/Visillect/videoreader`

When the SDKs aren't in default locations, the user can specify directories explicitly:
`FFMPEG_DIR=... GALAXY_DIR=... PYLON_DIR=... IDATUM_DIR=... pip install git+https://github.com/Visillect/videoreader`


## Examples:

### `VideoReader` with numpy backend

```python
from videoreader.numpy import VideoReaderNumpy as VideoReader

uri = 'test/big_buck_bunny_480p_1mb.mp4'
reader = VideoReader(uri, extras=["pkt_dts", "pts"])
for image, number, timestamp, extras in reader:
    print(f"{image.shape} {image.dtype} {number}, {timestamp:g}, {extras}")

```

### `VideoWriter` with numpy backend

```python
from videoreader.numpy import VideoWriterNumpy as VideoWriter

writer = VideoWriter(
    'out.mkv', 640, 480, arguments=[], log_callback=print, realtime=True
)

for image, number, timestamp, extras in reader:
    ret = writer.push(image, timestamp)
    if not ret:
        print(f"skipping frame {number}!")
```
