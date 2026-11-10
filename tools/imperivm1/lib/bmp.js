// Reads uncompressed BMP files (8, 16, 24 or 32 bits) and writes 24-bit ones: enough for the adventure preview, which the
// game wants as a 440x360 24-bit BMP next to the .bfhp.
'use strict';

// Returns {width, height, rgb: Buffer (3 bytes per pixel, top row first)}.
function readBmp(buf) {
	if (buf.toString('latin1', 0, 2) !== 'BM') throw new Error('not a BMP');
	const dataAt = buf.readUInt32LE(10);
	const headerSize = buf.readUInt32LE(14);
	const width = buf.readInt32LE(18);
	const rawHeight = buf.readInt32LE(22);
	const bpp = buf.readUInt16LE(28);
	const compression = buf.readUInt32LE(30);
	const height = Math.abs(rawHeight);
	let masks = null;
	if (compression === 3) masks = [buf.readUInt32LE(14 + headerSize), buf.readUInt32LE(18 + headerSize), buf.readUInt32LE(22 + headerSize)];
	else if (compression !== 0) throw new Error(`compressed BMP (${compression})`);
	if (bpp === 16 && !masks) masks = [0x7c00, 0x03e0, 0x001f];
	const stride = Math.ceil((width * bpp) / 32) * 4;
	const palette = bpp === 8 ? buf.subarray(14 + headerSize, dataAt) : null;
	const rgb = Buffer.alloc(width * height * 3);
	const channel = (v, mask) => {
		if (!mask) return 0;
		let shift = 0;
		while (!((mask >> shift) & 1)) shift++;
		const max = mask >>> shift;
		return Math.round((((v & mask) >>> shift) * 255) / max);
	};
	for (let y = 0; y < height; y++) {
		const row = dataAt + (rawHeight > 0 ? height - 1 - y : y) * stride;
		for (let x = 0; x < width; x++) {
			const o = (y * width + x) * 3;
			let r;
			let g;
			let b;
			if (bpp === 24 || bpp === 32) {
				const p = row + x * (bpp / 8);
				[b, g, r] = [buf[p], buf[p + 1], buf[p + 2]];
			} else if (bpp === 16) {
				const v = buf.readUInt16LE(row + x * 2);
				[r, g, b] = masks.map((m) => channel(v, m));
			} else if (bpp === 8) {
				const p = buf[row + x] * 4;
				[b, g, r] = [palette[p], palette[p + 1], palette[p + 2]];
			} else {
				throw new Error(`${bpp}-bit BMP`);
			}
			rgb[o] = r;
			rgb[o + 1] = g;
			rgb[o + 2] = b;
		}
	}
	return { width, height, rgb };
}

function writeBmp24(img) {
	const stride = Math.ceil((img.width * 3) / 4) * 4;
	const size = 54 + stride * img.height;
	const out = Buffer.alloc(size);
	out.write('BM', 0, 'latin1');
	out.writeUInt32LE(size, 2);
	out.writeUInt32LE(54, 10);
	out.writeUInt32LE(40, 14);
	out.writeInt32LE(img.width, 18);
	out.writeInt32LE(img.height, 22);
	out.writeUInt16LE(1, 26);
	out.writeUInt16LE(24, 28);
	out.writeUInt32LE(stride * img.height, 34);
	out.writeUInt32LE(2835, 38);
	out.writeUInt32LE(2835, 42);
	for (let y = 0; y < img.height; y++) {
		const row = 54 + (img.height - 1 - y) * stride;
		for (let x = 0; x < img.width; x++) {
			const s = (y * img.width + x) * 3;
			out[row + x * 3] = img.rgb[s + 2];
			out[row + x * 3 + 1] = img.rgb[s + 1];
			out[row + x * 3 + 2] = img.rgb[s];
		}
	}
	return out;
}

// Scales the image to cover width x height (keeping its proportions) and crops the middle. Each output pixel averages the
// source pixels it covers.
function coverResize(img, width, height) {
	const scale = Math.max(width / img.width, height / img.height);
	const srcW = width / scale;
	const srcH = height / scale;
	const x0 = (img.width - srcW) / 2;
	const y0 = (img.height - srcH) / 2;
	const rgb = Buffer.alloc(width * height * 3);
	for (let y = 0; y < height; y++) {
		const sy0 = y0 + y / scale;
		const sy1 = sy0 + 1 / scale;
		for (let x = 0; x < width; x++) {
			const sx0 = x0 + x / scale;
			const sx1 = sx0 + 1 / scale;
			const sum = [0, 0, 0];
			let n = 0;
			for (let sy = Math.floor(sy0); sy < Math.max(Math.ceil(sy1), Math.floor(sy0) + 1); sy++) {
				for (let sx = Math.floor(sx0); sx < Math.max(Math.ceil(sx1), Math.floor(sx0) + 1); sx++) {
					const cx = Math.min(img.width - 1, Math.max(0, sx));
					const cy = Math.min(img.height - 1, Math.max(0, sy));
					const p = (cy * img.width + cx) * 3;
					sum[0] += img.rgb[p];
					sum[1] += img.rgb[p + 1];
					sum[2] += img.rgb[p + 2];
					n++;
				}
			}
			const o = (y * width + x) * 3;
			rgb[o] = Math.round(sum[0] / n);
			rgb[o + 1] = Math.round(sum[1] / n);
			rgb[o + 2] = Math.round(sum[2] / n);
		}
	}
	return { width, height, rgb };
}

// Paints the transparent key colour (pure green in the game's interface pictures) with `rgb`.
function fillKey(img, rgb) {
	const out = Buffer.from(img.rgb);
	for (let p = 0; p < out.length; p += 3) {
		if (out[p] < 64 && out[p + 1] > 192 && out[p + 2] < 64) {
			out[p] = rgb[0];
			out[p + 1] = rgb[1];
			out[p + 2] = rgb[2];
		}
	}
	return { width: img.width, height: img.height, rgb: out };
}

module.exports = { readBmp, writeBmp24, coverResize, fillKey };
