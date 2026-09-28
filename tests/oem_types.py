"""Native ABI mirrors used by routine and composed diagnostic tests."""
import ctypes as C

class Adc(C.Structure):
    _fields_=[('scan',C.c_uint16*16),('input',C.c_uint16*5),
              ('battery',C.c_uint8),('iat',C.c_uint8)]

class Records(C.Structure):
    _fields_=[('records',(C.c_uint8*24)*20),('count',C.c_uint8),('demand',C.c_uint8),
              ('active_demand',C.c_uint8),('phases',C.c_uint8*7)]

class State(C.Structure):
    _fields_=[('store',Records),('live',C.c_uint16*107),('gate',C.c_uint16),('timestamp',C.c_uint16),
              ('last_event',C.c_uint8),('overflow',C.c_uint8),('context',C.c_uint8*9),
              ('clear_request',C.c_uint16),('clear_inverse',C.c_uint16),('clear_mode',C.c_uint16),
              ('startup_flags',C.c_uint16),('scan_event',C.c_uint8),('clear_previous',C.c_uint8),
              ('scan_record',C.c_uint8),('scan_unused',C.c_uint8),('clear_wait',C.c_uint8),('lock_wait',C.c_uint8),
              ('run_flags',C.c_uint16),('clock_divider',C.c_uint16),('drive_timer',C.c_uint16),('drive_count',C.c_uint16),
              ('coolant',C.c_uint8),('warmup_start',C.c_uint8),('warmup_count',C.c_uint8)]

class Coolant(C.Structure):
    _fields_=[('value',C.c_uint16*33),('flags',C.c_uint16*7)]

class Mil(C.Structure):
    _fields_=[(n,C.c_uint8) for n in ['state','retained','prove_count','prove_flags','flash_count','demand','lamp']]+[
        (n,C.c_uint16) for n in ['fd08','fd0e','fd12','fd5a','fd6a']]

class Iat(C.Structure):
    _fields_=[(n,C.c_uint16) for n in ['descriptor','coolant_descriptor','status','filter','startup_flags','run_flags']]+[
        (n,C.c_uint8) for n in ['adc','raw','filtered','captured','pass_count','fail_count','coolant']]

class Engine(C.Structure):
    _fields_=[(n,C.c_uint16) for n in ['run_flags','rotation_flags','count_a','running_count']]+[
        (n,C.c_uint8) for n in ['coolant','iat','speed']]

class Context(C.Structure):
    _fields_=[('value',C.c_uint16*14),('coolant',C.c_uint8),('vehicle_speed',C.c_uint8),('output',C.c_uint8*14)]

class Voltage(C.Structure):
    _fields_=[(n,C.c_uint16) for n in ['descriptor','speed_descriptor','status','run_flags',
        'fraction','filter_high','scaled','filtered']]+[(n,C.c_uint8) for n in [
        'adc','voltage','scaled_byte','filtered_byte','delay','fail_count','pass_count','divider','vehicle_speed']]

class Vss(C.Structure):
    _fields_=[(n,C.c_uint16) for n in ['descriptor','source_a_descriptor','source_b_descriptor',
        'status','fd06','fd08','fd18','fd52','fd5e','speed','condition_speed']]+[
        (n,C.c_uint8) for n in ['source','source_a_status','source_b_status','fail_count',
        'pass_count','source_count','coolant','engine_speed','load']]

class VssInput(C.Structure):
    _fields_=[(n,C.c_uint16) for n in ['descriptor','fd00','fd06','fd08','status',
        'speed','physical_speed','source_speed','distance','target','batch','previous_speed',
        'capture','previous_capture','period','numerator_low','numerator_high','fraction','filter_high',
        'source_fraction','source_filter','source_target','pulse_total','source_a','source_b','timer',
        'pecc5','ccm3','srcp5','dstp5','cc14ic']]+[(n,C.c_uint8) for n in [
        'source','acceleration_status','acceleration_input','stale_count','next_batch','active_batch',
        'captured_batch','source_count','vehicle_speed','acceleration']]

class Digital(C.Structure):
    _fields_=[(n,C.c_uint16) for n in ['published','older','newer','sample','p4','p5','p6','p8',
        'fd04','fd08','fd0a','fd2c','fd2e','dp2','clock_low','clock_high','clock_irq',
        'stamp_low','stamp_high','stamp_flags']]+[(n,C.c_uint8) for n in [
        'pulse_count','pulse_state','retained_byte','source_byte']]

class Readiness(C.Structure):
    _fields_=[('descriptor',C.c_uint16*11)]+[(n,C.c_uint16) for n in
        ['fd02','config_a','config_b','startup_flags']]+[('count',C.c_uint8*5)]+[
        (n,C.c_uint8) for n in ['supported','pending','once']]

class Diagnostics(C.Structure):
    _fields_=[('events',State),('coolant',Coolant),('iat',Iat),('engine',Engine),('context',Context),('mil',Mil),('voltage',Voltage),('vss',Vss),('vss_input',VssInput),('digital',Digital),('readiness',Readiness)]
