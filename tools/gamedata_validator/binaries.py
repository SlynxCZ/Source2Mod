"""
Just enough ELF64 (x86-64) and PE32+ parsing to validate gamedata against a
game binary on disk: the executable section to pattern-scan, exported symbols,
and RTTI to find a class's primary vtable and count its virtual functions.

No third-party packages -- the validator runs on a bare Actions runner.
"""

import mmap
import re
import struct


class Binary:
    """Common interface of ElfBinary and PeBinary."""

    platform = None

    def __init__(self, path):
        self.path = path
        self._f = open(path, 'rb')
        self.data = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)

    def close(self):
        self.data.close()
        self._f.close()

    # -- pattern scanning ------------------------------------------------------

    def text(self):
        """(bytes, vaddr) of the section DynLibUtils scans: .text."""
        raise NotImplementedError

    def scan(self, pattern, limit=10):
        """Matches of a gamedata pattern in .text, up to `limit`: [vaddr, ...]."""
        rx = compile_pattern(pattern)
        buf, base = self.text()
        out = []
        pos = 0
        while len(out) < limit:
            m = rx.search(buf, pos)
            if not m:
                break
            out.append(base + m.start())
            pos = m.start() + 1
        return out

    # -- symbols -------------------------------------------------------------------

    def symbol(self, name):
        raise NotImplementedError

    # -- vtables -------------------------------------------------------------------

    def vtable_size(self, class_name):
        """Number of virtual functions in the class's primary vtable, or None
        if the class has no RTTI in this binary."""
        raise NotImplementedError


_pattern_cache = {}


def compile_pattern(pattern):
    """A gamedata pattern ("48 8B ? ?? 05") as a bytes regex; `?` and `??` are
    one wildcard byte each, as in DynLibUtils::ParsePattern."""
    rx = _pattern_cache.get(pattern)
    if rx is not None:
        return rx
    parts = []
    for tok in pattern.split():
        if tok in ('?', '??'):
            parts.append(b'.')
        else:
            if not re.fullmatch(r'[0-9A-Fa-f]{2}', tok):
                raise ValueError('bad pattern byte %r' % tok)
            parts.append(re.escape(bytes([int(tok, 16)])))
    if not parts:
        raise ValueError('empty pattern')
    rx = re.compile(b''.join(parts), re.S)
    _pattern_cache[pattern] = rx
    return rx


# ==============================================================================
# ELF
# ==============================================================================

R_X86_64_64 = 1
R_X86_64_GLOB_DAT = 6
R_X86_64_RELATIVE = 8


class ElfBinary(Binary):
    platform = 'linux'

    def __init__(self, path):
        super().__init__(path)
        d = self.data
        if d[:4] != b'\x7fELF' or d[4] != 2:
            raise ValueError('%s: not an ELF64 file' % path)
        (self.e_phoff, self.e_shoff) = struct.unpack_from('<QQ', d, 0x20)
        (self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum, self.e_shstrndx) = \
            struct.unpack_from('<HHHHH', d, 0x36)

        self.segments = []  # (vaddr, offset, filesz, memsz, flags)
        for i in range(self.e_phnum):
            p_type, p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, _ = \
                struct.unpack_from('<IIQQQQQQ', d, self.e_phoff + i * self.e_phentsize)
            if p_type == 1:  # PT_LOAD
                self.segments.append((p_vaddr, p_offset, p_filesz, p_memsz, p_flags))

        raw = []
        for i in range(self.e_shnum):
            sh = struct.unpack_from('<IIQQQQIIQQ', d, self.e_shoff + i * self.e_shentsize)
            raw.append(sh)
        strtab = raw[self.e_shstrndx]
        self.sections = {}
        self._section_list = []
        for sh in raw:
            name_off, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, _info, _align, sh_entsize = sh
            end = d.find(b'\0', strtab[4] + name_off)
            name = d[strtab[4] + name_off:end].decode('ascii', 'replace')
            sec = {'name': name, 'type': sh_type, 'flags': sh_flags, 'addr': sh_addr,
                   'offset': sh_offset, 'size': sh_size, 'link': sh_link, 'entsize': sh_entsize}
            self.sections[name] = sec
            self._section_list.append(sec)

        self._relocs = None
        self._dynsym = None

    def _sec_bytes(self, name):
        s = self.sections[name]
        return self.data[s['offset']:s['offset'] + s['size']], s['addr']

    def text(self):
        if not hasattr(self, '_text'):
            self._text = self._sec_bytes('.text')
        return self._text

    def exec_range(self):
        s = self.sections['.text']
        return s['addr'], s['addr'] + s['size']

    def va_to_off(self, va):
        for vaddr, off, filesz, _memsz, _flags in self.segments:
            if vaddr <= va < vaddr + filesz:
                return off + (va - vaddr)
        return None

    def off_to_va(self, off):
        for vaddr, soff, filesz, _memsz, _flags in self.segments:
            if soff <= off < soff + filesz:
                return vaddr + (off - soff)
        return None

    # -- dynamic symbols -----------------------------------------------------------

    def _load_dynsym(self):
        if self._dynsym is not None:
            return
        self._dynsym = {}
        self._dynsym_by_index = []
        sec = self.sections.get('.dynsym')
        if not sec:
            return
        strsec = self._section_list[sec['link']]
        stroff = strsec['offset']
        d = self.data
        for i in range(sec['size'] // 24):
            st_name, _info, _other, st_shndx, st_value, _size = \
                struct.unpack_from('<IBBHQQ', d, sec['offset'] + i * 24)
            end = d.find(b'\0', stroff + st_name)
            name = d[stroff + st_name:end].decode('ascii', 'replace')
            self._dynsym_by_index.append((name, st_value, st_shndx))
            if name and st_shndx != 0:
                self._dynsym[name] = st_value

    def symbol(self, name):
        self._load_dynsym()
        va = self._dynsym.get(name)
        return va or None

    # -- relocations ---------------------------------------------------------------

    def _load_relocs(self):
        """offset -> ('rel', target_va) | ('sym', name), and target_va -> [offset]."""
        if self._relocs is not None:
            return
        self._load_dynsym()
        self._relocs = {}
        self._reloc_targets = {}
        for sec in self._section_list:
            if sec['type'] != 4:  # SHT_RELA
                continue
            buf = self.data[sec['offset']:sec['offset'] + sec['size']]
            for r_offset, r_info, r_addend in struct.iter_unpack('<QQq', buf):
                rtype = r_info & 0xffffffff
                if rtype == R_X86_64_RELATIVE:
                    self._relocs[r_offset] = ('rel', r_addend)
                    self._reloc_targets.setdefault(r_addend, []).append(r_offset)
                elif rtype in (R_X86_64_64, R_X86_64_GLOB_DAT):
                    symidx = r_info >> 32
                    name, value, shndx = self._dynsym_by_index[symidx] if symidx < len(self._dynsym_by_index) else ('', 0, 0)
                    if shndx != 0 and value:
                        target = value + r_addend
                        self._relocs[r_offset] = ('rel', target)
                        self._reloc_targets.setdefault(target, []).append(r_offset)
                    else:
                        self._relocs[r_offset] = ('sym', name)

    def _qword(self, va):
        """The pointer stored at va after relocation: ('rel', va) / ('sym', name) / ('raw', value)."""
        r = self._relocs.get(va)
        if r:
            return r
        off = self.va_to_off(va)
        if off is None:
            return ('raw', None)
        return ('raw', struct.unpack_from('<Q', self.data, off)[0])

    # -- vtables (Itanium ABI) -----------------------------------------------------

    def _is_func(self, entry, lo, hi):
        kind, val = entry
        if kind == 'rel':
            return lo <= val < hi
        return kind == 'sym' and bool(val)  # __cxa_pure_virtual and friends

    def _count_slots(self, slot, lo, hi):
        """Function slots from `slot` on. CS2's vtables have the odd null slot
        in the middle, so a run of zeros counts when a function follows it; the
        zeros that end the table are the next vtable's offset-to-top, followed
        by a typeinfo pointer, which is not code."""
        n = 0
        while True:
            e = self._qword(slot)
            if self._is_func(e, lo, hi):
                n += 1
                slot += 8
                continue
            if e != ('raw', 0):
                return n
            zeros = 0
            while self._qword(slot + 8 * zeros) == ('raw', 0) and zeros < 8:
                zeros += 1
            if not self._is_func(self._qword(slot + 8 * zeros), lo, hi):
                return n
            n += zeros
            slot += 8 * zeros

    def vtable_size(self, class_name):
        self._load_relocs()
        # typeinfo name: "<len><name>" as a NUL-terminated string
        needle = b'%d%s\0' % (len(class_name), class_name.encode())
        d = self.data
        lo, hi = self.exec_range()
        best = None
        start = 0
        while True:
            off = d.find(needle, start)
            if off < 0:
                break
            start = off + 1
            # must be the start of a string (preceded by NUL), not the tail of a longer name
            if off > 0 and d[off - 1] != 0:
                continue
            name_va = self.off_to_va(off)
            if name_va is None:
                continue
            for ref in self._reloc_targets.get(name_va, []):
                typeinfo = ref - 8
                for vt_ref in self._reloc_targets.get(typeinfo, []):
                    # vt_ref is the typeinfo slot of a vtable; offset-to-top sits
                    # right before it and is 0 for the primary vtable.
                    ott = self._qword(vt_ref - 8)
                    if ott != ('raw', 0):
                        continue
                    n = self._count_slots(vt_ref + 8, lo, hi)
                    if n and (best is None or n > best):
                        best = n
        return best


# ==============================================================================
# PE
# ==============================================================================

class PeBinary(Binary):
    platform = 'windows'

    def __init__(self, path):
        super().__init__(path)
        d = self.data
        if d[:2] != b'MZ':
            raise ValueError('%s: not a PE file' % path)
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe + 4] != b'PE\0\0':
            raise ValueError('%s: bad PE signature' % path)
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        opt_size = struct.unpack_from('<H', d, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from('<H', d, opt)[0] != 0x20b:
            raise ValueError('%s: not PE32+' % path)
        self.image_base = struct.unpack_from('<Q', d, opt + 24)[0]
        ndirs = struct.unpack_from('<I', d, opt + 108)[0]
        self.dirs = [struct.unpack_from('<II', d, opt + 112 + 8 * i) for i in range(ndirs)]
        self.sections = {}
        self._section_list = []
        sh = opt + opt_size
        for i in range(nsec):
            name = d[sh + 40 * i:sh + 40 * i + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, rva, rawsize, rawptr = struct.unpack_from('<IIII', d, sh + 40 * i + 8)
            chars = struct.unpack_from('<I', d, sh + 40 * i + 36)[0]
            sec = {'name': name, 'rva': rva, 'vsize': vsize, 'rawsize': rawsize, 'rawptr': rawptr, 'chars': chars}
            self.sections.setdefault(name, sec)
            self._section_list.append(sec)
        self._exports = None

    def rva_to_off(self, rva):
        for s in self._section_list:
            if s['rva'] <= rva < s['rva'] + max(s['vsize'], s['rawsize']):
                if rva - s['rva'] >= s['rawsize']:
                    return None
                return s['rawptr'] + (rva - s['rva'])
        return None

    def _sec(self, name):
        s = self.sections[name]
        size = min(s['vsize'], s['rawsize']) if s['vsize'] else s['rawsize']
        return self.data[s['rawptr']:s['rawptr'] + size], s['rva']

    def text(self):
        if not hasattr(self, '_text'):
            buf, rva = self._sec('.text')
            self._text = (buf, self.image_base + rva)
        return self._text

    def symbol(self, name):
        if self._exports is None:
            self._exports = {}
            rva, size = self.dirs[0] if self.dirs else (0, 0)
            off = self.rva_to_off(rva) if rva else None
            if off is not None:
                d = self.data
                (nfuncs, nnames, funcs_rva, names_rva, ords_rva) = struct.unpack_from('<IIIII', d, off + 20)
                funcs = self.rva_to_off(funcs_rva)
                names = self.rva_to_off(names_rva)
                ords = self.rva_to_off(ords_rva)
                for i in range(nnames):
                    nrva = struct.unpack_from('<I', d, names + 4 * i)[0]
                    noff = self.rva_to_off(nrva)
                    end = d.find(b'\0', noff)
                    ename = d[noff:end].decode('ascii', 'replace')
                    ordinal = struct.unpack_from('<H', d, ords + 2 * i)[0]
                    frva = struct.unpack_from('<I', d, funcs + 4 * ordinal)[0]
                    self._exports[ename] = self.image_base + frva
        return self._exports.get(name)

    # -- vtables (MSVC RTTI) -------------------------------------------------------

    def vtable_size(self, class_name):
        d = self.data
        needle = b'.?AV%s@@\0' % class_name.encode()
        tname_off = d.find(needle)
        if tname_off < 0:
            needle = b'.?AU%s@@\0' % class_name.encode()
            tname_off = d.find(needle)
            if tname_off < 0:
                return None

        td_rva = self._off_to_rva(tname_off) - 16  # TypeDescriptor: pVFTable, spare, name[]
        rdata, rdata_rva = self._sec('.rdata')
        text = self.sections['.text']
        lo = self.image_base + text['rva']
        hi = lo + text['vsize']

        best = None
        td = struct.pack('<I', td_rva)
        pos = 0
        while True:
            i = rdata.find(td, pos)
            if i < 0:
                break
            pos = i + 1
            col = i - 12  # CompleteObjectLocator: signature, offset, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf
            if col < 0 or (col & 3):
                continue
            sig, offset, _cd, _td, _chd, pself = struct.unpack_from('<IIIIII', rdata, col)
            col_rva = rdata_rva + col
            if sig != 1 or offset != 0 or pself != col_rva:
                continue
            # the vtable is preceded by a pointer to its COL
            ref = struct.pack('<Q', self.image_base + col_rva)
            rpos = 0
            while True:
                j = rdata.find(ref, rpos)
                if j < 0:
                    break
                rpos = j + 1
                if j & 7:
                    continue
                n = 0
                slot = j + 8
                while slot + 8 <= len(rdata):
                    v = struct.unpack_from('<Q', rdata, slot)[0]
                    if not (lo <= v < hi):
                        break
                    n += 1
                    slot += 8
                if n and (best is None or n > best):
                    best = n
        return best

    def _off_to_rva(self, off):
        for s in self._section_list:
            if s['rawptr'] <= off < s['rawptr'] + s['rawsize']:
                return s['rva'] + (off - s['rawptr'])
        return None


def open_binary(path):
    with open(path, 'rb') as f:
        magic = f.read(4)
    if magic == b'\x7fELF':
        return ElfBinary(path)
    if magic[:2] == b'MZ':
        return PeBinary(path)
    raise ValueError('%s: unknown binary format' % path)
