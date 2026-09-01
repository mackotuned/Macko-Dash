from pathlib import Path
from urllib.request import urlopen

from PIL import Image


SOURCE_URL = (
    "https://yt3.googleusercontent.com/"
    "vaeOBg4CtuZM47Ygv3-RRQO9jRIeYW0SFfgzTwDyc4v77OhIEDDB7tOYrOvFkA6MpnCMqPwVpA="
    "w1707-fcrop64=1,00005a57ffffa5a8-k-c0xffffffff-no-nd-rj"
)
WIDTH = 360
HEIGHT = 59
OUTPUT = Path(__file__).parents[2] / "main" / "huntertuned_logo.c"


def rgb565a8_bytes(image: Image.Image) -> bytes:
    output = bytearray()
    for red, green, blue in image.getdata():
        luminance = (red * 299 + green * 587 + blue * 114) // 1000
        alpha = 255 - luminance
        output.extend((0xFF, 0xFF, alpha))
    return bytes(output)


with urlopen(SOURCE_URL) as response:
    image = Image.open(response).convert("RGB")

image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
data = rgb565a8_bytes(image)
rows = [
    "  " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16]) + ","
    for offset in range(0, len(data), 16)
]

source = f'''#include "lvgl.h"

/* Official HunterTuned YouTube channel banner, retrieved 2026-08-31.
 * Source: {SOURCE_URL}
 */
#ifndef LV_ATTRIBUTE_IMG_HUNTERTUNED_LOGO
#define LV_ATTRIBUTE_IMG_HUNTERTUNED_LOGO
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_HUNTERTUNED_LOGO
uint8_t huntertuned_logo_map[] = {{
{chr(10).join(rows)}
}};

const lv_img_dsc_t huntertuned_logo = {{
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = {WIDTH},
    .header.h = {HEIGHT},
    .data_size = {WIDTH} * {HEIGHT} * (LV_COLOR_SIZE / 8 + 1),
    .data = huntertuned_logo_map,
}};
'''

OUTPUT.write_text(source, encoding="ascii")
print(f"Generated {OUTPUT} ({WIDTH}x{HEIGHT}, {len(data)} bytes)")