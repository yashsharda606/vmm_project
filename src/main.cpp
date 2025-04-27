#include "types.h"
#include "pager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <algorithm>

#ifdef _WIN32
#define PRIu64 "I64u"
#define PRIzu "u"
#else
#define PRIu64 "llu"
#define PRIzu "zu"
#endif

using namespace std;

std::vector<Process> processes;
frame_t frame_table[MAX_FRAMES];
std::deque<int> free_frames;
Process *current_process = nullptr;
std::vector<Instruction> instructions;
unsigned long long inst_count = 0, ctx_switches = 0, process_exits = 0, cost = 0;

void read_input(const std::string &filename) {
    std::ifstream file(filename);
    std::string line;
    int num_processes;
    while (std::getline(file, line)) {
        if (line[0] == '#') continue;
        num_processes = std::stoi(line);
        break;
    }
    for (int i = 0; i < num_processes; ++i) {
        Process proc;
        proc.pid = i;
        proc.unmaps = proc.maps = proc.ins = proc.outs = proc.fins = proc.fouts = proc.zeros = proc.segv = proc.segprot = 0;
        for (int j = 0; j < MAX_VPAGES; ++j) {
            proc.page_table[j] = {0};
        }
        std::getline(file, line);
        if (line[0] == '#') std::getline(file, line);
        int num_vmas = std::stoi(line);
        for (int j = 0; j < num_vmas; ++j) {
            std::getline(file, line);
            if (line[0] == '#') std::getline(file, line);
            std::istringstream iss(line);
            VMA vma;
            iss >> vma.start_vpage >> vma.end_vpage >> vma.write_protected >> vma.file_mapped;
            proc.vmas.push_back(vma);
        }
        processes.push_back(proc);
    }
    while (std::getline(file, line)) {
        if (line[0] == '#') continue;
        std::istringstream iss(line);
        Instruction inst;
        iss >> inst.op >> inst.value;
        instructions.push_back(inst);
    }
    file.close();
}

void init_frame_table(int num_frames) {
    for (int i = 0; i < num_frames; ++i) {
        frame_table[i].proc_id = -1;
        frame_table[i].vpage = -1;
        frame_table[i].age = 0;
        frame_table[i].last_used = 0;
        free_frames.push_back(i);
    }
}

frame_t* get_frame(Pager *pager, int num_frames) {
    if (!free_frames.empty()) {
        int frame_id = free_frames.front();
        free_frames.pop_front();
        fprintf(stderr, "DEBUG: Allocated free frame %d\n", frame_id);
        return &frame_table[frame_id];
    }
    frame_t *victim = pager->select_victim_frame();
    fprintf(stderr, "DEBUG: Selected victim frame %d (proc %d, vpage %d)\n", 
            (int)(victim - frame_table), victim->proc_id, victim->vpage);
    return victim;
}

bool is_in_vma(Process *proc, int vpage, VMA &out_vma) {
    for (const auto &vma : proc->vmas) {
        if (vpage >= vma.start_vpage && vpage <= vma.end_vpage) {
            out_vma = vma;
            return true;
        }
    }
    return false;
}

void handle_page_fault(Process *proc, int vpage, char op, Pager *pager, int num_frames, bool output_O) {
    pte_t *pte = &proc->page_table[vpage];
    VMA vma;
    if (!is_in_vma(proc, vpage, vma)) {
        if (output_O) printf("%" PRIu64 ": ==> %c %d\nSEGV\n", inst_count, op, vpage);
        proc->segv++;
        cost += 444;
        fprintf(stderr, "DEBUG: Cost after SEGV (proc %d, vpage %d) = %llu\n", proc->pid, vpage, cost);
        fprintf(stderr, "DEBUG: SEGV on proc %d, vpage %d\n", proc->pid, vpage);
        return;
    }
    frame_t *newframe = get_frame(pager, num_frames);
    if (newframe->proc_id != -1) {
        Process *old_proc = &processes[newframe->proc_id];
        pte_t *old_pte = &old_proc->page_table[newframe->vpage];
        if (output_O) printf("UNMAP %d:%d\n", newframe->proc_id, newframe->vpage);
        old_proc->unmaps++;
        cost += 400;
        fprintf(stderr, "DEBUG: Cost after UNMAP (proc %d, vpage %d) = %llu\n", newframe->proc_id, newframe->vpage, cost);
        fprintf(stderr, "DEBUG: Unmapping proc %d, vpage %d, frame %d, modified=%d, file_mapped=%d\n",
                newframe->proc_id, newframe->vpage, (int)(newframe - frame_table), 
                old_pte->modified, old_pte->file_mapped);
        if (old_pte->modified && old_pte->file_mapped) {
            if (output_O) printf("FOUT\n");
            old_proc->fouts++;
            cost += 1523;
            fprintf(stderr, "DEBUG: Cost after FOUT (proc %d, vpage %d) = %llu\n", newframe->proc_id, newframe->vpage, cost);
            fprintf(stderr, "DEBUG: FOUT for proc %d, vpage %d\n", newframe->proc_id, newframe->vpage);
        }
        old_pte->present = 0;
        old_pte->frame = 0;
        old_pte->referenced = 0;
        old_pte->modified = 0;
        free_frames.push_back(newframe - frame_table);
    }
    if (vma.file_mapped) {
        if (output_O) printf("FIN\n");
        proc->fins++;
        cost += 1500;
        fprintf(stderr, "DEBUG: Cost after FIN (proc %d, vpage %d) = %llu\n", proc->pid, vpage, cost);
        fprintf(stderr, "DEBUG: FIN (first access) for proc %d, vpage %d\n", proc->pid, vpage);
    } else {
        if (output_O) printf("ZERO\n");
        proc->zeros++;
        cost += 140;
        fprintf(stderr, "DEBUG: Cost after ZERO (proc %d, vpage %d) = %llu\n", proc->pid, vpage, cost);
        fprintf(stderr, "DEBUG: ZERO for proc %d, vpage %d\n", proc->pid, vpage);
    }
    if (output_O) printf("MAP %d\n");
    proc->maps++;
    cost += 300;
    fprintf(stderr, "DEBUG: Cost after MAP (proc %d, vpage %d) = %llu\n", proc->pid, vpage, cost);
    pte->present = 1;
    pte->frame = newframe - frame_table;
    pte->write_protect = vma.write_protected;
    pte->file_mapped = vma.file_mapped;
    pte->referenced = 1;
    if (!pte->pagedout) pte->pagedout = 1;
    if (op == 'w' && !vma.write_protected) pte->modified = 1;
    else if (op == 'r' && !vma.file_mapped) pte->modified = 1;
    else if (op == 'r' && vma.file_mapped) pte->modified = 1;
    if (op == 'w' && vma.write_protected) {
        if (output_O) printf("SEGPROT\n");
        proc->segprot++;
        cost += 340;
        fprintf(stderr, "DEBUG: Cost after SEGPROT (proc %d, vpage %d) = %llu\n", proc->pid, vpage, cost);
        fprintf(stderr, "DEBUG: SEGPROT for proc %d, vpage %d, pagedout=1\n", proc->pid, vpage);
    }
    newframe->proc_id = proc->pid;
    newframe->vpage = vpage;
    pager->reset_age(pte->frame);
    newframe->last_used = inst_count;
    fprintf(stderr, "DEBUG: Mapped frame %d to proc %d, vpage %d, modified=%d, pagedout=%d, file_mapped=%d\n",
            (int)(newframe - frame_table), proc->pid, vpage, pte->modified, pte->pagedout, pte->file_mapped);
}

void print_page_table(const Process &proc, bool all) {
    printf("PT[%d]: ", proc.pid);
    for (int i = 0; i < MAX_VPAGES; ++i) {
        const pte_t *pte = &proc.page_table[i];
        if (pte->present) {
            printf("%d:%c%c%c ", i,
                   pte->referenced ? 'R' : '-',
                   pte->modified ? 'M' : '-',
                   pte->pagedout ? 'S' : '-');
        } else {
            printf("%c ", pte->pagedout ? '#' : '*');
        }
    }
    printf("\n");
}

void simulate(Pager *pager, int num_frames, const std::string &options) {
    bool output_O = options.find('O') != std::string::npos;
    bool output_P = options.find('P') != std::string::npos;
    bool output_F = options.find('F') != std::string::npos;
    bool output_S = options.find('S') != std::string::npos;
    bool output_x = options.find('x') != std::string::npos;
    bool output_y = options.find('y') != std::string::npos;
    bool output_f = options.find('f') != std::string::npos;

    for (const auto &inst : instructions) {
        if (output_O) printf("%" PRIu64 ": ==> %c %d\n", inst_count, inst.op, inst.value);
        if (inst.op == 'c') {
            Process *new_process = &processes[inst.value];
            if (current_process != new_process && (current_process != nullptr || ctx_switches == 0)) {
                ctx_switches++;
                cost += 130;
                fprintf(stderr, "DEBUG: Cost after context switch (%c %d) = %llu\n", inst.op, inst.value, cost);
                fprintf(stderr, "DEBUG: Context switch to proc %d, ctx_switches=%llu\n", 
                        new_process->pid, ctx_switches);
            } else {
                fprintf(stderr, "DEBUG: No context switch, staying on proc %d\n", new_process->pid);
            }
            current_process = new_process;
        } else if (inst.op == 'e') {
            Process *exiting_process = &processes[inst.value];
            for (int i = 0; i < MAX_VPAGES; ++i) {
                pte_t *pte = &exiting_process->page_table[i];
                if (pte->present) {
                    if (output_O) printf("UNMAP %d:%d\n", exiting_process->pid, i);
                    exiting_process->unmaps++;
                    cost += 400;
                    fprintf(stderr, "DEBUG: Cost after UNMAP (proc %d, vpage %d) = %llu\n", exiting_process->pid, i, cost);
                    fprintf(stderr, "DEBUG: Process exit, unmapping proc %d, vpage %d, frame %d, modified=%d, file_mapped=%d\n",
                            exiting_process->pid, i, pte->frame, pte->modified, pte->file_mapped);
                    if (pte->modified && pte->file_mapped) {
                        if (output_O) printf("FOUT\n");
                        exiting_process->fouts++;
                        cost += 1523;
                        fprintf(stderr, "DEBUG: Cost after FOUT (proc %d, vpage %d) = %llu\n", exiting_process->pid, i, cost);
                        fprintf(stderr, "DEBUG: FOUT for proc %d, vpage %d\n", exiting_process->pid, i);
                    }
                    free_frames.push_back(pte->frame);
                    fprintf(stderr, "DEBUG: Freed frame %d during process exit\n", pte->frame);
                    pte->present = 0;
                    pte->frame = 0;
                    pte->referenced = 0;
                    pte->modified = 0;
                    pte->write_protect = 0;
                    pte->file_mapped = 0;
                    pte->pagedout = 0;
                }
            }
            process_exits++;
            cost += 400;
            fprintf(stderr, "DEBUG: Cost after process exit (%c %d) = %llu\n", inst.op, inst.value, cost);
            fprintf(stderr, "DEBUG: Process %d exited, process_exits=%llu\n", exiting_process->pid, process_exits);
            if (current_process == exiting_process) {
                current_process = nullptr;
                fprintf(stderr, "DEBUG: Current process set to nullptr\n");
            }
        } else {
            int vpage = inst.value;
            pte_t *pte = &current_process->page_table[vpage];
            if (!pte->present) {
                handle_page_fault(current_process, vpage, inst.op, pager, num_frames, output_O);
            } else {
                if (inst.op == 'w' && pte->write_protect) {
                    if (output_O) printf("SEGPROT\n");
                    current_process->segprot++;
                    cost += 340;
                    fprintf(stderr, "DEBUG: Cost after SEGPROT (proc %d, vpage %d) = %llu\n", current_process->pid, vpage, cost);
                    fprintf(stderr, "DEBUG: SEGPROT on proc %d, vpage %d, pagedout=1\n", current_process->pid, vpage);
                    pte->referenced = 1;
                    pte->pagedout = 1;
                } else {
                    pte->referenced = 1;
                    if (inst.op == 'w' && !pte->write_protect) {
                        pte->modified = 1;
                        fprintf(stderr, "DEBUG: Set modified=1 for proc %d, vpage %d (write)\n", current_process->pid, vpage);
                    }
                    if ((inst.op == 'r' || inst.op == 'w') && pte->file_mapped) {
                        pte->modified = 1;
                        fprintf(stderr, "DEBUG: Set modified=1 for proc %d, vpage %d (file_mapped)\n", current_process->pid, vpage);
                    }
                }
            }
            cost += 1;
            fprintf(stderr, "DEBUG: Cost after instruction (%c %d) = %llu\n", inst.op, inst.value, cost);
            fprintf(stderr, "DEBUG: Processed %c %d, cost=%llu\n", inst.op, inst.value, cost);
        }
        inst_count++;
        if (output_x && current_process) print_page_table(*current_process, false);
        if (output_y) {
            for (const auto &proc : processes) print_page_table(proc, true);
        }
        if (output_f) {
            printf("FT:");
            for (int i = 0; i < num_frames; ++i) {
                if (frame_table[i].proc_id == -1) {
                    printf(" *");
                } else {
                    printf(" %d:%d", frame_table[i].proc_id, frame_table[i].vpage);
                }
            }
            printf("\n");
            fprintf(stderr, "DEBUG: Frame table printed, free_frames size=%zu\n", free_frames.size());
        }
    }
    if (output_P) {
        for (const auto &proc : processes) print_page_table(proc, true);
    }
    if (output_F) {
        printf("FT:");
        for (int i = 0; i < num_frames; ++i) {
            if (frame_table[i].proc_id == -1) {
                printf(" *");
            } else {
                printf(" %d:%d", frame_table[i].proc_id, frame_table[i].vpage);
            }
        }
        printf("\n");
    }
    if (output_S) {
        for (const auto &proc : processes) {
            printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
                   proc.pid, proc.unmaps, proc.maps, proc.ins, proc.outs,
                   proc.fins, proc.fouts, proc.zeros, proc.segv, proc.segprot);
            fprintf(stderr, "DEBUG: Proc %d stats: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
                    proc.pid, proc.unmaps, proc.maps, proc.ins, proc.outs,
                    proc.fins, proc.fouts, proc.zeros, proc.segv, proc.segprot);
        }
        printf("TOTALCOST %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIzu "\n",
               inst_count, ctx_switches, process_exits, cost, sizeof(pte_t));
        fprintf(stderr, "DEBUG: TOTALCOST inst=%llu ctx_switches=%llu exits=%llu cost=%llu\n",
                inst_count, ctx_switches, process_exits, cost);
    }
}

int main(int argc, char *argv[]) {
    int num_frames = 0;
    char algo = '\0';
    std::string options, inputfile, randfile;
    int opt;
    while ((opt = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (opt) {
            case 'f': num_frames = atoi(optarg); break;
            case 'a': algo = optarg[0]; break;
            case 'o': options = optarg; break;
            default:
                std::cerr << "Usage: " << argv[0] << " -f<num_frames> -a<algo> [-o<options>] inputfile randomfile\n";
                return 1;
        }
    }
    if (optind + 2 != argc) {
        std::cerr << "Missing inputfile or randomfile\n";
        return 1;
    }
    inputfile = argv[optind];
    randfile = argv[optind + 1];

    static_assert(sizeof(pte_t) == 4, "pte_t must be 32 bits");

    read_input(inputfile);
    init_frame_table(num_frames);
    fprintf(stderr, "DEBUG: Initialized %d frames, input file %s\n", num_frames, inputfile.c_str());

    Pager *pager;
    switch (algo) {
        case 'f': pager = new FIFO_Pager(); break;
        case 'r': pager = new Random_Pager(randfile, num_frames); break;
        case 'c': pager = new Clock_Pager(); break;
        case 'e': pager = new NRU_Pager(); break;
        case 'a': pager = new Aging_Pager(); break;
        case 'w': pager = new WorkingSet_Pager(); break;
        default: std::cerr << "Invalid algorithm\n"; return 1;
    }

    simulate(pager, num_frames, options);
    delete pager;
    return 0;
}