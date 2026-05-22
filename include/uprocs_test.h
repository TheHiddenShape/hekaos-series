#ifndef UPROCS_TEST_H
#define UPROCS_TEST_H

/*
 * Ring 3 demo workloads (defined in uprocs_test.s).
 *
 * Each is PIC by construction: immediates + user stack only, int $0x80 inline,
 * NO .rodata, NO external call. This is the only payload shape that survives
 * being byte-copied into a user page (see draft §11, remediation (a)). Output
 * bytes are built on the user stack, never referenced from kernel .rodata.
 *
 * Same fn / fn_end sentinel convention as the kernel-thread workloads: pass
 * (fn_end - fn) as the size argument to the user exec path.
 */

/* write "ring3\n" via SYS_WRITE, then SYS_EXIT(0) */
void uhello_fn (void);
void uhello_fn_end (void);

/* SYS_FORK; child writes "C\n" + exit(1); parent writes "P\n" + waitpid +
 * exit(0) */
void ufork_fn (void);
void ufork_fn_end (void);

/* SYS_GETUID, print the digit, SYS_EXIT(0) (assumes single-digit uid for demo)
 */
void uid_fn (void);
void uid_fn_end (void);

/* pure compute loop, never exits: proves Ring 3 scheduling (visible in eyeproc)
 */
void uspin_fn (void);
void uspin_fn_end (void);

#endif
