"""Offline adversarial journal admission tests, with no driver dependency."""
import io
import random
import struct
import unittest
from check_journal import check_stream, abi_tag, MAGIC, SIGNATURE

def header(): return struct.pack('<4I2Q',MAGIC,9,32,32,abi_tag(),SIGNATURE)
def record(op, h, data=b'', relocs=()):
    return struct.pack('<4I',op,len(h),len(data),len(relocs))+h+data+b''.join(struct.pack('<2I',*r) for r in relocs)
def fixture():
    return header()+record(1,struct.pack('<Q2I40s',32,1048577,1,b'buffer'),bytes(32))+record(2,struct.pack('<I',1048577))

class JournalTests(unittest.TestCase):
    def reject(self, raw):
        with self.assertRaises(ValueError): check_stream(io.BytesIO(raw))
    def test_good_fixture(self): self.assertEqual(check_stream(io.BytesIO(fixture()))['records'],2)
    def test_every_truncation(self):
        raw=fixture()
        # A complete prefix ending after creation is valid; every partial record is not.
        for n in range(len(raw)):
            if n not in (32,136): self.reject(raw[:n])
    def test_abi_and_endian(self):
        raw=bytearray(header()); raw[16]^=1; self.reject(raw)
        self.reject(struct.pack('>4I2Q',MAGIC,9,32,32,abi_tag(),SIGNATURE))
    def test_legacy_requires_explicit_admission(self):
        raw=bytearray(header()); struct.pack_into('<I',raw,4,8)
        self.reject(raw)
        self.assertEqual(check_stream(io.BytesIO(raw),allow_legacy=True)['version'],8)
    def test_length_opcode_and_budget(self):
        for op,hdr,size,count in [(0,0,0,0),(99,0,0,0),(1,256,0,0),(1,56,1<<31,0),(1,56,0,4097)]:
            self.reject(header()+struct.pack('<4I',op,hdr,size,count))
    def test_relocation_duplicates_and_unknown_identity(self):
        create=record(1,struct.pack('<Q2I40s',32,1048577,0,b'buffer'))
        for relocs,data in [([(0,3),(0,3)],struct.pack('<Q',1048577<<32)),
                            ([(0,3)],struct.pack('<Q',123<<32)), ([(1,3)],bytes(8)), ([(0,99)],bytes(8))]:
            self.reject(header()+create+record(3,struct.pack('<2IQ',1048577,0,0),data,relocs))
    def test_wrapper_ranges_and_frames(self):
        self.reject(header()+record(3,struct.pack('<2IQ',1,0,0),b'x'))
        self.reject(header()+record(10,bytes(160)))
        self.reject(header()+record(12,struct.pack('<2IQ',0,0,8)))
    def test_bounded_mutation_campaign(self):
        rng=random.Random(17); raw=fixture(); rejected=0
        for _ in range(1000):
            data=bytearray(raw); pos=rng.randrange(len(data)); data[pos]^=1<<rng.randrange(8)
            try: check_stream(io.BytesIO(data),max_record=1024)
            except ValueError: rejected+=1
        self.assertGreater(rejected,350)

if __name__=='__main__': unittest.main()
