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

    Task(std::vector<int>&& weights, std::vector<int>&& energies) noexcept
        : weights(std::move(weights)), energies(std::move(energies)) {}
};

struct TaskGraph {
    std::vector<Task> tasks;
    std::vector<Transfer> transfers; // redundant. for convenience

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
    TaskGraph taskGraph;
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

void calculateWeightFrom(int srcIndex, int selfIndex, int cumulativeWeight,
        std::vector<std::pair<int, int>>& weightFor, const TaskGraph& taskGraph) {
    const auto& selfTask = taskGraph.tasks[selfIndex];
    const int selfWeight = selfTask.weights[selfTask.policy];
    const int newWeight = cumulativeWeight + selfWeight;
    bool mustContinue = weightFor[selfIndex].first == -1; // not initialized
    if (weightFor[selfIndex].first < newWeight) {
        weightFor[selfIndex].first = newWeight;
        weightFor[selfIndex].second = srcIndex;
        mustContinue = true; // to recalculate from here on
    }
    if (!mustContinue) return;
    const int resultingWeight = weightFor[selfIndex].first;
    for (const auto& [dstIndex, _] : selfTask.targets) {
        calculateWeightFrom(selfIndex, dstIndex, resultingWeight, weightFor, taskGraph);
    }
}


std::pair<std::vector<int>, int> findReversedCriticalPath(const TaskGraph& taskGraph,
        const std::vector<int>& rootTasks) noexcept {
    // <cumulative sum, from where>
    std::vector<std::pair<int, int>> weightFor(taskGraph.tasks.size(), std::make_pair(-1, -1));
    for (int i : rootTasks) calculateWeightFrom(-1, i, 0, weightFor, taskGraph);

    // Find max cumulative weight
    int maxIndex = -1;
    int max = -1;
    for (unsigned int i = 0; i < weightFor.size(); i++) {
        const auto& [w, _] = weightFor[i];
        if (w > max) {
            max = w;
            maxIndex = i;
        }
    }

    std::vector<int> criticalPath;
    int curr = maxIndex;
    while (curr != -1) {
        criticalPath.push_back(curr);
        curr = weightFor[curr].second;
    }

    return std::make_pair(std::move(criticalPath), weightFor[maxIndex].first);
}
// ============================================================================
// ============================================================================
// ============================================================================
int main() {
    const auto taskGraphOpt = readTaskGraph("taskGraph.txt");
    if (!taskGraphOpt) return -1;
    TaskGraph taskGraph = *taskGraphOpt;
    const std::vector<int> rootTasks = getRootTasks(taskGraph);

    if (cyclesExist(taskGraph, rootTasks)) {
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

    const auto cp = findReversedCriticalPath(taskGraph, rootTasks);
    std::cout << "CP=" << cp.second << '\n';
    for (int i : cp.first) {
        std::cout << i << '\n';
    }

    return 0;
}
