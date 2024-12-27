from pathlib import Path
from argparse import ArgumentParser
from . import VideoWriter


def main():
    parser = ArgumentParser()
    default = Path(__file__).parents[2] / "test" / "big_buck_bunny_480p_1mb.mp4"
    parser.add_argument("uri", nargs="?", default=default)
    parser.add_argument(
        "--backend", choices=["base", "numpy", "minimg"], default="base"
    )
    parser.add_argument("--out")
    args = parser.parse_args()

    fmt_image = repr
    match args.backend:
        case "base":
            from . import VideoReaderBase as VideoReader

            fmt_image = lambda image: f"{image.width}x{image.height}x{image.channels}"

        case "numpy":
            from .numpy import VideoReaderNumpy as VideoReader

            fmt_image = lambda image: f"{image.shape}:{image.dtype}"

        case "minimg":
            from .minimg import VideoReaderMinImg as VideoReader

        case unknown:
            assert False, unknown

    reader = VideoReader(args.uri, extras=["pkt_dts", "pts"])
    if args.out:
        writer = VideoWriter(
            args.out, 640, 480, arguments=[], log_callback=print, realtime=True
        )
    delay = 0.0

    from minimg.view.view_client import connect

    c = connect(__file__)

    for image, number, timestamp, extras in reader:
        c.log(f"{number} @ timestamp", image)
        print(f"{fmt_image(image)} {number}, {timestamp:g}, {extras}")
        if args.out:
            from random import random

            delay += 0.02  # random() * 2
            ret = writer.push(image, timestamp * (1.0) + delay)
            if not ret:
                print(f"skipping frame {number}!")


main()
