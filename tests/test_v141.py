"""Execute cross-compiled SDK wrappers/runtime against isolated MIPS services.
This is ABI/regression coverage, not a claim of fresh physical H1 testing.
"""
from pathlib import Path
import struct
import tempfile
import unittest
from h1_bda.build import compile_sources
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN
from unicorn.mips_const import (UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_V0,
    UC_MIPS_REG_GP,UC_MIPS_REG_S0,UC_MIPS_REG_CP0_STATUS,UC_MIPS_REG_PC)
FIXTURES=Path(__file__).parent/'fixtures'

def run(payload):
    u=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN)
    u.mem_map(0,64*1024*1024);u.mem_map(0x10003000,4096)
    u.mem_write(0x10003000,struct.pack('<II',1,1791199800))
    u.mem_write(0x3c00020,payload);u.reg_write(UC_MIPS_REG_CP0_STATUS,0)
    u.reg_write(UC_MIPS_REG_SP,0x83aff000);u.reg_write(UC_MIPS_REG_RA,0x80001ff0)
    u.reg_write(UC_MIPS_REG_GP,0x12345678);u.reg_write(UC_MIPS_REG_S0,0xabcdef01)
    u.emu_start(0x83c00020,0x80001ff0,timeout=5_000_000,count=1_000_000)
    if u.reg_read(UC_MIPS_REG_PC)!=0x80001ff0: raise AssertionError('MIPS instruction budget exceeded')
    return u

class V141Tests(unittest.TestCase):
    def test_generated_code_cache_sync_and_address_bounds(self):
        # Unicorn accepts privileged CACHE but does not model physical cache
        # coherence; this checks emitted-code execution and API address bounds.
        u=run(compile_sources([FIXTURES/'code_cache.c'],[]))
        self.assertEqual(u.reg_read(UC_MIPS_REG_V0),0,'failure is the fixture C line number')

    def test_profile_clock_wrap_terminal_faults_ownership_and_restore(self):
        u=run(compile_sources([FIXTURES/'profile_clock.c'],[],compiler_flags=['-Wall','-Wextra','-Werror']))
        self.assertEqual(u.reg_read(UC_MIPS_REG_V0),0,'failure is the fixture C line number')

    def test_real_wrappers_preserve_input_audio_and_path_contracts(self):
        u=run(compile_sources([FIXTURES/'v141_services.c'],[],compiler_flags=['-Wall','-Wextra','-Werror']))
        self.assertEqual(u.reg_read(UC_MIPS_REG_V0),0,'failure is the fixture C line number')

    def test_cpp_constructors_libgcc_private_stack_and_aligned_assembly(self):
        payload=compile_sources([FIXTURES/'runtime_app.cpp',FIXTURES/'aligned_helper.S'],[],runtime=True)
        u=run(payload)
        self.assertEqual(u.reg_read(UC_MIPS_REG_V0),42)
        self.assertEqual(struct.unpack('<I',u.mem_read(0x10000,4))[0],432,'destructors run in reverse priority order')
        self.assertEqual(u.reg_read(UC_MIPS_REG_GP),0x12345678)
        self.assertEqual(u.reg_read(UC_MIPS_REG_S0),0xabcdef01)
        self.assertEqual(u.reg_read(UC_MIPS_REG_SP),0x83aff000)
        self.assertLess(len(payload),32768,'NOLOAD private stack must not inflate the BDA')
