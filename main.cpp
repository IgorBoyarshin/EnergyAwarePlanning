#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <string_view>

// For drawing
#include <string>
#include <utility>
#include <optional>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>


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

    int finishedAt() const noexcept {
        int max = 0;
        for (const auto& [_start, finish, _id] : processingTimeline) {
            if (finish > max) max = finish;
        }
        return max;
    }

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

    bool canImprove() const noexcept { return policy > 0; }
    int delta() const noexcept { return *late - *early; }
    int weight() const noexcept { return weights[policy]; }
    int energy() const noexcept { return energies[policy]; }
    void clearStats() noexcept { early = std::nullopt; late = std::nullopt; }
    int volumeOfTargetTo(int id) const noexcept {
        for (const auto& [dst, volume] : targets) {
            if (dst == id) return volume;
        }
        return -1;
    }

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

    int i = 0;
    for (const auto& task : taskGraph.tasks) {
        std::cout << i << ": E=" << *task.early << " L=" << *task.late << '\n';
        i++;
    }

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
struct PlanningStuff {
    std::vector<Processor> processors;
    // <core, finish time>
    std::vector<std::pair<unsigned int, int>> assignmentOf;

    PlanningStuff() noexcept {}
    PlanningStuff(std::vector<Processor>&& processors, std::vector<std::pair<unsigned int, int>>&& assignmentOf)
        : processors(std::move(processors)), assignmentOf(std::move(assignmentOf)) {}
};

PlanningStuff planning(const TaskGraph& taskGraph, const std::vector<int>& rootTasks, int CORES_COUNT) {
    std::vector<int> readyTasks = rootTasks;
    std::vector<int> doneTasks;
    std::vector<Processor> processors(CORES_COUNT);
    // <core, finish time>
    std::vector<std::pair<unsigned int, int>> assignmentOf(taskGraph.tasks.size(), std::make_pair(-1, -1));

    // We've found the most urgent Task among the ready ones
    const auto determineAssignmentCore = [&processors, &taskGraph, &assignmentOf](int taskId){
        int bestTime = -1;
        unsigned int bestCore;
        for (unsigned int core = 0; core < processors.size(); core++) {
            int dataReadyAt = 0;
            for (int parent : taskGraph.tasks[taskId].parents) {
                if (assignmentOf[parent].first == core) continue;
                const int parentFinishedAt = assignmentOf[parent].second;
                const int transferTime = taskGraph.tasks[parent].volumeOfTargetTo(taskId);
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

        std::cout << "Shall assign " << taskToAssign
            << " with delta = " << taskGraph.tasks[taskToAssign].delta() << '\n';

        // Assign
        const auto [core, startTime] = determineAssignmentCore(taskToAssign);
        const int finishTime = startTime + taskGraph.tasks[taskToAssign].weight();
        assignmentOf[taskToAssign] = std::make_pair(core, finishTime);
        processors[core].processingTimeline.emplace_back(startTime, finishTime, taskToAssign);
        for (int parent : taskGraph.tasks[taskToAssign].parents) {
            const auto [parentCore, parentFinish] = assignmentOf[parent];
            if (core != parentCore) {
                const int duration = taskGraph.tasks[parent].volumeOfTargetTo(taskToAssign);
                processors[parentCore].transferTimeline.emplace_back(parentFinish, duration, parent, taskToAssign);
            }
        }

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

    return { std::move(processors), std::move(assignmentOf) };
}

std::vector<int> findEarliestToImproveFrom(int taskId, const TaskGraph& taskGraph,
        const std::vector<std::pair<unsigned int, int>>& assignmentOf) { // <core, finish time>
    const auto& task = taskGraph.tasks[taskId];
    const auto [selfCore, selfFinish] = assignmentOf[taskId];
    const int selfStart = selfFinish - task.weight();

    std::vector<int> idsToSpeedup;
    for (int parent : task.parents) {
        const auto [parentCore, parentFinish] = assignmentOf[parent];
        const int transferTime = (selfCore == parentCore) ? 0 : taskGraph.tasks[parent].volumeOfTargetTo(taskId);
        const int couldStartAt = parentFinish + transferTime;
        if (couldStartAt == selfStart) { // parent potentially held us up
            std::cout << "Task " << parent << "(parent of " << taskId << ") maybe held us up.\n";
            const auto parentSuggestion = findEarliestToImproveFrom(parent, taskGraph, assignmentOf);
            idsToSpeedup.insert(idsToSpeedup.end(), parentSuggestion.begin(), parentSuggestion.end());
        }
    }
    if (idsToSpeedup.empty()) {
        // TODO: remove
        std::cout << "Parents of " << taskId << " had no suggestions.\n";
    }
    if (idsToSpeedup.empty() && task.canImprove()) idsToSpeedup.push_back(taskId);
    return idsToSpeedup;
}
// ============================================================================
// ============================================================================
// ============================================================================
const int SCREEN_WIDTH = 640;
const int SCREEN_HEIGHT = 480;


SDL_Window*   gWindow   = nullptr; // The window we'll be rendering to
SDL_Renderer* gRenderer = nullptr; // The window renderer


// ========== Data types ======== //

struct Transmission {
    unsigned int begin_at;
    unsigned int finish_at;
    unsigned int proc_dest;

    Transmission(unsigned int begin_at, unsigned int finish_at, unsigned int proc_dest)
        : begin_at(begin_at), finish_at(finish_at), proc_dest(proc_dest) {}

    friend std::ostream& operator<<(std::ostream& os, const Transmission& transmission);
};


struct Subtask {
    unsigned int proc_num;
    std::string name;
    unsigned int begin_at;
    unsigned int finish_at;
    std::vector<Transmission> transmissions;

    Subtask(unsigned int proc_num, const std::string& name,
            unsigned int begin_at, unsigned int finish_at,
            const std::vector<Transmission>& transmissions) :
        proc_num(proc_num), name(name), begin_at(begin_at),
        finish_at(finish_at), transmissions(transmissions) {}

    friend std::ostream& operator<<(std::ostream& os, const Subtask& subtask);
};


struct DrawingBasics {
    std::pair<unsigned int, unsigned int> units;
    std::vector<int> trans_count;

    DrawingBasics(const std::pair<unsigned int, unsigned int>& units,
            const std::vector<int>& trans_count) :
        units(units), trans_count(trans_count) {}
};


struct DrawingElement {
    SDL_Rect rectangle;
    SDL_Surface*  surface;
    SDL_Texture* texture;
    SDL_Color color;

    DrawingElement(const SDL_Rect& rectangle, SDL_Surface*  surface,
            SDL_Texture* texture, const SDL_Color& color) :
        rectangle(rectangle), surface(surface), texture(texture), color(color) {}
};


std::ostream& operator<<(std::ostream& os, const Transmission& trans) {
    os << " T(b: " << trans.begin_at << ", f: " << trans.finish_at << ", dest: " << trans.proc_dest << ")";
    return os;
}


std::ostream& operator<<(std::ostream& os, const Subtask& subtask) {
    os << "Subtask(proc: " << subtask.proc_num << ", name: " << subtask.name << ", b: " << subtask.begin_at
        << ", f: " << subtask.finish_at << ", transmissions:";

    if (subtask.transmissions.empty()) {
        os << " none";
    } else {
        for (const auto& transmission : subtask.transmissions) {
            os << transmission << "";
        }
    }
    os << ")";

    return os;
}
// =========================================================


// ==================== Methods for SDL ==================== //

bool init() {

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cout << "SDL could not initialize! SDL Error: " << SDL_GetError() << std::endl;
        return false;
    }

    if (TTF_Init() == -1) {
        std::cout << "SDL_ttf could not initialize! SDL Error: " << TTF_GetError() << std::endl;
        return false;
    }

    // Set texture filtering to linear
    if (!SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1")) {
        std::cout << "Warning: Linear texture filtering not enabled!" << std::endl;
    }

    // Create window
    gWindow = SDL_CreateWindow("Little SDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (gWindow == nullptr) {
        std::cout << "Window could not be created! SDL Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create renderer for window
    gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED);
    if (gRenderer == nullptr) {
        std::cout << "Renderer could not be created! SDL Error: " << SDL_GetError() << std::endl;
        return false;
    }

    //Initialize renderer color
    SDL_SetRenderDrawColor(gRenderer, 0xFF, 0xFF, 0xFF, 0xFF);


    return true;
}


void close() {
    // Destroy window
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(gWindow);
    gWindow = nullptr;
    gRenderer = nullptr;

    // Quit SDL subsystems
    SDL_Quit();
}


DrawingBasics getDrawingBasics(const std::vector<Subtask>& subtasks) {
    // calculation of x_unit
    unsigned int max_x = 0;
    unsigned int max_proc_num = 0;
    for (const auto& subtask : subtasks) {
        unsigned int curr_max_time = subtask.finish_at;
        unsigned int curr_proc_num = subtask.proc_num;
        if (!subtask.transmissions.empty()) {
            curr_max_time = subtask.transmissions.back().finish_at;
        }
        if (curr_max_time > max_x) max_x = curr_max_time;
        if (curr_proc_num > max_proc_num) max_proc_num = curr_proc_num;
    }

    std::vector<int> trans_count_max(max_proc_num + 1, -1);
    for (const auto& subtask : subtasks) {
        const unsigned int curr_proc_num = subtask.proc_num;
        const int curr_trans_size = subtask.transmissions.size();
        if (curr_trans_size > trans_count_max[curr_proc_num])
            trans_count_max[curr_proc_num] = curr_trans_size;
    }

    // calculation of y_unit
    unsigned int sum_y = 0;
    for (const auto& trans_count : trans_count_max) {
        if (trans_count != -1) {
            sum_y += trans_count + 2; // + 2 = 1 * 2 for the Subtask itself (weight == 2)
        }
    }


    return DrawingBasics({SCREEN_WIDTH / max_x, SCREEN_HEIGHT / sum_y},
            trans_count_max);
}


void drawGraph(const std::vector<Subtask>& subtasks) {
    const auto& [units, trans_count] = getDrawingBasics(subtasks);
    const auto& [x_unit, y_unit] = units;
    std::cout << "(x_unit = " << x_unit << ", y_unit = " << y_unit << ")" << std::endl;

    if (!init()) {
        std::cout << "Failed to initialize!" << std::endl;
        return;
    }

    // TTF_Font* font = TTF_OpenFont("/usr/share/fonts/truetype/freefont/FreeMono.ttf", 200);
    TTF_Font* font = TTF_OpenFont("DejaVuSans-Bold.ttf", 200);
    if (font == nullptr) {
        std::cout << "Unable to open font" << std::endl;
        return;
    }

    // Prepare stuff to draw
    const SDL_Color subtask_color = {255, 0, 0, 0};
    const SDL_Color transmission_color = {0, 255, 0, 0};
    std::vector<DrawingElement> drawing_elements;
    std::vector<unsigned int> core_separators;

    for (const auto& subtask : subtasks) {
        // TODO: add margin
        const auto calculate_begin = [x_unit](const auto& elem) {
            return elem.begin_at * x_unit;
            // return margin + elem.begin_at * x_unit;
        };

        const auto calculate_width = [x_unit](const auto& elem) {
            return (elem.finish_at - elem.begin_at) * x_unit;
            // return margin + elem.finish_at * x_unit;
        };

        // unsigned int y = margin + elems_before * y_unit;
        const unsigned int elems_before = [&](){
            unsigned int count = 0;
            for (unsigned int i = 0; i < subtask.proc_num; i++) {
                if (trans_count[i] != -1) {
                    count += trans_count[i] + 2; // + 2 = 1 * 2 for the Subtask itself (weight == 2)
                }
            }
            return count;
        }();
        const int x = calculate_begin(subtask);
        const int y = elems_before * y_unit;
        const int width = calculate_width(subtask);
        const int height = y_unit * 2;
        const SDL_Rect rect{x, y, width, height};

        SDL_Surface*  surface = TTF_RenderText_Solid(font, subtask.name.c_str(), subtask_color);
        SDL_Texture* texture = SDL_CreateTextureFromSurface(gRenderer, surface);
        drawing_elements.emplace_back(std::move(rect), surface, texture, subtask_color);

        const auto& curr_transmissions = subtask.transmissions;
        for (unsigned int index = 0; index < curr_transmissions.size(); index++) {
            const auto& curr_trans = curr_transmissions[index];
            const int x = calculate_begin(curr_trans);
            const int y = (elems_before + 2 + index) * y_unit; // + 2 = 1 * 2 for the Subtask itself (weight == 2)
            const int width = calculate_width(curr_trans);
            const int height = y_unit;
            const SDL_Rect rect{x, y, width, height};

            const std::string to_proc = std::string{"To "} + std::to_string(curr_trans.proc_dest) + " : " + subtask.name;
            SDL_Surface*  surface = TTF_RenderText_Solid(font, to_proc.c_str(), transmission_color);
            SDL_Texture* texture = SDL_CreateTextureFromSurface(gRenderer, surface);
            drawing_elements.emplace_back(std::move(rect), surface, texture, transmission_color);
        }
        core_separators.push_back((elems_before + 2 + curr_transmissions.size()) * y_unit);
    }

    // Draw stuff
    bool quit = false;
    SDL_Event e;

    while (!quit) {
        //Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            if ((e.type == SDL_QUIT) || (e.key.keysym.sym == SDLK_q)) {
                quit = true;
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(gRenderer, 0xFF, 0xFF, 0xFF, 0xFF);
        SDL_RenderClear(gRenderer);

        // Draw a grid
        SDL_SetRenderDrawColor(gRenderer, 0xC0, 0xC0, 0xC0, 0xFF);
        for (unsigned int i = 0; i < SCREEN_HEIGHT; i += y_unit) {
            SDL_RenderDrawLine(gRenderer, 0, i, SCREEN_WIDTH, i);
        }
        for (unsigned int i = 0; i < SCREEN_WIDTH; i += x_unit) {
            SDL_RenderDrawLine(gRenderer, i, 0, i, SCREEN_HEIGHT);
        }
        SDL_SetRenderDrawColor(gRenderer, 0x00, 0x00, 0xF0, 0xFF);
        for (unsigned int separator : core_separators) {
            for (int line = -2; line <= 2; line++) {
                SDL_RenderDrawLine(gRenderer, 0, separator + line, SCREEN_WIDTH, separator + line);
            }
        }

        if (!drawing_elements.empty()) {
            for (const DrawingElement element : drawing_elements) {
                SDL_SetRenderDrawColor(gRenderer, 0xFF, 0x00, 0x00, 0xFF);
                SDL_RenderDrawRect(gRenderer, &element.rectangle);
                SDL_RenderCopy(gRenderer, element.texture, nullptr, &element.rectangle);
            }
        }
        // Update screen
        SDL_RenderPresent(gRenderer);
    }

    for (const DrawingElement element : drawing_elements) {
        SDL_DestroyTexture(element.texture);
        SDL_FreeSurface(element.surface);
    }
    TTF_CloseFont(font);
    TTF_Quit();

    close();
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


    PlanningStuff planningStuff;
    while (true) {
        const auto CORES_COUNT = 3;
        planningStuff = planning(taskGraph, rootTaskIndices, CORES_COUNT);
        const auto& [cores, assignmentOf] = planningStuff;

        // Display planning
        int coreId = 0;
        for (const auto& processor : cores) {
            std::cout << "==== Core " << coreId++ << '\n';
            for (const auto& [start, finish, taskId] : processor.processingTimeline) {
                std::cout << "[" << taskId << "]: " << start << "--" << finish << '\n';
            }
            for (const auto& [start, duration, src, dst] : processor.transferTimeline) {
                std::cout << "From " << src << " to " << dst << " : " << start << "--" << start+duration << '\n';
            }
        }

        int totalTime = 0;
        for (const auto& processor : cores) {
            const int finish = processor.finishedAt();
            if (finish > totalTime) totalTime = finish;
        }
        std::cout << "Total time = " << totalTime << '\n';
        if (totalTime <= DESIRED_TIME) {
            std::cout << "The planning is sufficient.\n";
            break;
        }

        // Find earliest of late finish time
        int earliestTime = -1;
        unsigned int earliestId;
        for (unsigned int taskId = 0; taskId < taskGraph.tasks.size(); taskId++) {
            const auto& task = taskGraph.tasks[taskId];
            const auto startTime = assignmentOf[taskId].second - task.weight();
            const int late = DESIRED_TIME + *task.late;
            if (startTime > late) { // started late
                if (earliestTime == -1 || earliestTime > startTime) {
                    earliestTime = startTime;
                    earliestId = taskId;
                }
            }
        }
        std::cout << "Earliest of late task is " << earliestId << ": ";
        std::cout << "It should have started by "
            << (DESIRED_TIME + *taskGraph.tasks[earliestId].late)
            << " but started at " << earliestTime << ". Shall improve" << '\n';

        const auto suggestedImprovements = findEarliestToImproveFrom(earliestId, taskGraph, assignmentOf);
        if (suggestedImprovements.empty()) {
            std::cout << "There is nothing to be done..." << '\n';
            break;
        }

        std::cout << "Suggestions:" << '\n';
        for (int s : suggestedImprovements) std::cout << s << ",";
        std::cout << '\n';
        std::cout << "Applying suggestions:\n";
        for (int s : suggestedImprovements) {
            std::cout << "Incing " << s << '\n';
            taskGraph.tasks[s].policy--; // improve performance of this Task
        }
        recalculateStats(taskGraph, rootTaskIndices);
    }

    // Prep staff for drawing
    std::vector<Subtask> subtasks;
    int processorIndex = 0;
    for (const auto& [processingTimeline, transferTimeline] : planningStuff.processors) {
        for (const auto& [start, finish, taskId] : processingTimeline) {
            std::vector<Transmission> transmissions;
            for (const auto& [start, duration, src, dst] : transferTimeline) {
                if (src == taskId) {
                    const int destCore = planningStuff.assignmentOf[dst].first;
                    transmissions.emplace_back(start, start + duration, destCore);
                }
            }
            subtasks.emplace_back(processorIndex, std::to_string(taskId), start, finish, std::move(transmissions));
        }
        processorIndex++;
    }

    drawGraph(subtasks);

    return 0;
}
