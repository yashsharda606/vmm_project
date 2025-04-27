#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <deque>

#define MAX_FRAMES 128
#define MAX_VPAGES 64

struct pte_t {
    unsigned int present : 1;
    unsigned int write_protect : 1;
    unsigned int modified : 1;
    unsigned int referenced : 1;
    unsigned int pagedout : 1;
    unsigned int frame : 7;
    unsigned int file_mapped : 1;
    unsigned : 19; // Padding
};

struct VMA {
    int start_vpage;
    int end_vpage;
    int write_protected;
    int file_mapped;
};

struct frame_t {
    int proc_id;
    int vpage;
    unsigned int age;
    unsigned long long last_used;
};

struct Process {
    int pid;
    pte_t page_table[MAX_VPAGES];
    std::vector<VMA> vmas;
    unsigned long unmaps, maps, ins, outs, fins, fouts, zeros, segv, segprot;
};

struct Instruction {
    char op;
    int value;
};

#endif