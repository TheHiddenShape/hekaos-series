  .global init_fn
  .global init_fn_end
  
  # init (PID 1): userland reaper. Loop continuously waitpid.
  # Each int $0x80 blocks until SIGCHLD then reaps a zombie child.
  init_fn:
  .Linit_loop:
      mov $7, %eax # SYS_WAITPID
      xor %ebx, %ebx # wstatus = NULL
      xor %ecx, %ecx
      xor %edx, %edx
      int $0x80 
      jmp .Linit_loop
  init_fn_end:
