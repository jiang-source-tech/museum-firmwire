from pathlib import Path

from PIL import Image, ImageSequence


BOOT_GIF = Path("main/assets/images/boot.gif")


def test_boot_gif_has_transparent_background_and_visible_logo_pixels():
    image = Image.open(BOOT_GIF)
    frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(image)]

    assert frames, "boot.gif must contain at least one frame"
    assert len(frames) >= 2, "boot.gif should remain animated"

    width, height = frames[0].size
    corner_points = [
        (0, 0),
        (width - 1, 0),
        (0, height - 1),
        (width - 1, height - 1),
    ]

    for frame in frames[: min(3, len(frames))]:
        for point in corner_points:
            assert frame.getpixel(point)[3] == 0

    visible_pixels = sum(
        1
        for frame in frames
        for pixel in frame.getdata()
        if pixel[3] > 0
    )
    total_pixels = len(frames) * width * height

    assert visible_pixels > 0
    assert visible_pixels < total_pixels * 0.75


def test_boot_gif_has_no_lower_white_rule_artifacts():
    image = Image.open(BOOT_GIF)
    frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(image)]

    assert frames, "boot.gif must contain at least one frame"

    for frame_index, frame in enumerate(frames):
        width, height = frame.size
        for y in range(height // 2, height):
            near_white_visible = 0
            for x in range(width):
                red, green, blue, alpha = frame.getpixel((x, y))
                if alpha > 0 and red >= 180 and green >= 180 and blue >= 180:
                    near_white_visible += 1
            assert near_white_visible < 64, (
                f"frame {frame_index} row {y} has a lower white rule artifact "
                f"({near_white_visible} visible near-white pixels)"
            )
