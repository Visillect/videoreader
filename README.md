# VideoReader
c++/c/python wrapper for

* [FFmpeg](https://ffmpeg.org/)
* [Galaxy driver](https://en.daheng-imaging.com) used in Daheng cameras
* [pylon driver](https://www.baslerweb.com/) used in Basler cameras


## Installation

### FFmpeg in Debian based distributions

`apt install libavcodec-dev libavdevice-dev libavformat-dev libswscale-dev`.

Set `FFMPEG_DIR` environment variable when setting custom ffmpeg directory. See [Findffmpeg.cmake](cmake/Findffmpeg.cmake)

### Daheng cameras

Download [galaxy SDK](https://en.daheng-imaging.com/list-58-1.html). Then set `GALAXY_DIR` environment variable to the SDK directory.
See [Findgalaxy.cmake](cmake/Findgalaxy.cmake) for exact logic of finding galaxy SDK.

### Basler cameras

Download [pylon SDK](https://www.baslerweb.com/). Then set `PYLON_DIR` environment variable to the SDK directory. See [Findpylon.cmake](cmake/Findpylon.cmake) for exact logic of finding pylon SDK.

### iDatum cameras (contrastech)

Download [iDatum SDK](https://www.visiondatum.com/upfile/onlinedoc/LEO_Area_USB_EN.html). Then set `IDATUM_DIR` environment variable to the SDK directory. See [Findidatum.cmake](cmake/Findidatum.cmake) for exact logic of finding iDatum SDK.


### pip

`pip install git+https://github.com/Visillect/videoreader`

When the SDKs aren't in default locations, the user can specify directories explicitly:
`FFMPEG_DIR=... GALAXY_DIR=... PYLON_DIR=... IDATUM_DIR=... pip install git+https://github.com/Visillect/videoreader`


## Examples:

### numpy backend

```python
from videoreader.numpy import VideoReaderNumpy as VideoReader

# uri = 'galaxy://192.168.0.13'
uri = 'test/big_buck_bunny_480p_1mb.mp4'
reader = VideoReader(uri, extras=["pkt_dts", "pts"])
for image, number, timestamp, extras in reader:
    print(f"{fmt_image(image)} {number}, {timestamp:g}, {extras}")

```