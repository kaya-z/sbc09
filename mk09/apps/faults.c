/* SPDX-License-Identifier: MIT */
/*
 * faults - what the kernel does with wrong system calls (a test; it ends by trashing its own stack
 * pointer, which stops the program and leaves the system alone)
 */
#include "types.h"
#include "mklib.h"

int main(void)
{
    msg_t m;
    tcreate_t tc;
    u8 tid;
    char junk[4];

    printf("send to task 99: %d\n", mk_send(99, &m));
    printf("send to a task that is not there: %d\n", mk_send(7, &m));
    printf("send to myself: %d\n", mk_send(mk_self(), &m));
    printf("message outside my memory: %d\n", mk_send(TID_FS, (msg_t *)0x0100));
    m.op = FS_READ;
    m.a = 0;
    m.b = 0x0100;                                   /* a grant that is not mine */
    m.c = 16;
    printf("grant outside my memory: %d\n", mk_callg(TID_FS, &m, GR_WRITE));
    printf("copyto without a client: %d\n", mk_copyto(TID_FS, 0, junk, 4));
    printf("create a task: %d\n", mk_tcreate(&tc, &tid));
    printf("kill a task: %d\n", mk_tkill(TID_FS, 1));
    printf("hook the tick: %d\n", mk_hook(1));
    printf("reply to nobody: %d\n", mk_reply(TID_FS, &m));
    printf("receive from task 99: %d\n", mk_recv(99, &m));
    fflush();
    {
        asm {
            lds #$0100
        }
        mk_yield();                                 /* the kernel finds the stack outside of the program */
    }
    return 0;
}
