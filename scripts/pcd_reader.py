"""Read binary or LZF binary_compressed PCD without changing source bytes.

Compressed fields are decoded to a disk mapping, then interleaved in bounded
blocks. The returned structured array is a read-only mapping in original point
order. Cache manifests record full byte-level checksums for every source field.
"""
import ctypes
import ctypes.util
import fcntl
import hashlib
import json
import mmap
import os
from pathlib import Path
import struct
import tempfile

import numpy as np


_CACHE_VERSION = 1
_LZF = None


def _header_and_layout(path):
    header = {}
    with path.open('rb') as stream:
        for _ in range(100):
            raw = stream.readline(65537)
            if not raw:
                raise ValueError('Missing PCD DATA header')
            if len(raw) > 65536:
                raise ValueError('PCD header line exceeds 64 KiB')
            try:
                line = raw.decode('ascii').strip()
            except UnicodeDecodeError as error:
                raise ValueError('PCD header must be ASCII') from error
            if not line or line.startswith('#'):
                continue
            key, *values = line.split()
            key = key.upper()
            if key in header:
                raise ValueError(f'Duplicate PCD header entry: {key}')
            header[key] = values
            if key == 'DATA':
                break
        else:
            raise ValueError('Missing PCD DATA header')
        offset = stream.tell()
    for key in ('FIELDS', 'SIZE', 'TYPE', 'POINTS', 'DATA'):
        if not header.get(key):
            raise ValueError(f'Missing PCD {key} header')
    fields = header['FIELDS']
    try:
        sizes = list(map(int, header['SIZE']))
        counts = list(map(int, header.get('COUNT', ['1'] * len(fields))))
        points = int(header['POINTS'][0])
    except ValueError as error:
        raise ValueError('Invalid numeric PCD field metadata') from error
    kinds = [kind.upper() for kind in header['TYPE']]
    if len({len(fields), len(sizes), len(counts), len(kinds)}) != 1:
        raise ValueError('PCD field metadata lengths differ')
    if len(header['POINTS']) != 1 or points <= 0:
        raise ValueError('PCD POINTS must be positive')
    if 'WIDTH' in header and 'HEIGHT' in header:
        if int(header['WIDTH'][0]) * int(header['HEIGHT'][0]) != points:
            raise ValueError('PCD WIDTH * HEIGHT does not match POINTS')
    layout = []
    names = set()
    for index, (name, size, count, kind) in enumerate(zip(fields, sizes, counts, kinds)):
        if count < 1 or kind not in ('F', 'I', 'U'):
            raise ValueError(f'Invalid PCD COUNT/TYPE for {name}')
        if size not in ((4, 8) if kind == 'F' else (1, 2, 4, 8)):
            raise ValueError(f'Unsupported PCD SIZE/TYPE for {name}: {size}/{kind}')
        # Repeated padding fields are allowed by PCD; retain their bytes too.
        array_name = f'_padding_{index}' if name == '_' else name
        if array_name in names:
            raise ValueError(f'Duplicate PCD field: {array_name}')
        names.add(array_name)
        scalar = '<' + {'F': 'f', 'U': 'u', 'I': 'i'}[kind] + str(size)
        layout.append((array_name, scalar, (count,)) if count > 1 else (array_name, scalar))
    dtype = np.dtype(layout)
    if header['DATA'] not in (['binary'], ['binary_compressed']):
        raise ValueError('Supported PCD encodings are binary and binary_compressed')
    return header, offset, dtype, points


def _signature(path, header, offset, dtype, points):
    info = path.stat()
    return {'version': _CACHE_VERSION, 'source': str(path), 'size_bytes': info.st_size,
            'mtime_ns': info.st_mtime_ns, 'ctime_ns': info.st_ctime_ns,
            'device': info.st_dev, 'inode': info.st_ino, 'payload_offset': offset,
            'fields': header['FIELDS'], 'sizes': header['SIZE'], 'types': header['TYPE'],
            'counts': header.get('COUNT', ['1'] * len(header['FIELDS'])),
            'points': points, 'point_bytes': dtype.itemsize}


def _lzf_function():
    global _LZF
    if _LZF is not None:
        return _LZF[1]
    candidates = ((ctypes.util.find_library('lzf'), 'lzf_decompress'),
                  ('liblzf.so.1', 'lzf_decompress'),
                  (ctypes.util.find_library('pcl_io'), '_ZN3pcl13lzfDecompressEPKvjPvj'),
                  ('libpcl_io.so', '_ZN3pcl13lzfDecompressEPKvjPvj'))
    for library_name, symbol in candidates:
        if not library_name:
            continue
        try:
            library = ctypes.CDLL(library_name, use_errno=True)
            function = getattr(library, symbol)
        except (OSError, AttributeError):
            continue
        function.argtypes = (ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint)
        function.restype = ctypes.c_uint
        _LZF = (library, function)  # Keep the dynamic library alive with its function.
        return function
    raise RuntimeError('LZF decoder unavailable: install liblzf or Point Cloud Library libpcl_io')


def _drop_pages(mapping):
    if hasattr(mapping._mmap, 'madvise') and hasattr(mmap, 'MADV_DONTNEED'):
        mapping._mmap.madvise(mmap.MADV_DONTNEED)


def _build_cache(path, payload_offset, compressed_bytes, unpacked_bytes,
                 dtype, points, output_path, temporary_dir):
    """Decode SoA and interleave raw bytes; never assign/canonicalize float NaNs."""
    source = soa = target = None
    try:
        source = np.memmap(path, mode='r', dtype='u1', offset=payload_offset,
                           shape=(compressed_bytes,))
        soa = np.memmap(temporary_dir / 'decoded-field-major.bin', mode='w+',
                        dtype='u1', shape=(unpacked_bytes,))
        actual = _lzf_function()(ctypes.c_void_p(source.ctypes.data), compressed_bytes,
                                 ctypes.c_void_p(soa.ctypes.data), unpacked_bytes)
        if actual != unpacked_bytes:
            raise ValueError(f'Invalid LZF payload: decoded {actual}, expected {unpacked_bytes} bytes')
        source._mmap.close()
        source = None
        soa.flush()
        _drop_pages(soa)
        target = np.memmap(output_path, mode='w+', dtype='u1', shape=(points, dtype.itemsize))
        fields = []
        offset = 0
        for name in dtype.names:
            field_dtype, interleaved_offset = dtype.fields[name]
            field_bytes = field_dtype.itemsize
            values = np.ndarray((points, field_bytes), dtype='u1', buffer=soa, offset=offset)
            fields.append((name, values, interleaved_offset, field_bytes, hashlib.sha256(), hashlib.sha256()))
            offset += points * field_bytes
        block_points = max(1, (8 * 1024 * 1024) // dtype.itemsize)
        for index, start in enumerate(range(0, points, block_points)):
            end = min(points, start + block_points)
            for _name, values, destination, field_bytes, source_hash, result_hash in fields:
                source_bytes = values[start:end]
                result_bytes = target[start:end, destination:destination + field_bytes]
                result_bytes[:] = source_bytes
                source_hash.update(source_bytes)
                # The bounded copy turns a strided field into its original byte order.
                result_hash.update(result_bytes.tobytes())
            if (index + 1) % 16 == 0:
                target.flush()
                _drop_pages(target)
                _drop_pages(soa)
        checksums = {}
        for name, _values, _destination, _field_bytes, source_hash, result_hash in fields:
            if source_hash.digest() != result_hash.digest():
                raise RuntimeError(f'Byte-level field verification failed: {name}')
            checksums[name] = source_hash.hexdigest()
        target.flush()
        return checksums
    finally:
        # NumPy views do not export Python buffer handles, so these mappings can
        # be closed deterministically before temporary files are removed.
        for mapping in (source, soa, target):
            if mapping is not None:
                mapping._mmap.close()


def read_pcd(path, cache_dir=None):
    """Return (read-only structured memmap, header) preserving every field/point.

    Binary PCD is mapped directly. Compressed PCD uses an atomic, locked cache
    under cache_dir (default /tmp/chaos-pcd-cache), never beside the source.
    Cache keys include source identity, timestamps, size, and complete layout.
    """
    path = Path(path).expanduser().resolve()
    header, offset, dtype, points = _header_and_layout(path)
    expected = points * dtype.itemsize
    signature = _signature(path, header, offset, dtype, points)
    if header['DATA'] == ['binary']:
        if signature['size_bytes'] - offset != expected:
            raise ValueError('PCD size does not match POINTS and field layout')
        return np.memmap(path, mode='r', offset=offset, dtype=dtype, shape=(points,)), header
    with path.open('rb') as stream:
        stream.seek(offset)
        sizes = stream.read(8)
    if len(sizes) != 8:
        raise ValueError('Truncated binary_compressed PCD size prefix')
    compressed_bytes, unpacked_bytes = struct.unpack('<II', sizes)
    if (compressed_bytes == 0 or unpacked_bytes != expected or
            signature['size_bytes'] - offset - 8 != compressed_bytes):
        raise ValueError('Compressed PCD size prefix does not match file, POINTS, and field layout')
    cache_dir = Path(cache_dir or Path(tempfile.gettempdir()) / 'chaos-pcd-cache').expanduser().resolve()
    if cache_dir == path.parent:
        raise ValueError('PCD cache directory must differ from the original file directory')
    cache_dir.mkdir(parents=True, exist_ok=True)
    key = hashlib.sha256(json.dumps(signature, sort_keys=True).encode()).hexdigest()
    output = cache_dir / (key + '.bin')
    manifest = cache_dir / (key + '.json')
    with (cache_dir / (key + '.lock')).open('a+b') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            metadata = json.loads(manifest.read_text())
            cached = (metadata.get('signature') == signature and
                      metadata.get('field_bytes_verified') is True and
                      output.stat().st_size == expected)
        except (OSError, ValueError, AttributeError):
            cached = False
        if not cached:
            with tempfile.TemporaryDirectory(prefix=key[:12] + '-', dir=cache_dir) as directory:
                temporary = Path(directory)
                temporary_output = temporary / 'interleaved.bin'
                checksums = _build_cache(path, offset + 8, compressed_bytes, unpacked_bytes,
                                         dtype, points, temporary_output, temporary)
                if _signature(path, header, offset, dtype, points) != signature:
                    raise RuntimeError('PCD source changed while being decoded; cache not published')
                with temporary_output.open('rb') as stream:
                    os.fsync(stream.fileno())
                temporary_manifest = temporary / 'manifest.json'
                with temporary_manifest.open('w') as stream:
                    json.dump({'signature': signature, 'points': points, 'array_bytes': expected,
                               'field_bytes_verified': True, 'field_sha256': checksums}, stream, indent=2)
                    stream.write('\n')
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary_output, output)
                os.replace(temporary_manifest, manifest)
                directory_fd = os.open(cache_dir, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory_fd)
                finally:
                    os.close(directory_fd)
    return np.memmap(output, mode='r', dtype=dtype, shape=(points,)), header


def read_pcd_header(path):
    """Read/validate only the small header, without decoding or creating caches."""
    header, _offset, _dtype, _points = _header_and_layout(Path(path).expanduser().resolve())
    return header
