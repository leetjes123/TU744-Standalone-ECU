"""Address bindings shared by independent and composed speed-input tests."""
words=dict(descriptor=0xB2F4,fd00=0xFD00,fd06=0xFD06,fd08=0xFD08,status=0x8178,
           speed=0x95AA,physical_speed=0x95A4,source_speed=0x95A6,distance=0x95A8,target=0x95AC,
           batch=0x815A,previous_speed=0x815C,capture=0x815E,previous_capture=0x8160,period=0x8162,
           numerator_low=0x8164,numerator_high=0x8166,fraction=0x8168,filter_high=0x816A,
           source_fraction=0x816C,source_filter=0x816E,source_target=0x8170,pulse_total=0x817A,
           source_a=0x9ACE,source_b=0x9AD0,timer=0xFE44,pecc5=0xFECA,ccm3=0xFF58,
           srcp5=0xFCF4,dstp5=0xFCF6,cc14ic=0xFF94)
octets=dict(source=0x94A3,acceleration_status=0x947A,acceleration_input=0x943E,stale_count=0x8156,
            next_batch=0x8157,active_batch=0x8158,captured_batch=0x8159,source_count=0x8176,
            vehicle_speed=0x9201,acceleration=0x9202)
