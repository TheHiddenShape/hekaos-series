.global tss_flush

tss_flush:
    mov $0x38, %ax  # selector for GDT entry 7 (7 << 3), RPL 0
    ltr %ax         # load task register; from now on the CPU reads ss0:esp0
                    # from `tss` on every Ring3 -> Ring0 transition
    ret
