#include "pager.h"
#include "types.h"
#include <fstream>
#include <sstream>

extern frame_t frame_table[MAX_FRAMES];
extern std::vector<Process> processes;
extern unsigned long long inst_count;

FIFO_Pager::FIFO_Pager() : hand(0) {}

frame_t* FIFO_Pager::select_victim_frame() {
    frame_t *victim = &frame_table[hand];
    hand = (hand + 1) % MAX_FRAMES;
    return victim;
}

Random_Pager::Random_Pager(const std::string &randfile, int num_frames) : ofs(0) {
    std::ifstream file(randfile);
    int value;
    while (file >> value) {
        random_values.push_back(value);
    }
    file.close();
}

frame_t* Random_Pager::select_victim_frame() {
    int r = random_values[ofs] % MAX_FRAMES;
    ofs = (ofs + 1) % random_values.size();
    return &frame_table[r];
}

Clock_Pager::Clock_Pager() : hand(0) {}

frame_t* Clock_Pager::select_victim_frame() {
    while (true) {
        frame_t *frame = &frame_table[hand];
        Process *proc = &processes[frame->proc_id];
        pte_t *pte = &proc->page_table[frame->vpage];
        if (pte->referenced) {
            pte->referenced = 0;
            hand = (hand + 1) % MAX_FRAMES;
        } else {
            hand = (hand + 1) % MAX_FRAMES;
            return frame;
        }
    }
}

NRU_Pager::NRU_Pager() : hand(0), last_reset(0) {}

frame_t* NRU_Pager::select_victim_frame() {
    // Reset reference bits every 10 instructions
    if (inst_count - last_reset >= 10) {
        for (int i = 0; i < MAX_FRAMES; ++i) {
            if (frame_table[i].proc_id != -1) {
                Process *proc = &processes[frame_table[i].proc_id];
                pte_t *pte = &proc->page_table[frame_table[i].vpage];
                pte->referenced = 0;
            }
        }
        last_reset = inst_count;
    }

    // NRU classes: (0) not referenced, not modified; (1) not referenced, modified;
    // (2) referenced, not modified; (3) referenced, modified
    int min_class = 4;
    int victim_frame = -1;
    int start_hand = hand;

    do {
        frame_t *frame = &frame_table[hand];
        if (frame->proc_id != -1) {
            Process *proc = &processes[frame->proc_id];
            pte_t *pte = &proc->page_table[frame->vpage];
            int nru_class = (pte->referenced << 1) | pte->modified;
            if (nru_class < min_class) {
                min_class = nru_class;
                victim_frame = hand;
            }
            if (min_class == 0) break; // Lowest class found
        }
        hand = (hand + 1) % MAX_FRAMES;
    } while (hand != start_hand);

    if (victim_frame == -1) {
        victim_frame = hand;
        hand = (hand + 1) % MAX_FRAMES;
    }

    return &frame_table[victim_frame];
}

Aging_Pager::Aging_Pager() : hand(0) {
    for (int i = 0; i < MAX_FRAMES; ++i) {
        age[i] = 0;
    }
}

frame_t* Aging_Pager::select_victim_frame() {
    unsigned min_age = 0xFFFFFFFF;
    int victim_frame = -1;
    int start_hand = hand;

    // Update ages and find minimum
    do {
        frame_t *frame = &frame_table[hand];
        if (frame->proc_id != -1) {
            Process *proc = &processes[frame->proc_id];
            pte_t *pte = &proc->page_table[frame->vpage];
            // Shift right and add referenced bit as MSB
            age[hand] >>= 1;
            if (pte->referenced) {
                age[hand] |= 0x80000000;
                pte->referenced = 0;
            }
            if (age[hand] < min_age) {
                min_age = age[hand];
                victim_frame = hand;
            }
        }
        hand = (hand + 1) % MAX_FRAMES;
    } while (hand != start_hand);

    if (victim_frame == -1) {
        victim_frame = hand;
        hand = (hand + 1) % MAX_FRAMES;
    }

    frame_t *victim = &frame_table[victim_frame];
    age[victim_frame] = 0; // Reset age on eviction
    hand = (victim_frame + 1) % MAX_FRAMES;
    return victim;
}

void Aging_Pager::reset_age(int frame) {
    age[frame] = 0;
}

WorkingSet_Pager::WorkingSet_Pager() : hand(0) {}

frame_t* WorkingSet_Pager::select_victim_frame() {
    const unsigned long long TAU = 49;
    int start_hand = hand;
    unsigned long long oldest_time = inst_count;
    int oldest_frame = -1;

    do {
        frame_t *frame = &frame_table[hand];
        if (frame->proc_id != -1) {
            Process *proc = &processes[frame->proc_id];
            pte_t *pte = &proc->page_table[frame->vpage];
            if (pte->referenced) {
                // Referenced recently, reset last_used and clear reference bit
                frame->last_used = inst_count;
                pte->referenced = 0;
            } else if (inst_count - frame->last_used > TAU) {
                // Not referenced within TAU, select as victim
                hand = (hand + 1) % MAX_FRAMES;
                return frame;
            }
            // Track oldest frame for LRU fallback
            if (frame->last_used < oldest_time) {
                oldest_time = frame->last_used;
                oldest_frame = hand;
            }
        }
        hand = (hand + 1) % MAX_FRAMES;
    } while (hand != start_hand);

    // No frame outside TAU, select oldest (LRU)
    if (oldest_frame == -1) {
        oldest_frame = hand;
        hand = (hand + 1) % MAX_FRAMES;
    }

    frame_t *victim = &frame_table[oldest_frame];
    hand = (oldest_frame + 1) % MAX_FRAMES;
    return victim;
}