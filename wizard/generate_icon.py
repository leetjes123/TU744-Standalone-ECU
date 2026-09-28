"""Generate a high-quality multi-resolution icon for Tuning Wizard."""
from PIL import Image, ImageDraw, ImageFont
import struct
import os
import io

def create_icon_image(size):
    """Create a single icon image at the given size."""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    margin = max(1, size // 32)
    radius = max(2, size // 6)

    bg_color = (26, 26, 33, 255)
    accent = (230, 179, 51, 255)

    draw.rounded_rectangle(
        [margin, margin, size - margin - 1, size - margin - 1],
        radius=radius,
        fill=bg_color,
        outline=accent,
        width=max(1, size // 32)
    )

    font_size = int(size * 0.48)
    font = None
    try:
        for font_name in ['C:/Windows/Fonts/impact.ttf', 'C:/Windows/Fonts/arialbd.ttf']:
            if os.path.exists(font_name):
                font = ImageFont.truetype(font_name, font_size)
                break
    except Exception:
        pass
    if font is None:
        font = ImageFont.load_default()

    text = "TW"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = (size - tw) // 2
    y = (size - th) // 2 - bbox[1]

    draw.text((x, y), text, fill=accent, font=font)

    stripe_h = max(1, size // 16)
    stripe_y = size - margin - radius // 2 - stripe_h
    stripe_margin = margin + radius // 2
    draw.rectangle(
        [stripe_margin, stripe_y, size - stripe_margin - 1, stripe_y + stripe_h - 1],
        fill=(200, 50, 50, 200)
    )

    return img

def write_ico_manual(images, path):
    """Write ICO file manually to ensure all sizes are included."""
    # ICO format:
    # Header: 6 bytes (reserved=0, type=1, count=N)
    # Directory entries: 16 bytes each
    # Image data: PNG-encoded for each size

    count = len(images)
    header = struct.pack('<HHH', 0, 1, count)

    dir_entries = []
    image_data = []
    offset = 6 + 16 * count  # after header + all directory entries

    for img in images:
        w, h = img.size
        # Encode as PNG for best quality (supported in Vista+)
        buf = io.BytesIO()
        img.save(buf, format='PNG')
        data = buf.getvalue()

        # Width/height: 0 means 256
        bw = 0 if w >= 256 else w
        bh = 0 if h >= 256 else h

        entry = struct.pack('<BBBBHHII',
            bw, bh,
            0,  # color count
            0,  # reserved
            1,  # color planes
            32, # bits per pixel
            len(data),
            offset
        )
        dir_entries.append(entry)
        image_data.append(data)
        offset += len(data)

    with open(path, 'wb') as f:
        f.write(header)
        for entry in dir_entries:
            f.write(entry)
        for data in image_data:
            f.write(data)

def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [create_icon_image(s) for s in sizes]

    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src', 'icon.ico')
    write_ico_manual(images, out_path)

    total = os.path.getsize(out_path)
    print(f"Generated icon: {out_path}")
    print(f"  Sizes: {sizes}")
    print(f"  File size: {total:,} bytes")

if __name__ == '__main__':
    main()
