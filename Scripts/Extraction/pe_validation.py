"""Reject interrupted Win64 link outputs before treating builds as usable."""
import hashlib
from pathlib import Path
import struct


def validate_executable(path):
    path=Path(path)
    size=path.stat().st_size
    with path.open('rb') as stream:
        dos=stream.read(64)
        if len(dos)!=64 or dos[:2]!=b'MZ':raise ValueError(f'{path}: missing DOS header')
        offset=struct.unpack_from('<I',dos,60)[0]
        if offset<64 or offset+24>size:raise ValueError(f'{path}: invalid PE offset')
        stream.seek(offset)
        pe=stream.read(24)
        if pe[:4]!=b'PE\0\0':raise ValueError(f'{path}: missing PE signature (possibly interrupted link)')
        machine,sections=struct.unpack_from('<HH',pe,4)
        optional=struct.unpack_from('<H',pe,20)[0]
        if machine!=0x8664 or not 0<sections<100:raise ValueError(f'{path}: invalid Win64 image')
        stream.seek(optional,1)
        for _ in range(sections):
            section=stream.read(40)
            if len(section)!=40:raise ValueError(f'{path}: incomplete section table')
            length,start=struct.unpack_from('<II',section,16)
            if start+length>size:raise ValueError(f'{path}: truncated section')
        stream.seek(0)
        digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path),bytes=size,sha256=digest,machine='AMD64',sections=sections)
