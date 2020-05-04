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
    int start, finish, taskId;
    inline ProcessingEvent(int start, int finish, int taskId) noexcept
        : start(start), finish(finish), taskId(taskId) {}
    // int duration() const noexcept { return start + duration; }
};


struct Processor {
    std::vector<ProcessingEvent> processingTimeline;
    std::vector<TransferEvent> transferTimeline;

    int availableAt(int duration, int let) const noexcept {
        bool conflict = true;
        while (conflict) {
            conflict = false;
            for (const auto& [start, finish, _] : processingTimeline) {
                if (finish <= let || let + duration <= start) continue;
                conflict = true;
                let = finish;
                break;
            }
        }

        return let;
    }
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
    std::vector<int> parents;

    int policy = 0;
    std::optional<int> early = std::nullopt;
    std::optional<int> late = std::nullopt;

    int delta() const noexcept { return *late - *early; }
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
        tasks[dst].parents.emplace_back(src);
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

    // int i = 0;
    // for (const auto& task : taskGraph.tasks) {
    //     std::cout << i << ": E=" << *task.early << " L=" << *task.late << '\n';
    //     i++;
    // }

    std::vector<int> criticalPath;
    int currId = criticalPathRoot;
    while (!taskGraph.tasks[currId].targets.empty()) {
        // std::cout << "For id " << currId << '\n';
        const auto& curr = taskGraph.tasks[currId];
        criticalPath.push_back(currId);
        const int expectedTargetLate = *curr.late + curr.weight();
        // std::cout << "Looking for " << expectedTargetLate << '\n';
        bool found = false; // TODO: assert remove
        for (const auto& [id, _] : curr.targets) {
            // std::cout << "Have " << *taskGraph.tasks[id].late << '\n';
            if (*taskGraph.tasks[id].late == expectedTargetLate) {
                currId = id;
                found = true;
                break;
            }
        }
        if (!found) {
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
std::vector<Processor> planning(const TaskGraph& taskGraph, const std::vector<int>& rootTasks, int CORES_COUNT) {
    std::vector<int> readyTasks = rootTasks;
    std::vector<int> doneTasks;
    std::vector<Processor> processors(CORES_COUNT);
    // <core, finish time>
    std::vector<std::pair<unsigned int, int>> assignmentOf(taskGraph.tasks.size(), std::make_pair(-1, -1));

    // We've found the most urgent Task among the ready ones
    const auto determineAssignmentCore = [&processors, &taskGraph, &assignmentOf](int taskId){
        int bestTime = -1;
        int bestCore;
        for (unsigned int core = 0; core < processors.size(); core++) {
            int dataReadyAt = 0;
            for (int parent : taskGraph.tasks[taskId].parents) {
                if (assignmentOf[parent].first == core) continue;
                const int parentFinishedAt = assignmentOf[parent].second;
                const auto& parentTargets = taskGraph.tasks[parent].targets;
                int transferTime;
                for (const auto& [dst, volume] : parentTargets) {
                    if (dst == taskId) { transferTime = volume; break; }
                }
                const int newDataReadyAt = parentFinishedAt + transferTime;
                if (newDataReadyAt > dataReadyAt) dataReadyAt = newDataReadyAt;
            }

            const int weight = taskGraph.tasks[taskId].weight();
            const int canStartAt = processors[core].availableAt(weight, dataReadyAt);

            if (bestTime == -1 || canStartAt < bestTime) {
                bestTime = canStartAt;
                bestCore = core;
            }
        }
        return std::make_pair(bestCore, bestTime);
    };

    while (!readyTasks.empty()) {
        // Find most urgent Task (min delta = Late - Early)
        int taskToAssign;
        int min = 0;
        for (int i : readyTasks) {
            if (taskGraph.tasks[i].delta() < min) {
                min = taskGraph.tasks[i].delta();
                taskToAssign = i;
            }
        }

        std::cout << "Shall assign " << taskToAssign << " with delta = " << taskGraph.tasks[taskToAssign].delta() << '\n';

        // Assign
        const auto [core, startTime] = determineAssignmentCore(taskToAssign);
        const int finishTime = startTime + taskGraph.tasks[taskToAssign].weight();
        assignmentOf[taskToAssign] = std::make_pair(core, finishTime);
        processors[core].processingTimeline.emplace_back(startTime, finishTime, taskToAssign);

        // Move to doneTasks
        auto it = std::find(readyTasks.begin(), readyTasks.end(), taskToAssign);
        readyTasks.erase(it);
        doneTasks.push_back(taskToAssign);

        // Find new ready Tasks
        for (const auto& [id, _] : taskGraph.tasks[taskToAssign].targets) {
            bool ready = true;
            for (int parent : taskGraph.tasks[id].parents) {
                if (std::find(doneTasks.begin(), doneTasks.end(), parent) == doneTasks.end()) {
                    ready = false;
                    break;
                }
            }

            if (ready) readyTasks.push_back(id);
        }
    }

    return processors;
}
// ============================================================================
// ============================================================================
// ============================================================================
int main() {
    const int DESIRED_TIME = 12;

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

    // Start by setting the slowest(last) policy for each Task.
    const auto POLICIES_COUNT = taskGraph.tasks.front().weights.size();
    for (auto& task : taskGraph.tasks) task.policy = POLICIES_COUNT - 1;

    auto [criticalPath, criticalTime] = recalculateStats(taskGraph, rootTaskIndices);

    std::cout << "Got CT=" << criticalTime << " for ";
    for (int i : criticalPath) std::cout << i << ",";
    std::cout << '\n';

    while (criticalTime > DESIRED_TIME) {
        const auto taskToSpeedupOpt = findTaskToSpeedup(criticalPath, taskGraph);
        if (!taskToSpeedupOpt) {
            std::cout << ":> The critical path on best performance does not meet the desired time.\n";
            return 0;
        }
        std::cout << "Incing " << *taskToSpeedupOpt << '\n';
        taskGraph.tasks[*taskToSpeedupOpt].policy--; // improve performance of this Task
        const auto [newCriticalPath, newCriticalTime] = recalculateStats(taskGraph, rootTaskIndices);
        criticalPath = std::move(newCriticalPath);
        criticalTime = newCriticalTime;

        std::cout << "Got CT=" << criticalTime << " for ";
        for (int i : criticalPath) std::cout << i << ",";
        std::cout << '\n';
    }

    const auto CORES_COUNT = 3;
    const auto cores = planning(taskGraph, rootTaskIndices, CORES_COUNT);

    int coreId = 0;
    for (const auto& processor : cores) {
        std::cout << "==== Core " << coreId++ << '\n';
        for (const auto& [start, finish, taskId] : processor.processingTimeline) {
            std::cout << "[" << taskId << "]: " << start << "--" << finish << '\n';
        }
    }

    return 0;
}
