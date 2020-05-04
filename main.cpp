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
struct Transfer {
    int src, dst, volume;
    Transfer(int src, int dst, int volume) noexcept : src(src), dst(dst), volume(volume) {}
};

struct TransferTo {
    int dst, volume;
    TransferTo(int dst, int volume) noexcept : dst(dst), volume(volume) {}
};

struct Task {
    std::vector<int> weights;
    std::vector<int> energies;
    std::vector<TransferTo> targets;

    int policy = 0;
    std::optional<int> early = std::nullopt;
    std::optional<int> late = std::nullopt;

    int weight() const noexcept { return weights[policy]; }
    void clearStats() noexcept { early = std::nullopt; late = std::nullopt; }

    Task(std::vector<int>&& weights, std::vector<int>&& energies) noexcept
        : weights(std::move(weights)), energies(std::move(energies)) {}
};

struct TaskGraph {
    std::vector<Task> tasks;
    std::vector<Transfer> transfers; // redundant. for convenience
    bool indexingFromZero; // to determine what output the User expects

    TaskGraph(bool indexingFromZero) noexcept : indexingFromZero(indexingFromZero) {}
    void add(std::vector<int>&& weights, std::vector<int>&& energies) noexcept {
        tasks.emplace_back(std::move(weights), std::move(energies));
    }
    void addTransfer(int src, int dst, int volume) noexcept {
        transfers.emplace_back(src, dst, volume);
        tasks[src].targets.emplace_back(dst, volume);
    }
};

// Used only for cyclesExist()
bool allGoodFrom(int id, std::vector<int> visited, const TaskGraph& taskGraph) noexcept {
    if (std::find(visited.begin(), visited.end(), id) != visited.end()) return false;
    visited.push_back(id);
    const auto& targets = taskGraph.tasks[id].targets;
    for (const auto& [dst, _volume] : targets) {
        if (!allGoodFrom(dst, visited, taskGraph)) return false;
    }
    return true;
}

bool cyclesExist(const TaskGraph& taskGraph, const std::vector<int>& rootTaskIndices) noexcept {
    if (rootTaskIndices.empty()) return true;
    for (int id : rootTaskIndices) {
        if (!allGoodFrom(id, {}, taskGraph)) return true;
    }
    return false;
}
// ============================================================================
// ============================================================================
// ============================================================================
std::optional<TaskGraph> readTaskGraph(std::string_view path) {
    std::ifstream file(path.data());
    char type;
    file >> type;
    if (type != 'V') {
        std::cout << "::> Expected voltage levels amount (V) to be the first entry.\n";
        return std::nullopt;
    }

    unsigned int voltageLevelsAmount;
    file >> voltageLevelsAmount;

    file >> type;
    if (type != 'I') {
        std::cout << "::> Expected indexing specification to be the second entry.\n";
        return std::nullopt;
    }
    file >> type;
    bool indexingFromZero;
    if (type == '0') indexingFromZero = true;
    else if (type == '1') indexingFromZero = false;
    else {
        std::cout << "::> Unexpected indexing specification.\n";
        return std::nullopt;
    }

    TaskGraph taskGraph(indexingFromZero);
    int expectedId = indexingFromZero ? 0 : 1;
    while (file >> type) {
        if (type == 'T') {
            int id;
            std::vector<int> weights(voltageLevelsAmount);
            std::vector<int> energies(voltageLevelsAmount);
            file >> id >> type;
            if (expectedId != id) {
                std::cout << "::> Unexpected indexing while listing Tasks.\n";
                return std::nullopt;
            }
            expectedId++;
            for (unsigned int i = 0; i < voltageLevelsAmount; i++) file >> weights[i];
            file >> type;
            for (unsigned int i = 0; i < voltageLevelsAmount; i++) file >> energies[i];
            taskGraph.add(std::move(weights), std::move(energies));
        } else if (type == 'S') {
            int from, to, volume;
            file >> from >> type >> to >> type >> volume;
            if (!indexingFromZero) { from--; to--; }
            taskGraph.addTransfer(from, to, volume);
        } else {
            std::cout << "::> Unexpected beginning of a line in " << path << ":" << type << '\n';
            exit(-1);
        }
    }

    return { taskGraph };
}
// ============================================================================
// ============================================================================
// ============================================================================
std::vector<int> getRootTasks(const TaskGraph& taskGraph) {
    std::vector<bool> taskIsDestination(taskGraph.tasks.size(), false);
    for (const auto& [_src, dst, _volume] : taskGraph.transfers) taskIsDestination[dst] = true;

    std::vector<int> rootTaskIndices;
    for (unsigned int i = 0; i < taskIsDestination.size(); i++) {
        if (!taskIsDestination[i]) rootTaskIndices.push_back(i);
    }

    return rootTaskIndices;
}

std::optional<int> findTaskToSpeedup(const std::vector<int>& path, const TaskGraph& taskGraph) {
    // Find the first Task with 'INCable' policy
    for (int id : path) {
        if (taskGraph.tasks[id].policy > 0) return { id };
    }
    return std::nullopt;
}

int recalculateStatsFrom(int id, int parentCumulativeWeight, TaskGraph& taskGraph) {
    auto& task = taskGraph.tasks[id];

    // Set Early
    if (!task.early || (*task.early < parentCumulativeWeight)) task.early = { parentCumulativeWeight };

    // Set Late
    int min = 0; // find max cumulative time, but treat as min because they are stored negative
    for (const auto& [i, _] : task.targets) {
        const int targetLateTime = recalculateStatsFrom(i, *task.early + task.weight(), taskGraph);
        if (targetLateTime < min) min = targetLateTime;
    }
    const int maybeNewLate = min - task.weight();
    if (!task.late || (*task.late > maybeNewLate)) task.late = { maybeNewLate };

    return *task.late;
}

std::pair<std::vector<int>, int> recalculateStats(TaskGraph& taskGraph, const std::vector<int>& rootTaskIndices) {
    for (auto& task : taskGraph.tasks) task.clearStats();

    int criticalTime = 0; // they are stored negative
    int criticalPathRoot;
    for (int id : rootTaskIndices) {
        const int maybeCriticalTime = recalculateStatsFrom(id, 0, taskGraph);
        if (maybeCriticalTime < criticalTime) {
            criticalTime = maybeCriticalTime;
            criticalPathRoot = id;
        }
    }

    std::vector<int> criticalPath;
    int currId = criticalPathRoot;
    while (!taskGraph.tasks[currId].targets.empty()) {
        const auto& curr = taskGraph.tasks[currId];
        criticalPath.push_back(currId);
        const int expectedTargetLate = *curr.late + curr.weight();
        for (const auto& [id, _] : curr.targets) {
            if (*taskGraph.tasks[id].late == expectedTargetLate) {
                currId = id;
                break;
            }
            std::cout << "::> Logic error in recalculateStats().\n";
            exit(-1);
        }
    }
    criticalPath.push_back(currId);

    return std::make_pair(criticalPath, -criticalTime);
}
// ============================================================================
// ============================================================================
// ============================================================================
int main() {
    const int DESIRED_TIME = 11;

    const auto taskGraphOpt = readTaskGraph("taskGraph.txt");
    if (!taskGraphOpt) return -1;
    TaskGraph taskGraph = *taskGraphOpt;
    const std::vector<int> rootTaskIndices = getRootTasks(taskGraph);

    if (cyclesExist(taskGraph, rootTaskIndices)) {
        std::cout << "::> Cycles detected in tasks graph" << '\n';
        return -1;
    }
    if (taskGraph.tasks.empty()) {
        std::cout << "Nothing to do...\n";
        return -1;
    }

    std::cout << "===============================================" << '\n';
    const auto PROCESSORS_COUNT = 3;
    std::vector<Processor> processors(PROCESSORS_COUNT);

    // Start by specifying the slowest(last) policy for each Task.
    const auto POLICIES_COUNT = taskGraph.tasks.front().weights.size();
    for (auto& task : taskGraph.tasks) task.policy = POLICIES_COUNT - 1;

    auto [criticalPath, criticalTime] = recalculateStats(taskGraph, rootTaskIndices);
    while (criticalTime > DESIRED_TIME) {
        const auto taskToSpeedupOpt = findTaskToSpeedup(criticalPath, taskGraph);
        if (!taskToSpeedupOpt) {
            std::cout << ":> The critical path on best performance does not meet the desired time.\n";
            return 0;
        }
        taskGraph.tasks[*taskToSpeedupOpt].policy--; // improve performance of this Task
        const auto [newCriticalPath, newCriticalTime] = recalculateStats(taskGraph, rootTaskIndices);
        criticalPath = std::move(newCriticalPath);
        criticalTime = newCriticalTime;
    }


    return 0;
}
