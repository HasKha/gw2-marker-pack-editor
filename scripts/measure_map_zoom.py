"""Measures how the game zooms its full map as it opens or closes, from a screen recording, and how far
our own map markers are from it.

The game draws the full map at the view MumbleLink describes and scales the whole picture about the
screen's centre while it opens and closes (map_scene::FullMapZoom). This finds that scale for each frame
of a recording: the settled map, zoomed about the centre, is matched against the frame by correlating
their edges, which holds up while the map is still fading in over the world. Then it fits a line to the
frames where the match is clear.

With --markers, it measures our markers' scale too, from where their glyphs are, and says how far ahead
of the game's picture they run: what FullMapZoom's delays are for. Give the centres, in the reference
frame, of markers drawn as light glyphs (red over 215, blue over 140: the numbered packs' cream) sitting
on map icons of another colour, so the game's picture cannot pass for them.

    uv run scripts/measure_map_zoom.py <video> --reference <frame> --frames <first> <last>
        [--scales <least> <most>] [--exclude x0 y0 x1 y1]... [--markers x y]...

--reference is a frame with the map fully open and still. --exclude, in the recording's pixels, leaves out
what does not move with the map: addon windows, the chat, the skill bar, and the markers. Frames are
numbered from 0; times are in seconds from --frames' first. Give --frames one opening or one closing.
"""

import argparse

import cv2
import numpy as np

# Worked at half of 1080p for the map: plenty for its edges, four times faster.
WORK_SIZE = (960, 540)
# Below this the map's match is the world's noise: the map is too faint.
CLEAR_MATCH = 0.13
# Below this a markers' match is noise: their glyphs are off screen or faded out.
CLEAR_MARKERS = 25.0
# How far around each marker's centre its glyph is looked for, in the recording's pixels.
GLYPH_REACH = 8


def read_frames(path, wanted):
    """Each wanted frame as half-size grey, and at full size its darkest channel; the reference whole."""
    capture = cv2.VideoCapture(path)
    grey, darkest, whole, index = {}, {}, {}, 0
    while True:
        ok, frame = capture.read()
        if not ok:
            break
        if index in wanted:
            small = cv2.resize(frame, WORK_SIZE, interpolation=cv2.INTER_AREA)
            grey[index] = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY).astype(np.float32)
            darkest[index] = frame.min(axis=2).astype(np.float32)
            whole[index] = frame
        index += 1
    return grey, darkest, whole, capture.get(cv2.CAP_PROP_FPS)


def edges(gray):
    gray = cv2.GaussianBlur(gray, (0, 0), 1.0)
    return cv2.magnitude(cv2.Sobel(gray, cv2.CV_32F, 1, 0), cv2.Sobel(gray, cv2.CV_32F, 0, 1))


def correlation(a, b, valid):
    a = a[valid] - a[valid].mean()
    b = b[valid] - b[valid].mean()
    return float((a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum() + 1e-9))


def zoom_about(scale, centre):
    return np.float32([[scale, 0, centre[0] * (1 - scale)], [0, scale, centre[1] * (1 - scale)]])


def fit(measured, label):
    """The line through (seconds, scale), and when it crosses 5: half way, where both are on screen."""
    if len(measured) < 3:
        print(f"{label}: too few clear frames to fit")
        return None
    times, values = np.array(measured).T
    rate, at_start = np.polyfit(times, values, 1)
    worst = np.abs(np.polyval([rate, at_start], times) - values).max()
    print(f"{label}: {rate:+.2f} per second over {len(measured)} frames, scale 1 at {(1 - at_start) / rate:.3f} s, "
          f"worst miss {worst:.2f}")
    return (5 - at_start) / rate, rate


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("video")
    parser.add_argument("--reference", type=int, required=True)
    parser.add_argument("--frames", type=int, nargs=2, required=True)
    parser.add_argument("--scales", type=float, nargs=2, default=(0.9, 14.0))
    parser.add_argument("--exclude", type=int, nargs=4, action="append", default=[])
    parser.add_argument("--markers", type=int, nargs=2, action="append", default=[])
    args = parser.parse_args()

    first, last = args.frames
    grey, darkest, whole, fps = read_frames(args.video, set(range(first, last + 1)) | {args.reference})
    full_size = (whole[args.reference].shape[1], whole[args.reference].shape[0])
    shrink = WORK_SIZE[0] / full_size[0]
    mask = np.ones((WORK_SIZE[1], WORK_SIZE[0]), np.float32)
    for x0, y0, x1, y1 in args.exclude:
        mask[int(y0 * shrink) : int(y1 * shrink), int(x0 * shrink) : int(x1 * shrink)] = 0
    scales = np.exp(np.linspace(np.log(args.scales[0]), np.log(args.scales[1]), 300))

    # The settled map's edges at every scale tried, the same for every frame.
    centre = ((WORK_SIZE[0] - 1) / 2, (WORK_SIZE[1] - 1) / 2)
    reference = grey[args.reference]
    ones = np.ones_like(reference)
    map_candidates = []
    for scale in scales:
        matrix = zoom_about(scale, centre)
        valid = (cv2.warpAffine(ones, matrix, WORK_SIZE) * mask) > 0
        map_candidates.append((scale, edges(cv2.warpAffine(reference, matrix, WORK_SIZE)), valid))

    # Our markers' glyphs at every scale, as pixel indices: the glyph, and a ring around it as thick
    # as its outline, which a light glyph stands out from.
    marker_candidates = []
    if args.markers:
        settled = whole[args.reference].astype(int)
        light = (settled[:, :, 2] > 215) & (settled[:, :, 0] > 140)
        glyph = np.zeros(light.shape, np.uint8)
        for x, y in args.markers:
            near = np.s_[y - GLYPH_REACH : y + GLYPH_REACH + 1, x - GLYPH_REACH : x + GLYPH_REACH + 1]
            glyph[near] = light[near]
        full_centre = ((full_size[0] - 1) / 2, (full_size[1] - 1) / 2)
        for scale in scales:
            zoomed = cv2.warpAffine(glyph, zoom_about(scale, full_centre), full_size, flags=cv2.INTER_NEAREST)
            if zoomed.sum() < 30:
                continue
            reach = max(1, round(1.2 * scale))
            ring = cv2.dilate(zoomed, np.ones((2 * reach + 1, 2 * reach + 1), np.uint8)) - zoomed
            marker_candidates.append((scale, np.nonzero(zoomed), np.nonzero(ring)))

    game, ours = [], []
    for index in range(first, last + 1):
        seconds = (index - first) / fps
        frame_edges = edges(grey[index])
        match, scale = max((correlation(e, frame_edges, v), s) for s, e, v in map_candidates)
        line = f"frame {index:4d}  {seconds:6.3f} s  map {scale:7.3f} (match {match:.2f})"
        if match >= CLEAR_MATCH and 1.3 < scale < 11:
            game.append((seconds, scale))
        if marker_candidates:
            frame = darkest[index]
            contrast, marker_scale = max((frame[g].mean() - frame[r].mean(), s) for s, g, r in marker_candidates)
            line += f"  markers {marker_scale:7.3f} (contrast {contrast:5.1f})"
            if contrast >= CLEAR_MARKERS and 1.3 < marker_scale < 11:
                ours.append((seconds, marker_scale))
        print(line)

    game_line = fit(game, "map")
    if marker_candidates:
        ours_line = fit(ours, "markers")
        if game_line and ours_line:
            opening = game_line[1] < 0
            lead = game_line[0] - ours_line[0]
            print(f"the markers run {lead * 1000:.1f} ms ahead of the map ({'opening' if opening else 'closing'})")


if __name__ == "__main__":
    main()
