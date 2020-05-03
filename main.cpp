#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <string_view>


struct TransferEvent {
    int start, duration, src, dst;
    inline TransferEvent(int start, int duration, int src, int dst) noexcept
        : start(start), duration(duration), src(src), dst(dst) {}
    int finish() const noexcept { return start + duration; }
};

struct ProcessingEvent {
    int start, duration, taskId;
    inline ProcessingEvent(int start, int duration, int taskId) noexcept
        : start(start), duration(duration), taskId(taskId) {}
    int finish() const noexcept { return start + duration; }
};


struct Processor {
    std::vector<ProcessingEvent> processingTimeline;
    std::vector<TransferEvent> transferTimeline;
};
// ============================================================================
// ============================================================================
// ============================================================================
template <typename T>
T& withId(int id, std::vector<T>& ts) noexcept {
    for (T& t : ts) {
        if (t.id == id) return t;
    }
    std::cout << "::> No T with id " << id << '\n';
    exit(-1);
}
template <typename T>
const T& withId(int id, const std::vector<T>& ts) noexcept {
    for (const T& t : ts) {
        if (t.id == id) return t;
    }
    std::cout << "::> No T with id " << id << '\n';
    exit(-1);
}
// ============================================================================
// ============================================================================
// ============================================================================
struct Transfer {
    int src, dst, volume;
    Transfer(int src, int dst, int volume) noexcept : src(src), dst(dst), volume(volume) {}
};

struct TransferTo {
    int dst, volume;
    TransferTo(int dst, int volume) noexcept : dst(dst), volume(volume) {}
};

struct Task {
    int id;
    std::vector<int> weights;
    std::vector<int> energies;
    std::vector<TransferTo> targets;

    Task(int id, std::vector<int>&& weights, std::vector<int>&& energies) noexcept
        : id(id), weights(std::move(weights)), energies(std::move(energies)) {}
};

struct TaskGraph {
    std::vector<Task> tasks;
    std::vector<Transfer> transfers; // redundant. for convenience

    Task& taskWithId(int id) noexcept { return withId(id, tasks); }
    const Task& taskWithId(int id) const noexcept { return withId(id, tasks); }
    void add(int id, std::vector<int>&& weights, std::vector<int>&& energies) noexcept {
        tasks.emplace_back(id, std::move(weights), std::move(energies));
    }
    void addTransfer(int src, int dst, int volume) noexcept {
        transfers.emplace_back(src, dst, volume);
        taskWithId(src).targets.emplace_back(dst, volume);
    }
};

// Used only for cyclesExist()
bool allGoodFrom(int id, std::vector<int> visited, const TaskGraph& taskGraph) noexcept {
    if (std::find(visited.begin(), visited.end(), id) != visited.end()) return false;
    visited.push_back(id);
    const auto& targets = taskGraph.taskWithId(id).targets;
    for (const auto& [dst, _volume] : targets) {
        if (!allGoodFrom(dst, visited, taskGraph)) return false;
    }
    return true;
}

bool cyclesExist(const TaskGraph& taskGraph) noexcept {
    std::vector<int> sourceIds;
    for (const auto& [id, _weights, _energies, _targets] : taskGraph.tasks) sourceIds.push_back(id);
    for (const auto& [id, _weights, _energies, targets] : taskGraph.tasks) {
        for (const auto& [dst, _volume] : targets) {
            const auto pos = std::find(sourceIds.begin(), sourceIds.end(), dst);
            if (pos != sourceIds.end()) sourceIds.erase(pos);
        }
    }

    if (sourceIds.empty()) return true;
    for (int id : sourceIds) {
        if (!allGoodFrom(id, {}, taskGraph)) return true;
    }
    return false;
}

// bool hangingNodesExist(const TaskGraph& taskGraph) noexcept {
//     std::vector<int> endingNodes;
//     for (const auto& [id, _weight, targets] : taskGraph.tasks) {
//         if (targets.empty()) { // ending node
//             bool found = false;
//             for (const auto& [_src, dst, _volume] : taskGraph.transfers) {
//                 if (dst == id) {
//                     found = true;
//                     break;
//                 }
//             }
//             if (!found) {
//                 std::cout << ":> Hanging Node with id = " << id << '\n';
//                 return true;
//             }
//         }
//     }
//     return false;
// }
// ============================================================================
// ============================================================================
// ============================================================================
TaskGraph readTaskGraph(std::string_view path) {
    TaskGraph taskGraph;
    std::ifstream file(path.data());
    char type;
    file >> type;
    if (type != 'V') {
        std::cout << "::> Expected voltage levels amount (V) to be the first entry.\n";
        exit(-1);
    }

    unsigned int voltageLevelsAmount;
    file >> voltageLevelsAmount;

    while (file >> type) {
        if (type == 'T') {
            int id;
            std::vector<int> weights(voltageLevelsAmount);
            std::vector<int> energies(voltageLevelsAmount);
            file >> id >> type;
            for (unsigned int i = 0; i < voltageLevelsAmount; i++) file >> weights[i];
            file >> type;
            for (unsigned int i = 0; i < voltageLevelsAmount; i++) file >> energies[i];
            taskGraph.add(id, std::move(weights), std::move(energies));
        } else if (type == 'S') {
            int from, to, volume;
            file >> from >> type >> to >> type >> volume;
            taskGraph.addTransfer(from, to, volume);
        } else {
            std::cout << "::> Unexpected beginning of a line in " << path << ":" << type << '\n';
            exit(-1);
        }
    }

    return taskGraph;
}
// ============================================================================
// ============================================================================
// ============================================================================
int main() {
    const TaskGraph taskGraph = readTaskGraph("taskGraph.txt");
    if (cyclesExist(taskGraph)) {
        std::cout << "::> Cycles detected in tasks graph" << '\n';
        exit(-1);
    }

    std::cout << "===============================================" << '\n';
    std::vector<Processor> units;

    return 0;
}
