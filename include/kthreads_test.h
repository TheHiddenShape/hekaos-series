#ifndef KTHREADS_TEST_H
#define KTHREADS_TEST_H

/* names describe the workload profile, not a pid: real pids are assigned at
 * spawn time (3+). pid 0/1/2 are reserved (init / userspace init / kthreadd).
 */
void kth_heartbeat_fn (void); /* heartbeat counter */
void kth_compute_fn (void);   /* fibonacci, high CPU pressure */
void kth_memwrite_fn (void);  /* sequential buffer writes */
void kth_memread_fn (void);   /* alternating read/write */
void kth_slow_fn (void);      /* coarse-granularity counter */
void kth_idle_fn (void);      /* near-empty idle loop */

/* sentinel symbols: placed immediately after each fn by the linker order;
 * use (fn_end - fn) as the size argument to exec_fn */
void kth_heartbeat_fn_end (void);
void kth_compute_fn_end (void);
void kth_memwrite_fn_end (void);
void kth_memread_fn_end (void);
void kth_slow_fn_end (void);
void kth_idle_fn_end (void);

#endif
