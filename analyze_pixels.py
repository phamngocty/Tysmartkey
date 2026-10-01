import base64

with open("scratch_check_bmp.py", "r", encoding="utf-8") as f:
    code = f.read()

# Lấy b64_str
start_idx = code.find('"""data:image/bmp;base64,') + len('"""data:image/bmp;base64,')
end_idx = code.find('"""', start_idx)
b64_data = code[start_idx:end_idx].strip()

bmp = base64.b64decode(b64_data)
header_offset = int.from_bytes(bmp[10:14], 'little')
pixel_data = bmp[header_offset:]

# In 500 bytes đầu tiên của pixel_data dưới dạng repr
with open("pixel_analysis.txt", "w", encoding="utf-8") as out:
    out.write(f"Total pixel bytes: {len(pixel_data)}\n")
    # Lấy các byte khác 0
    nz_indices = [i for i, b in enumerate(pixel_data) if b != 0]
    out.write(f"Non zero range: min {min(nz_indices) if nz_indices else 'none'} to max {max(nz_indices) if nz_indices else 'none'}\n")
    
    # Đoạn dữ liệu có pixel
    if nz_indices:
        start = max(0, min(nz_indices) - 10)
        end = min(len(pixel_data), max(nz_indices) + 20)
        sample = pixel_data[start:end]
        out.write(f"Sample slice [{start}:{end}]:\n")
        out.write(f"Hex: {sample.hex(' ')}\n")
        out.write(f"Raw repr: {repr(sample)}\n")
        try:
            out.write(f"UTF-16LE: {sample.decode('utf-16le', errors='replace')}\n")
        except Exception as e:
            out.write(f"UTF-16 error: {e}\n")
print("Done analysis, written to pixel_analysis.txt")
