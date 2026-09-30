"""Native digital-input RAM/SFR bindings, including internal clock-high RAM."""
from oem_harness import ram
words=dict(published=0x8D0A,older=0x8D0C,newer=0x8D0E,sample=0x8D10,
           p4=0xFFC8,p5=0xFFA2,p6=0xFFCC,p8=0xFFD4,fd04=0xFD04,fd08=0xFD08,
           fd0a=0xFD0A,fd2c=0xFD2C,fd2e=0xFD2E,dp2=0xFFC2,
           clock_low=0xFE44,clock_high=0xE068,clock_irq=0xFF64,
           stamp_low=0x8024,stamp_high=0x8026,stamp_flags=0x8094)
octets=dict(pulse_count=0x81C4,pulse_state=0x81C5,retained_byte=0xF880,source_byte=0xF881)
def address(a):return a if a>=0xE000 else ram(a)
