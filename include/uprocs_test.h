#ifndef UPROCS_TEST_H
#define UPROCS_TEST_H

/* SYS_FORK; child exit(1); parent waitpid + exit(0). Silent. */
void ufork_fn (void);
void ufork_fn_end (void);

/* SYS_GETUID then SYS_EXIT(0); return value discarded. Silent. */
void uid_fn (void);
void uid_fn_end (void);

/* pure compute loop, never exits: proves Ring 3 scheduling (visible in
 * eyeproc). */
void uspin_fn (void);
void uspin_fn_end (void);

#endif
